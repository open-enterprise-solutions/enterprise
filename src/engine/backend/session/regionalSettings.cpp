////////////////////////////////////////////////////////////////////////////
//	Description : the base's time zone and locale - named on the base, in force on its connections and its clients (regionalSettings.h)
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

#include <atomic>
#include <cstdlib>   // std::llabs


namespace {

// Two rows, one a setting: fixed objects under the category, shared by everybody (no user in the key). The
// zone's is the row it has had; the locale's stands beside it rather than in it, so that a process writing
// the one - from a cache up to a minute old - cannot write over the other as somebody just saved it.
const wxChar* const kZoneObject   = wxT("72656769-6f6e-616c-0000-000000000001");
const wxChar* const kLocaleObject = wxT("72656769-6f6e-616c-0000-000000000002");

ibSettingsKey ZoneKey()   { return ibSettingsKey(ibSettingsCategory::Regional, ibGuid(kZoneObject)); }
ibSettingsKey LocaleKey() { return ibSettingsKey(ibSettingsCategory::Regional, ibGuid(kLocaleObject)); }

// The locale's generation, unique for the process rather than per base: a reader that keys a copy by the
// base's address and the generation (the translations of a thread, appHost.cpp) must tell a base made at a
// closed one's address from the one it knew.
std::atomic<unsigned> s_localeGenerations{ 0 };
unsigned NextLocaleGeneration() { return s_localeGenerations.fetch_add(1, std::memory_order_acq_rel) + 1; }

} // namespace

ibRegionalSettings::ibRegionalSettings(ib::AppDataCtorToken owner)
	: m_owner(owner.GetApplicationInstance())
{
	// Its holder takes connections from ITS base's pool - named once, here (the base builds the pool first),
	// never asked of "the current one": the lock manager's arrangement, for the lock manager's reason.
	m_holder.SetPool(ibApplicationInstance::GetConnectionPool(m_owner));
	m_localeGeneration.store(NextLocaleGeneration(), std::memory_order_release);
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
	const std::lock_guard<std::mutex> use(m_holderUse);
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
			*refusal = wxString::Format(_("The time zone '%s' could not be put on a connection to the base - its server does not know the name or has no session zone (Firebird 4 or later, PostgreSQL), or the connection was lost; the setting was not saved."), zone);
		return false;
	}
	ibDataNode node;
	node.SetValue(wxT("timeZone"), zone);
	if (!storage->Save(ZoneKey(), node, &m_holder)) {
		if (refusal != nullptr)
			*refusal = _("The time zone could not be written to the base.");
		return false;
	}
	{
		std::lock_guard<std::mutex> lock(m_mutex);
		m_timeZone = zone;
		m_read = true;
		++m_saves;
	}
	// In force at once: every other connection takes the zone as it is next handed out, and the clock is
	// measured in it - on this connection, which stands in it already.
	if (ibConnectionPool* const pool = ibApplicationInstance::GetConnectionPool(m_owner))
		pool->SetSessionTimeZone(zone);
	MeasureClock(*scope.get(), zone);
	return true;
}

wxString ibRegionalSettings::GetLocale()
{
	std::lock_guard<std::mutex> lock(m_mutex);
	return m_locale;
}

bool ibRegionalSettings::SetLocale(const wxString& locale, wxString* refusal)
{
	ibSettingsStorage* const storage = ibApplicationInstance::GetSettingsStorage(m_owner);
	if (storage == nullptr) {
		if (refusal != nullptr)
			*refusal = _("The base is not open; the locale was not saved.");
		return false;
	}
	// Under the canonical name with its region - the name the process gives of itself once it speaks it
	// (wxLocale::GetCanonicalName): "uk", "uk_UA", "uk-UA" and "Ukrainian" are all uk_UA (the translations'
	// one door for a name, ibTranslations::CanonicalLanguage). A name wx knows no language for is refused
	// here, in front of whoever is saving.
	wxString canonical;
	if (!locale.IsEmpty()) {
		canonical = ibTranslations::CanonicalLanguage(locale);
		if (canonical.IsEmpty()) {
			if (refusal != nullptr)
				*refusal = wxString::Format(_("'%s' names no language the platform knows (a name such as uk_UA, en_GB or ru_RU); the setting was not saved."), locale);
			return false;
		}
	}
	{
		// An OK that changed nothing writes nothing: the page compares what was typed, this what it means.
		std::lock_guard<std::mutex> lock(m_mutex);
		if (m_read && canonical == m_locale)
			return true;
	}
	const std::lock_guard<std::mutex> use(m_holderUse);
	const ibConnectionScope scope(&m_holder);
	if (!scope) {
		if (refusal != nullptr)
			*refusal = _("No connection to the base; the locale was not saved.");
		return false;
	}
	ibDataNode node;
	node.SetValue(wxT("locale"), canonical);
	if (!storage->Save(LocaleKey(), node, &m_holder)) {
		if (refusal != nullptr)
			*refusal = _("The locale could not be written to the base.");
		return false;
	}
	std::lock_guard<std::mutex> lock(m_mutex);
	m_locale = canonical;
	m_localeGeneration.store(NextLocaleGeneration(), std::memory_order_release);
	m_read = true;
	++m_saves;
	return true;
}

bool ibRegionalSettings::Refresh(ibDatabaseConnectionHolder* holder)
{
	ibSettingsStorage* const storage = ibApplicationInstance::GetSettingsStorage(m_owner);
	if (storage == nullptr)
		return false;
	ibDatabaseConnectionHolder* const through = holder != nullptr ? holder : &m_holder;
	// Its own holder one call at a time (a caller's holder is the caller's business).
	std::unique_lock<std::mutex> use(m_holderUse, std::defer_lock);
	if (holder == nullptr)
		use.lock();
	// One connection for the whole call, FIRST: the row is read through it and the zone put on it. The
	// registry's thread names its holder and works on the connection that holder keeps - a connection the
	// pool handed out long ago, before the zone was named, which no hand-out will put into it.
	const ibConnectionScope scope(through);
	if (!scope)
		return false;
	// A save on another thread between this read and what is done with it would be undone by it - the row read
	// before the save committed, put in force after - so what is read is taken only if no save came in between.
	unsigned long savesBefore = 0;
	{
		std::lock_guard<std::mutex> lock(m_mutex);
		savesBefore = m_saves;
	}
	// A read that FAILED - the base gone for a moment, a statement refused - is not "the base names nothing":
	// what is in force stays in force, and the next minute asks again. Taking a failure for an empty row would
	// take every connection out of the base's zone on a hiccup.
	ibDataNode zoneRow, localeRow;
	bool failed = false, localeFailed = false;
	const bool heldZone   = storage->Restore(ZoneKey(),   zoneRow,   through, &failed);
	const bool heldLocale = storage->Restore(LocaleKey(), localeRow, through, &localeFailed);
	failed = failed || localeFailed;
	const bool held = heldZone || heldLocale;
	{
		std::lock_guard<std::mutex> lock(m_mutex);
		const bool said = m_readFailing;
		m_readFailing = failed;
		if (failed && !said)
			ibJournalWarning(wxT("session.zone"), wxT("the base's regional settings could not be read; what is in force stays, and the next minute asks again"));
	}
	if (failed)
		return false;
	wxString zone, locale;
	if (heldZone)
		zoneRow.GetValue(wxT("timeZone"), zone);
	if (heldLocale)
		localeRow.GetValue(wxT("locale"), locale);

	bool first = false;
	wxString before, localeBefore;
	{
		std::lock_guard<std::mutex> lock(m_mutex);
		if (m_saves != savesBefore)
			return held;   // a save came in between: it is in force, and the next minute reads it back
		first = !m_read;
		before = m_timeZone;
		localeBefore = m_locale;
		m_read = true;
		m_timeZone = zone;
		if (m_locale != locale) {
			m_locale = locale;
			m_localeGeneration.store(NextLocaleGeneration(), std::memory_order_release);
		}
	}
	if (first ? !locale.IsEmpty() : localeBefore != locale) {
		if (!locale.IsEmpty())
			ibJournalInfo(wxT("session.locale"), wxT("the base names the locale %s"), locale);
		else
			ibJournalInfo(wxT("session.locale"), wxT("the base names no locale any more; the platform's stands"));
	}
	// Recorded for the pool at every read - the pool asks each connection its own zone at hand-out, and a zone
	// a hand-out was refused is tried again once it is recorded again, so once a minute rather than never.
	if (ibConnectionPool* const pool = ibApplicationInstance::GetConnectionPool(m_owner))
		pool->SetSessionTimeZone(zone);
	if (first || before != zone) {
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
	if (m_owner == nullptr)
		return;
	if (dialect.m_localTimestamp.IsEmpty()
		|| (connection.HasSessionTimeZone() && (zone.IsEmpty() || connection.GetSessionTimeZone() != zone))) {
		bool dropped = false;
		{
			std::lock_guard<std::mutex> lock(m_mutex);   // as below: only for the zone still in force
			if (m_timeZone == zone && m_owner->GetClockOffset() != 0) {
				m_owner->SetClockOffset(0);
				dropped = true;
			}
		}
		if (dropped)
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
	// measurement would run up to a second behind. The machine's reading is its own clock - never
	// ibDateTime::Now(), which is what this measurement moves.
	ibDateTime machine = ibDateTime::MachineNow();
	if (server.GetValue() % 1000 == 0)
		machine = ibDateTime(machine.GetValue() - machine.GetValue() % 1000);
	const long long measured = server - machine;
	long long before = 0;
	{
		// Kept only for the zone still in force - checked and kept under one lock, so a save that replaces the
		// zone cannot come between: a distance measured in the zone that was replaced is the distance of nothing.
		std::lock_guard<std::mutex> lock(m_mutex);
		if (m_timeZone != zone)
			return;
		// A step under two seconds is the servers' resolution and this machine's tick, not the clocks moving:
		// kept as it was, or Now() would step back and forth by a second from one minute to the next.
		before = m_owner->GetClockOffset();
		if (std::llabs(measured - before) < 2000)
			return;
		m_owner->SetClockOffset(measured);
	}
	// Said when it matters: a base whose clock stands a minute or more from this machine's is worth a line.
	if (std::llabs(measured - before) >= 60000)
		ibJournalInfo(wxT("session.clock"), wxT("the base's clock reads %lld ms from this machine's (was %lld)"), measured, before);
}

std::vector<wxString> ibRegionalSettings::KnownTimeZones()
{
	std::vector<wxString> zones;
	const std::lock_guard<std::mutex> use(m_holderUse);
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
