
#include "settingsdialog.h"
#include "keybinderdialog.h"
#include "fontcolorsettingspanel.h"
#include "editorsettingspanel.h"
#include "showhelpevent.h"

#include <wx/bookctrl.h>
    
BEGIN_EVENT_TABLE(ibDialogSettings, wxPropertySheetDialog)
    EVT_INIT_DIALOG(        ibDialogSettings::OnInitDialog)
    EVT_HELP(wxID_ANY,                      ibDialogSettings::OnHelp)
END_EVENT_TABLE()

ibDialogSettings::ibDialogSettings(wxWindow* parent)
    : wxPropertySheetDialog(parent, -1, _("Settings"), wxDefaultPosition, wxSize(450, 450), wxDEFAULT_DIALOG_STYLE | wxRESIZE_BORDER) 
{
    SetMinSize(wxSize(450, 450));

    CreateButtons(wxOK | wxCANCEL);
    
    m_keyBinderDialog = new ibDialogKeyBinder(GetBookCtrl());
    GetBookCtrl()->AddPage(m_keyBinderDialog, _("Key Bindings"));

    m_editorSettingsPanel = new ibPanelEditorSettings(GetBookCtrl());
    GetBookCtrl()->AddPage(m_editorSettingsPanel, _("Editor"));

    m_fontColorSettingsPanel = new ibPanelFontColorSettings(GetBookCtrl());
    GetBookCtrl()->AddPage(m_fontColorSettingsPanel, _("Font and Colors"));

    LayoutDialog();

}

void ibDialogSettings::OnInitDialog(wxInitDialogEvent& event)
{
    m_keyBinderDialog->Initialize();
    m_fontColorSettingsPanel->Initialize();
    m_editorSettingsPanel->Initialize();
}

ibDialogKeyBinder* ibDialogSettings::GetKeyBinderDialog() const
{
    return m_keyBinderDialog;
}

ibPanelFontColorSettings* ibDialogSettings::GetFontColorSettingsPanel() const
{
    return m_fontColorSettingsPanel;
}

ibPanelEditorSettings* ibDialogSettings::GetEditorSettingsPanel() const
{
    return m_editorSettingsPanel;
}

void ibDialogSettings::OnHelp(wxHelpEvent&)
{
    wxCommandEvent event( wxEVT_SHOW_HELP_EVENT, GetId() );
    event.SetEventObject( this );
    event.SetString( wxT("Key Bindings") );
    GetParent()->GetEventHandler()->ProcessEvent( event );
}