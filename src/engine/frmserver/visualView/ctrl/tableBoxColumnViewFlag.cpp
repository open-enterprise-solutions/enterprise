#include "tableBox.h"
#include "core/serialize/dataBuilder.h"   // ibDataNode — an event's arguments

//****************************************************************************
//*                                 Flag                                     *
//****************************************************************************

bool ibColumnViewFlag::OnClientEvent(ibProtocol::ibEvent::ibKind event, const ibDataNode& args)
{
	if (event != ibProtocol::ibEvent::ibKind::Toggle)
		return false;

	// A read-only column takes no toggle: nothing is written and no event is raised — the next fetch shows the cell as it
	// was.
	ibValueModelTableBoxColumn* const column = GetColumn();
	if (column->IsReadOnly())
		return true;

	// The state the person toggled it to, written as a Boolean into the current row's cell.
	column->SetControlValue(ibValue(args.GetValue<bool>(ibProtocol::ibEvent::Checked)));
	column->CallAsEvent(m_eventOnChange, column->GetValue());
	return true;
}

//*******************************************************************
//*                              Data	                            *
//*******************************************************************

bool ibColumnViewFlag::ReadData(const ibDataNode& node)
{
	m_eventOnChange->SetNodeValue(node.GetProperty(m_eventOnChange->GetName()));
	return true;
}

bool ibColumnViewFlag::WriteData(ibDataNode& node) const
{
	node.SetProperty(m_eventOnChange->GetName(), m_eventOnChange->GetNodeValue());
	return true;
}
