# The date model

**What.** A date is a wall-clock reading: the parts a calendar and a clock show -
`2026-03-29 02:30:00` - and no zone. In the engine it is `ibDateTime` (`core/fdatetime.h`), the
engine's own type beside `ibNumber` and `ibString`: 8 bytes, milliseconds from `0001-01-01 00:00:00`
of the proleptic Gregorian calendar, counted by integer arithmetic that never asks the machine what
its clock says or which zone it stands in. The same parts give the same number on every machine, and
the same number gives the same parts. `wxDateTime` stays at the windows.

**Why.** A `TIMESTAMP` is a wall-clock reading, and so is the reference system's date. A value that
holds an instant instead - real time read through the machine's zone - reads the same column
differently on two clients an hour apart, is empty on one clock only, and cannot hold a time the
machine's clock skips (02:30 on the morning it goes forward).

The model began in pull request #217: a calendar without a clock, a date as a wall-clock reading,
drivers by parts, tests under three clocks.

## The reading

- **Zero is the empty date.** `ibDateTime()` is `0001-01-01 00:00:00`, all-zero bits. `Date(1, 1, 1)`
  is the empty date on every machine.
- **Every day is 86 400 seconds.** `Date(2026, 3, 29) + 86400` is the 30th at midnight on the
  morning the clocks go forward too. A difference of two dates is seconds on the wall; a date read
  as a number is seconds from the empty date.
- **Only through its API.** Nothing converts to or from it implicitly - not a number, not a
  `wxDateTime`. The count goes in and out only where a date is stored as a number (`GetValue`, the
  explicit constructor); everything else asks the date.
- **The calendar is the date's.** The parts (`ToParts`: weekday with Monday = 1, day of the year,
  ISO week), the periods (`BeginOfPeriod`, `EndOfPeriod`, `AddPeriods`, `PeriodsUntil`) and the parts
  a query takes (`GetPart`) are methods of the date. A script's `BegOfMonth`, a query's
  `BEGINOFPERIOD` folded in memory and a job's schedule ask the same method, so they cannot disagree.
- **The hot path is inline.** Order, equality, shift and span are one integer operation, `constexpr`
  in the header. Only the division into parts and the text live in `fdatetime.cpp`.

## The doors

- **Parts.** `FromParts` checks them against the calendar: a 30th of February is refused, never
  rolled over into March. A script's `Date(y, m, d, …)` comes this way.
- **Text.** One door, `FromString`: `dd.mm.yyyy hh:mm:ss`, `yyyymmddhhmmss`, `yyyymmdd` and ISO 8601
  are read by their digits; any other spelling goes to wx's free-form reader and comes back by its
  local parts. `ToString` writes `dd.mm.yyyy hh:mm:ss`.
- **The database.** A driver writes and reads a date by its parts - `SetParamDate` takes an
  `ibDateTime`, `GetResultDate` gives one. Firebird encodes a `struct tm`, SQLite and PostgreSQL spell
  the parts the ISO way, ODBC fills `TIMESTAMP_STRUCT`. No clock stands on that road. `NULL` reads as
  the empty date, and `IsFieldNull` tells the two apart. Firebird, SQLite and PostgreSQL store to the
  second.
- **Now.** `ibDateTime::Now()`: the clock of the base the calling thread works for, by its parts, to
  the millisecond - the machine's clock moved by the distance measured to the base's (below). The one
  door "now" is asked through.
- **The bridge.** `OfWxDateTime` / `ToWxDateTime` carry a reading to a `wxDateTime` and back by its
  local parts - for a window (a picker, a list that prints a time) and for a stored form older than
  this model. The empty date and an invalid `wxDateTime` are each other's. The bridge cannot hold a
  skipped hour, which is why nothing of the value's goes over it.

## Stored forms

A stored date is the count itself, under a version that says so: a data node (format 3), a data
dump's date form (chunk 3), a job's schedule blob (version 3); the AOT cache regenerates. Older
versions are still read: #217's count from 1970 (version 2) as the same parts, and an instant of the
writing machine (version 1) through the bridge - right wherever the reader stands in the writer's
zone, off by the difference elsewhere. The old empty literal reads as the empty date. A dump whose
date form this build does not know is refused, not guessed.

## Time on the server

Jobs, sessions, locks and settings keep `ibDateTime`. Where a span has to be real time - a session's
liveness, a job's last run - it is `ElapsedSince`, measured through this machine's zone, so a change
of the clocks between two readings is not an hour of silence. A session's heartbeat and the watch on it
are readings of the MACHINE's clock (`MachineNow`): the base's clock is the machine's moved by a distance
each process measures once a minute, and the minute it steps a peer's beat would read as hours of silence.

## The base's zone, clock and locale

- **The zone belongs to the base.** It is named once, on the base (the designer's settings, *Time
  zone*), kept as one shared row of `sys_settings`, and put on the connections the base's pool hands
  out - Firebird `SET TIME ZONE`, PostgreSQL `set_config('TimeZone', …)` - so what asks the server for
  its clock or for a zone's conversion reads in the base's zone. Dates themselves are bound with no zone,
  and the engine's own statements do not ask it; the measurement below does, and so may SQL a script
  writes. A base that names none leaves each engine in its own: UTC on a Firebird attach, the server's
  setting on PostgreSQL. Each process reads the row when the base opens and once a minute
  after; a zone named in one reaches every other within the minute (`session/regionalSettings.h`).
- **"Now" is the base's clock.** Each time the zone is read the server is asked its local time
  (`LOCALTIMESTAMP`; on PostgreSQL the statement's moment) through a connection standing in the base's zone, and the distance from this
  machine's clock is kept; `Now()` is the machine's reading moved by it. Core knows no base, so the
  distance comes through `ibDateTime::SetNowOffset`, which the backend fills with "the base this thread
  works for": the session's, the one it is bound to, or the one whose own service runs on it (the
  session registry, the job tick). A thread that works for no base has the machine's clock.
- **In the base's zone or not at all.** A server reads its clock in whatever zone the session stands
  in, which is nobody's clock for a base that names none: there nothing is measured and "now" is the
  machine's clock. (SQLite has no server and no session zone; its clock is the process's own, so the
  distance measured there is nothing.) A step under two seconds is the servers' resolution, not the
  clocks moving, and is not taken. The distance is one of two wall readings, so a change of the clocks on either side
  shows for at most the minute to the next measurement.
- **The zone rules** are the ones laid out beside the engine (`_fb/tzdata`); without them the ICU
  built into Firebird's kit knows 2018's rules and counts a newer name (`Europe/Kyiv`) as UTC.
- **The locale belongs to the base too.** It is named beside the zone (*Regional settings*), in a row of
  its own, under the canonical name with its region (`uk_UA` - the name a process gives of itself; `uk`,
  `uk-UA` and `Ukrainian` are all that); empty is the platform's (`backend.conf`, `--locale`). What the
  base names is what the processes opened on it come up in - the designer, the thick client, a launcher's
  child, and a thin client on a file base, the engine running in its process; over an application server
  a thin client has no locale of its own (its windows are not translated), and the server speaks the first
  base's. `wxLocale` is process state, settled by the first base a process opens, so a process holding
  several bases speaks the first one's, and a base that names another is said in the journal. A locale
  this machine cannot grant - not generated on a Linux - does not refuse the base: the process comes up
  in the machine's own, and the journal says which wish went ungranted. A base asked for its own locale
  (`ibApplicationInstance::GetLocale`, `platform_state`) answers what it names, whatever the process
  speaks.

## Where it stops

- The zone is not in the value: a reading is what the base's clock showed, in the base's zone.
- The locale a base names reaches `_()` - the engine's own words - only through the process it settles:
  a process of several bases translates in the first one's. A translation per base would need a set of
  catalogs chosen by the thread's base, which is a decision of its own.
- Liveness is the machine's clock read through the machine's zone, as before the base had a clock: two
  processes of one base on machines in different zones still read each other's heartbeats hours apart.
  A zone-free reading for the heartbeat (UTC, compared by subtraction) is the change that would close it.
- A reading before year 1 or past 9999 is counted and printed, by the general formatter.

## Tested across clocks

CTest runs the date suites three more times: under `TZ=UTC0`, two hours east of it with a summer
name, and one hour east with one (the C runtime reads TZ in its POSIX spelling on every platform,
Windows included). A base written under one clock and read under another runs as two fixtures.
