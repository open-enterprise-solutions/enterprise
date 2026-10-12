////////////////////////////////////////////////////////////////////////////
//	Author		: Maxim Kornienko, wxFormBuilder
//	Description : an HTML box of a view
////////////////////////////////////////////////////////////////////////////

#include "widgets.h"

void ibValueHTMLBox::Create(wxWindow* parent, const ibProtocolNode& /*node*/)
{
	m_htmlBox = new wxHtmlWindow(parent, wxID_ANY, wxDefaultPosition, wxDefaultSize);
}

void ibValueHTMLBox::Update(const ibProtocolNode& node)
{
	const ibProtocolNode state = node.FindChild(ibProtocolName::State);

	// Set again, it scrolls back to the top — only another page is.
	const wxString page = state.GetString(ibProtocolName::Page);
	if (page != m_page) {
		m_page = page;
		m_htmlBox->SetPage(m_page);
	}

	UpdateWindow(m_htmlBox, node);
}

wxWindow* ibValueHTMLBox::GetWindow() const
{
	return m_htmlBox;
}
