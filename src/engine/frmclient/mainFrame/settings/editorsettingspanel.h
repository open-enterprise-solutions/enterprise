#ifndef EDITOR_SETTINGS_PANEL_H
#define EDITOR_SETTINGS_PANEL_H

/////////////////////////////////////////////////////////////////////////////
// The editor page of the settings dialog. Every setting has its field, so
// the fields ARE the value: SetSettings fills them, GetSettings reads them.
/////////////////////////////////////////////////////////////////////////////

#include <wx/wx.h>
#include <wx/spinctrl.h>

#include "editorsettings.h"

class FRMCLIENT_API ibPanelEditorSettings : public wxPanel
{
public:

	explicit ibPanelEditorSettings(wxWindow* parent);

	void SetSettings(const ibEditorSettings& settings);
	ibEditorSettings GetSettings() const;

private:

	wxSpinCtrl* m_indentSizeCtrl = nullptr;
	wxRadioButton* m_useSpaces = nullptr;
	wxRadioButton* m_useTabs = nullptr;
	wxCheckBox* m_removeTabsOnLoad = nullptr;
	wxCheckBox* m_showLineNumbers = nullptr;
	wxCheckBox* m_showWhiteSpace = nullptr;
	wxCheckBox* m_enableAutoComplete = nullptr;
	wxCheckBox* m_mostRecentlyUsedTabSwitching = nullptr;
};

#endif
