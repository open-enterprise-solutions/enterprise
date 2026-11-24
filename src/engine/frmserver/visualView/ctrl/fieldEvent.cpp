#include "field.h"
#include "core/serialize/dataBuilder.h"   // ibDataNode — an event's arguments

//*******************************************************************
//*                             Events                              *
//*******************************************************************

// What the person did in the field is the view's to handle: typing into an Input, toggling a Flag, opening a Label's
// link, picking a Switch's item. What the view does not take, the window does.
bool ibValueField::OnClientEvent(ibProtocol::ibEvent::ibKind event, const ibDataNode& args)
{
	return m_provider->OnClientEvent(event, args) || ibValueWindow::OnClientEvent(event, args);
}

bool ibValueField::IsSubscribed(ibProtocol::ibEvent::ibKind event) const
{
	return m_provider->IsSubscribed(event) || ibValueWindow::IsSubscribed(event);
}
