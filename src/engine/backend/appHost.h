#ifndef __APP_HOST_H__
#define __APP_HOST_H__

// ibApplicationHost — THE PROCESS, and the bases it serves.
//
// A process holds one base — every host of today: the desktop client, the designer, codeRunner, the web
// host, the tests — or several: the application server. Each base is an ibApplicationInstance. What belongs to the process
// rather than to any base lives here, once: the plugins (DLLs loaded into the process, each initialised
// once), the platform locale (wxLocale is process state) and the syntax-helper corpus that follows it.
// See docs/private/multi-base-process.md.
//
// There is NO global current base. `appData`, `activeMetaData`, `db_query` and the static accessors of
// ibApplicationInstance keep their spelling and answer through the SESSION (ibApplicationInstance::Get — the
// session's application data, else the base the thread is bound to).

#include "backend/appData.h"

#include <atomic>
#include <memory>
#include <mutex>
#include <vector>

#include <wx/intl.h>

class ibPluginManager;
class ibWorkerPool;

class BACKEND_API ibApplicationHost {
public:

	// The process. Null before its first base is opened and after the last one is closed.
	static ibApplicationHost* Get() { return s_applicationHost.get(); }

	// Does the process hold no base? The gate for every "before / after application data" question. It must
	// not depend on WHICH base, or asking it would need the answer first.
	static bool IsEmpty() { return s_instanceCount.load(std::memory_order_acquire) == 0; }

	// Every base this process holds, in the order they were opened.
	static std::vector<ibApplicationInstance*> GetInstances();

	// Does the process still hold the base this registry / this pool belongs to? Asked by pointer, without
	// touching it — for a session or a connection holder that may outlive its base on the way out.
	static bool HasRegistry(const class ibSessionRegistry* registry);
	static bool HasPool(const class ibConnectionPool* pool);

	// A base's OWN SERVICE names the thread it runs on — the registry thread, the job tick — so the journal can
	// say which base their lines are about. For the journal only: the service holds its base and asks IT; this
	// answers nothing else.
	static void SetThreadOwner(const ibApplicationInstance* applicationInstance);

	// --- the core family ---

	ibPluginManager* GetPluginManager() const { return m_pluginManager.get(); }

	// The threads sessions run their work on — ONE POOL FOR THE PROCESS, whatever number of bases it holds:
	// threads grow with the process, not with its bases (docs/private/multi-base-process.md § 4.4). A worker
	// binds the session of the task it runs, and the base comes with the session. Null where nothing runs
	// sessions in the background (the launcher).
	ibWorkerPool*    GetWorkerPool() const    { return m_workerPool.get(); }

	// The connections to its DBMS a base may hold when its own infobase.conf does not say — backend.conf
	// (Connections), else the built-in 32. A base's connections are its own; this is only their default.
	std::size_t      GetDefaultConnections() const;

	// How many client connections the application server will hold at once — backend.conf
	// (ClientConnections). Left out, or 0, that is kDefaultClientConnections, not "no limit".
	// Above kMostClientConnections the most is used, and the journal says so. Each one keeps a
	// thread until the network core is asynchronous.
	static constexpr std::size_t kDefaultClientConnections = 1000;
	static constexpr std::size_t kMostClientConnections    = 10000;
	std::size_t      GetClientConnections() const { return m_configClientConnections; }
	ibHelpService*   GetHelpService() const   { return m_helpService.get(); }
	wxString         GetLocale() const        { return m_locale.GetCanonicalName(); }
	wxString         GetLocaleName() const    { return m_locale.GetName(); }

	// The platform locale, set once for the process: the first base to ask settles it, and a later base's
	// wish is not a second locale — wxLocale is process state. False when no catalog could be set at all.
	bool InitLocale(const wxString& locale);

	// A count from a settings file — backend.conf, a base's infobase.conf: left out or 0 means the default, and
	// one below `least` is said in the journal and the default is used. `most` of 0 is no ceiling; a number
	// above `most` is said and `most` is used.
	static std::size_t ReadCount(const class wxConfigBase& conf, const wxString& file, const wxString& key, long least,
		std::size_t byDefault, std::size_t most = 0);

private:

	// Opening and closing a base is ibApplicationInstance's own lifecycle; it drives these.
	friend class ibApplicationInstance;
	friend struct std::default_delete<ibApplicationHost>;   // s_applicationHost

	explicit ibApplicationHost(ibRunMode runMode);
	~ibApplicationHost();

	// The process — it comes up with its first base — with room for one more: refused (thrown) when it already
	// holds as many as backend.conf allows (Bases). Asked by every opener before a database is touched.
	static ibApplicationHost* Ensure(ibRunMode runMode);

	// Takes ownership — the process is the one owner of every base — and answers the base it now holds.
	static ibApplicationInstance* Adopt(std::unique_ptr<ibApplicationInstance> applicationInstance);

	// THE THREAD THAT OPENS A BASE WORKS FOR IT — while the base comes up, and afterwards, until it opens
	// another or the base closes: the host goes on to log into it from that thread. Thread-local, not global.
	static void SetThreadInstance(ibApplicationInstance* applicationInstance);

	// Closes one base in two steps. It is STOPPED WHILE IT IS STILL LISTED — its teardown reads `appData`, the
	// journal and the pool through the very accessors this set answers, and a session removing its row asks
	// HasRegistry — and only then taken out of the set and freed, an empty shell by then. The process stays.
	static void Close(ibApplicationInstance* applicationInstance);

	// Every base, newest first, and then the process itself.
	static void CloseAll();

	// The journal's question "which base is this line about" (ibTechJournal::SetContext): the name of the
	// base the writing thread works for — its session's, else the base it is bound to, else the one whose
	// service it is — and nothing for a thread that works for none. Never throws, never waits, never writes
	// to the journal.
	static wxString GetJournalContext();

	void ReadBackendConf();

	// NOT "s_host": winsock2.h defines it as a macro (in_addr::S_un.S_un_b.s_b2) — see pluginHost.cpp.
	static std::unique_ptr<ibApplicationHost>                  s_applicationHost;
	static std::mutex                                          s_mutex;
	static std::vector<std::unique_ptr<ibApplicationInstance>> s_instances;       // their owner; in the order opened
	static std::atomic<std::size_t>                            s_instanceCount;   // its size, read without the lock

	// backend.conf — read once for the process: the locale, how much the process may consume, and the default
	// a base's own infobase.conf overrides (Workers and Bases: the most there may be, 0 = no limit; Connections 0 =
	// the built-in value). ClientConnections is the application server's own ceiling: 0 means
	// kDefaultClientConnections, and a number above kMostClientConnections is that most.
	wxString    m_configLocale;
	std::size_t m_configWorkers            = 0;
	std::size_t m_configBases              = 0;
	std::size_t m_configConnections        = 0;
	std::size_t m_configClientConnections  = kDefaultClientConnections;

	wxLocale m_locale;
	int      m_localeLang;

	std::unique_ptr<ibPluginManager> m_pluginManager;
	std::unique_ptr<ibHelpService>   m_helpService;
	std::unique_ptr<ibWorkerPool>    m_workerPool;
};

// ⭐ A THREAD THAT WORKS FOR ONE BASE WITHOUT A SESSION — a base being closed, a read a model runs on a
// thread of its own for whoever asked — binds it for as long as it works. The base's own services do NOT:
// they hold their base (the registry, the job manager, the MCP server, the debugger through its metadata).
// The twin of ibSessionScope, and restored the same way when it ends.
// …AND HOLDS THE CONFIGURATION IT WORKS IN, as a session does: the base's active one, acquired for the length of
// the work, so a replacement meanwhile does not take it from under the thread (Max, 2026-10-06 — the shared wx
// icons' disease, cured by the sessions' rule). A scope must therefore end while its base still stands.
class BACKEND_API ibApplicationInstanceScope {
public:
	explicit ibApplicationInstanceScope(ibApplicationInstance* applicationInstance);
	~ibApplicationInstanceScope();

	ibApplicationInstanceScope(const ibApplicationInstanceScope&)            = delete;
	ibApplicationInstanceScope& operator=(const ibApplicationInstanceScope&) = delete;

	// The base this thread works for without a session — bound by a scope, or by opening it; null when none is.
	static ibApplicationInstance* Current();

	// The configuration the innermost scope of this thread holds — null when none does (no scope, or a base with
	// no configuration yet).
	static class ibMetaDataConfigurationBase* GetMetaData();

private:
	ibApplicationInstance* m_prev;
	std::shared_ptr<class ibMetaDataConfigurationBase> m_metaData;
	class ibMetaDataConfigurationBase* m_prevMetaData;
};

#endif
