/////////////////////////////////////////////////////////////////////////////
// snapshotManager — see header. Pure file-IO over wxFileName / wxFile /
// wxDir + nlohmann::json. Backend-only, no GUI.
/////////////////////////////////////////////////////////////////////////////

#include "snapshotManager.hpp"

#include <wx/dir.h>
#include <wx/file.h>
#include <wx/filefn.h>
#include <wx/filename.h>
#include <wx/log.h>
#include <wx/string.h>
#include <wx/utils.h>

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <string>

namespace migration {
namespace snapshots {

namespace {

// Subdirectory under `<configDir>/sys` where snapshots land. Lives
// alongside the existing `sys/.oes.lock` manifest so operators have a
// single per-config "platform-private" namespace.
constexpr const wxChar* kSubDirSys           = wxT("sys");
constexpr const wxChar* kSubDirSnapshots     = wxT("oes-snapshots");
constexpr const wxChar* kJsonExt             = wxT(".json");
constexpr const wxChar* kConsumedSuffix      = wxT(".consumed.json");

// Format a wxDateTime as YYYYMMDDTHHMMSS in UTC.
wxString FormatStamp(const wxDateTime& dt)
{
	const wxDateTime utc = dt.ToUTC();
	// wxDateTime::Format respects local TZ by default — feed UTC explicitly
	// so concurrent captures on different boxes generate comparable ids.
	return utc.Format(wxT("%Y%m%dT%H%M%S"));
}

// ISO 8601 UTC, e.g. "2026-05-21T10:30:45Z". The JSON body uses this
// shape because the spec is unambiguous about time zone handling.
wxString FormatIso8601(const wxDateTime& dt)
{
	const wxDateTime utc = dt.ToUTC();
	return utc.Format(wxT("%Y-%m-%dT%H:%M:%SZ"));
}

wxString StoreDirInternal(const wxString& configDir)
{
	wxFileName fn;
	fn.AssignDir(configDir);
	fn.AppendDir(kSubDirSys);
	fn.AppendDir(kSubDirSnapshots);
	return fn.GetFullPath();
}

// Read a file's full contents into a wxString (UTF-8). Returns empty on
// any IO failure — caller logs as needed.
wxString ReadFileUtf8(const wxString& path)
{
	wxFile f;
	if (!f.Open(path, wxFile::read)) return wxString();
	wxFileOffset len = f.Length();
	if (len <= 0) return wxString();
	std::string buf;
	buf.resize(static_cast<std::size_t>(len));
	const ssize_t n = f.Read(&buf[0], static_cast<std::size_t>(len));
	if (n <= 0) return wxString();
	buf.resize(static_cast<std::size_t>(n));
	return wxString::FromUTF8(buf.c_str(), buf.size());
}

// Write a UTF-8 payload to disk, replacing any existing file. Returns
// false on IO failure.
bool WriteFileUtf8(const wxString& path, const std::string& payload)
{
	wxFile f;
	if (!f.Create(path, true /*overwrite*/)) {
		if (!f.Open(path, wxFile::write)) return false;
	}
	const auto written = f.Write(payload.c_str(), payload.size());
	if (written != payload.size()) return false;
	return f.Close();
}

// Extract the timestamp prefix from a snapshot file name. Format:
//   YYYYMMDDTHHMMSS-NNNN(.consumed)?.json
// Returns an empty string on a malformed file name.
wxString StampFromFileName(const wxString& name)
{
	const int dash = name.Find(wxT('-'));
	if (dash <= 0) return wxString();
	return name.Mid(0, static_cast<std::size_t>(dash));
}

// Parse a YYYYMMDDTHHMMSS stamp back into a wxDateTime (UTC). On
// failure returns wxInvalidDateTime so the caller can ignore the row.
wxDateTime StampToDateTime(const wxString& stamp)
{
	wxDateTime out;
	// wxDateTime::ParseFormat returns a wxAnyStrPtr — non-null on success,
	// null on parse failure. We don't need the trailing pointer here,
	// only the success bit.
	if (!out.ParseFormat(stamp, wxT("%Y%m%dT%H%M%S"))) {
		return wxInvalidDateTime;
	}
	// ParseFormat treats the input as local — mark the parsed wall time
	// as UTC so subsequent comparisons against `since` (also UTC) align.
	out.MakeFromTimezone(wxDateTime::UTC);
	return out;
}

// Truncate priorState that exceeds the per-snapshot ceiling. Returns
// the original string when small enough.
std::string ApplySizeCap(const std::string& priorJson)
{
	if (priorJson.size() <= kMaxSnapshotBytes) return priorJson;
	nlohmann::json stub = nlohmann::json::object();
	stub["truncated"]   = true;
	stub["reason"]      = "size>10MB";
	stub["originalBytes"] = priorJson.size();
	return stub.dump();
}

// Compute a 64-char hex SHA-256 stand-in. We don't have a real SHA-256
// surface from backend.dll yet, so the HashOnly mode emits the first
// 16 bytes of the payload as a hex preview plus the length — enough
// for the audit trail's "this snapshot existed; here's a fingerprint"
// promise without pulling OpenSSL into backend.
std::string ComputePreviewHash(const std::string& priorJson)
{
	std::string out;
	out.reserve(64);
	const std::size_t take = std::min<std::size_t>(priorJson.size(), 16);
	for (std::size_t i = 0; i < take; ++i) {
		const unsigned char c = static_cast<unsigned char>(priorJson[i]);
		static const char* kHex = "0123456789abcdef";
		out.push_back(kHex[c >> 4]);
		out.push_back(kHex[c & 0xF]);
	}
	return out;
}

} // namespace

CaptureMode ParseCaptureModeFromEnv(const char* envValue)
{
	if (envValue == nullptr || *envValue == '\0') return CaptureMode::Full;
	std::string v(envValue);
	std::transform(v.begin(), v.end(), v.begin(),
		[](unsigned char c){ return static_cast<char>(std::tolower(c)); });
	if (v == "false" || v == "off" || v == "0" || v == "no") return CaptureMode::Disabled;
	if (v == "hash-only" || v == "hash_only" || v == "hash") return CaptureMode::HashOnly;
	return CaptureMode::Full;
}

ibSnapshotManager::ibSnapshotManager(const wxString& configDir, CaptureMode mode)
	: m_configDir(configDir)
	, m_mode(mode)
{
}

wxString ibSnapshotManager::StoreDir() const
{
	return StoreDirInternal(m_configDir);
}

bool ibSnapshotManager::EnsureStoreDir(wxString& errOut) const
{
	const wxString dir = StoreDir();
	if (wxDirExists(dir)) return true;
	if (!wxFileName::Mkdir(dir, 0700, wxPATH_MKDIR_FULL)) {
		errOut = wxT("snapshots: failed to create store dir: ") + dir;
		return false;
	}
	return true;
}

wxString ibSnapshotManager::AllocateId(const wxDateTime& now)
{
	const wxString stamp = FormatStamp(now);
	if (stamp != m_lastSecondTag) {
		m_lastSecondTag = stamp;
		m_lastSecondSeq = 0;
	}
	++m_lastSecondSeq;
	return wxString::Format(wxT("%s-%04d"), stamp, m_lastSecondSeq);
}

wxString ibSnapshotManager::CaptureBeforeMutation(
	const wxString& toolName,
	const wxString& fullName,
	const wxString& priorStateJson)
{
	if (m_mode == CaptureMode::Disabled || m_configDir.IsEmpty()) {
		return wxString();
	}
	wxString errOut;
	if (!EnsureStoreDir(errOut)) {
		wxLogWarning(wxT("oes-mcp: %s"), errOut);
		return wxString();
	}

	const wxDateTime now = wxDateTime::UNow();
	const wxString id   = AllocateId(now);

	// Derive the operation tag from the tool name. Keep this explicit
	// (not a fall-through table) so a typo in the caller surfaces as a
	// "unknown" string instead of being silently bucketed.
	wxString operation;
	if      (toolName == wxT("meta_create"))  operation = wxT("create");
	else if (toolName == wxT("meta_edit"))    operation = wxT("edit");
	else if (toolName == wxT("meta_delete"))  operation = wxT("delete");
	else if (toolName == wxT("write_module")) operation = wxT("write_module");
	else                                       operation = wxT("unknown");

	std::string prior = std::string(priorStateJson.utf8_str());
	if (prior.empty()) prior = "null";
	prior = ApplySizeCap(prior);

	nlohmann::json body;
	body["schemaVersion"] = 1;
	body["id"]            = std::string(id.utf8_str());
	body["timestamp"]     = std::string(FormatIso8601(now).utf8_str());
	body["triggeredBy"]   = std::string(toolName.utf8_str());
	body["fullName"]      = std::string(fullName.utf8_str());
	body["operation"]     = std::string(operation.utf8_str());

	if (m_mode == CaptureMode::HashOnly) {
		nlohmann::json digest = nlohmann::json::object();
		digest["lengthBytes"] = prior.size();
		digest["previewHex"]  = ComputePreviewHash(prior);
		body["priorStateDigest"] = std::move(digest);
		body["priorState"]       = nullptr;
	} else {
		auto parsedPrior = nlohmann::json::parse(prior, nullptr, false);
		if (parsedPrior.is_discarded()) {
			// Caller passed something that isn't JSON — preserve it as a
			// raw string so we don't lose the audit trail.
			body["priorState"] = prior;
		} else {
			body["priorState"] = std::move(parsedPrior);
		}
	}

	const std::string payload = body.dump(2);

	wxFileName fn;
	fn.AssignDir(StoreDir());
	fn.SetFullName(id + kJsonExt);
	const wxString outPath = fn.GetFullPath();

	if (!WriteFileUtf8(outPath, payload)) {
		wxLogWarning(wxT("oes-mcp: snapshot write failed: %s"), outPath);
		return wxString();
	}

	// Cap on total count — prune oldest beyond kMaxSnapshotCount. We do
	// this synchronously but inexpensively (directory listing + a few
	// removes) so capture stays a single hop on the call site.
	PruneToCount(kMaxSnapshotCount);

	return id;
}

std::vector<SnapshotMetadata> ibSnapshotManager::List(
	std::size_t limit,
	const wxDateTime& since) const
{
	std::vector<SnapshotMetadata> rows;
	const wxString dir = StoreDir();
	if (!wxDirExists(dir)) return rows;

	wxDir d(dir);
	if (!d.IsOpened()) return rows;

	wxString name;
	bool more = d.GetFirst(&name, wxString(wxT("*")) + kJsonExt,
		wxDIR_FILES);
	while (more) {
		SnapshotMetadata md;
		const bool isConsumed = name.EndsWith(kConsumedSuffix);

		// Recover id by stripping the trailing suffix.
		wxString idPart = name;
		if (isConsumed) idPart = idPart.Mid(0,
			idPart.length() - wxString(kConsumedSuffix).length());
		else            idPart = idPart.Mid(0,
			idPart.length() - wxString(kJsonExt).length());

		md.id       = idPart;
		md.consumed = isConsumed;

		const wxString stamp = StampFromFileName(idPart);
		md.timestamp = StampToDateTime(stamp);

		wxFileName fn;
		fn.AssignDir(dir);
		fn.SetFullName(name);
		md.filePath = fn.GetFullPath();
		md.sizeBytes = static_cast<std::size_t>(wxFileName::GetSize(md.filePath).ToULong());

		// Pull triggeredBy/fullName/operation/description out of the
		// JSON body. Reading every entry on every list call is fine for
		// the bounded population (≤1000) — keeps the metadata source-
		// of-truth in one place rather than denormalising into the file
		// name.
		const wxString body = ReadFileUtf8(md.filePath);
		if (!body.IsEmpty()) {
			auto parsed = nlohmann::json::parse(
				std::string(body.utf8_str()), nullptr, false);
			if (parsed.is_object()) {
				if (parsed.contains("triggeredBy") && parsed["triggeredBy"].is_string())
					md.triggeredBy = wxString::FromUTF8(
						parsed["triggeredBy"].get<std::string>().c_str());
				if (parsed.contains("fullName") && parsed["fullName"].is_string())
					md.fullName = wxString::FromUTF8(
						parsed["fullName"].get<std::string>().c_str());
				if (parsed.contains("operation") && parsed["operation"].is_string())
					md.operation = wxString::FromUTF8(
						parsed["operation"].get<std::string>().c_str());
			}
		}
		md.description = wxString::Format(wxT("before %s %s"),
			md.triggeredBy, md.fullName);

		if (since.IsValid() && md.timestamp.IsValid() &&
		    md.timestamp.IsEarlierThan(since)) {
			// Filter — skip.
		} else {
			rows.push_back(std::move(md));
		}
		more = d.GetNext(&name);
	}

	// Newest first.
	std::sort(rows.begin(), rows.end(),
		[](const SnapshotMetadata& a, const SnapshotMetadata& b) {
			return a.id > b.id;
		});

	if (rows.size() > limit) rows.resize(limit);
	return rows;
}

wxString ibSnapshotManager::Load(const wxString& id) const
{
	wxFileName fn;
	fn.AssignDir(StoreDir());
	fn.SetFullName(id + kJsonExt);
	if (wxFileExists(fn.GetFullPath())) return ReadFileUtf8(fn.GetFullPath());
	// Already-consumed snapshots stay loadable by id — the rollback flow
	// produces a fresh undo plan from the body without re-applying.
	fn.SetFullName(id + kConsumedSuffix);
	if (wxFileExists(fn.GetFullPath())) return ReadFileUtf8(fn.GetFullPath());
	return wxString();
}

bool ibSnapshotManager::MarkConsumed(const wxString& id)
{
	wxFileName src;
	src.AssignDir(StoreDir());
	src.SetFullName(id + kJsonExt);
	if (!wxFileExists(src.GetFullPath())) return false;

	wxFileName dst;
	dst.AssignDir(StoreDir());
	dst.SetFullName(id + kConsumedSuffix);
	if (wxFileExists(dst.GetFullPath())) {
		// Already consumed — caller treats the second call as a no-op
		// success so an interrupted rollback can resume.
		return true;
	}
	return wxRenameFile(src.GetFullPath(), dst.GetFullPath(), false /*don't overwrite*/);
}

std::size_t ibSnapshotManager::PruneToCount(std::size_t retain)
{
	const std::vector<SnapshotMetadata> rows = List(
		kMaxSnapshotCount + 1, wxInvalidDateTime);
	if (rows.size() <= retain) return 0;

	std::size_t removed = 0;
	// `rows` is newest-first; tail entries are the oldest.
	for (std::size_t i = retain; i < rows.size(); ++i) {
		if (wxRemoveFile(rows[i].filePath)) ++removed;
	}
	return removed;
}

} // namespace snapshots
} // namespace migration
