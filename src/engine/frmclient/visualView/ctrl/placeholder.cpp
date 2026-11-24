////////////////////////////////////////////////////////////////////////////
//	Author		: Maxim Kornienko
//	Description : a control this client does not draw yet
////////////////////////////////////////////////////////////////////////////

#include "widgets.h"

#include "deskgui/univ/ctrl/staticText.h"
#include "deskgui/univ/theme.h"          // ibThemeEngine — the note's grey is the theme's
#include "deskgui/univ/colourScheme.h"   // wxTHEME_COLOUR

void ibViewPlaceholder::Create(wxWindow* parent, const ibProtocolNode& /*node*/)
{
	m_panel = new ibPanel(parent, wxID_ANY);

	ibStaticText* const text = new ibStaticText(m_panel, wxID_ANY,
		ibString::Format(_("The control type \"%s\" is not registered in this client yet."), GetClassName()),
		wxDefaultPosition, wxDefaultSize, wxALIGN_CENTRE_HORIZONTAL);
	text->SetForegroundColour(wxTHEME_COLOUR(CONTROL_TEXT_DISABLED));

	// In the middle of the place the control would take.
	wxBoxSizer* const sizer = new wxBoxSizer(wxVERTICAL);
	sizer->AddStretchSpacer();
	sizer->Add(text, 0, wxALIGN_CENTRE_HORIZONTAL | wxALL, m_panel->FromDIP(8));
	sizer->AddStretchSpacer();
	m_panel->SetSizer(sizer);
}

void ibViewPlaceholder::Update(const ibProtocolNode& node)
{
	UpdateWindow(m_panel, node);
}
