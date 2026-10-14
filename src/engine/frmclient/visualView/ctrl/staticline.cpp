////////////////////////////////////////////////////////////////////////////
//	Author		: Maxim Kornienko, wxFormBuilder
//	Description : a line of a view
////////////////////////////////////////////////////////////////////////////

#include "widgets.h"

void ibValueStaticLine::Create(wxWindow* parent, const ibProtocolNode& node)
{
	m_staticLine = new wxStaticLine(parent, wxID_ANY, wxDefaultPosition, wxDefaultSize,
		static_cast<long>(node.GetInt(ibProtocolName::Orient, wxLI_HORIZONTAL)));
}

void ibValueStaticLine::Update(const ibProtocolNode& node)
{
	UpdateWindow(m_staticLine, node);
}

