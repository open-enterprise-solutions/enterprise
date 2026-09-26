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

// The machine's clock as a reading - the local parts of this instant, which a real instant always
// has (the bridge is exact here: no instant sits in a skipped hour).
wxLongLong_t MachineNow() { return ibWallOfDateTime(wxDateTime::Now()); }

} // namespace

wxLongLong_t ibServerClock::Now()
{
	return MachineNow() + s_offsetMs.load(std::memory_order_relaxed);
}

wxLongLong_t ibServerClock::Offset()
{
	return s_offsetMs.load(std::memory_order_relaxed);
}

bool ibServerClock::Refresh(ibDatabaseLayer& layer)
{
	const ibDialectDictionary& dialect = layer.GetDialect();
	if (dialect.m_localTimestamp.IsEmpty())
		return false;   // the dialect has no word for its server's clock - the machine's stands

	wxString sql = wxT("SELECT ") + dialect.m_localTimestamp + wxT(" AS server_now");
	if (!dialect.m_selectFromDual.IsEmpty())
		sql += wxT(" FROM ") + dialect.m_selectFromDual;

	wxLongLong_t server = emptyDate;
	try {
		server = layer.GetSingleResultDate(sql, 1);
	}
	catch (const ibBackendException&) {
		return false;   // the question failed; the difference measured before still stands
	}
	if (server == emptyDate)
		return false;

	const wxLongLong_t measured = server - MachineNow();
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
