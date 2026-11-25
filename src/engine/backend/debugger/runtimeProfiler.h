#ifndef __IB_RUNTIME_PROFILER_H__
#define __IB_RUNTIME_PROFILER_H__

#include <atomic>
#include <chrono>
#include <cstdint>
#include <map>
#include <mutex>
#include <tuple>
#include <unordered_map>
#include <vector>

#include "backend/backend_core.h"
#include "backend/query/queryProfiler.h"   // ibQueryProfiler — the queries under its lines
#include "core/debugger/debugDefs.h"     // ibProfileData — what it measured

struct ibByteUnit;
struct ibProcUnitState;
struct ibRunContext;

// ⭐ A MEASUREMENT — which lines of which modules the runs spent their time on, from its making to its end (Max,
// 2026-11-14: *"the measurement must say which module, so a report can be built from it"*). It runs while it lives:
// made, it is told every frame and every line of the runs it watches; gone, it is told nothing. Of every session — the
// person's, started and taken by the debugger's commands (the studio's Debug menu, profile_start / profile_stop) and
// held by the debug server between them — or of one: a run of code_run {profile}, made around its code.
//
// THE INTERPRETER TELLS IT THREE THINGS, on the thread of the session they are of: a frame entered and left
// (ibProcStackGuard) and a line begun (EnterDebugger, where a new line is already noticed) — so lines are told while a
// studio is attached, which every road to a measurement is.
//
// What a line is given: SELF, its time without the calls it made; TOTAL, with them — a recursive line at its outermost
// occurrence only, the inner ones being inside it; COUNT, how often it began. A frame's first line takes the frame's
// entry from its start and its last line the return, so a frame's lines sum to the frame. The clock is the wall's: a
// line a session stands on — stopped at a breakpoint, waiting for the person's answer — is given the wait.
//
// …AND THE QUERIES THE LINE RAN (Max, 2026-11-14: *"a query and its own subtree within the profile"*): it holds a
// query measurement (backend/query/queryProfiler.h), whose work goes under the line its fiber is on — of no line, under
// the platform's own, a row of no module. Kept by the fiber (ibFiber::Current), not by the thread: a session's lease is
// a fiber, and two sessions on one thread suspend out of stack order — a thread's id would mix them.
//
// …AND A LINE GOES UNDER WHAT A MEASUREMENT ABOVE SAYS (PlaceNow) — the client's call its session is answering
// (protocolProfiler.h); of none, at the top. The platform's own work goes under it too.
class BACKEND_API ibRuntimeProfiler {
public:

	// Of every session (nullptr), or of the one whose interpreter `only` is.
	explicit ibRuntimeProfiler(const ibProcUnitState* only = nullptr);
	virtual ~ibRuntimeProfiler();

	ibRuntimeProfiler(const ibRuntimeProfiler&) = delete;
	ibRuntimeProfiler& operator=(const ibRuntimeProfiler&) = delete;

	// It is told nothing more, and its queries nothing either. Its destructor does it; a class that answers PlaceNow does
	// it first in its own, while it is still that class — a line of another session told meanwhile would ask a PlaceNow
	// already gone.
	void Stop();

	// What it measured so far, the rows by their own time descending. A line still running is not in it.
	virtual ibProfileData GetProfile() const;

	// The interpreter's three moments — asked only while one runs.
	static bool IsRunning() { return s_running.load(std::memory_order_relaxed) != 0; }
	static void Enter(const ibProcUnitState* state, const ibRunContext* context);
	static void Leave(const ibProcUnitState* state, const ibRunContext* context);
	static void Line(const ibProcUnitState* state, const ibRunContext* context, const ibByteUnit& code);

protected:

	using ibClock = std::chrono::steady_clock;

	// The place a line of the fiber telling now goes under — none (nullptr): the top. Asked under s_mutex, on that fiber:
	// a lock taken in it is taken after this one.
	virtual const void* PlaceNow() { return nullptr; }

	// Every line, by the place it went under (none — nullptr), the platform's own among them: what GetProfile writes.
	std::map<const void*, std::vector<ibProfileData::ibProfileRow>> Places() const;

	const ibClock::time_point m_begun;   // since when it measures

private:

	// A line's sums — of its bytecode, so two runs of a module add up in one; merged by the module's address when written.
	struct ibRow {
		const void*       m_place = nullptr;   // what it went under (PlaceNow)
		ibString          m_module;      // the address a stop names it by (ibByteUnit::m_strDocPath)
		ibString          m_name;        // the module as a person reads it
		ibString          m_procedure;   // empty — the module's body
		unsigned int      m_line = 0;    // from 1
		std::uint64_t     m_count = 0;
		ibClock::duration m_self{};
		ibClock::duration m_total{};
	};

	// A frame of a session's run, as this measurement saw it come.
	struct ibFrame {
		const ibRunContext* m_context = nullptr;
		ibClock::time_point m_entered;
		ibRow*              m_row = nullptr;   // the line it is on, none before its first
		ibClock::time_point m_begun;           // …since when
		ibClock::duration   m_called{};        // …and how long the calls the line made took
	};

	// A fiber's frames, and the rows open in them: a row open twice is a recursion. Threads that are no fiber share the
	// null one.
	struct ibTrack {
		std::vector<ibFrame>                           m_frames;
		std::unordered_map<const ibRow*, unsigned int> m_open;
	};

	// Its query measurement: a fiber's queries and statements go under the line the fiber is on.
	class ibQueries final : public ibQueryProfiler {
	public:
		explicit ibQueries(ibRuntimeProfiler& profile) : m_profile(profile) {}
		// Stopped first, while it is still this class: a statement of another session told meanwhile asks its PlaceNow.
		~ibQueries() override { Stop(); }

	private:
		// The line of the fiber's top frame that has begun one. Of no frame — the platform's own, when every session is
		// measured and the fiber is a session's lease: under the place the measurement's PlaceNow says, the call it is
		// answering — or of none, this measurement itself; a thread that is no fiber works for no session, and with one
		// session measured a fiber of no frame of it is another's — none.
		const void* PlaceNow() override;

		ibRuntimeProfiler& m_profile;
	};

	void OnEnter(const ibRunContext* context, ibClock::time_point now);
	void OnLeave(const ibRunContext* context, ibClock::time_point now);
	void OnLine(const ibRunContext* context, const ibByteUnit& code, ibClock::time_point now);

	// The frame's line ended at `now`: its sums are given.
	void Close(ibTrack& track, ibFrame& frame, ibClock::time_point now);
	// The frames from `depth` up left at `now`, each one's time given to the line that called it.
	void Unwind(ibTrack& track, std::size_t depth, ibClock::time_point now);
	// The row of that line under that place, made at its first.
	ibRow* RowOf(const void* place, const ibRunContext* context, const ibByteUnit& code);
	// The line of the nearest frame that has begun one — a frame just entered is still on its caller's line.
	static const ibRow* RowNow(const ibTrack& track);

	// Every measurement's sums and the list of them, under one lock: an event takes it once. Taken inside the queries'
	// lock (ibQueries::PlaceNow), never around it.
	static std::mutex                   s_mutex;
	static std::vector<ibRuntimeProfiler*> s_profiles;
	static std::atomic<unsigned int>    s_running;

	const ibProcUnitState* const m_only;

	std::map<std::tuple<const void*, const struct ibByteCode*, unsigned int>, ibRow> m_rows;
	std::unordered_map<const class ibFiber*, ibTrack>                                m_tracks;

	ibQueries m_queries;   // last: let go first, while what it reaches is still here
};

#endif
