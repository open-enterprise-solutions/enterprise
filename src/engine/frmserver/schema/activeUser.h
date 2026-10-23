#ifndef __ACTIVE_USER_H__
#define __ACTIVE_USER_H__

// ACTIVE USERS — the dialog of that name (frontend/win/dlgs/activeUser), as the server holds it: the base's sessions,
// as the cluster sees them, and the locks they hold. Shared by every mode — the designer's dialog and the runtime's
// showed the same two tables. What the designer's dialog alone did there — a session kicked or its clients reloaded, a
// lock released by force — are its commands, refused to any other session.

#include "clientSchema.h"

class FRMSERVER_API ibSchemaActiveUser : public ibClientSchema {
public:

	// The configuration's right to active users — what the desktop's menu item stood on.
	virtual bool AccessRight(const ibSession& session) const override;

	// Sessions {User, Application, Type, Started, Computer, Session} and Locks {Namespace, Key, Mode, User,
	// Acquired, Lock} — the dialog's two tables, column for column.
	virtual void Build(const ibSession& session, const ibDataNode& args, ibDataNode& result) const override;

	// The dialog's context menus, the designer's (OnKickSelected, OnReloadSelected, OnForceReleaseSelected there):
	// Kick {Session}, Reload {Session} — not the asking session's own; Release {Lock}. Done in the asking.
	virtual std::function<void()> Command(const ibSession& session, std::int32_t command,
		const ibDataNode& args, ibProtocol::ibRefusal& refusal, wxString& error) const override;
};

#endif
