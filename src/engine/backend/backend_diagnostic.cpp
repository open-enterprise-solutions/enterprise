////////////////////////////////////////////////////////////////////////////
//	Description : the diagnostic sink — subscribers, and publishing to them
////////////////////////////////////////////////////////////////////////////

#include "backend/backend_diagnostic.h"

#include <algorithm>
#include <mutex>
#include <vector>

namespace {

// A PLAIN VECTOR behind a mutex, not a lock-free anything: subscriptions happen
// a handful of times per process (a plugin loads, a check starts), while
// publishing happens only when something has already failed. Neither is a hot
// path, and the simplest correct thing is the right one here.
//
// ⚠ THE LIST CAN GO FIRST. It is made at the first subscription, so at exit it is
// destroyed before what was made earlier and unsubscribes on its way out — the
// application host, whose plugins sign off as it goes (2026-10-27: "vector erase
// iterator outside range" at the exit of every test that loaded a plugin). Past
// that there is nobody to deliver to, and every call does nothing; the flag that
// says so is constant-initialised and never destroyed.
bool s_sinkListGone = false;

struct ibSinkList {
	std::mutex                     m_mutex;
	std::vector<ibDiagnosticSink*> m_sinks;
	~ibSinkList() { s_sinkListGone = true; }
};

ibSinkList* SinkList()
{
	if (s_sinkListGone)
		return nullptr;
	static ibSinkList list;
	return &list;
}

} // namespace

void ibDiagnostics::Subscribe(ibDiagnosticSink* sink)
{
	ibSinkList* const list = SinkList();
	if (sink == nullptr || list == nullptr)
		return;

	std::lock_guard<std::mutex> lock(list->m_mutex);
	auto& sinks = list->m_sinks;
	// Subscribing twice would deliver twice — refuse rather than duplicate.
	if (std::find(sinks.begin(), sinks.end(), sink) == sinks.end())
		sinks.push_back(sink);
}

void ibDiagnostics::Unsubscribe(ibDiagnosticSink* sink)
{
	ibSinkList* const list = SinkList();
	if (sink == nullptr || list == nullptr)
		return;

	std::lock_guard<std::mutex> lock(list->m_mutex);
	auto& sinks = list->m_sinks;
	sinks.erase(std::remove(sinks.begin(), sinks.end(), sink), sinks.end());
}

bool ibDiagnostics::HasSubscribers()
{
	ibSinkList* const list = SinkList();
	if (list == nullptr)
		return false;
	std::lock_guard<std::mutex> lock(list->m_mutex);
	return !list->m_sinks.empty();
}

void ibDiagnostics::Publish(const ibDiagnostic& diagnostic)
{
	// COPIED OUT UNDER THE LOCK, delivered outside it. A sink is free to do
	// whatever it likes in OnDiagnostic — including, on a bad day, subscribing
	// or unsubscribing another one — and holding the mutex across that call
	// would deadlock on exactly the day nobody has time to debug it.
	ibSinkList* const list = SinkList();
	if (list == nullptr)
		return;
	std::vector<ibDiagnosticSink*> snapshot;
	{
		std::lock_guard<std::mutex> lock(list->m_mutex);
		if (list->m_sinks.empty())
			return;
		snapshot = list->m_sinks;
	}

	for (ibDiagnosticSink* sink : snapshot) {
		// SWALLOWED DELIBERATELY. This runs inside the error path, usually with
		// an exception already in flight; a sink that throws must not replace
		// the failure being reported with its own.
		try { sink->OnDiagnostic(diagnostic); }
		catch (...) {}
	}
}
