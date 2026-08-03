/////////////////////////////////////////////////////////////////////////////
// ibLockManager implementation — sys_lock app-table coordination.
//
// Acquire algorithm (per item, inside a dedicated TX on the lock
// holder so the row commit doesn't ride on the caller's business TX):
//
//   1. Compute keyHash = SHA-256 of canonical serialization.
//   2. SELECT sessionGuid, userName, lockMode FROM sys_lock
//        WHERE namespace = ? AND keyHash = ?  <FOR UPDATE>   (ir.m_lockForUpdate; the dialect renders the clause)
//      — DB row-lock pins the index entry for the rest of this TX so
//      a concurrent acquire serialises behind us.
//   3. For each result row:
//        - if otherSession == mySession → re-entrant (upgrade or no-op)
//        - if otherMode == Exclusive OR requestedMode == Exclusive →
//          conflict, rollback, throw LockConflict with otherUser
//   4. INSERT INTO sys_lock (...) VALUES (...).
//   5. COMMIT.
//
// On any conflict / driver error mid-batch we rollback the whole TX,
// so partial acquires never persist (atomic batch semantics).
//
// See docs/record-locks.md for the full design.
/////////////////////////////////////////////////////////////////////////////

#include "backend/lock/lockManager.h"

#include "backend/appData.h"
#include "backend/backend_exception.h"
#include "backend/databaseLayer/connectionPool.h"
#include "backend/databaseLayer/connectionScope.h"
#include "backend/databaseLayer/databaseLayer.h"            // ibTxOptions (the lock TX still rides the driver's tpb)
#include "backend/databaseLayer/databaseQueryBuilder.h"     // L2 door — pessimistic SELECT via ir.m_lockForUpdate/m_lockNoWait
#include "backend/databaseLayer/databaseErrorCodes.h"
#include "backend/session/session.h"
#include "backend/userInfo.h"

#include <wx/datetime.h>

#include <set>

namespace {

// Table name kept here so call sites don't drift. Mirrors the
// session-registry's sys_session table naming.
const wxString kSysLockTable = wxT("sys_lock");

} // namespace

// No Instance() — ibApplicationData owns the only instance. Callers
// reach it through ibApplicationData::GetLockManager().

ibLockManager::ibLockManager(ib::AppDataCtorToken) {}

ibLockHandle ibLockManager::Acquire(const std::vector<ibLockItem>& items,
                                     const ibLockOptions&           opts,
                                     ibLockHolder*                  customHolder)
{
	if (items.empty())
		return ibLockHandle();   // empty batch → empty handle, no-op

	// Resolve owner identity. Custom holder wins when supplied; else
	// fall back to the active session. Either is required — no
	// "anonymous" lock owners.
	ibGuid    ownerGuid;
	wxString  ownerName;
	wxString  ownerComputer;

	if (customHolder != nullptr) {
		ownerGuid     = customHolder->Identity();
		ownerName     = customHolder->DisplayName();
		ownerComputer = customHolder->Computer();
	}
	else {
		ibSession* const session = ibSession::Current();
		if (session == nullptr) {
			ibBackendCoreException::Error(
				_("ibLockManager::Acquire requires an active session or a custom holder."));
		}
		ownerGuid     = session->Identity().m_guid;
		ownerName     = session->Identity().m_userName;
		ownerComputer = session->Identity().m_computer;
	}

	// Dedicated holder — Acquire's TX must not piggyback on the
	// caller's business TX (lock rows must be visible to other
	// processes the moment Acquire returns, not when the caller
	// eventually commits).
	// The lock TX runs on the lock manager's own holder (out of the caller's business TX) — lock rows
	// must be visible to peer processes the moment Acquire returns. One builder = one borrowed
	// connection for the whole batch; its scope carries the TX and every statement below.
	ibDatabaseQueryBuilder q(&m_lockHolder);
	if (!q.IsOpen()) {
		ibBackendCoreException::Error(
			_("ibLockManager::Acquire - database is not open."));
	}

	ibDatabaseLayer::ibTxOptions txOpts;
	txOpts.noWait = !opts.wait;
	q.BeginTransaction(txOpts);

	std::vector<ibGuid> acquired;

	try {
		const wxString ownerGuidStr = ownerGuid.str();
		const wxDateTime nowUtc = wxDateTime::UNow();

		for (const auto& item : items) {
			// Hash + canonical now owned by the item (lazy-cached) —
			// lockManager no longer composes them locally. See
			// lockTypes.h ibLockItem.
			const wxString& keyHash = item.KeyHash();
			const wxString& keyData = item.KeyData();

			// ---- Conflict check (pessimistic FOR UPDATE [NOWAIT] via the L2 lock-hint path) ----
			bool       ownExisting = false;
			ibGuid     ownExistingGuid;
			ibLockMode ownExistingMode = ibLockMode::Shared;
			bool       conflictHit = false;
			wxString   conflictUser;
			{
				ibQueryIR ir(ibProject(
					ibFilter(ibScan(kSysLockTable),
						ibBinOp(ibQueryBinOp::And,
							ibBinOp(ibQueryBinOp::Eq, ibCol(wxT("namespace")), ibConst(ibValue(item.namespaceName))),
							ibBinOp(ibQueryBinOp::Eq, ibCol(wxT("keyHash")),   ibConst(ibValue(keyHash))))),
					{ { ibCol(wxT("sessionGuid")), wxEmptyString },
					  { ibCol(wxT("userName")),    wxEmptyString },
					  { ibCol(wxT("lockMode")),    wxEmptyString } }));
				ir.m_lockForUpdate = true;
				ir.m_lockNoWait    = !opts.wait;   // non-blocking acquire fails fast on a held row

				// The cursor must be released before the INSERT/UPDATE on the same connection, so it
				// lives in this inner block (mirrors the old CloseResultSet placement).
				ibQueryResult rs = q.ExecuteIR(ir);
				while (rs.Next()) {
					const wxString otherSess  = rs.GetResultString(wxT("sessionGuid"));
					const wxString otherUser  = rs.GetResultString(wxT("userName"));
					const ibLockMode otherMode =
						static_cast<ibLockMode>(rs.GetResultInt(wxT("lockMode")));

					if (otherSess.CmpNoCase(ownerGuidStr) == 0) {
						// Re-entrant — track the first own row we find.
						// (Same-session multiple rows shouldn't happen
						// in practice — Acquire ensures at most one row
						// per (session, namespace, keyHash).)
						ownExisting     = true;
						ownExistingMode = otherMode;
						continue;
					}

					// Other session — conflict iff either side is X.
					if (otherMode == ibLockMode::Exclusive ||
					    item.lockMode == ibLockMode::Exclusive)
					{
						conflictHit  = true;
						conflictUser = otherUser;
						break;
					}
				}
			}

			if (conflictHit) {
				// Roll back any inserts we did earlier in this batch.
				q.RollBack();
				ibBackendLockException::LockConflictThrow(item.namespaceName, conflictUser);
			}

			// ---- Acquire ----
			if (ownExisting) {
				if (item.lockMode > ownExistingMode && opts.allowUpgrade) {
					// S → X upgrade in place. No new row added to
					// the handle — caller already owns this lock,
					// just at a higher level now.
					q.Execute(ibUpdate(kSysLockTable,
						{ { wxT("lockMode"), ibConst(ibValue(static_cast<int>(item.lockMode))) } },
						ibBinOp(ibQueryBinOp::Eq, ibCol(wxT("lockGuid")),
						        ibConst(ibValue(ownExistingGuid.str())))));
				}
				// Same-mode or X→S → no-op.
				continue;
			}

			// Fresh INSERT. (acquiredAt is a TIMESTAMP column — bound as an ibValue date, not a string;
			// keyData is the canonical human-readable key, stored as text — wide enough for typical keys.)
			const ibGuid newGuid = wxNewUniqueGuid;
			if (q.Execute(ibInsert(kSysLockTable, {
					{ wxT("lockGuid"),    ibConst(ibValue(newGuid.str())) },
					{ wxT("sessionGuid"), ibConst(ibValue(ownerGuidStr)) },
					{ wxT("namespace"),   ibConst(ibValue(item.namespaceName)) },
					{ wxT("keyHash"),     ibConst(ibValue(keyHash)) },
					{ wxT("keyData"),     ibConst(ibValue(keyData)) },
					{ wxT("lockMode"),    ibConst(ibValue(static_cast<int>(item.lockMode))) },
					{ wxT("acquiredAt"),  ibConst(ibValue(nowUtc)) },
					{ wxT("userName"),    ibConst(ibValue(ownerName)) },
					{ wxT("computer"),    ibConst(ibValue(ownerComputer)) },
				})) < 1) {   // the lock row must land; 0 is a row count, not a failure code
				q.RollBack();
				ibBackendCoreException::Error(
					_("ibLockManager: failed to insert sys_lock row."));
			}
			acquired.push_back(newGuid);
		}
	}
	catch (const ibBackendException&) {
		// RollBack already fired in the throw paths above; this catches any other
		// ibBackendException from the SQL helpers so we always rollback before propagating.
		if (q.IsActiveTransaction())
			q.RollBack();
		throw;
	}

	q.Commit();

	return ibLockHandle(std::move(acquired), ownerGuid);
}

void ibLockManager::ReleaseRows(const std::vector<ibGuid>& lockGuids)
{
	if (lockGuids.empty())
		return;

	ibDatabaseQueryBuilder q(&m_lockHolder);
	if (!q.IsOpen())
		return;  // best-effort — silent fail if DB closed (process shutdown)

	q.BeginTransaction();
	try {
		std::vector<ibQueryExprPtr> guids;
		guids.reserve(lockGuids.size());
		for (const auto& g : lockGuids)
			guids.push_back(ibConst(ibValue(g.str())));
		q.Execute(ibDelete(kSysLockTable, ibIn(ibCol(wxT("lockGuid")), std::move(guids))));
	}
	catch (const ibBackendException&) {
		if (q.IsActiveTransaction())
			q.RollBack();
		// Swallow — Release is best-effort. Stale rows get cleaned
		// up by the zombie sweep eventually.
		return;
	}
	q.Commit();
}

void ibLockManager::OnSessionEnd(const ibGuid& sessionGuid)
{
	if (!sessionGuid.isValid())
		return;

	ibDatabaseQueryBuilder q(&m_lockHolder);
	if (!q.IsOpen())
		return;

	q.BeginTransaction();
	try {
		q.Execute(ibDelete(kSysLockTable,
			ibBinOp(ibQueryBinOp::Eq, ibCol(wxT("sessionGuid")), ibConst(ibValue(sessionGuid.str())))));
	}
	catch (const ibBackendException&) {
		if (q.IsActiveTransaction())
			q.RollBack();
		return;
	}
	q.Commit();
}

void ibLockManager::SweepOrphans(const std::vector<ibGuid>& liveSessionGuids)
{
	ibDatabaseQueryBuilder q(&m_lockHolder);
	if (!q.IsOpen())
		return;

	std::set<wxString> live;
	for (const ibGuid& g : liveSessionGuids)
		live.insert(g.str());

	// Read the owners first, delete after — the DELETE runs per owner through
	// OnSessionEnd, which owns its own TX. Distinct owners, so a session holding
	// several locks is one DELETE, not one per row.
	std::set<wxString> orphans;
	try {
		ibQueryResult rs = q.ExecuteIR(ibQueryIR(ibProject(ibScan(kSysLockTable),
			{ { ibCol(wxT("sessionGuid")), wxEmptyString } })));
		while (rs.Next()) {
			const wxString owner = rs.GetResultString(wxT("sessionGuid"));
			if (!owner.IsEmpty() && live.find(owner) == live.end())
				orphans.insert(owner);
		}
	}
	catch (const ibBackendException&) {
		return;   // transient DB error — the next sweep tick retries
	}

	for (const wxString& owner : orphans)
		OnSessionEnd(ibGuid(owner));
}

std::vector<ibLockSnapshotRow> ibLockManager::GetSnapshot() const
{
	std::vector<ibLockSnapshotRow> rows;

	ibDatabaseQueryBuilder q(&m_lockHolder);
	if (!q.IsOpen())
		return rows;

	// Read-only — no TX needed; default isolation gives us latest committed state from other processes.
	try {
		ibQueryResult rs = q.ExecuteIR(ibQueryIR(ibProject(ibScan(kSysLockTable),
			{ { ibCol(wxT("lockGuid")),    wxEmptyString },
			  { ibCol(wxT("sessionGuid")), wxEmptyString },
			  { ibCol(wxT("namespace")),   wxEmptyString },
			  { ibCol(wxT("keyData")),     wxEmptyString },
			  { ibCol(wxT("lockMode")),    wxEmptyString },
			  { ibCol(wxT("acquiredAt")),  wxEmptyString },
			  { ibCol(wxT("userName")),    wxEmptyString },
			  { ibCol(wxT("computer")),    wxEmptyString } })));
		while (rs.Next()) {
			ibLockSnapshotRow r;
			r.lockGuid      = ibGuid(rs.GetResultString(wxT("lockGuid")));
			r.sessionGuid   = ibGuid(rs.GetResultString(wxT("sessionGuid")));
			r.namespaceName = rs.GetResultString(wxT("namespace"));
			r.keyData       = rs.GetResultString(wxT("keyData"));
			r.lockMode      = static_cast<ibLockMode>(rs.GetResultInt(wxT("lockMode")));
			r.acquiredAt    = rs.GetResultDate(wxT("acquiredAt"));
			r.userName      = rs.GetResultString(wxT("userName"));
			r.computer      = rs.GetResultString(wxT("computer"));
			rows.push_back(std::move(r));
		}
	}
	catch (...) { /* best-effort snapshot — empty/partial on failure */ }
	return rows;
}
