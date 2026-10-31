#ifndef _FRMCLIENT_BACKEND_APP_DATA_H__
#define _FRMCLIENT_BACKEND_APP_DATA_H__

// THE APPLICATION INSTANCE — the engine's (backend/appData.h): the server's. What the copied code reaches through it,
// the client reaches through its session (frmclient/backend/session/session.h).
#include "frmclient/frmclient.h"
#include "frmclient/backend/session/session.h"

class ibMetaData;

// THE APPLICATION'S DATA as the copied windows ask it — it holds no configuration of its own: what an active one would
// answer is the server's (a window is handed what it needs, or asks).
class FRMCLIENT_API ibApplicationData {
public:
	// The designer — the frame logged in as one (ibProtocol::ibLogin::ibMode::Designer): the thin designer is.
	bool DesignerMode() const;

	// A client of the same base started beside this one, on this computer — `strAppName` the binary next to this one,
	// given the base this one opened (the fields below; `--debug` when `searchDebug`). Its pid; 0 — not started.
	// `useManifest` — the web server (ibStartWebServer) publishing this base, its page opened in the browser. ⚠ The
	// debugger's search for it is the caller's.
	long RunApplication(const ibString& strAppName, bool searchDebug = true, bool useManifest = false) const;

	// The file base's folder this client opened (the engine's: a Firebird base's — the VCS working copy root); empty: a
	// base on a server, or on a DBMS.
	const ibString& GetFile() const { return m_strFile; }

private:

	// THE BASE THIS CLIENT OPENED — the engine's appData's fields: what the application read from its command line or
	// had the person pick (designer-thin/mainApp.cpp, the one that fills them).
	friend class ibAppDesigner;

	// APPLICATION SERVER ENTRY — oes://server[:port]/<base>, or oess:// over TLS; the authorities it trusts, a PEM file
	// (empty — the platform's store)
	ibString m_strAddress;
	ibString m_strTrusted;

	// FILE ENTRY
	ibString m_strFile;

	// SERVER ENTRY — a file base on a DBMS server (PostgreSQL)
	ibString m_strServer;
	ibString m_strPort;
	ibString m_strDatabase;
	ibString m_strUser;
	ibString m_strPassword;

	// IB ENTRY
	ibString m_strIBUser;
	ibString m_strIBPassword;

	//LOCALE
	ibString m_strLocale;
};

// THE APPLICATION INSTANCE as the copied windows reach the base's parts through it — the server's: asked of their
// schemas.
class FRMCLIENT_API ibApplicationInstance {
public:
	// The person's assistant access (frmclient/backend/mcp/mcpServer.h) — none when they may not ask for it.
	static class ibMcpServer* GetMcpServer();
	// …as its schema's answer already at hand shows it (a menu item's) — received from it, not asked again.
	static class ibMcpServer* GetMcpServer(const class ibProtocolNode& shown);
};

inline ibApplicationData s_appData;
inline ibApplicationData* const appData = &s_appData;

inline const ibMetaData* const activeMetaData = nullptr;

#endif
