////////////////////////////////////////////////////////////////////////////
//	Author		: Maxim Kornienko, wxFormBuilder community
//	Description : title window
////////////////////////////////////////////////////////////////////////////

#include "titleFrame.h"

ibPanelTitle::ibPanelTitle(wxWindow *parent, const wxString &title) : wxPanel(parent, wxID_ANY)
{
	wxBoxSizer* sizer = new wxBoxSizer(wxVERTICAL);

	wxStaticText *text = new wxStaticText(this, wxID_ANY, title);
	// Interior palette caption — deep dusty blue + white text. Was
	// system ACTIVECAPTION (Windows-blue) which clashed with palette.
	SetBackgroundColour(wxColour(0x3F, 0x5C, 0x77));   // #3F5C77 deep dusty blue
	text->SetBackgroundColour(wxColour(0x3F, 0x5C, 0x77));
	text->SetForegroundColour(*wxWHITE);
	text->SetFont(
		wxFont(8, wxFONTFAMILY_SWISS, wxFONTSTYLE_NORMAL, wxFONTWEIGHT_BOLD, false, wxString()));

	sizer->Add(text, 0, wxALL | wxEXPAND, 2);
	SetSizer(sizer);
	Fit();
}

wxWindow * ibPanelTitle::CreateTitle(wxWindow *inner, const wxString &title)
{
	wxWindow *parent = inner->GetParent();

	wxPanel *container = new wxPanel(parent, wxID_ANY);
	ibPanelTitle *titleWin = new ibPanelTitle(container, title);
	inner->Reparent(container);

	wxBoxSizer *sizer = new wxBoxSizer(wxVERTICAL);
	sizer->Add(titleWin, 0, wxEXPAND);
	sizer->Add(inner, 1, wxEXPAND);
	container->SetSizer(sizer);

	return container;
}
