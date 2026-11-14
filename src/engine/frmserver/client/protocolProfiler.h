#ifndef __PROTOCOL_PROFILER_H__
#define __PROTOCOL_PROFILER_H__

// ⭐ THE PERSON'S MEASUREMENT WHERE CLIENTS ARE ANSWERED — the lines (backend/debugger/runtimeProfiler.h), and over them
// the clients' calls that ran them: a call by its method — how often, how long, the bytes of its requests and of its
// answers — with the lines its session ran while it was open, the platform's own work among them. It lives where the
// calls are answered, beside the client host (Max, 2026-11-14: *"the protocol profiler, by the idea, lives on the forms
// server"*); the debug server makes it between ProfileStart and ProfileStop as this library said once as it loaded
// (ibDebuggerServer::SetProfileMaker, the bottom of the .cpp). A run of code_run {profile} measures the lines alone.
//
// A CLIENT'S CALLS COME ONE AT A TIME, so what its session runs while one is open is that call's: a line asks which call
// is open of the session its fiber is leasing (ibWorkerPoolHeadless::CurrentLease). A call made while another is open
// takes the lines until it is answered. A client's debugger's and assistant's calls are not told (ibClientHost::Call) —
// they come beside its calls, not as one of them.
//
// THE PLACE IS THE METHOD'S TEXT, interned once for the process — its address. So what a line asks (PlaceNow) is never
// this object's: a line of another session told while it is being made, or gone, reaches nothing of it.

#include <cstddef>
#include <cstdint>
#include <map>
#include <mutex>
#include <set>
#include <unordered_map>
#include <vector>

#include "backend/debugger/runtimeProfiler.h"

class ibProtocolProfiler final : public ibRuntimeProfiler {
public:

	ibProtocolProfiler();
	virtual ~ibProtocolProfiler() override;

	// What it measured so far: the calls by their time descending, each with the lines it ran, and the lines of no call.
	virtual ibProfileData GetProfile() const override;

	// A client's call made and answered, in pairs — told by the client host while a measurement runs (IsRunning).
	static void EnterCall(const class ibSession* session, const ibString& method);
	static void LeaveCall(const class ibSession* session, std::size_t requestBytes, std::size_t answerBytes);

private:

	// A method's sums.
	struct ibCall {
		std::uint64_t     m_count = 0;
		ibClock::duration m_total{};
		std::uint64_t     m_requestBytes = 0;
		std::uint64_t     m_answerBytes = 0;
	};

	// A call open: its method's interned text (s_methods), and since when.
	struct ibOpen {
		const ibString*     m_method = nullptr;
		ibClock::time_point m_begun;
	};

	// The call open of the session the fiber is leasing — none: the work is no client's.
	virtual const void* PlaceNow() override;

	// The calls open by session, the methods' texts and the measurements told, under one lock: taken inside the lines'
	// and the queries' (PlaceNow), never around them.
	static std::mutex                                                     s_mutex;
	static std::vector<ibProtocolProfiler*>                               s_profiles;
	static std::unordered_map<const class ibSession*, std::vector<ibOpen>> s_open;
	static std::set<ibString>                                             s_methods;

	std::map<const ibString*, ibCall> m_calls;   // by the method's interned text — the place its lines went under
};

#endif
