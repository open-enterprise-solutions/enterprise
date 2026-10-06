
#include "widgets.h"
#include "backend/serialize/dataBuilder.h"   // ibDataNode (control -> node)
#include "backend/compiler/procUnit.h"


//****************************************************************************
//*                             Slider                                       *
//****************************************************************************

ibValueSlider::ibValueSlider() : ibValueWindow()
{
}

//*******************************************************************
//*                           Property                              *
//*******************************************************************

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

//***********************************************************************
//*                       Register in runtime                           *
//***********************************************************************

CONTROL_TYPE_REGISTER(ibValueSlider, "Slider", "Widget", control_to_clsid("CT_SLID"));
