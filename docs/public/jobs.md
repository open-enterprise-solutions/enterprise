# Jobs

**What.** A job is script or engine work that runs on the server outside the session that asked for it,
in a session of its own. Three kinds come into one manager:

| Kind | Declared by | Runs |
|---|---|---|
| Platform | the engine, as a base opens: folding register totals, Firebird's sweep and backup | by its schedule |
| Scheduled | the configuration: a **predefined job** (`PredefinedJob`, under Common - the metaobject is the job) or a **parameterized job** (`ScheduledJob` - a reference whose every row is a job with its own schedule) | by its schedule, or `Execute` by hand |
| Background | a script, `RunBackground("Module.Method", args, key, description)` | once, now |

**Why.** Housekeeping, an overnight recalculation and a form's heavy read all want their work off the
thread that asked. They differ only in who decides when, and whether the work outlives whoever started
it; one manager gives all of them one claim, one journal and one list. A predefined job serves the
configuration, a parameterized one serves the data: ten near-identical predefined jobs should have been
ten rows.

## The schedule

- **A calendar and an interval, joined by AND.** The calendar says which moments qualify: a time window
  (it may wrap midnight), days of the week, days of the month and from its end, months, "the second
  Tuesday", every N weeks or months from an anchor, a validity range, a time too late to begin. The
  interval says how long since the last run. "Every 10 minutes" is an interval; "nightly" is 24 hours in a
  02:00-05:00 window. An empty day list means any day.
- **A time of day means "not before".** A machine that was off at 02:00 and starts at 07:00 runs the job at
  07:00; a first run with a time of day does not wait out the interval.
- **The calendar reads the wall clock, the interval a steady one**, so moving the system clock cannot run
  a job twice or stall it.
- **A schedule that can never match is refused** when it is declared, not left to be found as silence.
- **Retries are the author's.** `RetryCount` (0 unless set) and `RetryInterval`: work that failed is
  repeated only where the author says it is safe to do twice.

## From a script

```
job = RunBackground("Exchange.Upload", parameters, "upload", "Upload to the warehouse");
job.Wait(5000);          // milliseconds; 0 waits until it ends
result = job.Result();   // job.Error() when it failed; job.State, job.Begin, job.End
job.Cancel();            // asks; the run stops at its next loop boundary
jobs = BackgroundJobs.GetBackgroundJobs(New Structure("Key, State", "upload", BackgroundJobState.Active));
```

- **The method is `Module.Method`**, a public method of a common module: the call is made by name in
  another session, so the module has to be in the name.
- **A key is a guard.** While a run of a method under a key is active, a second start under the same key
  is refused; the check and the holding are one step.
- **The run is the caller's.** It takes the caller's identity and sees what the caller sees, row filters
  included; no password is asked again.
- `RunJob(name)` starts a scheduled job now, whatever its schedule; `RunScheduledJobs()` forces a round.

## What it guarantees

- **Only what the caller does not own crosses.** Numbers, strings, dates, references, arrays,
  structures, maps and value tables go to the job. A loaded object, a record set, a tabular section, a
  manager, a form, a lambda or an iterator is refused at the call, naming the argument: copy the data out
  (`Unload`) or pass the reference, which the job reads again under its own rights.
- **One run across the servers.** The jobs start with the base, not with its first user. Several servers
  on one base each claim a due job in `sys_lock` and read its last run from `sys_job`: the first runs it,
  the rest record it as done. A run still going is skipped, not queued again.
- **Every run says how it ended.** The manager, not the job, writes a line to the base's journal:
  finished, failed with what was thrown, cancelled - and a run that left its body by any other path says
  that too.
- **Runs are found again.** The active ones and the last 256 that ended: `GetBackgroundJobs`,
  `FindByUUID`, and a scheduled job's `LastJob` - the pass in flight, or the last one that did its work.
- **An administrator tells them apart.** A background, a scheduled and a system job are three session
  kinds in the active-users list: a user waits for the first, the second comes back on its interval, the
  third is housekeeping.
- Dropping the job value cancels nothing; the run finishes on its own.

## Where it stops

- A scheduled job runs as nobody: no row filter is built, and it sees every row. Work that must see what
  one user sees is started by that user, as a background job.
- A run cannot be started under another user's name.
- `Cancel` is a request. A run blocked inside the database does not reach its next loop boundary and is
  not interrupted by it.
- One schedule has one time window a day; two windows are two jobs.
