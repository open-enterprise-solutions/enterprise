/////////////////////////////////////////////////////////////////////////////
// pluginInstaller implementation. See header for the bundle layout.
/////////////////////////////////////////////////////////////////////////////

#include "pluginInstaller.h"
#include "pluginManager.h"
#include "byokEnv.h"

#include "3rdparty/nlohmann/json.hpp"

#include <wx/filename.h>
#include <wx/wfstream.h>
#include <wx/zipstrm.h>
#include <wx/dir.h>
#include <wx/log.h>

#include <fstream>
#include <memory>
#include <vector>

#ifndef _WIN32
#include <unistd.h>
#endif

namespace pluginInstaller {
namespace {

// Stream a single zip entry into outPath (creating parent dirs).
bool ExtractEntry(wxZipInputStream& zip, wxZipEntry& entry,
                   const wxString& outPath, wxString* errorMsg)
{
	if (entry.IsDir()) {
		wxFileName::Mkdir(outPath, 0755, wxPATH_MKDIR_FULL);
		return true;
	}
	wxFileName fn(outPath);
	fn.Mkdir(0755, wxPATH_MKDIR_FULL);

	wxFileOutputStream out(outPath);
	if (!out.IsOk()) {
		if (errorMsg) *errorMsg = wxString::Format(wxT("write %s failed"), outPath);
		return false;
	}
	if (!zip.OpenEntry(entry)) {
		if (errorMsg) *errorMsg = wxString::Format(wxT("open zip entry '%s' failed"),
		                                              entry.GetName());
		return false;
	}
	zip.Read(out);
	return zip.LastRead() > 0 || entry.GetSize() == 0;
}

// Walk every entry; pull the one named "manifest.json" into outBuf.
// Returns true when the file was found and parsed; false on missing /
// malformed.
bool FindAndReadManifest(const wxString& zipPath, std::string& outBuf,
                          wxString* errorMsg)
{
	wxFileInputStream f(zipPath);
	if (!f.IsOk()) {
		if (errorMsg) *errorMsg = wxT("zip file open failed");
		return false;
	}
	wxZipInputStream zip(f);
	if (!zip.IsOk()) {
		if (errorMsg) *errorMsg = wxT("not a zip archive");
		return false;
	}
	std::unique_ptr<wxZipEntry> entry;
	while ((entry.reset(zip.GetNextEntry())), entry.get() != nullptr) {
		const wxString name = entry->GetName();
		if (name == wxT("manifest.json")) {
			if (!zip.OpenEntry(*entry)) {
				if (errorMsg) *errorMsg = wxT("could not open manifest.json");
				return false;
			}
			outBuf.clear();
			char buf[4096];
			while (zip.CanRead()) {
				zip.Read(buf, sizeof(buf));
				outBuf.append(buf, zip.LastRead());
				if (zip.LastRead() == 0) break;
			}
			return true;
		}
	}
	if (errorMsg) *errorMsg = wxT("manifest.json not found in archive");
	return false;
}

// Recursive directory remove. wxDir::Rmdir handles non-empty? No — we
// roll it ourselves because plugin dirs contain nested webview/ etc.
bool RmRf(const wxString& root)
{
	if (!wxDirExists(root)) return true;
	wxDir dir(root);
	if (!dir.IsOpened()) return false;
	wxString name;
	bool more = dir.GetFirst(&name);
	while (more) {
		const wxString sub = root + wxFILE_SEP_PATH + name;
		if (wxDirExists(sub)) {
			if (!RmRf(sub)) return false;
		} else {
			wxRemoveFile(sub);
		}
		more = dir.GetNext(&name);
	}
	return wxRmdir(root);
}

} // namespace

bool ReadManifest(const wxString& zipPath, Manifest& out, wxString* errorMsg)
{
	std::string raw;
	if (!FindAndReadManifest(zipPath, raw, errorMsg)) return false;
	auto j = nlohmann::json::parse(raw, nullptr, /*allow_exceptions*/ false);
	if (j.is_discarded() || !j.is_object()) {
		if (errorMsg) *errorMsg = wxT("manifest.json is not valid JSON object");
		return false;
	}
	if (auto it = j.find("pluginId"); it != j.end() && it->is_string()) {
		out.pluginId = it->get<std::string>();
	}
	if (auto it = j.find("version"); it != j.end() && it->is_string()) {
		out.version = it->get<std::string>();
	}
	if (auto it = j.find("abiVersion"); it != j.end() && it->is_number_integer()) {
		out.abiVersion = it->get<int>();
	}
	if (auto it = j.find("binary"); it != j.end() && it->is_string()) {
		out.binary = it->get<std::string>();
	}
	if (out.pluginId.empty() || out.version.empty() ||
	    out.abiVersion <= 0 || out.binary.empty()) {
		if (errorMsg) *errorMsg = wxT("manifest.json missing required fields (pluginId, version, abiVersion, binary)");
		return false;
	}
	// Reject path traversal in binary (we extract under <pluginsDir>/<pluginId>).
	if (out.binary.find("..") != std::string::npos ||
	    out.binary.find('\\') != std::string::npos ||
	    (!out.binary.empty() && out.binary.front() == '/')) {
		if (errorMsg) *errorMsg = wxT("manifest.binary must be a relative path within the bundle");
		return false;
	}
	return true;
}

InstallResult Install(const wxString& zipPath, bool allowOverwrite,
                       wxString* errorMsg)
{
	Manifest m;
	if (!ReadManifest(zipPath, m, errorMsg)) {
		return InstallResult::BadManifest;
	}
	// ABI gate — host supports v3 + v4 currently (kAbiMin..kAbiMax in
	// pluginManager.cpp). Re-check here so a stale .zip from a too-new
	// release doesn't silently install.
	if (m.abiVersion < 3 || m.abiVersion > IB_PLUGIN_ABI_VERSION) {
		if (errorMsg) {
			*errorMsg = wxString::Format(
			    wxT("manifest.abiVersion %d outside host range [3, %d]"),
			    m.abiVersion, IB_PLUGIN_ABI_VERSION);
		}
		return InstallResult::AbiUnsupported;
	}

	const wxString pluginsDir = ibPluginManager::GetPluginsDir();
	const wxString pluginDir  = pluginsDir + wxFILE_SEP_PATH +
	                              wxString::FromUTF8(m.pluginId);
	const wxString versionDir = pluginDir + wxFILE_SEP_PATH +
	                              wxString::FromUTF8(m.version);

	if (wxDirExists(versionDir) && !allowOverwrite) {
		if (errorMsg) {
			*errorMsg = wxString::Format(
			    wxT("plugin %s %s already installed; use overwrite to replace"),
			    wxString::FromUTF8(m.pluginId), wxString::FromUTF8(m.version));
		}
		return InstallResult::AlreadyExists;
	}

	// Extract into a sibling .new dir first so an interrupted install
	// can't corrupt the live <version> dir.
	const wxString stagingDir = versionDir + wxT(".new");
	RmRf(stagingDir);
	if (!wxFileName::Mkdir(stagingDir, 0755, wxPATH_MKDIR_FULL)) {
		if (errorMsg) *errorMsg = wxT("could not create staging dir");
		return InstallResult::IoError;
	}

	wxFileInputStream f(zipPath);
	if (!f.IsOk()) {
		if (errorMsg) *errorMsg = wxT("zip open failed");
		RmRf(stagingDir);
		return InstallResult::IoError;
	}
	wxZipInputStream zip(f);
	// SEC-P1-7: compute the canonical staging root once for prefix check.
	wxFileName stagingRoot(stagingDir, wxEmptyString);
	stagingRoot.Normalize(wxPATH_NORM_ABSOLUTE | wxPATH_NORM_DOTS);
	const wxString stagingRootPath = stagingRoot.GetPath() + wxFILE_SEP_PATH;
	std::unique_ptr<wxZipEntry> entry;
	while ((entry.reset(zip.GetNextEntry())), entry.get() != nullptr) {
		const wxString name = entry->GetName();
		// SEC-P1-7: refuse symlink entries — a zip carrying a symlink
		// could point outside stagingDir and a later extract would write
		// through it. wxZipEntry exposes file mode bits when the archive
		// was made on a Unix system; treat anything that looks like a
		// symlink as hostile.
		if (entry->IsMadeByUnix()) {
			const int mode = static_cast<int>(entry->GetMode());
			if ((mode & 0xF000) == 0xA000) { // S_IFLNK
				if (errorMsg) *errorMsg = wxT("zip entry is a symlink — refused");
				RmRf(stagingDir);
				return InstallResult::BadZip;
			}
		}
		// SEC-P1-7: canonicalise the computed output path and verify it
		// stays under stagingRoot. Substring "..". heuristic missed cases
		// like backslash separators on POSIX, "....//", or UTF-8 normalised
		// dots; comparing real paths catches them all.
		wxFileName outName(stagingDir + wxFILE_SEP_PATH + name);
		outName.Normalize(wxPATH_NORM_ABSOLUTE | wxPATH_NORM_DOTS);
		const wxString outFull = outName.GetFullPath();
		if (!outFull.StartsWith(stagingRootPath)) {
			if (errorMsg) *errorMsg = wxT("zip entry escapes bundle root");
			RmRf(stagingDir);
			return InstallResult::BadZip;
		}
		const wxString outPath = outFull;
		wxString sub;
		if (!ExtractEntry(zip, *entry, outPath, &sub)) {
			if (errorMsg) *errorMsg = sub;
			RmRf(stagingDir);
			return InstallResult::IoError;
		}
	}

	// Sanity-check the binary actually landed where the manifest said.
	const wxString binPath = stagingDir + wxFILE_SEP_PATH +
	                            wxString::FromUTF8(m.binary);
	if (!wxFileExists(binPath)) {
		if (errorMsg) {
			*errorMsg = wxString::Format(
			    wxT("manifest.binary '%s' not found in archive"),
			    wxString::FromUTF8(m.binary));
		}
		RmRf(stagingDir);
		return InstallResult::BadZip;
	}

	// Atomic rename: blow away the prior version dir + swap staging in.
	if (wxDirExists(versionDir)) RmRf(versionDir);
	if (!wxRenameFile(stagingDir, versionDir, /*overwrite*/ true)) {
		if (errorMsg) *errorMsg = wxT("atomic rename to versioned dir failed");
		RmRf(stagingDir);
		return InstallResult::IoError;
	}

	// Update the "current" pointer. On Unix we use a relative symlink;
	// on Windows we copy the binary into <pluginDir>/<binary> so the
	// LoadAll DLL scanner finds it.
	const wxString currentBin = pluginDir + wxFILE_SEP_PATH +
	                               wxString::FromUTF8(m.binary);
#ifndef _WIN32
	const wxString relTarget = wxString::FromUTF8(m.version) +
	                              wxFILE_SEP_PATH + wxString::FromUTF8(m.binary);
	::unlink(std::string(currentBin.utf8_str()).c_str());
	::symlink(std::string(relTarget.utf8_str()).c_str(),
	          std::string(currentBin.utf8_str()).c_str());
#else
	wxCopyFile(versionDir + wxFILE_SEP_PATH + wxString::FromUTF8(m.binary),
	            currentBin, /*overwrite*/ true);
#endif

	wxLogMessage(wxT("[plugins] installed %s %s -> %s"),
	             wxString::FromUTF8(m.pluginId),
	             wxString::FromUTF8(m.version), versionDir);
	return InstallResult::OK;
}

int Uninstall(const std::string& pluginId)
{
	if (pluginId.empty()) return -1;
	const wxString pluginsDir = ibPluginManager::GetPluginsDir();
	const wxString pluginDir  = pluginsDir + wxFILE_SEP_PATH +
	                              wxString::FromUTF8(pluginId);
	int rc = 0;
	if (wxDirExists(pluginDir)) {
		if (!RmRf(pluginDir)) {
			wxLogWarning(wxT("[plugins] uninstall: failed to remove %s"), pluginDir);
			rc = -1;
		}
	}
	const wxString envPath = byokEnv::GetEnvFilePath(pluginId);
	if (wxFileExists(envPath)) {
		if (!wxRemoveFile(envPath)) {
			wxLogWarning(wxT("[plugins] uninstall: failed to remove %s"), envPath);
			rc = -1;
		}
	}
	wxLogMessage(wxT("[plugins] uninstalled %s"), wxString::FromUTF8(pluginId));
	return rc;
}

} // namespace pluginInstaller
