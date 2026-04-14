////////////////////////////////////////////////////////////////////////////
//	Author		: Tetracode Dev
//	Description : OES Web Event Bus — implementation
////////////////////////////////////////////////////////////////////////////

#include "ibWebEventBus.h"

ibWebEventBus* ibWebEventBus::ms_instance = nullptr;

ibWebEventBus* ibWebEventBus::Get()
{
	if (ms_instance == nullptr)
		ms_instance = new ibWebEventBus();
	return ms_instance;
}

void ibWebEventBus::Reset()
{
	if (ms_instance != nullptr) {
		delete ms_instance;
		ms_instance = nullptr;
	}
}

// ---------------------------------------------------------------------------

int ibWebEventBus::Subscribe(
	std::function<void(const std::string&, const json&)> callback)
{
	std::lock_guard<std::mutex> lock(m_mutex);
	const int id = m_nextId++;
	m_subscribers.emplace(id, std::move(callback));
	return id;
}

void ibWebEventBus::Unsubscribe(int id)
{
	std::lock_guard<std::mutex> lock(m_mutex);

	if (m_dispatching) {
		// Defer the actual erase until Publish() unwinds
		m_pendingUnsubscribe.push_back(id);
	}
	else {
		m_subscribers.erase(id);
	}
}

void ibWebEventBus::Publish(const std::string& eventType, const json& data)
{
	// Take a snapshot of current subscribers so new Subscribe()/Unsubscribe()
	// calls during dispatch don't race with the iteration.
	std::vector<std::pair<int, std::function<void(const std::string&, const json&)>>>
		snapshot;

	{
		std::lock_guard<std::mutex> lock(m_mutex);
		m_dispatching = true;
		snapshot.reserve(m_subscribers.size());
		for (const auto& [id, cb] : m_subscribers)
			snapshot.emplace_back(id, cb);
	}

	// Dispatch outside the lock so callbacks can safely call Unsubscribe()
	for (auto& [id, cb] : snapshot) {
		try {
			cb(eventType, data);
		}
		catch (...) {
			// Never let a subscriber crash the publisher.
		}
	}

	// Flush deferred removals
	{
		std::lock_guard<std::mutex> lock(m_mutex);
		m_dispatching = false;
		for (int id : m_pendingUnsubscribe)
			m_subscribers.erase(id);
		m_pendingUnsubscribe.clear();
	}
}
