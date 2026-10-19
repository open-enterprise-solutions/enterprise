#ifndef SETTINGS_DIALOG_H
#define SETTINGS_DIALOG_H

/////////////////////////////////////////////////////////////////////////////
// Tools → Options: one page per thing a person sets. The dialog only holds
// the pages; the caller fills them before ShowModal and reads them after OK.
/////////////////////////////////////////////////////////////////////////////

#include <wx/wx.h>
#include <wx/propdlg.h>

#include "frontend/frontend.h"

class ibPanelKeyBinder;
class ibPanelFontColorSettings;
class ibPanelEditorSettings;
class ibPanelMcpSettings;

class FRONTEND_API ibDialogSettings : public wxPropertySheetDialog
{
public:

	explicit ibDialogSettings(wxWindow* parent);

	ibPanelKeyBinder* GetKeyBinderPanel() const { return m_keyBinderPanel; }
	ibPanelEditorSettings* GetEditorSettingsPanel() const { return m_editorSettingsPanel; }
	ibPanelFontColorSettings* GetFontColorSettingsPanel() const { return m_fontColorSettingsPanel; }
	ibPanelMcpSettings* GetMcpSettingsPanel() const { return m_mcpSettingsPanel; }

private:

	// The pages that are filled after construction finish here, once the caller has filled them.
	void OnInitDialog(wxInitDialogEvent& event);

	ibPanelKeyBinder* m_keyBinderPanel = nullptr;
	ibPanelEditorSettings* m_editorSettingsPanel = nullptr;
	ibPanelFontColorSettings* m_fontColorSettingsPanel = nullptr;
	ibPanelMcpSettings* m_mcpSettingsPanel = nullptr;
};

#endif
