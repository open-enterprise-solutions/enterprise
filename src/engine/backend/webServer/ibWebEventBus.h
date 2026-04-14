////////////////////////////////////////////////////////////////////////////
//	Author		: Tetracode Dev
//	Description : OES Web Event Bus — broadcast SSE events to subscribers
//	              Thread-safe singleton. Handlers MUST be fast (no blocking).
////////////////////////////////////////////////////////////////////////////

#ifndef _IB_WEB_EVENT_BUS_H__
#define _IB_WEB_EVENT_BUS_H__

#include "backend.h"

#include <functional>
#include <map>
#include <mutex>
#include <string>

#include <json.hpp>

using json = nlohmann::json;

class BACKEND_API ibWebEventBus {
public:

	static ibWebEventBus* Get();
	static void           Reset(); // destroy singleton (call on server Stop)

	// Publish an event to every active subscriber.
	// eventType: "data_changed" | "notification" | "heartbeat" | custom
	// data:      arbitrary JSON payload
	// Thread-safe. Returns immediately after dispatching to all callbacks.
	void Publish(const std::string& eventType, const json& data);

	// Register a callback; returns a stable subscription id.
	// The callback is called on the publishing thread — keep it fast.
	int Subscribe(std::function<void(const std::string&, const json&)> callback);

	// Remove a previously registered callback by its id.
	// Safe to call from inside the callback itself (deferred removal).
	void Unsubscribe(int id);

private:

	ibWebEventBus() = default;
	~ibWebEventBus() = default;

	// No copies
	ibWebEventBus(const ibWebEventBus&)            = delete;
	ibWebEventBus& operator=(const ibWebEventBus&) = delete;

	std::mutex m_mutex;
	std::map<int, std::function<void(const std::string&, const json&)>> m_subscribers;
	int m_nextId = 0;

	// Ids collected during Publish() for deferred removal
	std::vector<int> m_pendingUnsubscribe;
	bool             m_dispatching = false;

	static ibWebEventBus* ms_instance;
};

#define webEventBus (ibWebEventBus::Get())

#endif // _IB_WEB_EVENT_BUS_H__
