#ifndef __REGIONAL_SETTINGS_H__
#define __REGIONAL_SETTINGS_H__

#include "backend/backend_core.h"
#include "backend/appDataCtorToken.h"
#include "backend/databaseLayer/connectionHolder.h"

#include <mutex>
#include <vector>

// ⭐⭐ THE BASE'S TIME ZONE - named on the base, kept in the base, put on the connections its pool hands out.
//
// A date is a wall-clock reading with no zone in it (core/fdatetime.h). What zone the readings of a base
// stand in - what 09:00 in its documents MEANS - is a property of the BASE: every client of it, wherever
// it sits, works in the base's zone. So the zone is named once, on the base (the designer's settings),
// kept as one shared row of sys_settings (ibSettingsCategory::Regional, no user), and put on the
// connections the base's pool hands out (Firebird SET TIME ZONE, PostgreSQL set_config), so what asks the
// server for its clock or a zone's conversion reads in it. Dates are bound with no zone, and the engine's
// own statements do not ask; the measurement below does. A base that names none leaves each engine in its
// own: UTC on a Firebird attach, the server's setting on PostgreSQL.
//
// ⭐ "NOW" IS THE BASE'S CLOCK. Every client that writes a date it took from "now" - a document's date, a
// job's last run - writes what the base's clock shows, or two clients in two zones write two "nows" into
// one base and nobody can order them. (A session's heartbeat is not such a date: it is the machine's clock,
// MachineNow, so that a distance stepping between two measurements is not read as silence.) The clock is not asked for every date: it is
// MEASURED here - each time the zone is read (once when the base opens, once a minute after) the server is
// asked its local time through a connection standing in the base's zone - and ibDateTime::Now() is this
// machine's reading moved by the difference (appHost.cpp puts the source of it into core). Measured in the
// base's zone or not at all: a server with a session zone reads its clock in whatever zone the session
// stands in - UTC on a Firebird attach - which is nobody's clock for a base that names none; there the
// difference is zero and "now" is the machine's clock, as before.
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
	// The server's clock read through `connection`, standing in `zone`, against this machine's; the difference
	// kept. Zero where nothing can be measured; a question that merely failed keeps the last measurement.
	void MeasureClock(class ibDatabaseLayer& connection, const wxString& zone);

	class ibApplicationInstance* const m_owner;
	ibSingleConnectionHolder           m_holder;      // a connection of THIS base's pool, for its own calls...
	std::mutex                         m_holderUse;   // ...one call at a time: one holder is one bound connection
	std::mutex                         m_mutex;
	wxString                           m_timeZone;
	bool                               m_read = false;        // Refresh has read the row once
	bool                               m_readFailing = false; // the last read of the row failed (said once)
	unsigned long                      m_saves = 0;           // SetTimeZone calls that wrote: a read begun before one is stale
};

#endif // __REGIONAL_SETTINGS_H__
