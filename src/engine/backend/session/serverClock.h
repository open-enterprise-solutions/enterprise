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
// The clock is not asked for every date. It is measured: once the base is up, and once a minute
// from the session registry's thread, this process asks the base what its server's local time is
// (the dialect's word for it - LOCALTIMESTAMP on Firebird and PostgreSQL, the process's own local
// time on SQLite, which has no server) and keeps the DIFFERENCE from the machine's clock. Now() is
// then the machine's clock plus that difference, as a wall-clock reading: the server's local time,
// on a client in any zone. Until a measurement succeeds the difference is zero and Now() is the
// machine's own clock, which is what it always was.
//
// ⭐ MEASURED IN THE BASE'S ZONE, OR NOT AT ALL. A server with a session zone reads its clock in
// whatever zone the session stands in - UTC on a Firebird attach, the server's own on PostgreSQL -
// and that is a reading of nothing for a base that has not said which zone it counts in. So on
// such a driver the clock is measured only once the base names its zone (the regional setting,
// session/regionalSettings.h), and through a connection standing in that zone: Refresh puts the
// connection it is given into the zone first. The difference is one of two wall readings, so a
// change of the clocks on either side between two measurements shows in Now() for at most the
// minute to the next one - which is why what must not be an hour off between two clients (the
// session registry's liveness) reads the server directly, through Read, where it compares.
class BACKEND_API ibServerClock {
public:

	// The reading the base's server shows right now.
	static wxLongLong_t Now();

	// Server minus this machine, in milliseconds, as of the last successful Refresh; 0 before one.
	static wxLongLong_t Offset();

	// The server's clock read now through `layer`, in the zone the connection stands in. False when
	// the dialect has no word for its clock, when a driver with a session zone stands in none (UTC
	// on a Firebird attach: nobody's clock for this base), or when the question failed.
	static bool Read(ibDatabaseLayer& layer, wxLongLong_t& reading);

	// Ask `layer` what its server's local time is - in `zone`, the base's - and remember the
	// difference. A driver with a session zone is asked only when `zone` names one, and `layer` is
	// put into it first (it is the caller's own connection, leased for the call); with no zone to
	// measure in, or a name the server refuses, the difference is dropped and Now() is the
	// machine's clock again. A driver with no session zone has the one clock it has. False when
	// nothing was measured; a question that merely failed leaves the difference as it was.
	static bool Refresh(ibDatabaseLayer& layer, const wxString& zone);

	// The difference is zero again: the base is closed.
	static void Reset();
};

#endif // __SERVER_CLOCK_H__
