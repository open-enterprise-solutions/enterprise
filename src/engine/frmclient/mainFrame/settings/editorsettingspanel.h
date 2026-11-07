#ifndef EDITOR_SETTINGS_PANEL_H
#define EDITOR_SETTINGS_PANEL_H

/////////////////////////////////////////////////////////////////////////////
// The editor page of the settings dialog. Every setting has its field, so
// the fields ARE the value: SetSettings fills them, GetSettings reads them.
/////////////////////////////////////////////////////////////////////////////

#include <wx/wx.h>
#include "gui/univ/ctrl/spinCtrl.h"

#include "editorsettings.h"
#include "gui/univ/ctrl/checkBox.h"
#include "gui/univ/ctrl/radioButton.h"
#include "gui/univ/panel.h"

class FRMCLIENT_API ibPanelEditorSettings : public ibPanel
{
public:

	explicit ibPanelEditorSettings(wxWindow* parent);

	void SetSettings(const ibEditorSettings& settings);
	ibEditorSettings GetSettings() const;

private:

	ibSpinCtrl* m_indentSizeCtrl = nullptr;
	ibRadioButton* m_useSpaces = nullptr;
	ibRadioButton* m_useTabs = nullptr;
	ibCheckBox* m_removeTabsOnLoad = nullptr;
	ibCheckBox* m_showLineNumbers = nullptr;
	ibCheckBox* m_showWhiteSpace = nullptr;
	ibCheckBox* m_enableAutoComplete = nullptr;
	ibCheckBox* m_mostRecentlyUsedTabSwitching = nullptr;
};

#endif
