#include "session.h"
#include "sessionRegistry.h"

#include "backend/moduleManager/moduleManager.h"
#include "backend/metadataConfiguration.h"
#include "backend/compiler/procUnit.h"
#include "backend/compiler/procUnitState.h"
#include "backend/databaseLayer/databaseLayer.h"
#include "backend/databaseLayer/connectionPool.h"
#include "backend/appData.h"
#include "workerPool.h"

#include <utility>
#include <chrono>
#include <shared_mutex>
#include <thread>
#include <unordered_map>
#include <algorithm>
#include <functional>

// RLS — the concrete access policy lives here (session side); the L3 door sees
// only the ibAccessPolicy interface.
#include "backend/query/dataQueryBuilder.h"          // ibAccessPolicy / ibDataQueryBuilder
#include "backend/query/queryable.h"                 // ibBackendQueryable — GetMetaData / GetQueryName / GetPrimaryKeyColumns
#include "backend/system/value/valueQueryable.h"     // ibValueQueryable — a role-module restriction returned as a set
#include "backend/metaCollection/metaRoleObject.h"   // ibValueMetaObjectRole — GetRoleModule()
#include "backend/metaCollection/genericData.h"      // AccessRight_Show / _Modify / _Erase — the rights, as the metadata already answers them
#include "backend/backend_exception.h"               // ibBackendAccessException

namespace {

// ---------------------------------------------------------------------------
// ibRuntimeAccessPolicy — the SESSION-side concrete RLS policy the L3 door
// consults opaquely. For a metadata-backed source it walks the current user's
// role modules and lets each AUGMENT the query: the module's handler runs at
// POLICY time (breakpointable). First slice — the handler returns a Boolean
// gate (False DENIES → throw; absent / non-False = allow). Next slice — it
// receives a query handle and ADDS a restricting semi-join so the query returns
// only the rows the role's keys permit (the row filter, on the SUBD side).
//
// ---------------------------------------------------------------------------
// WHICH right answers for a stage — a pointer to the metaobject predicate that already knows how
// to answer it (IsFullAccess, the roles, the object's own mapping are all inside it). The policy
// names the right; it does not re-implement it.
typedef bool (ibValueMetaObjectGenericData::*RightPredicate)() const;

class ibRuntimeAccessPolicy : public ibAccessPolicy
{
public:
	ibRuntimeAccessPolicy(ibSession* session, const ibMetaData* metaData)
		: m_session(session), m_metaData(metaData)
	{
		// Resolve the current user's role-module procUnits ONCE, right here. The policy is built in
		// CompileRoot right AFTER AttachRuntime, so the role modules are attached (as common modules) and
		// their procUnits are LIVE — yet no query has fired yet, so the policy is in place before anything
		// it must guard. Roles are fixed while the session lives, so this vector is valid for the whole
		// life; re-resolving it per query would only cost (hot path) and risk a desync. Empty = no
		// restricting role (default-allow); a role with no module adds no restriction.
		ibValueModuleManagerRuntimeConfiguration* mm = m_session != nullptr ? m_session->GetManagerModule() : nullptr;
		if (mm == nullptr || m_metaData == nullptr)
			return;
		for (const ibUserInfo::ibUserRole& userRole : appData->GetUserRoleArray()) {
			const ibValueMetaObjectRole* role =
				m_metaData->FindAnyObjectByFilter<ibValueMetaObjectRole>(userRole.m_miRoleId);
			if (role == nullptr)
				continue;
			const ibValueMetaObjectManagerModule* roleModule = role->GetRoleModule();
			if (roleModule == nullptr)
				continue;                                 // no RLS module on this role
			ibValueModuleManager::ibValueModuleUnit* unit = mm->FindCommonModule(roleModule);
			if (unit == nullptr)
				continue;
			if (const std::shared_ptr<ibProcUnit> proc = unit->GetProcUnit())
				m_roleProcs.push_back(proc);
		}
	}

	// ONE METHOD PER OPERATION; the stage picks which of the two questions. Creating asks the Write
	// right — creating IS writing — and stays its own method because its restriction checks the row
	// being made rather than filtering rows that already exist.
	bool CheckSelect(ibDataQueryBuilder& query, const ibAccessStage& stage, long affected) const override
	{ return Check(query, &ibValueMetaObjectGenericData::AccessRight_Show, wxT("Read"), stage, affected); }

	bool CheckCreate(ibDataQueryBuilder& query, const ibAccessStage& stage, long affected) const override
	{ return Check(query, &ibValueMetaObjectGenericData::AccessRight_Modify, wxT("Create"), stage, affected); }

	bool CheckUpdate(ibDataQueryBuilder& query, const ibAccessStage& stage, long affected) const override
	{ return Check(query, &ibValueMetaObjectGenericData::AccessRight_Modify, wxT("Write"), stage, affected); }

	bool CheckDelete(ibDataQueryBuilder& query, const ibAccessStage& stage, long affected) const override
	{ return Check(query, &ibValueMetaObjectGenericData::AccessRight_Erase, wxT("Delete"), stage, affected); }

private:
	// What the four share: the stage decides which question, the action decides which right answers.
	bool Check(ibDataQueryBuilder& query, RightPredicate right, const wxString& operation,
	           const ibAccessStage& stage, long affected) const
	{
		return stage == ibAccessStage::Table
			? Gate(query, right, operation)
			: Verdict(query, affected);
	}



	// May every table this query touches be touched this way? The right itself answers — the
	// metaobject predicate already folds in full access and the roles behind it — and the first one
	// that refuses ends the walk: the query is not going to run either way.
	//
	// Not cached. It is a hot path and the answer is a session constant, so caching is tempting, but
	// nothing here has been measured yet and a cache brings a staleness question with it. The place
	// to put one is exactly here, when there is a number saying it is needed.
	bool TablesAllowed(const ibDataQueryBuilder& query, RightPredicate right) const
	{
		bool allowed = true;
		Unwind(query, [&](const ibBackendQueryable*, const ibValueMetaObjectGenericData* srcMeta) {
			if ((srcMeta->*right)())
				return true;
			allowed = false;
			return false;                                 // the first table that refuses ends the walk
		});
		return allowed;
	}

	// THE UNWINDER — the one procedure that turns a query into the tables it actually touches, and
	// the only walk in this class. Both sides of every Join, every branch of every Union (that is
	// what GetSources does), each visited ONCE even when joined twice under two aliases, and each
	// handed over as what carries its RIGHTS. Sources with nothing behind them — temp, computed,
	// subquery — are skipped, exactly as the row filter skips them: no rights to ask of.
	//
	// All four operations and both stages go through here. Nobody else walks sources.
	// `visit` returns false to STOP: the first table that refuses ends the walk. There is nothing to
	// learn from the rest — the query is not going to run either way, and the user gets the error
	// and fixes it.
	void Unwind(const ibDataQueryBuilder& query,
	            const std::function<bool(const ibBackendQueryable*,
	                                     const ibValueMetaObjectGenericData*)>& visit) const
	{
		std::vector<const ibBackendQueryable*> sources;
		query.GetSources(sources);
		std::vector<ibMetaID> seen;
		for (const ibBackendQueryable* source : sources) {
			const ibValueMetaObjectGenericData* srcMeta = FindSourceMetaObject(source);
			if (srcMeta == nullptr)
				continue;
			const ibMetaID tableId = source->GetQueryTableId();
			if (std::find(seen.begin(), seen.end(), tableId) != seen.end())
				continue;
			seen.push_back(tableId);
			if (!visit(source, srcMeta))
				return;
		}
	}

	// AFTER the statement — the only question a result can answer: did the write happen? Zero rows
	// means one thing on a ROW-controlled source and another on a table-controlled one, and which
	// it is the OBJECT says (ibAccessObject::IsAccessPerRecord — row by default, table for a register):
	//
	//   row-controlled (a record) — the row exists as itself, so nothing written means the folded
	//                               filter kept the statement off a row that is there: REFUSED.
	//   table-controlled (a register) — its set is addressed by its recorder and may legitimately be
	//                               empty; the count carries no verdict, so it passes.
	//
	// A non-zero count passed; a negative one is the write having thrown — a database failure, not
	// an access decision, and the caller's own error path speaks for it.
	bool Verdict(const ibDataQueryBuilder& query, long affected) const
	{
		if (affected != 0)
			return true;
		// Nothing was restricting this write in the first place, so nothing could have been kept
		// from it: an empty result is an empty result. Without these two, un-posting a document
		// that never had movements would report "not enough access rights" to a full-access user —
		// which is exactly the confusion this whole second stage exists to end.
		if (m_roleProcs.empty())
			return true;                                  // no restricting role (or Designer)
		if (m_metaData != nullptr && m_metaData->IsFullAccess())
			return true;                                  // Tier 0 — full access

		bool allowed = true;
		Unwind(query, [&](const ibBackendQueryable*, const ibValueMetaObjectGenericData* srcMeta) {
			if (!srcMeta->IsAccessPerRecord())
				return true;                              // a set — its count says nothing; keep looking
			allowed = false;
			return false;                                 // refused: stop here
		});
		return allowed;
	}

	// The TABLE stage — the SAME unwind the row filter uses, run BEFORE it, on the query that is
	// waiting to go: every table it touches is asked for the right that guards this very action.
	// One table saying no is enough — the answer is FALSE, and nothing is folded, nothing runs. The
	// door turns that into the exception; here it stays a verdict, as the row filter's is.
	//
	// It is asked even with no RLS module anywhere: a role can restrict nothing per-row and still
	// have a flag cleared on a table.
	bool Gate(ibDataQueryBuilder& query, RightPredicate right, const wxString& operation) const
	{
		if (!TablesAllowed(query, right))
			return false;                                 // refused on a table — the filter never runs

		// Whether anything restricts at all is a property of the RUN, not of a table (Designer / no
		// restricting role / full access), so it is settled once, outside the walk.
		if (m_roleProcs.empty())
			return true;
		if (m_metaData != nullptr && m_metaData->IsFullAccess())
			return true;

		// The modules run one at a time: they belong to the host session's runtime and keep their
		// frame in the object, and a rented run borrows this very policy on another thread.
		std::lock_guard<std::mutex> lk(m_applyMtx);
		Unwind(query, [&](const ibBackendQueryable* source, const ibValueMetaObjectGenericData*) {
			ApplyToSource(query, source, operation);
			return true;
		});
		return true;
	}

	// Outcome of running ONE role's OnAccess* handler. The door owns the safe default (fail-closed),
	// NOT the module's error handling: a handler that does not explicitly signal success is DENIED.
	enum class RoleOutcome {
		NoHandler,   // CallAsFunc found no such handler -> this role imposes no restriction -> ALLOW
		Failed,      // handler threw / swallowed / did not set Allowed=True -> DENY (fail-closed)
		Succeeded,   // handler set Allowed=True -> trust what it folded (restriction or full-allow)
	};

	// The metaobject behind a source, or null for one that has none (temp / computed / subquery) —
	// the only place a source is turned into metadata, which is why the door never has to.
	const ibValueMetaObjectGenericData* FindSourceMetaObject(const ibBackendQueryable* source) const
	{
		if (source == nullptr || m_metaData == nullptr)
			return nullptr;
		return m_metaData->FindAnyObjectByFilter<ibValueMetaObjectGenericData>(source->GetQueryTableId());
	}

	// One table, EVERY query (no cache — real-time). The module gets the SOURCE as a base decorator over
	// `target`; its Source.Join(…) / Source.Where(…) (or the `restrict` keyword) fold the restriction
	// STRAIGHT into `target` as a SIDE EFFECT — one builder, no subquery wrap. The real source stays the
	// query's From, so the query still PAGES and PUSHES DOWN and register aggregates auto-restrict. The
	// module runs ONCE per query to BUILD the join (breakpointable), NEVER per row.
	//
	// FAIL-CLOSED: the door owns the safe default, NOT the module's error handling. The handler must set
	// its by-ref `Allowed` arg to True to be trusted; if it does not (fell through, swallowed its own
	// exception, or Allowed=False) OR it throws, the role FAILED -> DENY. A handler that is ABSENT
	// (CallAsFunc returns false) is different — that role simply imposes no restriction -> ALLOW
	// (migration-safe). A mistake thus over-restricts (visible), never exposes.
	// Multi-role = OR (a user sees a row allowed by ANY role): a role granting full access (no handler, or
	// succeeded with no restriction) opens the whole OR; a restricting role contributes its predicate; a
	// FAILED role contributes NOTHING (does not widen); EVERY role failing -> the door denies outright.
	// TODO(perf): the module is a per-query template — JIT-compile it (hot path); do NOT cache its RESULT.
	void ApplyToSource(ibDataQueryBuilder& query, const ibBackendQueryable* source,
	                   const wxString& operation) const
	{
		// The module identifies the source by its canonical FULL NAME ("Document.X" / "Catalog.X"),
		// so pass GetFullName() (not the short GetQueryName()); fall back to the short name if the
		// metaobject cannot be resolved.
		const ibValueMetaObjectGenericData* srcMeta = FindSourceMetaObject(source);
		const wxString sourceName = srcMeta != nullptr ? srcMeta->GetFullName() : source->GetQueryName();
		const wxString handler = (operation == wxT("Read")) ? wxT("OnAccessRead") : wxT("OnAccessWrite");

		// The restricting roles' module procUnits were resolved ONCE in the ctor (post-compile, pre-run)
		// and cached for the session's life; Apply already returned when the list is empty, so at least one
		// role restricts here.
		const std::vector<std::shared_ptr<ibProcUnit>>& procs = m_roleProcs;

		// Run one role's handler over a base decorator on `target`. The decorator folds Join/Where into
		// `target` as a SIDE EFFECT; the handler signals its verdict by setting the by-ref `Allowed` arg to
		// True. Args (by-ref): Source (the decorator), Operation, Allowed (default False = deny).
		const auto runRole = [&](const std::shared_ptr<ibProcUnit>& proc, ibDataQueryBuilder& target) -> RoleOutcome {
			ibValue src(new ibValueQueryDecorator(&target, source, sourceName));
			ibValue op(operation);
			ibValue allowed(false);            // the handler's VERDICT — a by-ref out-param (the OES idiom, like
			                                   // BeforeOpen's `Cancel`); default DENY, the handler sets it True.
			                                   // A grant-flag is more informative than a deny/cancel one — the
			                                   // positive `Allowed = True` says exactly what happened.
			try {
				// The handler runs PRIVILEGED: any query its body builds during this call sees a trusted
				// session (GetAccessPolicy -> null), so reading the very source it restricts does NOT re-enter
				// RLS. RAII restores enforcement on every exit, including the throw caught just below.
				ibAccessTrustScope trust(m_session);
				// CallAsProc (comma-separated args) returns TRUE only if the procedure was FOUND and RAN; it
				// returns FALSE — WITHOUT throwing and WITHOUT running anything — when there is no such handler.
				// So absence is the bool, not an exception: no handler -> this role imposes no restriction ->
				// ALLOW. Only a PRESENT handler is held to the fail-closed verdict below.
				if (!proc->CallAsProc(handler, src, op, allowed))
					return RoleOutcome::NoHandler;
			}
			catch (const ibBackendException&) {
				return RoleOutcome::Failed;    // the handler body threw (e.g. "cannot be lowered") -> deny
			}
			return (allowed.GetType() == ibValueTypes::TYPE_BOOLEAN && allowed.GetBoolean())
				? RoleOutcome::Succeeded
				: RoleOutcome::Failed;         // ran but did not grant (swallowed / forgot / Allowed=False) -> deny
		};

		if (procs.size() == 1) {
			// ONE restricting role — folds straight into the query (real source: pages + pushes down).
			switch (runRole(procs.front(), query)) {
			case RoleOutcome::Failed:    // loud fail-closed deny — say which source and which operation
				ibBackendAccessException::Error(wxString::Format(_("%s on '%s' was refused by the role's access policy"),
					operation, sourceName));
			case RoleOutcome::NoHandler:                                     // no restriction -> allow
			case RoleOutcome::Succeeded: return;                            // fold (if any) already applied
			}
			return;
		}

		// SEVERAL roles — a user gains access via ANY of them, so their restrictions OR (not AND). Each role
		// folds into a per-role scratch; we OR the succeeding roles' restrictions into the main query, keeping
		// the real source (still pages). A WHERE-only role contributes its predicate; a role that JOINs a table
		// is reduced by MATERIALISING the source keys it admits into `key = v … OR` (the SQL IR has no
		// IN-subquery). A FAILED role contributes nothing (does not widen the OR).
		//
		// A role with NO handler for this op does NOT participate in RLS: it is NEUTRAL — it neither widens the
		// OR (a role that simply does not implement RLS must NOT silently OPEN everything for a user who also
		// holds a restricting role — the multi-role fail-open footgun) nor denies. Only an EXPLICIT full-grant
		// (a handler that runs, succeeds, and folds NO restriction) opens the OR. Terminal: if NO role
		// participated (all handler-less) there is no RLS here -> ALLOW (migration-safe, matches the single-role
		// NoHandler path); if roles participated but ALL failed -> fail-closed DENY.
		bool anyParticipated = false;                     // any role RAN a handler (restrict / full-grant / fail)
		std::vector<ibQueryPredicatePtr> rolePredicates;
		for (const std::shared_ptr<ibProcUnit>& proc : procs) {
			ibDataQueryBuilder scratch;
			scratch.From(source);
			scratch.WithAccessPolicy(nullptr);            // trusted: the scratch is only mined for its predicate
			const RoleOutcome outcome = runRole(proc, scratch);

			if (outcome == RoleOutcome::NoHandler)
				continue; // no handler -> this role does not participate -> NEUTRAL (does not widen, does not deny)
			anyParticipated = true;
			if (outcome == RoleOutcome::Failed)
				continue; // fail-closed: a failed role admits no rows -> contributes NOTHING to the OR

			// Succeeded — fold its restriction into the OR.
			std::vector<const ibBackendQueryable*> scratchSources;
			scratch.GetSources(scratchSources);
			if (scratchSources.size() > 1) {
				// JOIN-based role — a join can't be OR-folded as SQL, so MATERIALISE the rows it admits:
				// project the source key of the joined result and OR-fold `key = v` over those keys (mirrors
				// the query language's `key IN (subquery)` lowering). No key to reduce onto -> fail closed.
				const std::vector<const ibBackendQueryColumn*> keyCols = source->GetPrimaryKeyColumns();
				if (keyCols.empty() || keyCols.front() == nullptr)
					ibBackendAccessException::Error(wxString::Format(
						_("%s on '%s': the role restricts it through a join, but the source has no key to reduce onto"),
						operation, sourceName));
				const ibBackendQueryColumn* keyCol = keyCols.front();
				scratch.Select(keyCol, wxT("v"));
				ibDataQueryResult r = scratch.Execute(ibReadPageRequest{});
				ibQueryPredicatePtr admitted;
				while (r.Next()) {
					ibQueryCondition c;
					c.m_col = keyCol; c.m_value = r.GetColumn(wxT("v")); c.m_op = ibQueryFilterOp::Equal;
					ibQueryPredicatePtr eq = ibQueryPredicate::Leaf(c);
					admitted = admitted ? ibQueryPredicate::Compose(ibQueryPredicateKind::Or, admitted, eq) : eq;
				}
				if (!admitted)   // the role admits NO rows -> a contradiction, so its OR branch is FALSE
					admitted = ibQueryPredicate::Compose(ibQueryPredicateKind::And,
						ibQueryPredicate::Null(keyCol, false), ibQueryPredicate::Null(keyCol, true));
				rolePredicates.push_back(admitted);
				continue;
			}
			ibQueryPredicatePtr pred = scratch.GetWherePredicate();
			if (!pred)
				return;   // succeeded with NO Join and NO Where -> EXPLICIT full grant -> the OR is unrestricted
			rolePredicates.push_back(pred);
		}

		if (!anyParticipated)
			return;                                       // no role implements RLS here -> migration-safe ALLOW
		if (rolePredicates.empty())                       // roles participated but ALL failed -> fail-closed deny
			ibBackendAccessException::Error(wxString::Format(
				_("%s on '%s' was refused by every role's access policy"), operation, sourceName));

		ibQueryPredicatePtr orPred = rolePredicates.front();
		for (size_t i = 1; i < rolePredicates.size(); ++i)
			orPred = ibQueryPredicate::Compose(ibQueryPredicateKind::Or, orPred, rolePredicates[i]);
		query.Where(orPred);                              // (role1 WHERE) OR (role2 WHERE) OR …
	}

	ibSession*        m_session;
	const ibMetaData* m_metaData;   // the session's config metadata — resolves role metaobjects (config-level)
	// The current user's role-module procUnits — resolved ONCE in the ctor (built post-compile, pre-run)
	// and held for the session's whole life (roles are fixed while it lives). Empty = no restricting role.
	std::vector<std::shared_ptr<ibProcUnit>> m_roleProcs;
	// Serialises the role-module run — see Apply. Mutable because applying a policy
	// is const from the door's side; the lock is what makes that true when a rented
	// run borrows this object onto another thread.
	mutable std::mutex m_applyMtx;
};

} // namespace

namespace {
// Per-thread current-session map. Lookup semantics depend on the
// access mode owned by ibSessionRegistry — see ibSession::Current.
//
// weak_ptr storage so a session destroyed while a binding still
// references it auto-expires. Critical for the nested-SessionScope
// refresh-cycle race: the inner scope's dtor restores its captured
// m_prev into the map; if that previous session got destroyed in the
// meantime, restoring a raw pointer would resurrect a dangling
// reference. With weak_ptr the expired binding either unlocks to
// nullptr (Current returns null/fallback) or is simply erased on the
// next observation — no UAF.
std::shared_mutex s_currentMutex;
std::unordered_map<std::thread::id, std::weak_ptr<ibSession>> s_currentByThread;

} // namespace

ibSession::ibSession(wxString id, ibSessionKind kind)
	: m_id(std::move(id))
	, m_kind(kind)
	, m_workDate(wxDateTime::Now())
{
}

ibSession::~ibSession()
{
	// s_currentByThread holds weak_ptr<ibSession>; when the last strong
	// reference drops, every entry pointing here auto-expires. Subsequent
	// Current() calls do lock() and observe nullptr. The normal teardown
	// path also calls UnbindSession(this) explicitly from appData's
	// OnDisconnect listener — that's the place that does the cleanup;
	// duplicating it here would only walk the bindings map under a unique
	// lock on every destruction, contending with Current() readers, with
	// no correctness benefit.

	// Ensure the destruction chain for objects created by this session
	// runs even when the session falls off without an explicit ClearRoot
	// (registry-driven Remove, abnormal teardown). Idempotent — ClearRoot
	// is a no-op when m_root is already null.
	ClearRoot();
}

ibValueModuleManagerRuntimeConfiguration* ibSession::GetManagerModule() const
{
	return m_root;   // ibValuePtr's implicit operator T*()
}

ibValueModuleManager* ibSession::GetEditModuleManager(const ibMetaData* metaData) const
{
	// Two roads off one seam, keyed on THIS session's kind (not the process-global
	// appData->DesignerMode()): a Designer session has no per-session runtime root —
	// it reads the lightweight designer manager from the metadata's compile cache.
	// Every other kind (Enterprise / WebClient / Service / …) uses its root mm.
	if (m_kind == ibSessionKind::Designer) {
		if (auto* cc = metaData ? metaData->GetCompileCache() : nullptr)
			return cc->GetModuleManager();
		return nullptr;
	}
	return m_root;
}

ibValueModuleManager* ibSession::EditModuleManagerFor(const ibMetaData* metaData)
{
	ibSession* session = ibSession::Current();
	return session ? session->GetEditModuleManager(metaData) : nullptr;
}

bool ibSession::Close(bool force)
{
	// Close does ONE thing: hand the decision to whatever this session
	// is — a desktop window, a web tab, a job runner. It is the same as
	// the user pressing [X], just arriving from the backend. It does not
	// tear anything down itself: the thing it just asked to close will
	// die, its holder will be released, and THAT is the teardown.
	//
	// A refusal is a normal answer — nothing happened, try again later.
	const ibSessionState state = State();
	if (state == ibSessionState::Stopping || state == ibSessionState::Gone)
		return true;

	// Force also stops whatever is running: the interpreter sees the flag
	// at its next opcode and unwinds, so nothing executes while the close
	// goes through.
	if (force)
		RequestForceExit();

	// And that is all Close does — start the close of whatever owns us.
	// It deliberately does NOT tear the session down itself, not even
	// under force: the teardown belongs to the holder release, and a
	// session torn down while its holder still lives would leave the
	// owner sitting on a corpse — a live window whose GetSession()
	// answers with a session that has no row, no runtime and no state.
	//
	// When there is nothing to close (no window, no tab) the default
	// OnClose ends the session right there, because in that case the
	// holder belongs to plain code that will drop it on its own.
	return OnClose(force);
}

void ibSession::Teardown()
{
	// The other half: what actually dismantles the session. Reached only
	// by releasing the owning holder — so it runs exactly once, when the
	// owner is really gone, and closing a window does NOT have to tell
	// the session anything: the holder release says it.
	const ibSessionState state = State();
	if (state == ibSessionState::Stopping || state == ibSessionState::Gone)
		return;

	// Mark the point of no return synchronously. The registry stamps
	// Stopping too, but on its own thread after the Remove below is
	// drained; without this line a second release in that gap would run
	// the whole teardown again.
	Transition(ibSessionState::Stopping);

	// --- quiesce ---------------------------------------------------
	// Expiring weak thread-bindings is not enough to make teardown safe:
	// a script thread holds its session by RAW pointer on its own stack,
	// where no weak_ptr can reach it. So we ask any in-flight script to
	// stop (checked at loop boundaries in ibProcUnit::Execute) and then
	// wait behind it in the session's own FIFO — when our empty task
	// runs, everything queued before it is done and the worker has given
	// up its lease on us.
	//
	// Re-entrant by construction: Submit runs inline when we are already
	// on this session's worker — the headless pool when we hold its lease,
	// the GUI pool when we are on the wx main thread it drains onto, and
	// trivially when there is no pool. So the future is ready before Submit
	// returns and this never deadlocks against itself.
	RequestCancel();
	{
		std::future<void> drained = Submit([] {});
		if (drained.valid())
			drained.wait_for(std::chrono::seconds(5));
	}
	// Lower the flag again: the teardown below still runs script-visible
	// handlers (per-kind hooks, module OnDestroy through DestroyRoot),
	// and a latched cancel would abort them at their first loop check.
	ClearCancel();

	// NEVER TAKEN IN, SO NOTHING TO GIVE BACK. An unlisted session (a rented read —
	// see m_listed) has no row to DELETE, no index entry to drop and nothing that
	// ever announced itself, so a Remove would only make the registry thread fire
	// disconnect listeners for it — an audit row per scrolled page. What it does
	// own is a queue in the worker pool, keyed on this pointer; drop that and we
	// are done. The connection goes back with the holder in ~ibSession.
	if (!m_listed) {
		if (ibWorkerPool* const pool = GetWorkerPool())
			pool->DropSession(this);
		Transition(ibSessionState::Gone);
		return;
	}

	// Submit Remove@Urgent — the registry thread DELETEs the sys_session
	// row, fires OnDisconnect and drops the index entry. It does NOT free
	// the object: m_own is a weak index, so the object dies when the last
	// holder does, which is normally the window that just went down.
	// Tolerate a release arriving from a dtor chain after appData is
	// gone — nothing to do, the registry went with it.
	ibSessionRegistry* const regPtr = ibApplicationData::GetSessionRegistry();
	if (regPtr == nullptr) return;
	auto& reg = *regPtr;
	if (reg.IsFatal())
		return;
	ibRegistryRequest req;
	req.kind    = ibRegistryRequestKind::Remove;
	req.session = shared_from_this();
	reg.Submit(std::move(req), ibPriority::Urgent);
}

void ibSession::Detach(std::chrono::milliseconds timeout)
{
	ibSessionRegistry* const regPtr = ibApplicationData::GetSessionRegistry();
	if (regPtr == nullptr) return;
	auto& reg = *regPtr;
	if (reg.IsFatal()) return;
	if (State() != ibSessionState::Added) return;

	// Prime the axis so WaitForAuth below sees the actual handler result.
	if (Auth() == ibAuthState::Authenticated)
		TransitionAuth(ibAuthState::Authenticated);

	ibRegistryRequest req;
	req.kind    = ibRegistryRequestKind::Detach;
	req.session = shared_from_this();
	reg.Submit(std::move(req), ibPriority::Normal);

	WaitForAuth(Auth(), timeout);
}

void ibSession::SetActivity(const wxString& activity)
{
	ibSessionRegistry* const regPtr = ibApplicationData::GetSessionRegistry();
	if (regPtr == nullptr) return;
	auto& reg = *regPtr;
	if (reg.IsFatal()) return;

	ibRegistryRequest req;
	req.kind     = ibRegistryRequestKind::SetActivity;
	req.session  = shared_from_this();
	req.activity = activity;
	reg.Submit(std::move(req), ibPriority::Low);
}

void ibSession::SetExclusive(bool on)
{
	// Registry runs the queue handshake + wait and gives us back the
	// verdict; we only translate it into an exception for the script
	// layer. Granted == success path (acquire AND release).
	ibSessionRegistry* const regPtr = ibApplicationData::GetSessionRegistry();
	if (regPtr == nullptr)
		ibBackendCoreException::Error(_("Session registry not initialised"));
	const ibExclusiveResult r = regPtr->SetExclusive(this, on);
	switch (r) {
	case ibExclusiveResult::Granted:
		return;
	case ibExclusiveResult::HeldByOther:
		ibBackendCoreException::Error(_("Another session is in exclusive mode"));
	case ibExclusiveResult::NotSole:
		ibBackendCoreException::Error(_("Cannot acquire exclusive mode: other sessions are active"));
	case ibExclusiveResult::Pending:
		ibBackendCoreException::Error(_("Exclusive mode request did not complete"));
	}
}

ibValueModuleManagerRuntimeConfiguration* ibSession::CreateRoot(ibMetaDataConfigurationBase* metaData)
{
	// Per-session root mm — replaces the legacy ibMetaDataConfigurationFile
	// process-singleton. Each session owns its own copy of the metadata
	// tree's runtime state. CreateRoot only allocates; CreateMainModule
	// (compile) runs from RunDatabase after common-module descriptors are
	// registered via OnBeforeRunMetaObject. Idempotent — second call with
	// the same metadata returns the existing root unchanged.
	if (metaData == nullptr)
		return nullptr;
	if (m_root)
		return m_root;
	auto* commonMeta = metaData->GetCommonMetaObject();
	if (commonMeta == nullptr)
		return nullptr;

	m_root = ibValuePtr<ibValueModuleManagerRuntimeConfiguration>(
		new ibValueModuleManagerRuntimeConfiguration(metaData, commonMeta));

	return m_root;
}

bool ibSession::CompileRoot()
{
	if (!m_root) return false;
	if (!m_root->CreateMainModule()) return false;

	// Runtime bring-up — formerly an explicit mm->AttachRuntime(s)
	// call from appData / webSession after CompileRoot. Folded in
	// here so callers see one entry point: compile + attach is the
	// session's own responsibility. AttachRuntime self-gates by
	// session kind (Enterprise / WebClient / Service execute; others
	// short-circuit), so no external wantsRuntime check is needed.
	m_root->AttachRuntime(this);

	// RLS — build the session's access policy HERE, right AFTER runtime bring-up: the role modules
	// attach as common modules DURING AttachRuntime, so their procUnits (FindCommonModule -> GetProcUnit)
	// are only live NOW — the policy ctor resolves + caches them once. Still before the session serves
	// any user query (CompileRoot finishes first), so it is in place before anything it must guard.
	// Designer never enforces (it runs off the edit-time manager, not this runtime root).
	if (!m_accessPolicy && !appData->DesignerMode())
		m_accessPolicy = std::make_unique<ibRuntimeAccessPolicy>(this, activeMetaData);

	// Lambda executor — m_root's procUnit is live after AttachRuntime,
	// so SetParent target is valid. ibValueFunction's Execute resolves
	// this through ibSession::GetLambdaRuntime().
	//
	// Custom frame array layout: regular ProcUnit setup puts own
	// m_cCurContext at m_pppArrayList[0] AND [1] (duplicate, since
	// runtime slot indices start at 1 with bDelta=false). The shim
	// has no own locals — lambda body's frame is per-call cRunContext
	// — so we substitute root's frame for the [0,1] pair. That way
	// lambda compile's depth=1 stamping (lambda discipline walks bc
	// chain to topmost = root, single increment) lands directly on
	// root mm's bound slots: Catalogs / Documents / Manager / system
	// functions all resolve at depth=1 without an offset hack.
	if (m_lambdaRuntime == nullptr) {
		if (auto rootPu = m_root->GetProcUnit()) {
			m_lambdaRuntime = std::make_unique<ibProcUnit>();
			m_lambdaRuntime->SetParent(rootPu.get());

			ibProcUnit* shim = m_lambdaRuntime.get();
			const unsigned int n = shim->GetParentCount();
			shim->m_ppArrayCode = new ibProcUnit*[n + 1];
			shim->m_ppArrayCode[0] = shim;
			shim->m_pppArrayList = new ibValue**[n + 2];
			shim->m_pppArrayList[0] = rootPu->m_cCurContext.m_pRefLocVars;
			shim->m_pppArrayList[1] = rootPu->m_cCurContext.m_pRefLocVars;
			for (unsigned int i = 0; i < n; i++) {
				ibProcUnit* p = shim->GetParent(i);
				shim->m_ppArrayCode[i + 1] = p;
				shim->m_pppArrayList[i + 2] = p->m_cCurContext.m_pRefLocVars;
			}
		}
	}

	return true;
}

bool ibSession::DestroyRoot()
{
	if (!m_root) return false;
	// Lambda runtime's parent is m_root's procUnit; drop it first so
	// the SetParent target stays valid right up to the moment we
	// release it.
	m_lambdaRuntime.reset();
	// The access policy caches the role-module procUnits resolved at CompileRoot; drop it so a later
	// CompileRoot rebuilds it against the recompiled modules (no stale procUnits after a reload).
	m_accessPolicy.reset();
	// Symmetric to CompileRoot: detach runtime before destroying the
	// main module so common-module ProcUnits drop in order. Formerly
	// an explicit mm->DetachRuntime(s) call from webSession.
	m_root->DetachRuntime(this);
	return m_root->DestroyMainModule();
}

void ibSession::ClearRoot()
{
	// Lambda runtime depends on m_root's procUnit (SetParent target);
	// drop it before m_root itself goes away.
	m_lambdaRuntime.reset();
	m_accessPolicy.reset();   // rebuilt by the next CompileRoot (cached role procUnits go stale here)

	if (m_root) {
		m_root->DetachRuntime(this);
		m_root->DestroyMainModule();
		m_root = nullptr;
	}
}

void ibSession::EnsureRoot()
{
	// Wired by ibSessionRegistry::NotifyAuthenticated to land between
	// OnFirstConnect (metadataCreate) and OnAuthenticated (RunDatabase /
	// CompileRoot). CreateRoot itself is idempotent; this wrapper just
	// guards on activeMetaData so headless sessions (Launcher, technical)
	// without metadata don't fault.
	if (m_root) return;
	if (activeMetaData == nullptr) return;
	// Designer never executes script — it has its own lightweight designer
	// module manager in the compile cache (ibValueModuleManagerDesigner). No
	// per-session runtime root mm is created; designer-path consumers read the
	// manager module from the compile cache instead of session->GetManagerModule().
	if (appData->DesignerMode()) return;

	// RLS — the access policy is NOT built here: it is built in CompileRoot, between module compile and
	// run, so its ctor can resolve the user's role-module procUnits (see there). The L3 door pulls it via
	// GetAccessPolicy(); no query fires before CompileRoot, so it is always in place when needed.
	CreateRoot(activeMetaData);
}

const ibAccessPolicy* ibSession::GetAccessPolicy() const
{
	// Inside a trusted window (a role module runs privileged) the door must see
	// no policy even though m_accessPolicy is real — the handler's own queries
	// must not re-enter RLS. The bypass is CONSTRUCTIVE (ibAccessTrustScope is
	// the only thing that sets the flag), so it never masks a forgotten policy.
	if (m_accessTrusted)
		return nullptr;
	if (m_accessPolicy)
		return m_accessPolicy.get();

	// NO POLICY OF ITS OWN — BORROW THE HOST'S. A session builds a policy at
	// CompileRoot, out of the user's role modules; a session with no identity and
	// no runtime therefore has none to build. A RENTED run is exactly that (see
	// ibJobTenancy): it reads on behalf of the session that started it, so the
	// rows it may see are the rows that session may see, and the policy that says
	// so already exists — one per user, where it was built, rather than a second
	// copy that could answer differently.
	//
	// The chain is walked rather than followed once, because a host may itself be
	// hosted. The host's TRUST flag is deliberately not consulted: a trusted
	// window is a property of what the host is doing on its own thread, and
	// lifting enforcement here because the host happens to be inside one would be
	// a bypass nobody asked for. Not a failure default either — a session that
	// has no host answers null exactly as before.
	for (std::shared_ptr<ibSession> host = Server(); host; host = host->Server()) {
		if (host->m_accessPolicy)
			return host->m_accessPolicy.get();
	}
	return nullptr;
}

ibSession* ibSession::Current()
{
	// Hot path — runs from BackendError handlers, logging, every script
	// opcode that asks for the current session. Must tolerate pre-appData
	// (bootstrap statics) and post-appData (process teardown listeners)
	// states without faulting.
	ibSessionRegistry* const regPtr = ibApplicationData::GetSessionRegistry();
	if (regPtr == nullptr) return nullptr;
	auto& reg = *regPtr;
	const auto tid = std::this_thread::get_id();

	// Debug-thread redirection: a thread registered as a debug-server
	// worker resolves Current() to "whichever script thread is parked
	// at a breakpoint right now" (front of the FIFO queue maintained
	// by EnterDebugLoop / LeaveDebugLoop). Lets debug command handlers
	// (Eval, ExpandExpression, EvalToolTip, EvalAutocomplete) reach
	// the right session through the same Current() call other code
	// uses, without an explicit sid threaded through every handler.
	if (reg.IsDebugThread(tid)) {
		// Raw out of a temporary hold — the caller keeps nothing, and the
		// session stays alive because its owner does. Debug commands run
		// synchronously while the script thread is parked, so it cannot
		// go away for the duration of the handler.
		if (auto s = reg.GetActiveDebugTarget().Share()) return s.get();
		// No session parked → fall through to the regular path below
		// so a debug worker can still observe its own Designer-side
		// connection on a thread that was bound separately.
	}

	// ONE RULE, EVERY HOST: the calling thread's own binding, else the fallback.
	//
	// There used to be two. Single mode meant "one session per process — hand the
	// lone map entry to whoever asks, regardless of thread", and that was true
	// right up until a desktop process stopped having one session. It has not had
	// one for a while: the job manager gives platform jobs their own, background
	// runs take their own, and a window's reader takes one more. With two entries
	// `begin()` on an unordered_map is an arbitrary one of them, so the UI thread
	// could resolve to a reader's session and a reader's thread to the window's —
	// silently, and differently from run to run.
	//
	// The fix is not a better Single; it is not having one. A thread that bound
	// itself means it (that is the whole point of ibSessionScope), and a thread
	// that did not gets the process's fallback — the first authenticated session,
	// which on a desktop IS the lone session the old branch was reaching for. The
	// access mode still sizes the worker pool; it no longer decides identity.
	std::shared_lock<std::shared_mutex> lk(s_currentMutex);
	if (auto it = s_currentByThread.find(tid); it != s_currentByThread.end()) {
		if (auto sp = it->second.lock()) return sp.get();
	}
	return reg.GetFallback();
}

void ibSession::SetAccessMode(AccessMode mode)
{
	// Static config setter — set once at process start by appData's ctor.
	// Null registry means we're outside the appData lifetime; ignore.
	if (auto* reg = ibApplicationData::GetSessionRegistry())
		reg->SetAccessMode(mode);
}

ibSession::AccessMode ibSession::GetAccessMode()
{
	// Default to Single if no registry — the most conservative fallback
	// (one session per process). Pre-appData / post-appData readers see
	// a sane value instead of faulting.
	auto* reg = ibApplicationData::GetSessionRegistry();
	return reg != nullptr ? reg->GetAccessMode() : AccessMode::Single;
}

void ibSession::SetFallback(ibSession* s)
{
	if (auto* reg = ibApplicationData::GetSessionRegistry())
		reg->SetFallback(s);
}

void ibSession::ClearFallback()
{
	if (auto* reg = ibApplicationData::GetSessionRegistry())
		reg->ClearFallback();
}

ibSession* ibSession::GetByThread(std::thread::id tid)
{
	ibSessionRegistry* const regPtr = ibApplicationData::GetSessionRegistry();
	if (regPtr == nullptr) return nullptr;
	// Same one rule as Current(), for a named thread rather than this one.
	std::shared_lock<std::shared_mutex> lk(s_currentMutex);
	if (auto it = s_currentByThread.find(tid); it != s_currentByThread.end()) {
		if (auto sp = it->second.lock()) return sp.get();
	}
	return regPtr->GetFallback();
}

std::vector<std::pair<std::thread::id, ibSession*>> ibSession::SnapshotByThread()
{
	std::shared_lock<std::shared_mutex> lk(s_currentMutex);
	std::vector<std::pair<std::thread::id, ibSession*>> out;
	out.reserve(s_currentByThread.size());
	for (const auto& kv : s_currentByThread) {
		// Skip expired entries — the binding's session was destroyed.
		// Snapshot reflects live state, not historical bindings.
		if (auto sp = kv.second.lock())
			out.emplace_back(kv.first, sp.get());
	}
	return out;
}

void ibSession::BindSessionToThread(ibSession* s, std::thread::id tid)
{
	std::unique_lock<std::shared_mutex> lk(s_currentMutex);
	if (s != nullptr)
		// weak_from_this is C++17, doesn't throw if the session isn't
		// yet wrapped in a shared_ptr — returns expired weak_ptr in
		// that case. All ibSession instances are made via make_shared
		// in the registry's typed factory, so by the time anyone calls
		// Bind the shared control block exists. If a future code path
		// constructs ibSession on the stack, the binding silently
		// expires on next lookup — safer than dangling raw pointer.
	{
		s_currentByThread[tid] = s->weak_from_this();
	}
	else
		s_currentByThread.erase(tid);
	// Interpreter state needs no separate setup — ibSession::GetPUState()
	// resolves via Current() each call, so the binding above is the
	// single point that "switches" the state visible to this thread.
}

void ibSession::UnbindThread(std::thread::id tid)
{
	std::unique_lock<std::shared_mutex> lk(s_currentMutex);
	s_currentByThread.erase(tid);
}

void ibSession::UnbindSession(ibSession* s)
{
	// Idempotent cleanup. Erases entries pointing to `s` AND any expired
	// weak_ptr entries we encounter while iterating — defensive, since
	// the bindings would auto-expire on next lookup anyway. Caller still
	// passes raw `ibSession*` (this) for ergonomics.
	std::unique_lock<std::shared_mutex> lk(s_currentMutex);
	for (auto it = s_currentByThread.begin(); it != s_currentByThread.end();) {
		auto locked = it->second.lock();
		if (!locked || (s != nullptr && locked.get() == s))
			it = s_currentByThread.erase(it);
		else
			++it;
	}
}

ibBackendDocFrame* ibSession::CurrentFrame()
{
	ibSession* s = Current();
	if (s == nullptr)
		return nullptr;

	// A force-exiting session hands out no window. The frame is still
	// there and still owns the session — this says nothing about lifetime,
	// and the closing sequence itself does not come this way (it holds its
	// frame directly). It says that from the outside the window is already
	// gone: nothing may open a form on it, ask a question through it, or
	// draw into it while it is going down.
	//
	// One gate instead of one per question, because "no frame" is a state
	// every caller already knows how to be in: the save prompts read it as
	// "ok to close", the status / message / refresh calls become no-ops,
	// and the context functions answer that they are not available.
	if (s->IsForceExit())
		return nullptr;

	return s->GetFrame();
}

ibProcUnitState* ibSession::GetPUState()
{
	if (ibSession* s = Current())
		return &s->m_procUnitState;

	// Sessionless fallback — codeRunner.exe (and any other host that
	// runs ad-hoc scripts without a session, e.g. command-line script
	// runners) needs a real ibProcUnitState to back m_currentRunModule
	// / m_runContext stack / error_place during Compile + Execute.
	// thread_local so concurrent sessionless callers each get their
	// own state — no shared mutation, no race.
	static thread_local ibProcUnitState ts_fallbackPUState;
	return &ts_fallbackPUState;
}

void ibSession::WakeDebugLoop()
{
	// Mark the session for cancellation and pop any parked debug loop.
	//
	// Step 1 — set m_forceExit so ibProcUnit::Execute's opcode loop
	// unwinds on the next iteration (after returning from DoDebugLoop)
	// instead of resuming user script. We bypass RequestForceExit
	// because its OnForceExit side effect (web → wfrontendCallProcessExitHook
	// when wes was started in --debug mode) would kill the entire
	// process; here we want to cancel only THIS session's interpreter.
	// Direct atomic store; deduplication against a later RequestForceExit
	// is fine since the destroy path doesn't follow up with one.
	m_forceExit.store(true, std::memory_order_release);
	// Step 2 — flip the debug-park flag and notify the per-session CV
	// so a script worker parked in ibDebuggerServer::DoDebugLoop
	// returns immediately. Mirrors the per-session wake performed by
	// ibDebuggerServer::WakeDebugSession on designer disconnect — same
	// graceful LeaveLoop is sent on the wire when the loop unwinds.
	if (m_debug == nullptr) return;
	m_debug->m_debugLoop = false;
	std::lock_guard<std::mutex> lk(m_debug->m_mutex);
	m_debug->m_cv.notify_all();
}

bool ibSession::OnClose(bool force)
{
	// See the declaration: cancel before teardown so the queue this is about to wait behind is idle.
	// Only under force — a polite close is a request, and a request does not interrupt work.
	if (force) {
		if (ibWorkerPool* const pool = GetWorkerPool())
			pool->CancelSession(this);
	}
	Teardown();
	return true;
}

void ibSession::RequestForceExit()
{
	// Raise the flag only. The interpreter observes it on its next opcode
	// loop iteration and unwinds. Announcing the close is NOT done here —
	// Close() does that exactly once, right after calling us; doing it in
	// both places fired OnClose twice on every forced close.
	if (m_forceExit.exchange(true, std::memory_order_acq_rel))
		return;   // already requested

	// Registry fan-out — covers session kinds with nothing of their own
	// to close (wes' WebServer technical session). Without it,
	// a debug-thread Current() that falls back to the system row would
	// close it but no host listener would learn about it. Tolerate a
	// post-teardown trigger silently — there's no one left to notify.
	if (auto* reg = ibApplicationData::GetSessionRegistry())
		reg->NotifyForceExit(this);
}

ibWorkerPool* ibSession::GetWorkerPool() const
{
	ibSessionRegistry* const regPtr = ibApplicationData::GetSessionRegistry();
	return regPtr != nullptr ? regPtr->GetWorkerPool() : nullptr;
}

std::future<void> ibSession::Submit(std::function<void()> task)
{
	if (ibWorkerPool* pool = GetWorkerPool())
		return pool->Submit(this, std::move(task));

	// No pool for this session: either none is installed in the process,
	// or the session answered nullptr on purpose because its work belongs
	// on the calling thread (the desktop GUI session). Run inline so the
	// future contract still holds — the call returns with it already
	// fulfilled, or carrying the exception the task threw.
	//
	// BOUND EITHER WAY. A worker takes an ibSessionScope around every task it
	// drains, and work that runs here must not be the exception to that: what a
	// task resolves through Current() — the interpreter state, the connection, the
	// access policy the query layer reads — would otherwise be somebody else's.
	// The case that made it visible is a RENTED read: it mints a session precisely
	// to get a connection of its own, and unbound it would go straight back to
	// reading through the parent's, which is the one that is busy.
	//
	// Costs nothing where the binding already holds (the GUI session submitting to
	// itself, a re-entrant submit from inside this session's own worker): the scope
	// saves and restores whatever was there.
	ibSessionScope scope(this);
	std::promise<void> p;
	try { task(); p.set_value(); }
	catch (...) { p.set_exception(std::current_exception()); }
	return p.get_future();
}

wxString ibSession::Reason() const
{
	std::lock_guard<std::mutex> lk(m_mtx);
	return m_reason;
}

void ibSession::SetReason(const wxString& reason)
{
	std::lock_guard<std::mutex> lk(m_mtx);
	m_reason = reason;
}

void ibSession::Transition(ibSessionState next, const wxString& reason)
{
	{
		std::lock_guard<std::mutex> lk(m_mtx);
		if (!reason.IsEmpty()) m_reason = reason;
		m_state.store(next, std::memory_order_release);
	}
	// notify_all — multiple producers may wait on different predicates
	// (WaitForState vs WaitForAuth). Spurious wakes short-circuit via the
	// predicate lambda.
	m_cv.notify_all();
}

void ibSession::TransitionAuth(ibAuthState next, const wxString& reason)
{
	{
		std::lock_guard<std::mutex> lk(m_mtx);
		if (!reason.IsEmpty()) m_reason = reason;
		m_auth.store(next, std::memory_order_release);
	}
	m_cv.notify_all();
}

ibSessionState ibSession::WaitForState(ibSessionState from, std::chrono::milliseconds timeout)
{
	std::unique_lock<std::mutex> lk(m_mtx);
	m_cv.wait_for(lk, timeout, [this, from]{
		return m_state.load(std::memory_order_acquire) != from;
	});
	return m_state.load(std::memory_order_acquire);
}

ibAuthState ibSession::WaitForAuth(ibAuthState from, std::chrono::milliseconds timeout)
{
	std::unique_lock<std::mutex> lk(m_mtx);
	m_cv.wait_for(lk, timeout, [this, from]{
		return m_auth.load(std::memory_order_acquire) != from;
	});
	return m_auth.load(std::memory_order_acquire);
}

ibSession::OpenResult ibSession::Open(const wxString& user, const wxString& password)
{
	// Must be Added (registry accepted the session); Anonymous or AuthFailed
	// on the auth axis — either is a valid retry point.
	if (State() != ibSessionState::Added) return OpenResult::Failed;

	ibSessionRegistry* const regPtr = ibApplicationData::GetSessionRegistry();
	if (regPtr == nullptr) return OpenResult::Failed;
	auto& reg = *regPtr;
	if (reg.IsFatal()) return OpenResult::Failed;

	constexpr auto timeout = std::chrono::seconds(20);

	auto submitAttach = [&](const wxString& u, const wxString& p) {
		// Reset auth axis so WaitForAuth below has a known "from" value —
		// otherwise a prior AuthFailed state would trigger the wait
		// immediately without the new Attach having been processed.
		TransitionAuth(ibAuthState::Anonymous);

		ibRegistryRequest req;
		req.kind     = ibRegistryRequestKind::Attach;
		req.session  = shared_from_this();
		req.user     = u;
		req.password = p;
		reg.Submit(std::move(req), ibPriority::Normal);

		return WaitForAuth(ibAuthState::Anonymous, timeout);
	};

	ibAuthState res = submitAttach(user, password);
	if (res == ibAuthState::Authenticated) {
		// NotifyAuthenticated fires three phases in order:
		//   1. OnFirstConnect listeners — process-level metadata bootstrap
		//      (metadataCreate, populates activeMetaData) on the first auth.
		//   2. session->EnsureRoot — per-session root mm allocated NOW so
		//      step 3's listeners can rely on GetManagerModule() != null.
		//   3. OnAuthenticated listeners — per-session bring-up
		//      (RunDatabase fires OnBefore/AfterRunMetaObject which read
		//      session->mm; CompileRoot; AttachRuntime).
		// Note: NotifyAuthenticated already calls BindSessionToThread
		// before firing listeners, so any breakpoint hit inside them
		// resolves Current() to THIS session (the registry-fallback
		// trap is closed at that level, no extra scope needed here).
		reg.NotifyAuthenticated(this);
		return OpenResult::Authenticated;
	}

	// Interactive fallback — GUI override shows login dialog (shared for
	// designer + enterprise via ibGUISession::OnShowAuthenticate). Pin
	// `this` as Current() for the dialog's lifetime so the OK handler's
	// appData->Login → InstallUser writes m_userInfo / m_sessionRawPassword
	// onto THIS session (Current() resolves to it). Without the scope,
	// InstallUser would target whatever the calling thread last bound —
	// often nullptr in pre-auth flows — and m_userInfo would stay empty,
	// making submitAttach below fire with blank creds (= "invalid user
	// or password" on the second pass through ProcessAttach).
	bool dlgOk;
	{
		ibSessionScope scope(this);
		dlgOk = OnShowAuthenticate(user, password);
	}
	// Dialog returning false == user clicked Cancel. Distinguish from
	// "creds rejected by server" so the GUI app's no-session branch
	// stays silent on cancel and only messages on a real auth failure.
	if (!dlgOk) return OpenResult::Cancelled;

	res = submitAttach(m_userInfo.m_strUserName, m_sessionRawPassword);
	if (res == ibAuthState::Authenticated) {
		reg.NotifyAuthenticated(this);
		return OpenResult::Authenticated;
	}
	return OpenResult::Failed;
}

// --- ibSessionThreadBinding --------------------------------------------

ibSessionThreadBinding::ibSessionThreadBinding(ibSession* s) noexcept
	: m_tid(std::this_thread::get_id())
{
	ibSession::BindSessionToThread(s, m_tid);
}

ibSessionThreadBinding::~ibSessionThreadBinding()
{
	ibSession::UnbindThread(m_tid);
}

// --- ibSessionScope -----------------------------------------------------

ibSessionScope::ibSessionScope(ibSession* s)
{
	const std::thread::id tid = std::this_thread::get_id();
	std::unique_lock<std::shared_mutex> lk(s_currentMutex);
	auto it = s_currentByThread.find(tid);
	// Save the previous binding as a weak_ptr COPY (not raw). If the
	// referenced session is destroyed while this scope is alive, the
	// dtor's restore-step lock()s and either falls back to "no binding"
	// or restores a still-live session — never resurrects a freed
	// pointer. Was the root cause of the rapid-F5 UAF in CurrentFrame
	// (see project_refresh_execute_crash 2026-04-27).
	if (it != s_currentByThread.end()) m_prev = it->second;
	if (s != nullptr) {
		s_currentByThread[tid] = s->weak_from_this();
	}
	else
		s_currentByThread.erase(tid);
	// Interpreter state — no separate cache to manage. ibSession::GetPUState()
	// resolves through Current() each call; the binding update above is
	// what makes the new session's state visible.
}

ibSessionScope::~ibSessionScope()
{
	const std::thread::id tid = std::this_thread::get_id();
	std::unique_lock<std::shared_mutex> lk(s_currentMutex);
	if (auto sp = m_prev.lock())
		s_currentByThread[tid] = m_prev;   // weak_ptr copy of still-live binding
	else
		s_currentByThread.erase(tid);
}

std::shared_ptr<ibDatabaseLayer> ibSession::DatabaseLayer()
{
	ibSession* sess = ibSession::Current();
	if (sess == nullptr)
		ibBackendCoreException::Error(_("ses_query: no active session"));
	// Single entry — holder's EnsureConnection resolves TX > scope >
	// fresh Checkout (auto-bound as scope). See connectionHolder.h.
	auto conn = sess->EnsureConnection();
	if (!conn || !conn->IsOpen())
		ibBackendCoreException::Error(_("ses_query: session failed to acquire a database connection"));
	return conn;
}
