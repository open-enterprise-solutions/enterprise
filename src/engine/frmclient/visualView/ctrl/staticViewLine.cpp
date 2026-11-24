////////////////////////////////////////////////////////////////////////////
//	Author		: Maxim Kornienko
//	Description : a static of a view - a line across or down (Line)
////////////////////////////////////////////////////////////////////////////

#include "static.h"

#include "deskgui/univ/ctrl/staticLine.h"

void ibStaticViewLine::Create(wxWindow* parent, const ibProtocolNode& node)
{
	m_staticLine = new ibStaticLine(parent, wxID_ANY, wxDefaultPosition, wxDefaultSize,
		static_cast<long>(node.GetInt(ibProtocol::ibForm::ibProperty::Orient, wxLI_HORIZONTAL)));
}

void ibStaticViewLine::Update(const ibProtocolNode& /*node*/)
{
	// Nothing of the State to show; its Orient is what it is built with — a line turned is built anew (BuiltWith).
}

wxWindow* ibStaticViewLine::GetWindow() const
{
	return m_staticLine;
}
