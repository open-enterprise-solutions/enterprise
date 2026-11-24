#include "field.h"
#include "core/serialize/dataBuilder.h"   // ibDataNode — the state written, an event's arguments

//****************************************************************************
//*                                 Label                                    *
//****************************************************************************

void ibFieldViewLabel::OnUpdate(ibDataNode& state, ibVisualHost* host)
{
	// UNBOUND, THE TITLE IS THE WHOLE LABEL — no value text at all: the client shows no value half, and the title stays
	// out of the form's title column (the static text's rule).
	ibValueField* const field = GetField();
	if (field->HasValueInControl())
		return;

	// Bound, the value's presentation sits beside the title, a link where there is something to open: an empty value
	// is plain text, because a link that leads nowhere is worse than no link.
	ibValue value;
	const bool read = field->GetControlValue(value);
	state.SetValue(ibProtocol::ibForm::ibState::Text, read ? value.GetString() : ibString());
	state.SetValue(ibProtocol::ibForm::ibState::Link, read && !value.IsEmpty());
}

bool ibFieldViewLabel::OnClientEvent(ibProtocol::ibEvent::ibKind event, const ibDataNode& args)
{
	if (event != ibProtocol::ibEvent::ibKind::Open)
		return false;

	// The text field's Opening, with the same standard processing: an application that means to open something else —
	// or nothing — says so there.
	ibValueField* const field = GetField();
	ibValue standardProcessing = true;
	field->CallAsEvent(m_eventOpening, field->GetValue(), standardProcessing);
	if (!standardProcessing.GetBoolean() || field->HasValueInControl())
		return true;

	// OPEN THE VALUE — which window opens is the value's own answer (a reference its card, a schedule its editor).
	ibValue value;
	if (field->GetControlValue(value) && !value.IsEmpty())
		value.ShowValue();
	return true;
}

//*******************************************************************
//*                              Data	                            *
//*******************************************************************

bool ibFieldViewLabel::ReadData(const ibDataNode& node)
{
	m_eventOpening->SetNodeValue(node.GetProperty(m_eventOpening->GetName()));
	return true;
}

bool ibFieldViewLabel::WriteData(ibDataNode& node) const
{
	node.SetProperty(m_eventOpening->GetName(), m_eventOpening->GetNodeValue());
	return true;
}
