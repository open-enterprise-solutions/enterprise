/////////////////////////////////////////////////////////////////////////////
// ibChatContext implementation — see header for the contract.
//
// Two-track resolver:
//   1. Metadata tokens — walk activeMetaData and (optionally) call into
//      metaBridge::HostMetaQuery for the JSON shape. We render the
//      attributes/tabularSections list ourselves rather than dumping the
//      raw JSON; the LLM prompt is easier to follow when each token
//      produces a short uniform block.
//   2. Editor tokens — @selection / @file / @open. Walks the focused
//      window tree to find a visible wxStyledTextCtrl and pulls
//      GetSelectedText / GetText off it.
//
// Suggestion collection runs straight off activeMetaData's child
// vector. We deliberately do NOT call HostMetaQuery here because the
// popup needs only labels — a per-row JSON round-trip would multiply
// latency by 10-20x for no UX gain.
/////////////////////////////////////////////////////////////////////////////

#include "frontend/pluginWebPane/chatContext.h"

#include "backend/metadataConfiguration.h"
#include "backend/metaCollection/metaObject.h"
#include "backend/metaCollection/metaObjectMetadata.h"
#include "backend/compiler/value.h"
#include "backend/plugin/metaBridge.h"

#include "3rdparty/nlohmann/json.hpp"

#include <wx/stc/stc.h>
#include <wx/aui/auibook.h>
#include <wx/window.h>

#include <algorithm>
#include <cstdlib>
#include <functional>
#include <set>
#include <string>
#include <unordered_set>

namespace ibChatContext {
namespace {

// CLSID → Russian display label. Matches the labels Designer uses
// elsewhere in the metadata tree.
wxString KindLabelForCLSID(ibClassID clsid)
{
	if (clsid == g_metaConstantCLSID)                   return wxT("Константа");
	if (clsid == g_metaCatalogCLSID)                    return wxT("Справочник");
	if (clsid == g_metaDocumentCLSID)                   return wxT("Документ");
	if (clsid == g_metaEnumerationCLSID)                return wxT("Перечисление");
	if (clsid == g_metaDataProcessorCLSID)              return wxT("Обработка");
	if (clsid == g_metaReportCLSID)                     return wxT("Отчёт");
	if (clsid == g_metaInformationRegisterCLSID)        return wxT("РегистрСведений");
	if (clsid == g_metaAccumulationRegisterCLSID)       return wxT("РегистрНакопления");
	if (clsid == g_metaChartOfCharacteristicTypesCLSID) return wxT("ПланВидовХарактеристик");
	if (clsid == g_metaChartOfAccountsCLSID)            return wxT("ПланСчетов");
	if (clsid == g_metaAccountingRegisterCLSID)         return wxT("РегистрБухгалтерии");
	return wxEmptyString;
}

// English kind name used in @Catalog.Foo style tokens — matches the
// metaBridge::KindStringToCLSID vocabulary so HostMetaQuery accepts the
// strings without translation.
wxString KindNameForCLSID(ibClassID clsid)
{
	if (clsid == g_metaConstantCLSID)                   return wxT("Constant");
	if (clsid == g_metaCatalogCLSID)                    return wxT("Catalog");
	if (clsid == g_metaDocumentCLSID)                   return wxT("Document");
	if (clsid == g_metaEnumerationCLSID)                return wxT("Enumeration");
	if (clsid == g_metaDataProcessorCLSID)              return wxT("DataProcessor");
	if (clsid == g_metaReportCLSID)                     return wxT("Report");
	if (clsid == g_metaInformationRegisterCLSID)        return wxT("InformationRegister");
	if (clsid == g_metaAccumulationRegisterCLSID)       return wxT("AccumulationRegister");
	if (clsid == g_metaChartOfCharacteristicTypesCLSID) return wxT("ChartOfCharacteristicTypes");
	if (clsid == g_metaChartOfAccountsCLSID)            return wxT("ChartOfAccounts");
	if (clsid == g_metaAccountingRegisterCLSID)         return wxT("AccountingRegister");
	return wxEmptyString;
}

// Case-insensitive "starts-with" — used by CollectSuggestions.
bool StartsWithCI(const wxString& haystack, const wxString& needle)
{
	if (needle.IsEmpty()) return true;
	if (haystack.size() < needle.size()) return false;
	return haystack.Left(needle.size()).CmpNoCase(needle) == 0;
}

// Walk activeMetaData and append matching suggestions.
void CollectFromMetadata(const wxString& prefix,
                          size_t cap,
                          std::vector<Suggestion>& out)
{
	if (out.size() >= cap) return;
	ibMetaDataConfigurationBase* cfg = ibMetaDataConfigurationBase::Get();
	if (cfg == nullptr) return;
	ibValueMetaObjectConfiguration* root = cfg->GetCommonMetaObject();
	if (root == nullptr) return;

	// Configurable kind filter — every top-level business object is fair
	// game. metaObject.h templates accept either a filter list or empty
	// for "every child"; we use the empty form and inspect CLSID per
	// row.
	std::vector<ibValueMetaObject*> all = root->GetAnyArrayObject<>();
	for (ibValueMetaObject* obj : all) {
		if (out.size() >= cap) break;
		if (obj == nullptr) continue;
		const ibClassID clsid = obj->GetClassType();
		const wxString kindName = KindNameForCLSID(clsid);
		if (kindName.IsEmpty()) continue;       // not a business object
		const wxString name = obj->GetName();
		if (name.IsEmpty()) continue;
		const wxString full = kindName + wxT(".") + name;
		// Match against either the bare name or the qualified form so
		// the popup catches both "Cou" and "Catalog.Cou".
		if (!StartsWithCI(full, prefix) && !StartsWithCI(name, prefix))
			continue;
		Suggestion s;
		s.kindLabel = KindLabelForCLSID(clsid);
		s.fullName  = full;
		out.push_back(std::move(s));
	}
}

// Synthetic editor tokens. Order matches their typical usage frequency
// in 1С:Workmate (selection > file > open).
void CollectSpecials(const wxString& prefix,
                      size_t cap,
                      std::vector<Suggestion>& out)
{
	struct Spec { const wxChar* token; const wxChar* label; };
	static const Spec kSpecials[] = {
		{ wxT("selection"), wxT("Выделение в редакторе") },
		{ wxT("file"),      wxT("Текст файла")           },
		{ wxT("open"),      wxT("Открытые вкладки")      },
	};
	for (const auto& sp : kSpecials) {
		if (out.size() >= cap) break;
		wxString tok(sp.token);
		if (!StartsWithCI(tok, prefix)) continue;
		Suggestion s;
		s.kindLabel = sp.label;
		s.fullName  = tok;
		out.push_back(std::move(s));
	}
}

// Token character predicate. Letters, digits, '.', '_' — keeps the
// scanner Unicode-safe (only ASCII counts as token cont) but tolerant
// of dotted qualifiers like Catalog.Counterparties.
bool IsTokenChar(wxUniChar c)
{
	const auto v = static_cast<unsigned>(c.GetValue());
	return (v >= 'A' && v <= 'Z') ||
	       (v >= 'a' && v <= 'z') ||
	       (v >= '0' && v <= '9') ||
	       v == '_' || v == '.';
}

// Walk window tree for the first visible wxStyledTextCtrl.
wxStyledTextCtrl* FindFocusedEditor(wxWindow* root)
{
	if (auto* stc = dynamic_cast<wxStyledTextCtrl*>(wxWindow::FindFocus()))
		return stc;
	if (root == nullptr) return nullptr;
	std::function<wxStyledTextCtrl*(wxWindow*)> walk =
	    [&walk](wxWindow* w) -> wxStyledTextCtrl* {
		if (w == nullptr) return nullptr;
		if (auto* s = dynamic_cast<wxStyledTextCtrl*>(w))
			if (s->IsShown()) return s;
		for (wxWindow* child : w->GetChildren())
			if (auto* found = walk(child)) return found;
		return nullptr;
	};
	return walk(root);
}

// Walk window tree for a wxAuiNotebook; used by @open. There may be
// several (Designer hosts a few AUI books); we accumulate page titles
// from every visible book.
void CollectOpenTabs(wxWindow* root, std::vector<wxString>& out)
{
	if (root == nullptr) return;
	std::function<void(wxWindow*)> walk = [&walk, &out](wxWindow* w) {
		if (w == nullptr) return;
		if (auto* nb = dynamic_cast<wxAuiNotebook*>(w)) {
			for (size_t i = 0; i < nb->GetPageCount(); ++i) {
				const wxString title = nb->GetPageText(i);
				if (!title.IsEmpty()) out.push_back(title);
			}
		}
		for (wxWindow* child : w->GetChildren()) walk(child);
	};
	walk(root);
}

} // namespace

std::vector<Suggestion> CollectSuggestions(const wxString& prefix, size_t cap)
{
	std::vector<Suggestion> out;
	out.reserve(cap);
	// Specials first — they're cheap and predictable; users typing "@"
	// with no prefix see them at the top of the popup, matching
	// 1С:Workmate's UX.
	CollectSpecials(prefix, cap, out);
	CollectFromMetadata(prefix, cap, out);
	return out;
}

std::vector<wxString> ExtractTokens(const wxString& prompt)
{
	std::vector<wxString> out;
	std::unordered_set<std::string> seen;
	const size_t n = prompt.size();
	size_t i = 0;
	while (i < n) {
		if (prompt[i] != wxT('@')) { ++i; continue; }
		// Bare '@' at end-of-string or followed by whitespace → skip
		size_t j = i + 1;
		while (j < n && IsTokenChar(prompt[j])) ++j;
		if (j == i + 1) { ++i; continue; }
		const wxString tok = prompt.SubString(i + 1, j - 1);
		const std::string key(tok.utf8_str());
		if (seen.insert(key).second) out.push_back(tok);
		i = j;
	}
	return out;
}

wxString RenderMetadataBlock(const wxString& token)
{
	// metaBridge::HostMetaQuery expects "Kind.Name". If the token is the
	// bare object name we have to find its kind first by scanning the
	// active metadata tree.
	wxString fullName = token;
	if (fullName.Find(wxT('.')) == wxNOT_FOUND) {
		ibMetaDataConfigurationBase* cfg = ibMetaDataConfigurationBase::Get();
		if (cfg == nullptr) return wxEmptyString;
		ibValueMetaObjectConfiguration* root = cfg->GetCommonMetaObject();
		if (root == nullptr) return wxEmptyString;
		std::vector<ibValueMetaObject*> all = root->GetAnyArrayObject<>();
		for (ibValueMetaObject* obj : all) {
			if (obj == nullptr) continue;
			if (obj->GetName() == token) {
				const wxString kn = KindNameForCLSID(obj->GetClassType());
				if (!kn.IsEmpty()) {
					fullName = kn + wxT(".") + token;
					break;
				}
			}
		}
		if (fullName.Find(wxT('.')) == wxNOT_FOUND) return wxEmptyString;
	}

	char* json    = nullptr;
	char* errMsg  = nullptr;
	const int rc  = metaBridge::HostMetaQuery(
	    std::string(fullName.utf8_str()).c_str(),
	    /*fieldsFilter=*/nullptr,
	    &json,
	    &errMsg);
	if (rc != 0 || json == nullptr) {
		if (json   != nullptr) std::free(json);
		if (errMsg != nullptr) std::free(errMsg);
		return wxEmptyString;
	}

	auto doc = nlohmann::json::parse(json, nullptr, /*allow_exceptions=*/false);
	std::free(json);
	if (errMsg != nullptr) std::free(errMsg);
	if (doc.is_discarded() || !doc.is_object()) return wxEmptyString;

	const std::string kind = doc.value("kind", "");
	const std::string name = doc.value("name", "");
	const wxString    kindLabel = KindLabelForCLSID(
	    metaBridge::KindStringToCLSID(kind.c_str()));

	wxString out;
	out += wxString::Format(wxT("%s \"%s\":\n"),
	                         kindLabel.IsEmpty()
	                             ? wxString::FromUTF8(kind.c_str())
	                             : kindLabel,
	                         wxString::FromUTF8(name.c_str()));

	if (doc.contains("children") && doc["children"].is_array()) {
		// Group children by kind so the prompt block reads like a
		// schema definition rather than a flat dump.
		std::vector<std::string> attrs, tabs, forms, modules, other;
		for (const auto& c : doc["children"]) {
			if (!c.is_object()) continue;
			const std::string ckind = c.value("kind",  "");
			const std::string cname = c.value("name",  "");
			if      (ckind == "Attribute")       attrs.push_back(cname);
			else if (ckind == "TabularSection")  tabs.push_back(cname);
			else if (ckind == "Form")            forms.push_back(cname);
			else if (ckind == "Module")          modules.push_back(cname);
			else if (!ckind.empty() && !cname.empty())
				other.push_back(ckind + "." + cname);
		}
		auto dump = [&out](const wxString& label,
		                    const std::vector<std::string>& items) {
			if (items.empty()) return;
			out += wxT("  ") + label + wxT(": ");
			for (size_t i = 0; i < items.size(); ++i) {
				if (i > 0) out += wxT(", ");
				out += wxString::FromUTF8(items[i].c_str());
			}
			out += wxT("\n");
		};
		dump(wxT("Attributes"),      attrs);
		dump(wxT("TabularSections"), tabs);
		dump(wxT("Forms"),            forms);
		dump(wxT("Modules"),          modules);
		dump(wxT("Other"),            other);
	}
	return out;
}

wxString RenderEditorBlock(const wxString& specialToken, wxWindow* searchRoot)
{
	if (specialToken.CmpNoCase(wxT("selection")) == 0) {
		wxStyledTextCtrl* stc = FindFocusedEditor(searchRoot);
		if (stc == nullptr) return wxEmptyString;
		const wxString sel = stc->GetSelectedText();
		if (sel.IsEmpty()) return wxEmptyString;
		return wxT("Выделение в редакторе:\n```\n") + sel + wxT("\n```\n");
	}
	if (specialToken.CmpNoCase(wxT("file")) == 0) {
		wxStyledTextCtrl* stc = FindFocusedEditor(searchRoot);
		if (stc == nullptr) return wxEmptyString;
		const wxString text = stc->GetText();
		if (text.IsEmpty()) return wxEmptyString;
		return wxT("Текст активного файла:\n```\n") + text + wxT("\n```\n");
	}
	if (specialToken.CmpNoCase(wxT("open")) == 0) {
		std::vector<wxString> tabs;
		CollectOpenTabs(searchRoot, tabs);
		if (tabs.empty()) return wxEmptyString;
		wxString out = wxT("Открытые вкладки:\n");
		for (const auto& t : tabs) out += wxT("  - ") + t + wxT("\n");
		return out;
	}
	return wxEmptyString;
}

wxString BuildContextBlock(const wxString& prompt, wxWindow* searchRoot)
{
	const std::vector<wxString> tokens = ExtractTokens(prompt);
	if (tokens.empty()) return wxEmptyString;

	wxString body;
	for (const auto& tok : tokens) {
		wxString block;
		// Specials live in their own keyword set; everything else is
		// treated as metadata.
		if (tok.CmpNoCase(wxT("selection")) == 0 ||
		    tok.CmpNoCase(wxT("file"))      == 0 ||
		    tok.CmpNoCase(wxT("open"))      == 0) {
			block = RenderEditorBlock(tok, searchRoot);
		} else {
			block = RenderMetadataBlock(tok);
		}
		if (!block.IsEmpty()) {
			body += block;
			if (!body.EndsWith(wxT("\n"))) body += wxT("\n");
		}
	}
	if (body.IsEmpty()) return wxEmptyString;

	wxString out;
	out += wxT("=== Context ===\n");
	out += body;
	out += wxT("=== End context ===\n\n");
	return out;
}

} // namespace ibChatContext
