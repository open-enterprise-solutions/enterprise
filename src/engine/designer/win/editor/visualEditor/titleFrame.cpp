////////////////////////////////////////////////////////////////////////////
//	Author		: Maxim Kornienko, wxFormBuilder community
//	Description : title window
////////////////////////////////////////////////////////////////////////////

#include "titleFrame.h"

CPanelTitle::CPanelTitle(wxWindow *parent, const wxString &title) : wxPanel(parent, wxID_ANY)
{
	wxBoxSizer* sizer = new wxBoxSizer(wxVERTICAL);

	wxStaticText *text = new wxStaticText(this, wxID_ANY, title);
	SetBackgroundColour(wxSystemSettings::GetColour(wxSYS_COLOUR_ACTIVECAPTION));
	text->SetBackgroundColour(wxSystemSettings::GetColour(wxSYS_COLOUR_ACTIVECAPTION));
	text->SetForegroundColour(wxSystemSettings::GetColour(wxSYS_COLOUR_CAPTIONTEXT));
	text->SetFont(
		wxFont(8, wxFONTFAMILY_SWISS, wxFONTSTYLE_NORMAL, wxFONTWEIGHT_BOLD, false, wxString()));

	sizer->Add(text, 0, wxALL | wxEXPAND, 2);
	SetSizer(sizer);
	Fit();
}

wxWindow * CPanelTitle::CreateTitle(wxWindow *inner, const wxString &title)
{
	wxWindow *parent = inner->GetParent();

	wxPanel *container = new wxPanel(parent, wxID_ANY);
	CPanelTitle *titleWin = new CPanelTitle(container, title);
	inner->Reparent(container);

	wxBoxSizer *sizer = new wxBoxSizer(wxVERTICAL);
	sizer->Add(titleWin, 0, wxEXPAND);
	sizer->Add(inner, 1, wxEXPAND);
	container->SetSizer(sizer);

	return container;
}
