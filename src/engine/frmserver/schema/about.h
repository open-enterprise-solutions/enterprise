#ifndef __ABOUT_H__
#define __ABOUT_H__

// ABOUT — the dialog of that name (frmclient/win/dlgs/about.h), as the server holds it: what the client works in — the
// build, the base, the application, the user, the locale, the plugins loaded. Shared by every mode — the designer's
// Help and the runtime's showed the same dialog.

#include "clientSchema.h"

class FRMSERVER_API ibSchemaAbout : public ibClientSchema {
public:

	// Build, Picture (the configuration's), Database (none — no base), Application, User, Locale and Plugins {Name,
	// Version} — the dialog's rows, row for row.
	virtual void Build(const ibSession& session, const ibDataNode& args, ibDataNode& result) const override;
};

#endif
