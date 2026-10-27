#ifndef _FRMCLIENT_BACKEND_MCP_SERVER_H__
#define _FRMCLIENT_BACKEND_MCP_SERVER_H__

#include <optional>

#include <wx/string.h>

#include "frmclient/frmclient.h"
#include "protocol/protocolNode.h"
#include "core/mcp/mcpSettings.h"   // ibMcpSettings — what a person set, as the settings page edits it

class ibSession;

// THE ASSISTANT ACCESS — the engine's ibMcpServer: the server's, the person's in their base. What the copied code asks
// of it is asked of its schema (McpServer); what it does, the server does.
class FRMCLIENT_API ibMcpServer {
public:

	// ⭐ WHAT ITS SCHEMA SHOWS (McpServer: Allowed, the settings, Running, the Endpoint), by the metaobject's verb
	// (ibValueMetaObject::Receive): asked for and taken in, or taken from an answer at hand (the menu item's). The getters
	// read it — one exchange per act, not one per getter: opening Options was four (the menu's, Allowed, the settings,
	// the endpoint), the first answered them all.
	void Receive();
	void Receive(const ibProtocolNode& shown) { m_shown = shown; }

	// Whether the person may switch it here — not on the application server, nor where the mode has no such schema.
	bool IsAllowed() const;

	// The settings the next Start or SaveSettings takes — sent with them, as the page's value.
	void          Configure(const ibMcpSettings& settings) { m_settings = settings; }
	// The settings in force — the server's.
	ibMcpSettings GetSettings() const;

	bool SaveSettings(ibSession* session) const;

	// Started in the name of this session; `refusal` — why it did not start.
	bool Start(ibSession* session, ibString& refusal);
	void Stop();

	bool IsRunning() const;

	// What it answers on — empty while it is not running.
	ibString GetEndpoint() const;

private:

	// What Configure was given — none: the settings in force are sent.
	std::optional<ibMcpSettings> m_settings;
	// What its schema showed, as last received — empty: nothing received yet, or refused (the person told why).
	ibProtocolNode m_shown;
};

#endif
