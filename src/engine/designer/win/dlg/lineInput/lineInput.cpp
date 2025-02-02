////////////////////////////////////////////////////////////////////////////
//	Author		: Maxim Kornienko
//	Description : line input window
////////////////////////////////////////////////////////////////////////////

#include "lineInput.h"

CDialogLineInput::CDialogLineInput(wxWindow* parent, wxWindowID id, const wxString& title, const wxPoint& pos, const wxSize& size, long style) : wxDialog(parent, id, title, pos, size, style)
{
	this->SetSizeHints(wxDefaultSize, wxDefaultSize);

	wxBoxSizer* bSizerTotal = new wxBoxSizer(wxHORIZONTAL);
	wxBoxSizer* bSizerLeft = new wxBoxSizer(wxVERTICAL);

	m_staticTextLine = new wxStaticText(this, wxID_ANY, _("Enter line number:"), wxDefaultPosition, wxDefaultSize, 0);
	m_staticTextLine->Wrap(-1);
	bSizerLeft->Add(m_staticTextLine, 0, wxEXPAND | wxTOP | wxRIGHT | wxLEFT, 5);

	m_lineNumber = new wxTextCtrl(this, wxID_ANY, _("1"), wxDefaultPosition, wxDefaultSize, 0);
	bSizerLeft->Add(m_lineNumber, 0, wxTOP | wxRIGHT | wxLEFT, 5);

	bSizerTotal->Add(bSizerLeft, 0, wxEXPAND, 5);

	wxBoxSizer* bSizerRight;
	bSizerRight = new wxBoxSizer(wxVERTICAL);

	m_buttonGoTo = new wxButton(this, wxID_ANY, _("Go to"), wxDefaultPosition, wxDefaultSize, 0);
	bSizerRight->Add(m_buttonGoTo, 0, wxTOP | wxRIGHT | wxLEFT, 5);

	m_buttonCancel = new wxButton(this, wxID_ANY, _("Cancel"), wxDefaultPosition, wxDefaultSize, 0);
	bSizerRight->Add(m_buttonCancel, 0, wxBOTTOM | wxRIGHT | wxLEFT, 5);

	bSizerTotal->Add(bSizerRight, 0, 0, 5);

	this->SetSizer(bSizerTotal);
	this->Layout();

	bSizerTotal->Fit(this);
	this->Centre(wxBOTH);

	// Connect Events
	m_buttonGoTo->Connect(wxEVT_COMMAND_BUTTON_CLICKED, wxCommandEventHandler(CDialogLineInput::OnButtonGoToClick), nullptr, this);
	m_buttonCancel->Connect(wxEVT_COMMAND_BUTTON_CLICKED, wxCommandEventHandler(CDialogLineInput::OnButtonCancelClick), nullptr, this);
}

CDialogLineInput::~CDialogLineInput()
{
	// Disconnect Events
	m_buttonGoTo->Disconnect(wxEVT_COMMAND_BUTTON_CLICKED, wxCommandEventHandler(CDialogLineInput::OnButtonGoToClick), nullptr, this);
	m_buttonCancel->Disconnect(wxEVT_COMMAND_BUTTON_CLICKED, wxCommandEventHandler(CDialogLineInput::OnButtonCancelClick), nullptr, this);
}

void CDialogLineInput::OnButtonGoToClick(wxCommandEvent& event) 
{
	EndModal(wxAtoi(m_lineNumber->GetValue()));
}

void CDialogLineInput::OnButtonCancelClick(wxCommandEvent& event) 
{
	EndModal(wxNOT_FOUND);
}