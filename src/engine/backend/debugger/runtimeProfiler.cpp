#include "runtimeProfiler.h"

#include <algorithm>

#include "backend/compiler/byteCode.h"
#include "backend/compiler/procContext.h"
#include "core/fiber/fiber.h"   // ibFiber — whose frames these are

std::mutex                     ibRuntimeProfiler::s_mutex;
std::vector<ibRuntimeProfiler*> ibRuntimeProfiler::s_profiles;
std::atomic<unsigned int>      ibRuntimeProfiler::s_running{ 0 };

ibRuntimeProfiler::ibRuntimeProfiler(const ibProcUnitState* only)
	: m_begun(ibClock::now()), m_only(only), m_queries(*this)
{
	const std::lock_guard<std::mutex> lock(s_mutex);
	s_profiles.push_back(this);
	s_running.store(static_cast<unsigned int>(s_profiles.size()), std::memory_order_relaxed);
}

ibRuntimeProfiler::~ibRuntimeProfiler()
{
	Stop();
}

// The queries first: their PlaceNow asks this measurement's.
void ibRuntimeProfiler::Stop()
{
	m_queries.Stop();

	const std::lock_guard<std::mutex> lock(s_mutex);
	s_profiles.erase(std::remove(s_profiles.begin(), s_profiles.end(), this), s_profiles.end());
	s_running.store(static_cast<unsigned int>(s_profiles.size()), std::memory_order_relaxed);
}

ibProfileData ibRuntimeProfiler::GetProfile() const
{
	ibProfileData measured;
	measured.m_duration = std::chrono::duration_cast<std::chrono::microseconds>(ibClock::now() - m_begun).count();
	std::map<const void*, std::vector<ibProfileData::ibProfileRow>> places = Places();
	if (const auto found = places.find(nullptr); found != places.end())
		measured.m_rows = std::move(found->second);
	return measured;
}

// The queries are read first, under their own lock, and the lines after, under this one: the two are never held the
// other way round.
std::map<const void*, std::vector<ibProfileData::ibProfileRow>> ibRuntimeProfiler::Places() const
{
	const auto microseconds = [](ibClock::duration spent) {
		return static_cast<std::int64_t>(std::chrono::duration_cast<std::chrono::microseconds>(spent).count());
	};

	std::map<const void*, ibQueryProfiler::ibSums> queries = m_queries.Places();

	const std::lock_guard<std::mutex> lock(s_mutex);

	// A row a line of a module under a place, whichever of its bytecodes ran it.
	struct ibLine {
		ibRow                   m_row;
		ibQueryProfiler::ibSums m_queries;
	};
	std::map<std::tuple<const void*, ibString, unsigned int>, ibLine> lines;
	for (const auto& [key, row] : m_rows) {
		const auto [at, made] = lines.try_emplace(std::make_tuple(row.m_place, row.m_module, row.m_line), ibLine{ row, {} });
		if (!made) {
			at->second.m_row.m_count += row.m_count;
			at->second.m_row.m_self  += row.m_self;
			at->second.m_row.m_total += row.m_total;
		}
		if (const auto found = queries.find(static_cast<const void*>(&row)); found != queries.end()) {
			at->second.m_queries.Add(found->second);
			queries.erase(found);
		}
	}

	std::vector<const ibLine*> sorted;
	sorted.reserve(lines.size());
	for (const auto& [key, line] : lines)
		sorted.push_back(&line);
	std::stable_sort(sorted.begin(), sorted.end(),
		[](const ibLine* a, const ibLine* b) { return a->m_row.m_self > b->m_row.m_self; });

	std::map<const void*, std::vector<ibProfileData::ibProfileRow>> places;
	for (const ibLine* line : sorted) {
		ibProfileData::ibProfileRow written;
		written.m_moduleName = line->m_row.m_module;
		written.m_name       = line->m_row.m_name;
		written.m_procedure  = line->m_row.m_procedure;
		written.m_line       = line->m_row.m_line;
		written.m_count      = line->m_row.m_count;
		written.m_self       = microseconds(line->m_row.m_self);
		written.m_total      = microseconds(line->m_row.m_total);
		written.m_queries    = line->m_queries.GetProfile();
		places[line->m_row.m_place].push_back(written);
	}

	// …and the platform's own, of no module — what the queries keep by no line: of no place, by the query measurement
	// itself; of a place, by the place (ibQueries::PlaceNow). Its time is its queries' and its statements', and it stands
	// where that time puts it.
	const void* const own = static_cast<const void*>(static_cast<const ibQueryProfiler*>(&m_queries));
	for (const auto& [key, sums] : queries) {
		const ibQueryDebugData platform = sums.GetProfile();
		if (platform.IsEmpty())
			continue;

		ibProfileData::ibProfileRow written;
		for (const ibQueryDebugData::ibQuery& query : platform.m_queries) {
			written.m_count += query.m_count;
			written.m_self  += query.m_total;
		}
		for (const ibQueryDebugData::ibStatement& statement : platform.m_statements) {
			written.m_count += statement.m_count;
			written.m_self  += statement.m_run + statement.m_fetch;
		}
		written.m_total   = written.m_self;
		written.m_queries = platform;
		std::vector<ibProfileData::ibProfileRow>& rows = places[key == own ? nullptr : key];
		const auto at = std::find_if(rows.begin(), rows.end(),
			[&written](const ibProfileData::ibProfileRow& row) { return row.m_self < written.m_self; });
		rows.insert(at, written);
	}
	return places;
}

// The moment is read before the lock is waited for: it is the interpreter's, not the lock's.
void ibRuntimeProfiler::Enter(const ibProcUnitState* state, const ibRunContext* context)
{
	const ibClock::time_point now = ibClock::now();
	const std::lock_guard<std::mutex> lock(s_mutex);
	for (ibRuntimeProfiler* profile : s_profiles) {
		if (profile->m_only == nullptr || profile->m_only == state)
			profile->OnEnter(context, now);
	}
}

void ibRuntimeProfiler::Leave(const ibProcUnitState* state, const ibRunContext* context)
{
	const ibClock::time_point now = ibClock::now();
	const std::lock_guard<std::mutex> lock(s_mutex);
	for (ibRuntimeProfiler* profile : s_profiles) {
		if (profile->m_only == nullptr || profile->m_only == state)
			profile->OnLeave(context, now);
	}
}

void ibRuntimeProfiler::Line(const ibProcUnitState* state, const ibRunContext* context, const ibByteUnit& code)
{
	const ibClock::time_point now = ibClock::now();
	const std::lock_guard<std::mutex> lock(s_mutex);
	for (ibRuntimeProfiler* profile : s_profiles) {
		if (profile->m_only == nullptr || profile->m_only == state)
			profile->OnLine(context, code, now);
	}
}

void ibRuntimeProfiler::OnEnter(const ibRunContext* context, ibClock::time_point now)
{
	ibFrame frame;
	frame.m_context = context;
	frame.m_entered = now;
	m_tracks[ibFiber::Current()].m_frames.push_back(frame);
}

// A frame entered before the measurement began is in no track, and its leaving says nothing.
void ibRuntimeProfiler::OnLeave(const ibRunContext* context, ibClock::time_point now)
{
	const auto found = m_tracks.find(ibFiber::Current());
	if (found == m_tracks.end())
		return;

	ibTrack& track = found->second;
	for (std::size_t depth = track.m_frames.size(); depth > 0; depth--) {
		if (track.m_frames[depth - 1].m_context == context) {
			Unwind(track, depth - 1, now);
			break;
		}
	}

	if (track.m_frames.empty())
		m_tracks.erase(found);
}

// The line is of the top frame. A frame entered before the measurement began is taken up at the first line seen of it.
void ibRuntimeProfiler::OnLine(const ibRunContext* context, const ibByteUnit& code, ibClock::time_point now)
{
	ibTrack& track = m_tracks[ibFiber::Current()];

	ibFrame* frame = nullptr;
	for (std::size_t depth = track.m_frames.size(); depth > 0; depth--) {
		if (track.m_frames[depth - 1].m_context == context) {
			Unwind(track, depth, now);
			frame = &track.m_frames.back();
			break;
		}
	}
	if (frame == nullptr) {
		ibFrame taken;
		taken.m_context = context;
		taken.m_entered = now;
		track.m_frames.push_back(taken);
		frame = &track.m_frames.back();
	}

	if (frame->m_row != nullptr) {
		Close(track, *frame, now);
		frame->m_begun  = now;
		frame->m_called = {};
	}
	else
		frame->m_begun = frame->m_entered;

	ibRow* const row = RowOf(PlaceNow(), context, code);
	row->m_count++;
	frame->m_row = row;
	track.m_open[row]++;
}

void ibRuntimeProfiler::Close(ibTrack& track, ibFrame& frame, ibClock::time_point now)
{
	ibRow& row = *frame.m_row;
	const ibClock::duration spent = now - frame.m_begun;
	row.m_self += spent - frame.m_called;

	const auto open = track.m_open.find(&row);
	if (open != track.m_open.end() && --open->second == 0) {
		track.m_open.erase(open);
		row.m_total += spent;
	}
}

void ibRuntimeProfiler::Unwind(ibTrack& track, std::size_t depth, ibClock::time_point now)
{
	while (track.m_frames.size() > depth) {
		ibFrame& frame = track.m_frames.back();
		if (frame.m_row != nullptr)
			Close(track, frame, now);
		const ibClock::duration spent = now - frame.m_entered;
		track.m_frames.pop_back();
		if (!track.m_frames.empty())
			track.m_frames.back().m_called += spent;
	}
}

ibRuntimeProfiler::ibRow* ibRuntimeProfiler::RowOf(const void* place, const ibRunContext* context, const ibByteUnit& code)
{
	const auto [at, made] = m_rows.try_emplace(std::make_tuple(place, context->GetByteCode(), code.m_numLine));
	ibRow& row = at->second;
	if (made) {
		row.m_place  = place;
		row.m_module = code.m_strDocPath;
		row.m_name   = code.m_strModuleName;
		row.m_line   = code.m_numLine + 1;
		if (const ibByteCode::ibByteFunction* const function = context->m_currentFunction; function != nullptr)
			row.m_procedure = function->m_strRealName.IsEmpty() ? ibString(ibT("<fn>")) : function->m_strRealName;
	}
	return &row;
}

const ibRuntimeProfiler::ibRow* ibRuntimeProfiler::RowNow(const ibTrack& track)
{
	for (auto frame = track.m_frames.rbegin(); frame != track.m_frames.rend(); ++frame) {
		if (frame->m_row != nullptr)
			return frame->m_row;
	}
	return nullptr;
}

// Under the queries' lock; the lines' is taken inside it, the one order the two are ever held in.
const void* ibRuntimeProfiler::ibQueries::PlaceNow()
{
	const std::lock_guard<std::mutex> lock(ibRuntimeProfiler::s_mutex);

	const ibFiber* const fiber = ibFiber::Current();
	const auto found = m_profile.m_tracks.find(fiber);
	if (found != m_profile.m_tracks.end()) {
		if (const ibRow* const row = RowNow(found->second); row != nullptr)
			return row;
	}
	// Of no frame: the session registry, the jobs' clock, the debugger, an assistant's call run on threads that are no
	// fiber, and are nobody's work measured — so are they not the platform's row.
	else if (m_profile.m_only != nullptr || fiber == nullptr || fiber->IsScheduler())
		return nullptr;

	// The platform's own — under what a line would have gone under.
	const void* const place = m_profile.PlaceNow();
	return place != nullptr ? place : ibQueryProfiler::PlaceNow();
}
