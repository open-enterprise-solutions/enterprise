////////////////////////////////////////////////////////////////////////////
//	Description : the process and the bases it serves
////////////////////////////////////////////////////////////////////////////

#include "backend/appHost.h"

#include <algorithm>
#include <thread>

#include <wx/fileconf.h>
#include <wx/filename.h>
#include <wx/stdpaths.h>

#include "backend/backend_exception.h"   // a process with no room for another base refuses it
#include "backend/backend_localization.h"
#include "backend/diagnostics/journal.h"
#include "backend/plugin/pluginManager.h"
#include "backend/session/session.h"   // ibSession::CurrentCached — the journal's first question
#include "backend/session/workerPoolHeadless.h"   // the process's worker pool
#include "backend/syntaxHelper/helpService.h"

std::unique_ptr<ibApplicationHost>                  ibApplicationHost::s_applicationHost;
std::mutex                                          ibApplicationHost::s_mutex;
std::vector<std::unique_ptr<ibApplicationInstance>> ibApplicationHost::s_instances;
std::atomic<std::size_t>                            ibApplicationHost::s_instanceCount{ 0 };

// The base a thread with no session works for (ibApplicationInstanceScope, SetThreadInstance).
static thread_local ibApplicationInstance* t_instance = nullptr;

// The base whose service runs on this thread (SetThreadOwner) — for the journal's lines only.
static thread_local const ibApplicationInstance* t_owner = nullptr;

// The connections to its DBMS a base may hold when neither its infobase.conf nor backend.conf says (Connections):
// enough for the heartbeat, the metadata watcher, ~20 concurrent sessions and slack.
static constexpr std::size_t kDefaultConnections = 32;

///////////////////////////////////////////////////////////////////////////////
//	The set
///////////////////////////////////////////////////////////////////////////////

std::vector<ibApplicationInstance*> ibApplicationHost::GetInstances()
{
	std::lock_guard<std::mutex> lk(s_mutex);
	std::vector<ibApplicationInstance*> instances;
	instances.reserve(s_instances.size());
	for (const auto& applicationInstance : s_instances)
		instances.push_back(applicationInstance.get());
	return instances;
}

bool ibApplicationHost::HasRegistry(const ibSessionRegistry* registry)
{
	if (registry == nullptr)
		return false;
	std::lock_guard<std::mutex> lk(s_mutex);
	for (const auto& applicationInstance : s_instances)
		if (ibApplicationInstance::GetSessionRegistry(applicationInstance.get()) == registry)
			return true;
	return false;
}

bool ibApplicationHost::HasPool(const ibConnectionPool* pool)
{
	if (pool == nullptr)
		return false;
	std::lock_guard<std::mutex> lk(s_mutex);
	for (const auto& applicationInstance : s_instances)
		if (ibApplicationInstance::GetConnectionPool(applicationInstance.get()) == pool)
			return true;
	return false;
}

ibApplicationHost* ibApplicationHost::Ensure(ibRunMode runMode)
{
	// Startup is one thread, and the ctor loads plugins, whose initialise hook must be free to ask anything —
	// so no lock is held around it.
	if (s_applicationHost == nullptr) {
		s_applicationHost.reset(new ibApplicationHost(runMode));
		ibTechJournal::SetContext(&ibApplicationHost::GetJournalContext);
	}

	// …WITH ROOM FOR ONE MORE — as many bases as its config allows (backend.conf, Bases). Refused out loud and
	// before a database is touched: an opener asks this first.
	const std::size_t bases = s_applicationHost->m_configBases;
	if (bases > 0 && s_instanceCount.load(std::memory_order_acquire) >= bases) {
		ibBackendCoreException::Error(
			_("This process already holds %u base(s) - as many as its configuration allows (backend.conf, Bases)."),
			static_cast<unsigned>(bases));
		return nullptr;
	}
	return s_applicationHost.get();
}

std::size_t ibApplicationHost::GetDefaultConnections() const
{
	return m_configConnections > 0 ? m_configConnections : kDefaultConnections;
}

wxString ibApplicationHost::GetJournalContext()
{
	if (s_instanceCount.load(std::memory_order_acquire) == 0)
		return wxString();

	// THE SAME ORDER `appData` ASKS IN — the session the thread works for, then the base it is bound to — and
	// then the service whose thread it is (a registry thread, a job tick: no session, no binding, one owner).
	// Each without a lock: a line is written from anywhere, under anybody's locks, and asking the registry
	// here could wait on the very lock the writer holds.
	const ibSession* const session = ibSession::CurrentCached();
	const ibApplicationInstance* applicationInstance = session != nullptr ? session->GetApplicationInstance() : nullptr;
	if (applicationInstance == nullptr)
		applicationInstance = t_instance;
	if (applicationInstance == nullptr)
		applicationInstance = t_owner;
	return applicationInstance != nullptr ? applicationInstance->GetInstanceName() : wxString();
}

void ibApplicationHost::SetThreadOwner(const ibApplicationInstance* applicationInstance)
{
	t_owner = applicationInstance;
}

ibApplicationInstance* ibApplicationHost::Adopt(std::unique_ptr<ibApplicationInstance> applicationInstance)
{
	ibApplicationInstance* const adopted = applicationInstance.get();
	std::lock_guard<std::mutex> lk(s_mutex);
	s_instances.push_back(std::move(applicationInstance));
	s_instanceCount.store(s_instances.size(), std::memory_order_release);
	return adopted;
}

void ibApplicationHost::SetThreadInstance(ibApplicationInstance* applicationInstance)
{
	t_instance = applicationInstance;
}

void ibApplicationHost::Close(ibApplicationInstance* applicationInstance)
{
	const auto held = [applicationInstance](const std::unique_ptr<ibApplicationInstance>& instance) {
		return instance.get() == applicationInstance;
	};
	{
		// Only what this process holds — a base closed twice is not closed twice.
		std::lock_guard<std::mutex> lk(s_mutex);
		if (applicationInstance == nullptr || std::none_of(s_instances.begin(), s_instances.end(), held))
			return;
	}

	// With the closing thread working for it: its teardown reads the journal, the registry and the pool through
	// `appData` and the accessors.
	{
		ibApplicationInstanceScope closing(applicationInstance);

		// 1. Closed WHILE LISTED — its sessions, removing their rows as the registry stops, ask HasRegistry. (A
		//    unique_ptr's reset() empties the slot before it deletes: freed at once, it is unlisted for its whole
		//    teardown.)
		applicationInstance->Close();

		// 2. Then out of the set, and freed outside the lock — an empty shell by now.
		std::unique_ptr<ibApplicationInstance> shell;
		{
			std::lock_guard<std::mutex> lk(s_mutex);
			const auto it = std::find_if(s_instances.begin(), s_instances.end(), held);
			if (it != s_instances.end()) {
				shell = std::move(*it);
				s_instances.erase(it);
				s_instanceCount.store(s_instances.size(), std::memory_order_release);
			}
		}
	}
	// …and a thread that was bound to it — the one that opened it, if it is this one — is bound to nothing.
	if (t_instance == applicationInstance)
		t_instance = nullptr;
}

void ibApplicationHost::CloseAll()
{
	// Newest first — the reverse of opening, as everything else in this process is torn down.
	for (;;) {
		ibApplicationInstance* newest = nullptr;
		{
			std::lock_guard<std::mutex> lk(s_mutex);
			if (s_instances.empty())
				break;
			newest = s_instances.back().get();
		}
		Close(newest);
	}

	ibTechJournal::SetContext(nullptr);
	s_applicationHost.reset();
}

///////////////////////////////////////////////////////////////////////////////
//	The process
///////////////////////////////////////////////////////////////////////////////

// How many workers the process's pool gets, by what the process is. The web host serves many sessions at once:
// 4 × CPU cores, up to 32. Every other host that opens a base runs its scheduled and background sessions there:
// two — enough that one long job cannot stall another, small enough that background work never crowds out
// interactive sessions (a session owns ONE connection, so the real ceiling is the connection pool). The
// launcher runs no session at all.
static std::size_t PickWorkerCount(ibRunMode runMode)
{
	if (runMode == eLAUNCHER_MODE) return 0;
	if (runMode != eWEB_RUNTIME_MODE) return 2;
	const std::size_t hw = std::thread::hardware_concurrency();
	return std::min<std::size_t>(32, std::max<std::size_t>(4, hw * 4));
}

ibApplicationHost::ibApplicationHost(ibRunMode runMode) :
	m_localeLang(wxLanguage::wxLANGUAGE_UNKNOWN),
	m_pluginManager(std::unique_ptr<ibPluginManager>(new ibPluginManager(ib::AppDataCtorToken{})))
{
	ReadBackendConf();

	// The config's word on the workers, else the run mode's; the launcher runs no session either way.
	const std::size_t workers = runMode == eLAUNCHER_MODE ? 0
		: m_configWorkers > 0 ? m_configWorkers
		: PickWorkerCount(runMode);
	if (workers > 0)
		m_workerPool = std::make_unique<ibWorkerPoolHeadless>(workers);

	// Load everything under <exe-dir>/plugins that exports the OES plugin ABI — ONCE FOR THE PROCESS. A
	// plugin is a DLL loaded into it, and its initialise hook registers what it brings; a second load per
	// base would register it twice. Launcher has no script/metadata subsystem so plugins have nothing to
	// hook — skip it there to avoid paying the scan cost on every connection chooser.
	if (runMode != eLAUNCHER_MODE)
		m_pluginManager->LoadAll();
}

ibApplicationHost::~ibApplicationHost()
{
	// Every base is closed by now (CloseAll), and each drained its own sessions' work from the pool as it closed
	// (ibSessionRegistry::Stop) — so the workers have nothing left to run and are joined before the plugins
	// whose code a task could have reached go away.
	if (m_workerPool)
		m_workerPool->Stop();

	// …so a plugin's shutdown runs with nothing of ours still holding values of its types — and the plugin
	// host it was initialised with is still here.
	if (m_pluginManager)
		m_pluginManager->UnloadAll();
}

#define BACKEND_CONF wxT("backend.conf")

std::size_t ibApplicationHost::ReadCount(const wxConfigBase& conf, const wxString& file, const wxString& key, long least,
	std::size_t byDefault)
{
	long configured = 0;
	if (!conf.Read(key, &configured) || configured == 0)   // left out, or 0 — the default, on purpose
		return byDefault;
	if (configured < least) {
		// A value that cannot be used is SAID, not quietly replaced.
		ibTechJournal::Print(ibJournalMark::Warning, wxT("engine"),
			wxT("%s: %s = %ld is less than %ld - the default is used"), file, key, configured, least);
		return byDefault;
	}
	return static_cast<std::size_t>(configured);
}

void ibApplicationHost::ReadBackendConf()
{
	const wxString& workingDir = wxGetCwd(); wxString strConfigFile;
	if (wxFileName::FileExists(workingDir + wxFILE_SEP_PATH + BACKEND_CONF)) {
		strConfigFile = workingDir +
			wxFILE_SEP_PATH + BACKEND_CONF;
	}
	else {
		wxFileName fn(wxStandardPaths::Get().GetExecutablePath());
		wxString exeDir = fn.GetPath();
		if (wxFileName::FileExists(exeDir + wxFILE_SEP_PATH + BACKEND_CONF)) {
			strConfigFile = exeDir + wxFILE_SEP_PATH + BACKEND_CONF;
		}
#if defined(__WXOSX__) || defined(__APPLE__)
		// On macOS, exe is inside .app/Contents/MacOS/ — check 3 levels up
		else {
			wxFileName bundlePath(exeDir);
			bundlePath.RemoveLastDir(); // MacOS
			bundlePath.RemoveLastDir(); // Contents
			bundlePath.RemoveLastDir(); // .app
			wxString bundleDir = bundlePath.GetPath();
			if (wxFileName::FileExists(bundleDir + wxFILE_SEP_PATH + BACKEND_CONF)) {
				strConfigFile = bundleDir + wxFILE_SEP_PATH + BACKEND_CONF;
			}
		}
#endif
	}

	wxFileConfig fc(wxT(""), wxT(""), wxT(""), strConfigFile);
	fc.Read(wxT("Locale"), &m_configLocale);

	// HOW MUCH THE PROCESS MAY CONSUME — said by whoever runs it, not compiled in. A key left out leaves the
	// built-in value (0): PickWorkerCount for the workers; Bases left out means as many as are opened.
	m_configWorkers = ReadCount(fc, BACKEND_CONF, wxT("Workers"), 1, 0);
	m_configBases   = ReadCount(fc, BACKEND_CONF, wxT("Bases"), 1, 0);
	// The connections are a base's own — its infobase.conf says them; this is only the default for a base that does
	// not. Two at least: the registry holds one for its writes, a session needs another.
	m_configConnections = ReadCount(fc, BACKEND_CONF, wxT("Connections"), 2, 0);

	// (THE MCP SERVER'S SETTINGS ARE NOT HERE. They belong to a PERSON in a
	//  BASE — the server is started from an authenticated designer session and
	//  has nothing to say before one exists — so they live in sys_settings with
	//  every other saved setting, under their own category. See
	//  ibMcpServer::LoadSettings. A copy in this file would be a second road,
	//  and the two would disagree the first time somebody edited the page.)
}

bool ibApplicationHost::InitLocale(const wxString& locale)
{
	// SETTLED ALREADY — by the first base. That is success, not a refusal: a later base asks for the locale
	// the process speaks, and it has one. (This used to answer false on a second call, which on a base of its
	// own never came; on a process of several it would fail every base after the first.)
	if (m_localeLang != wxLanguage::wxLANGUAGE_UNKNOWN)
		return true;

#ifdef DEBUG_TRANSLATE
	wxLog::AddTraceMask(wxS("i18n"));
#endif // WXDEBUG

	m_localeLang = wxLocale::GetSystemLanguage();
	if (m_localeLang == wxLanguage::wxLANGUAGE_UNKNOWN)
		m_localeLang = wxLanguage::wxLANGUAGE_DEFAULT;

	// The base's word, else backend.conf's.
	const wxString& wanted = !locale.IsEmpty() ? locale : m_configLocale;
	if (!wanted.IsEmpty())
		if (const wxLanguageInfo* const info = wxLocale::FindLanguageInfo(wanted))
			m_localeLang = info->Language;

	// Independently of whether we succeeded to set the locale or not, try
	// to load the translations (for the default system language) here.

	const wxString& workingDir = wxGetCwd();

	// normally this wouldn't be necessary as the catalog files would be found
	// in the default locations, but when the program is not installed the
	// catalogs are in the build directory where we wouldn't find them by
	// default

	wxFileName fn(wxStandardPaths::Get().GetExecutablePath());

	wxLocale::AddCatalogLookupPathPrefix(workingDir + wxFILE_SEP_PATH + wxT("lang"));
	wxLocale::AddCatalogLookupPathPrefix(fn.GetPath() + wxFILE_SEP_PATH + wxT("lang"));

	// …AND, FOR A RUN FROM THE BUILD TREE, THE SOURCE'S OWN `lang` ABOVE THE EXECUTABLE — the walk-up the
	// syntax helper makes for its corpus (helpService.cpp; bin/Win32/Debug is three levels under it). The
	// build does not copy the catalogs beside the binaries (the nightly packages do), so a process started
	// anywhere but the source root found none and spoke English engine words between the configuration's
	// own: a document read "… 00000000003 from 06.07.2026" in a Russian session (2026-09-28).
	{
		wxFileName walk(fn.GetPath(), wxEmptyString);
		for (int level = 0; level < 6; ++level) {
			const wxString candidate = walk.GetPath() + wxFILE_SEP_PATH + wxT("lang");
			if (wxFileName::DirExists(candidate)) {
				wxLocale::AddCatalogLookupPathPrefix(candidate);
				break;
			}
			if (walk.GetDirCount() == 0)
				break;
			walk.RemoveLastDir();
		}
	}
#if defined(__WXOSX__) || defined(__APPLE__)
	// On macOS, also check outside .app bundle
	wxFileName bundleLang(fn.GetPath());
	bundleLang.RemoveLastDir(); bundleLang.RemoveLastDir(); bundleLang.RemoveLastDir();
	wxLocale::AddCatalogLookupPathPrefix(bundleLang.GetPath() + wxFILE_SEP_PATH + wxT("lang"));
#endif

	if (!m_locale.Init(m_localeLang)) {
		if (!m_locale.Init(wxLanguage::wxLANGUAGE_ENGLISH))
			return false;
		m_localeLang = wxLanguage::wxLANGUAGE_ENGLISH;
	}

	// Initialize the catalogs we'll be using.
	m_locale.AddCatalog(wxT("open_es"));

	// Initialize localization engine
	ibBackendLocalization::SetUserLanguage(m_locale.GetName());

	//Set default time
	wxDateTime::SetCountry(wxDateTime::Country::Country_Default);

	// Syntax-helper corpus comes up here — locale is settled.
	m_helpService.reset(new ibHelpService(ib::AppDataCtorToken{}, m_locale.GetCanonicalName()));

	return true;
}

///////////////////////////////////////////////////////////////////////////////
//	ibApplicationInstanceScope
///////////////////////////////////////////////////////////////////////////////

ibApplicationInstanceScope::ibApplicationInstanceScope(ibApplicationInstance* applicationInstance) :
	m_prev(t_instance)
{
	t_instance = applicationInstance;
}

ibApplicationInstanceScope::~ibApplicationInstanceScope()
{
	t_instance = m_prev;
}

ibApplicationInstance* ibApplicationInstanceScope::Current()
{
	return t_instance;
}
