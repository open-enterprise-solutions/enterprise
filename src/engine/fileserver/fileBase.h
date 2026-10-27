#ifndef __FILESERVER_FILE_BASE_H__
#define __FILESERVER_FILE_BASE_H__

#include <functional>
#include <string>

#include <wx/string.h>

#include "core/fstring.h"                 // ibString — where the base lives, and why it did not open
#include "core/memory/memoryBuffer.h"     // ibMemoryBuffer — the packets the doors pass

#include "fileserver/fileserver.h"

// A FILE BASE'S FIVE DOORS, for the program that loads this library — a thin client, the web server: it asks for them
// by their names (protocol/connectionFile.cpp). The base is opened here and its clients' host stands beside it.
//
// PACKETS, AS A SERVER'S PORT SPEAKS THEM (OES-RPC 1.0, core/serialize/dataBuilder.h) — one request in, its answer
// out: the call goes through the very door the port's does (ibClientHost::Call(packet)), so a file base gives the client
// exactly what a server would, and the client keeps no road of its own for it. The client and the base are one
// process: the request is read where its bytes lie and the answer handed back as the bytes it was written into — one
// more owner of them, nothing parsed, nothing encoded. Nothing of the backend is in these signatures — the client is
// built without it; the core is the one library both have.
//
// ONE BASE, MANY CONNECTIONS — as the port's: a call names the connection it came by, the clients logged in through
// it are its own, it is told what the base says to them, and they go when it does (ibFileBaseDisconnect). A thin
// client is one connection; the web server is one per browser.
//
// C names, so a loader asks for them as they are written.
struct ibFileBase;

extern "C" {

// `request` — a JSON object saying where the base lives, as ibFileInstanceRequest does: Directory (a Firebird base's
// folder), or Server, Port, User, Password, Database (PostgreSQL); Name, Locale; Create (true — made there when there
// is none yet, as the designer makes it); Debug (true — the engine's debug server comes up, as enterprise-thin.exe's
// `--debug`). Null when it did not open, and why in `error`.
FILESERVER_API ibFileBase* ibFileBaseOpen(const ibString& request, ibString& error);

// One request to the base's clients, and its answer — packets (empty: a notification, answered with none).
// `connection` and `address` — as ibClientHost::Call takes them: the connection it came by (null — none, a request on
// its own: an HTTP POST) and the computer it comes from (empty — this one).
FILESERVER_API bool ibFileBaseCall(ibFileBase* base, const ibMemoryBuffer& request, const void* connection,
	const ibString& address, ibMemoryBuffer& answer, ibString& error);

// How a connection is told something — a notification of the base's (`changed {Client}`: a client's frame changed by
// itself), a packet handed to `notified` on whichever thread of the base's said it; empty — told nothing.
FILESERVER_API void ibFileBaseListen(ibFileBase* base, const void* connection,
	std::function<void(const ibMemoryBuffer& packet)> notified);

// The connection closed: the clients logged in through it go, and it is told nothing more.
FILESERVER_API void ibFileBaseDisconnect(ibFileBase* base, const void* connection);

// Its clients go — each its own session's teardown, while the base still stands — and then the base.
FILESERVER_API void ibFileBaseClose(ibFileBase* base);

}

#endif
