#include "backend/lock/lockHandle.h"
#include "backend/lock/lockManager.h"

ibLockHandle::ibLockHandle() = default;

ibLockHandle::ibLockHandle(std::vector<ibGuid> lockGuids, const ibGuid& sessionGuid)
	: m_lockGuids(std::move(lockGuids))
	, m_sessionGuid(sessionGuid)
{
}

ibLockHandle::ibLockHandle(ibLockHandle&& other) noexcept
	: m_lockGuids(std::move(other.m_lockGuids))
	, m_sessionGuid(other.m_sessionGuid)
{
	other.m_lockGuids.clear();
}

ibLockHandle& ibLockHandle::operator=(ibLockHandle&& other) noexcept
{
	if (this != &other) {
		// Release whatever rows we currently own before adopting the
		// other handle's rows — avoids leaking sys_lock rows when a
		// caller re-assigns into a non-empty handle.
		Release();
		m_lockGuids   = std::move(other.m_lockGuids);
		m_sessionGuid = other.m_sessionGuid;
		other.m_lockGuids.clear();
	}
	return *this;
}

ibLockHandle::~ibLockHandle()
{
	Release();
}

void ibLockHandle::Release()
{
	if (m_lockGuids.empty())
		return;
	// Manager takes the row list, issues DELETE, clears the vector
	// regardless of DB outcome — best-effort release; if the row
	// already vanished (cluster cleanup, force-release from admin)
	// we still want our handle to forget it.
	ibLockManager::Instance().ReleaseRows(m_lockGuids);
	m_lockGuids.clear();
}
