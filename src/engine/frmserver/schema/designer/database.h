#ifndef __SCHEMA_DATABASE_H__
#define __SCHEMA_DATABASE_H__

// THE BASE AS A WHOLE — the designer's Administration → Restore, Dump and Clear database (designer/mainFrame,
// OnLoadDatabase, OnSaveDatabase and OnClearDatabase), as the server holds them: the calls those handlers made on
// appData, the questions they asked the person and what they told them after, and the registry's CloseAll after a
// restore. The file the person picks is asked of them as File → Open asks it (Request File), the dump taken down the
// same way. The designer's own: a runtime does not restore its base.

#include "frmserver/schema/clientSchema.h"

class FRMSERVER_API ibSchemaDatabase : public ibClientSchema {
public:

	// The configuration's right to administer the data — what the desktop's three items stood on.
	virtual bool AccessRight(const ibSession& session) const override;

	// Nothing: the base is acted on, not shown.
	virtual void Build(const ibSession& session, const ibDataNode& args, ibDataNode& result) const override;

	// Load — restored from a file the person is asked for, every session of the base closed after; Save — dumped into a
	// file of the session's, taken down where the person chooses; Clear — asked first. Each the desktop handler's work,
	// said to the person as it said it.
	virtual std::function<void()> Command(const ibSession& session, s32 command,
		const ibDataNode& args, ibProtocolRefusal& refusal, wxString& error) const override;
};

#endif
