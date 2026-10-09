#ifndef _FILESERVER_OES_H__
#define _FILESERVER_OES_H__

#include <wx/wx.h>

// A FILE BASE FOR THE THIN CLIENT — a small library the thin client (frmclient) LOADS, never links, and only when the
// base it opens is a file base. Loading it brings the server in: it links the backend and frmserver, opens the base
// in the client's process and hands the client the base's clients' host through three C doors (fileBase.h). A thin
// client working with a server loads none of it.
#if defined(FILESERVER_EXPORTS)
#define FILESERVER_API WXEXPORT
#else
#define FILESERVER_API WXIMPORT
#endif

#endif
