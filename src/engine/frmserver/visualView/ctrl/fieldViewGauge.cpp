#include "field.h"
#include "core/serialize/dataBuilder.h"   // ibDataNode — the state written
#include "backend/appData.h"

#include <algorithm>   // std::clamp — the value held within the range

//****************************************************************************
//*                                 Gauge                                    *
//****************************************************************************

void ibFieldViewGauge::OnUpdate(ibDataNode& state, ibVisualHost* host)
{
	// The value, read through the binding. Not in the designer: there the gauge shows no value.
	if (appData->DesignerMode())
		return;

	ibValue value;
	GetField()->GetControlValue(value);
	const int range = std::max(m_propertyRange->GetValueAsInteger(), 0);
	state.SetValue(ibProtocol::ibForm::ibState::Position, std::clamp(value.GetNumber().ToInt(), 0, range));
}

//*******************************************************************
//*                              Data	                            *
//*******************************************************************

bool ibFieldViewGauge::ReadData(const ibDataNode& node)
{
	m_propertyRange->SetNodeValue(node.GetProperty(m_propertyRange->GetName()));
	m_propertyOrient->SetNodeValue(node.GetProperty(m_propertyOrient->GetName()));
	return true;
}

bool ibFieldViewGauge::WriteData(ibDataNode& node) const
{
	node.SetProperty(m_propertyRange->GetName(), m_propertyRange->GetNodeValue());
	node.SetProperty(m_propertyOrient->GetName(), m_propertyOrient->GetNodeValue());
	return true;
}
