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

namespace {
const char* PolicyToName(ibPluginManager::MutationPolicy p)
{
	switch (p) {
	case ibPluginManager::MutationPolicy::Ask:          return "Ask";
	case ibPluginManager::MutationPolicy::AllowSession: return "AllowSession";
	case ibPluginManager::MutationPolicy::AllowAlways:  return "AllowAlways";
	case ibPluginManager::MutationPolicy::Deny:         return "Deny";
	}
	return "Ask";
}
} // namespace

int Save(const Snapshot& snap)
{
	nlohmann::json root = nlohmann::json::object();
	nlohmann::json plugins = nlohmann::json::object();
	for (const auto& [pluginId, entry] : snap.plugins) {
		nlohmann::json e = nlohmann::json::object();
		e["enabled"] = entry.enabled;
		if (!entry.endpoint.IsEmpty()) {
			e["endpoint"] = std::string(entry.endpoint.utf8_str());
		}
		if (!entry.byokRef.IsEmpty()) {
			e["byokRef"]  = std::string(entry.byokRef.utf8_str());
		}
		plugins[pluginId] = std::move(e);
	}
	if (!plugins.empty()) root["plugins"] = std::move(plugins);

	nlohmann::json policy = nlohmann::json::object();
	for (const auto& [pluginId, pe] : snap.policies) {
		nlohmann::json ops = nlohmann::json::object();
		for (const auto& [opName, p] : pe.ops) {
			ops[opName] = PolicyToName(p);
		}
		if (!ops.empty()) policy[pluginId] = std::move(ops);
	}
	if (!policy.empty()) root["policy"] = std::move(policy);

	const wxString final = GetConfigPath();
	wxFileName fn(final);
	if (!fn.Mkdir(0700, wxPATH_MKDIR_FULL)) {
		// Mkdir returns false when dir exists — not an error.
	}

	// Atomic swap via temp + rename. A crashed write leaves the prior
	// plugins.json5 intact rather than truncated.
	const wxString tmp = final + wxT(".tmp");
	{
		std::ofstream f(std::string(tmp.utf8_str()), std::ios::binary | std::ios::trunc);
		if (!f.is_open()) {
			wxLogWarning(wxT("pluginsConfig: failed to write %s"), tmp);
			return -1;
		}
		// Header comment makes the file's purpose clear when a user
		// stumbles into it via `find ~/.config/OES`.
		f << "// Generated by OES Designer.\n";
		f << "// Hand-edits survive — the Tools \\u2192 Plugins dialog\n";
		f << "// rewrites this file but preserves unknown keys.\n";
		f << root.dump(2) << "\n";
	}
	if (!wxRenameFile(tmp, final, /*overwrite*/ true)) {
		wxRemoveFile(tmp);
		wxLogWarning(wxT("pluginsConfig: atomic rename to %s failed"), final);
		return -1;
	}
	return 0;
}

} // namespace pluginsConfig
