#include "databaseProfiler.h"

#include <algorithm>

#include "databaseLayer.h"   // GetDatabaseLayerType — which database a statement went to

#include "core/fiber/fiberLocals.h"   // the run's time in the database is kept per fiber

std::mutex                       ibDatabaseProfiler::s_mutex;
std::vector<ibDatabaseProfiler*> ibDatabaseProfiler::s_measurements;
std::atomic<unsigned int>        ibDatabaseProfiler::s_running{ 0 };
std::atomic<std::uint64_t>       ibDatabaseProfiler::s_statements{ 0 };

namespace {

// The run's time in the database, while a measurement ran — see Spent. Saved and put back on every switch of fibers:
// two sessions on one thread suspend out of stack order, and a query's difference must not take in the other's time.
thread_local ibDatabaseProfiler::ibClock::duration t_spent{};

struct ibRegisterSpentLocal {
	ibRegisterSpentLocal()
	{
		ibFiberLocals::RegisterTrivial<ibDatabaseProfiler::ibClock::duration>(
			[](void* dst) { *static_cast<ibDatabaseProfiler::ibClock::duration*>(dst) = t_spent; },
			[](const void* src) { t_spent = *static_cast<const ibDatabaseProfiler::ibClock::duration*>(src); });
	}
} s_registerSpentLocal;

std::int64_t Microseconds(ibDatabaseProfiler::ibClock::duration spent)
{
	return static_cast<std::int64_t>(std::chrono::duration_cast<std::chrono::microseconds>(spent).count());
}

ibString DbmsOf(const ibDatabaseLayer* db)
{
	if (db == nullptr)
		return ibString();
	switch (db->GetDatabaseLayerType()) {
	case DATABASELAYER_FIREBIRD:   return ibT("Firebird");
	case DATABASELAYER_POSTGRESQL: return ibT("PostgreSQL");
	case DATABASELAYER_ODBC:       return ibT("ODBC");
	case DATABASELAYER_SQLLITE:    return ibT("SQLite");
	}
	return ibString();
}

} // namespace

ibDatabaseProfiler::ibDatabaseProfiler()
{
	const std::lock_guard<std::mutex> lock(s_mutex);
	s_measurements.push_back(this);
	s_running.store(static_cast<unsigned int>(s_measurements.size()), std::memory_order_relaxed);
}

ibDatabaseProfiler::~ibDatabaseProfiler()
{
	Stop();
}

void ibDatabaseProfiler::Stop()
{
	const std::lock_guard<std::mutex> lock(s_mutex);
	s_measurements.erase(std::remove(s_measurements.begin(), s_measurements.end(), this), s_measurements.end());
	s_running.store(static_cast<unsigned int>(s_measurements.size()), std::memory_order_relaxed);
}

std::map<const void*, ibDatabaseProfiler::ibSums> ibDatabaseProfiler::Places() const
{
	const std::lock_guard<std::mutex> lock(s_mutex);
	return m_places;
}

void ibDatabaseProfiler::Add(ibSums& into, const ibSums& sums)
{
	for (const auto& [text, statement] : sums) {
		ibStatementSum& sum = into[text];
		if (sum.m_plan.IsEmpty())
			sum.m_plan = statement.m_plan;
		if (sum.m_dbms.IsEmpty())
			sum.m_dbms = statement.m_dbms;
		sum.m_count += statement.m_count;
		sum.m_rows  += statement.m_rows;
		sum.m_run   += statement.m_run;
		sum.m_fetch += statement.m_fetch;
		if (statement.m_slowest > sum.m_slowest) {
			sum.m_slowest = statement.m_slowest;
			sum.m_values  = statement.m_values;
		}
	}
}

std::vector<ibQueryDebugData::ibStatement> ibDatabaseProfiler::GetStatements(const ibSums& sums)
{
	std::vector<ibQueryDebugData::ibStatement> written;
	written.reserve(sums.size());
	for (const auto& [text, sum] : sums) {
		ibQueryDebugData::ibStatement statement;
		statement.m_text  = text;
		statement.m_plan  = sum.m_plan;
		statement.m_dbms  = sum.m_dbms;
		statement.m_count = sum.m_count;
		statement.m_rows  = sum.m_rows;
		statement.m_run     = Microseconds(sum.m_run);
		statement.m_fetch   = Microseconds(sum.m_fetch);
		statement.m_slowest = Microseconds(sum.m_slowest);
		statement.m_values  = sum.m_values;
		written.push_back(statement);
	}
	std::stable_sort(written.begin(), written.end(),
		[](const ibQueryDebugData::ibStatement& a, const ibQueryDebugData::ibStatement& b) {
			return a.m_run + a.m_fetch > b.m_run + b.m_fetch;
		});
	return written;
}

void ibDatabaseProfiler::Sql(const ibDatabaseLayer* db, const ibString& text, ibClock::duration run, std::uint64_t rows,
	const ibPlan& plan, const ibValues& values)
{
	t_spent += run;
	const std::lock_guard<std::mutex> lock(s_mutex);
	for (ibDatabaseProfiler* measurement : s_measurements)
		measurement->OnSql(db, text, run, rows, plan, values, 0);
}

std::uint64_t ibDatabaseProfiler::SqlWithResults(const ibDatabaseLayer* db, const ibString& text, ibClock::duration run,
	const ibPlan& plan, const ibValues& values)
{
	t_spent += run;
	const std::uint64_t statement = ++s_statements;
	const std::lock_guard<std::mutex> lock(s_mutex);
	for (ibDatabaseProfiler* measurement : s_measurements)
		measurement->OnSql(db, text, run, 0, plan, values, statement);
	return statement;
}

// Told to every measurement: the one that saw the statement sent knows it by its number.
void ibDatabaseProfiler::Fetched(std::uint64_t statement, ibClock::duration fetch, std::uint64_t rows)
{
	t_spent += fetch;
	const std::lock_guard<std::mutex> lock(s_mutex);
	for (ibDatabaseProfiler* measurement : s_measurements)
		measurement->OnFetched(statement, fetch, rows);
}

ibDatabaseProfiler::ibClock::duration ibDatabaseProfiler::Spent()
{
	return t_spent;
}

void ibDatabaseProfiler::OnSql(const ibDatabaseLayer* db, const ibString& text, ibClock::duration run,
	std::uint64_t rows, const ibPlan& plan, const ibValues& values, std::uint64_t statement)
{
	const void* const place = PlaceNow();
	if (place == nullptr)
		return;

	ibStatementSum& sum = m_places[place][text];
	if (sum.m_count++ == 0) {
		sum.m_dbms = DbmsOf(db);

		// The plan of a text is asked once: a plan is the database's answer to the text, not to a run of it.
		auto [known, made] = m_plans.try_emplace(text);
		if (made && plan)
			known->second = plan();
		sum.m_plan = known->second;
	}
	sum.m_run  += run;
	sum.m_rows += rows;
	// The slowest run is the one worth sending again — its values with it.
	if (run > sum.m_slowest) {
		sum.m_slowest = run;
		sum.m_values  = values ? values() : std::vector<ibString>();
	}

	if (statement != 0)
		m_reading[statement] = &sum;
}

void ibDatabaseProfiler::OnFetched(std::uint64_t statement, ibClock::duration fetch, std::uint64_t rows)
{
	const auto reading = m_reading.find(statement);
	if (reading == m_reading.end())
		return;

	reading->second->m_fetch += fetch;
	reading->second->m_rows  += rows;
	m_reading.erase(reading);
}
