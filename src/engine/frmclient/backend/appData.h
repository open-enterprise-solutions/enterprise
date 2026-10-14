#ifndef _FRMCLIENT_BACKEND_APP_DATA_H__
#define _FRMCLIENT_BACKEND_APP_DATA_H__

// THE APPLICATION INSTANCE — the engine's (backend/appData.h): the server's. What the copied code reaches through it,
// the client reaches through its session (frmclient/backend/session/session.h).
#include "frmclient/frmclient.h"
#include "frmclient/backend/session/session.h"

class ibMetaData;

// THE APPLICATION'S DATA as the copied windows ask it — it holds no configuration of its own: what an active one would
// answer is the server's (a window is handed what it needs, or asks).
class ibApplicationData {
public:
	// The designer — the frame logged in as one (ibProtocolMode::Designer): the thin designer is.
	bool DesignerMode() const;
};

// THE APPLICATION INSTANCE as the copied windows reach the base's parts through it — the server's: asked of their
// schemas.
class FRMCLIENT_API ibApplicationInstance {
public:
	// The person's assistant access (frmclient/backend/mcp/mcpServer.h) — none when they may not ask for it.
	static class ibMcpServer* GetMcpServer();
};

inline ibApplicationData s_appData;
inline ibApplicationData* const appData = &s_appData;

inline const ibMetaData* const activeMetaData = nullptr;

#endif
