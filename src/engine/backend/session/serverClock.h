#ifndef __SERVER_CLOCK_H__
#define __SERVER_CLOCK_H__

#include "backend/backend_core.h"   // wxLongLong_t - a date's reading (fdate.h)

class ibDatabaseLayer;

// ⭐⭐ "NOW" IS THE SERVER'S CLOCK.
//
// A base has one clock: the one its server reads. Every client that writes a date it took from
// "now" - a document's date, a session's heartbeat, a job's last run, a setting's change - writes
// what THAT clock shows, or two clients an hour apart write two different "nows" into one base and
// nobody can order what they wrote (the user's rule: there is the server's time and there is the
// client's, and the client follows the server).
//
// The clock is not asked for every date. It is measured: at connection, and once a minute from the
// session registry's heartbeat, this process asks the base what its server's local time is (the
// dialect's word for it - LOCALTIMESTAMP on Firebird and PostgreSQL, the process's own local time
// on SQLite, which has no server) and keeps the DIFFERENCE from the machine's clock. Now() is then
// the machine's clock plus that difference, as a wall-clock reading: the server's local time, on a
// client in any zone. Until a measurement succeeds the difference is zero and Now() is the machine's
// own clock, which is what it always was.
//
// What the reading MEANS - which zone the server's local time is in - is the base's regional
// setting, applied to the connection; that is the next step, not this one.
class BACKEND_API ibServerClock {
public:

	// The reading the base's server shows right now.
	static wxLongLong_t Now();

	// Server minus this machine, in milliseconds, as of the last successful Refresh; 0 before one.
	static wxLongLong_t Offset();

	// Ask `layer` what its server's local time is and remember the difference. False - and the
	// difference left as it was - when the dialect has no word for it or the question failed.
	static bool Refresh(ibDatabaseLayer& layer);

	// The difference is zero again: the base is closed.
	static void Reset();
};

#endif // __SERVER_CLOCK_H__
