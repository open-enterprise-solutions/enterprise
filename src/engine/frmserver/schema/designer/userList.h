#ifndef __SCHEMA_USER_LIST_H__
#define __SCHEMA_USER_LIST_H__

// THE BASE'S USERS — the designer's Administration → Users (frontend/win/dlgs, ibDialogUserList and ibDialogUserItem), as
// the server holds them: the calls those dialogs made on ibUserInfo, and what the item's OK checked before it saved. The
// dialogs themselves — the list, the user's two pages, the roles and the language offered — are the client's, copied as
// they were. A stored password stays here: the client is told whether there is one, and sends a new one to be hashed.

#include "frmserver/schema/clientSchema.h"

class FRMSERVER_API ibSchemaUserList : public ibClientSchema {
public:

	// The configuration's right to administer the data — what the desktop's menu item stood on.
	virtual bool AccessRight(const ibSession& session) const override;

	// Users {Guid, Name, FullName} — the list's rows (ibUserInfo::ListAll), with the configuration's Roles {Guid, Name,
	// Title, Mode} and Languages {Guid, Title, Code} a record is chosen from; with `Args: {Guid}`, that one's User {Guid,
	// Name, FullName, Password — whether one is stored, Language, Roles} (ibUserInfo::Read). A list that cannot be read
	// is thrown, as ListAll throws it: an unreadable list is not an empty one.
	virtual void Build(const ibSession& session, const ibDataNode& args, ibDataNode& result) const override;

	// Save {Guid, Name, FullName, Language, Roles[, Password]} — the item's OK: the language and the roles found in the
	// configuration by their guids, the password hashed (absent: the stored one kept), and refused when no user would
	// keep a role with the administration rights; Remove {Guid} — the list's Delete. Done in the asking.
	virtual std::function<void()> Command(const ibSession& session, std::int32_t command,
		const ibDataNode& args, ibProtocol::ibRefusal& refusal, wxString& error) const override;
};

#endif
