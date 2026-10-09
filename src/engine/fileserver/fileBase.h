#ifndef __FILESERVER_FILE_BASE_H__
#define __FILESERVER_FILE_BASE_H__

#include <functional>
#include <string>

#include <wx/string.h>

#include "fileserver/fileserver.h"

// A FILE BASE'S FOUR DOORS, for the thin client that loads this library: it asks for them by their names
// (protocol/connectionFile.cpp). The base is opened here and its clients' host stands beside it.
//
// TEXT, AS A SERVER'S PORT SPEAKS IT — one JSON-RPC request in, its answer out, UTF-8 both ways: the call goes through
// the very door the port's does (ibClientHost::Call(text)), so a file base gives the client exactly what a server
// would, and the client keeps no road of its own for it. Nothing of the backend is in these signatures — the client
// is built without it.
//
// C names, so a loader asks for them as they are written.
struct ibFileBase;

extern "C" {

// `request` — a JSON object saying where the base lives, as ibFileInstanceRequest does: Directory (a Firebird base's
// folder), or Server, Port, User, Password, Database (PostgreSQL); Name, Locale; Debug (true — the engine's debug
// server comes up, as enterprise.exe's `--debug`). Null when it did not open, and why in `error`.
FILESERVER_API ibFileBase* ibFileBaseOpen(const std::string& request, wxString& error);

// One JSON-RPC request to the base's clients, and its answer.
FILESERVER_API bool ibFileBaseCall(ibFileBase* base, const std::string& request, std::string& answer, wxString& error);

// How the program is told something — a JSON-RPC notification of the base's (`changed {Client}`: a client's frame
// changed by itself), handed to `notified` on whichever thread of the base's said it; empty — told nothing.
FILESERVER_API void ibFileBaseListen(ibFileBase* base, std::function<void(const std::string& text)> notified);

// Its clients go — each its own session's teardown, while the base still stands — and then the base.
FILESERVER_API void ibFileBaseClose(ibFileBase* base);

}

#endif
