////////////////////////////////////////////////////////////////////////////
//	Author		: Maxim Kornienko
//	Description : a line of a view
////////////////////////////////////////////////////////////////////////////

#include "widgets.h"

#include "gui/univ/ctrl/staticLine.h"

void ibValueStaticLine::Create(wxWindow* parent, const ibProtocolNode& node)
{
	m_staticLine = new ibStaticLine(parent, wxID_ANY, wxDefaultPosition, wxDefaultSize,
		static_cast<long>(node.GetInt(ibProtocol::ibForm::ibProperty::Orient, wxLI_HORIZONTAL)));
}

void ibValueStaticLine::Update(const ibProtocolNode& node)
{
	UpdateWindow(m_staticLine, node);
}

wxWindow* ibValueStaticLine::GetWindow() const
{
	return m_staticLine;
}

CONTROL_REGISTER(ibValueStaticLine, ibProtocol::ibForm::ibType::Staticline)
