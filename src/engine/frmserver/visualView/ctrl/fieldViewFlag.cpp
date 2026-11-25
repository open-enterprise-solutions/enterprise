#include "field.h"
#include "core/serialize/dataBuilder.h"   // ibDataNode — the state written, an event's arguments
#include "backend/appData.h"

//****************************************************************************
//*                                 Flag                                     *
//****************************************************************************

void ibFieldViewFlag::OnUpdate(ibDataNode& state, ibVisualHost* host)
{
	// The value's truth, read through the binding. Not in the studio: there the box shows no value.
	if (appData->StudioMode())
		return;

	ibValue value;
	GetField()->GetControlValue(value);
	state.SetValue(ibProtocol::ibForm::ibState::Checked, value.GetBoolean());
}

bool ibFieldViewFlag::OnClientEvent(ibProtocol::ibEvent::ibKind event, const ibDataNode& args)
{
	if (event != ibProtocol::ibEvent::ibKind::Toggle)
		return false;

	// A read-only field takes no toggle: nothing is written and no event is raised — the next Update shows the value
	// unchanged.
	ibValueField* const field = GetField();
	if (field->IsReadOnly())
		return true;

	// The state the person toggled it to, written as a Boolean — the source's type makes of it what it holds.
	field->SetControlValue(ibValue(args.GetValue<bool>(ibProtocol::ibEvent::Checked)));
	field->CallAsEvent(m_eventOnChange, field->GetValue());
	return true;
}

//*******************************************************************
//*                              Data	                            *
//*******************************************************************

bool ibFieldViewFlag::ReadData(const ibDataNode& node)
{
	m_eventOnChange->SetNodeValue(node.GetProperty(m_eventOnChange->GetName()));
	return true;
}

bool ibFieldViewFlag::WriteData(ibDataNode& node) const
{
	node.SetProperty(m_eventOnChange->GetName(), m_eventOnChange->GetNodeValue());
	return true;
}
