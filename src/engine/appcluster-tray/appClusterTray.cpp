#include "appClusterTray.h"

#include <string>

#include <wx/cmdline.h>
#include <wx/filename.h>
#include <wx/msgdlg.h>
#include <wx/process.h>
#include <wx/utils.h>   // wxExecute

#ifdef __WXMSW__
#include <windows.h>   // the service manager — the cluster's service started, and its end read
#include <wx/log.h>    // wxSysErrorMsgStr
#endif

#include "core/diagnostics/crashGuard.h"   // ibCrashGuard::Install — the process's journal and dumps
#include "core/net/httpClient.h"
#include "core/fileSystem.h"
#include "core/server/listener.h"   // ibTlsFiles — whether the cluster's port speaks TLS

#include "clusterTray.h"

wxIMPLEMENT_APP(ibAppClusterTray);

namespace {

// How often a cluster not reached is tried again.
constexpr int kTimerMs = 2000;

// The cluster beside the tray.
ibString ClusterProgram()
{
	wxFileName program(ibProgramFolder(), wxT("appcluster"));
#ifdef __WXMSW__
	program.SetExt(wxT("exe"));
#endif
	return program.GetFullPath();
}

// Where the cluster says why it did not serve: its journal, beside the program.
ibString JournalFolder()
{
	return wxFileName(ibDiagnosticFolder(), wxT("journal")).GetFullPath();
}

#ifdef __WXMSW__

// THE CLUSTER'S SERVICE asked to start (or found running). False: not installed — `installed` false — or refused, with
// the reason.
bool StartClusterService(bool& installed, ibString& error)
{
	installed = false;
	const SC_HANDLE manager = ::OpenSCManagerW(nullptr, nullptr, SC_MANAGER_CONNECT);
	if (manager == nullptr)
		return false;
	const SC_HANDLE service = ::OpenServiceW(manager, ibClusterControl::ServiceName, SERVICE_START);
	if (service == nullptr) {
		const DWORD code = ::GetLastError();
		::CloseServiceHandle(manager);
		if (code == ERROR_SERVICE_DOES_NOT_EXIST)
			return false;
		installed = true;
		error = ibString::Format(_("The cluster's service is not opened: %s"), wxSysErrorMsgStr(code));
		return false;
	}
	installed = true;
	const bool started = ::StartServiceW(service, 0, nullptr) != FALSE;
	const DWORD code = started ? 0 : ::GetLastError();
	::CloseServiceHandle(service);
	::CloseServiceHandle(manager);
	if (started || code == ERROR_SERVICE_ALREADY_RUNNING)
		return true;
	error = ibString::Format(_("The cluster's service is not started: %s"), wxSysErrorMsgStr(code));
	return false;
}

// The cluster's service stopped — and the code it stopped with.
bool ClusterServiceStopped(unsigned long& code)
{
	const SC_HANDLE manager = ::OpenSCManagerW(nullptr, nullptr, SC_MANAGER_CONNECT);
	if (manager == nullptr)
		return false;
	const SC_HANDLE service = ::OpenServiceW(manager, ibClusterControl::ServiceName, SERVICE_QUERY_STATUS);
	SERVICE_STATUS status{};
	const bool read = service != nullptr && ::QueryServiceStatus(service, &status) != FALSE;
	if (service != nullptr)
		::CloseServiceHandle(service);
	::CloseServiceHandle(manager);
	if (!read || status.dwCurrentState != SERVICE_STOPPED)
		return false;
	code = status.dwWin32ExitCode == ERROR_SERVICE_SPECIFIC_ERROR ? status.dwServiceSpecificExitCode : status.dwWin32ExitCode;
	return true;
}

#endif

} // namespace

ibAppClusterTray::ibAppClusterTray() = default;

// Out of line: the tray and the socket are only declared in the header.
ibAppClusterTray::~ibAppClusterTray() = default;

#if wxUSE_CMDLINE_PARSER

void ibAppClusterTray::OnInitCmdLine(wxCmdLineParser& parser)
{
	parser.AddOption(wxT("d"), wxT("dir"), "Server folder of the cluster shown (default: server beside the executable)",
		wxCMD_LINE_VAL_STRING, wxCMD_LINE_PARAM_OPTIONAL);

	wxApp::OnInitCmdLine(parser);
}

bool ibAppClusterTray::OnCmdLineParsed(wxCmdLineParser& parser)
{
	wxString folder = ibServerConfig::DefaultFolder();
	parser.Found(wxT("dir"), &folder);
	m_folder = folder;

	return wxApp::OnCmdLineParsed(parser);
}

#endif

bool ibAppClusterTray::OnInit()
{
	// THE JOURNAL AND THE DUMPS — the first act, as every application of the platform does.
	ibCrashGuard::Install(wxT("appcluster-tray"));

	if (!wxApp::OnInit())
		return false;

	// No window of its own: a message box closed is not the last window gone.
	SetExitOnFrameDelete(false);

	m_config = std::make_unique<ibServerConfig>(m_folder);
	m_tray = std::make_unique<ibClusterTray>(*this);
	Connect();
	m_tray->Draw();

	m_timer.Bind(wxEVT_TIMER, &ibAppClusterTray::OnTimer, this);
	m_timer.Start(kTimerMs);
	return true;
}

int ibAppClusterTray::OnRun()
{
	const int code = wxApp::OnRun();
	// THE ICON GOES AS THE LOOP ENDS, whichever way it ended. wx's cleanup deletes every top-level window BEFORE OnExit
	// (CallOnExit), the icon's hidden one among them; an icon deleted after it deletes that window a second time.
	m_tray.reset();
	return code;
}

int ibAppClusterTray::OnExit()
{
	m_timer.Stop();
	++m_socketNumber;
	m_socket.reset();
	if (m_cluster != nullptr) {
		// Left serving: a tray closed is no stop. The process object frees itself once the cluster has ended.
		m_cluster->Unbind(wxEVT_END_PROCESS, &ibAppClusterTray::OnClusterEnded, this);
		m_cluster->Detach();
		m_cluster = nullptr;
	}
	return wxApp::OnExit();
}

void ibAppClusterTray::Connect()
{
	unsigned short port = 0;
	ibString token, error;
	if (!m_config->ReadPort(port, error) || !m_config->ReadClusterToken(token, error)) {
		m_whyNotReached = error;
		return;
	}
	if (port == 0) {
		m_whyNotReached = ibString::Format(_("%s names no port - the cluster has never started"), m_config->GetConfPath());
		return;
	}

	// THE CLUSTER'S PORT SPEAKS TLS when backend.conf names a certificate (core/server/listener.h) — the tray then does
	// too. Its certificate is not checked: the tray speaks to this machine's own address, where nobody else holds the
	// port, and is let in by the token, not by the name on a certificate.
	std::shared_ptr<const ibTlsContext> tls;
	if (ibTlsFiles::FromConf(ibBackendConfPath()).IsNamed()) {
		std::string refused;
		if (ibTlsClientContext(std::string(), std::string(), std::string(), /*verify*/ false, tls, refused)
			!= ibHttpClientError::None) {
			m_whyNotReached = ibString::FromUTF8(refused.data(), refused.size());
			return;
		}
	}

	// What the socket says is taken on the main thread — and only while it is the tray's socket, not one let go of.
	const unsigned number = ++m_socketNumber;
	m_socket = std::make_unique<ibWebSocketClient>(m_io.GetContext(), nullptr,
		[this, number](const ibString& text) {
			CallAfter([this, number, text] {
				if (number == m_socketNumber)
					Receive(text);
			});
		},
		[](std::string) {},
		[this, number](const std::string& reason) {
			const ibString why = ibString::FromUTF8(reason.data(), reason.size());
			CallAfter([this, number, why] {
				if (number == m_socketNumber)
					OnClosed(why);
			});
		});
	if (tls)
		m_socket->Secure(std::move(tls));
	m_socket->Open("127.0.0.1", port,
		std::string(ibClusterControl::Path) + "?" + ibClusterControl::Token + "=" + token.ToUtf8());
}

void ibAppClusterTray::Receive(const ibString& text)
{
	std::uint16_t port = 0;
	std::vector<ibClusterServer> servers;
	if (!ibClusterControl::ReadServers(text, port, servers)) {
		ibAppClusterTraySay(ibJournalMark::Warning, wxT("the cluster said what is not its list: %s"), text);
		return;
	}
	m_port = port;
	m_servers = std::move(servers);
	m_reached = true;
	m_whyNotReached.Clear();
	m_serviceStarting = false;
	m_tray->Draw();
}

void ibAppClusterTray::OnClosed(const ibString& reason)
{
	++m_socketNumber;
	m_socket.reset();
	m_reached = false;
	m_servers.clear();
	m_whyNotReached = reason.IsEmpty() ? ibString(_("the cluster closed the connection")) : reason;
	m_tray->Draw();
}

void ibAppClusterTray::Send(const char* verb)
{
	if (m_socket != nullptr)
		m_socket->Send(ibClusterControl::WriteVerb(verb));
}

void ibAppClusterTray::Send(const char* verb, const ibClusterServer& server)
{
	if (m_socket != nullptr)
		m_socket->Send(ibClusterControl::WriteVerb(verb, server));
}

void ibAppClusterTray::StartCluster()
{
	ibString error;
#ifdef __WXMSW__
	bool installed = false;
	if (StartClusterService(installed, error)) {
		ibAppClusterTraySay(ibJournalMark::Info, wxT("the cluster's service asked to start"));
		m_serviceStarting = true;
		return;
	}
	if (installed) {
		Tell(error);
		return;
	}
#endif
	if (m_cluster != nullptr)
		return;   // started, not reached yet

	// NO SERVICE — the program, with this tray's folder: a process of its own, which a tray closed leaves serving.
	const ibString command = ibString::Format(wxT("\"%s\" --dir=\"%s\""), ClusterProgram(), m_config->GetFolder());
	m_cluster = new wxProcess();
	m_cluster->Bind(wxEVT_END_PROCESS, &ibAppClusterTray::OnClusterEnded, this);
	const long pid = wxExecute(command, wxEXEC_ASYNC | wxEXEC_HIDE_CONSOLE, m_cluster);
	if (pid == 0) {
		// Not started (wx said why) — the process object is still ours.
		delete m_cluster;
		m_cluster = nullptr;
		Tell(ibString::Format(_("%s did not start."), ClusterProgram()));
		return;
	}
	ibAppClusterTraySay(ibJournalMark::Info, wxT("the cluster started, process %ld"), pid);
}

void ibAppClusterTray::Tell(const ibString& what)
{
	ibAppClusterTraySay(ibJournalMark::Warning, wxT("%s"), what);
	wxMessageBox(what, _("Application cluster"), wxOK | wxICON_WARNING);
}

void ibAppClusterTray::OnTimer(wxTimerEvent&)
{
	if (m_socket == nullptr) {
		Connect();
		m_tray->Draw();
	}
#ifdef __WXMSW__
	unsigned long code = 0;
	if (m_serviceStarting && ClusterServiceStopped(code)) {
		m_serviceStarting = false;
		Tell(ibString::Format(_("The cluster's service stopped with code %lu - its journal in %s says why."), code,
			JournalFolder()));
	}
#endif
}

void ibAppClusterTray::OnClusterEnded(wxProcessEvent& event)
{
	// Freed once this event is done with it — wx leaves that to whoever handled it.
	ScheduleForDestruction(m_cluster);
	m_cluster = nullptr;
	if (event.GetExitCode() != 0) {
		Tell(ibString::Format(_("The application cluster ended with code %d - its journal in %s says why."),
			event.GetExitCode(), JournalFolder()));
	}
}
