/////////////////////////////////////////////////////////////////////////////
// pluginsConfig implementation. See header for the schema.
/////////////////////////////////////////////////////////////////////////////

#include "pluginsConfig.h"

#include "3rdparty/nlohmann/json.hpp"

#include <wx/filename.h>
#include <wx/stdpaths.h>
#include <wx/wfstream.h>
#include <wx/txtstrm.h>
#include <wx/log.h>

#include <fstream>
#include <sstream>

namespace pluginsConfig {
namespace {

ibPluginManager::MutationPolicy ParsePolicy(const std::string& s)
{
	if (s == "Ask")          return ibPluginManager::MutationPolicy::Ask;
	if (s == "AllowSession") return ibPluginManager::MutationPolicy::AllowSession;
	if (s == "AllowAlways")  return ibPluginManager::MutationPolicy::AllowAlways;
	if (s == "Deny")         return ibPluginManager::MutationPolicy::Deny;
	return ibPluginManager::MutationPolicy::Ask;
}

std::string ReadFileToString(const wxString& path)
{
	std::ifstream f(std::string(path.utf8_str()), std::ios::binary);
	if (!f.is_open()) return std::string();
	std::ostringstream ss;
	ss << f.rdbuf();
	return ss.str();
}

} // namespace

wxString GetConfigPath()
{
	wxString dir = wxGetenv(wxT("OES_PLUGIN_CONFIG_DIR"));
	if (dir.IsEmpty()) {
		const auto& sp = wxStandardPaths::Get();
		dir = sp.GetUserConfigDir();
		// wxStandardPaths gives e.g. ~/Library/Preferences (macOS) or
		// %APPDATA% (Windows) or $XDG_CONFIG_HOME (Linux). Append
		// /OES/plugins so we don't collide with vendor data.
		dir += wxFILE_SEP_PATH + wxString(wxT("OES")) +
		        wxFILE_SEP_PATH + wxString(wxT("plugins"));
	}
	wxFileName fn(dir, wxT("plugins.json5"));
	return fn.GetFullPath();
}

Snapshot Load()
{
	Snapshot snap;
	const wxString path = GetConfigPath();
	const std::string raw = ReadFileToString(path);
	if (raw.empty()) return snap;

	auto j = nlohmann::json::parse(raw, nullptr, /*allow_exceptions*/ false,
	                                 /*ignore_comments*/ true);
	if (j.is_discarded() || !j.is_object()) {
		wxLogWarning(wxT("pluginsConfig: %s malformed, ignoring"), path);
		return snap;
	}

	if (auto pluginsIt = j.find("plugins");
	    pluginsIt != j.end() && pluginsIt->is_object()) {
		for (auto& [pluginId, body] : pluginsIt->items()) {
			if (!body.is_object()) continue;
			PluginEntry e;
			if (auto it = body.find("enabled"); it != body.end() && it->is_boolean()) {
				e.enabled = it->get<bool>();
			}
			if (auto it = body.find("endpoint"); it != body.end() && it->is_string()) {
				e.endpoint = wxString::FromUTF8(it->get<std::string>());
			}
			if (auto it = body.find("byokRef"); it != body.end() && it->is_string()) {
				e.byokRef = wxString::FromUTF8(it->get<std::string>());
			}
			snap.plugins.emplace(pluginId, std::move(e));
		}
	}

	if (auto polIt = j.find("policy"); polIt != j.end() && polIt->is_object()) {
		for (auto& [pluginId, body] : polIt->items()) {
			if (!body.is_object()) continue;
			PolicyEntry pe;
			for (auto& [opName, val] : body.items()) {
				if (!val.is_string()) continue;
				pe.ops.emplace(opName, ParsePolicy(val.get<std::string>()));
			}
			snap.policies.emplace(pluginId, std::move(pe));
		}
	}

	return snap;
}

void Apply(const Snapshot& snap, ibPluginManager& pm)
{
	for (const auto& [pluginId, entry] : snap.policies) {
		for (const auto& [opName, policy] : entry.ops) {
			pm.SetMutationPolicy(wxString::FromUTF8(pluginId),
			                       wxString::FromUTF8(opName),
			                       policy);
		}
	}
	// Note: enabled flags consumed by LoadAll's DLL scan via IsEnabled,
	// not via SetMutationPolicy. Keep both worlds explicit.
}

bool IsEnabled(const Snapshot& snap, const std::string& pluginId)
{
	auto it = snap.plugins.find(pluginId);
	if (it == snap.plugins.end()) return true; // absent = enabled
	return it->second.enabled;
}

} // namespace pluginsConfig
