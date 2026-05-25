#include "backend/lock/lockHolder.h"

#include <wx/utils.h>   // wxGetHostName

ibSingleLockHolder::ibSingleLockHolder(const wxString& displayName,
                                        const wxString& computer)
	: m_identity(wxNewUniqueGuid)
	, m_displayName(displayName)
	, m_computer(computer.IsEmpty() ? wxGetHostName() : computer)
{
}
