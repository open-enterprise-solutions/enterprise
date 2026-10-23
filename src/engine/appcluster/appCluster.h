#ifndef __APP_CLUSTER_H__
#define __APP_CLUSTER_H__

// THE APPLICATION CLUSTER — the server folder served by server processes, as many for each base as its Processes says
// (appserver --worker=<base> --route=<n>): it reads the folder's config, starts the servers, holds the port the clients
// come in by and carries each client's socket on to a server of its base — the one its session lives in, else the least
// busy (ibClusterRoute) — and starts a server again when it falls. It runs no session and no code of a base, so
// there is nothing in it to fall: a server that falls takes only its own clients with it (docs/private/appcluster.md). It links the core alone, and of wx the base: it has no window. Started by hand it
// narrates in its console; as a service of the system (--service, clusterService.h) it has none. What it holds is shown
// and steered by its tray (appcluster-tray), over the control socket (core/server/clusterControl.h).

#include <memory>
#include <mutex>
#include <vector>

#include <wx/app.h>
#include <wx/timer.h>

#include "core/net/httpServer.h"
#include "core/server/serverConfig.h"

class ibClusterWorker;

// What the cluster says goes through Print, as the application server's does (ibAppServerSay): a person watching it must
// always see it.
template <typename... Args>
void ibAppClusterSay(ibJournalMark mark, const wxFormatString& format, const Args&... args)
{
	ibTechJournal::Print(mark, wxT("appcluster"), format, args...);
}

class ibAppCluster : public wxAppConsole {
public:

	ibAppCluster();
	virtual ~ibAppCluster();

	virtual bool OnInit() override;
	virtual int  OnExit() override;

#if wxUSE_CMDLINE_PARSER
	virtual void OnInitCmdLine(wxCmdLineParser& parser) override;
	virtual bool OnCmdLineParsed(wxCmdLineParser& parser) override;
#endif

	// What a server is started with: the folder's config, and the platform's locale given to the cluster, passed on
	// (appserver --locale).
	const ibServerConfig& GetConfig() const { return *m_config; }
	const ibString&       GetLocale() const { return m_locale; }

	// Every server told to stop; the cluster goes once the last of them has gone.
	void Stop();

	// A server's state changed: every tray is told the list again; a base's first server serving, its others held are
	// started — and, the cluster stopping, it goes with the last server.
	void OnWorkerChanged();

private:

	// What this start is for: the cluster, by hand or under the service manager — or its service installed or removed.
	enum class ibMode { Console, Service, Install, Uninstall };

	// Said to the journal — and so to the console, or the service's exit code — and the start refused.
	bool Refuse(const ibString& reason);

	// A verb a tray sent, for a server or for the cluster (ibClusterControl).
	void Receive(const ibString& message);
	// THE BASE'S SERVERS COUNTED AGAIN — server.conf's Processes of `base`, read now, as a tray starts or restarts one of
	// them: servers added (started once its first serves, as at the start), or the last ones stopped and taken out of
	// the list.
	void ApplyProcesses(const ibString& base);

	// The servers of `base` a client's socket may be carried to — asked by the port's threads as a socket opens.
	std::vector<ibClusterWorker*> WorkersOf(const ibString& base);

	// The servers' pipes read — what they said — and an ask to stop taken, a few times a second.
	void OnTimer(wxTimerEvent& event);

	ibMode                                        m_mode = ibMode::Console;
	ibString                                      m_folder;
	ibString                                      m_locale;
	std::unique_ptr<ibServerConfig>               m_config;
	std::vector<std::unique_ptr<ibClusterWorker>> m_workers;
	std::mutex                                    m_workersMutex;   // the list changed while the port serves (WorkersOf)
	std::vector<std::unique_ptr<ibClusterWorker>> m_retired;   // taken out of the list: sockets carried to them may
	                                                           // still count on them, so they live till the cluster goes
	std::vector<ibClusterWorker*>                 m_held;    // a base's others, not started till its first serves
	ibHttpServer                                  m_http;
	unsigned short                                m_port = 0;
	ibString                                      m_token;   // what a tray opens the control socket with
	std::vector<std::weak_ptr<ibWebSocket>>       m_trays;   // their sockets: told the list on every change
	wxTimer                                       m_timer;
	bool                                          m_stopping = false;
};

wxDECLARE_APP(ibAppCluster);

#endif
