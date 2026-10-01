////////////////////////////////////////////////////////////////////////////
//	Description : the history and the listeners for what the platform says
////////////////////////////////////////////////////////////////////////////

#include "mcp/mcpDesignerMessages.h"

#include <algorithm>

namespace {

// One designer, one window, one record. Function-local so it is built on first
// use — the window reports before anything here would have been constructed at
// namespace scope.
std::vector<ibDesignerMessages::Message>& Store()
{
	static std::vector<ibDesignerMessages::Message> s_messages;
	return s_messages;
}

std::vector<ibDesignerMessages::Listener*>& Listeners()
{
	static std::vector<ibDesignerMessages::Listener*> s_listeners;
	return s_listeners;
}

} // namespace

void ibDesignerMessages::Report(const Message& message)
{
	std::vector<Message>& store = Store();
	store.push_back(message);

	// Oldest out first, at a size nobody will notice and nobody will read past.
	// A designer session runs for hours.
	if (store.size() > 500)
		store.erase(store.begin(), store.begin() + 100);

	// KEPT FIRST, TOLD SECOND: a listener that turns round and asks for the
	// history sees the message it was just told about.
	for (Listener* listener : Listeners()) {
		if (listener != nullptr)
			listener->OnMessage(message);
	}
}

const std::vector<ibDesignerMessages::Message>& ibDesignerMessages::All()
{
	return Store();
}

void ibDesignerMessages::Clear()
{
	Store().clear();
}

void ibDesignerMessages::AddListener(Listener* listener)
{
	if (listener == nullptr)
		return;

	// Twice on the list would deliver every message twice, which reads downstream
	// as the platform having refused twice.
	std::vector<Listener*>& listeners = Listeners();
	if (std::find(listeners.begin(), listeners.end(), listener) == listeners.end())
		listeners.push_back(listener);
}

void ibDesignerMessages::RemoveListener(Listener* listener)
{
	std::vector<Listener*>& listeners = Listeners();
	listeners.erase(
		std::remove(listeners.begin(), listeners.end(), listener),
		listeners.end());
}
