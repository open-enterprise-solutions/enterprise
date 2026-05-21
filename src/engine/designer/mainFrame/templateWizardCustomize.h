/////////////////////////////////////////////////////////////////////////////
// ibTemplateWizardCustomizePage — Page 3 of the template wizard.
//
// Three sub-modes selected via radio buttons:
//   3a. Без изменений — straight through to apply.
//   3b. Ручная настройка — table of {orig name → new name} + per-object
//        exclude checkboxes; outputs a `modifications` JSON object the
//        wizard hands to oes_template_customize.
//   3c. Tweak with AI — large prompt textarea; outputs `userPrompt` for
//        oes_template_customize.
//
// Footer: [Назад] [Применить] buttons.
//
// The page exposes BuildPayload() which returns a struct describing the
// chosen sub-mode; the wizard then dispatches:
//   3a → ApplyMutations() directly (using the structure already cached
//        on the wizard from oes_template_get).
//   3b/3c → StartFetchTemplateCustomize(...) with the appropriate
//        modifications / userPrompt fields.
/////////////////////////////////////////////////////////////////////////////

#ifndef _IB_TEMPLATE_WIZARD_CUSTOMIZE_H_
#define _IB_TEMPLATE_WIZARD_CUSTOMIZE_H_

#include <wx/panel.h>
#include <wx/string.h>
#include <functional>
#include <vector>

class wxRadioButton;
class wxTextCtrl;
class wxListCtrl;
class wxButton;
class wxStaticText;

class ibTemplateWizardCustomizePage : public wxPanel {
public:
	enum class Mode { None, Manual, AiTweak };

	struct Payload {
		Mode     mode = Mode::None;
		wxString modificationsJson;   // {renames:{...}, excludes:[...]}
		wxString userPrompt;
	};

	using BackCallback  = std::function<void()>;
	using ApplyCallback = std::function<void(const Payload&)>;

	ibTemplateWizardCustomizePage(wxWindow* parent,
	                                BackCallback  onBack,
	                                ApplyCallback onApply);

	~ibTemplateWizardCustomizePage() override = default;

	// Populate the rename/exclude table from the cached oes_template_get
	// response. Walks structure[] or mutations[] and seeds one row per
	// object.
	void LoadObjectsFrom(const wxString& templateGetResponseJson);

private:
	void OnModeChanged(wxCommandEvent& event);
	Payload BuildPayload() const;

private:
	wxRadioButton* m_radioNone   = nullptr;
	wxRadioButton* m_radioManual = nullptr;
	wxRadioButton* m_radioAi     = nullptr;

	// Manual-mode controls
	wxListCtrl*   m_renameList    = nullptr;  // 3 cols: include, original, new

	// AI-tweak controls
	wxTextCtrl*   m_aiPromptCtrl = nullptr;

	// Tracking
	std::vector<wxString> m_objectFullNames;
};

#endif // _IB_TEMPLATE_WIZARD_CUSTOMIZE_H_
