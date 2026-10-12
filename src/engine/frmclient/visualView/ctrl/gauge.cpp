////////////////////////////////////////////////////////////////////////////
//	Author		: Maxim Kornienko, wxFormBuilder
//	Description : a gauge of a view
////////////////////////////////////////////////////////////////////////////

#include "widgets.h"
#include "core/serialize/dataBuilder.h"   // the properties, as the frame carries them

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

//*******************************************************************************************
//*                  The properties — the desktop's ReadData / WriteData                    *
//*******************************************************************************************

bool ibValueGauge::ReadData(const ibDataNode& node)
{
	m_propertyRange->SetNodeValue(node.GetProperty(m_propertyRange->GetName()));
	m_propertyValue->SetNodeValue(node.GetProperty(m_propertyValue->GetName()));
	m_propertyOrient->SetNodeValue(node.GetProperty(m_propertyOrient->GetName()));

	return ibValueWindow::ReadData(node);
}

bool ibValueGauge::WriteData(ibDataNode& node) const
{
	node.SetProperty(m_propertyRange->GetName(), m_propertyRange->GetNodeValue());
	node.SetProperty(m_propertyValue->GetName(), m_propertyValue->GetNodeValue());
	node.SetProperty(m_propertyOrient->GetName(), m_propertyOrient->GetNodeValue());

	return ibValueWindow::WriteData(node);
}
