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
#include <regex>           // SEC-P1-11: validate configHash before path use
#include <sstream>
#include <iomanip>
#include <string>

namespace ibChatHistory {
namespace {

const char* RoleToString(ibPluginWebPane::Entry::Role role)
{
	using R = ibPluginWebPane::Entry::Role;
	switch (role) {
	case R::User:         return "user";
	case R::Assistant:    return "assistant";
	case R::System:       return "system";
	case R::Error:        return "error";
	case R::Plan:         return "plan";
	case R::TripleReview: return "tripleReview";
	case R::Subtasks:     return "subtasks";
	}
	return "user";
}

ibPluginWebPane::Entry::Role RoleFromString(const std::string& s)
{
	using R = ibPluginWebPane::Entry::Role;
	if (s == "user")         return R::User;
	if (s == "assistant")    return R::Assistant;
	if (s == "system")       return R::System;
	if (s == "error")        return R::Error;
	if (s == "plan")         return R::Plan;
	if (s == "tripleReview") return R::TripleReview;
	if (s == "subtasks")     return R::Subtasks;
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

// SEC-P1-11: std::hash<std::string> is libc++-implementation-defined,
// collides between releases, and on some platforms returns a 32-bit value
// padded into size_t — meaning the printed hex was effectively 32 bits
// of entropy. Roll FNV-1a 64-bit instead: deterministic across builds,
// no DOS-resistance salt needed because the bucket key is local (no
// adversarial network input), and produces a stable 16-char lowercase
// hex digest matching the existing on-disk file layout.
namespace {
std::uint64_t Fnv1a64(const std::string& s)
{
	std::uint64_t h = 0xcbf29ce484222325ULL;
	for (unsigned char c : s) {
		h ^= c;
		h *= 0x100000001b3ULL;
	}
	return h;
}
} // namespace

wxString ComputeConfigHashFor(const wxString& configName)
{
	const std::string key = configName.IsEmpty()
	    ? std::string("default")
	    : std::string(configName.utf8_str());
	std::ostringstream oss;
	oss << std::hex << std::setw(16) << std::setfill('0')
	    << Fnv1a64(key);
	return wxString::FromUTF8(oss.str().c_str());
}

wxString PathForBucket(const wxString& configHash)
{
	const wxString root = StorageRoot();
	if (root.IsEmpty()) return wxEmptyString;
	// SEC-P1-11: validate the hash string before using it as a file name.
	// A hostile caller passing "../../etc/passwd" would otherwise escape
	// the bucket dir. Fall back to "default" on mismatch — preserves the
	// "we always have a usable path" contract callers rely on.
	static const std::regex kHexRe(R"(^[0-9a-f]{16}$)");
	const std::string asNarrow(configHash.utf8_str());
	const wxString safe = std::regex_match(asNarrow, kHexRe)
	    ? configHash
	    : wxString(wxT("default"));
	wxFileName fn(root, safe + wxT(".json"));
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

		// TripleReview-only fields. Written for every role so the schema
		// stays uniform; readers that don't know these fields just ignore
		// them, and roles that don't use them carry zero/empty defaults.
		if (e.role == ibPluginWebPane::Entry::Role::TripleReview) {
			j["verdict"]       = std::string(e.verdict.utf8_str());
			j["verdictReason"] = std::string(e.verdictReason.utf8_str());
			j["countP0"]       = e.countP0;
			j["countP1"]       = e.countP1;
			j["countP2"]       = e.countP2;
			j["countP3"]       = e.countP3;

			nlohmann::json reviewers = nlohmann::json::array();
			for (const auto& r : e.reviewers) {
				nlohmann::json rj;
				rj["model"]     = std::string(r.model.utf8_str());
				rj["content"]   = std::string(r.content.utf8_str());
				rj["p0"]        = r.p0;
				rj["p1"]        = r.p1;
				rj["p2"]        = r.p2;
				rj["p3"]        = r.p3;
				rj["latencyMs"] = r.latencyMs;
				reviewers.push_back(std::move(rj));
			}
			j["reviewers"] = std::move(reviewers);

			nlohmann::json findings = nlohmann::json::array();
			for (const auto& f : e.findings) {
				nlohmann::json fj;
				fj["severity"] = std::string(f.severity.utf8_str());
				fj["reviewer"] = std::string(f.reviewer.utf8_str());
				fj["line"]     = f.line;
				fj["message"]  = std::string(f.message.utf8_str());
				fj["fix"]      = std::string(f.fix.utf8_str());
				findings.push_back(std::move(fj));
			}
			j["findings"] = std::move(findings);
		}

		// Subtasks-only fields. Same uniform-schema approach as TripleReview:
		// written only on the Subtasks role; absent on other entries so
		// readers that pre-date the field default cleanly to empty.
		if (e.role == ibPluginWebPane::Entry::Role::Subtasks) {
			j["parentPlanId"] = std::string(e.parentPlanId.utf8_str());
			nlohmann::json subtasks = nlohmann::json::array();
			for (const auto& s : e.subtasks) {
				nlohmann::json sj;
				sj["id"]     = std::string(s.id.utf8_str());
				sj["title"]  = std::string(s.title.utf8_str());
				sj["status"] = std::string(s.status.utf8_str());
				nlohmann::json deps = nlohmann::json::array();
				for (const auto& d : s.dependsOn) {
					deps.push_back(std::string(d.utf8_str()));
				}
				sj["dependsOn"] = std::move(deps);
				subtasks.push_back(std::move(sj));
			}
			j["subtasks"] = std::move(subtasks);
		}
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

		// TripleReview-only fields. Missing keys (older files / non-tripleReview
		// roles) parse as defaults — backward compatible by construction.
		e.verdict       = wxString::FromUTF8(j.value("verdict",       "").c_str());
		e.verdictReason = wxString::FromUTF8(j.value("verdictReason", "").c_str());
		e.countP0       = j.value("countP0", 0);
		e.countP1       = j.value("countP1", 0);
		e.countP2       = j.value("countP2", 0);
		e.countP3       = j.value("countP3", 0);
		if (j.contains("reviewers") && j["reviewers"].is_array()) {
			for (const auto& r : j["reviewers"]) {
				if (!r.is_object()) continue;
				ibPluginWebPane::Entry::ReviewerSummary rs;
				rs.model     = wxString::FromUTF8(r.value("model",   "").c_str());
				rs.content   = wxString::FromUTF8(r.value("content", "").c_str());
				rs.p0        = r.value("p0", 0);
				rs.p1        = r.value("p1", 0);
				rs.p2        = r.value("p2", 0);
				rs.p3        = r.value("p3", 0);
				rs.latencyMs = r.value("latencyMs", 0L);
				e.reviewers.push_back(std::move(rs));
			}
		}
		if (j.contains("findings") && j["findings"].is_array()) {
			for (const auto& f : j["findings"]) {
				if (!f.is_object()) continue;
				ibPluginWebPane::Entry::Finding fd;
				fd.severity = wxString::FromUTF8(f.value("severity", "").c_str());
				fd.reviewer = wxString::FromUTF8(f.value("reviewer", "").c_str());
				fd.line     = f.value("line", 0);
				fd.message  = wxString::FromUTF8(f.value("message",  "").c_str());
				fd.fix      = wxString::FromUTF8(f.value("fix",      "").c_str());
				e.findings.push_back(std::move(fd));
			}
		}

		// Subtasks-only fields. Older files (pre-Subtasks landing) miss
		// these keys; the value() defaults restore an empty list, matching
		// the "missing entry" semantic for non-Subtasks roles.
		e.parentPlanId = wxString::FromUTF8(j.value("parentPlanId", "").c_str());
		if (j.contains("subtasks") && j["subtasks"].is_array()) {
			for (const auto& st : j["subtasks"]) {
				if (!st.is_object()) continue;
				ibPluginWebPane::Entry::Subtask s;
				s.id     = wxString::FromUTF8(st.value("id",     "").c_str());
				s.title  = wxString::FromUTF8(st.value("title",  "").c_str());
				s.status = wxString::FromUTF8(st.value("status", "").c_str());
				if (st.contains("dependsOn") && st["dependsOn"].is_array()) {
					for (const auto& d : st["dependsOn"]) {
						if (d.is_string()) {
							s.dependsOn.push_back(
							    wxString::FromUTF8(d.get<std::string>().c_str()));
						}
					}
				}
				e.subtasks.push_back(std::move(s));
			}
		}
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
