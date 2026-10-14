#ifndef _FRMCLIENT_BACKEND_MCP_SERVER_H__
#define _FRMCLIENT_BACKEND_MCP_SERVER_H__

#include <optional>

#include <wx/string.h>

#include "frmclient/frmclient.h"

class ibSession;

// WHAT A PERSON SET — the engine's (backend/mcp/mcpServer.h), as the settings page edits it.
struct FRMCLIENT_API ibMcpSettings {

	bool           m_enabled = false;

	wxString       m_address = wxT("127.0.0.1");
	unsigned short m_port    = 3737;

	wxString       m_token;

	bool IsOk() const { return !m_address.IsEmpty() && m_port != 0; }
};

// THE ASSISTANT ACCESS — the engine's ibMcpServer: the server's, the person's in their base. What the copied code asks
// of it is asked of its schema (McpServer); what it does, the server does.
class FRMCLIENT_API ibMcpServer {
public:

	// The settings the next Start or SaveSettings takes — sent with them, as the page's value.
	void          Configure(const ibMcpSettings& settings) { m_settings = settings; }
	// The settings in force — the server's.
	ibMcpSettings GetSettings() const;

	bool SaveSettings(ibSession* session) const;

	// Started in the name of this session; `refusal` — why it did not start.
	bool Start(ibSession* session, wxString& refusal);
	void Stop();

	bool IsRunning() const;

	// What it answers on — empty while it is not running.
	wxString GetEndpoint() const;

private:

	// What Configure was given — none: the settings in force are sent.
	std::optional<ibMcpSettings> m_settings;
};

#endif
