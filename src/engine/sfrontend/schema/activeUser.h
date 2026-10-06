#ifndef __ACTIVE_USER_H__
#define __ACTIVE_USER_H__

// ACTIVE USERS — the dialog of that name (frontend/win/dlgs/activeUser), as the server holds it: the base's sessions,
// as the cluster sees them, and the locks they hold. Shared by every mode — the designer's dialog and the runtime's
// showed the same two tables. (Kicking a session, the designer's own action there, is not a schema's: it is a
// command of the designer's protocol.)

#include "clientSchema.h"

class SFRONTEND_API ibSchemaActiveUser : public ibClientSchema {
public:

	// The configuration's right to active users — what the desktop's menu item stood on.
	virtual bool AccessRight(const ibSession& session) const override;

	// Sessions {User, Application, Type, Started, Computer, Session} and Locks {Namespace, Key, Mode, User,
	// Acquired, Lock} — the dialog's two tables, column for column.
	virtual void Build(const ibSession& session, ibDataNode& result) const override;
};

#endif
