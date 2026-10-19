////////////////////////////////////////////////////////////////////////////
//	Author		: Maxim Kornienko
//	Description : a caption over a tree of the form editor
////////////////////////////////////////////////////////////////////////////

#include "titleFrame.h"

#include <wx/sizer.h>
#include <wx/stattext.h>

ibPanelTitle::ibPanelTitle(wxWindow* parent, const wxString& title) : wxPanel(parent, wxID_ANY)
{
	// Deep dusty blue #3F5C77 from the palette, not the system's caption blue that clashed with it; the
	// label made after it inherits it.
	SetBackgroundColour(wxColour(0x3F, 0x5C, 0x77));

	wxStaticText* const label = new wxStaticText(this, wxID_ANY, title);
	label->SetForegroundColour(*wxWHITE);
	label->SetFont(wxFontInfo(8).Family(wxFONTFAMILY_SWISS).Bold());

	wxBoxSizer* const sizer = new wxBoxSizer(wxVERTICAL);
	sizer->Add(label, wxSizerFlags().Expand().Border(wxALL, 2));
	SetSizerAndFit(sizer);
}

wxWindow* ibPanelTitle::CreateTitle(wxWindow* inner, const wxString& title)
{
	wxPanel* const holder = new wxPanel(inner->GetParent(), wxID_ANY);
	inner->Reparent(holder);

	wxBoxSizer* const column = new wxBoxSizer(wxVERTICAL);
	column->Add(new ibPanelTitle(holder, title), wxSizerFlags().Expand());
	column->Add(inner, wxSizerFlags(1).Expand());
	holder->SetSizer(column);
	return holder;
}
