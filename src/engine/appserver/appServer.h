#ifndef __APP_SERVER_H__
#define __APP_SERVER_H__

// THE APPLICATION SERVER — one base of its folder, for the cluster that started it (appserver --worker=<base>,
// appcluster): opens the base, holds it by a session of its own (kind Service: while it lives the base admits other
// application servers and no client process), serves the clients the cluster hands it, and lets the base go when told
// to stop. A base that does not open is said, and the server ends: the cluster shows that line as the cause.

#include <map>
#include <memory>
#include <vector>

#include "clientListener.h"
#include "core/server/serverConfig.h"
#include "backend/session/sessionHolder.h"

class ibClientHost;

class ibAppServer {
public:
	// `workerBase` — the one base the cluster started this server for: served on this machine alone, on the first free
	// port of ibPort::Workers, said on stdout (ibWorkerPipe). Empty — refused: nothing else starts a server.
	// `route` — which of the base's servers this one is (server.conf's Processes): its clients' tokens end in it, and
	// the base's assistant access is the first one's. 0 — the base's only one.
	ibAppServer(ibServerConfig& config, const ibString& locale, const ibString& workerBase, unsigned route);
	~ibAppServer();

	// The whole life: open, serve until Ctrl+C / SIGTERM / the console closing, stop. The process's exit code.
	int Run();

private:
	// Opens one base and logs the server into it. False — and the reason said — when either step fails; a base
	// that opened but refused the login is closed again, alone.
	bool Open(const ibConfiguredInstance& instance);

	// The server's sessions first, newest first — each ended by its base's registry — then every base.
	void Stop();

	ibServerConfig&              m_config;
	ibString                     m_locale;
	ibString                     m_workerBase;
	unsigned                     m_route;
	std::vector<ibSessionHolder> m_sessions;   // one per base served — the base is reached through its session

	// The clients of each base, working through the protocol (frmserver) — one host per base served, by the
	// base's name — and the one port they all come in by.
	std::map<ibString, std::unique_ptr<ibClientHost>> m_clientHosts;
	ibClientListener                                  m_clientListener;
};

#endif
