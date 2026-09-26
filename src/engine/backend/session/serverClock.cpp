////////////////////////////////////////////////////////////////////////////
//	Description : "now" is the server's clock (serverClock.h)
////////////////////////////////////////////////////////////////////////////

#include "serverClock.h"

#include "backend/backend_exception.h"
#include "backend/databaseLayer/databaseLayer.h"
#include "backend/diagnostics/journal.h"

#include <atomic>
#include <cstdlib>

namespace {

std::atomic<wxLongLong_t> s_offsetMs{ 0 };

// The machine's clock as a reading, to the millisecond - the local parts of this instant, which a
// real instant always has (the bridge is exact here: no instant sits in a skipped hour).
wxLongLong_t MachineNow() { return ibWallOfDateTime(wxDateTime::UNow()); }

} // namespace

wxLongLong_t ibServerClock::Now()
{
	return MachineNow() + s_offsetMs.load(std::memory_order_relaxed);
}

wxLongLong_t ibServerClock::Offset()
{
	return s_offsetMs.load(std::memory_order_relaxed);
}

bool ibServerClock::Read(ibDatabaseLayer& layer, wxLongLong_t& reading)
{
	const ibDialectDictionary& dialect = layer.GetDialect();
	if (dialect.m_localTimestamp.IsEmpty())
		return false;   // the dialect has no word for its server's clock
	if (layer.HasSessionTimeZone() && layer.GetSessionTimeZone().IsEmpty())
		return false;   // the session stands in no named zone: its clock reads UTC or the server's own, nobody's here
	wxString sql = wxT("SELECT ") + dialect.m_localTimestamp + wxT(" AS server_now");
	if (!dialect.m_selectFromDual.IsEmpty())
		sql += wxT(" FROM ") + dialect.m_selectFromDual;
	try {
		reading = layer.GetSingleResultDate(sql, 1);
	}
	catch (const ibBackendException&) {
		return false;
	}
	return reading != emptyDate;
}

bool ibServerClock::Refresh(ibDatabaseLayer& layer, const wxString& zone)
{
	if (layer.GetDialect().m_localTimestamp.IsEmpty())
		return false;   // the dialect has no word for its server's clock - the machine's stands

	// In the base's zone, or not at all (the header): a server that reads its clock in the
	// session's zone is asked only in a zone the base named, through a connection standing in it.
	// This connection is the caller's own, so it is put into the zone here - which is also how a
	// connection bound before the zone was saved comes to stand in it. With no zone to measure in,
	// or a zone the server refuses, "now" is the machine's clock again: a difference measured in a
	// zone the base no longer names is a difference of nothing.
	if (layer.HasSessionTimeZone()) {
		if (zone.IsEmpty() || (layer.GetSessionTimeZone() != zone && !layer.SetSessionTimeZone(zone))) {
			Reset();
			return false;
		}
	}

	wxLongLong_t server = emptyDate;
	if (!Read(layer, server))
		return false;   // the question failed; the difference measured before still stands

	// The two readings compared at the resolution the server answered in: a server that says its
	// clock to the second (Firebird's reading through struct tm, SQLite's datetime()) is set against
	// this machine's clock to the second, or every measurement would run up to a second behind.
	const wxLongLong_t machine = MachineNow();
	const wxLongLong_t compared = server % 1000 == 0 ? machine - machine % 1000 : machine;
	const wxLongLong_t measured = server - compared;
	const wxLongLong_t before = s_offsetMs.exchange(measured, std::memory_order_relaxed);
	// Said once, when it matters: a base whose clock stands an hour from this machine's is worth a
	// line in the journal; the drift of a second between two measurements is not.
	if (std::llabs(static_cast<long long>(measured - before)) >= 60000)
		ibJournalInfo(wxT("session.clock"), wxT("the base's clock reads %lld ms from this machine's (was %lld)"),
			static_cast<long long>(measured), static_cast<long long>(before));
	return true;
}

void ibServerClock::Reset()
{
	s_offsetMs.store(0, std::memory_order_relaxed);
}
