////////////////////////////////////////////////////////////////////////////
//	Author		: Maxim Kornienko
//	Description : a field of a view - how far a number has come (Gauge)
////////////////////////////////////////////////////////////////////////////

#include "field.h"

#include "deskgui/univ/ctrl/gauge.h"

void ibFieldViewGauge::Create(wxWindow* parent, const ibProtocolNode& node)
{
	m_gauge = new ibGauge(parent, wxID_ANY, static_cast<int>(node.GetInt(ibProtocol::ibForm::ibProperty::Range, 100)),
		wxDefaultPosition, wxDefaultSize, static_cast<long>(node.GetInt(ibProtocol::ibForm::ibProperty::Orient, wxHORIZONTAL)));
}

void ibFieldViewGauge::Update(const ibProtocolNode& node)
{
	const ibProtocolNode state = node.FindChild(ibProtocol::ibForm::ibState::State);

	// The way it runs, turned where it stands: the gauge reads it from its style as it paints and measures.
	const long orient = static_cast<long>(node.GetInt(ibProtocol::ibForm::ibProperty::Orient, wxHORIZONTAL));
	const long style = (m_gauge->GetWindowStyleFlag() & ~(wxGA_HORIZONTAL | wxGA_VERTICAL)) | orient;
	if (style != m_gauge->GetWindowStyleFlag()) {
		m_gauge->SetWindowStyleFlag(style);
		m_gauge->InvalidateBestSize();
	}

	m_gauge->SetRange(static_cast<int>(node.GetInt(ibProtocol::ibForm::ibProperty::Range, 100)));
	// The value — none in the studio.
	if (state.Has(ibProtocol::ibForm::ibState::Position))
		m_gauge->SetValue(static_cast<int>(state.GetInt(ibProtocol::ibForm::ibState::Position)));
}

ibControl* ibFieldViewGauge::GetControl() const
{
	return m_gauge;
}
