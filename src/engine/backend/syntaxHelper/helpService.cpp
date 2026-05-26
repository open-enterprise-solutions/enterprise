/////////////////////////////////////////////////////////////////////////////
// ibHelpService — see helpService.h for the contract.
/////////////////////////////////////////////////////////////////////////////

#include "backend/syntaxHelper/helpService.h"

#include "backend/syntaxHelper/helpCorpus.h"
#include "backend/syntaxHelper/helpLoader.h"

#include <wx/filename.h>
#include <wx/stdpaths.h>

namespace {

// Resolve the platform corpus directory. Returns the first root that
// contains either <root>/<locale>.hlk zip (prod layout) or
// <root>/<locale>/ directory (dev / unpacked). helpLoader's
// ResolveSource picks zip-first inside the chosen root. On no hit
// returns the prod path candidate — the loader will surface a kFatal
// and the service publishes an empty corpus snapshot.
wxString ResolvePlatformDir(const wxString& locale)
{
	wxFileName exeFile(wxStandardPaths::Get().GetExecutablePath());
	const wxString exeDir = exeFile.GetPath();

	auto hasLocale = [&](const wxString& root) -> bool {
		return wxFileName::FileExists(root + wxFILE_SEP_PATH + locale + wxT(".hlk"))
		    || wxFileName::DirExists(root + wxFILE_SEP_PATH + locale);
	};

	// Candidate 1: <exe>/help — prod layout. PostBuildEvent copies
	// syntaxHelper/<locale>/ into here in 1.1; 1.1b will switch
	// the layout to <locale>.hlk and the service will gain a ZipSource.
	wxString platformDir = exeDir + wxFILE_SEP_PATH + wxT("help");

#if defined(__WXOSX__) || defined(__APPLE__)
	if (!hasLocale(platformDir)) {
		// macOS .app bundle sibling — exe lives in
		// MyApp.app/Contents/MacOS/, three RemoveLastDir walks out.
		wxFileName bundleSibling(exeDir, wxEmptyString);
		bundleSibling.RemoveLastDir();
		bundleSibling.RemoveLastDir();
		bundleSibling.RemoveLastDir();
		const wxString c = bundleSibling.GetPath() + wxFILE_SEP_PATH + wxT("help");
		if (hasLocale(c)) platformDir = c;
	}
	if (!hasLocale(platformDir)) {
		// Alt macOS layout — bundled inside Resources.
		wxFileName resources(exeDir, wxEmptyString);
		resources.RemoveLastDir();
		resources.AppendDir(wxT("Resources"));
		const wxString c = resources.GetPath() + wxFILE_SEP_PATH + wxT("help");
		if (hasLocale(c)) platformDir = c;
	}
#endif

	// Dev fallback: walk up from <exe> looking for syntaxHelper.
	// Typical layouts:
	//   bin/Win32/Debug/                 3 walk-ups to repo root
	//   build/bin/                       2 walk-ups
	//   build/src/engine/X/X.app/...     varies (macOS)
	// Cap at 6 levels — bounded, no infinite loop on weird paths.
	if (!hasLocale(platformDir)) {
		wxFileName walk(exeDir, wxEmptyString);
		for (int i = 0; i < 6; ++i) {
			const wxString candidate = walk.GetPath() + wxFILE_SEP_PATH
				+ wxT("syntaxHelper");
			if (hasLocale(candidate)) { platformDir = candidate; break; }
			if (walk.GetDirCount() == 0) break;
			walk.RemoveLastDir();
		}
	}

	// Last resort: <cwd>/syntaxHelper — running from repo root.
	if (!hasLocale(platformDir)) {
		const wxString cwdCandidate = wxGetCwd() + wxFILE_SEP_PATH
			+ wxT("syntaxHelper");
		if (hasLocale(cwdCandidate)) platformDir = cwdCandidate;
	}

	return platformDir;
}

}   // namespace

ibHelpService::ibHelpService(ib::AppDataCtorToken, const wxString& locale)
	: m_locale(CanonicaliseLocale(locale))
{
	Rebuild();
}

ibHelpService::~ibHelpService() = default;

std::shared_ptr<const ibHelpCorpus> ibHelpService::GetCorpus() const
{
	// Atomic-load. No lock on the hot path — readers never race with
	// the publisher because the handle is an atomically-loaded
	// shared_ptr and the pointed-at corpus is immutable. The empty-
	// fallback corpus is published in the ctor (via Rebuild), so this
	// load returns non-null in the normal lifecycle.
	//
	// When the project moves to C++20, switch m_corpus to
	// std::atomic<std::shared_ptr<const ibHelpCorpus>> — the public
	// ABI of GetCorpus / Reload does not change.
	return std::atomic_load_explicit(&m_corpus,
	                                  std::memory_order_acquire);
}

void ibHelpService::Reload()
{
	Rebuild();
}

void ibHelpService::Rebuild()
{
	// Serialise concurrent rebuild requests so two builders don't
	// publish in arbitrary order (Phase 5 may fire reload from a
	// configuration save while a manual hot-key also triggers it).
	// The mutex covers BOTH the build AND the atomic_store —
	// latest waiter wins.
	std::lock_guard<std::mutex> lk(m_mutex);

	const wxString platformDir = ResolvePlatformDir(m_locale);
	ibHelpLoadResult result =
	    LoadHelpCorpus(m_locale, platformDir, wxEmptyString);

	// Errors live inside the corpus snapshot — readers reach them via
	// GetCorpus()->LoadErrors(). No parallel error vector on the
	// service itself; one shared_ptr publish, both surfaces visible
	// together, no torn-publish data race possible.
	std::atomic_store_explicit(&m_corpus,
	                            result.corpus,
	                            std::memory_order_release);
}
