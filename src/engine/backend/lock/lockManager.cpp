/////////////////////////////////////////////////////////////////////////////
// ibLockManager implementation — sys_lock app-table coordination.
//
// Acquire algorithm (one transaction on the lock holder, so the commit
// does not ride on the caller's business transaction):
//
//   1. For each key, in (namespace, keyHash) order: lock sys_lock_key
//      until this transaction ends. UPDATE the header row; when it is
//      missing, INSERT it. Two inserts of the same key meet the primary
//      key — the loser rolls back and retries, then updates the row the
//      winner left. The wait is the transaction's lock_timeout, so a
//      noWait acquire refuses immediately.
//   2. Read sys_lock for that key.
//        - same owner → re-entrant (upgrade or no-op)
//        - other owner and either side Exclusive → LockConflict
//   3. INSERT the caller's sys_lock row.
//   4. COMMIT. The header row stays. Release does not delete it.
//
// On any conflict / driver error mid-batch we rollback the whole TX,
// so partial acquires never persist (atomic batch semantics).
//
// See docs/private/record-locks.md for the full design.
/////////////////////////////////////////////////////////////////////////////

#include "backend/lock/lockManager.h"

#include "backend/appData.h"
#include "backend/backend_exception.h"
#include "backend/databaseLayer/connectionPool.h"
#include "backend/databaseLayer/connectionScope.h"
#include "backend/databaseLayer/databaseLayer.h"            // ibTxOptions (the lock TX still rides the driver's tpb)
#include "backend/databaseLayer/databaseQueryBuilder.h"     // L2 door
#include "backend/databaseLayer/databaseLayerException.h"   // a primary-key race arrives as this
#include "backend/databaseLayer/databaseErrorCodes.h"
#include <algorithm>
#include <numeric>
#include "backend/session/session.h"
#include "backend/userInfo.h"

#include <set>

namespace {

// Table name kept here so call sites don't drift. Mirrors the
// session-registry's sys_session table naming.
const wxString kSysLockTable = wxT("sys_lock");
const wxString kSysLockKeyTable = wxT("sys_lock_key");

// The header insert lost the primary key. PostgreSQL aborts the transaction
// on that error, so the caller rolls back and tries the UPDATE, which now
// finds the row.
struct HeaderAlreadyThere {};

bool IsHeaderRace(const ibDatabaseLayerException& err)
{
	if (err.GetKind() == ibBackendDatabaseException::Kind::Constraint)
		return true;
	// Firebird reports the SQLCODE (-803) from a prepared statement; the isc
	// codes are what the connection classifier names for the same failure.
	const int code = err.GetDriverErrorCode();
	if (code == -803 || code == 335544665 || code == 335544349)
		return true;
	const wxString text = err.GetErrorDescription().Lower();
	return text.Find(wxT("unique")) != wxNOT_FOUND
		|| text.Find(wxT("duplicate")) != wxNOT_FOUND;
}

// Hold sys_lock_key for this key until the transaction ends. An UPDATE of the
// existing row is the lock. A missing row is inserted; the primary key decides
// the race with the other connection that inserted it first.
void LockKeyHeader(ibDatabaseQueryBuilder& q, const wxString& ns, const wxString& hash)
{
	const int locked = q.Execute(ibUpdate(kSysLockKeyTable,
		{ { wxT("namespace"), ibConst(ibValue(ns)) } },
		ibBinOp(ibQueryBinOp::And,
			ibBinOp(ibQueryBinOp::Eq, ibCol(wxT("namespace")), ibConst(ibValue(ns))),
			ibBinOp(ibQueryBinOp::Eq, ibCol(wxT("keyHash")),   ibConst(ibValue(hash))))));
	if (locked >= 1)
		return;

	try {
		if (q.Execute(ibInsert(kSysLockKeyTable, {
				{ wxT("namespace"), ibConst(ibValue(ns)) },
				{ wxT("keyHash"),   ibConst(ibValue(hash)) },
			})) < 1) {
			ibBackendCoreException::Error(_("ibLockManager: failed to insert the lock key."));
		}
	}
	catch (const ibDatabaseLayerException& err) {
		if (IsHeaderRace(err))
			throw HeaderAlreadyThere();
		throw;
	}
}

} // namespace

// No Instance() — ibApplicationInstance owns the only instance. Callers
// reach it through ibApplicationInstance::GetLockManager().

ibLockManager::ibLockManager(ib::AppDataCtorToken owner)
{
	// Its holder takes connections from ITS base's pool — named once, here (the base builds the pool first),
	// never asked of "the current one". The holder dies with the base, after the registry that question is
	// answered through: unnamed, it asked a freed registry on the way out, and every process crashed on exit
	// (2026-10-01, ~ibLockManager → GetPool → Current → IsDebugThread).
	m_lockHolder.SetPool(ibApplicationInstance::GetConnectionPool(owner.GetApplicationInstance()));
}

ibLockHandle ibLockManager::Acquire(const std::vector<ibLockItem>& items,
                                     const ibLockOptions&           opts,
                                     ibLockHolder*                  customHolder)
{
	if (items.empty())
		return ibLockHandle();   // empty batch → empty handle, no-op

	// Every path that touches the lock table takes this — see ibLockManager::m_mtx: the holder
	// owns ONE connection and does not serialise the threads that ask it for one.
	std::lock_guard<std::recursive_mutex> serialised(m_mtx);

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
	txOpts.noWait = !opts.wait;   // the header wait is this transaction's lock_timeout

	// Headers in a stable order, so two batches that name the same keys cannot
	// each hold the one the other is about to ask for.
	std::vector<std::size_t> headerOrder(items.size());
	std::iota(headerOrder.begin(), headerOrder.end(), 0);
	std::sort(headerOrder.begin(), headerOrder.end(), [&](std::size_t a, std::size_t b) {
		const int byName = items[a].namespaceName.Cmp(items[b].namespaceName);
		if (byName != 0)
			return byName < 0;
		return items[a].KeyHash().Cmp(items[b].KeyHash()) < 0;
	});

	std::vector<ibGuid> acquired;
	constexpr int kHeaderAttempts = 5;

	for (int attempt = 0; attempt < kHeaderAttempts; ++attempt) {
		q.BeginTransaction(txOpts);
		acquired.clear();
		try {
			for (std::size_t index : headerOrder)
				LockKeyHeader(q, items[index].namespaceName, items[index].KeyHash());

			const wxString ownerGuidStr = ownerGuid.str();
			const ibDateTime now = ibDateTime::Now();

			for (const auto& item : items) {
				const wxString& keyHash = item.KeyHash();
				const wxString& keyData = item.KeyData();

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
						{ { ibCol(wxT("lockGuid")),    wxEmptyString },
						  { ibCol(wxT("sessionGuid")), wxEmptyString },
						  { ibCol(wxT("userName")),    wxEmptyString },
						  { ibCol(wxT("lockMode")),    wxEmptyString } }));

					// The cursor must be released before the INSERT/UPDATE on the same connection.
					ibQueryResult rs = q.ExecuteIR(ir);
					while (rs.Next()) {
						const wxString otherSess  = rs.GetResultString(wxT("sessionGuid"));
						const wxString otherUser  = rs.GetResultString(wxT("userName"));
						const ibLockMode otherMode =
							static_cast<ibLockMode>(rs.GetResultInt(wxT("lockMode")));

						if (otherSess.CmpNoCase(ownerGuidStr) == 0) {
							// The first own row is the one an upgrade writes. Same-owner
							// duplicates should not happen — Acquire inserts one.
							if (!ownExisting)
								ownExistingGuid = ibGuid(rs.GetResultString(wxT("lockGuid")));
							ownExisting     = true;
							ownExistingMode = otherMode;
							continue;
						}

						if (otherMode == ibLockMode::Exclusive ||
						    item.lockMode == ibLockMode::Exclusive)
						{
							conflictHit  = true;
							conflictUser = otherUser;
							break;
						}
					}
				}

				if (conflictHit)
					ibBackendLockException::LockConflictThrow(item.namespaceName, conflictUser);

				if (ownExisting) {
					if (item.lockMode > ownExistingMode && opts.allowUpgrade) {
						// S → X on the row we already hold. The guid has to be the one
						// just read — an empty guid updates nothing and leaves the lock shared.
						const int updated = q.Execute(ibUpdate(kSysLockTable,
							{ { wxT("lockMode"), ibConst(ibValue(static_cast<int>(item.lockMode))) } },
							ibBinOp(ibQueryBinOp::Eq, ibCol(wxT("lockGuid")),
							        ibConst(ibValue(ownExistingGuid.str())))));
						if (updated < 1)
							ibBackendCoreException::Error(
								_("ibLockManager: failed to upgrade the held lock."));
					}
					continue;
				}

				const ibGuid newGuid = wxNewUniqueGuid;
				if (q.Execute(ibInsert(kSysLockTable, {
						{ wxT("lockGuid"),    ibConst(ibValue(newGuid.str())) },
						{ wxT("sessionGuid"), ibConst(ibValue(ownerGuidStr)) },
						{ wxT("namespace"),   ibConst(ibValue(item.namespaceName)) },
						{ wxT("keyHash"),     ibConst(ibValue(keyHash)) },
						{ wxT("keyData"),     ibConst(ibValue(keyData)) },
						{ wxT("lockMode"),    ibConst(ibValue(static_cast<int>(item.lockMode))) },
						{ wxT("acquiredAt"),  ibConst(ibValue(now)) },
						{ wxT("userName"),    ibConst(ibValue(ownerName)) },
						{ wxT("computer"),    ibConst(ibValue(ownerComputer)) },
					})) < 1) {
					ibBackendCoreException::Error(
						_("ibLockManager: failed to insert sys_lock row."));
				}
				acquired.push_back(newGuid);
			}

			q.Commit();
			return ibLockHandle(std::move(acquired), ownerGuid);
		}
		catch (const HeaderAlreadyThere&) {
			if (q.IsActiveTransaction())
				q.RollBack();
			// The other connection inserted the header and may still hold it.
			// The next attempt updates that row and waits with the transaction.
		}
		catch (const ibCoreException&) {
			if (q.IsActiveTransaction())
				q.RollBack();
			throw;
		}
	}

	ibBackendCoreException::Error(_("ibLockManager: the lock key stayed busy."));
	return ibLockHandle();
}

void ibLockManager::ReleaseRows(const std::vector<ibGuid>& lockGuids)
{
	// Every path that touches the lock table takes this - see ibLockManager::m_mtx: the
	// holder owns ONE connection and does not serialise the threads that ask it for one.
	std::lock_guard<std::recursive_mutex> serialised(m_mtx);

	if (lockGuids.empty())
		return;

	// The whole thing is inside the try, transaction calls included. They were
	// outside it, and BeginTransaction and Commit throw like everything else in
	// the database layer: a shutdown with a document still open crashed the web
	// server here twice on 2026-09-07, because the exception left this function,
	// left ibLockHandle::Release, and reached the destructor that called it,
	// where an escaping exception is std::terminate.
	//
	// Swallowing is the contract, not a shortcut: releasing a lock row is
	// best-effort, and a row that survives is collected by the zombie sweep.
	// The key header is not deleted here. The next acquire locks the same row.
	try {
		ibDatabaseQueryBuilder q(&m_lockHolder);
		if (!q.IsOpen())
			return;  // DB closed — process shutdown

		q.BeginTransaction();
		try {
			std::vector<ibQueryExprPtr> guids;
			guids.reserve(lockGuids.size());
			for (const auto& g : lockGuids)
				guids.push_back(ibConst(ibValue(g.str())));
			q.Execute(ibDelete(kSysLockTable, ibIn(ibCol(wxT("lockGuid")), std::move(guids))));
		}
		catch (const ibCoreException&) {
			if (q.IsActiveTransaction())
				q.RollBack();
			return;
		}
		q.Commit();
	}
	catch (const ibCoreException& err) {
		ibJournalInfo(wxT("lock"), wxT("release of %d row(s) did not complete: %s"),
			static_cast<int>(lockGuids.size()), err.GetErrorDescription());
	}
	catch (...) {
		ibJournalInfo(wxT("lock"), wxT("release of %d row(s) did not complete"),
			static_cast<int>(lockGuids.size()));
	}
}

void ibLockManager::OnSessionEnd(const ibGuid& sessionGuid)
{
	// Every path that touches the lock table takes this - see ibLockManager::m_mtx: the
	// holder owns ONE connection and does not serialise the threads that ask it for one.
	std::lock_guard<std::recursive_mutex> serialised(m_mtx);

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
	catch (const ibCoreException&) {
		if (q.IsActiveTransaction())
			q.RollBack();
		return;
	}
	q.Commit();
}

void ibLockManager::SweepOrphans(const std::vector<ibGuid>& liveSessionGuids)
{
	// Every path that touches the lock table takes this - see ibLockManager::m_mtx: the
	// holder owns ONE connection and does not serialise the threads that ask it for one.
	std::lock_guard<std::recursive_mutex> serialised(m_mtx);

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
	catch (const ibCoreException&) {
		return;   // transient DB error — the next sweep tick retries
	}

	for (const wxString& owner : orphans)
		OnSessionEnd(ibGuid(owner));
}

std::vector<ibLockSnapshotRow> ibLockManager::GetSnapshot() const
{
	// Every path that touches the lock table takes this - see ibLockManager::m_mtx: the
	// holder owns ONE connection and does not serialise the threads that ask it for one.
	std::lock_guard<std::recursive_mutex> serialised(m_mtx);

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
