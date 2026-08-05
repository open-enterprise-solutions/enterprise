#include "wfrontend.h"

////////////////////////////////////////////////////////////////////////////
// The single-page client, LOADED rather than compiled in.
//
// It used to be a 2 300-line raw string literal in this file, split into five
// pieces to get under MSVC's ~16 KB per-literal cap. That worked, and it cost
// three things every day: no highlighting, no linting, no browser devtools over
// the actual source — and a one-character fix to a script meant rebuilding
// backend and frontend.
//
// Now the client lives in `webClient/` as ordinary files, and this file finds
// them. The resolution is the SAME one the syntax helper uses (helpService.cpp):
//
//   1. <exe>/web/client.wpk   — a zip, what a release ships;
//   2. <exe>/web/client.html  — unpacked beside the binary;
//   3. walk up from <exe> looking for `webClient/` — the development tree, so a
//      dev run reads the file being edited with no build step at all.
//
// That last one is the point: during the web phase the client is edited
// constantly, and an edit-reload cycle that goes through a C++ build is the
// difference between an afternoon and a week. It is also what lets a customer
// replace the client with their own — the files are simply there.
//
// The result is cached: the client is served on every page load, and re-reading
// 100 KB per request would be a self-inflicted slowdown. A dev run picks changes
// up on restart, which is the same granularity a rebuild used to give.
////////////////////////////////////////////////////////////////////////////

#include <wx/filename.h>
#include <wx/stdpaths.h>
#include <wx/wfstream.h>
#include <wx/zipstrm.h>

#include <mutex>
#include <string>

namespace {

// A WORD, not an abbreviation of somebody else's format — the same rule the
// platform's other file types follow.
const char* const kPackName  = "client.wpk";
const char* const kEntryName = "client.html";

std::string ReadWholeFile(const wxString& path)
{
	wxFileInputStream input(path);
	if (!input.IsOk())
		return std::string();

	std::string out;
	out.resize(static_cast<std::size_t>(input.GetLength()));
	if (out.empty())
		return out;

	input.Read(&out[0], out.size());
	// A SHORT READ IS A REFUSAL, not a truncated page: half a client renders as
	// a blank window with a script error nobody traces back to here.
	if (input.LastRead() != out.size())
		return std::string();

	return out;
}

std::string ReadFromPack(const wxString& packPath)
{
	wxFileInputStream file(packPath);
	if (!file.IsOk())
		return std::string();

	// UTF-8 entry names: the pack is produced by the build step, and its entries
	// are plain leaf names — the same shape the help archives use.
	wxZipInputStream zip(file, wxConvUTF8);
	if (!zip.IsOk())
		return std::string();

	while (wxZipEntry* entry = zip.GetNextEntry()) {
		const wxString name = entry->GetName();
		delete entry;

		if (!name.IsSameAs(wxString::FromUTF8(kEntryName), /*caseSensitive*/ false))
			continue;

		std::string out;
		char buffer[8192];
		// Chunked: a zip stream is not random-access, and its entry size is the
		// COMPRESSED one, which is not what is being collected here.
		while (zip.CanRead()) {
			zip.Read(buffer, sizeof(buffer));
			const std::size_t got = static_cast<std::size_t>(zip.LastRead());
			if (got == 0)
				break;
			out.append(buffer, got);
		}
		return out;
	}

	return std::string();
}

// Where the client is, in the order a running binary should look.
std::string LoadClient()
{
	wxFileName exeFile(wxStandardPaths::Get().GetExecutablePath());
	const wxString exeDir = exeFile.GetPath();

	// 1. Packed, beside the binary — the release layout.
	const wxString packPath = exeDir + wxFILE_SEP_PATH + wxT("web")
		+ wxFILE_SEP_PATH + wxString::FromUTF8(kPackName);
	if (wxFileName::FileExists(packPath)) {
		const std::string packed = ReadFromPack(packPath);
		if (!packed.empty())
			return packed;
	}

	// 2. Unpacked, beside the binary — what an administrator adjusts on a live
	//    installation without touching the platform.
	const wxString besidePath = exeDir + wxFILE_SEP_PATH + wxT("web")
		+ wxFILE_SEP_PATH + wxString::FromUTF8(kEntryName);
	if (wxFileName::FileExists(besidePath)) {
		const std::string beside = ReadWholeFile(besidePath);
		if (!beside.empty())
			return beside;
	}

	// 3. The development tree. Bounded at six levels for the same reason the
	//    help resolver bounds it: a strange path must not become a walk to the
	//    filesystem root.
	wxFileName walk(exeDir, wxEmptyString);
	for (int i = 0; i < 6; ++i) {
		const wxString candidate = walk.GetPath() + wxFILE_SEP_PATH
			+ wxT("webClient") + wxFILE_SEP_PATH + wxString::FromUTF8(kEntryName);
		if (wxFileName::FileExists(candidate)) {
			const std::string dev = ReadWholeFile(candidate);
			if (!dev.empty())
				return dev;
		}
		if (walk.GetDirCount() == 0)
			break;
		walk.RemoveLastDir();
	}

	return std::string();
}

// SAID OUT LOUD, not served blank. A missing client is a deployment mistake —
// the pack was not copied, the directory was not shipped — and an empty page
// sends whoever sees it looking in the wrong place.
const char* const kMissingClient =
	"<!doctype html><html><head><meta charset=\"utf-8\">"
	"<title>OES Web</title></head><body>"
	"<h3>Web client not found</h3>"
	"<p>Expected <code>web/client.wpk</code> or <code>web/client.html</code> next to the "
	"executable, or a <code>webClient/</code> directory in the source tree.</p>"
	"</body></html>";

} // namespace

extern "C" WFRONTEND_API const char* wfrontendClientHTML()
{
	// Loaded once, and the failure is explicit: an empty load falls back to the
	// message above rather than being retried on every page request.
	static std::once_flag s_once;
	static std::string    s_client;

	std::call_once(s_once, [] {
		s_client = LoadClient();
	});

	return s_client.empty() ? kMissingClient : s_client.c_str();
}
