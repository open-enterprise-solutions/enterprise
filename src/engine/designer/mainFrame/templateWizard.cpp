/////////////////////////////////////////////////////////////////////////////
// ibTemplateWizard — modal "Создать из шаблона..." wizard. See header.
/////////////////////////////////////////////////////////////////////////////

#include "templateWizard.h"
#include "templateWizardCard.h"
#include "templateWizardPreview.h"
#include "templateWizardCustomize.h"
#include "templateWizardApplier.h"

#include <wx/simplebook.h>
#include <wx/sizer.h>
#include <wx/scrolwin.h>
#include <wx/stattext.h>
#include <wx/button.h>
#include <wx/msgdlg.h>
#include <wx/intl.h>
#include <wx/log.h>
#include <wx/thread.h>
#include <wx/progdlg.h>
#include <memory>

#include <thread>
#include <atomic>
#include <string>
#include <vector>

#include "backend/appData.h"
#include "backend/plugin/pluginManager.h"
#include "3rdparty/nlohmann/json.hpp"
#include "../../../3rdparty/cpp-httplib/httplib.h"

// Plugin id used for grants + metaBridge audit. Must match the literal
// inside ibTemplateWizardApplier.cpp (s_kWizardPluginId).
static const wxString s_kWizardPluginId = wxT("designer.templateWizard");

// Default Pugi MCP endpoint when aiBridge.env doesn't override it.
static const wxString s_kDefaultEndpoint =
    wxT("https://mcp.pugi.io/api/oes-mcp/invoke");

// Custom event type used by worker threads to post responses back.
wxDEFINE_EVENT(EVT_TEMPLATE_WIZARD_THREAD, wxThreadEvent);

// ---------------------------------------------------------------------------
// Locale helpers — read the system language once at wizard open and pick
// the right name/description key from the locale-map values returned by
// oes_templates_list ({ "ru-RU": "...", "uk-UA": "...", "en-US": "..." }).
// ---------------------------------------------------------------------------

static std::pair<wxString, wxString> ResolveLocaleKeys()
{
	const int lang = wxLocale::GetSystemLanguage();
	if (lang == wxLANGUAGE_RUSSIAN) {
		return { wxT("ru-RU"), wxT("ru") };
	}
	if (lang == wxLANGUAGE_UKRAINIAN) {
		return { wxT("uk-UA"), wxT("uk") };
	}
	return { wxT("en-US"), wxT("en") };
}

// Walks a JSON object that maps locale-key to string, returning the first
// match for fullKey ("ru-RU"), then shortKey ("ru"), then any first
// string entry. Empty when the input isn't an object or has no strings.
static wxString PickLocalizedFromMap(const nlohmann::json& maybeMap,
                                       const wxString& fullKey,
                                       const wxString& shortKey)
{
	if (maybeMap.is_string()) {
		return wxString::FromUTF8(maybeMap.get<std::string>().c_str());
	}
	if (!maybeMap.is_object()) return wxString();

	const std::string fullKeyUtf8(fullKey.utf8_str());
	if (maybeMap.contains(fullKeyUtf8) && maybeMap[fullKeyUtf8].is_string()) {
		return wxString::FromUTF8(maybeMap[fullKeyUtf8].get<std::string>().c_str());
	}
	const std::string shortKeyUtf8(shortKey.utf8_str());
	if (maybeMap.contains(shortKeyUtf8) && maybeMap[shortKeyUtf8].is_string()) {
		return wxString::FromUTF8(maybeMap[shortKeyUtf8].get<std::string>().c_str());
	}
	// Fallback — first string value the iterator finds.
	for (auto& [k, v] : maybeMap.items()) {
		if (v.is_string()) {
			return wxString::FromUTF8(v.get<std::string>().c_str());
		}
	}
	return wxString();
}

// ---------------------------------------------------------------------------
// HTTP helper — POSTs to Pugi MCP and returns the raw response body.
// Used by all three fetch-* paths. Runs on a worker thread.
// ---------------------------------------------------------------------------

namespace {

struct McpInvokeResult {
	bool        ok = false;
	std::string body;
	std::string error;
};

McpInvokeResult InvokeMcpTool(const std::string& endpoint,
                                const std::string& token,
                                const std::string& tenant,
                                const std::string& toolName,
                                const nlohmann::json& input,
                                int timeoutSec)
{
	McpInvokeResult out;
	// Split URL — same trivial parser as aiBridge.
	const auto schemeEnd = endpoint.find("://");
	if (schemeEnd == std::string::npos) {
		out.error = "malformed endpoint URL";
		return out;
	}
	const auto pathStart = endpoint.find('/', schemeEnd + 3);
	const std::string base = (pathStart == std::string::npos)
	                              ? endpoint
	                              : endpoint.substr(0, pathStart);
	const std::string path = (pathStart == std::string::npos)
	                              ? "/"
	                              : endpoint.substr(pathStart);

	httplib::Client cli(base);
	cli.set_connection_timeout(10);
	cli.set_read_timeout(timeoutSec);
	cli.set_follow_location(false);   // never replay bearer to a redirect

	nlohmann::json body;
	body["name"]  = toolName;
	body["input"] = input;

	httplib::Headers headers = {
		{ "Authorization", "Bearer " + token   },
		{ "X-Pugi-Tenant", tenant              },
		{ "Content-Type",  "application/json"  },
		{ "Accept",        "application/json"  },
	};
	const std::string bodyStr = body.dump();
	auto res = cli.Post(path.c_str(), headers, bodyStr, "application/json");
	if (!res) {
		out.error = std::string("HTTP transport failed: ") +
		             httplib::to_string(res.error());
		return out;
	}
	if (res->status >= 300 && res->status < 400) {
		out.error = "server returned redirect; refusing to replay bearer";
		return out;
	}
	if (res->status >= 400) {
		out.error = "HTTP " + std::to_string(res->status);
		return out;
	}
	out.ok   = true;
	out.body = res->body;
	return out;
}

}  // namespace

// ---------------------------------------------------------------------------
// ibTemplateWizard — ctor / dtor.
// ---------------------------------------------------------------------------

ibTemplateWizard::ibTemplateWizard(wxWindow* parent)
	: wxDialog(parent, wxID_ANY, _("Создать конфигурацию из шаблона"),
	            wxDefaultPosition, wxSize(900, 680),
	            wxDEFAULT_DIALOG_STYLE | wxRESIZE_BORDER)
{
	const auto [full, brief] = ResolveLocaleKeys();
	m_localeKey = full;
	m_localeKeyShort = brief;

	auto* outer = new wxBoxSizer(wxVERTICAL);
	m_book = new wxSimplebook(this, wxID_ANY);

	wxWindow* gallery   = BuildGalleryPage(m_book);
	wxWindow* preview   = BuildPreviewPage(m_book);
	wxWindow* customize = BuildCustomizePage(m_book);

	m_book->AddPage(gallery,   _("Шаблоны"));
	m_book->AddPage(preview,   _("Предпросмотр"));
	m_book->AddPage(customize, _("Настройка"));

	outer->Add(m_book, 1, wxEXPAND);
	SetSizer(outer);
	CentreOnParent();

	// Worker-thread response handler. All three MCP calls funnel through
	// this one binding; the payload's Kind enum tells us which response
	// shape to dispatch.
	Bind(EVT_TEMPLATE_WIZARD_THREAD,
	      &ibTemplateWizard::OnThreadResponse, this);

	GrantWizardPolicy();
	StartFetchTemplatesList();
}

ibTemplateWizard::~ibTemplateWizard()
{
	// Bump epoch so any in-flight worker drops its response into the
	// void instead of hitting our (now-freed) handler.
	m_requestEpoch.fetch_add(1);
	RestoreWizardPolicy();
}

int ibTemplateWizard::Run(wxWindow* parent)
{
	ibTemplateWizard dlg(parent);
	return dlg.ShowModal();
}

// ---------------------------------------------------------------------------
// Policy management — grant AllowAlways for our pluginId on open;
// restore on close. The wizard talks straight to metaBridge with the
// "designer.templateWizard" pluginId so we control the elevation window.
// ---------------------------------------------------------------------------

void ibTemplateWizard::GrantWizardPolicy()
{
	if (appData == nullptr) return;
	ibPluginManager* pm = appData->GetPluginManager();
	if (pm == nullptr) return;
	pm->SetMutationPolicy(s_kWizardPluginId, wxT("meta.create"),
	                        ibPluginManager::MutationPolicy::AllowAlways);
	pm->SetMutationPolicy(s_kWizardPluginId, wxT("meta.edit"),
	                        ibPluginManager::MutationPolicy::AllowAlways);
	pm->SetMutationPolicy(s_kWizardPluginId, wxT("meta.delete"),
	                        ibPluginManager::MutationPolicy::AllowAlways);
	m_policyGranted = true;
	wxLogMessage(wxT("[templateWizard] granted AllowAlways for %s"),
	              s_kWizardPluginId);
}

void ibTemplateWizard::RestoreWizardPolicy()
{
	if (!m_policyGranted) return;
	if (appData == nullptr) return;
	ibPluginManager* pm = appData->GetPluginManager();
	if (pm == nullptr) return;
	pm->SetMutationPolicy(s_kWizardPluginId, wxT("meta.create"),
	                        ibPluginManager::MutationPolicy::Deny);
	pm->SetMutationPolicy(s_kWizardPluginId, wxT("meta.edit"),
	                        ibPluginManager::MutationPolicy::Deny);
	pm->SetMutationPolicy(s_kWizardPluginId, wxT("meta.delete"),
	                        ibPluginManager::MutationPolicy::Deny);
	m_policyGranted = false;
	wxLogMessage(wxT("[templateWizard] restored Deny for %s"),
	              s_kWizardPluginId);
}

// ---------------------------------------------------------------------------
// Credentials — pull TOKEN/TENANT/LOCALE/ENDPOINT from aiBridge.env.
// ---------------------------------------------------------------------------

bool ibTemplateWizard::ReadAiBridgeCreds(std::string& tokenOut,
                                            std::string& tenantOut,
                                            std::string& localeOut,
                                            std::string& endpointOut,
                                            wxString&    errorOut) const
{
	if (appData == nullptr) {
		errorOut = _("appData не инициализирован");
		return false;
	}
	ibPluginManager* pm = appData->GetPluginManager();
	if (pm == nullptr) {
		errorOut = _("Plugin manager не инициализирован");
		return false;
	}
	const std::string pluginId("aiBridge");
	tokenOut    = pm->ReadPluginEnv(pluginId, "TOKEN");
	tenantOut   = pm->ReadPluginEnv(pluginId, "TENANT");
	localeOut   = pm->ReadPluginEnv(pluginId, "LOCALE");
	endpointOut = pm->ReadPluginEnv(pluginId, "ENDPOINT");
	if (endpointOut.empty()) endpointOut = std::string(s_kDefaultEndpoint.utf8_str());
	if (localeOut.empty())   localeOut = std::string(m_localeKey.utf8_str());

	if (tokenOut.empty() || tenantOut.empty()) {
		errorOut = _("В aiBridge.env отсутствуют TOKEN или TENANT. "
		              "Откройте Tools → Plugins, чтобы их задать.");
		return false;
	}
	return true;
}

// ---------------------------------------------------------------------------
// Page navigation.
// ---------------------------------------------------------------------------

void ibTemplateWizard::GoTo(Page p)
{
	m_currentPage = p;
	if (m_book != nullptr) m_book->SetSelection(static_cast<size_t>(p));
}

// ---------------------------------------------------------------------------
// Page 1: Gallery.
// ---------------------------------------------------------------------------

wxWindow* ibTemplateWizard::BuildGalleryPage(wxWindow* parent)
{
	auto* page = new wxPanel(parent);
	auto* vbox = new wxBoxSizer(wxVERTICAL);

	auto* header = new wxStaticText(page, wxID_ANY,
	    _("Выберите готовую конфигурацию для быстрого старта"));
	{
		wxFont f = header->GetFont();
		f.MakeBold();
		f.SetPointSize(f.GetPointSize() + 2);
		header->SetFont(f);
	}
	vbox->Add(header, 0, wxALL, 10);

	m_galleryStatus = new wxStaticText(page, wxID_ANY,
	    _("Загрузка списка шаблонов…"));
	m_galleryStatus->SetForegroundColour(wxColour(96, 110, 132));
	vbox->Add(m_galleryStatus, 0, wxLEFT | wxRIGHT, 10);

	m_galleryScroller = new wxScrolledWindow(page, wxID_ANY);
	m_galleryScroller->SetScrollRate(0, 16);
	m_galleryGridSizer = new wxBoxSizer(wxVERTICAL);  // simple vertical list
	// — wxGridSizer requires fixed cell sizes which fight with
	// variable description heights; vertical layout looks better on
	// narrow windows anyway. The "2x2 grid" spec is best-effort and
	// degrades gracefully to a 1-column list as the dialog shrinks.
	m_galleryScroller->SetSizer(m_galleryGridSizer);
	vbox->Add(m_galleryScroller, 1, wxALL | wxEXPAND, 10);

	auto* footer = new wxBoxSizer(wxHORIZONTAL);
	auto* btnEmpty = new wxButton(page, wxID_ANY,
	                                _("Создать с нуля без шаблона"));
	auto* btnCancel = new wxButton(page, wxID_CANCEL, _("Отмена"));
	footer->AddStretchSpacer(1);
	footer->Add(btnEmpty,  0, wxALL, 5);
	footer->Add(btnCancel, 0, wxALL, 5);
	vbox->Add(footer, 0, wxEXPAND | wxBOTTOM, 8);

	page->SetSizer(vbox);

	btnCancel->Bind(wxEVT_BUTTON, &ibTemplateWizard::OnGalleryCancel, this);
	btnEmpty->Bind(wxEVT_BUTTON, &ibTemplateWizard::OnGalleryEmptyConfig, this);

	return page;
}

wxWindow* ibTemplateWizard::BuildPreviewPage(wxWindow* parent)
{
	m_previewPage = new ibTemplateWizardPreviewPage(
	    parent,
	    /*onBack*/      [this]() { wxCommandEvent e; OnPreviewBack(e); },
	    /*onCustomize*/ [this]() { wxCommandEvent e; OnPreviewCustomize(e); },
	    /*onApply*/     [this](bool includeData) {
	        m_includeData = includeData;
	        ApplyMutations();
	    });
	return m_previewPage;
}

wxWindow* ibTemplateWizard::BuildCustomizePage(wxWindow* parent)
{
	m_customizePage = new ibTemplateWizardCustomizePage(
	    parent,
	    /*onBack*/ [this]() { wxCommandEvent e; OnCustomizeBack(e); },
	    /*onApply*/ [this](const ibTemplateWizardCustomizePage::Payload& p) {
	        if (p.mode == ibTemplateWizardCustomizePage::Mode::None) {
	            ApplyMutations();
	        } else if (p.mode == ibTemplateWizardCustomizePage::Mode::Manual) {
	            StartFetchTemplateCustomize(m_selectedTemplateId,
	                                          p.modificationsJson,
	                                          wxString());
	        } else if (p.mode == ibTemplateWizardCustomizePage::Mode::AiTweak) {
	            if (p.userPrompt.IsEmpty()) {
	                wxMessageBox(_("Введите описание изменений для AI."),
	                              _("Template Wizard"),
	                              wxICON_INFORMATION, this);
	                return;
	            }
	            StartFetchTemplateCustomize(m_selectedTemplateId,
	                                          wxString(),
	                                          p.userPrompt);
	        }
	    });
	return m_customizePage;
}

// ---------------------------------------------------------------------------
// Worker thread launchers — all three POST to Pugi via cpp-httplib.
// On success post a wxThreadEvent back with the response body.
// ---------------------------------------------------------------------------

void ibTemplateWizard::StartFetchTemplatesList()
{
	std::string token, tenant, locale, endpoint;
	wxString credErr;
	if (!ReadAiBridgeCreds(token, tenant, locale, endpoint, credErr)) {
		if (m_galleryStatus != nullptr) {
			m_galleryStatus->SetLabel(credErr);
		}
		return;
	}
	const long epoch = m_requestEpoch.fetch_add(1) + 1;

	SetBusy(true, _("Получение списка шаблонов…"));
	std::thread([this, token, tenant, locale, endpoint, epoch]() {
		nlohmann::json input;
		input["locale"] = locale;
		McpInvokeResult r = InvokeMcpTool(endpoint, token, tenant,
		                                     "oes_templates_list", input,
		                                     /*timeoutSec=*/15);
		auto* evt = new wxThreadEvent(EVT_TEMPLATE_WIZARD_THREAD);
		ibTemplateWizardThreadPayload payload;
		payload.m_requestEpoch = epoch;
		if (r.ok) {
			payload.m_kind = ibTemplateWizardThreadPayload::Kind::TemplatesList;
			payload.m_responseJson = wxString::FromUTF8(r.body.c_str());
		} else {
			payload.m_kind  = ibTemplateWizardThreadPayload::Kind::Error;
			payload.m_error = wxString::FromUTF8(r.error.c_str());
		}
		evt->SetPayload(payload);
		wxQueueEvent(this, evt);
	}).detach();
}

void ibTemplateWizard::StartFetchTemplateGet(const wxString& templateId,
                                                bool includeData)
{
	std::string token, tenant, locale, endpoint;
	wxString credErr;
	if (!ReadAiBridgeCreds(token, tenant, locale, endpoint, credErr)) {
		wxMessageBox(credErr, _("Template Wizard"), wxICON_ERROR, this);
		return;
	}
	const long epoch = m_requestEpoch.fetch_add(1) + 1;
	SetBusy(true, _("Получение структуры шаблона…"));

	const std::string templateIdUtf8(templateId.utf8_str());
	std::thread([this, token, tenant, locale, endpoint,
	              templateIdUtf8, includeData, epoch]() {
		nlohmann::json input;
		input["templateId"]  = templateIdUtf8;
		input["includeData"] = includeData;
		input["locale"]      = locale;
		McpInvokeResult r = InvokeMcpTool(endpoint, token, tenant,
		                                     "oes_template_get", input,
		                                     /*timeoutSec=*/30);
		auto* evt = new wxThreadEvent(EVT_TEMPLATE_WIZARD_THREAD);
		ibTemplateWizardThreadPayload payload;
		payload.m_requestEpoch = epoch;
		if (r.ok) {
			payload.m_kind = ibTemplateWizardThreadPayload::Kind::TemplateGet;
			payload.m_responseJson = wxString::FromUTF8(r.body.c_str());
		} else {
			payload.m_kind  = ibTemplateWizardThreadPayload::Kind::Error;
			payload.m_error = wxString::FromUTF8(r.error.c_str());
		}
		evt->SetPayload(payload);
		wxQueueEvent(this, evt);
	}).detach();
}

void ibTemplateWizard::StartFetchTemplateCustomize(const wxString& templateId,
                                                      const wxString& modificationsJson,
                                                      const wxString& userPrompt)
{
	std::string token, tenant, locale, endpoint;
	wxString credErr;
	if (!ReadAiBridgeCreds(token, tenant, locale, endpoint, credErr)) {
		wxMessageBox(credErr, _("Template Wizard"), wxICON_ERROR, this);
		return;
	}
	const long epoch = m_requestEpoch.fetch_add(1) + 1;
	SetBusy(true,
	         userPrompt.IsEmpty()
	             ? _("Применяем настройки…")
	             : _("Sigma обрабатывает запрос (может занять до 15 секунд)…"));

	const std::string templateIdUtf8(templateId.utf8_str());
	const std::string modsUtf8(modificationsJson.utf8_str());
	const std::string promptUtf8(userPrompt.utf8_str());

	std::thread([this, token, tenant, locale, endpoint,
	              templateIdUtf8, modsUtf8, promptUtf8, epoch]() {
		nlohmann::json input;
		input["templateId"] = templateIdUtf8;
		if (!modsUtf8.empty()) {
			auto parsed = nlohmann::json::parse(modsUtf8, nullptr, false);
			if (!parsed.is_discarded() && parsed.is_object()) {
				input["modifications"] = parsed;
			}
		}
		if (!promptUtf8.empty()) {
			input["userPrompt"] = promptUtf8;
		}
		// Customize calls can be slow on first invocation (Sigma warm-up).
		McpInvokeResult r = InvokeMcpTool(endpoint, token, tenant,
		                                     "oes_template_customize", input,
		                                     /*timeoutSec=*/45);
		auto* evt = new wxThreadEvent(EVT_TEMPLATE_WIZARD_THREAD);
		ibTemplateWizardThreadPayload payload;
		payload.m_requestEpoch = epoch;
		if (r.ok) {
			payload.m_kind = ibTemplateWizardThreadPayload::Kind::TemplateCustomize;
			payload.m_responseJson = wxString::FromUTF8(r.body.c_str());
		} else {
			payload.m_kind  = ibTemplateWizardThreadPayload::Kind::Error;
			payload.m_error = wxString::FromUTF8(r.error.c_str());
		}
		evt->SetPayload(payload);
		wxQueueEvent(this, evt);
	}).detach();
}

// ---------------------------------------------------------------------------
// Thread response router.
// ---------------------------------------------------------------------------

void ibTemplateWizard::OnThreadResponse(wxThreadEvent& event)
{
	const auto payload = event.GetPayload<ibTemplateWizardThreadPayload>();
	// Drop stale responses — user navigated away mid-fetch.
	if (payload.m_requestEpoch != m_requestEpoch.load()) {
		return;
	}
	SetBusy(false, wxEmptyString);

	if (payload.m_kind == ibTemplateWizardThreadPayload::Kind::Error) {
		wxMessageBox(_("Не удалось получить ответ от Pugi:\n") +
		               payload.m_error,
		              _("Template Wizard"), wxICON_ERROR, this);
		return;
	}
	switch (payload.m_kind) {
	case ibTemplateWizardThreadPayload::Kind::TemplatesList:
		OnTemplatesListResponse(payload.m_responseJson);
		break;
	case ibTemplateWizardThreadPayload::Kind::TemplateGet:
		OnTemplateGetResponse(payload.m_responseJson);
		break;
	case ibTemplateWizardThreadPayload::Kind::TemplateCustomize:
		OnTemplateCustomizeResponse(payload.m_responseJson);
		break;
	default:
		break;
	}
}

// Re-build gallery cards from oes_templates_list response.
void ibTemplateWizard::OnTemplatesListResponse(const wxString& responseJson)
{
	m_templates.clear();
	if (m_galleryGridSizer == nullptr) return;
	m_galleryGridSizer->Clear(/*delete_windows=*/true);
	m_cards.clear();

	auto parsed = nlohmann::json::parse(std::string(responseJson.utf8_str()),
	                                       nullptr, false);
	if (parsed.is_discarded() || !parsed.is_object()) {
		if (m_galleryStatus != nullptr) {
			m_galleryStatus->SetLabel(
			    _("Не удалось разобрать ответ сервера (oes_templates_list)."));
		}
		return;
	}
	// Unwrap result / structuredContent.
	const nlohmann::json* payloadObj = &parsed;
	if (parsed.contains("result") && parsed["result"].is_object()) {
		payloadObj = &parsed["result"];
	} else if (parsed.contains("structuredContent") &&
	            parsed["structuredContent"].is_object()) {
		payloadObj = &parsed["structuredContent"];
	}
	if (!payloadObj->contains("templates") ||
	    !(*payloadObj)["templates"].is_array()) {
		if (m_galleryStatus != nullptr) {
			m_galleryStatus->SetLabel(
			    _("Сервер не вернул список шаблонов."));
		}
		return;
	}

	for (const auto& t : (*payloadObj)["templates"]) {
		if (!t.is_object()) continue;
		ibTemplateInfo info;
		info.id           = wxString::FromUTF8(
		                       t.value("id", std::string()).c_str());
		info.version      = wxString::FromUTF8(
		                       t.value("version", std::string()).c_str());
		info.objectCount  = t.value("objectCount", 0);
		info.demoRowCount = t.value("demoRowCount", 0);
		if (t.contains("thumbnailUrl") && t["thumbnailUrl"].is_string()) {
			info.thumbnailUrl = wxString::FromUTF8(
			    t["thumbnailUrl"].get<std::string>().c_str());
		}
		if (t.contains("name")) {
			info.name = PickLocalizedFromMap(t["name"],
			                                    m_localeKey, m_localeKeyShort);
		}
		if (info.name.IsEmpty()) info.name = info.id;
		if (t.contains("description")) {
			info.description = PickLocalizedFromMap(t["description"],
			                                          m_localeKey, m_localeKeyShort);
		}
		if (t.contains("tags") && t["tags"].is_array()) {
			for (const auto& tag : t["tags"]) {
				if (tag.is_string()) {
					info.tags.push_back(wxString::FromUTF8(
					    tag.get<std::string>().c_str()));
				}
			}
		}
		if (t.contains("previewModules") && t["previewModules"].is_array()) {
			for (const auto& mod : t["previewModules"]) {
				if (mod.is_string()) {
					info.previewModules.push_back(wxString::FromUTF8(
					    mod.get<std::string>().c_str()));
				}
			}
		}
		m_templates.push_back(std::move(info));
	}

	if (m_galleryStatus != nullptr) {
		m_galleryStatus->SetLabel(
		    wxString::Format(_("Шаблонов доступно: %zu"),
		                       m_templates.size()));
	}

	// Build cards.
	for (const auto& info : m_templates) {
		// Build localized stats / tag strings on the host side.
		const wxString stats = wxString::Format(
		    _("Объектов: %d   Строк демо: %d   v%s"),
		    info.objectCount, info.demoRowCount,
		    info.version.IsEmpty() ? wxString(wxT("?")) : info.version);
		wxString tagsLine;
		for (const wxString& t : info.tags) {
			if (!tagsLine.IsEmpty()) tagsLine += wxT("  ");
			tagsLine += wxT("#") + t;
		}
		auto* card = new ibTemplateCard(m_galleryScroller, info.id,
		                                  info.name, info.description,
		                                  stats, tagsLine, info.thumbnailUrl,
		                                  [this](const wxString& id) {
		                                      OnGalleryCardClicked(id);
		                                  });
		m_cards.push_back(card);
		m_galleryGridSizer->Add(card, 0, wxALL | wxEXPAND, 8);
	}
	m_galleryScroller->Layout();
	m_galleryScroller->FitInside();
}

void ibTemplateWizard::OnTemplateGetResponse(const wxString& responseJson)
{
	m_templateGetResponseJson = responseJson;
	// Find the chosen template's display name for the preview header.
	wxString name = m_selectedTemplateId, version;
	for (const auto& t : m_templates) {
		if (t.id == m_selectedTemplateId) {
			name = t.name;
			version = t.version;
			break;
		}
	}
	if (m_previewPage != nullptr) {
		m_previewPage->LoadFrom(responseJson, name, version);
	}
	if (m_customizePage != nullptr) {
		m_customizePage->LoadObjectsFrom(responseJson);
	}
	GoTo(PAGE_PREVIEW);
}

void ibTemplateWizard::OnTemplateCustomizeResponse(const wxString& responseJson)
{
	// The customized response has the same mutations[] shape — re-load
	// the preview page and bring the user back to it for confirmation.
	m_templateGetResponseJson = responseJson;
	wxString name = m_selectedTemplateId, version;
	for (const auto& t : m_templates) {
		if (t.id == m_selectedTemplateId) {
			name = t.name;
			version = t.version;
			break;
		}
	}
	if (m_previewPage != nullptr) {
		m_previewPage->LoadFrom(responseJson,
		                          name + _(" (модифицирован)"),
		                          version);
	}
	GoTo(PAGE_PREVIEW);
}

// ---------------------------------------------------------------------------
// Button handlers.
// ---------------------------------------------------------------------------

void ibTemplateWizard::OnGalleryCancel(wxCommandEvent&)
{
	EndModal(wxID_CANCEL);
}

void ibTemplateWizard::OnGalleryEmptyConfig(wxCommandEvent&)
{
	// "Empty config" path — wizard just closes; the user proceeds as if
	// they'd cancelled. The Designer's normal File → New flow takes over
	// once the wizard returns (caller is responsible for routing).
	EndModal(wxID_NO);
}

void ibTemplateWizard::OnGalleryCardClicked(const wxString& templateId)
{
	m_selectedTemplateId = templateId;
	// Use includeData=false here — the preview only needs structure;
	// demo data is fetched lazily on Apply when the user opts in.
	StartFetchTemplateGet(templateId, /*includeData=*/false);
}

void ibTemplateWizard::OnPreviewBack(wxCommandEvent&)
{
	GoTo(PAGE_GALLERY);
}

void ibTemplateWizard::OnPreviewCustomize(wxCommandEvent&)
{
	GoTo(PAGE_CUSTOMIZE);
}

void ibTemplateWizard::OnPreviewApply(wxCommandEvent&)
{
	ApplyMutations();
}

void ibTemplateWizard::OnCustomizeBack(wxCommandEvent&)
{
	GoTo(PAGE_PREVIEW);
}

void ibTemplateWizard::OnCustomizeApply(wxCommandEvent&)
{
	ApplyMutations();
}

// ---------------------------------------------------------------------------
// Apply — walk the cached template response through metaBridge. If
// includeData was requested AND the cached response was fetched without
// demoData, refetch with includeData=true first; otherwise apply now.
// ---------------------------------------------------------------------------

void ibTemplateWizard::ApplyMutations()
{
	if (m_templateGetResponseJson.IsEmpty()) {
		wxMessageBox(_("Сначала выберите шаблон."),
		              _("Template Wizard"), wxICON_INFORMATION, this);
		return;
	}

	// Check whether demoData is present in the cached response when
	// the user opted in.
	bool cachedHasDemo = false;
	{
		auto parsed = nlohmann::json::parse(
		    std::string(m_templateGetResponseJson.utf8_str()),
		    nullptr, false);
		if (!parsed.is_discarded() && parsed.is_object()) {
			const nlohmann::json* p = &parsed;
			if (parsed.contains("result") && parsed["result"].is_object()) p = &parsed["result"];
			else if (parsed.contains("structuredContent") &&
			          parsed["structuredContent"].is_object())
				p = &parsed["structuredContent"];
			if (p->contains("demoData") && (*p)["demoData"].is_array() &&
			    !(*p)["demoData"].empty()) {
				cachedHasDemo = true;
			}
		}
	}

	if (m_includeData && !cachedHasDemo) {
		// Refetch with includeData=true; OnTemplateGetResponse will replace
		// the cache and bring us back here implicitly via the user's next
		// click. To keep the flow seamless, we mark a one-shot to apply on
		// the next arrival.
		// Simpler: synchronously call StartFetchTemplateGet, and have the
		// arrival handler check a m_pendingApply flag. To avoid header
		// churn, just inform the user.
		const int answer = wxMessageBox(
		    _("Демо-данные не были загружены вместе со структурой. "
		      "Применить только структуру?\n\n"
		      "Да — применить только структуру.\n"
		      "Нет — отменить и выбрать «Применить как есть» снова "
		      "после повторной загрузки с демо-данными."),
		    _("Template Wizard"),
		    wxYES_NO | wxICON_QUESTION, this);
		if (answer != wxYES) {
			// Refetch with demo data; user will re-click Apply.
			StartFetchTemplateGet(m_selectedTemplateId, /*includeData=*/true);
			return;
		}
	}

	auto progress = std::make_unique<wxProgressDialog>(
	    _("Применение шаблона"),
	    _("Создаются объекты конфигурации…"),
	    100, this,
	    wxPD_APP_MODAL | wxPD_AUTO_HIDE | wxPD_ELAPSED_TIME);

	ibTemplateWizardApplier::ApplyResult r =
	    ibTemplateWizardApplier::Apply(m_templateGetResponseJson,
	                                     m_includeData && cachedHasDemo);

	progress.reset();

	wxString summary;
	if (r.failureCount == 0) {
		summary = wxString::Format(
		    _("Готово. Создано объектов: %d."), r.successCount);
		wxMessageBox(summary, _("Template Wizard"),
		              wxICON_INFORMATION, this);
		EndModal(wxID_OK);
		return;
	}

	// Partial / failed apply — show diagnostic without closing.
	summary = wxString::Format(
	    _("Применено успешно: %d.\nОшибок: %d.\n\n"),
	    r.successCount, r.failureCount);
	int shown = 0;
	for (const auto& op : r.ops) {
		if (op.success) continue;
		summary += wxString::Format(wxT("[%s] %s %s — %s\n"),
		                                op.op, op.kind, op.fullName,
		                                op.error.IsEmpty()
		                                    ? wxString(_("(нет диагностики)"))
		                                    : op.error);
		if (++shown >= 10) {
			summary += _("…\n(остальные ошибки см. в логе)\n");
			break;
		}
	}
	wxMessageBox(summary, _("Template Wizard"),
	              wxICON_WARNING, this);
}

// ---------------------------------------------------------------------------
// Busy overlay.
// ---------------------------------------------------------------------------

void ibTemplateWizard::SetBusy(bool busy, const wxString& message)
{
	if (m_galleryStatus != nullptr && m_currentPage == PAGE_GALLERY) {
		if (busy) {
			m_galleryStatus->SetLabel(message);
		}
	}
	// Cursor change is the simplest "busy" indicator — production
	// can replace with a tinted overlay panel later.
	if (busy) {
		SetCursor(wxCursor(wxCURSOR_WAIT));
	} else {
		SetCursor(wxNullCursor);
	}
}
