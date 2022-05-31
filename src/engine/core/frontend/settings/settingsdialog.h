#ifndef SETTINGS_DIALOG_H
#define SETTINGS_DIALOG_H

#include <wx/wx.h>
#include <wx/propdlg.h> 

//
// Forward declarations.
//

class KeyBinderDialog;
class FontColorSettingsPanel;
class EditorSettingsPanel;

/**
 * Settings dialog class.
 */
class SettingsDialog : public wxPropertySheetDialog
{

public:

    /**
     * Constructor.
     */
    explicit SettingsDialog(wxWindow* parent);

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
    KeyBinderDialog* GetKeyBinderDialog() const;

    /**
     * Returns the font and color part of the settings dialog.
     */
    FontColorSettingsPanel* GetFontColorSettingsPanel() const;

    /**
     * Returns the editor part of the settings dialog.
     */
    EditorSettingsPanel* GetEditorSettingsPanel() const;

    DECLARE_EVENT_TABLE()

private:

    KeyBinderDialog*            m_keyBinderDialog;
    FontColorSettingsPanel*     m_fontColorSettingsPanel;
    EditorSettingsPanel*        m_editorSettingsPanel;

};

#endif