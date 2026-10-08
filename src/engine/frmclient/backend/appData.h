#ifndef _FRMCLIENT_BACKEND_APP_DATA_H__
#define _FRMCLIENT_BACKEND_APP_DATA_H__

// THE APPLICATION INSTANCE — the engine's (backend/appData.h): the server's. What the copied code reaches through it,
// the client reaches through its session (frmclient/backend/session/session.h).
#include "frmclient/backend/session/session.h"

class ibMetaData;

// THE APPLICATION'S DATA as the copied windows ask it — the client is never the designer, and holds no configuration of
// its own: what an active one would answer is the server's (a window is handed what it needs, or asks).
class ibApplicationData {
public:
	bool DesignerMode() const { return false; }
};

inline ibApplicationData s_appData;
inline ibApplicationData* const appData = &s_appData;

inline const ibMetaData* const activeMetaData = nullptr;

#endif
