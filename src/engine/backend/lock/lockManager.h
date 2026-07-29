/////////////////////////////////////////////////////////////////////////////
// ibLockManager — process-wide singleton coordinating long-held
// pessimistic locks through the sys_lock app-table.
//
// Use cases (driven from outside this layer):
//   - Form-open auto-acquire: held across many TXs until form close
//     (covered in Phase B.3 of the record-locks plan).
//   - Script-driven explicit lock via `New DataLock` (Phase B.4).
//   - Admin force-release / "who holds what" snapshot (Phase B.5).
//
// What this is NOT for: short TX-scoped writer serialization — that's
// already covered by DB-level row locks (SELECT FOR UPDATE / WITH
// LOCK) in record-locks Phase 5-7. sys_lock is exclusively for locks
// that need to live longer than a transaction.
//
// Cluster-aware via the DB table — multiple wenterprise-server.exe
// processes on one .fdb see each other's sys_lock rows. Cleanup on
// session end / zombie sweep handled by ibSessionRegistry (Phase B.2).
//
// See docs/record-locks.md.
/////////////////////////////////////////////////////////////////////////////

#ifndef _IB_LOCK_MANAGER_H_
#define _IB_LOCK_MANAGER_H_

#include "backend/appDataCtorToken.h"
#include "backend/lock/lockTypes.h"
#include "backend/lock/lockHandle.h"
#include "backend/lock/lockHolder.h"
#include "backend/databaseLayer/connectionHolder.h"

#include <mutex>
#include <vector>

// Snapshot row — one per held sys_lock entry. Returned by GetSnapshot
// for admin UI / /admin/locks endpoint (wired in Phase B.5).
struct BACKEND_API ibLockSnapshotRow {
	ibGuid       lockGuid;
	ibGuid       sessionGuid;
	wxString     namespaceName;
	wxString     keyData;          // human-readable canonical key
	ibLockMode   lockMode;
	wxDateTime   acquiredAt;
	wxString     userName;
	wxString     computer;
};

class BACKEND_API ibLockManager {
public:
	// No public static accessor — `ibLockManager` is created exclusively
	// by `ibApplicationData` (ctor reaches it via `friend` below) and
	// reached through `ibApplicationData::GetLockManager()` which
	// returns nullptr pre-appData / post-appData. Mirrors the
	// session-registry and connection-pool ownership pattern: subsystems
	// do not own their own global state, they exist for the duration
	// of appData and only.

	// Acquire a batch of locks atomically — either all granted or
	// none (throws on conflict). Throws ibBackendLockException::
	// LockConflict if any item collides with another owner's lock;
	// throws ibBackendCoreException for DB / type errors.
	//
	// Owner identity (the holder) decides what counts as same-owner
	// re-entry:
	//   - customHolder == nullptr (the default) → owner = active
	//     ibSession::Current(). Standard form-open / script-driven
	//     case.
	//   - customHolder != nullptr → owner = customHolder->Identity().
	//     For long-running jobs, system coordinators, tests — any
	//     owner not tied to a user session.
	//
	// Same-owner re-entry on (namespace, keyHash) is not a conflict:
	// same-mode = no-op, S→X = in-place upgrade when opts.allowUpgrade
	// (default true), X→S = no-op (X dominates).
	//
	// Caller may pass empty items vector — returns an empty handle,
	// no DB hit.
	ibLockHandle Acquire(const std::vector<ibLockItem>& items,
	                      const ibLockOptions&           opts        = {},
	                      ibLockHolder*                  customHolder = nullptr);

	// Release a specific set of lock rows by guid. Called by
	// ibLockHandle::Release / dtor; not for direct end-user use.
	// Best-effort — missing rows (already cleaned up by sweep, force-
	// released by admin, etc.) are silent.
	void ReleaseRows(const std::vector<ibGuid>& lockGuids);

	// Hook for ibSessionRegistry::ProcessRemove — drops every lock
	// row owned by the dying session in one DELETE. Wired in Phase
	// B.2. No-op safe to call from anywhere.
	void OnSessionEnd(const ibGuid& sessionGuid);

	// Drop every lock row whose owner is NOT in the live session list —
	// the caller passes what sys_session currently holds, this drops the
	// rest. Called from the session sweep on every tick.
	//
	// Stated as "who is ALIVE" rather than "who just died", and that is
	// the whole point. Keying cleanup on the sessions a sweep had just
	// removed left a hole nothing ever closed: once the session row was
	// gone — its owner force-killed, the row swept by another process, a
	// crash between the two deletes — its locks had no one left to
	// mention them, and they stayed FOREVER. A lock nobody can release
	// is not a stale row: it is a document that cannot be opened again
	// on any machine, with no owner to name in the message.
	//
	// A lock whose session is missing is unambiguous: a session row is
	// written when the session starts, long before it can take a lock,
	// so it cannot be a lock racing ahead of its own registration.
	void SweepOrphans(const std::vector<ibGuid>& liveSessionGuids);

	// Cluster snapshot for admin UI / /admin/locks. Returns a copy;
	// the caller may iterate freely without holding the manager
	// lock. Wired in Phase B.5.
	std::vector<ibLockSnapshotRow> GetSnapshot() const;

	// Destructor must be public so std::unique_ptr<ibLockManager>'s
	// default_delete can fire from inside `<memory>` (which is not a
	// friend). ctor stays private — that's where the owner-only rule
	// matters; allowing delete on a managed pointer doesn't reopen the
	// construction path.
	~ibLockManager() = default;

	// Construction restricted to ibApplicationData via the
	// ib::AppDataCtorToken gate — appData is the owning coordinator;
	// lifetime is tied to its ctor/dtor.
	explicit ibLockManager(ib::AppDataCtorToken);

private:
	ibLockManager(const ibLockManager&) = delete;
	ibLockManager& operator=(const ibLockManager&) = delete;

	// Dedicated connection-holder so Acquire-TX never piggybacks on
	// the caller's business TX — lock rows must commit immediately
	// (so other processes see them) regardless of whether the
	// caller's business TX commits later. See connection-pool.md
	// for the holder/scope discipline.
	mutable ibSingleConnectionHolder m_lockHolder;

	// Acquire is multi-step (SELECT-then-INSERT inside its own TX);
	// the holder discipline already serialises DB ops on the holder's
	// connection. The mutex here only protects in-process bookkeeping
	// (counters / diagnostics if added) — kept for future use.
	mutable std::mutex m_mtx;
};

#endif // _IB_LOCK_MANAGER_H_
