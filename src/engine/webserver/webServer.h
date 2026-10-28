#ifndef __WEBSERVER_WEB_SERVER_H__
#define __WEBSERVER_WEB_SERVER_H__

// THE WEB SERVER — the bases' transport for browsers: it serves a site's files, and carries the protocol of a base's
// clients between a browser and the base. A PUBLICATION is a base under a name in the address — a file base, opened
// here and held as a server holds its bases (protocol/connectionFile.h: one base, a connection per browser), or a
// base of an application server, reached over its port (core/net/httpClient.h), over TLS when its address says oess://.
// Either way the browser sees the same
// door, the one an application server has — and the site's page beside it:
//
//   GET  /<publication>/          the page: the site's index.html, the same for every publication — the base is the
//                                 address's; the page's own files beside it (/<publication>/js/…)
//   WS   /<publication>/client    a connection: each text message one JSON-RPC request, answered by its response
//   POST /<publication>/client    one request, its response
//
// Started by hand it narrates in its console and stays until Ctrl+C; later it becomes a service.
//
//   <web folder>/                   web beside the program, unless --dir names another
//     web.conf
//     www/                          the site's files, unless Root names another folder: served as they are
//     www.wpk                       …packed (a zip), what a release ships — read when there is no such folder. The
//                                   build packs src/engine/webserver/www, the site's source, into it
//
//   ; web.conf
//   Host=127.0.0.1                  where browsers reach it: empty — every interface (HTTP there only sends a
//                                   browser on to HTTPS)
//   HttpPort=8080                   left out — 8080 (ibPort::Web); empty — no HTTP; 0 — the system chooses
//   HttpsPort=443                   left out — 443 when a Certificate is named (ibPort::WebSecure), else none; empty —
//                                   no HTTPS; it speaks over Certificate and PrivateKey (PEM)
//   Certificate=
//   PrivateKey=
//   Root=www                        the site's files — a folder: a site being written names its source here, and an
//                                   edit is served at the next request, with no build
//   Origins=                        the pages of other sites that may open a socket here, comma-separated
//   Workers=                        the threads that call the bases; empty — four per core, 32 at least
//   [trade]                         a publication: the base at /trade/client
//   Kind=firebird                   firebird — a file base, opened here from its Directory
//   Directory=bases/trade
//   Locale=                         the platform's locale for the base; empty — as the base says
//   Debug=false                     true — the engine's debug server comes up, as enterprise's --debug
//   [remote]
//   Kind=server                     server — a base of an application server, at its Address
//   Address=oes://appserver:7373/trade   oess:// — over TLS, the server's certificate checked against its name
//   Trusted=                        oess://: the authorities trusted, a PEM file; empty — the system's store
//
// Every path is relative to the web folder.

#include <cstdint>
#include <map>
#include <memory>
#include <optional>
#include <vector>

#include <wx/string.h>

#include "core/diagnostics/journal.h"

#include "core/net/httpServer.h"
#include "core/net/ports.h"

class ibProtocolFileBase;

// What the web server says goes through Print, not the journal macros: those compile to nothing in a release build,
// and this is what a person watching the console must always see.
template <typename... Args>
void ibWebServerSay(ibJournalMark mark, const ibString& format, const Args&... args)
{
	ibTechJournal::Print(mark, ibT("webserver"), format, args...);
}

// ONE PUBLICATION — a base under a name in the address.
struct ibWebPublication {
	ibString m_name;        // the path's first segment — the group of web.conf
	ibString m_kind;        // lower-cased: firebird, server
	ibString m_directory;   // firebird: the base's folder
	ibString m_locale;
	bool     m_debug = false;
	ibString m_address;     // server: oes://server[:port]/<base>, or oess://
	ibString m_trusted;     // server, oess://: a PEM file of the authorities trusted; empty — the system's store
};

// WHAT IS SERVED — web.conf, with the command line over it.
struct ibWebConfig {
	ibString                      m_folder;
	ibString                      m_host = ibT("127.0.0.1");
	std::optional<std::uint16_t>  m_httpPort = ibPort::Web.m_first;
	std::optional<std::uint16_t>  m_httpsPort;
	ibString                      m_certificate;   // paths
	ibString                      m_privateKey;
	ibString                      m_root;
	std::vector<ibString>         m_origins;
	long                          m_workers = 0;
	std::vector<ibWebPublication> m_publications;
	ibString                      m_manifest;   // a file the page's address is written to once the port is open: the
	                                            // last publication's - the command line's goes last (--manifest)

	// web.conf in `folder`, when there is one; false with why in `error` — a port that is not one, a publication of
	// no kind this server knows. No file is no error: nothing is published but what the command line says.
	bool Read(const ibString& folder, ibString& error);
};

class ibWebServer {
public:

	explicit ibWebServer(const ibWebConfig& config);
	~ibWebServer();

	// The whole life: open, serve until Ctrl+C / SIGTERM / the console closing, stop. The process's exit code.
	int Run();

private:

	// One publication: its file base opened, or its server's address read — and its routes given. False, the reason
	// said, when it cannot be served; the rest are.
	bool Publish(const ibWebPublication& publication);

	// The connections let go first — each browser's clients leave their base — and then the file bases.
	void Stop();

	const ibWebConfig& m_config;
	ibHttpServer       m_http;

	// Every publication served, by name: the file base opened here for it, each the server of its browsers — none for
	// an application server's.
	std::map<ibString, std::unique_ptr<ibProtocolFileBase>> m_bases;
};

#endif
