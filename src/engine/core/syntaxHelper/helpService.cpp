/////////////////////////////////////////////////////////////////////////////
// ibHelpService — see helpService.h for the contract.
/////////////////////////////////////////////////////////////////////////////

#include <atomic>   // std::atomic — MSVC supplied this transitively
#include "core/syntaxHelper/helpService.h"

#include "core/syntaxHelper/helpCorpus.h"

#include "core/fileSystem.h"

namespace {

// Resolve the platform corpus directory. Returns the first root that
// contains either <root>/<locale>.hlk zip (prod layout) or
// <root>/<locale>/ directory (dev / unpacked). helpLoader's
// ResolveSource picks zip-first inside the chosen root. On no hit
// returns the prod path candidate — the loader will surface a kFatal
// and the service publishes an empty corpus snapshot.
ibString ResolvePlatformDir(const ibString& locale)
{
	const std::filesystem::path exeDir = ibPathOf(ibExecutablePath()).parent_path();
	const ibString programDir = ibProgramFolder();

	auto hasLocale = [&](const ibString& root) -> bool {
		return ibFileExists(root + ibFILE_SEP_PATH + locale + ibT(".hlk"))
		    || ibDirExists(root + ibFILE_SEP_PATH + locale);
	};

	// Candidate 1: <program folder>/help — prod layout. Inside a bundle that
	// folder is beside the .app, not Contents/MacOS/. PostBuildEvent copies
	// syntaxHelper/<locale>/ into here in 1.1; 1.1b will switch
	// the layout to <locale>.hlk and the service will gain a ZipSource.
	ibString platformDir = programDir + ibFILE_SEP_PATH + ibT("help");

#if defined(__IBOSX__)
	if (!hasLocale(platformDir)) {
		// Inside the bundle (Contents/Resources), which is not the program folder.
		const ibString c = ibString((exeDir.parent_path() / "Resources" / "help").wstring());
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
		std::filesystem::path walk = exeDir;
		for (int i = 0; i < 6; ++i) {
			const ibString candidate = ibString((walk / "syntaxHelper").wstring());
			if (hasLocale(candidate)) { platformDir = candidate; break; }
			if (!walk.has_relative_path()) break;   // the root: no folder left to leave
			walk = walk.parent_path();
		}
	}

	// Last resort: <cwd>/syntaxHelper — running from repo root.
	if (!hasLocale(platformDir)) {
		const ibString cwdCandidate = ibGetCwd() + ibFILE_SEP_PATH
			+ ibT("syntaxHelper");
		if (hasLocale(cwdCandidate)) platformDir = cwdCandidate;
	}

	return platformDir;
}

}   // namespace

ibHelpService::ibHelpService(const ibString& locale, const ibHelpClassName& className)
	: m_locale(CanonicaliseLocale(locale)), m_className(className)
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
	// When Apple Clang's libc++ has std::atomic<std::shared_ptr>
	// (portability.md § 1.11), switch m_corpus to it — the public
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

	const ibString platformDir = ResolvePlatformDir(m_locale);
	ibHelpLoadResult result =
	    LoadHelpCorpus(m_locale, platformDir, ibEmptyString, m_className);

	// Errors live inside the corpus snapshot — readers reach them via
	// GetCorpus()->LoadErrors(). No parallel error vector on the
	// service itself; one shared_ptr publish, both surfaces visible
	// together, no torn-publish data race possible.
	std::atomic_store_explicit(&m_corpus,
	                            result.corpus,
	                            std::memory_order_release);
}
