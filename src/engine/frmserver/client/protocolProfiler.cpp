#include "protocolProfiler.h"

#include <algorithm>
#include <chrono>
#include <memory>

#include "backend/debugger/debugServer.h"         // SetProfileMaker — the person's measurement in this process
#include "backend/session/workerPoolHeadless.h"   // CurrentLease — whose work a line is

std::mutex                                                                     ibProtocolProfiler::s_mutex;
std::vector<ibProtocolProfiler*>                                               ibProtocolProfiler::s_profiles;
std::unordered_map<const ibSession*, std::vector<ibProtocolProfiler::ibOpen>> ibProtocolProfiler::s_open;
std::set<ibString>                                                             ibProtocolProfiler::s_methods;

ibProtocolProfiler::ibProtocolProfiler()
{
	const std::lock_guard<std::mutex> lock(s_mutex);
	s_profiles.push_back(this);
}

// Stopped first, while it is still this class: a line of another session told meanwhile asks its PlaceNow.
ibProtocolProfiler::~ibProtocolProfiler()
{
	Stop();

	const std::lock_guard<std::mutex> lock(s_mutex);
	s_profiles.erase(std::remove(s_profiles.begin(), s_profiles.end(), this), s_profiles.end());
}

// The lines are read first, under their own locks, and the calls after, under this one.
ibProfileData ibProtocolProfiler::GetProfile() const
{
	const auto microseconds = [](ibClock::duration spent) {
		return static_cast<std::int64_t>(std::chrono::duration_cast<std::chrono::microseconds>(spent).count());
	};

	std::map<const void*, std::vector<ibProfileData::ibProfileRow>> places = Places();

	ibProfileData profile;
	profile.m_duration = microseconds(ibClock::now() - m_begun);
	if (const auto found = places.find(nullptr); found != places.end()) {
		profile.m_rows = std::move(found->second);
		places.erase(found);
	}

	const std::lock_guard<std::mutex> lock(s_mutex);
	for (const auto& [method, sums] : m_calls) {
		ibProfileData::ibProfileCall call;
		call.m_method       = *method;
		call.m_count        = sums.m_count;
		call.m_total        = microseconds(sums.m_total);
		call.m_requestBytes = sums.m_requestBytes;
		call.m_answerBytes  = sums.m_answerBytes;
		if (const auto found = places.find(method); found != places.end()) {
			call.m_rows = std::move(found->second);
			places.erase(found);
		}
		profile.m_calls.push_back(std::move(call));
	}
	// …and a call still open: its lines so far, counted when it is answered.
	for (auto& [place, rows] : places) {
		ibProfileData::ibProfileCall call;
		call.m_method = *static_cast<const ibString*>(place);
		call.m_rows   = std::move(rows);
		profile.m_calls.push_back(std::move(call));
	}
	std::stable_sort(profile.m_calls.begin(), profile.m_calls.end(),
		[](const ibProfileData::ibProfileCall& a, const ibProfileData::ibProfileCall& b) { return a.m_total > b.m_total; });
	return profile;
}

// The moment is read before the lock is waited for: it is the call's, not the lock's.
void ibProtocolProfiler::EnterCall(const ibSession* session, const ibString& method)
{
	const ibClock::time_point now = ibClock::now();
	const std::lock_guard<std::mutex> lock(s_mutex);
	s_open[session].push_back(ibOpen{ &*s_methods.insert(method).first, now });
}

// Given to every measurement that runs — from its beginning, when it began while the call was open.
void ibProtocolProfiler::LeaveCall(const ibSession* session, std::size_t requestBytes, std::size_t answerBytes)
{
	const ibClock::time_point now = ibClock::now();
	const std::lock_guard<std::mutex> lock(s_mutex);
	const auto found = s_open.find(session);
	if (found == s_open.end())
		return;
	const ibOpen open = found->second.back();
	found->second.pop_back();
	if (found->second.empty())
		s_open.erase(found);

	for (ibProtocolProfiler* const profile : s_profiles) {
		ibCall& call = profile->m_calls[open.m_method];
		call.m_count        += 1;
		call.m_total        += now - std::max(open.m_begun, profile->m_begun);
		call.m_requestBytes += requestBytes;
		call.m_answerBytes  += answerBytes;
	}
}

// The lease is read without the pool's lock (CurrentLease): asked under the lines' lock, this one waits for nothing but
// its own.
const void* ibProtocolProfiler::PlaceNow()
{
	const ibSession* const session = ibWorkerPoolHeadless::CurrentLease();
	if (session == nullptr)
		return nullptr;

	const std::lock_guard<std::mutex> lock(s_mutex);
	const auto found = s_open.find(session);
	return found != s_open.end() ? static_cast<const void*>(found->second.back().m_method) : nullptr;
}

// ⭐ THE PERSON'S MEASUREMENT HERE MEASURES THE CLIENTS' CALLS TOO — said once, as this library loads.
[[maybe_unused]] static const bool s_profileMaker = (ibDebuggerServer::SetProfileMaker(
	[]() -> std::unique_ptr<ibRuntimeProfiler> { return std::make_unique<ibProtocolProfiler>(); }), true);
