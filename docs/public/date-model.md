# The date model

**What.** A date value is a wall-clock reading: the parts a calendar and a clock show -
`2026-03-29 02:30:00` - and nothing else. No zone. Inside the engine it is counted as
milliseconds from `1970-01-01 00:00:00` of that same calendar (`backend/fdate.h`:
`ibWallFromParts`, `ibWallToParts`, `ibDaysFromCivil`), by proleptic Gregorian arithmetic that
never asks the machine what its clock says or which zone it stands in. The same parts give the
same number on every machine; the same number gives the same parts. "Now" is the base's
server's clock, and which zone that clock stands in is a setting of the base.

**Why.** A `TIMESTAMP` is a wall-clock reading, and so is the reference system's date. A value
that holds an instant instead - real time read through the machine's zone - means the same
column reads differently on two clients an hour apart, the empty date is empty on one clock
only, and a time the machine's clock skips (02:30 on the morning it goes forward) cannot be
held at all. Every one of those was measured on a base opened from two zones.

## The reading

- Every day is 86 400 seconds on this calendar. `Date(2026, 3, 29) + 86400` is the 30th at
  midnight on the morning the clocks go forward too; a difference of two dates is a count of
  seconds on the wall.
- The **empty date** is `0001-01-01 00:00:00` read this way: `emptyDate` (`backend_core.h`),
  a `constexpr` equal to `ibWallFromParts(1, 1, 1)`. `Date(1, 1, 1)` is the empty date on every
  machine, as the reference system's idiom expects.
- The day's place - weekday (Monday is 1), day of the year, ISO week - comes off the same parts
  (`ibDateParts`), so the script's `GetWeekOfYear` and the query's `WEEK` cannot disagree.
- The SQL calendar functions - `BEGINOFPERIOD`, `ENDOFPERIOD`, `DATEADD`, `DATEDIFF`, the date
  parts - have one twin each in the engine, over the reading, so a period folded in memory is the
  period the server folds.

## The doors

A date comes in through four kinds of door, and each gives the reading:

- **Parts.** The value's constructor, the script's `Date(y, m, d, …)`, a query's `DATETIME(…)`.
  The parts are checked against the calendar first (`ibPartsAreADate`): a 30th of February is
  refused, or is the empty date, never rolled over into March.
- **Text.** One door, `ibWallOfText`: the reference system's forms (`dd.mm.yyyy hh:mm:ss`,
  `yyyymmddhhmmss`, `yyyymmdd`) and ISO 8601 as an engine writes a `TIMESTAMP`, read by their
  digits. Any other spelling goes to wx's free-form reader and comes back by its local parts.
- **The database.** A driver writes and reads a date by its parts: Firebird encodes a
  `struct tm`, SQLite and PostgreSQL spell the parts the ISO way, ODBC fills
  `TIMESTAMP_STRUCT`. No clock stands on that road, so the reading a column holds is the reading
  the value held - to the second: Firebird, SQLite and PostgreSQL write no milliseconds (as they
  never did); ODBC binds the parameter as its driver describes it, the fraction included where the
  driver takes one. A Firebird `DATE` is its day, a `TIME` its time of day on the empty date's day.
  `NULL` reads as the empty date, and `IsFieldNull` tells the two apart.
- **The bridge.** `ibWallOfDateTime` / `ibDateTimeOfWall` carry a reading to a `wxDateTime` and
  back by its *local parts*, for the edges that still hold an instant: a picker in a window, a
  job schedule, a heartbeat. It is the one road that cannot hold a skipped hour - 02:30 on a
  morning the machine's clocks go forward comes back 03:30 - which is why nothing of the value's
  goes over it.

A stored date - a value packed into a node, a property, a job's anchor, an AOT constant, a row of a
data dump - is the number itself, under a format version that says so (the node's stamp, the
schedule blob's byte, the dump's chunk); the AOT cache regenerates.

## "Now"

`CurrentDate()` is the base's clock: the server's local time, in the base's zone
(`backend/session/serverClock.h`). The clock is measured, not asked for every date - once the base
is up, and once a minute from the session registry's thread, the process asks the base for its
server's local time in the dialect's word for it (`LOCALTIMESTAMP` on Firebird and PostgreSQL; on
SQLite, which has no server, the process's own local time) and keeps the difference from the
machine's clock, compared at the resolution the server answered in. Firebird and PostgreSQL read
their clock in whatever zone the session stands in - UTC on a Firebird attach - so on them the
measurement is taken only once the base names its zone, and through a connection put into that zone
first; until then, when the zone is cleared again, and on a driver whose dialect has no word for its
clock, "now" is the machine's clock, as it always was. Every stamp the engine writes into the base -
a session's heartbeat, a job's registration and last run, a lock's acquisition, a user's or a
setting's change, the frontier of the current period - is taken from it; the process's own
diagnostic files (the journal, a crash dump) keep the machine's clock.

The difference is one of two wall readings, so a change of the clocks on either side between two
measurements - the machine's, or the base's zone's - shows in "now" for at most the minute to the
next measurement, and two processes re-measure at different seconds of their minute. What must not
be an hour off between two clients for even that long - the session registry's liveness, the
heartbeat one process writes and the cutoff another compares it with - is therefore read from the
server directly, through the registry's own connection in the base's zone, where it is compared
(`ibServerClock::Read`); with no zone named, each client's own clock stands there too, as it always
did. A holder that still keeps a `wxDateTime` (a job schedule, a heartbeat) turns the reading into
an instant by this machine's local parts, so a span it measures across the machine's own clock
change is an hour off; that is the bridge's edge, not the value's.

For the date a person is *posting under*, which may deliberately differ, there is `WorkingDate()`.

## The base's zone and locale

What the reading *means* - which zone `09:00` stands in - is a property of the base, set once in
the designer (Settings → Regional settings) and kept in the base as one shared row of
`sys_settings` (`backend/session/regionalSettings.h`). Once the base is up, the zone is put on
its connections: Firebird and PostgreSQL take a session zone (`SET TIME ZONE`; the attach stays
at UTC, so a name the server refuses refuses a statement and not the base), and the clock is
measured in it. The pool records the zone and puts each connection whose own zone differs into it
as it is next handed out, outside its lock; a Firebird reconnect puts the session's zone back, and
`Close` forgets it. Every statement of the settings' own runs on a connection of the calling
thread's own. A save goes to that connection first - a name the server refuses is refused there, in
front of the person, and nothing is written - and the saving process has the zone at once; every
other process reads the row again once a minute, from the session registry's thread on its own
connection, and has it within the minute. A read that fails leaves what is in force as it was.
SQLite and the ODBC baseline have no session zone and say so.

Name the zone once every client of the base runs this build: a client of an older build writes its
own machine's local time whatever the base names, and where that machine stands in another zone the
two disagree by the difference.

A date already written is never converted: a document dated `09:00` stays `09:00`. The zone
decides what the server's clock reads and how the server converts when a query asks it to.

The locale is the base's answer to "in what language and with which separators does a date or
a number print when nothing narrower is asked"; `backend.conf` keeps the platform's default
(`Locale`), which stands for a base that names none. `platform_state` reports the server's
"now", the offset from this machine, the zone and the locale.

## Where it stops

- A date serialised before this model was an instant of the writing machine's clock. Each stored
  form carries a version - a node's stamp, the schedule blob's byte, a data dump's chunk - and an
  older version is read through the bridge: the parts this machine's clock shows for the instant,
  which are the parts the writer saw wherever this machine stands in the writer's zone, and are
  off by the difference elsewhere. The old empty literal is the empty date. Dates in the database
  itself are unaffected: a `TIMESTAMP` was always the parts. The other way round, a build before
  this model refuses a version-2 schedule blob and keeps the job's defaults: rolling back loses
  stored schedules to their defaults.
- The ODBC baseline has no word for its server's clock and no session zone; on it "now" is the
  machine's clock and the base's zone is recorded, not applied.
- The locale is kept and reported; what reads it when a date or a number is formatted is the
  formatting work, not this model.

## Tested across clocks

CTest runs the date suites three more times, under `TZ=UTC0`, two hours east of it with a
summer name, and one hour east with one (the C runtime reads TZ in its POSIX spelling on every
platform, Windows included, and only that spelling). The hour a clock skips is checked on every
machine by a test that sets the American Eastern rule for its own length. A base written under
one clock is read under another as two CTest fixtures.
