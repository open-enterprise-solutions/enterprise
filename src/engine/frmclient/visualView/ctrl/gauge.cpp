////////////////////////////////////////////////////////////////////////////
//	Author		: Maxim Kornienko
//	Description : a gauge of a view
////////////////////////////////////////////////////////////////////////////

#include "widgets.h"

#include "gui/univ/ctrl/gauge.h"

void ibValueGauge::Create(wxWindow* parent, const ibProtocolNode& node)
{
	m_gauge = new ibGauge(parent, wxID_ANY,
		static_cast<int>(node.GetInt(ibProtocol::ibForm::ibProperty::Range, 100)),
		wxDefaultPosition,
		wxDefaultSize,
		static_cast<long>(node.GetInt(ibProtocol::ibForm::ibProperty::Orient, wxHORIZONTAL)));
}

void ibValueGauge::Update(const ibProtocolNode& node)
{
	m_gauge->SetRange(static_cast<int>(node.GetInt(ibProtocol::ibForm::ibProperty::Range, 100)));
	m_gauge->SetValue(static_cast<int>(node.GetInt(ibProtocol::ibForm::ibProperty::Value, 30)));

	UpdateWindow(m_gauge, node);
}

wxWindow* ibValueGauge::GetWindow() const
{
	return m_gauge;
}

CONTROL_REGISTER(ibValueGauge, ibProtocol::ibForm::ibType::Gauge)
