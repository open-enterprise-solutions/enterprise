////////////////////////////////////////////////////////////////////////////
// AI-assisted property fill — see aiPropertyHelper.h for the design notes.
//
// Provider routing reuses ibPluginManager::CompleteCodeAsync (which already
// drives the inline code-completion / ghost-text path in the code editor),
// so this helper inherits whatever LLM was registered by the active AI
// plugin without any new wiring.
////////////////////////////////////////////////////////////////////////////

#include "aiPropertyHelper.h"

#include "backend/appData.h"
#include "backend/plugin/pluginManager.h"
#include "backend/propertyManager/property/propertyString.h"
#include "backend/metaCollection/metaObject.h"

#include "frontend/mainFrame/objinspect/objinspect.h"

#include <wx/log.h>
#include <wx/menu.h>
#include <wx/msgdlg.h>
#include <wx/propgrid/manager.h>
#include <wx/propgrid/propgrid.h>
#include <wx/window.h>

#include <atomic>

#include "3rdparty/nlohmann/json.hpp"

namespace {

// Single in-flight generation token. Debounces re-clicks (`ShowContextMenu`
// returns early when this is true) and ignores stale callbacks if the
// inspector switched objects before the LLM came back.
std::atomic<unsigned> g_aiGenerationCounter{ 0 };
std::atomic<bool>     g_aiBusy{ false };

// Menu id reserved for the "Сгенерировать через AI" entry. wxID_HIGHEST
// guarantees no collision with the inspector's own ids.
constexpr int kAIGenerateMenuId = wxID_HIGHEST + 9501;

// Map an `ibProperty::GetName()` to a stable JSON key the model output
// will be matched against. Both the request format and the answer field
// names are lowercase + English so the prompt template stays single-locale.
wxString FieldKeyForProperty(const wxString& propName)
{
	if (propName == wxT("Synonym")) return wxT("synonym");
	if (propName == wxT("Comment"))  return wxT("comment");
	if (propName == wxT("Tooltip"))  return wxT("tooltip");
	if (propName == wxT("Title"))    return wxT("title");
	if (propName == wxT("Caption"))  return wxT("title");   // alias of title
	return wxEmptyString;
}

// Strip surrounding quotes / whitespace / markdown fences. The shim path
// hands us model output verbatim, including occasional ``` wrappers.
wxString CleanModelString(const wxString& raw)
{
	wxString s = raw;
	s.Replace(wxT("```json"), wxString(), false);
	s.Replace(wxT("```"),     wxString(), false);
	s.Trim(/*fromRight=*/false);
	s.Trim(/*fromRight=*/true);
	// Drop a single layer of surrounding quotes, common when the model
	// returns a bare string instead of a JSON object.
	if (s.length() >= 2 &&
	    ((s.StartsWith(wxT("\"")) && s.EndsWith(wxT("\""))) ||
	     (s.StartsWith(wxT("'"))  && s.EndsWith(wxT("'"))))) {
		s = s.Mid(1, s.length() - 2);
	}
	return s;
}

// Build the chat prompt. The model is asked for a JSON object so we can
// fill peer fields too — clicking "Synonym" pre-populates Comment +
// Tooltip when the model returns them.
wxString BuildPrompt(ibPropertyObject* owner,
                      ibProperty* property,
                      const wxString& fieldKey)
{
	const wxString objectKind = owner != nullptr ? owner->GetClassName()
	                                              : wxString(wxT("metadata object"));

	// Pull the object's current Name / Synonym for context. Both are
	// optional — when missing the model still gets a useful kind +
	// field hint and improvises.
	wxString objectName, currentSynonym, currentComment;
	if (owner != nullptr) {
		if (ibProperty* nameP = owner->GetProperty(wxT("Name"))) {
			if (auto* sp = dynamic_cast<ibPropertyStringBase*>(nameP))
				objectName = sp->GetValueAsString();
		}
		if (ibProperty* synP = owner->GetProperty(wxT("Synonym"))) {
			if (auto* sp = dynamic_cast<ibPropertyStringBase*>(synP))
				currentSynonym = sp->GetValueAsString();
		}
		if (ibProperty* comP = owner->GetProperty(wxT("Comment"))) {
			if (auto* sp = dynamic_cast<ibPropertyStringBase*>(comP))
				currentComment = sp->GetValueAsString();
		}
	}

	// Sibling attribute summary — captures up to 12 same-level
	// attributes so the prompt has a feel for the object's surface
	// without exceeding the typical short-context request budget.
	wxString attributesSummary;
	if (auto* metaOwner = dynamic_cast<ibValueMetaObject*>(owner)) {
		auto attributes = metaOwner->GetAnyArrayObject<ibValueMetaObject>();
		int captured = 0;
		for (auto* child : attributes) {
			if (!child || !child->IsAllowed()) continue;
			if (captured >= 12) { attributesSummary += wxT(", ..."); break; }
			if (captured > 0) attributesSummary += wxT(", ");
			attributesSummary += child->GetName();
			++captured;
		}
	}

	wxString prompt;
	prompt += wxT("Ты ассистент low-code платформы Open Enterprise Solutions ");
	prompt += wxT("(аналог 1С:Предприятие). Сгенерируй короткие тексты для ");
	prompt += wxT("полей метаданных. Объект имеет тип ");
	prompt += objectKind;
	prompt += wxT(", идентификатор '");
	prompt += objectName.IsEmpty() ? wxString(wxT("(без имени)")) : objectName;
	prompt += wxT("'.\n");
	if (!currentSynonym.IsEmpty()) {
		prompt += wxT("Текущий синоним: '");
		prompt += currentSynonym;
		prompt += wxT("'.\n");
	}
	if (!currentComment.IsEmpty()) {
		prompt += wxT("Текущий комментарий: '");
		prompt += currentComment;
		prompt += wxT("'.\n");
	}
	if (!attributesSummary.IsEmpty()) {
		prompt += wxT("Состав: ");
		prompt += attributesSummary;
		prompt += wxT(".\n");
	}
	prompt += wxT("Пользователь запросил поле '");
	prompt += fieldKey;
	prompt += wxT("'.\n\n");
	prompt += wxT("Верни строго JSON одним объектом без markdown-обрамления, ");
	prompt += wxT("по схеме:\n");
	prompt += wxT("{\n");
	prompt += wxT("  \"synonym\": \"<краткая человекочитаемая надпись, до 60 символов>\",\n");
	prompt += wxT("  \"comment\": \"<пояснение назначения объекта, 1-2 предложения>\",\n");
	prompt += wxT("  \"tooltip\": \"<подсказка для конечного пользователя в форме, до 120 символов>\",\n");
	prompt += wxT("  \"title\": \"<заголовок элемента формы, до 40 символов>\"\n");
	prompt += wxT("}\n");
	prompt += wxT("Все значения на русском языке. Пустые поля допустимы, но запрошенное поле обязательно заполни.");

	return prompt;
}

// Parse the model response as JSON. Returns an empty map on failure —
// callers fall back to plain text in that case.
std::map<wxString, wxString> ParseJsonResponse(const wxString& text)
{
	std::map<wxString, wxString> out;
	const std::string utf8(text.utf8_str().data());
	auto parsed = nlohmann::json::parse(utf8, nullptr, /*allow_exceptions=*/false);
	if (parsed.is_discarded() || !parsed.is_object()) return out;
	for (auto& [k, v] : parsed.items()) {
		if (v.is_string()) {
			out[wxString::FromUTF8(k.c_str())] =
			    wxString::FromUTF8(v.get<std::string>().c_str());
		}
	}
	return out;
}

// Apply a single value to (1) the ibProperty model, (2) the visible
// wxPGProperty, and (3) the ibPropertyObject's OnPropertyChanged hook so
// metadata mutation flags fire (Designer's "modified" indicator).
void ApplyValueToProperty(wxPropertyGridManager* pgManager,
                           ibPropertyObject* owner,
                           ibProperty* property,
                           const wxString& newValue)
{
	if (property == nullptr) return;
	const wxVariant oldValue = property->GetValue();
	const wxVariant newVariant = wxVariant(newValue);

	if (owner != nullptr && !owner->OnPropertyChanging(property, newVariant)) {
		return; // owner vetoed (e.g. invalid name); leave property untouched
	}
	property->SetValue(newVariant);
	if (owner != nullptr) {
		owner->OnPropertyChanged(property, oldValue, newVariant);
	}

	if (pgManager != nullptr) {
		// Walk every page until we find the property — multi-page
		// inspector style otherwise hides matches behind GetGrid().
		const size_t pageCount = pgManager->GetPageCount();
		for (size_t i = 0; i < pageCount; ++i) {
			if (wxPGProperty* pg = pgManager->GetPage(i)->GetPropertyByName(property->GetName())) {
				pg->SetValue(newVariant);
				pgManager->RefreshProperty(pg);
				break;
			}
		}
	}
}

} // namespace

bool ibAIPropertyHelper::IsAIEligible(const wxString& propertyName)
{
	return propertyName == wxT("Synonym")
	    || propertyName == wxT("Comment")
	    || propertyName == wxT("Tooltip")
	    || propertyName == wxT("Title")
	    || propertyName == wxT("Caption");
}

bool ibAIPropertyHelper::IsBusy()
{
	return g_aiBusy.load();
}

void ibAIPropertyHelper::ShowContextMenu(wxWindow* parent,
                                          wxPropertyGridManager* pgManager,
                                          wxPGProperty* pgProperty,
                                          ibProperty* property,
                                          ibPropertyObject* owner)
{
	if (parent == nullptr || property == nullptr) return;
	if (!IsAIEligible(property->GetName())) return;

	wxMenu menu;
	wxMenuItem* item = menu.Append(kAIGenerateMenuId,
	                                _("Сгенерировать через AI"),
	                                _("Запросить языковую модель сгенерировать значение поля"));
	if (item == nullptr) return;

	const bool busy = IsBusy();
	if (busy) {
		item->Enable(false);
		item->SetItemLabel(wxString::Format(wxT("%s  %s"),
		                                     _("Сгенерировать через AI"),
		                                     wxT("…")));
	}

	// Connect via the menu itself so the handler is torn down with the
	// transient wxMenu when PopupMenu returns — no leftover binding on
	// the parent window across right-clicks.
	menu.Bind(wxEVT_MENU,
	    [parent, pgManager, pgProperty, property, owner]
	    (wxCommandEvent&) {
		RunGenerate(parent, pgManager, pgProperty, property, owner);
	}, kAIGenerateMenuId);

	parent->PopupMenu(&menu);
}

void ibAIPropertyHelper::RunGenerate(wxWindow* parent,
                                       wxPropertyGridManager* pgManager,
                                       wxPGProperty* /*pgProperty*/,
                                       ibProperty* property,
                                       ibPropertyObject* owner)
{
	if (property == nullptr || owner == nullptr) return;

	// Debounce — silent no-op if a request initiated by this helper is
	// already running. The spec calls this out explicitly: "if user
	// clicks AI button again while pending, do nothing".
	if (g_aiBusy.exchange(true)) {
		wxLogStatus(_("AI: запрос уже выполняется…"));
		return;
	}

	const wxString fieldKey = FieldKeyForProperty(property->GetName());
	if (fieldKey.IsEmpty()) { g_aiBusy.store(false); return; }

	auto* pm = appData ? appData->GetPluginManager() : nullptr;
	if (pm == nullptr) {
		g_aiBusy.store(false);
		wxMessageBox(_("AI-провайдер не настроен. Установите плагин AI в каталоге plugins/."),
		              _("Сгенерировать через AI"),
		              wxOK | wxICON_INFORMATION, parent);
		return;
	}

	const wxString prompt = BuildPrompt(owner, property, fieldKey);
	wxLogStatus(_("AI: запрос отправлен…"));

	// Snapshot the owner pointer so we can ignore stale callbacks. The
	// inspector replaces m_currentSel on every selection change, which
	// would otherwise let a delayed response overwrite a different
	// object's fields.
	ibPropertyObject* const ownerSnapshot   = owner;
	ibProperty*       const propertySnapshot = property;
	const wxString          propertyName    = property->GetName();
	const unsigned          gen             = ++g_aiGenerationCounter;

	pm->CompleteCodeAsync(prompt, wxT("ru-RU"),
	    [gen, ownerSnapshot, propertySnapshot, propertyName, pgManager]
	    (bool ok, const wxString& text, const wxString& err) {
		// Always clear the busy flag on the UI thread before any
		// further work — even a stale callback should free the
		// gate.
		const bool stillCurrent = (gen == g_aiGenerationCounter.load());
		g_aiBusy.store(false);

		if (!stillCurrent) {
			wxLogStatus(_("AI: ответ устарел"));
			return;
		}

		if (!ok) {
			wxLogStatus(err.IsEmpty() ? wxString(_("AI: ошибка")) : err);
			return;
		}

		const wxString cleaned = CleanModelString(text);
		if (cleaned.IsEmpty()) {
			wxLogStatus(_("AI: пустой ответ"));
			return;
		}

		// Verify the inspector still shows the same property object
		// we kicked off the request from. The simplest stability
		// check: the inspector's current selection must match.
		ibPropertyObject* current = nullptr;
		if (auto* inspector = ibObjectInspector::GetObjectInspector()) {
			current = inspector->GetSelectedObject();
		}
		if (current != ownerSnapshot) {
			wxLogStatus(_("AI: выбран другой объект"));
			return;
		}

		// Try JSON first — that's our preferred response shape and it
		// lets one click populate multiple peer fields.
		auto values = ParseJsonResponse(cleaned);
		if (values.empty()) {
			// Plain-text fallback: stuff the raw model output into
			// the originally-requested property only.
			ApplyValueToProperty(pgManager, ownerSnapshot,
			                       propertySnapshot, cleaned);
			wxLogStatus(_("AI: готово"));
			return;
		}

		// Apply each recognised peer. Properties that don't exist on
		// this owner (e.g. "Tooltip" on a Catalog root) are skipped.
		auto applyByName = [&](const wxString& propName,
		                        const wxString& key) {
			auto it = values.find(key);
			if (it == values.end() || it->second.IsEmpty()) return;
			if (ibProperty* p = ownerSnapshot->GetProperty(propName)) {
				ApplyValueToProperty(pgManager, ownerSnapshot, p, it->second);
			}
		};

		applyByName(wxT("Synonym"), wxT("synonym"));
		applyByName(wxT("Comment"), wxT("comment"));
		applyByName(wxT("Tooltip"), wxT("tooltip"));
		applyByName(wxT("Title"),   wxT("title"));
		applyByName(wxT("Caption"), wxT("title"));

		wxLogStatus(_("AI: готово"));
	});
}
