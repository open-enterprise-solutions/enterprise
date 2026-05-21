/////////////////////////////////////////////////////////////////////////////
// ibAiTodoStore implementation. See header for contract.
//
// We reuse ibChatHistory::ComputeConfigHash so TODO rows and chat
// transcripts live in side-by-side buckets keyed by the same hash. The
// hash itself is computed inside the chat-history module to keep the FNV-1a
// constants in one place.
//
// File I/O follows the same atomic-rename pattern as chatHistory — write
// to "<path>.tmp" first, then rename. The pattern keeps a partially
// written file from corrupting state when Designer crashes mid-save.
/////////////////////////////////////////////////////////////////////////////

#include "aiTodoStore.h"

#include "frontend/pluginWebPane/chatHistory.h"  // ComputeConfigHash reuse
#include "3rdparty/nlohmann/json.hpp"

#include <wx/filename.h>
#include <wx/stdpaths.h>
#include <wx/wfstream.h>
#include <wx/datetime.h>
#include <wx/log.h>

#include <atomic>
#include <regex>
#include <string>

namespace ibAiTodoStore {
namespace {

// Storage root: <user-config>/OES/ai-todo/. Created on demand. The
// directory tree is intentionally separate from the chat bucket so
// stale TODO rows don't make a "New chat" delete sweep harder.
wxString StorageRoot()
{
	const wxString base = wxStandardPaths::Get().GetUserConfigDir();
	if (base.IsEmpty()) return wxEmptyString;
	wxFileName dir(base, wxEmptyString);
	dir.AppendDir(wxT("OES"));
	dir.AppendDir(wxT("ai-todo"));
	const wxString path = dir.GetPath();
	if (!wxFileName::DirExists(path)) {
		wxFileName::Mkdir(path, 0700, wxPATH_MKDIR_FULL);
	}
	return path;
}

} // namespace

wxString ComputeConfigHash()
{
	// Pure delegation — same hash, same bucket, same key derivation.
	// If the chat layer ever swaps algorithms we move with it for free.
	return ibChatHistory::ComputeConfigHash();
}

wxString PathForBucket(const wxString& configHash)
{
	const wxString root = StorageRoot();
	if (root.IsEmpty()) return wxEmptyString;
	// Same SEC-P1-11 sanitisation as chatHistory: refuse paths that
	// could escape the bucket directory.
	static const std::regex kHexRe(R"(^[0-9a-f]{16}$)");
	const std::string asNarrow(configHash.utf8_str());
	const wxString safe = std::regex_match(asNarrow, kHexRe)
	    ? configHash
	    : wxString(wxT("default"));
	wxFileName fn(root, safe + wxT(".json"));
	return fn.GetFullPath();
}

bool Save(const wxString& configHash, const std::vector<Item>& items)
{
	const wxString path = PathForBucket(configHash);
	if (path.IsEmpty()) return false;

	// Trim to cap. Prefer dropping completed items first — a long pending
	// list IS the value, so dropping a completed row loses less signal.
	std::vector<Item> trimmed = items;
	if (trimmed.size() > kMaxItems) {
		// Stable sort: completed items appear last (so erase from end).
		std::stable_sort(trimmed.begin(), trimmed.end(),
		    [](const Item& a, const Item& b) {
		        const bool aDone = (a.status == wxT("done"));
		        const bool bDone = (b.status == wxT("done"));
		        if (aDone != bDone) return !aDone;  // pending first
		        return a.createdAt < b.createdAt;
		    });
		trimmed.erase(trimmed.begin() + kMaxItems, trimmed.end());
	}

	nlohmann::json doc;
	doc["version"] = 1;
	doc["savedAt"] = std::string(
	    wxDateTime::UNow().FormatISOCombined().utf8_str());

	nlohmann::json arr = nlohmann::json::array();
	for (const auto& it : trimmed) {
		nlohmann::json j;
		j["id"]          = std::string(it.id.utf8_str());
		j["title"]       = std::string(it.title.utf8_str());
		j["status"]      = std::string(it.status.utf8_str());
		j["planId"]      = std::string(it.planId.utf8_str());
		j["createdAt"]   = std::string(it.createdAt.utf8_str());
		j["completedAt"] = std::string(it.completedAt.utf8_str());
		arr.push_back(std::move(j));
	}
	doc["items"] = std::move(arr);

	const std::string payload = doc.dump(2);
	const wxString tmpPath = path + wxT(".tmp");

	{
		wxFileOutputStream out(tmpPath);
		if (!out.IsOk()) {
			wxLogWarning(wxT("aiTodoStore: cannot open %s for write"),
			             tmpPath);
			return false;
		}
		out.Write(payload.data(), payload.size());
		if (out.GetLastError() != wxSTREAM_NO_ERROR) {
			wxLogWarning(wxT("aiTodoStore: write error on %s"), tmpPath);
			return false;
		}
	}

#ifdef __WXMSW__
	if (wxFileName::FileExists(path)) wxRemoveFile(path);
#endif
	if (!wxRenameFile(tmpPath, path, /*overwrite=*/true)) {
		wxLogWarning(wxT("aiTodoStore: rename %s -> %s failed"),
		             tmpPath, path);
		if (wxFileName::FileExists(tmpPath)) wxRemoveFile(tmpPath);
		return false;
	}
	return true;
}

bool Load(const wxString& configHash, std::vector<Item>& items)
{
	items.clear();
	const wxString path = PathForBucket(configHash);
	if (path.IsEmpty() || !wxFileName::FileExists(path)) return false;

	wxFileInputStream in(path);
	if (!in.IsOk()) return false;

	std::string buf;
	buf.resize(static_cast<size_t>(in.GetLength()));
	if (!buf.empty()) {
		in.Read(&buf[0], buf.size());
	}

	auto doc = nlohmann::json::parse(buf, nullptr, /*allow_exceptions=*/false);
	if (doc.is_discarded() || !doc.is_object()) {
		wxLogWarning(wxT("aiTodoStore: corrupt JSON at %s"), path);
		return false;
	}
	if (!doc.contains("items") || !doc["items"].is_array()) {
		return false;
	}

	for (const auto& j : doc["items"]) {
		if (!j.is_object()) continue;
		Item it;
		it.id          = wxString::FromUTF8(j.value("id",          "").c_str());
		it.title       = wxString::FromUTF8(j.value("title",       "").c_str());
		it.status      = wxString::FromUTF8(j.value("status",      "pending").c_str());
		it.planId      = wxString::FromUTF8(j.value("planId",      "").c_str());
		it.createdAt   = wxString::FromUTF8(j.value("createdAt",   "").c_str());
		it.completedAt = wxString::FromUTF8(j.value("completedAt", "").c_str());
		items.push_back(std::move(it));
	}

	if (items.size() > kMaxItems) {
		items.erase(items.begin(), items.begin() + (items.size() - kMaxItems));
	}
	return true;
}

wxString NewId()
{
	// Counter survives across calls but resets on Designer restart — that's
	// fine because the millisecond component differs across restarts and
	// the bucketed id-space is finite anyway (500 items, kMaxItems).
	static std::atomic<unsigned long long> s_counter{0};
	const auto ms   = wxDateTime::UNow().GetValue().GetValue();
	const auto seq  = s_counter.fetch_add(1, std::memory_order_relaxed);
	return wxString::Format(wxT("todo-%lld-%llu"),
	                         static_cast<long long>(ms),
	                         static_cast<unsigned long long>(seq));
}

wxString NowIso()
{
	return wxDateTime::UNow().FormatISOCombined();
}

} // namespace ibAiTodoStore
