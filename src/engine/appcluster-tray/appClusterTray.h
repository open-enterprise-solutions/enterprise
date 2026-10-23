#ifndef __APP_CLUSTER_TRAY_H__
#define __APP_CLUSTER_TRAY_H__

// THE CLUSTER'S TRAY — the monitor of the application cluster of one server folder, as Apache's is of Apache: it shows
// what the cluster holds — its port, the server of each base with its state, its process, its falls and why it fell —
// and steers it: a server started, stopped or restarted, the cluster stopped, and a cluster not running started — its
// service, if one is installed (appcluster --install), the program beside the tray otherwise. It speaks to the cluster
// over the control socket (core/server/clusterControl.h) and is no part of it: the cluster serves whether a tray runs or
// not, and a tray closed leaves it serving. It links the core alone.

#include <cstdint>
#include <memory>
#include <vector>

#include <wx/app.h>
#include <wx/timer.h>

#include "core/net/ioThreads.h"
#include "core/server/clusterControl.h"
#include "core/server/serverConfig.h"

class ibClusterTray;
class ibWebSocketClient;
class wxProcess;
class wxProcessEvent;

// What the tray says goes to the journal (appcluster-tray).
template <typename... Args>
void ibAppClusterTraySay(ibJournalMark mark, const wxFormatString& format, const Args&... args)
{
	ibTechJournal::Print(mark, wxT("appcluster-tray"), format, args...);
}

class ibAppClusterTray : public wxApp {
public:

	ibAppClusterTray();
	virtual ~ibAppClusterTray();

	virtual bool OnInit() override;
	virtual int  OnRun() override;
	virtual int  OnExit() override;

#if wxUSE_CMDLINE_PARSER
	virtual void OnInitCmdLine(wxCmdLineParser& parser) override;
	virtual bool OnCmdLineParsed(wxCmdLineParser& parser) override;
#endif

	// WHAT THE CLUSTER SAID LAST — reached: its socket open and its list come; not reached, the list is empty, and why.
	bool                                IsReached() const { return m_reached; }
	const ibString&                     GetWhyNotReached() const { return m_whyNotReached; }
	std::uint16_t                       GetPort() const { return m_port; }
	const std::vector<ibClusterServer>& GetServers() const { return m_servers; }
	const ibServerConfig&               GetConfig() const { return *m_config; }

	// A verb for the cluster, or for one server of it (ibClusterControl).
	void Send(const char* verb);
	void Send(const char* verb, const ibClusterServer& server);

	// THE CLUSTER STARTED — its service, if one is installed; the program beside the tray otherwise, a process of its own.
	void StartCluster();

private:

	// The socket opened to the cluster — its port and its token read from the folder as they are now.
	void Connect();
	// What the cluster said: the list.
	void Receive(const ibString& text);
	// The socket gone — the cluster stopped, or not reached: opened again on the timer.
	void OnClosed(const ibString& reason);

	// Said to the person, and to the journal.
	void Tell(const ibString& what);

	void OnTimer(wxTimerEvent& event);
	// The cluster this tray started as a program has ended — with a code other than 0, it did not serve.
	void OnClusterEnded(wxProcessEvent& event);

	ibString                           m_folder;
	std::unique_ptr<ibServerConfig>    m_config;
	ibIoThreads                        m_io;            // before the socket: it carries it
	std::unique_ptr<ibWebSocketClient> m_socket;
	unsigned                           m_socketNumber = 0;   // what the socket's calls are told apart by from an old one's
	bool                               m_reached = false;
	std::uint16_t                      m_port = 0;
	std::vector<ibClusterServer>       m_servers;
	std::unique_ptr<ibClusterTray>     m_tray;
	wxTimer                            m_timer;
	wxProcess*                         m_cluster = nullptr;   // the cluster this tray started as a program, while it runs
	bool                               m_serviceStarting = false;   // its service asked to start, the socket not open yet
	ibString                           m_whyNotReached;
};

wxDECLARE_APP(ibAppClusterTray);

#endif
