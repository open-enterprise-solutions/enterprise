#include "backend/lock/lockHolder.h"

#include "core/system.h"   // ibHostName

ibSingleLockHolder::ibSingleLockHolder(const ibString& displayName,
                                        const ibString& computer)
	: m_identity(ibNewUniqueGuid)
	, m_displayName(displayName)
	, m_computer(computer.IsEmpty() ? ibHostName() : computer)
{
}
