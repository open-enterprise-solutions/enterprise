#include "backend/lock/lockHolder.h"

#include <wx/utils.h>   // wxGetHostName

ibSingleLockHolder::ibSingleLockHolder(const ibString& displayName,
                                        const ibString& computer)
	: m_identity(wxNewUniqueGuid)
	, m_displayName(displayName)
	, m_computer(computer.IsEmpty() ? ibString(wxGetHostName()) : computer)
{
}
