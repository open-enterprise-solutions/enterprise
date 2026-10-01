#include "appServer.h"

#include <atomic>
#include <chrono>
#include <csignal>
#include <thread>

#include "backend/appHost.h"
#include "backend/backend_exception.h"
#include "backend/session/session.h"

namespace {

// Ctrl+C, a console closing, a service stop: the signal only raises the flag; the main thread does the work.
std::atomic<bool> s_stop{ false };

void OnStopSignal(int) { s_stop.store(true); }

} // namespace

ibAppServer::ibAppServer(ibServerConfig& config, const wxString& locale) :
	m_config(config), m_locale(locale)
{
}

int ibAppServer::Run()
{
	if (!m_config.HasConf()) {
		ibAppServerSay(ibJournalMark::Error, wxT("no config at %s - nothing to serve"), m_config.GetConfPath());
		return 1;
	}

	wxString keyError;
	if (!m_config.LoadKey(keyError)) {
		ibAppServerSay(ibJournalMark::Error, wxT("%s - nothing is served"), keyError);
		return 1;
	}

	std::vector<ibConfiguredInstance> instances = m_config.ReadInstances();
	m_config.AssignIds(instances);
	ibAppServerSay(ibJournalMark::Info, wxT("config %s: %u base(s)"),
		m_config.GetConfPath(), static_cast<unsigned>(instances.size()));

	// Told to stop from here on — while the bases are still being opened too: what is open is then closed in
	// order instead of the process being cut off with its bases in hand.
	std::signal(SIGINT, OnStopSignal);
	std::signal(SIGTERM, OnStopSignal);
#ifdef SIGBREAK
	std::signal(SIGBREAK, OnStopSignal);   // Windows: Ctrl+Break and the console window closing
#endif

	for (const ibConfiguredInstance& instance : instances) {
		if (s_stop.load())
			break;
		Open(instance);
	}

	if (m_sessions.empty()) {
		ibAppServerSay(ibJournalMark::Error, wxT("no base could be served - stopping"));
		ibApplicationInstance::DestroyAppDataEnv();
		return 1;
	}

	wxString names;
	for (const ibSessionHolder& session : m_sessions) {
		if (!names.IsEmpty())
			names += wxT(", ");
		names += session->GetApplicationInstance()->GetInstanceName();
	}
	ibAppServerSay(ibJournalMark::Info, wxT("serving %u of %u base(s): %s - Ctrl+C to stop"),
		static_cast<unsigned>(m_sessions.size()), static_cast<unsigned>(instances.size()), names);

	while (!s_stop.load())
		std::this_thread::sleep_for(std::chrono::milliseconds(250));

	ibAppServerSay(ibJournalMark::Info, wxT("stopping"));
	Stop();
	ibAppServerSay(ibJournalMark::Info, wxT("stopped"));
	return 0;
}

bool ibAppServer::Open(const ibConfiguredInstance& instance)
{
	wxString password, ibPassword, error;
	if (!m_config.OpenSecret(instance.m_name, wxT("Password"), instance.m_password, password, error)
		|| !m_config.OpenSecret(instance.m_name, wxT("IbPassword"), instance.m_ibPassword, ibPassword, error)) {
		ibAppServerSay(ibJournalMark::Error, wxT("base '%s' is not opened: %s"), instance.m_name, error);
		return false;
	}

	// THE THREAD COMES BACK AS IT WAS. Opening leaves it working for the base, and a login leaves the session
	// bound to it (NotifyAuthenticated) — right for a window's thread; wrong for this one, which opens every base
	// and works for none: the next base would be asked through this one's session. Both are given back.
	const ibSessionScope unbound(nullptr);
	const ibApplicationInstanceScope opening(nullptr);

	ibApplicationInstance* applicationInstance = nullptr;
	try {
		switch (instance.m_mode) {
		case eFILE: {
			ibFileInstanceRequest request;
			request.m_runMode   = ibRunMode::eSERVICE_MODE;
			request.m_name      = instance.m_name;
			request.m_locale    = m_locale;
			request.m_directory = instance.m_path;
			applicationInstance = ibApplicationInstance::CreateFileAppDataEnv(request);
			break;
		}
		case eSERVER: {
			ibServerInstanceRequest request;
			request.m_runMode  = ibRunMode::eSERVICE_MODE;
			request.m_name     = instance.m_name;
			request.m_locale   = m_locale;
			request.m_server   = instance.m_server;
			request.m_port     = instance.m_port;
			request.m_user     = instance.m_user;
			request.m_password = password;
			request.m_database = instance.m_database;
			request.m_dirLocal = instance.m_path;
			applicationInstance = ibApplicationInstance::CreateServerAppDataEnv(request);
			break;
		}
		default:
			ibAppServerSay(ibJournalMark::Error, wxT("base '%s': unknown Kind '%s' - expected firebird or postgresql"),
				instance.m_name, instance.m_kind);
			return false;
		}
	}
	catch (const ibBackendException& err) {
		error = err.GetErrorDescription();
	}
	if (applicationInstance == nullptr) {
		ibAppServerSay(ibJournalMark::Error, wxT("base '%s' did not open: %s"), instance.m_name,
			error.IsEmpty() ? wxString(wxT("no reason given")) : error);
		return false;
	}

	// THE SERVER'S OWN SESSION IN THE BASE — made by the base in hand and opened while this thread still works
	// for it (opening left it so), so the registry that makes it and the configuration it loads are the base's
	// own. Of kind Service: while it lives the base admits other application servers and no client process
	// (ibServiceExclusivePolicy).
	ibSessionHolder session;
	try {
		session = applicationInstance->CreateSession();
		if (session && session->Open(instance.m_ibUser, ibPassword) != ibSession::OpenResult::Authenticated)
			session.Reset();
	}
	catch (const ibBackendException& err) {
		ibAppServerSay(ibJournalMark::Error, wxT("base '%s': login refused: %s"), instance.m_name,
			err.GetErrorDescription());
		session.Reset();
	}
	if (!session) {
		ibAppServerSay(ibJournalMark::Error, wxT("base '%s': the server could not log in - the base is closed again"),
			instance.m_name);
		ibApplicationInstance::DestroyAppDataEnv(applicationInstance);
		return false;
	}

	m_sessions.push_back(std::move(session));
	ibAppServerSay(ibJournalMark::Info, wxT("base '%s' is served: %s"), instance.m_name,
		applicationInstance->GetDatabaseDescription());
	return true;
}

void ibAppServer::Stop()
{
	while (!m_sessions.empty()) {
		ibSessionHolder session = std::move(m_sessions.back());
		m_sessions.pop_back();
		ibApplicationInstanceScope working(session->GetApplicationInstance());
		session.Reset();
	}

	// Every base, newest first, and the process with them.
	ibApplicationInstance::DestroyAppDataEnv();
}
