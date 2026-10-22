#ifndef _FRMCLIENT_BACKEND_USER_INFO_H__
#define _FRMCLIENT_BACKEND_USER_INFO_H__

// ibUserInfo — a user of the base, as the engine's (backend/userInfo.h) holds one: the same record, the same calls, the
// designer's users dialogs (ibDialogUserList, ibDialogUserItem) copied onto them. Behind them is the server's schema
// (ibProtocol::ibSchema::ibKind::UserList), where sys_user is. One thing is not the desktop's: the stored password stays there —
// a record read says whether there is one (IsSetPassword), and one saved carries a new one only when it was typed.

#include "frmclient/backend/backend_core.h"
#include "core/roleCompositionMode.h"   // ibRoleCompositionMode — how a role combines

#include <vector>

#include <wx/string.h>

class ibGuid;
class ibMetaData;

struct FRMCLIENT_API ibUserInfo {

	struct ibUserRole {
		wxString m_strRoleGuid;
		wxString m_strRoleName;
		ibRoleID m_miRoleId = wxNOT_FOUND;
		// HOW this role combines with the user's others (ibRoleCompositionMode) — the server writes what its
		// configuration says; the guid is what it is found by.
		ibRoleCompositionMode m_mode = ibRoleCompositionMode_Union;
	};

	bool IsOk() const { return !m_strUserGuid.IsEmpty(); }

	bool IsSetPassword() const { return !m_strUserName.IsEmpty() && m_bPasswordSet; }
	bool IsSetRole()     const { return m_roleArray.size() > 0; }
	bool IsSetLanguage() const { return !m_strLanguageGuid.IsEmpty(); }

	//User info
	wxString m_strUserGuid;
	wxString m_strUserName;
	wxString m_strUserFullName;
	// Saved: the password typed — sent, to be hashed by the server, only when m_bPasswordTouched; untouched, the stored
	// one is kept. Read: empty — the stored one is never sent; m_bPasswordSet says whether there is one.
	wxString m_strUserPassword;
	bool m_bPasswordSet = false;
	bool m_bPasswordTouched = false;

	//Role info
	std::vector<ibUserRole> m_roleArray;

	//Language info
	wxString m_strLanguageGuid;
	wxString m_strLanguageName;
	wxString m_strLanguageCode;

	// Compact projection of a sys_user row — guid + name + fullName: the list's rows.
	struct Brief {
		wxString m_strUserGuid;
		wxString m_strUserName;
		wxString m_strUserFullName;
	};

	// The server's schema asked. Read returns a default-constructed (IsOk() == false) record on miss; Save writes the
	// record keyed by m_strUserGuid — false when the server refused it, its reason shown to the person (a user whose
	// roles would leave nobody an administrator among them); Delete removes the record keyed by guid.
	static ibUserInfo Read(const ibGuid& userGuid);
	static bool       Save(const ibUserInfo& info);
	static bool       Delete(const ibGuid& userGuid);

	// What a record is chosen from — the configuration's roles and languages, which the client does not hold: listed by
	// the server beside the users, as the desktop's item dialog listed them off the configuration.
	struct RoleBrief {
		wxString m_strRoleGuid;
		wxString m_strRoleName;
		wxString m_strRoleTitle;   // its synonym — what the list shows
		ibRoleCompositionMode m_mode = ibRoleCompositionMode_Union;
	};

	struct LanguageBrief {
		wxString m_strLanguageGuid;
		wxString m_strLanguageTitle;
		wxString m_strLanguageCode;
		bool m_default = false;   // the configuration's own language (its DefaultLanguage)
	};

	// ListAll — every row projected to Brief. A list the server could not read is refused to the person, and comes back
	// empty.
	static std::vector<Brief>  ListAll();
	// …and the roles and the languages a record is chosen from — the languages every translated text is written in too
	// (ibDialogTranslateConstructor), asked of the server's schema Languages in every mode: of the configuration
	// `metaData` is the door to (the tab that shows it); null — the session's.
	static std::vector<RoleBrief>     ListRoles();
	static std::vector<LanguageBrief> ListLanguages(const ibMetaData* metaData);
};

#endif
