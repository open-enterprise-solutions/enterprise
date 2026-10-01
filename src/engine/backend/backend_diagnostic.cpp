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
std::mutex& SinkMutex()
{
	static std::mutex mutex;
	return mutex;
}

std::vector<ibDiagnosticSink*>& Sinks()
{
	static std::vector<ibDiagnosticSink*> sinks;
	return sinks;
}

} // namespace

void ibDiagnostics::Subscribe(ibDiagnosticSink* sink)
{
	if (sink == nullptr)
		return;

	std::lock_guard<std::mutex> lock(SinkMutex());
	auto& sinks = Sinks();
	// Subscribing twice would deliver twice — refuse rather than duplicate.
	if (std::find(sinks.begin(), sinks.end(), sink) == sinks.end())
		sinks.push_back(sink);
}

void ibDiagnostics::Unsubscribe(ibDiagnosticSink* sink)
{
	if (sink == nullptr)
		return;

	std::lock_guard<std::mutex> lock(SinkMutex());
	auto& sinks = Sinks();
	sinks.erase(std::remove(sinks.begin(), sinks.end(), sink), sinks.end());
}

bool ibDiagnostics::HasSubscribers()
{
	std::lock_guard<std::mutex> lock(SinkMutex());
	return !Sinks().empty();
}

void ibDiagnostics::Publish(const ibDiagnostic& diagnostic)
{
	// COPIED OUT UNDER THE LOCK, delivered outside it. A sink is free to do
	// whatever it likes in OnDiagnostic — including, on a bad day, subscribing
	// or unsubscribing another one — and holding the mutex across that call
	// would deadlock on exactly the day nobody has time to debug it.
	std::vector<ibDiagnosticSink*> snapshot;
	{
		std::lock_guard<std::mutex> lock(SinkMutex());
		if (Sinks().empty())
			return;
		snapshot = Sinks();
	}

	for (ibDiagnosticSink* sink : snapshot) {
		// SWALLOWED DELIBERATELY. This runs inside the error path, usually with
		// an exception already in flight; a sink that throws must not replace
		// the failure being reported with its own.
		try { sink->OnDiagnostic(diagnostic); }
		catch (...) {}
	}
}
