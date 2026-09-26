////////////////////////////////////////////////////////////////////////////
//	Description : the base's regional settings - its zone and its locale (regionalSettings.h)
////////////////////////////////////////////////////////////////////////////

#include "regionalSettings.h"
#include "serverClock.h"

#include "backend/appData.h"
#include "backend/backend_exception.h"
#include "backend/databaseLayer/connectionPool.h"
#include "backend/databaseLayer/connectionScope.h"
#include "backend/databaseLayer/databaseLayer.h"
#include "backend/databaseLayer/databaseResultSet.h"
#include "backend/diagnostics/journal.h"
#include "backend/serialize/dataBuilder.h"
#include "backend/settings/settingsStorage.h"

#include <memory>
#include <mutex>

namespace {

// The one row: a fixed object under the category, shared by everybody (no user in the key).
const wxChar* const kRegionalSettingsObject = wxT("72656769-6f6e-616c-0000-000000000001");

ibSettingsKey KeyOfTheBase()
{
	return ibSettingsKey(ibSettingsCategory::Regional, ibGuid(kRegionalSettingsObject));
}

std::mutex         s_mutex;
ibRegionalSettings s_current;
bool               s_applied = false;   // ApplyFromBase has run once since the base came up

void SetCurrent(const ibRegionalSettings& settings)
{
	std::lock_guard<std::mutex> lock(s_mutex);
	s_current = settings;
}

} // namespace

ibRegionalSettings ibRegionalSettings::Load()
{
	ibRegionalSettings settings;
	ibSettingsStorage* storage = ibApplicationData::GetSettingsStorage();
	if (storage == nullptr)
		return settings;
	const ibConnectionScope scope;   // this thread's own connection for the read
	ibDataNode node;
	if (!scope || !storage->Restore(KeyOfTheBase(), node, scope.Holder()))
		return settings;
	node.GetValue(wxT("timeZone"), settings.m_timeZone);
	node.GetValue(wxT("locale"), settings.m_locale);
	return settings;
}

bool ibRegionalSettings::Save(const ibRegionalSettings& settings, wxString* refusal)
{
	ibSettingsStorage* storage = ibApplicationData::GetSettingsStorage();
	if (storage == nullptr) {
		if (refusal != nullptr)
			*refusal = _("No base is open; the regional settings were not saved.");
		return false;
	}
	// This thread's own connection for the call: the row is written through it, the zone is put on
	// it, and the clock is measured through it. Nobody else holds it meanwhile.
	const ibConnectionScope scope;
	if (!scope) {
		if (refusal != nullptr)
			*refusal = _("No connection to the base; the regional settings were not saved.");
		return false;
	}
	// The zone FIRST: the server's refusal of the name is heard here, in front of whoever is saving,
	// and nothing is written under a name the base cannot count in. A driver without a session zone
	// has nothing to refuse it with; the name is kept.
	if (scope->HasSessionTimeZone() && !scope->SetSessionTimeZone(settings.m_timeZone)) {
		if (refusal != nullptr)
			*refusal = wxString::Format(_("The base's server refused the time zone '%s' - it does not know the name, or it has no session zone (Firebird 4 or later, PostgreSQL); the setting was not saved."), settings.m_timeZone);
		return false;
	}
	ibDataNode node;
	node.SetValue(wxT("timeZone"), settings.m_timeZone);
	node.SetValue(wxT("locale"), settings.m_locale);
	if (!storage->Save(KeyOfTheBase(), node, scope.Holder())) {
		if (refusal != nullptr)
			*refusal = _("The regional settings could not be written to the base.");
		return false;
	}
	SetCurrent(settings);
	// In force at once: every other connection takes the zone as it is next handed out, and the
	// clock is measured in it - on this connection, which stands in it already.
	if (ibConnectionPool* pool = ibApplicationData::GetConnectionPool())
		pool->SetSessionTimeZone(settings.m_timeZone);
	ibServerClock::Refresh(*scope.get(), settings.m_timeZone);
	return true;
}

ibRegionalSettings ibRegionalSettings::Current()
{
	std::lock_guard<std::mutex> lock(s_mutex);
	return s_current;
}

bool ibRegionalSettings::ApplyFromBase(ibDatabaseConnectionHolder* holder)
{
	ibSettingsStorage* storage = ibApplicationData::GetSettingsStorage();
	if (storage == nullptr)
		return false;
	// This thread's own connection for the whole call, FIRST: the row is read through it, the zone
	// put on it, the clock measured through it. The registry's thread names its holder and works
	// on the connection that holder already has; the opening thread leases one from the pool.
	const ibConnectionScope scope(holder);
	if (!scope)
		return false;
	// A read that FAILED - the base gone for a moment, a statement refused - is not "the base names
	// nothing": what is in force stays in force, and the next minute asks again. Taking a failure
	// for an empty row would put this process on the machine's clock, and its heartbeats an hour
	// from its peers' where the machine stands in another zone - which is how peers sweep a live
	// session.
	ibDataNode node;
	bool failed = false;
	const bool held = storage->Restore(KeyOfTheBase(), node, scope.Holder(), &failed);
	if (failed)
		return false;
	ibRegionalSettings settings;
	if (held) {
		node.GetValue(wxT("timeZone"), settings.m_timeZone);
		node.GetValue(wxT("locale"), settings.m_locale);
	}
	ibRegionalSettings before;
	bool first = false;
	{
		std::lock_guard<std::mutex> lock(s_mutex);
		before = s_current;
		first = !s_applied;
		s_applied = true;
		s_current = settings;
	}
	const bool changed = first || before.m_timeZone != settings.m_timeZone;
	// Recorded for the pool whenever it changed - the pool asks the server itself at every hand-out,
	// so this needs no success of its own.
	if (changed)
		if (ibConnectionPool* pool = ibApplicationData::GetConnectionPool())
			pool->SetSessionTimeZone(settings.m_timeZone);
	// What the clock is measured by, said in the journal when it changes, with the cause: a zone
	// the driver cannot take, a zone the connection could not be put into, or no zone named at all
	// - in each of those "now" is this machine's clock, and the setting that would change it is named.
	if (!scope->HasSessionTimeZone()) {
		if (changed && !settings.m_timeZone.IsEmpty())
			ibJournalInfo(wxT("session.clock"), wxT("the base names the zone %s, and this driver has no session zone to put it in; 'now' is this machine's clock"),
				settings.m_timeZone);
	}
	else if (settings.m_timeZone.IsEmpty()) {
		if (changed)
			ibJournalInfo(wxT("session.clock"), wxT("the base names no time zone; its server's clock is not measured, and 'now' is this machine's (the designer's regional settings name one)"));
	}
	else if (scope->GetSessionTimeZone() != settings.m_timeZone && !scope->SetSessionTimeZone(settings.m_timeZone)) {
		if (changed)
			ibJournalError(wxT("session.clock"), wxT("the base names the zone %s, and this connection could not be put into it (the server refused the name, or the connection was lost); the clock is not measured, and 'now' is this machine's"),
				settings.m_timeZone);
		ibServerClock::Reset();   // not measured in a zone the connection does not stand in
		return held;
	}
	ibServerClock::Refresh(*scope.get(), settings.m_timeZone);
	return held;
}

std::vector<wxString> ibRegionalSettings::KnownTimeZones()
{
	std::vector<wxString> zones;
	const ibConnectionScope scope;
	if (!scope || !scope->HasSessionTimeZone())
		return zones;
	// The dialect's word for the list - an engine without one answers with nothing, which the
	// page reads as "type it".
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

wxString ibRegionalSettings::EffectiveLocale()
{
	{
		std::lock_guard<std::mutex> lock(s_mutex);
		if (!s_current.m_locale.IsEmpty())
			return s_current.m_locale;
	}
	const ibApplicationData* app = ibApplicationData::Get();
	return app != nullptr ? app->GetPlatformLocale() : wxString();
}

void ibRegionalSettings::Reset()
{
	std::lock_guard<std::mutex> lock(s_mutex);
	s_current = ibRegionalSettings();
	s_applied = false;
}
