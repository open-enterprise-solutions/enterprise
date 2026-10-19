////////////////////////////////////////////////////////////////////////////
//	Author		: Maxim Kornienko
//	Description : a slider of a view
////////////////////////////////////////////////////////////////////////////

#include "widgets.h"

void ibValueSlider::Create(wxWindow* parent, const ibProtocolNode& node)
{
	m_slider = new wxSlider(parent, wxID_ANY,
		static_cast<int>(node.GetInt(ibProtocol::ibForm::ibProperty::Value, 50)),
		static_cast<int>(node.GetInt(ibProtocol::ibForm::ibProperty::MinValue, 0)),
		static_cast<int>(node.GetInt(ibProtocol::ibForm::ibProperty::MaxValue, 100)),
		wxDefaultPosition,
		wxDefaultSize,
		static_cast<long>(node.GetInt(ibProtocol::ibForm::ibProperty::Orient, wxHORIZONTAL)));
}

void ibValueSlider::Update(const ibProtocolNode& node)
{
	m_slider->SetRange(static_cast<int>(node.GetInt(ibProtocol::ibForm::ibProperty::MinValue, 0)),
		static_cast<int>(node.GetInt(ibProtocol::ibForm::ibProperty::MaxValue, 100)));
	m_slider->SetValue(static_cast<int>(node.GetInt(ibProtocol::ibForm::ibProperty::Value, 50)));

	UpdateWindow(m_slider, node);
}
