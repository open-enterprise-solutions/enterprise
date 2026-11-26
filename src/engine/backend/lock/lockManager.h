/////////////////////////////////////////////////////////////////////////////
// ibLockManager — the base's long-held pessimistic locks, kept in its lock
// file (ibLockFile: `sys.locks` in the base's folder, every process of the
// base holding it open).
//
// Use cases (driven from outside this layer):
//   - Form-open auto-acquire: held across many TXs until form close
//     (covered in Phase B.3 of the record-locks plan).
//   - The query's LOCK: keys on a table's VALUES, held until the
//     business transaction ends (its handle is that transaction's
//     ticket, ibDatabaseLayer::AddTicket).
//   - A job's claim, a compile's module, the web addresses' row.
//   - Admin force-release / "who holds what" snapshot (Phase B.5).
//
// The row a write touches is still serialized by the DB row lock
// (record-locks Phase 5-7). This holds values — keys a row may not
// have yet.
//
// One file per base, so every process of it — a file base's clients, a
// cluster's workers — sees the same locks, and none of them is a row of the
// base: the DBMS takes no part (record-locks.md, stage 19, measured: a lock
// kept in a table was one transaction and commit apiece, one at a time).
/////////////////////////////////////////////////////////////////////////////

#ifndef _IB_LOCK_MANAGER_H_
#define _IB_LOCK_MANAGER_H_

#include "backend/appDataCtorToken.h"
#include "backend/lock/lockTypes.h"
#include "backend/lock/lockHandle.h"
#include "backend/lock/lockHolder.h"

#include <condition_variable>
#include <cstdint>
#include <map>
#include <memory>
#include <mutex>
#include <set>
#include <vector>

class ibLockFile;

// Snapshot row — one per held lock. Returned by GetSnapshot
// for admin UI / /admin/locks endpoint (wired in Phase B.5).
struct BACKEND_API ibLockSnapshotRow {
	ibGuid       lockGuid;
	ibGuid       sessionGuid;
	ibString     namespaceName;
	ibString     keyData;          // the key: "field=<value hash>;…" (ibLockKey::Canonicalize)
	ibLockMode   lockMode;
	ibDateTime   acquiredAt;
	ibString     userName;
	ibString     computer;
};

class BACKEND_API ibLockManager {
public:
	// No public static accessor — `ibLockManager` is created exclusively
	// by `ibApplicationInstance` (ctor reaches it via `friend` below) and
	// reached through `ibApplicationInstance::GetLockManager()` which
	// returns nullptr pre-appData / post-appData. Mirrors the
	// session-registry and connection-pool ownership pattern: subsystems
	// do not own their own global state, they exist for the duration
	// of appData and only.

	// Acquire a batch of locks atomically — either all granted or
	// none (throws on conflict). Throws ibBackendLockException::
	// LockConflict if any item collides with another owner's lock —
	// their keys meet (ibLockKey::Overlap) and either is Exclusive — at
	// once, or after opts.timeoutMs of asking again when that is given;
	// throws ibBackendCoreException when the lock file cannot be used.
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
	// the file untouched.
	ibLockHandle Acquire(const std::vector<ibLockItem>& items,
	                      const ibLockOptions&           opts        = {},
	                      ibLockHolder*                  customHolder = nullptr);

	// Release a specific set of lock rows by guid. Called by
	// ibLockHandle::Release / dtor; not for direct end-user use.
	// Best-effort — missing rows (already cleaned up by sweep, force-
	// released by admin, etc.) are silent.
	void ReleaseRows(const std::vector<ibGuid>& lockGuids);

	// Hook for ibSessionRegistry::ProcessRemove — drops every lock
	// row owned by the dying session. No-op safe to call from anywhere.
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
	// so it cannot be a lock racing ahead of its own registration. (A
	// process that died is found sooner, by its slot's byte — lockFile.h.)
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
	~ibLockManager();

	// Construction restricted to ibApplicationInstance via the
	// ib::AppDataCtorToken gate — appData is the owning coordinator;
	// lifetime is tied to its ctor/dtor.
	explicit ibLockManager(ib::AppDataCtorToken owner);

private:
	ibLockManager(const ibLockManager&) = delete;
	ibLockManager& operator=(const ibLockManager&) = delete;

	// The base's lock file, opened on the first use: where it is (ResolveLockFile) is known once the base is opened,
	// after this is made.
	ibLockFile& File() const;

	// Records that the owner of slot `waiter` waits for the owner of `holder`, in the file, and answers whether the
	// holder, down the chain, waits for the waiter — a deadlock, refused at once, whichever processes the chain passes.
	bool WaitsForItself(int waiter, int holder);

	// What this process stops holding: the rows by their guids, an owner's every row — its slot let go with the last.
	void Forget(const std::set<ibString>& lockGuids);
	void ForgetOwner(const ibString& owner);

	ibApplicationInstance* const m_applicationInstance;   // whose base's locks these are

	mutable std::unique_ptr<ibLockFile> m_file;

	// Every call takes it: the file is read, checked and written under it (for microseconds — the waiting is done
	// outside it). RECURSIVE, because SweepOrphans and OnSessionEnd share what they forget.
	mutable std::recursive_mutex m_mtx;

	// ⭐ WHAT THIS PROCESS HOLDS — a row's owner and table by its guid, an owner's slot and number of rows: a release finds
	// its rows' regions without reading every one, and an owner's slot goes with its last row (under m_mtx).
	std::map<ibString, std::pair<ibString, ibString>> m_rows;     // lockGuid → (owner, namespace)
	std::map<ibString, std::pair<int, std::size_t>>   m_owners;   // owner → (slot, rows)

	// The releases this process made (under m_mtx), and the wait that sleeps until their count moves: a lock let go
	// here wakes the owners waiting for it at once; one let go by another process wakes them by its slot's byte
	// (ibLockFile::WaitForRelease). Counted, so a release between a refused attempt and the sleep is not slept through.
	std::uint64_t                m_releaseCount = 0;
	std::condition_variable_any  m_releaseWake;
};

#endif // _IB_LOCK_MANAGER_H_
