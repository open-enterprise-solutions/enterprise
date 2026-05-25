/////////////////////////////////////////////////////////////////////////////
// ibLockHandle — RAII owner for a batch of sys_lock rows.
//
// ibLockManager::Acquire returns a handle; the handle's dtor releases
// all rows it owns (DELETE FROM sys_lock WHERE lockGuid IN (...)).
// Move-only — there is exactly one owner of each lock-row set at any
// time. Released early via explicit Release().
//
// See docs/record-locks.md.
/////////////////////////////////////////////////////////////////////////////

#ifndef _IB_LOCK_HANDLE_H_
#define _IB_LOCK_HANDLE_H_

#include "backend/backend.h"
#include "backend/guid.h"

#include <vector>

class BACKEND_API ibLockHandle {
public:
	ibLockHandle();
	~ibLockHandle();

	// Move-only: a handle is the sole owner of its lock rows.
	ibLockHandle(ibLockHandle&& other) noexcept;
	ibLockHandle& operator=(ibLockHandle&& other) noexcept;
	ibLockHandle(const ibLockHandle&) = delete;
	ibLockHandle& operator=(const ibLockHandle&) = delete;

	// True when this handle owns at least one acquired lock row.
	bool IsValid() const { return !m_lockGuids.empty(); }

	// Explicit early release. Called automatically by dtor.
	// Idempotent — safe to call twice, second call is a no-op.
	void Release();

	// Access to the underlying lock-row IDs — for ibLockManager and
	// for tests. End-user code should not poke these directly.
	const std::vector<ibGuid>& LockGuids() const { return m_lockGuids; }

private:
	friend class ibLockManager;

	// Only ibLockManager constructs a non-empty handle (via Acquire).
	explicit ibLockHandle(std::vector<ibGuid> lockGuids, const ibGuid& sessionGuid);

	std::vector<ibGuid> m_lockGuids;
	ibGuid              m_sessionGuid;
};

#endif // _IB_LOCK_HANDLE_H_
