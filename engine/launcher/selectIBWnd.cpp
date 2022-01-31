#include "selectIBWnd.h"

CSelectIBWnd::CSelectIBWnd(wxWindow* parent, wxWindowID id, const wxString& title, const wxPoint& pos, const wxSize& size, long style) :
	wxDialog(parent, id, title, pos, size, style)
{
	this->SetSizeHints(wxDefaultSize, wxDefaultSize);
	this->SetBackgroundColour(wxSystemSettings::GetColour(wxSYS_COLOUR_BTNFACE));

	wxBoxSizer* bSizer13 = new wxBoxSizer(wxVERTICAL);

	wxBoxSizer* mainSizer = new wxBoxSizer(wxHORIZONTAL);
	wxBoxSizer* bSizer11 = new wxBoxSizer(wxVERTICAL);

	m_staticName = new wxStaticText(this, wxID_ANY, _("Name:"), wxDefaultPosition, wxDefaultSize, 0);
	m_staticName->Wrap(-1);
	bSizer11->Add(m_staticName, 0, wxALL | wxEXPAND, 6);


	bSizer11->Add(0, 0, 1, wxEXPAND, 5);

	m_staticPath = new wxStaticText(this, wxID_ANY, _("Path:"), wxDefaultPosition, wxDefaultSize, 0);
	m_staticPath->Wrap(-1);
	bSizer11->Add(m_staticPath, 0, wxALL | wxEXPAND, 6);


	mainSizer->Add(bSizer11, 0, wxEXPAND, 5);

	wxBoxSizer* bSizer6 = new wxBoxSizer(wxVERTICAL);

	m_textCtrlName = new wxTextCtrl(this, wxID_ANY, wxEmptyString, wxDefaultPosition, wxDefaultSize, 0);
	m_textCtrlName->SetMinSize(wxSize(-1, 20));
	m_textCtrlName->SetMaxSize(wxSize(-1, 20));

	bSizer6->Add(m_textCtrlName, 0, wxALL | wxEXPAND, 5);

	wxBoxSizer* bSizer12;
	bSizer12 = new wxBoxSizer(wxHORIZONTAL);

	m_textCtrlPath = new wxTextCtrl(this, wxID_ANY, wxEmptyString, wxDefaultPosition, wxDefaultSize, 0);
	m_textCtrlPath->SetMaxSize(wxSize(-1, 20));

	bSizer12->Add(m_textCtrlPath, 1, wxEXPAND | wxTOP | wxBOTTOM | wxLEFT, 5);

	m_buttonSelect = new wxButton(this, wxID_ANY, _("..."), wxDefaultPosition, wxDefaultSize, wxBORDER_NONE | wxBU_EXACTFIT);
	m_buttonSelect->SetMinSize(wxSize(-1, 21));
	m_buttonSelect->SetMaxSize(wxSize(-1, 21));

	m_buttonSelect->Bind(wxEVT_BUTTON, &CSelectIBWnd::OnSelectButton, this);

	bSizer12->Add(m_buttonSelect, 0, wxTOP | wxBOTTOM | wxRIGHT | wxEXPAND, 5);
	bSizer6->Add(bSizer12, 1, wxEXPAND, 5);
	mainSizer->Add(bSizer6, 1, wxEXPAND, 5);

	wxBoxSizer* bSizer9;
	bSizer9 = new wxBoxSizer(wxVERTICAL);

	m_buttonOk = new wxButton(this, wxID_OK);
	m_buttonOk->SetMinSize(wxSize(-1, 21));

	bSizer9->Add(m_buttonOk, 0, wxALL, 4);
	bSizer9->Add(0, 0, 1, wxEXPAND, 5);

	m_buttonCancel = new wxButton(this, wxID_CANCEL);
	m_buttonCancel->SetMinSize(wxSize(-1, 21));

	bSizer9->Add(m_buttonCancel, 0, wxALL | wxEXPAND, 4);
	mainSizer->Add(bSizer9, 0, wxEXPAND, 5);
	bSizer13->Add(mainSizer, 0, wxEXPAND, 5);

	this->SetSizer(bSizer13);
	this->Layout();

	this->Centre(wxBOTH);
}

CSelectIBWnd::~CSelectIBWnd()
{
}

#include <wx/dirdlg.h>

void CSelectIBWnd::OnSelectButton(wxCommandEvent &event) {

	wxDirDialog dirDialog(this, _("Select IB path"));
	if (dirDialog.ShowModal() == wxID_CANCEL)
		return;     // the user changed idea...

	m_textCtrlPath->SetValue(dirDialog.GetPath());
	event.Skip();
}