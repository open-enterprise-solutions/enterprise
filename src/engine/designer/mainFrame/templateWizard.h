/////////////////////////////////////////////////////////////////////////////
// ibTemplateWizard — modal "Создать из шаблона..." wizard for OES Designer.
//
// Flagship "голое решение из коробки" UX: gallery of 4 production templates
// (accounting-demo, manufacturing-demo, services-demo, trade-demo) →
// preview → optional customize → apply via metaBridge mutations.
//
// Three pages, swapped inside a single wxDialog via wxSimplebook:
//   Page 1 — Gallery: 2x2 card grid; click selects + advances.
//   Page 2 — Preview: tree of objects, demo data summary, module list.
//   Page 3 — Customize: rename map, exclude, or natural-language tweak
//            via oes_template_customize Pugi tool.
//
// All MCP tool calls run on worker threads (cpp-httplib direct POST to
// https://mcp.pugi.io/api/oes-mcp/invoke) with credentials read from the
// aiBridge plugin's BYOK env (TOKEN/TENANT/LOCALE). Results post back to
// the UI thread via wxQueueEvent + bound wxEVT_THREAD handler.
//
// Apply walks the cached mutations[] from the template through
// metaBridge::HostMetaCreate/Edit/Delete. The wizard owns its own
// pluginId ("designer.templateWizard") which is granted AllowAlways on
// open and revoked on close — keeps the policy gate consistent with
// agent-mode plan application without leaking permissions long-term.
//
// Russian UI throughout — matches existing Designer panels (project
// search, AI todo, AI markers). Locale for template synonyms comes from
// wxLocale::GetSystemLanguage() at wizard open.
/////////////////////////////////////////////////////////////////////////////

#ifndef _IB_TEMPLATE_WIZARD_H_
#define _IB_TEMPLATE_WIZARD_H_

#include <wx/dialog.h>
#include <wx/string.h>
#include <wx/event.h>
#include <wx/thread.h>

#include <atomic>
#include <memory>
#include <string>
#include <vector>

class wxSimplebook;
class wxScrolledWindow;
class wxStaticText;
class wxButton;
class wxBoxSizer;
class wxRadioButton;
class wxCheckBox;
class wxTextCtrl;
class wxTreeCtrl;
class wxListCtrl;
class wxNotebook;
class wxListEvent;
class wxStyledTextCtrl;

class ibTemplateCard;
class ibTemplateWizardPreviewPage;
class ibTemplateWizardCustomizePage;

// Worker-thread response carrier — posts back to UI via wxThreadEvent.
// One field per MCP tool the wizard talks to. The `kind` field selects
// which payload field is meaningful; the rest stays empty.
struct ibTemplateWizardThreadPayload {
	enum class Kind {
		TemplatesList,    // m_responseJson = full oes_templates_list response
		TemplateGet,      // m_responseJson = full oes_template_get response
		TemplateCustomize,// m_responseJson = full oes_template_customize response
		Error,            // m_error = diagnostic text
	};
	Kind     m_kind   = Kind::Error;
	wxString m_responseJson;
	wxString m_error;
	// Token to drop stale responses (user navigated away mid-fetch).
	long m_requestEpoch = 0;
};

// Internal model of one template — the wizard pulls these from the
// oes_templates_list response and feeds them to ibTemplateCard.
struct ibTemplateInfo {
	wxString id;                     // "accounting-demo" etc.
	wxString version;
	wxString name;                   // localized
	wxString description;            // localized
	std::vector<wxString> tags;
	int      objectCount  = 0;
	int      demoRowCount = 0;
	wxString thumbnailUrl;           // optional, may be empty
	std::vector<wxString> previewModules;
};

class ibTemplateWizard : public wxDialog {
public:
	ibTemplateWizard(wxWindow* parent);
	~ibTemplateWizard() override;

	// Entry point — constructs + ShowModal. Designer's File menu handler
	// calls this static helper so the menu side stays a one-liner.
	static int Run(wxWindow* parent);

private:
	// ----- Page navigation -----
	enum Page { PAGE_GALLERY = 0, PAGE_PREVIEW = 1, PAGE_CUSTOMIZE = 2 };
	void GoTo(Page p);

	// ----- Build the three pages -----
	wxWindow* BuildGalleryPage(wxWindow* parent);
	wxWindow* BuildPreviewPage(wxWindow* parent);
	wxWindow* BuildCustomizePage(wxWindow* parent);

	// ----- Worker-thread launchers -----
	// All three start a std::thread that POSTs to Pugi, then queues a
	// wxThreadEvent back to the UI. The launchers bump m_requestEpoch
	// so any earlier in-flight fetch is dropped on arrival.
	void StartFetchTemplatesList();
	void StartFetchTemplateGet(const wxString& templateId, bool includeData);
	void StartFetchTemplateCustomize(const wxString& templateId,
	                                  const wxString& modificationsJson,
	                                  const wxString& userPrompt);

	// ----- UI thread response handlers -----
	void OnThreadResponse(wxThreadEvent& event);
	void OnTemplatesListResponse(const wxString& responseJson);
	void OnTemplateGetResponse(const wxString& responseJson);
	void OnTemplateCustomizeResponse(const wxString& responseJson);

	// ----- Page button handlers -----
	void OnGalleryCancel(wxCommandEvent&);
	void OnGalleryEmptyConfig(wxCommandEvent&);
	void OnGalleryCardClicked(const wxString& templateId);
	void OnPreviewBack(wxCommandEvent&);
	void OnPreviewCustomize(wxCommandEvent&);
	void OnPreviewApply(wxCommandEvent&);
	void OnCustomizeBack(wxCommandEvent&);
	void OnCustomizeApply(wxCommandEvent&);

	// ----- Apply flow -----
	// Walks m_mutations through metaBridge::HostMetaCreate/Edit/Delete.
	// Optionally also dispatches the demoData inserts when m_includeData
	// is true. Runs on the UI thread (metaBridge asserts main-thread).
	void ApplyMutations();

	// ----- Plugin id management -----
	// Grants AllowAlways for our wizard pluginId on open; restores prior
	// policy on close. Keeps the security boundary explicit.
	void GrantWizardPolicy();
	void RestoreWizardPolicy();

	// ----- Credentials helper -----
	// Fetches TOKEN/TENANT/LOCALE/ENDPOINT from aiBridge.env via the
	// plugin manager. Returns false + diagnostic if any are missing.
	bool ReadAiBridgeCreds(std::string& tokenOut,
	                        std::string& tenantOut,
	                        std::string& localeOut,
	                        std::string& endpointOut,
	                        wxString&    errorOut) const;

	// ----- Busy-state UI -----
	void SetBusy(bool busy, const wxString& message);

private:
	// ----- Page books + nav -----
	wxSimplebook* m_book          = nullptr;
	Page          m_currentPage   = PAGE_GALLERY;

	// ----- Gallery page widgets -----
	wxScrolledWindow* m_galleryScroller = nullptr;
	wxBoxSizer*       m_galleryGridSizer = nullptr;
	std::vector<ibTemplateCard*> m_cards;
	wxStaticText*     m_galleryStatus = nullptr;

	// ----- Preview page widgets -----
	ibTemplateWizardPreviewPage* m_previewPage = nullptr;

	// ----- Customize page widgets -----
	ibTemplateWizardCustomizePage* m_customizePage = nullptr;

	// ----- Model -----
	std::vector<ibTemplateInfo> m_templates;
	wxString m_selectedTemplateId;
	wxString m_localeKey;                 // "ru-RU" / "uk-UA" / "en-US"
	wxString m_localeKeyShort;            // "ru" / "uk" / "en"

	// Full last-fetched oes_template_get response (mutations[] + demoData[])
	// stashed as a string so the apply step can re-parse on-demand. Using
	// a string here avoids dragging nlohmann/json into the header.
	wxString m_templateGetResponseJson;

	// Request epoch — bumped on every fetch start; the response handler
	// drops the payload if the epoch doesn't match (user navigated away).
	std::atomic<long> m_requestEpoch{0};

	// True when the user wants demo data on apply (Page 2 checkbox).
	bool m_includeData = true;

	// Tracking: did we elevate policy, so we know to restore on close?
	bool m_policyGranted = false;
};

#endif // _IB_TEMPLATE_WIZARD_H_
