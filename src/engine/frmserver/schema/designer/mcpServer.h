#ifndef __SCHEMA_MCP_SERVER_H__
#define __SCHEMA_MCP_SERVER_H__

// THE DESIGNER'S ASSISTANT ACCESS — the Assistant access page of its Options (frontend/mainFrame/settings,
// ibPanelMcpSettings) and the dialog's OK (designer/mainFrame, OnToolsSettings), as the server holds them: the
// person's settings, whether the server listens and where, and the calls the page made on it — Start now, Stop now,
// and the settings kept. The server is the base's (ibApplicationInstance::GetMcpServer), started in the name of the
// session that asks. The designer's own: the runtime has no assistant.

#include "frmserver/schema/clientSchema.h"

class FRMSERVER_API ibSchemaMcpServer : public ibClientSchema {
public:

	// Enabled, Address, Port, Token — the settings in force — and Running, Endpoint — empty while it is not running;
	// Allowed — whether the person may switch it here.
	virtual void Build(const ibSession& session, const ibDataNode& args, ibDataNode& result) const override;

	// Start {Enabled, Address, Port, Token} — configured with those and started in the name of this session, done in
	// the asking: a refusal names why (switched off, the port taken), in words the page shows beside the setting that
	// fixes it. Stop. Save {…} — configured with those and kept as the person's.
	virtual std::function<void()> Command(const ibSession& session, s32 command,
		const ibDataNode& args, ibProtocolRefusal& refusal, wxString& error) const override;

	// THE MCP SERVER THE PERSON MAY SWITCH — their base's, where the process is theirs (a file base, opened in their own
	// process, as the desktop designer's). Not on the application server: the settings it holds are read at a login only
	// where the session is the process's own (ibApplicationInstance), and a port opened there would be the server
	// machine's — that server starts its bases' own (ibAppServer). None: neither.
	static class ibMcpServer* SwitchableServer(const ibSession& session);
};

#endif
