#include "backend/lock/lockHandle.h"
#include "backend/lock/lockManager.h"
#include "backend/appData.h"   // ibApplicationData::GetLockManager

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

	// The handle forgets its rows first, and does not remember them again
	// whatever the database answers. Best-effort release: a row that already
	// vanished — cluster cleanup, a force-release from an administrator — is
	// not ours to chase, and a retry from a destructor would only fail again.
	const std::vector<ibGuid> rows = std::move(m_lockGuids);
	m_lockGuids.clear();

	// Nothing escapes. Release runs from the destructor and from a noexcept
	// move-assignment, and in both an exception leaving here is std::terminate
	// rather than an error anyone reads. The manager swallows its own already;
	// this is the boundary the language actually requires it at.
	try {
		if (auto* lm = ibApplicationData::GetLockManager())
			lm->ReleaseRows(rows);
	}
	catch (...) {
	}
}
