////////////////////////////////////////////////////////////////////////////
//	Author		: Maxim Kornienko
//	Description : a caption over a tree of the form editor
////////////////////////////////////////////////////////////////////////////

#include "titleFrame.h"

#include <wx/sizer.h>
#include <wx/settings.h>   // wxSystemSettings - the title's font is the system's
#include "deskgui/univ/ctrl/staticText.h"

ibPanelTitle::ibPanelTitle(wxWindow* parent, const wxString& title) : ibPanel(parent, wxID_ANY)
{
	// Deep dusty blue #3F5C77 from the palette, not the system's caption blue that clashed with it; the
	// label made after it inherits it.
	SetBackgroundColour(wxColour(0x3F, 0x5C, 0x77));

	ibStaticText* const label = new ibStaticText(this, wxID_ANY, title);
	label->SetForegroundColour(*wxWHITE);
	// the system's face, bold, a point under its size - 8 on Windows' 9 (the Swiss family's 8 stood here until
	// 2026-11-12 - Arial on Windows)
	wxFont font = wxSystemSettings::GetFont(wxSYS_DEFAULT_GUI_FONT).Bold();
	font.SetPointSize(font.GetPointSize() - 1);
	label->SetFont(font);

	wxBoxSizer* const sizer = new wxBoxSizer(wxVERTICAL);
	sizer->Add(label, wxSizerFlags().Expand().Border(wxALL, 2));
	SetSizerAndFit(sizer);
}

wxWindow* ibPanelTitle::CreateTitle(wxWindow* inner, const wxString& title)
{
	ibPanel* const holder = new ibPanel(inner->GetParent(), wxID_ANY);
	inner->Reparent(holder);

	wxBoxSizer* const column = new wxBoxSizer(wxVERTICAL);
	column->Add(new ibPanelTitle(holder, title), wxSizerFlags().Expand());
	column->Add(inner, wxSizerFlags(1).Expand());
	holder->SetSizer(column);
	return holder;
}
