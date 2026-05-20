/////////////////////////////////////////////////////////////////////////////
// ibChatHistory implementation — see header for the contract.
//
// Implementation notes:
//   - JSON via the vendored nlohmann/json header. Same library the rest of
//     the pane uses for envelope dispatch; one parser, one dump path.
//   - File I/O via wx streams to stay portable. The atomic-rename pattern
//     is the same one ibPasswordHash uses for credential rotation.
//   - GetConfigName() returns wxString from activeMetaData; we resolve the
//     pointer lazily (it may be nullptr in tests).
/////////////////////////////////////////////////////////////////////////////

#include "frontend/pluginWebPane/chatHistory.h"

#include "3rdparty/nlohmann/json.hpp"

#include "backend/metadataConfiguration.h"

#include <wx/filename.h>
#include <wx/stdpaths.h>
#include <wx/wfstream.h>
#include <wx/datetime.h>
#include <wx/log.h>
#include <wx/dir.h>

#include <cstdint>
#include <functional>
#include <sstream>
#include <iomanip>
#include <string>

namespace ibChatHistory {
namespace {

const char* RoleToString(ibPluginWebPane::Entry::Role role)
{
	using R = ibPluginWebPane::Entry::Role;
	switch (role) {
	case R::User:      return "user";
	case R::Assistant: return "assistant";
	case R::System:    return "system";
	case R::Error:     return "error";
	case R::Plan:      return "plan";
	}
	return "user";
}

ibPluginWebPane::Entry::Role RoleFromString(const std::string& s)
{
	using R = ibPluginWebPane::Entry::Role;
	if (s == "user")      return R::User;
	if (s == "assistant") return R::Assistant;
	if (s == "system")    return R::System;
	if (s == "error")     return R::Error;
	if (s == "plan")      return R::Plan;
	return R::User;
}

// Root storage directory: <user-config>/OES/chat/. Created on demand.
wxString StorageRoot()
{
	const wxString base = wxStandardPaths::Get().GetUserConfigDir();
	if (base.IsEmpty()) return wxEmptyString;
	wxFileName dir(base, wxEmptyString);
	dir.AppendDir(wxT("OES"));
	dir.AppendDir(wxT("chat"));
	const wxString path = dir.GetPath();
	if (!wxFileName::DirExists(path)) {
		// Mkdir with full chain. Permissions default to user-only via
		// wx's wxPATH_MKDIR_FULL on POSIX (umask-dependent). Acceptable
		// for a per-user cache — the same policy Designer uses for
		// other state under ~/Library/Preferences/OES.
		wxFileName::Mkdir(path, 0700, wxPATH_MKDIR_FULL);
	}
	return path;
}

} // namespace

wxString ComputeConfigHash()
{
	wxString name;
	// activeMetaData macro can resolve to nullptr if no configuration is
	// loaded yet (designer cold-start, tests). Guard against that.
	if (ibMetaDataConfigurationBase* cfg = ibMetaDataConfigurationBase::Get()) {
		name = cfg->GetConfigName();
	}
	return ComputeConfigHashFor(name);
}

wxString ComputeConfigHashFor(const wxString& configName)
{
	std::string key = configName.IsEmpty()
	    ? std::string("default")
	    : std::string(configName.utf8_str());
	const std::size_t h = std::hash<std::string>{}(key);
	std::ostringstream oss;
	oss << std::hex << std::setw(16) << std::setfill('0')
	    << static_cast<std::uint64_t>(h);
	return wxString::FromUTF8(oss.str().c_str());
}

wxString PathForBucket(const wxString& configHash)
{
	const wxString root = StorageRoot();
	if (root.IsEmpty()) return wxEmptyString;
	wxFileName fn(root, configHash + wxT(".json"));
	return fn.GetFullPath();
}

bool Save(const wxString& configHash,
          const std::vector<ibPluginWebPane::Entry>& entries)
{
	const wxString path = PathForBucket(configHash);
	if (path.IsEmpty()) return false;

	// Trim to cap (drop oldest). Caller owns the original vector; we
	// build a view here without mutating it.
	const size_t start = (entries.size() > kMaxEntries)
	    ? entries.size() - kMaxEntries
	    : 0;

	nlohmann::json doc;
	doc["version"] = 1;
	doc["savedAt"] = std::string(
	    wxDateTime::UNow().FormatISOCombined().utf8_str());

	nlohmann::json arr = nlohmann::json::array();
	for (size_t i = start; i < entries.size(); ++i) {
		const auto& e = entries[i];
		nlohmann::json j;
		j["role"]           = RoleToString(e.role);
		j["markdown"]       = std::string(e.markdown.utf8_str());
		j["requestId"]      = std::string(e.requestId.utf8_str());
		j["planId"]         = std::string(e.planId.utf8_str());
		j["conversationId"] = std::string(e.conversationId.utf8_str());
		j["rationale"]      = std::string(e.rationale.utf8_str());
		j["mutations"]      = std::string(e.mutations.utf8_str());
		j["applied"]        = e.applied;
		arr.push_back(std::move(j));
	}
	doc["entries"] = std::move(arr);

	const std::string payload = doc.dump(2);
	const wxString tmpPath = path + wxT(".tmp");

	{
		wxFileOutputStream out(tmpPath);
		if (!out.IsOk()) {
			wxLogWarning(wxT("chatHistory: cannot open %s for write"),
			             tmpPath);
			return false;
		}
		out.Write(payload.data(), payload.size());
		if (out.GetLastError() != wxSTREAM_NO_ERROR) {
			wxLogWarning(wxT("chatHistory: write error on %s"), tmpPath);
			return false;
		}
	}

	// Atomic rename — overwrites any prior file. wxRenameFile returns
	// false on Windows if the destination already exists, so we delete
	// the target first there. POSIX rename(2) overwrites silently.
#ifdef __WXMSW__
	if (wxFileName::FileExists(path)) wxRemoveFile(path);
#endif
	if (!wxRenameFile(tmpPath, path, /*overwrite=*/true)) {
		wxLogWarning(wxT("chatHistory: rename %s -> %s failed"),
		             tmpPath, path);
		// Best-effort: drop the tmp file so retries don't leak.
		if (wxFileName::FileExists(tmpPath)) wxRemoveFile(tmpPath);
		return false;
	}
	return true;
}

bool Load(const wxString& configHash,
          std::vector<ibPluginWebPane::Entry>& entries)
{
	entries.clear();
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
		wxLogWarning(wxT("chatHistory: corrupt JSON at %s"), path);
		return false;
	}
	if (!doc.contains("entries") || !doc["entries"].is_array()) {
		return false;
	}

	for (const auto& j : doc["entries"]) {
		if (!j.is_object()) continue;
		ibPluginWebPane::Entry e;
		e.role           = RoleFromString(j.value("role", "user"));
		e.markdown       = wxString::FromUTF8(j.value("markdown",       "").c_str());
		e.requestId      = wxString::FromUTF8(j.value("requestId",      "").c_str());
		e.planId         = wxString::FromUTF8(j.value("planId",         "").c_str());
		e.conversationId = wxString::FromUTF8(j.value("conversationId", "").c_str());
		e.rationale      = wxString::FromUTF8(j.value("rationale",      "").c_str());
		e.mutations      = wxString::FromUTF8(j.value("mutations",      "").c_str());
		e.applied        = j.value("applied", false);
		// codeBlocks not persisted; RenderTranscript re-extracts.
		entries.push_back(std::move(e));
	}

	// Defensive cap-enforce on load (file may have been hand-edited).
	if (entries.size() > kMaxEntries) {
		entries.erase(entries.begin(),
		              entries.begin() + (entries.size() - kMaxEntries));
	}
	return true;
}

bool Clear(const wxString& configHash)
{
	const wxString path = PathForBucket(configHash);
	if (path.IsEmpty()) return true;
	if (!wxFileName::FileExists(path)) return true;
	return wxRemoveFile(path);
}

} // namespace ibChatHistory
