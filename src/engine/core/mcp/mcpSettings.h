#ifndef __MCP_SETTINGS_H__
#define __MCP_SETTINGS_H__

////////////////////////////////////////////////////////////////////////////
//	Description : WHAT A PERSON SET for the assistant access - one value for every side
////////////////////////////////////////////////////////////////////////////
//
// Kept apart from the server (backend/mcp/mcpServer.h) so a settings page edits a value and hands it
// over, instead of poking at a running object — the desktop's page beside the server, the client's
// through the server's schema (McpServer). One value, so it lives here.

#include <wx/string.h>

#include "core/net/ports.h"

struct ibMcpSettings {

	// Off unless somebody turned it on. An open port that describes and edits a
	// configuration is not something a platform should acquire by upgrading.
	bool           m_enabled = false;

	// Loopback by default, and a person has to mean it to widen that. The port is the designer's; a server's base takes
	// its own from server.conf (McpPort).
	wxString       m_address = wxT("127.0.0.1");
	unsigned short m_port    = ibPort::Assistant.m_first;

	// ⭐ WHO IS ALLOWED TO SPEAK. Every request carries it as `Authorization: Bearer <token>`,
	// and one without is refused before any tool is looked up.
	//
	// It exists because the address is not an authorisation. A loopback port is open to every
	// process on the machine — every other application, every script, anything a browser was
	// talked into fetching — and this one edits a configuration and runs code AS THE DEVELOPER
	// WHOSE SESSION IT LIVES IN. "Only local" answers where a caller is, never who.
	//
	// ⚠ AND IT IS NOT OPTIONAL. An empty token refuses everything rather than allowing it: a
	// secret that may be blank is a secret nobody sets, and the failure of that choice is silent
	// where the failure of this one is a message saying where to copy it from. One is generated
	// the first time the settings are written, so it is never blank by accident.
	wxString       m_token;

	bool IsOk() const { return !m_address.IsEmpty() && m_port != 0; }
};

#endif // __MCP_SETTINGS_H__
