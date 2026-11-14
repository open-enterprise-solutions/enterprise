#ifndef __IB_QUERY_PROFILER_H__
#define __IB_QUERY_PROFILER_H__

#include <atomic>
#include <chrono>
#include <cstdint>
#include <map>
#include <mutex>
#include <unordered_map>
#include <vector>

#include "backend/backend_core.h"
#include "backend/databaseLayer/databaseProfiler.h"   // ibDatabaseProfiler — the statements under its queries
#include "core/query/queryDebug.h"                    // ibQueryDebugData — what it measured

// ⭐ A QUERY MEASUREMENT — the queries that went through the L3 door, from its making to its end, and where each went
// (Max, 2026-11-14: *"whether a query goes to the DBMS or runs in memory"*): each by what it did and to what, with how
// often it ran, its time in the door, and of it the database's — the rest being the engine's own, in memory — and
// under it the statements it sent. It runs while it lives: made, it is told every query; gone, it is told nothing.
//
// THE DOOR TELLS IT, on the thread the query runs on: the terminals of ibDataQueryBuilder (dataQueryBuilder.cpp) —
// read, aggregate, totals, insert, upsert, update, delete — begun and ended (Enter, Leave). Every front-end comes down
// through them — the language, LINQ, the composers, an object's write, a policy's own reads (Max, 2026-11-14: *"there
// is one door there"*) — and a query run inside another is that one's part. The statements are a database
// measurement's (backend/databaseLayer/databaseProfiler.h), held here, whose place is the query open on the thread —
// or, with none open, this measurement's own place: a statement sent past the door. A cursor is drawn after its query
// returned: what its rows took is its statement's fetch, not the query's time in the door.
//
// ⚠ IT KNOWS NOTHING OF THE RUNTIME (Max, 2026-11-14). Where a thread's work goes is PlaceNow's — this measurement
// itself here; the profiler (ibRuntimeProfiler), which knows the runtime, says the line the thread is on.
class BACKEND_API ibQueryProfiler {
public:

	using ibClock = std::chrono::steady_clock;

	// A query's sums, by what it reads from — its runs, its whole time, and of it the database's; and its statements.
	struct ibQuerySum {
		std::uint64_t              m_count = 0;
		ibClock::duration          m_total{};
		ibClock::duration          m_database{};
		ibDatabaseProfiler::ibSums m_statements;
	};

	// The sums of one place: its queries, and the statements sent past them.
	struct ibSums {
		std::map<ibString, ibQuerySum> m_queries;
		ibDatabaseProfiler::ibSums     m_statements;

		bool             IsEmpty() const { return m_queries.empty() && m_statements.empty(); }
		void             Add(const ibSums& sums);
		ibQueryDebugData GetProfile() const;
	};

	ibQueryProfiler();
	virtual ~ibQueryProfiler();

	ibQueryProfiler(const ibQueryProfiler&) = delete;
	ibQueryProfiler& operator=(const ibQueryProfiler&) = delete;

	// It is told nothing more — its statements first, then its queries. Its destructor does it; a class that answers
	// PlaceNow does it first in its own, while it is still that class (ibDatabaseProfiler::Stop).
	void Stop();

	// What it measured so far, by the place each query and statement went to.
	std::map<const void*, ibSums> Places() const;
	// …of its own place: the queries by their time descending, and the statements of no query.
	ibQueryDebugData GetProfile() const;

	// The doors' moments — asked only while one runs.
	static bool IsRunning() { return s_running.load(std::memory_order_relaxed) != 0; }
	static void Enter(const ibString& source);
	static void Leave();

protected:

	// The place a query of the thread telling now goes to — this measurement itself; none (nullptr) — not this one's.
	// Asked under s_mutex, on that thread: a lock taken in it is taken after this one.
	virtual const void* PlaceNow() { return this; }

private:

	// Its statements: of the thread's query open, or else of its place.
	class ibStatements final : public ibDatabaseProfiler {
	public:
		explicit ibStatements(ibQueryProfiler& profiler) : m_profiler(profiler) {}

	private:
		const void* PlaceNow() override;

		ibQueryProfiler& m_profiler;
	};

	// A fiber's query open — the outermost, a query inside it being its part — since when, the fiber's time in the
	// database then, and how deep. No query (nullptr) when its place said none. Kept by the fiber (ibFiber::Current), not
	// by the thread: two sessions on one thread suspend out of stack order. Threads that are no fiber share the null one.
	struct ibTrack {
		ibQuerySum*         m_query = nullptr;
		ibClock::time_point m_begun;
		ibClock::duration   m_spent{};
		unsigned int        m_depth = 0;
	};

	void OnEnter(const ibString& source, ibClock::time_point now, ibClock::duration spent);
	void OnLeave(ibClock::time_point now, ibClock::duration spent);

	// Every measurement's sums and the list of them, under one lock: a moment takes it once.
	static std::mutex                     s_mutex;
	static std::vector<ibQueryProfiler*>  s_measurements;
	static std::atomic<unsigned int>      s_running;

	std::map<const void*, std::map<ibString, ibQuerySum>> m_places;   // the queries — their statements are m_statements'
	std::unordered_map<const class ibFiber*, ibTrack>     m_tracks;

	ibStatements m_statements;   // last: let go first, while what it reaches is still here
};

#endif
