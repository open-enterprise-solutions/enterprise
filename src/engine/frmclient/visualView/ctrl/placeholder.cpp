////////////////////////////////////////////////////////////////////////////
//	Author		: Maxim Kornienko
//	Description : a control this client does not draw yet
////////////////////////////////////////////////////////////////////////////

#include "widgets.h"

void ibViewPlaceholder::Create(wxWindow* parent, const ibProtocolNode& /*node*/)
{
	m_panel = new wxPanel(parent, wxID_ANY);

	wxStaticText* const text = new wxStaticText(m_panel, wxID_ANY,
		wxString::Format(_("The control type \"%s\" is not registered in this client yet."), m_type),
		wxDefaultPosition, wxDefaultSize, wxALIGN_CENTRE_HORIZONTAL);
	text->SetForegroundColour(wxSystemSettings::GetColour(wxSYS_COLOUR_GRAYTEXT));

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
