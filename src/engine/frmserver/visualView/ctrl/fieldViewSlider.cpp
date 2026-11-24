#include "field.h"
#include "core/serialize/dataBuilder.h"   // ibDataNode — the state written, an event's arguments
#include "backend/appData.h"

#include <algorithm>   // std::clamp — the value held between the ends

//****************************************************************************
//*                                 Slider                                   *
//****************************************************************************

void ibFieldViewSlider::OnUpdate(ibDataNode& state, ibVisualHost* host)
{
	// The value, read through the binding. Not in the designer: there the thumb stands at the start.
	if (appData->DesignerMode())
		return;

	ibValue value;
	GetField()->GetControlValue(value);
	const int min = m_propertyMinValue->GetValueAsInteger();
	state.SetValue(ibProtocol::ibForm::ibState::Position,
		std::clamp(value.GetNumber().ToInt(), min, std::max(m_propertyMaxValue->GetValueAsInteger(), min)));
}

bool ibFieldViewSlider::OnClientEvent(ibProtocol::ibEvent::ibKind event, const ibDataNode& args)
{
	if (event != ibProtocol::ibEvent::ibKind::Change || !args.Has(ibProtocol::ibEvent::Position))
		return false;

	// A read-only field takes no move: nothing is written and no event is raised — the next Update puts the thumb back.
	ibValueField* const field = GetField();
	if (field->IsReadOnly())
		return true;

	// Where it was let go, held between the ends, written as a Number.
	const int min = m_propertyMinValue->GetValueAsInteger();
	const int position = std::clamp(args.GetValue<std::int32_t>(ibProtocol::ibEvent::Position), min,
		std::max(m_propertyMaxValue->GetValueAsInteger(), min));
	field->SetControlValue(ibValue(ibNumber(position)));
	field->CallAsEvent(m_eventOnChange, field->GetValue());
	return true;
}

//*******************************************************************
//*                              Data	                            *
//*******************************************************************

bool ibFieldViewSlider::ReadData(const ibDataNode& node)
{
	m_propertyMinValue->SetNodeValue(node.GetProperty(m_propertyMinValue->GetName()));
	m_propertyMaxValue->SetNodeValue(node.GetProperty(m_propertyMaxValue->GetName()));
	m_propertyOrient->SetNodeValue(node.GetProperty(m_propertyOrient->GetName()));
	m_eventOnChange->SetNodeValue(node.GetProperty(m_eventOnChange->GetName()));
	return true;
}

bool ibFieldViewSlider::WriteData(ibDataNode& node) const
{
	node.SetProperty(m_propertyMinValue->GetName(), m_propertyMinValue->GetNodeValue());
	node.SetProperty(m_propertyMaxValue->GetName(), m_propertyMaxValue->GetNodeValue());
	node.SetProperty(m_propertyOrient->GetName(), m_propertyOrient->GetNodeValue());
	node.SetProperty(m_eventOnChange->GetName(), m_eventOnChange->GetNodeValue());
	return true;
}
