
#include "settingsdialog.h"
#include "keybinderdialog.h"
#include "fontcolorsettingspanel.h"
#include "editorsettingspanel.h"
#include "showhelpevent.h"

#include <wx/bookctrl.h>
    
BEGIN_EVENT_TABLE(SettingsDialog, wxPropertySheetDialog)
    EVT_INIT_DIALOG(        SettingsDialog::OnInitDialog)
    EVT_HELP(wxID_ANY,                      OnHelp) 
END_EVENT_TABLE()

SettingsDialog::SettingsDialog(wxWindow* parent)
    : wxPropertySheetDialog(parent, -1, "Settings", wxDefaultPosition, wxSize(450, 450), wxDEFAULT_DIALOG_STYLE | wxRESIZE_BORDER) 
{
    SetMinSize(wxSize(450, 450));

    CreateButtons(wxOK | wxCANCEL);
    
    m_keyBinderDialog = new KeyBinderDialog(GetBookCtrl());
    GetBookCtrl()->AddPage(m_keyBinderDialog, wxT("Key Bindings"));

    m_editorSettingsPanel = new EditorSettingsPanel(GetBookCtrl());
    GetBookCtrl()->AddPage(m_editorSettingsPanel, wxT("Editor"));

    m_fontColorSettingsPanel = new FontColorSettingsPanel(GetBookCtrl());
    GetBookCtrl()->AddPage(m_fontColorSettingsPanel, wxT("Font and Colors"));

    LayoutDialog();

}

void SettingsDialog::OnInitDialog(wxInitDialogEvent& event)
{
    m_keyBinderDialog->Initialize();
    m_fontColorSettingsPanel->Initialize();
    m_editorSettingsPanel->Initialize();
}

KeyBinderDialog* SettingsDialog::GetKeyBinderDialog() const
{
    return m_keyBinderDialog;
}

FontColorSettingsPanel* SettingsDialog::GetFontColorSettingsPanel() const
{
    return m_fontColorSettingsPanel;
}

EditorSettingsPanel* SettingsDialog::GetEditorSettingsPanel() const
{
    return m_editorSettingsPanel;
}

void SettingsDialog::OnHelp(wxHelpEvent&)
{
    wxCommandEvent event( wxEVT_SHOW_HELP_EVENT, GetId() );
    event.SetEventObject( this );
    event.SetString( wxT("Key Bindings") );
    GetParent()->GetEventHandler()->ProcessEvent( event );
}