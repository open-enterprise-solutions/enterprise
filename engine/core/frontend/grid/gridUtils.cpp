////////////////////////////////////////////////////////////////////////////
//	Author		: Maxim Kornienko
//	Description : input section window 
////////////////////////////////////////////////////////////////////////////

#include "gridUtils.h"

CInputSectionDialog::CInputSectionDialog(wxWindow* parent, wxWindowID id, const wxString& title, const wxPoint& pos, const wxSize& size, long style) : wxDialog(parent, id, title, pos, size, style)
{
	this->SetSizeHints(wxDefaultSize, wxDefaultSize);

	wxBoxSizer* bSizer1;
	bSizer1 = new wxBoxSizer(wxHORIZONTAL);

	m_section = new wxTextCtrl(this, wxID_ANY, wxEmptyString, wxDefaultPosition, wxDefaultSize, 0);
	bSizer1->Add(m_section, 1, wxALL, 5);

	m_sdbSizer = new wxStdDialogButtonSizer();
	m_sdbSizerOK = new wxButton(this, wxID_OK);
	m_sdbSizer->AddButton(m_sdbSizerOK);
	m_sdbSizerCancel = new wxButton(this, wxID_CANCEL);
	m_sdbSizer->AddButton(m_sdbSizerCancel);
	m_sdbSizer->Realize();

	bSizer1->Add(m_sdbSizer, 1, wxEXPAND, 5);

	this->SetSizer(bSizer1);
	this->Layout();
	bSizer1->Fit(this);

	this->Centre(wxBOTH);
}

CInputSectionDialog::~CInputSectionDialog()
{
}