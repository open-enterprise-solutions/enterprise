#ifndef _FRMCLIENT_BACKEND_MCP_SERVER_H__
#define _FRMCLIENT_BACKEND_MCP_SERVER_H__

#include <memory>

#include <wx/string.h>

#include "core/fstring.h"
#include "frmclient/frmclient.h"
#include "protocol/protocolNode.h"

// WHAT A PERSON SET for the port on their machine — a value, so a settings page edits it and hands it over.
struct ibMcpSettings {

	// Off unless somebody turned it on. An open port that describes and edits a configuration is not something a
	// platform should acquire by upgrading.
	bool           m_enabled = false;

	// Loopback by default, and a person has to mean it to widen that.
	ibString       m_address = ibT("127.0.0.1");
	unsigned short m_port    = 0;

	// ⭐ WHO IS ALLOWED TO SPEAK. Every request carries it as `Authorization: Bearer <token>`, and one without is refused
	// before anything is sent.
	//
	// It exists because the address is not an authorisation. A loopback port is open to every process on the machine —
	// every other application, every script, anything a browser was talked into fetching — and what it carries runs AS
	// THE PERSON WHOSE SESSION IT LIVES IN. "Only local" answers where a caller is, never who.
	//
	// ⚠ AND IT IS NOT OPTIONAL. An empty token refuses everything rather than allowing it. One is made the first time the
	// settings are read, so it is never blank by accident, and never made again: a token that changes on its own breaks
	// every client that was told the old one.
	ibString       m_token;

	bool IsOk() const { return !m_address.IsEmpty() && m_port != 0; }
};

// ⭐ THE ASSISTANT ACCESS — A TRANSPORT. It opens the port on this person's machine and carries what it hears to the
// engine's ibMcpServer as it is, over a connection of the client's own (ibProtocol::ibMcp), and the answer back. What a
// message means, the tools and their rights are all the engine's: none of it is known here. Only who may speak on the
// port (the key) and from where (the Origin) are asked here, before anything is sent.
//
// THE SETTINGS LIE ON THIS MACHINE, as the rest of a window's (wxConfig), each person's their own — the port is here, and
// so is whoever connects to it. The designer and the application keep them apart: open together, they take two ports.
class FRMCLIENT_API ibMcpServer {
public:

	ibMcpServer();
	~ibMcpServer();

	// ⭐ WHETHER THE PERSON MAY HAVE IT — what its schema shows (McpServer: Allowed), by the metaobject's verb
	// (ibValueMetaObject::Receive): asked for and taken in, or taken from an answer at hand (the menu item's).
	void Receive();
	void Receive(const ibProtocolNode& shown) { m_shown = shown; }

	// The designer's, or their roles give it. Not — the schema refused.
	bool IsAllowed() const;

	// The settings of this mode on this machine. Saved — what the next Start takes; a running port is not moved by it.
	ibMcpSettings GetSettings() const;
	void          SaveSettings(const ibMcpSettings& settings);

	// LISTENING WHEN THIS RETURNS, on the settings saved: bound to the server, then the port taken.
	// `refusal` — why it did not start, in words a person can act on: switched off, unusable, the port taken.
	bool Start(ibString& refusal);
	void Stop();

	bool IsRunning() const;

	// What it answers on — empty while it is not running.
	ibString GetEndpoint() const;

private:

	class ibMcpListener;   // the port and its stream, defined in the .cpp

	// What its schema showed, as last received — empty: nothing received yet, or refused (the person told why).
	ibProtocolNode m_shown;

	std::unique_ptr<ibMcpListener> m_listener;
	ibString                       m_endpoint;
};

#endif
