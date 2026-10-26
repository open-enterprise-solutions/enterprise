#include "applyChange.h"

#include <wx/settings.h>

#include "protocol/protocol.h"
#include "frmclient/win/picture.h"

// An entry's Level that is an error — the engine's ibRestructure::error.
static constexpr long long s_levelError = 2;

ibDialogApplyChange::ibDialogApplyChange(const ibProtocolNode& request, wxWindow* parent) :
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
	m_resultBox->SetBackgroundColour(wxSystemSettings::GetColour(wxSYS_COLOUR_BTNHIGHLIGHT));

	bool hasErrors = false;
	for (const ibProtocolNode& entry : request.Children()) {
		m_resultBox->Append(entry.GetString(ibProtocol::ibRequest::ibApplyChange::Text));
		if (entry.GetInt(ibProtocol::ibRequest::ibApplyChange::Level) == s_levelError)
			hasErrors = true;
	}

	windowSizer->Add(m_resultBox, 1, wxALL | wxEXPAND, FromDIP(5));
	resultSizer->Add(windowSizer, 1, wxEXPAND, FromDIP(5));

	wxBoxSizer* buttonSizer = new wxBoxSizer(wxVERTICAL);

	m_buttonApply = new wxButton(this, wxID_OK, _("Apply"), wxDefaultPosition, wxDefaultSize, 0);
	// AN ERROR IN THE LEDGER BLOCKS THE APPLY — the desktop's: an error entry says what has to be put right first; until
	// it is, there is nothing to press (the server rolls such a ledger back whatever is answered).
	if (hasErrors) {
		m_buttonApply->Enable(false);
		// ASCII ONLY in this literal, as the desktop's: the same message, the same translation.
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

	const wxBitmap picture = ibProtocolPicture(request, ibProtocol::ibRequest::ibApplyChange::Picture);
	if (picture.IsOk()) {
		wxIcon dlg_icon;
		dlg_icon.CopyFromBitmap(picture);
		wxDialog::SetIcon(dlg_icon);
	}
	wxDialog::SetFocus();
}
