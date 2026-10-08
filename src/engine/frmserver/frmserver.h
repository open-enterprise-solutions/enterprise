#ifndef _FRMSERVER_OES_H__
#define _FRMSERVER_OES_H__

#include <wx/wx.h>

// THE SERVER'S FRONTEND — the forms, their documents and the protocol a client renders them over. It holds no
// window: what a person sees is drawn by a client (the wx renderer, a browser, the assistant through MCP) from
// what this library answers. It grows beside frontend.dll, the desktop one, which stays the oracle it is checked
// against; when it is done it takes that name (docs/private/multi-base-process.md, the client protocol).
#if defined(FRMSERVER_EXPORTS)
#define FRMSERVER_API WXEXPORT
#else
#define FRMSERVER_API WXIMPORT
#endif

#endif
