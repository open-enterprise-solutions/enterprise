////////////////////////////////////////////////////////////////////////////
//	Author		: Maxim Kornienko
//	Description : form selector
////////////////////////////////////////////////////////////////////////////

#include "formSelector.h"

#include "core/stringUtils.h"
#include "frmclient/mainFrame/mainFrame.h"
#include "protocol/protocol.h"

namespace {

// The id of the first kind's button — the others follow it in their places.
constexpr int kFirstChoiceId = wxID_HIGHEST + 1;

} // namespace

ibDialogSelectTypeForm::ibDialogSelectTypeForm(const ibProtocolNode& request)
	: wxDialog(ibFrontendMainFrame::GetFrame(), wxID_ANY, wxEmptyString, wxDefaultPosition, wxDefaultSize, wxDEFAULT_DIALOG_STYLE | wxDIALOG_ADAPTATION_ANY_SIZER)
{
	SetTitle(request.GetString(ibProtocolName::Caption));
	SetClientSize(FromDIP(wxSize(480, 320)));

	for (const ibProtocolNode& choice : request.Children())
		m_listChoice.emplace_back(choice.GetString(ibProtocolName::Name), choice.GetString(ibProtocolName::Caption),
			static_cast<int>(choice.GetInt(ibProtocolName::Id)));

	wxBoxSizer* bSizerMain = new wxBoxSizer(wxVERTICAL);
	wxStaticBoxSizer* sbSizerMain = new wxStaticBoxSizer(new wxStaticBox(this, wxID_ANY, _("Select form type")), wxVERTICAL);

	// A button's id is its place: a generic form's kind (wxNOT_FOUND) is wxID_ANY to a window.
	for (std::size_t idx = 0; idx < m_listChoice.size(); ++idx) {
		wxRadioButton* radioButton = new wxRadioButton(sbSizerMain->GetStaticBox(), kFirstChoiceId + static_cast<int>(idx),
			m_listChoice[idx].m_label, wxDefaultPosition, wxDefaultSize);
		radioButton->Connect(wxEVT_RADIOBUTTON, wxCommandEventHandler(ibDialogSelectTypeForm::OnFormTypeChanged), nullptr, this);
		sbSizerMain->Add(radioButton, 1, wxALL | wxEXPAND, FromDIP(5));
	}

	bSizerMain->Add(sbSizerMain, 1, wxEXPAND, FromDIP(5));

	wxBoxSizer* bSizerHeader = new wxBoxSizer(wxHORIZONTAL);
	wxBoxSizer* bSizerLeft = new wxBoxSizer(wxVERTICAL);

	m_staticTextName = new wxStaticText(this, wxID_ANY, _("Name"), wxDefaultPosition, wxDefaultSize, 0);
	m_staticTextName->Wrap(-1);

	bSizerLeft->Add(m_staticTextName, 0, wxALL, FromDIP(5));

	m_staticTextSynonym = new wxStaticText(this, wxID_ANY, _("Synonym"), wxDefaultPosition, wxDefaultSize, 0);
	m_staticTextSynonym->Wrap(-1);

	bSizerLeft->Add(m_staticTextSynonym, 0, wxALL, FromDIP(5));

	m_staticTextComment = new wxStaticText(this, wxID_ANY, _("Comment"), wxDefaultPosition, wxDefaultSize, 0);
	m_staticTextComment->Wrap(-1);

	bSizerLeft->Add(m_staticTextComment, 0, wxALL, FromDIP(5));
	bSizerHeader->Add(bSizerLeft, 0, wxEXPAND, FromDIP(5));

	wxBoxSizer* bSizerRight = new wxBoxSizer(wxVERTICAL);

	m_textCtrlName = new wxTextCtrl(this, wxID_ANY, request.GetString(ibProtocolName::Name), wxDefaultPosition, wxDefaultSize, 0);
	m_textCtrlName->Connect(wxEVT_TEXT, wxCommandEventHandler(ibDialogSelectTypeForm::OnTextEnter), nullptr, this);
	bSizerRight->Add(m_textCtrlName, 0, wxALL | wxEXPAND, 1);

	m_textCtrlSynonym = new wxTextCtrl(this, wxID_ANY, request.GetString(ibProtocolName::Synonym), wxDefaultPosition, wxDefaultSize, 0);
	bSizerRight->Add(m_textCtrlSynonym, 0, wxALL | wxEXPAND, 1);

	m_textCtrlComment = new wxTextCtrl(this, wxID_ANY, request.GetString(ibProtocolName::Comment), wxDefaultPosition, wxDefaultSize, 0);
	bSizerRight->Add(m_textCtrlComment, 0, wxALL | wxEXPAND, 1);

	bSizerHeader->Add(bSizerRight, 1, wxEXPAND, FromDIP(5));
	bSizerMain->Add(bSizerHeader, 1, wxEXPAND, FromDIP(10));

	wxBoxSizer* bSizerBottom = new wxBoxSizer(wxHORIZONTAL);

	m_sdbSizerOK = new wxButton(this, wxID_ANY, _("OK"), wxDefaultPosition, wxDefaultSize, 0);
	m_sdbSizerOK->Connect(wxEVT_BUTTON, wxCommandEventHandler(ibDialogSelectTypeForm::OnButtonOk), nullptr, this);

	bSizerBottom->Add(m_sdbSizerOK, 0, wxALL, FromDIP(5));

	m_sdbSizerCancel = new wxButton(this, wxID_ANY, _("Cancel"), wxDefaultPosition, wxDefaultSize, 0);
	m_sdbSizerCancel->Connect(wxEVT_BUTTON, wxCommandEventHandler(ibDialogSelectTypeForm::OnButtonCancel), nullptr, this);
	bSizerBottom->Add(m_sdbSizerCancel, 0, wxALL, FromDIP(5));

	bSizerMain->Add(bSizerBottom, 0, wxEXPAND, FromDIP(5));

	wxDialog::SetSizer(bSizerMain);
	wxDialog::Layout();

	// The first kind is the one checked — and the one an OK without a click answers, named as a click on it names it
	// (the desktop's answered a kind it never set).
	if (!m_listChoice.empty()) {
		wxCommandEvent event(wxEVT_RADIOBUTTON, kFirstChoiceId);
		OnFormTypeChanged(event);
	}
}

ibDialogSelectTypeForm::~ibDialogSelectTypeForm()
{
}

bool ibDialogSelectTypeForm::ShowModal(ibProtocolNode& response)
{
	// Answered by OK alone: Cancel, Esc and the close box are all no kind.
	if (m_listChoice.empty() || wxDialog::ShowModal() != wxID_OK)
		return false;

	response.SetValue(ibProtocolName::Id, static_cast<long long>(m_choice));
	response.SetValue(ibProtocolName::Name, m_textCtrlName->GetValue());
	response.SetValue(ibProtocolName::Synonym, m_textCtrlSynonym->GetValue());
	response.SetValue(ibProtocolName::Comment, m_textCtrlComment->GetValue());
	return true;
}

void ibDialogSelectTypeForm::OnTextEnter(wxCommandEvent& event)
{
	wxString systemName =
		m_textCtrlName->GetValue();

	int pos = stringUtils::CheckCorrectName(systemName);
	if (pos > 0) {
		systemName = systemName.Left(pos);
		m_textCtrlName->SetValue(systemName);
		return;
	}

	wxString newSynonym = stringUtils::GenerateSynonym(systemName);
	m_textCtrlSynonym->SetValue(newSynonym);
	event.Skip();
}

void ibDialogSelectTypeForm::OnButtonOk(wxCommandEvent& event)
{
	EndModal(wxID_OK);
	event.Skip();
}

void ibDialogSelectTypeForm::OnButtonCancel(wxCommandEvent& event)
{
	EndModal(wxNOT_FOUND);
	event.Skip();
}

void ibDialogSelectTypeForm::OnFormTypeChanged(wxCommandEvent& event)
{
	const std::size_t idx = static_cast<std::size_t>(event.GetId() - kFirstChoiceId);

	// The name the kind gives the form — the server's (GetNewName there).
	if (idx < m_listChoice.size()) {
		m_choice = m_listChoice[idx].m_value;
		m_textCtrlName->SetValue(m_listChoice[idx].m_name);
		m_textCtrlSynonym->SetValue(
			stringUtils::GenerateSynonym(m_listChoice[idx].m_name)
		);
	}

	event.Skip();
}
