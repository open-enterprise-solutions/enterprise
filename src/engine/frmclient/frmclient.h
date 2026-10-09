#ifndef _FRMCLIENT_OES_H__
#define _FRMCLIENT_OES_H__

#include <wx/wx.h>

// THE FORMS' CLIENT — the half of the desktop's frontend that draws, as frmserver is the half that composes: what the
// thin runtime and the thin designer share, the main window and what is drawn in it, over the protocol's library
// (protocol.dll: the connections to a base's clients and the communicator that keeps the frame it was answered). It
// holds no model of a form: what it draws it is answered (docs/private/client-protocol-arc.md).
#if defined(FRMCLIENT_EXPORTS)
#define FRMCLIENT_API WXEXPORT
#else
#define FRMCLIENT_API WXIMPORT
#endif

#endif
