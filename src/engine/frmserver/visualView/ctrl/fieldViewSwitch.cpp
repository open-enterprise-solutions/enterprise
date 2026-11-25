#include "field.h"
#include "core/serialize/dataBuilder.h"     // ibDataNode — the state written, an event's arguments
#include "backend/appData.h"
#include "backend/backend_exception.h"      // ibCoreException — a value of a type the application lacks
#include "backend/metadataSerialization.h"  // ibDeserializeValue — an item's value, as its row stores it

//****************************************************************************
//*                                 Switch                                   *
//****************************************************************************

bool ibFieldViewSwitch::GetChoiceValue(size_t place, ibValue& value) const
{
	const std::vector<ibChoiceDescription>& rows = m_propertyChoices->GetValueAsChoicesDesc().m_rows;
	if (place >= rows.size())
		return false;

	try {
		value = ibDeserializeValue(rows[place].m_value, GetMetaData());
	}
	catch (const ibCoreException&) {
		return false;   // a type this application no longer has
	}
	return true;
}

void ibFieldViewSwitch::OnUpdate(ibDataNode& state, ibVisualHost* host)
{
	// The items, each its text, and the one the value stands on — none in the studio, where the field shows no value.
	const bool showValue = !appData->StudioMode();
	ibValue current;
	if (showValue)
		GetField()->GetControlValue(current);

	ibDataNode& choices = state.Child(ibProtocol::ibForm::ibState::Choices);
	const size_t count = m_propertyChoices->GetValueAsChoicesDesc().m_rows.size();
	for (size_t place = 0; place < count; place++) {
		ibValue value;
		const bool made = GetChoiceValue(place, value);

		ibDataNode& item = choices.AddChild(0, static_cast<ibMetaID>(place + 1));
		item.SetValue(ibProtocol::ibForm::ibState::Caption,
			ibChoiceCaption(m_propertyChoices->GetValueAsChoicesDesc().m_rows[place], GetMetaData()));
		if (showValue && made && value == current)
			item.SetValue(ibProtocol::ibForm::ibState::Selected, true);
	}

	// NO ITEMS — a form kept so, a field with no type: one still, saying the switch is empty. A switch of none has no
	// height, so neither its title nor anything beside it is drawn (Max, 2026-11-23); this one picks nothing (IsReadOnly).
	if (count == 0)
		choices.AddChild(0, 1).SetValue(ibProtocol::ibForm::ibState::Caption, ibString(_("<empty>")));
}

bool ibFieldViewSwitch::OnClientEvent(ibProtocol::ibEvent::ibKind event, const ibDataNode& args)
{
	if (event != ibProtocol::ibEvent::ibKind::Change || !args.Has(ibProtocol::ibEvent::Member))
		return false;

	// A read-only field takes no pick, and a place the switch does not have names nothing: nothing is written and no
	// event is raised — the next Update shows the item the value stands on. The pick is the switch's all the same, as a
	// flag's toggle is the flag's: the window does not get it.
	ibValueField* const field = GetField();
	if (field->IsReadOnly())
		return true;

	const std::int32_t place = args.GetValue<std::int32_t>(ibProtocol::ibEvent::Member);
	ibValue value;
	if (place < 0 || !GetChoiceValue(static_cast<size_t>(place), value))
		return true;

	field->SetControlValue(value);
	field->CallAsEvent(m_eventOnChange, field->GetValue());
	return true;
}

// THE FIELD'S SOURCE CHANGED — its type may be another: the items are set again, and the property keeps them of the
// field's type — those of a type the field has left are cleared (ibPropertyChoices::SetValue).
void ibFieldViewSwitch::OnViewChanged()
{
	m_propertyChoices->SetValue(m_propertyChoices->GetValueAsChoicesDesc());
}

//*******************************************************************
//*                              Data	                            *
//*******************************************************************

bool ibFieldViewSwitch::ReadData(const ibDataNode& node)
{
	m_propertyChoices->SetNodeValue(node.GetProperty(m_propertyChoices->GetName()));
	m_propertyOrient->SetNodeValue(node.GetProperty(m_propertyOrient->GetName()));

	//events
	m_eventOnChange->SetNodeValue(node.GetProperty(m_eventOnChange->GetName()));
	return true;
}

bool ibFieldViewSwitch::WriteData(ibDataNode& node) const
{
	node.SetProperty(m_propertyChoices->GetName(), m_propertyChoices->GetNodeValue());
	node.SetProperty(m_propertyOrient->GetName(), m_propertyOrient->GetNodeValue());

	//events
	node.SetProperty(m_eventOnChange->GetName(), m_eventOnChange->GetNodeValue());
	return true;
}
