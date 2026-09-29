#ifndef __REGIONAL_SETTINGS_H__
#define __REGIONAL_SETTINGS_H__

#include "backend/backend_core.h"

#include <vector>

class ibDatabaseConnectionHolder;

// ⭐⭐ THE BASE'S REGIONAL SETTINGS - ITS ZONE AND ITS LOCALE, KEPT IN THE BASE.
//
// A date is a wall-clock reading with no zone in it (fdate.h), and "now" is the server's clock
// (serverClock.h). What zone that clock stands in - what 09:00 in a document MEANS - is a property
// of the BASE: every client of it, wherever it sits, works in the base's zone, and the base's
// server does the zone arithmetic (its session is put into that zone, so LOCALTIMESTAMP and every
// conversion it makes read in it). The locale is the base's too: it is what a date or a number
// prints as when nothing narrower is asked for. Both are set on the base, in the designer, once;
// `backend.conf` names the PLATFORM's default locale, which stands for a base that names none.
//
// Kept as one shared row of sys_settings (ibSettingsCategory::Regional, no user). Read and put in
// force once the base is up and again once a minute (the session registry's thread, in every
// process), so a zone saved by one client reaches every other within the minute; the saving client
// has it at once. A base that names no zone is read by the machine's clock on Firebird and
// PostgreSQL (the clock is measured only in a named zone - serverClock.h).
//
// Every statement here runs on a connection of the calling thread's own - an ibConnectionScope
// on its holder, which is the connection that holder already has (the session registry's) or one
// leased from the pool for the call - never on the pool's shared primary, which another thread
// may be holding.
struct BACKEND_API ibRegionalSettings {

	wxString m_timeZone;   // an IANA name, "Europe/Kyiv"; empty = the base names none
	wxString m_locale;     // a BCP 47 tag, "uk-UA"; empty = the platform's default (backend.conf)

	// What is written in the base (the defaults when nothing is), and what is in force - by value,
	// because it is read from any thread (the assistant's, the registry's) while another saves it.
	static ibRegionalSettings Load();
	static ibRegionalSettings Current();

	// Written to the base and in force at once: the zone goes on this thread's connection FIRST - a
	// name the base's server refuses is refused here, `refusal` says why, and nothing is written -
	// then it is recorded for every connection the pool hands out from now on, and the clock is
	// measured in it. A driver without a session zone keeps the name and applies nothing.
	static bool Save(const ibRegionalSettings& settings, wxString* refusal = nullptr);

	// Read the base's row and put it in force: the zone on this thread's connection and recorded
	// for the pool's, the clock measured in it. Once the base is up, and once a minute after, on
	// the session registry's own holder. What changed - and why "now" is the machine's clock when
	// it is - goes to the journal once. False when the base holds no row; a read that FAILED
	// leaves everything in force as it was, and answers false too.
	static bool ApplyFromBase(ibDatabaseConnectionHolder* holder = nullptr);

	// The zones this base's server knows by name (the dialect's word for the list: Firebird and
	// PostgreSQL have one); empty on an engine that cannot say.
	static std::vector<wxString> KnownTimeZones();

	// The locale in force: the base's, else the platform's default from backend.conf, else empty
	// (the process's own).
	static wxString EffectiveLocale();

	// Forgotten: the base is closed.
	static void Reset();
};

#endif // __REGIONAL_SETTINGS_H__
