#ifndef __IB_USER_INFO_H__
#define __IB_USER_INFO_H__

// ibUserInfo — profile of an authenticated user inside the OES
// information base: name, GUID, password hash, role memberships,
// language. Process-local — every session that authenticates a user
// carries one (ibSession::m_userInfo); other GUIs (designer's Active
// Users, admin tools) display copies built from sys_session /
// sys_user reads. Was named ibApplicationDataUserInfo while the
// singleton ibApplicationData owned the only copy; the registry
// refactor distributed ownership, the name is now scope-neutral.

#include "backend/backend.h"
#include "backend/backend_core.h"

#include <vector>

#include <wx/string.h>
#include <wx/buffer.h>

class ibGuid;
class ibReaderMemory;
class ibWriterMemory;

struct BACKEND_API ibUserInfo {

	struct ibUserRole {
		wxString m_strRoleGuid;
		wxString m_strRoleName;
		ibRoleID m_miRoleId = wxNOT_FOUND;
	};

	bool IsOk() const { return !m_strUserGuid.IsEmpty(); }

	bool IsSetPassword() const { return !m_strUserName.IsEmpty() && !m_strUserPassword.IsEmpty(); }
	bool IsSetRole()     const { return m_roleArray.size() > 0; }
	bool IsSetLanguage() const { return !m_strLanguageGuid.IsEmpty() && !m_strLanguageCode.IsEmpty(); }

	//User info
	wxString m_strUserGuid;
	wxString m_strUserName;
	wxString m_strUserFullName;
	wxString m_strUserPassword;

	//Role info
	std::vector<ibUserRole> m_roleArray;

	//Language info
	wxString m_strLanguageGuid;
	wxString m_strLanguageName;
	wxString m_strLanguageCode;

	// Compact projection of a sys_user row — guid + name + fullName.
	// Used by login dialogs (combobox source) and admin UIs that only
	// need the visible columns, never the role / language / password
	// blob. Avoids reading binaryData for every row in the table.
	struct Brief {
		wxString m_strUserGuid;
		wxString m_strUserName;
		wxString m_strUserFullName;
	};

	// DB I/O against sys_user via the global db_query layer.
	// Read returns a default-constructed (IsOk() == false) record on miss
	// or invalid input; Save does an upsert keyed by m_strUserGuid.
	static ibUserInfo Read(const ibGuid& userGuid);
	static ibUserInfo Read(const wxString& userName);
	static bool       Save(const ibUserInfo& info);
	// Remove the sys_user row keyed by guid. True on success (or no-op miss).
	static bool       Delete(const ibGuid& userGuid);

	// Table-wide queries.
	// HasAny  — `SELECT 1 FROM sys_user LIMIT 1` semantics; true on any row.
	//           Used to distinguish open-access mode (empty sys_user) from
	//           normal auth flow.
	// ListAll — every row projected to Brief. Cheap by design — does NOT
	//           crack the binaryData blob.
	static bool                HasAny();
	static std::vector<Brief>  ListAll();

	// Buffer-level serialization — header + role/language/password chunks.
	// Used by configuration export/import paths that aggregate every
	// sys_user row into a single migration blob.
	void              Serialize(ibWriterMemory& writer) const;
	static ibUserInfo Deserialize(ibReaderMemory& reader);
};

#endif
