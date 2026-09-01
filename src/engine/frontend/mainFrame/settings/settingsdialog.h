#ifndef SETTINGS_DIALOG_H
#define SETTINGS_DIALOG_H

#include <wx/wx.h>
#include <wx/propdlg.h> 

//
// Forward declarations.
//

class ibDialogKeyBinder;
class ibPanelFontColorSettings;
class ibPanelEditorSettings;
class ibPanelMcpSettings;

#include "frontend/frontend.h"

/**
 * Settings dialog class.
 */
class FRONTEND_API ibDialogSettings : public wxPropertySheetDialog
{

public:

    /**
     * Constructor.
     */
    explicit ibDialogSettings(wxWindow* parent);

    /**
     * Called when the dialog is initialized.
     */
    void OnInitDialog(wxInitDialogEvent& event);

    /**
     * Called when the user activates the content help either by hitting the ? button
     * or pressing F1.
     */
    void OnHelp(wxHelpEvent& event);

    /**
     * Returns the key binder part of the settings dialog.
     */
    ibDialogKeyBinder* GetKeyBinderDialog() const;

    /**
     * Returns the font and color part of the settings dialog.
     */
    ibPanelFontColorSettings* GetFontColorSettingsPanel() const;

    /**
     * Returns the editor part of the settings dialog.
     */
    ibPanelEditorSettings* GetEditorSettingsPanel() const;

    /**
     * Returns the assistant-access (MCP) part of the settings dialog.
     */
    ibPanelMcpSettings* GetMcpSettingsPanel() const;

    wxDECLARE_EVENT_TABLE();

private:

    ibDialogKeyBinder*            m_keyBinderDialog;
    ibPanelFontColorSettings*     m_fontColorSettingsPanel;
    ibPanelEditorSettings*        m_editorSettingsPanel;
    ibPanelMcpSettings*           m_mcpSettingsPanel;

};

#endif