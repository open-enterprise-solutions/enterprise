#include "settingsdialog.h"
#include "keybinderpanel.h"
#include "editorsettingspanel.h"
#include "fontcolorsettingspanel.h"
#include "mcpsettingspanel.h"

#include <wx/bookctrl.h>

ibDialogSettings::ibDialogSettings(wxWindow* parent)
	: wxPropertySheetDialog(parent, wxID_ANY, _("Settings"), wxDefaultPosition, wxDefaultSize,
		wxDEFAULT_DIALOG_STYLE | wxRESIZE_BORDER)
{
	CreateButtons(wxOK | wxCANCEL);

	wxBookCtrlBase* const book = GetBookCtrl();
	m_keyBinderPanel = new ibPanelKeyBinder(book);
	book->AddPage(m_keyBinderPanel, _("Key Bindings"));
	m_editorSettingsPanel = new ibPanelEditorSettings(book);
	book->AddPage(m_editorSettingsPanel, _("Editor"));
	m_fontColorSettingsPanel = new ibPanelFontColorSettings(book);
	book->AddPage(m_fontColorSettingsPanel, _("Font and Colors"));
	m_mcpSettingsPanel = new ibPanelMcpSettings(book);
	book->AddPage(m_mcpSettingsPanel, _("Assistant access"));

	// The floor first, and then the dialog fits what its pages hold. Set to the floor AFTER the fit, it was squeezed
	// below the assistant page - the configuration showed four lines of eleven (2026-10-21).
	SetMinSize(FromDIP(wxSize(480, 480)));
	LayoutDialog();

	Bind(wxEVT_INIT_DIALOG, &ibDialogSettings::OnInitDialog, this);
}

void ibDialogSettings::OnInitDialog(wxInitDialogEvent& event)
{
	m_mcpSettingsPanel->Initialize();
	event.Skip();
}
