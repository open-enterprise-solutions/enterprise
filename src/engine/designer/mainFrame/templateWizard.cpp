/////////////////////////////////////////////////////////////////////////////
// ibTemplateWizard — modal "Создать из шаблона..." wizard. See header.
/////////////////////////////////////////////////////////////////////////////

#include "templateWizard.h"
#include "templateWizardCard.h"
#include "templateWizardPreview.h"
#include "templateWizardCustomize.h"
#include "templateWizardApplier.h"
#include "templateWizardHttp.h"
#include "metaTree/treeConfiguration.h"

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
#include "frontend/mainFrame/mainFrame.h"
#include <memory>

#include <cctype>
#include <thread>
#include <atomic>
#include <string>
#include <vector>

#include "backend/appData.h"
#include "backend/plugin/pluginManager.h"
#include "3rdparty/nlohmann/json.hpp"

// Plugin id used for grants + metaBridge audit. Must match the literal
// inside ibTemplateWizardApplier.cpp (s_kWizardPluginId).
static const wxString s_kWizardPluginId = wxT("designer.templateWizard");

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

static wxString AddPreviewModulesFromListResponse(
    const wxString& detailResponseJson,
    const std::vector<wxString>& previewModules)
{
	if (previewModules.empty()) return detailResponseJson;

	auto parsed = nlohmann::json::parse(
	    std::string(detailResponseJson.utf8_str()),
	    nullptr, false);
	if (parsed.is_discarded() || !parsed.is_object()) {
		return detailResponseJson;
	}

	nlohmann::json* payload = &parsed;
	if (parsed.contains("result") && parsed["result"].is_object()) {
		payload = &parsed["result"];
	} else if (parsed.contains("structuredContent") &&
	           parsed["structuredContent"].is_object()) {
		payload = &parsed["structuredContent"];
	}

	if (payload->contains("previewModules") &&
	    (*payload)["previewModules"].is_array() &&
	    !(*payload)["previewModules"].empty()) {
		return detailResponseJson;
	}

	nlohmann::json modules = nlohmann::json::array();
	for (const wxString& module : previewModules) {
		if (!module.IsEmpty()) {
			modules.push_back(std::string(module.utf8_str()));
		}
	}
	if (modules.empty()) return detailResponseJson;

	(*payload)["previewModules"] = std::move(modules);
	return wxString::FromUTF8(parsed.dump().c_str());
}

// ---------------------------------------------------------------------------
// HTTP helper — POSTs to the configured template provider and returns
// the raw response body. Used by all three fetch-* paths. Runs on a
// worker thread.
// ---------------------------------------------------------------------------

namespace {

struct McpInvokeResult {
	bool        ok = false;
	std::string body;
	std::string error;
};

struct TemplateProviderConfig {
	std::string pluginId;
	std::string endpoint;
	std::string token;
	std::string tenant;
	std::string locale;
	std::string listTool      = "oes_templates_list";
	std::string getTool       = "oes_template_get";
	std::string customizeTool = "oes_template_customize";
};

std::string FirstEnvValue(const ibPluginManager::PluginEnvMap::mapped_type& env,
                          std::initializer_list<const char*> keys)
{
	for (const char* key : keys) {
		if (key == nullptr) continue;
		auto it = env.find(key);
		if (it != env.end() && !it->second.empty()) return it->second;
	}
	return {};
}

bool EnvFlagEnabled(const ibPluginManager::PluginEnvMap::mapped_type& env,
                    const char* key)
{
	auto it = env.find(key);
	if (it == env.end()) return false;
	std::string value = it->second;
	for (char& c : value) {
		c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
	}
	return value == "1" || value == "true" || value == "yes" ||
	       value == "on";
}

bool BuildProviderConfig(const std::string& pluginId,
                         const ibPluginManager::PluginEnvMap::mapped_type& env,
                         const std::string& localeFallback,
                         TemplateProviderConfig& out)
{
	if (!EnvFlagEnabled(env, "OES_TEMPLATE_PROVIDER")) return false;
	TemplateProviderConfig cfg;
	cfg.pluginId = pluginId;
	cfg.endpoint = FirstEnvValue(env, {
		"OES_TEMPLATE_ENDPOINT", "TEMPLATE_ENDPOINT", "ENDPOINT"
	});
	cfg.token = FirstEnvValue(env, {
		"OES_TEMPLATE_TOKEN", "TEMPLATE_TOKEN", "TOKEN"
	});
	cfg.tenant = FirstEnvValue(env, {
		"OES_TEMPLATE_TENANT", "TEMPLATE_TENANT", "TENANT"
	});
	cfg.locale = FirstEnvValue(env, {
		"OES_TEMPLATE_LOCALE", "TEMPLATE_LOCALE", "LOCALE"
	});
	cfg.listTool = FirstEnvValue(env, {
		"OES_TEMPLATE_LIST_TOOL", "TEMPLATE_LIST_TOOL"
	});
	cfg.getTool = FirstEnvValue(env, {
		"OES_TEMPLATE_GET_TOOL", "TEMPLATE_GET_TOOL"
	});
	cfg.customizeTool = FirstEnvValue(env, {
		"OES_TEMPLATE_CUSTOMIZE_TOOL", "TEMPLATE_CUSTOMIZE_TOOL"
	});
	if (cfg.locale.empty()) cfg.locale = localeFallback;
	if (cfg.listTool.empty()) cfg.listTool = "oes_templates_list";
	if (cfg.getTool.empty()) cfg.getTool = "oes_template_get";
	if (cfg.customizeTool.empty()) cfg.customizeTool = "oes_template_customize";
	cfg.endpoint = ibTemplateWizardHttp::NormalizeEndpoint(cfg.endpoint, {});
	if (cfg.endpoint.empty() || cfg.token.empty()) return false;
	out = std::move(cfg);
	return true;
}

McpInvokeResult InvokeMcpTool(const std::string& endpoint,
                                const std::string& token,
                                const std::string& tenant,
                                const std::string& toolName,
                                const nlohmann::json& input,
                                int timeoutSec)
{
	McpInvokeResult out;
	nlohmann::json body;
	body["name"]  = toolName;
	body["input"] = input;

	ibTemplateWizardHttp::Response res =
	    ibTemplateWizardHttp::PostJson(endpoint, token, tenant, body, timeoutSec);
	out.ok    = res.Ok();
	out.body  = res.body;
	out.error = res.error;
	if (!out.ok && out.error.empty()) {
		out.error = "HTTP " + std::to_string(res.status);
	}
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
// Template provider discovery.
// ---------------------------------------------------------------------------

bool ibTemplateWizard::ReadTemplateProvider(std::string& tokenOut,
                                              std::string& tenantOut,
                                              std::string& localeOut,
                                              std::string& endpointOut,
                                              std::string& listToolOut,
                                              std::string& getToolOut,
                                              std::string& customizeToolOut,
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
	const std::string localeFallback(m_localeKey.utf8_str());
	TemplateProviderConfig cfg;
	for (const auto& kv : pm->PluginEnv()) {
		if (BuildProviderConfig(kv.first, kv.second, localeFallback, cfg)) {
			tokenOut         = cfg.token;
			tenantOut        = cfg.tenant;
			localeOut        = cfg.locale;
			endpointOut      = cfg.endpoint;
			listToolOut      = cfg.listTool;
			getToolOut       = cfg.getTool;
			customizeToolOut = cfg.customizeTool;
			return true;
		}
	}

	errorOut = _("Не найден plugin-provider шаблонов. Установите расширение, "
	              "которое задаёт OES_TEMPLATE_PROVIDER=1, "
	              "OES_TEMPLATE_ENDPOINT и OES_TEMPLATE_TOKEN.");
	return false;
}

void ibTemplateWizard::ShowGalleryMessage(const wxString& title,
                                           const wxString& body,
                                           bool showPluginsButton)
{
	if (m_galleryGridSizer == nullptr || m_galleryScroller == nullptr) return;
	m_galleryGridSizer->Clear(/*delete_windows=*/true);
	m_cards.clear();
	if (m_galleryStatus != nullptr) {
		m_galleryStatus->SetLabel(wxEmptyString);
	}

	auto* card = new wxPanel(m_galleryScroller, wxID_ANY);
	card->SetBackgroundColour(wxColour(248, 250, 252));
	auto* outer = new wxBoxSizer(wxVERTICAL);
	outer->AddStretchSpacer(1);

	auto* inner = new wxPanel(card, wxID_ANY, wxDefaultPosition,
	                          wxSize(560, -1), wxBORDER_SIMPLE);
	inner->SetBackgroundColour(*wxWHITE);
	auto* box = new wxBoxSizer(wxVERTICAL);

	auto* titleText = new wxStaticText(inner, wxID_ANY, title);
	wxFont titleFont = titleText->GetFont();
	titleFont.MakeBold();
	titleFont.SetPointSize(titleFont.GetPointSize() + 2);
	titleText->SetFont(titleFont);
	box->Add(titleText, 0, wxLEFT | wxRIGHT | wxTOP, 18);

	auto* bodyText = new wxStaticText(inner, wxID_ANY, body,
	                                  wxDefaultPosition, wxDefaultSize,
	                                  wxST_NO_AUTORESIZE);
	bodyText->SetForegroundColour(wxColour(82, 94, 112));
	bodyText->Wrap(510);
	box->Add(bodyText, 0, wxLEFT | wxRIGHT | wxTOP, 18);

	if (showPluginsButton) {
		auto* row = new wxBoxSizer(wxHORIZONTAL);
		auto* pluginsBtn = new wxButton(inner, wxID_ANY, _("Открыть Plugins"));
		auto* retryBtn = new wxButton(inner, wxID_ANY, _("Повторить"));
		row->Add(pluginsBtn, 0, wxRIGHT, 8);
		row->Add(retryBtn, 0);
		box->Add(row, 0, wxLEFT | wxRIGHT | wxTOP | wxBOTTOM, 18);

		pluginsBtn->Bind(wxEVT_BUTTON, [this](wxCommandEvent&) {
			wxCommandEvent evt(wxEVT_MENU, wxID_FRONTEND_PLUGIN_MANAGER);
			evt.SetEventObject(this);
			for (wxWindow* p = GetParent(); p != nullptr; p = p->GetParent()) {
				if (p->ProcessWindowEvent(evt)) return;
			}
			if (wxTheApp != nullptr && wxTheApp->GetTopWindow() != nullptr) {
				wxTheApp->GetTopWindow()->ProcessWindowEvent(evt);
			}
		});
		retryBtn->Bind(wxEVT_BUTTON, [this](wxCommandEvent&) {
			StartFetchTemplatesList();
		});
	} else {
		box->AddSpacer(18);
	}

	inner->SetSizerAndFit(box);
	outer->Add(inner, 0, wxALIGN_CENTER | wxALL, 24);
	outer->AddStretchSpacer(1);
	card->SetSizer(outer);
	m_galleryGridSizer->Add(card, 1, wxEXPAND);
	m_galleryScroller->Layout();
	m_galleryScroller->FitInside();
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
// Worker thread launchers — all three POST to the selected provider.
// On success post a wxThreadEvent back with the response body.
// ---------------------------------------------------------------------------

void ibTemplateWizard::StartFetchTemplatesList()
{
	std::string token, tenant, locale, endpoint, listTool, getTool, customizeTool;
	wxString credErr;
	if (!ReadTemplateProvider(token, tenant, locale, endpoint,
	                          listTool, getTool, customizeTool, credErr)) {
		ShowGalleryMessage(
		    _("Шаблоны недоступны"),
		    _("Расширение для шаблонов не настроено. Если plugin уже установлен, "
		      "перезапустите Designer: OES автоматически добавит совместимые "
		      "template-provider ключи из существующего plugin env. Если после "
		      "рестарта список не появился, откройте Plugins и проверьте TOKEN, "
		      "ENDPOINT и состояние plugin."),
		    true);
		return;
	}
	const long epoch = m_requestEpoch.fetch_add(1) + 1;

	SetBusy(true, _("Получение списка шаблонов…"));
	std::thread([this, token, tenant, locale, endpoint, listTool, epoch]() {
		nlohmann::json input;
		input["locale"] = locale;
		McpInvokeResult r = InvokeMcpTool(endpoint, token, tenant,
		                                     listTool, input,
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
	std::string token, tenant, locale, endpoint, listTool, getTool, customizeTool;
	wxString credErr;
	if (!ReadTemplateProvider(token, tenant, locale, endpoint,
	                          listTool, getTool, customizeTool, credErr)) {
		wxMessageBox(credErr, _("Template Wizard"), wxICON_ERROR, this);
		return;
	}
	const long epoch = m_requestEpoch.fetch_add(1) + 1;
	SetBusy(true, _("Получение структуры шаблона…"));

	const std::string templateIdUtf8(templateId.utf8_str());
	std::thread([this, token, tenant, locale, endpoint, getTool,
	              templateIdUtf8, includeData, epoch]() {
		nlohmann::json input;
		input["templateId"]  = templateIdUtf8;
		input["includeData"] = includeData;
		input["locale"]      = locale;
		McpInvokeResult r = InvokeMcpTool(endpoint, token, tenant,
		                                     getTool, input,
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
	std::string token, tenant, locale, endpoint, listTool, getTool, customizeTool;
	wxString credErr;
	if (!ReadTemplateProvider(token, tenant, locale, endpoint,
	                          listTool, getTool, customizeTool, credErr)) {
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

	std::thread([this, token, tenant, locale, endpoint, customizeTool,
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
		                                     customizeTool, input,
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
		wxMessageBox(_("Не удалось получить ответ от provider:\n") +
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
	// Find the chosen template's display name for the preview header.
	wxString name = m_selectedTemplateId, version;
	std::vector<wxString> previewModules;
	for (const auto& t : m_templates) {
		if (t.id == m_selectedTemplateId) {
			name = t.name;
			version = t.version;
			previewModules = t.previewModules;
			break;
		}
	}
	m_templateGetResponseJson =
	    AddPreviewModulesFromListResponse(responseJson, previewModules);
	if (m_previewPage != nullptr) {
		m_previewPage->LoadFrom(m_templateGetResponseJson, name, version);
	}
	if (m_customizePage != nullptr) {
		m_customizePage->LoadObjectsFrom(m_templateGetResponseJson);
	}
	GoTo(PAGE_PREVIEW);
}

void ibTemplateWizard::OnTemplateCustomizeResponse(const wxString& responseJson)
{
	// The customized response has the same mutations[] shape — re-load
	// the preview page and bring the user back to it for confirmation.
	wxString name = m_selectedTemplateId, version;
	std::vector<wxString> previewModules;
	for (const auto& t : m_templates) {
		if (t.id == m_selectedTemplateId) {
			name = t.name;
			version = t.version;
			previewModules = t.previewModules;
			break;
		}
	}
	m_templateGetResponseJson =
	    AddPreviewModulesFromListResponse(responseJson, previewModules);
	if (m_previewPage != nullptr) {
		m_previewPage->LoadFrom(m_templateGetResponseJson,
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
	StartFetchTemplateGet(templateId, /*includeData=*/true);
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
		if (activeMetaData != nullptr) {
			activeMetaData->Modify(true);
			if (auto* tree = dynamic_cast<ibMetadataTree*>(
			        activeMetaData->GetMetaTree())) {
				tree->Load(activeMetaData);
			}
		}
		summary = wxString::Format(
		    _("Готово. Создано объектов: %d."), r.successCount);
		if (r.skippedDataRows > 0) {
			summary += wxString::Format(
			    _("\n\nДемо-данные подготовлены (%d строк), но запись строк "
			      "пока отключена: нужен отдельный API данных, MetaCreate "
			      "создаёт только метаданные."),
			    r.skippedDataRows);
		}
		wxMessageBox(summary, _("Template Wizard"),
		              wxICON_INFORMATION, this);
		EndModal(wxID_OK);
		return;
	}

	// Partial / failed apply — show diagnostic without closing.
	summary = wxString::Format(
	    _("Применено успешно: %d.\nОшибок: %d.\n\n"),
	    r.successCount, r.failureCount);
	if (r.skippedDataRows > 0) {
		summary += wxString::Format(
		    _("Демо-данные не записаны: %d строк ожидают отдельный API данных.\n\n"),
		    r.skippedDataRows);
	}
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
