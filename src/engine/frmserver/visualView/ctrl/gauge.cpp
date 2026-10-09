
#include "widgets.h"
#include "core/serialize/dataBuilder.h"   // ibDataNode (control -> node)
#include "backend/compiler/procUnit.h"


//****************************************************************************
//*                             Gauge                                        *
//****************************************************************************

ibValueGauge::ibValueGauge() : ibValueWindow()
{
}

//*******************************************************************
//*								Data                                *
//*******************************************************************

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

//***********************************************************************
//*                       Register in runtime                           *
//***********************************************************************

CONTROL_TYPE_REGISTER(ibValueGauge, "Gauge", "Widget", control_to_clsid("CT_GAUG"));
