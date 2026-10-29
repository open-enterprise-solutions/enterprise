////////////////////////////////////////////////////////////////////////////
//	Description : the process and the bases it serves
////////////////////////////////////////////////////////////////////////////

#include "backend/appHost.h"
#include "core/diagnostics/journal.h"   // a locale this machine cannot grant is said, not refused

#include <algorithm>

#include <wx/fileconf.h>
#include <wx/filename.h>
#include <wx/stdpaths.h>

#include "backend/backend_exception.h"   // a process with no room for another base refuses it
#include "backend/backend_localization.h"
#include "backend/metadataConfiguration.h"   // a scope holds the configuration it works in
#include "backend/plugin/pluginManager.h"
#include "core/fiber/fiberLocals.h"
#include "core/fileSystem.h"   // backend.conf and lang/ sit in the program folder, not inside a bundle
#include "backend/session/session.h"   // ibSession::CurrentCached — the journal's first question
#include "backend/session/workerPoolHeadless.h"   // the process's worker pool
#include "backend/compiler/value.h"   // ibValue::IsRegisterCtor / GetNameObjectFromID — the registry the help corpus checks by
#include "core/syntaxHelper/helpService.h"

std::unique_ptr<ibApplicationHost>                  ibApplicationHost::s_applicationHost;
std::mutex                                          ibApplicationHost::s_mutex;
std::vector<std::unique_ptr<ibApplicationInstance>> ibApplicationHost::s_instances;
std::atomic<std::size_t>                            ibApplicationHost::s_instanceCount{ 0 };

// The base a thread with no session works for (ibApplicationInstanceScope, SetThreadInstance).
static thread_local ibApplicationInstance* t_instance = nullptr;

// …and the configuration it works in, held by its innermost ibApplicationInstanceScope.
static thread_local ibMetaDataConfigurationBase* t_metaData = nullptr;

// The base whose service runs on this thread (SetThreadOwner) — for the journal's lines only.
static thread_local const ibApplicationInstance* t_owner = nullptr;

namespace {

// A question parked on this thread has a base. The next session on the
// thread must not journal or resolve metadata as that base.
struct ibRegisterAppHostLocal {
	ibRegisterAppHostLocal()
	{
		ibFiberLocals::RegisterTrivial<ibApplicationInstance*>(
			[](void* dst) { *static_cast<ibApplicationInstance**>(dst) = t_instance; },
			[](const void* src) { t_instance = *static_cast<ibApplicationInstance* const*>(src); });
		ibFiberLocals::RegisterTrivial<ibMetaDataConfigurationBase*>(
			[](void* dst) { *static_cast<ibMetaDataConfigurationBase**>(dst) = t_metaData; },
			[](const void* src) { t_metaData = *static_cast<ibMetaDataConfigurationBase* const*>(src); });
		ibFiberLocals::RegisterTrivial<const ibApplicationInstance*>(
			[](void* dst) { *static_cast<const ibApplicationInstance**>(dst) = t_owner; },
			[](const void* src) { t_owner = *static_cast<const ibApplicationInstance* const*>(src); });
	}
} s_registerAppHostLocal;

// ⭐ THE WORDS THIS THREAD SPEAKS (ibDictionary, core/localization/dictionary.h): the dictionary of the base it
// works for - the session's base, else the base the thread is bound to, else the base whose service runs on it -
// and null for a thread that works for no base, or for a base that names no locale: the process's language then.
// A pointer read off the base, no lock on the way: _() is said a dozen times per control made, on every thread.
const ibDictionary* DictionaryOfThisThreadsBase()
{
	if (ibApplicationHost::IsEmpty())
		return nullptr;
	const ibSession* const session = ibSession::CurrentCached();
	const ibApplicationInstance* base = session != nullptr ? session->GetApplicationInstance() : nullptr;
	if (base == nullptr)
		base = t_instance;
	if (base == nullptr)
		base = t_owner;
	return base != nullptr ? base->GetDictionary() : nullptr;
}

} // namespace

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
	// ⚠ A RUN MODE NOBODY SAID IS REFUSED, not guessed: whether the process serves its bases or holds one as a file
	// base decides its pool, who may share the base with it and what its first session is — a default picking one
	// would pick it silently. Every opener comes through here first, before a database is touched.
	if (runMode == eUNKNOWN_MODE) {
		ibBackendCoreException::Error(_("The process did not say how it holds the base: the run mode is unknown."));
		return nullptr;
	}

	// Startup is one thread, and the ctor loads plugins, whose initialise hook must be free to ask anything —
	// so no lock is held around it.
	if (s_applicationHost == nullptr) {
		s_applicationHost.reset(new ibApplicationHost(runMode));
		ibTechJournal::SetContext(&ibApplicationHost::GetJournalContext);
		ibDictionary::SetSource(&DictionaryOfThisThreadsBase);
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

ibString ibApplicationHost::GetJournalContext()
{
	if (s_instanceCount.load(std::memory_order_acquire) == 0)
		return ibString();

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
	return applicationInstance != nullptr ? applicationInstance->GetInstanceName() : ibString();
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
	// `appData` and the accessors. The shell outlives the scope — what the scope holds of the base (its
	// configuration) is let go while the base still stands.
	std::unique_ptr<ibApplicationInstance> shell;
	{
		ibApplicationInstanceScope closing(applicationInstance);

		// 1. Closed WHILE LISTED — its sessions, removing their rows as the registry stops, ask HasRegistry. (A
		//    unique_ptr's reset() empties the slot before it deletes: freed at once, it is unlisted for its whole
		//    teardown.)
		applicationInstance->Close();

		// 2. Then out of the set, and freed outside the lock — an empty shell by now.
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
	shell.reset();
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
	ibDictionary::SetSource(nullptr);   // core outlives this library; its _() must not call back into it
	s_applicationHost.reset();
}

///////////////////////////////////////////////////////////////////////////////
//	The process
///////////////////////////////////////////////////////////////////////////////

ibApplicationHost::ibApplicationHost(ibRunMode runMode) :
	m_localeLang(wxLanguage::wxLANGUAGE_UNKNOWN),
	m_pluginManager(std::unique_ptr<ibPluginManager>(new ibPluginManager(ib::AppDataCtorToken{})))
{
	ReadBackendConf();

	// ⭐ THE WORKERS GROW WITH THE WORK, up to the config's word — 0 sets no limit. A worker is spawned only when none
	// is idle, and a session is worked by one at a time, so the workers follow the sessions that have work in hand,
	// whatever the process is. It used to be guessed from the run mode (the server 4 × cores, every other host 2),
	// and the web host, opening its base as a file one, was left with two for all its people (2026-10-06). The
	// launcher runs no session.
	if (runMode != eLAUNCHER_MODE)
		m_workerPool = std::make_unique<ibWorkerPoolHeadless>(m_configWorkers);

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

#define BACKEND_CONF ibT("backend.conf")

std::size_t ibApplicationHost::ReadCount(const wxConfigBase& conf, const ibString& file, const ibString& key, long least,
	std::size_t byDefault, std::size_t most)
{
	long configured = 0;
	if (!conf.Read(key, &configured) || configured == 0)   // left out, or 0 — the default, on purpose
		return byDefault;
	if (configured < least) {
		// A value that cannot be used is SAID, not quietly replaced.
		ibTechJournal::Print(ibJournalMark::Warning, ibT("engine"),
			ibT("%s: %s = %ld is less than %ld - the default is used"), file, key, configured, least);
		return byDefault;
	}
	if (most != 0 && configured > static_cast<long>(most)) {
		ibTechJournal::Print(ibJournalMark::Warning, ibT("engine"),
			ibT("%s: %s = %ld is more than %ld - %ld is used"), file, key, configured,
			static_cast<long>(most), static_cast<long>(most));
		return most;
	}
	return static_cast<std::size_t>(configured);
}

std::size_t ibApplicationHost::ResumeSeconds(bool present, long configured)
{
	// The same 30 minutes a connectionless client dies by when nobody calls (kIdleLimit). A window longer than that
	// would hold a dropped client's locks and seat for longer than any client is kept unheard.
	constexpr long kMost = 30 * 60;
	constexpr std::size_t kDefault = 120;
	if (!present)
		return kDefault;
	if (configured == 0)
		return 0;
	if (configured < 0) {
		ibTechJournal::Print(ibJournalMark::Warning, ibT("engine"),
			ibT("%s: Resume = %ld is not a duration - 120 is used"), BACKEND_CONF, configured);
		return kDefault;
	}
	if (configured > kMost) {
		ibTechJournal::Print(ibJournalMark::Warning, ibT("engine"),
			ibT("%s: Resume = %ld is above %ld seconds - %ld is used"), BACKEND_CONF, configured, kMost, kMost);
		return static_cast<std::size_t>(kMost);
	}
	return static_cast<std::size_t>(configured);
}

void ibApplicationHost::ReadBackendConf()
{
	const ibString strConfigFile = ibBackendConfPath();

	// Read as written, as infobase.conf is: a Windows path keeps its backslashes (the \t of C:\tls\server.key is not a tab).
	wxFileConfig fc(wxT(""), wxT(""), wxT(""), strConfigFile,
		wxCONFIG_USE_LOCAL_FILE | wxCONFIG_USE_GLOBAL_FILE | wxCONFIG_USE_NO_ESCAPE_CHARACTERS);
	m_configLocale = fc.Read(wxT("Locale"), m_configLocale);   // what was there, when the file says nothing

	// HOW MUCH THE PROCESS MAY CONSUME — said by whoever runs it, not compiled in. Workers and Bases are the most there
	// may be: left out, or 0, no limit.
	m_configWorkers = ReadCount(fc, BACKEND_CONF, ibT("Workers"), 1, 0);
	m_configBases   = ReadCount(fc, BACKEND_CONF, ibT("Bases"), 1, 0);
	// The application server's client ceiling. Left out, or 0: a thousand — not "no limit", which is what
	// Workers and Bases mean by 0. Above the most, the most is used. See clientListener.cpp — a connection
	// holds a thread, and this is how many of those threads the process will make.
	m_configClientConnections = ReadCount(fc, BACKEND_CONF, ibT("ClientConnections"), 1,
		kDefaultClientConnections, kMostClientConnections);
	// The connections are a base's own — its infobase.conf says them; this is only the default for a base that does
	// not. Two at least: the registry holds one for its writes, a session needs another.
	m_configConnections = ReadCount(fc, BACKEND_CONF, ibT("Connections"), 2, 0);
	// Resume is not ReadCount: there 0 means the default, and here 0 means the window is off.
	long resume = 0;
	const bool resumePresent = fc.Read(wxT("Resume"), &resume);
	m_configResume = ResumeSeconds(resumePresent, resume);
	// The certificate the listeners speak TLS over, named relative to this file's folder (core/server/listener.h).
	m_configTls = ibTlsFiles::FromConf(fc, wxFileName(strConfigFile).GetPath());

	// (THE MCP SERVER'S SETTINGS ARE NOT HERE. They belong to a PERSON in a
	//  BASE — the server is started from an authenticated designer session and
	//  has nothing to say before one exists — so they live in sys_settings with
	//  every other saved setting, under their own category. See
	//  ibMcpServer::LoadSettings. A copy in this file would be a second road,
	//  and the two would disagree the first time somebody edited the page.)
}

bool ibApplicationHost::InitLocale(const ibString& locale)
{
	// SETTLED ALREADY — by the first base. That is success, not a refusal: a later base asks for the locale
	// the process speaks, and it has one. (This used to answer false on a second call, which on a base of its
	// own never came; on a process of several it would fail every base after the first.)
	if (m_localeLang != wxLanguage::wxLANGUAGE_UNKNOWN)
		return true;

#ifdef DEBUG_TRANSLATE
	wxLog::AddTraceMask(wxS("i18n"));
#endif // WXDEBUG

	// The base's word, else backend.conf's — when this machine can set it. NOTHING NAMED, OR A WISH THIS MACHINE CANNOT
	// GRANT, IS ENGLISH — the language the engine's words are written in — never the machine's own: a process that
	// speaks whatever the system does answers differently on every machine it runs on (a test reading an English
	// sentence failed on a desktop whose Windows speaks Russian, 2026-10-28). A wish not granted is not a refusal to
	// open the base (a base names its locale on one machine and is opened on every other); the journal says which.
	// US English: wx's plain ENGLISH is en_GB, a locale a machine may not have made; en_US is the one it nearly always has.
	m_localeLang = wxLanguage::wxLANGUAGE_ENGLISH_US;
	const ibString& wanted = !locale.IsEmpty() ? locale : m_configLocale;
	const wxLanguageInfo* const wish = !wanted.IsEmpty() ? wxLocale::FindLanguageInfo(wanted) : nullptr;
	if (wish != nullptr && wxLocale::IsAvailable(wish->Language))
		m_localeLang = wish->Language;
	else if (!wxLocale::IsAvailable(m_localeLang))
		m_localeLang = wxLanguage::wxLANGUAGE_DEFAULT;   // a machine with no English locale made (a bare Linux): what it has

	// Independently of whether we succeeded to set the locale or not, try
	// to load the translations (for the language settled above) here.

	const ibString& workingDir = ibGetCwd();

	// normally this wouldn't be necessary as the catalog files would be found
	// in the default locations, but when the program is not installed the
	// catalogs are in the build directory where we wouldn't find them by
	// default

	wxFileName fn(wxStandardPaths::Get().GetExecutablePath());

	wxLocale::AddCatalogLookupPathPrefix(workingDir + wxFILE_SEP_PATH + ibT("lang"));
	wxLocale::AddCatalogLookupPathPrefix(ibProgramFolder() + wxFILE_SEP_PATH + ibT("lang"));

	// …AND, FOR A RUN FROM THE BUILD TREE, THE SOURCE'S OWN `lang` ABOVE THE EXECUTABLE — the walk-up the
	// syntax helper makes for its corpus (helpService.cpp; bin/Win32/Debug is three levels under it). The
	// build does not copy the catalogs beside the binaries (the nightly packages do), so a process started
	// anywhere but the source root found none and spoke English engine words between the configuration's
	// own: a document read "… 00000000003 from 06.07.2026" in a Russian session (2026-09-28).
	{
		wxFileName walk(fn.GetPath(), ibEmptyString);
		for (int level = 0; level < 6; ++level) {
			const ibString candidate = walk.GetPath() + wxFILE_SEP_PATH + ibT("lang");
			if (ibDirExists(candidate)) {
				wxLocale::AddCatalogLookupPathPrefix(candidate);
				break;
			}
			if (walk.GetDirCount() == 0)
				break;
			walk.RemoveLastDir();
		}
	}

	// Init once — a second call on the object asserts and leaves wx's locale pointer dangling — so the language is
	// settled above; a refusal says why instead of a bare false.
	if (!m_locale.Init(m_localeLang)) {
		const wxLanguageInfo* const info = wxLocale::GetLanguageInfo(m_localeLang);
		ibBackendCoreException::Error(_("The locale %s cannot be set on this machine; the base was not opened."),
			info != nullptr ? ibString(info->GetCanonicalWithRegion()) : ibString(ibT("(default)")));
	}
	if (wish != nullptr && wish->Language != m_localeLang)
		ibJournalWarning(ibT("session.locale"), ibT("the locale %s is not supported on this machine; the process speaks %s"),
			wish->GetCanonicalWithRegion(), m_locale.GetCanonicalName());

	// Initialize the catalogs we'll be using.
	m_locale.AddCatalog(ibString(kEngineDomain));
	// THE ENGINE'S OWN WORDS come through its own catalogs (core/localization/dictionary.h): one set per
	// language, chosen per thread by the base it works for; the process's language is this one. wxLocale keeps its copy of the catalog above for wx's own
	// windows.
	ibDictionary::SetProcessLanguage(m_locale.GetCanonicalName());

	// ⭐ NO CATALOG OF OURS FOR THE LANGUAGE — NOTHING TO ASK. The engine's strings stay as they are written (English
	// has no catalog: it is what they are written in), and a translations object with nothing of ours misses on every
	// one of them — and wx TRACES a miss (wxLogTrace in GetTranslatedString), taking a process-wide lock for it
	// (wxLog::GetComponentLevel). Every control and every property made says a dozen words: sixty logins at once kept
	// about two thirds of the application server's busy threads waiting on that one lock (2026-10-06, Debug, cdb).
	// Without the object, `_()` hands the string back at once.
	if (wxTranslations* const translations = wxTranslations::Get())
		if (!translations->IsLoaded(ibString(kEngineDomain)))
			wxTranslations::Set(nullptr);

	// Initialize localization engine
	ibBackendLocalization::SetUserLanguage(m_locale.GetName());

	//Set default time
	wxDateTime::SetCountry(wxDateTime::Country::Country_Default);

	// Syntax-helper corpus comes up here — locale is settled. An article's class_id is checked by the backend's
	// registry of classes.
	m_helpService.reset(new ibHelpService(m_locale.GetCanonicalName(), [](ibClassID id) {
		return ibValue::IsRegisterCtor(id) ? ibValue::GetNameObjectFromID(id) : ibString();
	}));

	return true;
}

///////////////////////////////////////////////////////////////////////////////
//	ibApplicationInstanceScope
///////////////////////////////////////////////////////////////////////////////

ibApplicationInstanceScope::ibApplicationInstanceScope(ibApplicationInstance* applicationInstance) :
	m_prev(t_instance),
	m_prevMetaData(t_metaData)
{
	if (ibMetaDataConfigurationBase* const active = ibApplicationInstance::GetActiveMetaData(applicationInstance))
		m_metaData = std::static_pointer_cast<ibMetaDataConfigurationBase>(active->shared_from_this());
	t_instance = applicationInstance;
	t_metaData = m_metaData.get();
}

ibApplicationInstanceScope::~ibApplicationInstanceScope()
{
	t_instance = m_prev;
	t_metaData = m_prevMetaData;
}

ibApplicationInstance* ibApplicationInstanceScope::Current()
{
	return t_instance;
}

ibMetaDataConfigurationBase* ibApplicationInstanceScope::GetMetaData()
{
	return t_metaData;
}
