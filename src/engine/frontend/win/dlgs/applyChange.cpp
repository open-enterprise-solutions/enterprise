#include "applyChange.h"

ibDialogApplyChange::ibDialogApplyChange(const ibRestructureInfo& info, wxWindow* parent) :
	wxDialog(parent, wxID_ANY, _("Design changes"), wxDefaultPosition, wxSize(500, 200), wxDEFAULT_DIALOG_STYLE)
{
	wxDialog::SetSizeHints(wxDefaultSize, wxDefaultSize);

	wxDialog::SetForegroundColour(wxSystemSettings::GetColour(wxSYS_COLOUR_SCROLLBAR));
	wxDialog::SetBackgroundColour(wxSystemSettings::GetColour(wxSYS_COLOUR_SCROLLBAR));

	wxBoxSizer* mainSizer = new wxBoxSizer(wxHORIZONTAL);
	wxBoxSizer* resultSizer = new wxBoxSizer(wxHORIZONTAL);
	wxBoxSizer* windowSizer = new wxBoxSizer(wxVERTICAL);

	m_staticInformation = new wxStaticText(this, wxID_ANY, _("Changes detected in metadata structure:"), wxDefaultPosition, wxDefaultSize, wxALIGN_CENTER_HORIZONTAL);
	m_staticInformation->Wrap(-1);
	m_staticInformation->SetForegroundColour(wxColour(0, 120, 215));

	windowSizer->Add(m_staticInformation, 0, wxEXPAND | wxRIGHT | wxLEFT, FromDIP(5));

	m_resultBox = new wxListBox(this, wxID_ANY, wxDefaultPosition, wxDefaultSize, 0, NULL, wxLB_SINGLE);
	//m_resultBox->SetForegroundColour(wxSystemSettings::GetColour(wxSYS_COLOUR_INFOBK));
	m_resultBox->SetBackgroundColour(wxSystemSettings::GetColour(wxSYS_COLOUR_BTNHIGHLIGHT));

	for (size_t idx = 0; idx < info.Count(); idx++) {
		m_resultBox->Append(info.At(idx).descr);
	}

	windowSizer->Add(m_resultBox, 1, wxALL | wxEXPAND, FromDIP(5));
	resultSizer->Add(windowSizer, 1, wxEXPAND, FromDIP(5));

	wxBoxSizer* buttonSizer = new wxBoxSizer(wxVERTICAL);

	m_buttonApply = new wxButton(this, wxID_OK, _("Apply"), wxDefaultPosition, wxDefaultSize, 0);
	// AN ERROR IN THE LEDGER BLOCKS THE APPLY. The ledger has carried AppendError / HasErrors all along
	// and nobody read them, so a change the platform knows it cannot carry out was still offered as if it
	// could — the user pressed Apply and met the failure halfway through the restructuring instead of
	// before it. An error entry says what has to be put right first; until it is, there is nothing to press.
	if (info.HasErrors()) {
		m_buttonApply->Enable(false);
		// ASCII ONLY in this literal: the file compiles as ANSI, so a typographic dash arrives as its
		// UTF-8 bytes shown one per character. (Same note as in mainFrameDesignerEvent.cpp.)
		m_staticInformation->SetLabel(_("The changes cannot be applied - see the errors below:"));
		m_staticInformation->SetForegroundColour(wxColour(200, 0, 0));
	}
	buttonSizer->Add(m_buttonApply, 0, wxALL, FromDIP(5));
	m_buttonCancel = new wxButton(this, wxID_CANCEL, _("Cancel"), wxDefaultPosition, wxDefaultSize, 0);
	
	buttonSizer->Add(m_buttonCancel, 0, wxALL, FromDIP(5));
	resultSizer->Add(buttonSizer, 0, wxEXPAND, FromDIP(5));

	mainSizer->Add(resultSizer, 1, wxEXPAND, FromDIP(5));

	wxDialog::SetSizer(mainSizer);
	wxDialog::Layout();

	wxDialog::Centre(wxBOTH);

	wxIcon dlg_icon;
	dlg_icon.CopyFromBitmap(ibBackendPicture::GetPicture(g_picStructureCLSID));

	wxDialog::SetIcon(dlg_icon);
	wxDialog::SetFocus();
}