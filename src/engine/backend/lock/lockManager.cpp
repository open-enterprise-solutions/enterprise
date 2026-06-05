/////////////////////////////////////////////////////////////////////////////
// ibLockManager implementation — sys_lock app-table coordination.
//
// Acquire algorithm (per item, inside a dedicated TX on the lock
// holder so the row commit doesn't ride on the caller's business TX):
//
//   1. Compute keyHash = SHA-256 of canonical serialization.
//   2. SELECT sessionGuid, userName, lockMode FROM sys_lock
//        WHERE namespace = ? AND keyHash = ?  <RowLockHint>
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
#include "backend/databaseLayer/databaseLayer.h"
#include "backend/databaseLayer/databaseErrorCodes.h"
#include "backend/session/session.h"
#include "backend/userInfo.h"

#include <wx/datetime.h>

namespace {

// Table name kept here so call sites don't drift. Mirrors the
// session-registry's sys_session table naming.
const wxString kSysLockTable = wxT("sys_lock");

// Build the bulk-DELETE SQL for a list of lock guids. PG / FB / MySQL
// / SQLite all accept "WHERE lockGuid IN ('a','b','c')" — keeps the
// implementation portable without per-driver branching.
wxString BuildDeleteByGuidsSQL(const std::vector<ibGuid>& guids)
{
	wxString sql = wxT("DELETE FROM ") + kSysLockTable + wxT(" WHERE lockGuid IN (");
	bool first = true;
	for (const auto& g : guids) {
		if (!first) sql += wxT(",");
		first = false;
		// Wrap each guid in quotes — guids are well-formed
		// hex+dashes, no escape concern.
		sql += wxT("'") + g.str() + wxT("'");
	}
	sql += wxT(");");
	return sql;
}

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
	ibConnectionScope scope(&m_lockHolder);
	if (!scope || !scope->IsOpen()) {
		ibBackendCoreException::Error(
			_("ibLockManager::Acquire - database is not open."));
	}

	ibDatabaseLayer::ibTxOptions txOpts;
	txOpts.noWait = !opts.wait;
	scope.SafeBeginTransaction(txOpts);

	std::vector<ibGuid> acquired;

	try {
		const wxString hint     = scope->RowLockHint();
		const wxString nowait   = (!opts.wait) ? scope->NoWaitClause() : wxString();
		const wxString hintClause = (hint.IsEmpty()   ? wxString() : wxT(" ") + hint)
		                          + (nowait.IsEmpty() ? wxString() : wxT(" ") + nowait);

		const wxString selectSqlTpl = wxT("SELECT sessionGuid, userName, lockMode FROM ")
		                            + kSysLockTable
		                            + wxT(" WHERE namespace = ? AND keyHash = ?")
		                            + hintClause + wxT(";");

		const wxString insertSqlTpl = wxT("INSERT INTO ") + kSysLockTable
		                            + wxT(" (lockGuid, sessionGuid, namespace, keyHash,")
		                            + wxT(" keyData, lockMode, acquiredAt, userName, computer)")
		                            + wxT(" VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?);");

		const wxString updateModeSqlTpl = wxT("UPDATE ") + kSysLockTable
		                                + wxT(" SET lockMode = ? WHERE lockGuid = ?;");

		const wxString ownerGuidStr = ownerGuid.str();
		const wxDateTime nowUtc = wxDateTime::UNow();

		for (const auto& item : items) {
			// Hash + canonical now owned by the item (lazy-cached) —
			// lockManager no longer composes them locally. See
			// lockTypes.h ibLockItem.
			const wxString& keyHash = item.KeyHash();
			const wxString& keyData = item.KeyData();

			// ---- Conflict check ----
			ibStatementGuard selectSt(scope.get(),
				scope->PrepareStatement(selectSqlTpl));
			if (!selectSt) {
				ibBackendCoreException::Error(
					_("ibLockManager: failed to prepare lock-conflict query."));
			}
			selectSt->SetParamString(1, item.namespaceName);
			selectSt->SetParamString(2, keyHash);
			ibDatabaseResultSet* rs = selectSt->RunQueryWithResults();
			if (rs == nullptr) {
				ibBackendCoreException::Error(
					_("ibLockManager: lock-conflict query returned no result set."));
			}

			bool       ownExisting = false;
			ibGuid     ownExistingGuid;
			ibLockMode ownExistingMode = ibLockMode::Shared;
			bool       conflictHit = false;
			wxString   conflictUser;

			while (rs->Next()) {
				const wxString otherSess  = rs->GetResultString(1);
				const wxString otherUser  = rs->GetResultString(2);
				const ibLockMode otherMode =
					static_cast<ibLockMode>(rs->GetResultInt(3));

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
			scope->CloseResultSet(rs);

			if (conflictHit) {
				// Roll back any inserts we did earlier in this batch.
				scope.SafeRollBackTransaction();
				ibBackendLockException::LockConflictThrow(item.namespaceName, conflictUser);
			}

			// ---- Acquire ----
			if (ownExisting) {
				if (item.lockMode > ownExistingMode && opts.allowUpgrade) {
					// S → X upgrade in place. No new row added to
					// the handle — caller already owns this lock,
					// just at a higher level now.
					ibStatementGuard updSt(scope.get(),
						scope->PrepareStatement(updateModeSqlTpl));
					if (updSt) {
						updSt->SetParamInt(1, static_cast<int>(item.lockMode));
						updSt->SetParamString(2, ownExistingGuid.str());
						updSt->RunQuery();
					}
				}
				// Same-mode or X→S → no-op.
				continue;
			}

			// Fresh INSERT.
			const ibGuid newGuid = wxNewUniqueGuid;
			ibStatementGuard insSt(scope.get(),
				scope->PrepareStatement(insertSqlTpl));
			if (!insSt) {
				scope.SafeRollBackTransaction();
				ibBackendCoreException::Error(
					_("ibLockManager: failed to prepare lock-insert query."));
			}
			insSt->SetParamString(1, newGuid.str());
			insSt->SetParamString(2, ownerGuidStr);
			insSt->SetParamString(3, item.namespaceName);
			insSt->SetParamString(4, keyHash);

			// keyData is the canonical human-readable representation
			// (sorted by name). Stored as text — sys_lock.keyData
			// VARCHAR is wide enough for typical keys; if a future
			// register lock has hundreds of dim values, swap to BLOB.
			insSt->SetParamString(5, keyData);
			insSt->SetParamInt   (6, static_cast<int>(item.lockMode));
			// TIMESTAMP column — use SetParamDate, not SetParamString.
			// FB driver AV's on SetParamString for TIMESTAMP-typed
			// columns (it tries to interpret the bytes as date raw
			// data). Mirrors how sys_session binds its started /
			// lastActive (sessionRegistry.cpp).
			insSt->SetParamDate  (7, nowUtc);
			insSt->SetParamString(8, ownerName);
			insSt->SetParamString(9, ownerComputer);

			if (insSt->RunQuery() == DATABASE_LAYER_QUERY_RESULT_ERROR) {
				scope.SafeRollBackTransaction();
				ibBackendCoreException::Error(
					_("ibLockManager: failed to insert sys_lock row."));
			}
			acquired.push_back(newGuid);
		}
	}
	catch (const ibBackendException&) {
		// SafeRollBack already fired in the throw paths above; this
		// catches any other ibBackendException from the SQL helpers
		// so we always rollback cleanly before propagating.
		if (scope.HasActiveTransaction())
			scope.SafeRollBackTransaction();
		throw;
	}

	scope.SafeCommitTransaction();

	return ibLockHandle(std::move(acquired), ownerGuid);
}

void ibLockManager::ReleaseRows(const std::vector<ibGuid>& lockGuids)
{
	if (lockGuids.empty())
		return;

	ibConnectionScope scope(&m_lockHolder);
	if (!scope || !scope->IsOpen())
		return;  // best-effort — silent fail if DB closed (process shutdown)

	scope.SafeBeginTransaction();
	try {
		scope->RunQuery(BuildDeleteByGuidsSQL(lockGuids));
	}
	catch (const ibBackendException&) {
		if (scope.HasActiveTransaction())
			scope.SafeRollBackTransaction();
		// Swallow — Release is best-effort. Stale rows get cleaned
		// up by the zombie sweep eventually.
		return;
	}
	scope.SafeCommitTransaction();
}

void ibLockManager::OnSessionEnd(const ibGuid& sessionGuid)
{
	if (!sessionGuid.isValid())
		return;

	ibConnectionScope scope(&m_lockHolder);
	if (!scope || !scope->IsOpen())
		return;

	scope.SafeBeginTransaction();
	try {
		ibStatementGuard st(scope.get(), scope->PrepareStatement(
			wxT("DELETE FROM ") + kSysLockTable + wxT(" WHERE sessionGuid = ?;")));
		if (st) {
			st->SetParamString(1, sessionGuid.str());
			st->RunQuery();
		}
	}
	catch (const ibBackendException&) {
		if (scope.HasActiveTransaction())
			scope.SafeRollBackTransaction();
		return;
	}
	scope.SafeCommitTransaction();
}

void ibLockManager::OnZombieSweep(const std::vector<ibGuid>& deadSessionGuids)
{
	for (const auto& g : deadSessionGuids)
		OnSessionEnd(g);
	// One-DELETE-per-zombie keeps the implementation trivial; if
	// sweep ever runs on hundreds of zombies at once, batch into
	// a single "WHERE sessionGuid IN (...)" DELETE.
}

std::vector<ibLockSnapshotRow> ibLockManager::GetSnapshot() const
{
	std::vector<ibLockSnapshotRow> rows;

	ibConnectionScope scope(&m_lockHolder);
	if (!scope || !scope->IsOpen())
		return rows;

	// Read-only — no TX needed; default isolation gives us latest
	// committed state from other processes.
	ibDatabaseResultSet* rs = scope->RunQueryWithResults(
		wxT("SELECT lockGuid, sessionGuid, namespace, keyData, lockMode,")
		wxT(" acquiredAt, userName, computer FROM ") + kSysLockTable + wxT(";"));
	if (rs == nullptr)
		return rows;

	while (rs->Next()) {
		ibLockSnapshotRow r;
		r.lockGuid      = ibGuid(rs->GetResultString(1));
		r.sessionGuid   = ibGuid(rs->GetResultString(2));
		r.namespaceName = rs->GetResultString(3);
		r.keyData       = rs->GetResultString(4);
		r.lockMode      = static_cast<ibLockMode>(rs->GetResultInt(5));
		r.acquiredAt    = rs->GetResultDate(6);
		r.userName      = rs->GetResultString(7);
		r.computer      = rs->GetResultString(8);
		rows.push_back(std::move(r));
	}
	scope->CloseResultSet(rs);
	return rows;
}
