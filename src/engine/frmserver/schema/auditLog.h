#ifndef __AUDIT_LOG_H__
#define __AUDIT_LOG_H__

// THE REGISTRATION JOURNAL — the menu item of that name (the desktop's OnAuditLog), as the server holds it: not a
// dialog but a tab, which its command opens (ibAuditLogDocument); its rows are read by the tab's own view. Shared by
// every mode — the designer's Administration and the runtime's opened the same tab.

#include "clientSchema.h"

class FRMSERVER_API ibSchemaAuditLog : public ibClientSchema {
public:

	// The configuration's right to active users — what the desktop's menu item stood on.
	virtual bool AccessRight(const ibSession& session) const override;

	// Nothing — the journal is a tab, opened by Open.
	virtual void Build(const ibSession& WXUNUSED(session), ibDataNode& WXUNUSED(result)) const override {}

	// Open — the journal's tab, as the desktop's menu item opened it, in the session's configuration.
	virtual std::function<void()> Command(const ibSession& session, s32 command,
		const ibDataNode& args, ibProtocolRefusal& refusal, wxString& error) const override;
};

#endif
