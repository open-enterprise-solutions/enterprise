#ifndef _FRMSERVER_OES_H__
#define _FRMSERVER_OES_H__

#include <wx/wx.h>

#include "core/core.h"   // IB_EXPORT / IB_IMPORT

// THE SERVER'S FRONTEND — the forms, their documents and the protocol a client renders them over. It holds no
// window: what a person sees is drawn by a client (the wx renderer, a browser, the assistant through MCP) from
// what this library answers. It grew beside the old desktop front, the oracle it was checked against, and replaced
// it (branch oes/thick_client; docs/private/multi-base-process.md, the client protocol).
#if defined(FRMSERVER_EXPORTS)
#define FRMSERVER_API IB_EXPORT
#else
#define FRMSERVER_API IB_IMPORT
#endif

#endif
