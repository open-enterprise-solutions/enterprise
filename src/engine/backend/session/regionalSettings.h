#ifndef __REGIONAL_SETTINGS_H__
#define __REGIONAL_SETTINGS_H__

#include "backend/backend_core.h"
#include "backend/appDataCtorToken.h"
#include "backend/databaseLayer/connectionHolder.h"

#include <mutex>
#include <vector>

// ⭐⭐ THE BASE'S TIME ZONE - named on the base, kept in the base, in force on every connection to it.
//
// A date is a wall-clock reading with no zone in it (core/fdatetime.h). What zone the readings of a base
// stand in - what 09:00 in its documents MEANS - is a property of the BASE: every client of it, wherever
// it sits, works in the base's zone, and the base's server does the zone arithmetic. So the zone is
// named once, on the base (the designer's settings), kept as one shared row of sys_settings
// (ibSettingsCategory::Regional, no user), and put on every connection the base's pool hands out
// (Firebird and PostgreSQL: SET TIME ZONE). A base that names none leaves each engine in its own: UTC on
// a Firebird attach, the server's setting on PostgreSQL.
//
// One per base, owned by it like the lock manager: several bases in one process are several zones. It
// reads and writes through a connection of its base - its own holder on the base's pool, or the holder
// a caller names (the session registry's, whose thread must not wait on the pool) - never through
// whatever base the calling thread happens to work for.
class BACKEND_API ibRegionalSettings {
public:
	explicit ibRegionalSettings(ib::AppDataCtorToken owner);

	// The zone in force: an IANA name, "Europe/Kyiv"; empty = the base names none.
	wxString GetTimeZone();

	// Named and kept: the zone goes on this object's connection FIRST - a name the base's server refuses is
	// refused here, `refusal` says why, and nothing is written - then the row is written, and the pool puts
	// every connection into it as it is next handed out. On a driver without a session zone the name is
	// kept and applies nothing.
	bool SetTimeZone(const wxString& zone, wxString* refusal = nullptr);

	// The base's row read and put in force - once when the base opens, and once a minute after from the
	// session registry's thread on its own holder, so a zone saved by one process reaches every other
	// within the minute. What changed is said in the journal once. A read that FAILED leaves everything in
	// force as it was; false then, and when the base holds no row.
	bool Refresh(ibDatabaseConnectionHolder* holder = nullptr);

	// The zones this base's server knows by name (the dialect's word for the list: Firebird and PostgreSQL
	// have one); empty on an engine that cannot say, which the designer's page reads as "type it".
	std::vector<wxString> KnownTimeZones();

private:
	class ibApplicationInstance* const m_owner;
	ibSingleConnectionHolder           m_holder;   // a connection of THIS base's pool, for its own calls
	std::mutex                         m_mutex;
	wxString                           m_timeZone;
	bool                               m_read = false;   // Refresh has read the row once
};

#endif // __REGIONAL_SETTINGS_H__
