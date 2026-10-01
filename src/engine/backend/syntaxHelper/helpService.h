/////////////////////////////////////////////////////////////////////////////
// ibHelpService — process-wide owner of the syntax-helper corpus snapshot.
//
// Separates "where the corpus lives + how it's reloaded" from appData,
// keeping appData free of the candidate-path-search and atomic-publish
// boilerplate (design v5 §3.1 keeps the corpus owned by appData; the
// service is just the encapsulating object — appData holds a
// unique_ptr<ibHelpService> like it holds the logger / pluginManager).
//
// Hot path: GetCorpus() — atomic_load of an immutable shared_ptr. No
// lock taken. Snapshot pointer stays valid for as long as the caller
// keeps it (§3.4 lifetime contract).
//
// Reload: Reload() — serialised by an internal mutex (latest waiter
// wins), runs LoadHelpCorpus over the resolved platform dir, atomically
// publishes the new snapshot. Old snapshots survive until their last
// reader releases.
//
// Path search order (first existing <root>/<locale>/ wins):
//   1. <exe>/help/                              prod layout — PostBuildEvent
//                                                copies syntaxHelper into
//                                                bin/<Platform>/<Config>/help/
//   2. <exe>/../../../help/                     macOS .app bundle sibling
//   3. <exe>/../Resources/help/                 alt macOS layout
//   4. walk-up from <exe> → <root>/syntaxHelper   dev fallback (up to 6 levels)
//   5. <cwd>/syntaxHelper                   running from repo root
/////////////////////////////////////////////////////////////////////////////

#ifndef _IB_HELP_SERVICE_H_
#define _IB_HELP_SERVICE_H_

#include "backend/backend.h"
#include "backend/appDataCtorToken.h"

#include <memory>
#include <mutex>

class BACKEND_API ibHelpCorpus;

class BACKEND_API ibHelpService {
public:
	// ib::AppDataCtorToken gate — appData is the owning coordinator;
	// no external code constructs the service directly. Same pattern as
	// ibLockManager / ibPluginManager / ibSessionRegistry. `locale` is
	// the canonical OES locale code ("ru-RU" / "uk-UA" / "en-US"). Ctor
	// publishes an initial corpus snapshot synchronously.
	ibHelpService(ib::AppDataCtorToken, const wxString& locale);
	~ibHelpService();

	ibHelpService(const ibHelpService&)            = delete;
	ibHelpService& operator=(const ibHelpService&) = delete;

	// Hot path — atomic-load of the published snapshot. Never returns
	// nullptr: on total load failure the snapshot is an empty corpus
	// whose LoadErrors() carries the diagnostics.
	std::shared_ptr<const ibHelpCorpus> GetCorpus() const;

	// Rebuild from disk; atomically swap on completion. Serialised by
	// the internal mutex — concurrent reload requests collapse to
	// latest-wins, no torn-publish window.
	void Reload();

	// Locale this service was constructed for. Currently immutable —
	// changing locale mid-session would need a fresh service.
	const wxString& GetLocale() const { return m_locale; }

private:
	void Rebuild();   // body of Reload() + ctor — assumes lock held.

	wxString m_locale;
	mutable std::shared_ptr<const ibHelpCorpus> m_corpus;
	mutable std::mutex m_mutex;
};

#endif // _IB_HELP_SERVICE_H_
