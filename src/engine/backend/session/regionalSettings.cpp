////////////////////////////////////////////////////////////////////////////
//	Description : the base's time zone - named on the base, in force on its connections (regionalSettings.h)
////////////////////////////////////////////////////////////////////////////

#include "regionalSettings.h"

#include "backend/appData.h"
#include "backend/backend_exception.h"
#include "backend/databaseLayer/connectionPool.h"
#include "backend/databaseLayer/connectionScope.h"
#include "backend/databaseLayer/databaseLayer.h"
#include "backend/databaseLayer/databaseResultSet.h"
#include "backend/settings/settingsStorage.h"
#include "core/diagnostics/journal.h"
#include "core/serialize/dataBuilder.h"

#include <cstdlib>   // std::llabs

#include <wx/datetime.h>   // wxDateTime::UNow - this machine's own clock, to measure against

namespace {

// The one row: a fixed object under the category, shared by everybody (no user in the key).
const wxChar* const kRegionalSettingsObject = wxT("72656769-6f6e-616c-0000-000000000001");

ibSettingsKey KeyOfTheBase()
{
	return ibSettingsKey(ibSettingsCategory::Regional, ibGuid(kRegionalSettingsObject));
}

} // namespace

ibRegionalSettings::ibRegionalSettings(ib::AppDataCtorToken owner)
	: m_owner(owner.GetApplicationInstance())
{
	// Its holder takes connections from ITS base's pool - named once, here (the base builds the pool first),
	// never asked of "the current one": the lock manager's arrangement, for the lock manager's reason.
	m_holder.SetPool(ibApplicationInstance::GetConnectionPool(m_owner));
}

wxString ibRegionalSettings::GetTimeZone()
{
	std::lock_guard<std::mutex> lock(m_mutex);
	return m_timeZone;
}

bool ibRegionalSettings::SetTimeZone(const wxString& zone, wxString* refusal)
{
	ibSettingsStorage* const storage = ibApplicationInstance::GetSettingsStorage(m_owner);
	if (storage == nullptr) {
		if (refusal != nullptr)
			*refusal = _("The base is not open; the time zone was not saved.");
		return false;
	}
	// A connection of this base for the whole call: the zone is put on it, and the row is written through it.
	const ibConnectionScope scope(&m_holder);
	if (!scope) {
		if (refusal != nullptr)
			*refusal = _("No connection to the base; the time zone was not saved.");
		return false;
	}
	// The zone FIRST: the server's refusal of the name is heard here, in front of whoever is saving, and
	// nothing is written under a name the base cannot count in. A driver without a session zone has nothing
	// to refuse it with; the name is kept.
	if (scope->HasSessionTimeZone() && !scope->SetSessionTimeZone(zone)) {
		if (refusal != nullptr)
			*refusal = wxString::Format(_("The base's server refused the time zone '%s' - it does not know the name, or it has no session zone (Firebird 4 or later, PostgreSQL); the setting was not saved."), zone);
		return false;
	}
	ibDataNode node;
	node.SetValue(wxT("timeZone"), zone);
	if (!storage->Save(KeyOfTheBase(), node, &m_holder)) {
		if (refusal != nullptr)
			*refusal = _("The time zone could not be written to the base.");
		return false;
	}
	{
		std::lock_guard<std::mutex> lock(m_mutex);
		m_timeZone = zone;
		m_read = true;
	}
	// In force at once: every other connection takes the zone as it is next handed out, and the clock is
	// measured in it - on this connection, which stands in it already.
	if (ibConnectionPool* const pool = ibApplicationInstance::GetConnectionPool(m_owner))
		pool->SetSessionTimeZone(zone);
	MeasureClock(*scope.get(), zone);
	return true;
}

bool ibRegionalSettings::Refresh(ibDatabaseConnectionHolder* holder)
{
	ibSettingsStorage* const storage = ibApplicationInstance::GetSettingsStorage(m_owner);
	if (storage == nullptr)
		return false;
	ibDatabaseConnectionHolder* const through = holder != nullptr ? holder : &m_holder;
	// One connection for the whole call, FIRST: the row is read through it and the zone put on it. The
	// registry's thread names its holder and works on the connection that holder keeps - a connection the
	// pool handed out long ago, before the zone was named, which no hand-out will put into it.
	const ibConnectionScope scope(through);
	if (!scope)
		return false;
	// A read that FAILED - the base gone for a moment, a statement refused - is not "the base names nothing":
	// what is in force stays in force, and the next minute asks again. Taking a failure for an empty row would
	// take every connection out of the base's zone on a hiccup.
	ibDataNode node;
	bool failed = false;
	const bool held = storage->Restore(KeyOfTheBase(), node, through, &failed);
	if (failed)
		return false;
	wxString zone;
	if (held)
		node.GetValue(wxT("timeZone"), zone);

	bool first = false;
	wxString before;
	{
		std::lock_guard<std::mutex> lock(m_mutex);
		first = !m_read;
		before = m_timeZone;
		m_read = true;
		m_timeZone = zone;
	}
	if (first || before != zone) {
		// Recorded for the pool whenever it changed; the pool asks each connection its own zone at hand-out.
		if (ibConnectionPool* const pool = ibApplicationInstance::GetConnectionPool(m_owner))
			pool->SetSessionTimeZone(zone);
		if (!zone.IsEmpty())
			ibJournalInfo(wxT("session.zone"), wxT("the base names the time zone %s"), zone);
		else if (!first)
			ibJournalInfo(wxT("session.zone"), wxT("the base names no time zone any more; each engine stands in its own"));
	}
	// …and this connection put into it, said when it cannot be (a name the server refuses, a lost
	// connection) - once per change, as the pool says its own.
	if (scope->HasSessionTimeZone() && scope->GetSessionTimeZone() != zone && !scope->SetSessionTimeZone(zone)
		&& (first || before != zone))
		ibJournalError(wxT("session.zone"), wxT("the base names the time zone %s, and a connection could not be put into it (the server refused the name, or the connection was lost)"), zone);
	MeasureClock(*scope.get(), zone);
	return held;
}

void ibRegionalSettings::MeasureClock(ibDatabaseLayer& connection, const wxString& zone)
{
	const ibDialectDictionary& dialect = connection.GetDialect();
	// In the base's zone, or not at all (the header): a server that reads its clock in the session's zone is
	// asked only in a zone the base named, through a connection standing in it. A dialect with no word for
	// its clock has the machine's.
	if (dialect.m_localTimestamp.IsEmpty()
		|| (connection.HasSessionTimeZone() && (zone.IsEmpty() || connection.GetSessionTimeZone() != zone))) {
		if (m_clockOffset.exchange(0, std::memory_order_relaxed) != 0)
			ibJournalInfo(wxT("session.clock"), wxT("the base's clock is not measured (no zone it stands in); 'now' is this machine's clock"));
		return;
	}
	wxString sql = wxT("SELECT ") + dialect.m_localTimestamp + wxT(" AS server_now");
	if (!dialect.m_selectFromDual.IsEmpty())
		sql += wxT(" FROM ") + dialect.m_selectFromDual;
	ibDateTime server;
	try {
		// As the one argument of "%s": the statement doors are printf-shaped, and SQLite's word holds '%'.
		ibDatabaseResultSet* rs = connection.RunQueryWithResults(wxT("%s"), sql);
		if (rs == nullptr)
			return;
		if (rs->Next())
			server = rs->GetResultDate(1);
		connection.CloseResultSet(rs);
	}
	catch (const ibBackendException&) {
		return;   // the question failed; the difference measured before still stands
	}
	if (server.IsEmpty())
		return;
	// The two readings compared at the resolution the server answered in: a server that says its clock to the
	// second (Firebird through struct tm) is set against this machine's clock to the second, or every
	// measurement would run up to a second behind. The machine's reading is the bridge of its own clock -
	// never ibDateTime::Now(), which is what this measurement moves.
	ibDateTime machine = ibDateTime::OfWxDateTime(wxDateTime::UNow());
	if (server.GetValue() % 1000 == 0)
		machine = ibDateTime(machine.GetValue() - machine.GetValue() % 1000);
	const long long measured = server - machine;
	const long long before = m_clockOffset.exchange(measured, std::memory_order_relaxed);
	// Said when it matters: a base whose clock stands a minute or more from this machine's is worth a line;
	// the drift of a second between two measurements is not.
	if (std::llabs(measured - before) >= 60000)
		ibJournalInfo(wxT("session.clock"), wxT("the base's clock reads %lld ms from this machine's (was %lld)"), measured, before);
}

std::vector<wxString> ibRegionalSettings::KnownTimeZones()
{
	std::vector<wxString> zones;
	const ibConnectionScope scope(&m_holder);
	if (!scope || !scope->HasSessionTimeZone())
		return zones;
	// The dialect's word for the list - an engine without one answers with nothing, which the page reads as
	// "type it".
	const wxString sql = scope->GetDialect().m_timeZoneNames;
	if (sql.IsEmpty())
		return zones;
	try {
		ibDatabaseResultSet* rs = scope->RunQueryWithResults(wxT("%s"), sql);
		if (rs != nullptr) {
			while (rs->Next()) {
				wxString name = rs->GetResultString(1);
				name.Trim();
				if (!name.IsEmpty())
					zones.push_back(name);
			}
			scope->CloseResultSet(rs);
		}
	}
	catch (const ibBackendException&) {
		zones.clear();
	}
	return zones;
}
