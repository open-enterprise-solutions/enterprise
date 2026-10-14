////////////////////////////////////////////////////////////////////////////
//	Author		: Maxim Kornienko, wxFormBuilder
//	Description : a gauge of a view
////////////////////////////////////////////////////////////////////////////

#include "widgets.h"

void ibValueGauge::Create(wxWindow* parent, const ibProtocolNode& node)
{
	m_gauge = new wxGauge(parent, wxID_ANY,
		static_cast<int>(node.GetInt(ibProtocolName::Range, 100)),
		wxDefaultPosition,
		wxDefaultSize,
		static_cast<long>(node.GetInt(ibProtocolName::Orient, wxHORIZONTAL)));
}

void ibValueGauge::Update(const ibProtocolNode& node)
{
	m_gauge->SetRange(static_cast<int>(node.GetInt(ibProtocolName::Range, 100)));
	m_gauge->SetValue(static_cast<int>(node.GetInt(ibProtocolName::Value, 30)));

	UpdateWindow(m_gauge, node);
}
