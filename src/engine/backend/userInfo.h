#ifndef __IB_USER_INFO_H__
#define __IB_USER_INFO_H__

// ibUserInfo — profile of an authenticated user inside the OES
// information base: name, GUID, password hash, role memberships,
// language. Process-local — every session that authenticates a user
// carries one (ibSession::m_userInfo); other GUIs (designer's Active
// Users, admin tools) display copies built from sys_session /
// sys_user reads. Was named ibApplicationDataUserInfo while the
// singleton ibApplicationInstance owned the only copy; the registry
// refactor distributed ownership, the name is now scope-neutral.

#include "backend/backend.h"
#include "backend/backend_core.h"
#include "backend/roleHelper.h"   // ibRoleCompositionMode — how a role combines (the role layer owns it)

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
		// HOW this role combines with the user's others (ibRoleCompositionMode) — it travels WITH the
		// membership, in the same chunk as the id, so a reader gets the identifier and how to compare
		// it in one read and resolves nothing. A row written before this field ends after the id and
		// the entry keeps Union, which is the behaviour that row had.
		ibRoleCompositionMode m_mode = ibRoleCompositionMode_Union;
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
	// A name is one account: "Admin" and "admin" are the same name.
	static wxString   NameKey(const wxString& userName);
	// The spelling a person should read. A name that is empty or only spaces is "(blank)".
	static wxString   NameAsShown(const wxString& userName);
	// How many accounts fold to this name. An empty name is not a name, so the count is 0
	// and a blank cannot be used to sign in.
	static unsigned   CountByName(const wxString& userName);
	// The accounts that fold to one name, as a person would list them ("Admin / admin").
	// Empty when every name is distinct.
	static wxString   SharedNames();
	// The sentence for the journal and the user list when SharedNames() is not empty.
	// Empty when every name is distinct.
	static wxString   SharedNameWarning();
	// True when saving this account would take a folded name that already belongs to
	// somebody else. An existing row may keep the name it already has, even while
	// another row still shares it, so that row can be edited or renamed.
	static bool       NameChangeConflicts(const ibUserInfo& info);
	static ibUserInfo Read(const ibGuid& userGuid);
	static ibUserInfo Read(const wxString& userName);
	// Saved, the name already belongs to someone else, or the write failed.
	// A conflict and a database error are not the same answer.
	enum class SaveReason { Saved, NameTaken, Failed };
	static SaveReason Save(const ibUserInfo& info);
	// Remove the sys_user row keyed by guid. True on success (or no-op miss).
	static bool       Delete(const ibGuid& userGuid);

	// Table-wide queries.
	// HasAny — true when sys_user has a row. A failure to read the table is
	// thrown: an unreadable list is not an empty one, and login must not
	// treat it as open access.
	// ListAll — every row projected to Brief. Cheap by design — does NOT
	//           crack the binaryData blob. A failure to read is thrown,
	//           the same as HasAny: an empty vector means the table was
	//           read and had no rows.
	static bool                HasAny();
	static std::vector<Brief>  ListAll();

	// Buffer-level serialization — header + role/language/password chunks.
	// Used by configuration export/import paths that aggregate every
	// sys_user row into a single migration blob.
	void              Serialize(ibWriterMemory& writer) const;
	static ibUserInfo Deserialize(ibReaderMemory& reader);
};

#endif
