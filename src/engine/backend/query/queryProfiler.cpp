#include "queryProfiler.h"

#include <algorithm>
#include <set>

#include "core/fiber/fiber.h"   // ibFiber::Current — whose query is open

std::mutex                    ibQueryProfiler::s_mutex;
std::vector<ibQueryProfiler*> ibQueryProfiler::s_measurements;
std::atomic<unsigned int>     ibQueryProfiler::s_running{ 0 };

namespace {

std::int64_t Microseconds(ibQueryProfiler::ibClock::duration spent)
{
	return static_cast<std::int64_t>(std::chrono::duration_cast<std::chrono::microseconds>(spent).count());
}

} // namespace

ibQueryProfiler::ibQueryProfiler()
	: m_statements(*this)
{
	const std::lock_guard<std::mutex> lock(s_mutex);
	s_measurements.push_back(this);
	s_running.store(static_cast<unsigned int>(s_measurements.size()), std::memory_order_relaxed);
}

ibQueryProfiler::~ibQueryProfiler()
{
	Stop();
}

// The statements first: one told now asks this measurement's PlaceNow. The two locks are taken one after the other.
void ibQueryProfiler::Stop()
{
	m_statements.Stop();

	const std::lock_guard<std::mutex> lock(s_mutex);
	s_measurements.erase(std::remove(s_measurements.begin(), s_measurements.end(), this), s_measurements.end());
	s_running.store(static_cast<unsigned int>(s_measurements.size()), std::memory_order_relaxed);
}

// The statements are read first, under their lock, and the queries after, under this one — the two are never held
// the other way round. A statement's place is a query's sum or a place of this measurement's: the first are put under
// their queries, the rest under their places.
std::map<const void*, ibQueryProfiler::ibSums> ibQueryProfiler::Places() const
{
	const std::map<const void*, ibDatabaseProfiler::ibSums> statements = m_statements.Places();

	std::map<const void*, ibSums> places;
	std::set<const void*>         queries;
	{
		const std::lock_guard<std::mutex> lock(s_mutex);
		for (const auto& [place, sums] : m_places) {
			ibSums& written = places[place];
			for (const auto& [source, query] : sums) {
				ibQuerySum& copy = written.m_queries[source];
				copy = query;
				if (const auto found = statements.find(&query); found != statements.end())
					copy.m_statements = found->second;
				queries.insert(&query);
			}
		}
	}
	for (const auto& [place, sums] : statements) {
		if (queries.count(place) == 0)
			places[place].m_statements = sums;
	}
	return places;
}

ibQueryDebugData ibQueryProfiler::GetProfile() const
{
	const std::map<const void*, ibSums> places = Places();
	const auto found = places.find(static_cast<const void*>(this));
	return found != places.end() ? found->second.GetProfile() : ibQueryDebugData();
}

void ibQueryProfiler::ibSums::Add(const ibSums& sums)
{
	for (const auto& [source, query] : sums.m_queries) {
		ibQuerySum& sum = m_queries[source];
		sum.m_count    += query.m_count;
		sum.m_total    += query.m_total;
		sum.m_database += query.m_database;
		ibDatabaseProfiler::Add(sum.m_statements, query.m_statements);
	}
	ibDatabaseProfiler::Add(m_statements, sums.m_statements);
}

ibQueryDebugData ibQueryProfiler::ibSums::GetProfile() const
{
	ibQueryDebugData measured;
	measured.m_queries.reserve(m_queries.size());
	for (const auto& [source, sum] : m_queries) {
		ibQueryDebugData::ibQuery query;
		query.m_source     = source;
		query.m_count      = sum.m_count;
		query.m_total      = Microseconds(sum.m_total);
		query.m_database   = Microseconds(sum.m_database);
		query.m_statements = ibDatabaseProfiler::GetStatements(sum.m_statements);
		measured.m_queries.push_back(query);
	}
	std::stable_sort(measured.m_queries.begin(), measured.m_queries.end(),
		[](const ibQueryDebugData::ibQuery& a, const ibQueryDebugData::ibQuery& b) { return a.m_total > b.m_total; });
	measured.m_statements = ibDatabaseProfiler::GetStatements(m_statements);
	return measured;
}

// The moment and the thread's time in the database are read before the lock is waited for: they are the query's, not
// the lock's.
void ibQueryProfiler::Enter(const ibString& source)
{
	const ibClock::time_point now = ibClock::now();
	const ibClock::duration spent = ibDatabaseProfiler::Spent();
	const std::lock_guard<std::mutex> lock(s_mutex);
	for (ibQueryProfiler* measurement : s_measurements)
		measurement->OnEnter(source, now, spent);
}

void ibQueryProfiler::Leave()
{
	const ibClock::time_point now = ibClock::now();
	const ibClock::duration spent = ibDatabaseProfiler::Spent();
	const std::lock_guard<std::mutex> lock(s_mutex);
	for (ibQueryProfiler* measurement : s_measurements)
		measurement->OnLeave(now, spent);
}

// A query inside the one the thread is in is that one's part: only the outermost is a query of its place.
void ibQueryProfiler::OnEnter(const ibString& source, ibClock::time_point now, ibClock::duration spent)
{
	ibTrack& track = m_tracks[ibFiber::Current()];
	if (track.m_depth++ > 0)
		return;

	const void* const place = PlaceNow();
	if (place == nullptr)
		return;

	ibQuerySum& query = m_places[place][source];
	query.m_count++;
	track.m_query = &query;
	track.m_begun = now;
	track.m_spent = spent;
}

// A query entered before the measurement began is in no track, and its leaving says nothing.
void ibQueryProfiler::OnLeave(ibClock::time_point now, ibClock::duration spent)
{
	const auto found = m_tracks.find(ibFiber::Current());
	if (found == m_tracks.end())
		return;

	ibTrack& track = found->second;
	if (--track.m_depth > 0)
		return;

	if (track.m_query != nullptr) {
		track.m_query->m_total    += now - track.m_begun;
		track.m_query->m_database += spent - track.m_spent;
	}
	m_tracks.erase(found);
}

// Under the statements' lock; this one is taken inside it, the one order the two are ever held in.
const void* ibQueryProfiler::ibStatements::PlaceNow()
{
	const std::lock_guard<std::mutex> lock(ibQueryProfiler::s_mutex);

	const auto found = m_profiler.m_tracks.find(ibFiber::Current());
	if (found != m_profiler.m_tracks.end() && found->second.m_depth > 0)
		return found->second.m_query;   // none — a query not this measurement's
	return m_profiler.PlaceNow();
}
