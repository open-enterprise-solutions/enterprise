/////////////////////////////////////////////////////////////////////////////
// ibTemplateWizardPreviewPage — Page 2 of the template wizard.
//
// Notebook with three tabs:
//   1. Структура   — wxTreeCtrl of objects from oes_template_get
//                     mutations[] (grouped by kind).
//   2. Demo data   — wxListCtrl of sampled demoData rows (when present).
//   3. Модули      — wxListBox of previewModule names; double-click opens
//                     a read-only wxStyledTextCtrl pop-up showing module
//                     source (also from previewModules[] if Pugi returns
//                     {name, source} pairs; we tolerate either flat-strings
//                     or objects).
//
// Bottom row: "Включить демо-данные" checkbox (default on), then
// [Назад] [Настроить] [Применить как есть] buttons.
//
// The page is owned by the wizard which feeds it a JSON response via
// LoadFrom(...). The page parses the response once and holds the parsed
// shape internally (no nlohmann in the header).
/////////////////////////////////////////////////////////////////////////////

#ifndef _IB_TEMPLATE_WIZARD_PREVIEW_H_
#define _IB_TEMPLATE_WIZARD_PREVIEW_H_

#include <wx/panel.h>
#include <wx/string.h>
#include <functional>
#include <vector>

class wxTreeCtrl;
class wxListCtrl;
class wxListBox;
class wxNotebook;
class wxCheckBox;
class wxStaticText;
class wxButton;

class ibTemplateWizardPreviewPage : public wxPanel {
public:
	using BackCallback     = std::function<void()>;
	using CustomizeCallback = std::function<void()>;
	using ApplyCallback    = std::function<void(bool includeData)>;

	ibTemplateWizardPreviewPage(wxWindow* parent,
	                              BackCallback     onBack,
	                              CustomizeCallback onCustomize,
	                              ApplyCallback    onApply);
	~ibTemplateWizardPreviewPage() override = default;

	// Populates the tree + lists from an oes_template_get response.
	// Tolerates Pugi's expected shape:
	//   { structure: [{op,kind,fullName,properties}], demoData: [...] }
	// AND the alternative {mutations:[...]} shape some older Pugi
	// versions return. Missing keys produce empty tabs (not an error —
	// the user still sees the template name/version in the header).
	void LoadFrom(const wxString& responseJson,
	               const wxString& templateName,
	               const wxString& version);

	// Current state of the "include demo data" checkbox.
	bool IncludeDataChecked() const;

private:
	wxStaticText* m_header     = nullptr;
	wxNotebook*   m_notebook   = nullptr;
	wxTreeCtrl*   m_structureTree = nullptr;
	wxListCtrl*   m_demoList    = nullptr;
	wxListBox*    m_modulesList = nullptr;
	wxCheckBox*   m_includeDataCheckBox = nullptr;
};

#endif // _IB_TEMPLATE_WIZARD_PREVIEW_H_
