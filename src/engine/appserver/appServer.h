#ifndef __APP_SERVER_H__
#define __APP_SERVER_H__

// THE APPLICATION SERVER — opens every base of its folder, holds each by a session of its own (kind Service:
// while it lives the base admits other application servers and no client process), says what it serves, and
// lets them go when told to stop.
// A base that does not open is said and left; the rest are served (docs/private/multi-base-process.md § 5).

#include <map>
#include <memory>
#include <vector>

#include "clientListener.h"
#include "backend/server/serverConfig.h"
#include "backend/session/sessionHolder.h"

class ibClientHost;

class ibAppServer {
public:
	ibAppServer(ibServerConfig& config, const wxString& locale);
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
	wxString                     m_locale;
	std::vector<ibSessionHolder> m_sessions;   // one per base served — the base is reached through its session

	// The clients of each base, working through the protocol (frmserver) — one host per base served, by the
	// base's name — and the one port they all come in by.
	std::map<wxString, std::unique_ptr<ibClientHost>> m_clientHosts;
	ibClientListener                                  m_clientListener;
};

#endif
