#ifndef __TIME_ZONE_H__
#define __TIME_ZONE_H__

////////////////////////////////////////////////////////////////////////////
// ibTimeZone — A ZONE BY ITS IANA NAME ("Europe/Kyiv"), AND THE ONE DOOR A READING CROSSES ZONES THROUGH.
////////////////////////////////////////////////////////////////////////////
//
// A date in the engine is a wall-clock reading with no zone (fdatetime.h): what was entered is what is kept.
// A zone is asked only where a reading has to be told in another one - "now" in a session's zone, a time
// exchanged as universal time. The rules (offsets, summer time, their history) are the IANA database's: on
// Windows the C++20 std::chrono::tzdb (from Windows' own ICU), on Linux and macOS Howard Hinnant's date
// library reading the system's zoneinfo (the tzdata package), and past a file's last transition its own rule.
// No rules are shipped.
//
////////////////////////////////////////////////////////////////////////////

#include "core/coreApi.h"
#include "core/fdatetime.h"
#include "core/fstring.h"

class CORE_API ibTimeZone {
public:

	// Universal time, UTC.
	ibTimeZone() = default;

	// The zone of that name - false, the zone left as it was, for a name the rules have not. A zone renamed in
	// the database is found by its old name and its new one alike (Europe/Kiev, Europe/Kyiv): Windows names
	// some zones by the old one, a stock Linux knows only the new.
	bool FromName(const ibString& name);

	// The zone this machine runs in - on Linux and macOS a TZ that names a zone first; else the system's zone
	// (/etc/localtime; on Windows the system's setting - TZ is not read there); UTC when it names none the rules
	// have.
	static ibTimeZone Machine();

	const ibString& GetName() const { return m_name; }

	// A reading in universal time told on this zone's clock, and back. The empty date is the empty date
	// either way. A reading the clocks skip (02:30 on the morning they go forward) is the moment they skip
	// at; a reading they show twice (the hour they go back) is the first of the two. A reading the zone would
	// put before year 1 is its first millisecond.
	ibDateTime ToLocal(const ibDateTime& universal) const;
	ibDateTime ToUniversal(const ibDateTime& local) const;

private:
	struct Rules;                     // the zone's rules, found once and kept for the process (timeZone.cpp)
	const Rules* m_rules = nullptr;   // none: universal time
	ibString m_name = ibT("UTC");
};

#endif
