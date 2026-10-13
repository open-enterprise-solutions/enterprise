////////////////////////////////////////////////////////////////////////////
//	Author		: Maxim Kornienko, wxFormBuilder
//	Description : a slider of a view
////////////////////////////////////////////////////////////////////////////

#include "widgets.h"
#include "core/serialize/dataBuilder.h"   // the properties, as the frame carries them

void ibValueSlider::Create(wxWindow* parent, const ibProtocolNode& node)
{
	m_slider = new wxSlider(parent, wxID_ANY,
		static_cast<int>(node.GetInt(ibProtocolName::Value, 50)),
		static_cast<int>(node.GetInt(ibProtocolName::MinValue, 0)),
		static_cast<int>(node.GetInt(ibProtocolName::MaxValue, 100)),
		wxDefaultPosition,
		wxDefaultSize,
		static_cast<long>(node.GetInt(ibProtocolName::Orient, wxHORIZONTAL)));
}

void ibValueSlider::Update(const ibProtocolNode& node)
{
	m_slider->SetRange(static_cast<int>(node.GetInt(ibProtocolName::MinValue, 0)),
		static_cast<int>(node.GetInt(ibProtocolName::MaxValue, 100)));
	m_slider->SetValue(static_cast<int>(node.GetInt(ibProtocolName::Value, 50)));

	UpdateWindow(m_slider, node);
}

//*******************************************************************************************
//*                  The properties — the desktop's ReadData / WriteData                    *
//*******************************************************************************************

bool ibValueSlider::ReadData(const ibDataNode& node)
{
	m_propertyMinValue->SetNodeValue(node.GetProperty(m_propertyMinValue->GetName()));
	m_propertyMaxValue->SetNodeValue(node.GetProperty(m_propertyMaxValue->GetName()));
	m_propertyValue->SetNodeValue(node.GetProperty(m_propertyValue->GetName()));
	m_propertyOrient->SetNodeValue(node.GetProperty(m_propertyOrient->GetName()));

	return ibValueWindow::ReadData(node);
}

bool ibValueSlider::WriteData(ibDataNode& node) const
{
	node.SetProperty(m_propertyMinValue->GetName(), m_propertyMinValue->GetNodeValue());
	node.SetProperty(m_propertyMaxValue->GetName(), m_propertyMaxValue->GetNodeValue());
	node.SetProperty(m_propertyValue->GetName(), m_propertyValue->GetNodeValue());
	node.SetProperty(m_propertyOrient->GetName(), m_propertyOrient->GetNodeValue());

	return ibValueWindow::WriteData(node);
}
