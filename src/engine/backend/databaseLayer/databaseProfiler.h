#ifndef __IB_DATABASE_PROFILER_H__
#define __IB_DATABASE_PROFILER_H__

#include <atomic>
#include <chrono>
#include <cstdint>
#include <functional>
#include <map>
#include <mutex>
#include <unordered_map>
#include <vector>

#include "backend/backend_core.h"
#include "core/query/queryDebug.h"   // ibQueryDebugData::ibStatement — what it measured, written

class ibDatabaseLayer;

// ⭐ A DATABASE MEASUREMENT — the statements the database got, from its making to its end (Max, 2026-11-14: *"its plan,
// the physical query and which DBMS"*): each by its text, with the plan the database chose, how often it was sent, the
// rows read back or changed, and the database's time running it and handing the rows over. It runs while it lives:
// made, it is told every statement; gone, it is told nothing.
//
// THE DOORS TELL IT, on the thread the statement runs on: the layer's own (ibDatabaseLayer::RunStatement / RunQuery /
// RunQueryWithResults; BeginTransaction / Commit / RollBack where the outermost level reaches the driver, as START
// TRANSACTION / COMMIT / ROLLBACK), and a statement prepared and run in L2 — a read (ibRunRendered, the runs of
// ibQueryResult), a write (ibDatabaseQueryBuilder::Execute, ibBatchInsert), the rows read through ibQueryResult::Next,
// told when it lets them go. A statement prepared past L2 by hand (the journal's, the session registry's) is not seen.
//
// WHERE A STATEMENT GOES is PlaceNow's — this measurement itself here; the query measurement above
// (backend/query/queryProfiler.h) says the query open on the thread, the profiler the line. A place is only a key, kept
// here: what each layer keeps, it keeps under its own lock.
class BACKEND_API ibDatabaseProfiler {
public:

	using ibClock = std::chrono::steady_clock;

	// A statement's sums, by its text — and its slowest run, with the values it was sent with.
	struct ibStatementSum {
		ibString              m_plan;
		ibString              m_dbms;
		std::uint64_t         m_count = 0;
		std::uint64_t         m_rows = 0;
		ibClock::duration     m_run{};
		ibClock::duration     m_fetch{};
		ibClock::duration     m_slowest{};
		std::vector<ibString> m_values;
	};
	using ibSums = std::map<ibString, ibStatementSum>;

	ibDatabaseProfiler();
	virtual ~ibDatabaseProfiler();

	ibDatabaseProfiler(const ibDatabaseProfiler&) = delete;
	ibDatabaseProfiler& operator=(const ibDatabaseProfiler&) = delete;

	// It is told nothing more. Its destructor does it; a class that answers PlaceNow does it first in its own, while it
	// is still that class — a statement of another session told meanwhile would ask a PlaceNow already gone.
	void Stop();

	// What it measured so far, by the place each statement went to.
	std::map<const void*, ibSums> Places() const;

	// Sums of two places as one, and written as the core's structure — the costliest first.
	static void                                      Add(ibSums& into, const ibSums& sums);
	static std::vector<ibQueryDebugData::ibStatement> GetStatements(const ibSums& sums);

	// The doors' moments — asked only while one runs.
	static bool IsRunning() { return s_running.load(std::memory_order_relaxed) != 0; }

	// The plan of a statement, as the database writes it (ibPreparedStatement::GetPlan) — asked once a text, the first
	// time a measurement meets it; empty when the driver cannot tell it. ⚠ It asks the driver alone and never a door: it
	// is asked under the measurements' lock.
	using ibPlan = std::function<ibString()>;
	// …and the values a run was sent with, each as SQL writes it — asked only of a run slower than every one before it
	// of that text, under the same lock: it reads what the run bound and nothing else.
	using ibValues = std::function<std::vector<ibString>()>;

	// A statement run that hands no rows back — `rows`, those it wrote or deleted.
	static void          Sql(const ibDatabaseLayer* db, const ibString& text, ibClock::duration run, std::uint64_t rows,
		const ibPlan& plan, const ibValues& values);
	// …and one that does: answered with its number, which its rows read are told by.
	static std::uint64_t SqlWithResults(const ibDatabaseLayer* db, const ibString& text, ibClock::duration run,
		const ibPlan& plan, const ibValues& values);
	static void          Fetched(std::uint64_t statement, ibClock::duration fetch, std::uint64_t rows);

	// The time the run telling now — a fiber, or a thread that is none — has spent in the database while a measurement
	// ran: read at a start and at an end by a measurement above, its own part of it is the difference.
	static ibClock::duration Spent();

protected:

	// The place a statement of the thread telling now goes to — this measurement itself; none (nullptr) — not this
	// one's. Asked under s_mutex, on that thread: a lock taken in it is taken after this one.
	virtual const void* PlaceNow() { return this; }

private:

	void OnSql(const ibDatabaseLayer* db, const ibString& text, ibClock::duration run, std::uint64_t rows,
		const ibPlan& plan, const ibValues& values, std::uint64_t statement);
	void OnFetched(std::uint64_t statement, ibClock::duration fetch, std::uint64_t rows);

	// Every measurement's sums and the list of them, under one lock: a moment takes it once.
	static std::mutex                        s_mutex;
	static std::vector<ibDatabaseProfiler*>  s_measurements;
	static std::atomic<unsigned int>         s_running;
	static std::atomic<std::uint64_t>        s_statements;   // the statements' numbers

	std::map<const void*, ibSums>                     m_places;
	std::map<ibString, ibString>                      m_plans;     // a text's plan, asked once
	std::unordered_map<std::uint64_t, ibStatementSum*> m_reading;  // a statement whose rows are being read
};

#endif
