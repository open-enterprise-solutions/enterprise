# Job manager — scheduled and background work

> **Status (2026-07-31):** built and running — steps 1–8 below. The engine exists, the platform's
> own jobs run on it (totals fold, Firebird maintenance), script starts background runs on it, and
> every paged table read is a RENTED run on it. Compiled Debug|x86 and exercised by hand; NOT
> committed. **The one kind still missing is the configuration-declared scheduled job**: the
> manager takes `ibJobOrigin::Configuration`, gives it a `ScheduledJob` session and a full
> calendar — what does not exist is the metadata that declares one and the read that turns a
> declaration into `Register` (§10 "Next", item 3).
>
> This file is the whole picture, so the next session does not have to re-derive it. Where
> something is a decision rather than code, it says so.

A job is script or engine work that runs **outside the thread that asked for it**. Three
things want that: the platform's own housekeeping, a user's "recalculate this overnight",
and a form that must not freeze while a heavy query runs.

They are one mechanism. What differs is who decides *when*, and whether the work outlives
whoever started it.

---

## 1. The one rule everything else follows

**A job may only receive what is not bound to the session that created it.**

The reason is concrete. A lambda value (`ibValueFunction`) holds `m_parentBc`, a pointer into
the **compiling session's** bytecode — and the comment on it is explicit: "The value must not
outlive the session that produced it" (`compiler/procUnitValues.h`). Bytecode is per-session:
`ibModuleStorage` keeps common-module **descriptors** (`metaData.h`), while the bytecode is
built by `CompileRoot`, which runs per session on the `Authenticated` phase (`appData.cpp`).

The same reasoning covers mutable values: handing a live object to another session means two
sessions mutating one object with no coordination. Immutable values are safe to share — the
process holds one memory space, so nothing needs serialising; it is ownership, not transport,
that is the problem.

So the gate is single and it is checked **at submission**, never at first touch inside the
job. In the background there is no caller left to raise the error to.

**The rule lives on the values, not on the job layer.** `ibValue::IsTransferable()` is virtual
and defaults to **true** — the overwhelming majority of values are fine, and a new value kind
should have to state its own case rather than be enumerated by a switch somewhere else.
`ibJobManager::FindNonTransferable` just asks every element of the argument array and reports
the first refusal.

The base implementation hops through `TYPE_REFFER` **and** `TYPE_CONST_REFFER` to the object
behind the alias, the same shape `Init()` uses. Without the hop the wrapper would answer on
behalf of what it points at — and a form, an object or a lambda is almost always reached
through one. Const-ness is not the question: a read-only alias to a mutable object still
aliases a mutable object.

| Crosses the session boundary | | Where it says so |
|---|---|---|
| number, string, date, boolean, reference, enum value | yes | the default |
| lambda | **no** | `ibValueFunction` — `m_parentBc` points into the compiling session's bytecode |
| iterator | **no** | `ibValueIterator` — a cursor inside somebody else's collection |
| OLE object | **no** | `ibValueOLE` — apartment-bound COM pointer |
| **value table**, array, structure, map | yes | self-contained runtime values — they travel with their own data |
| loaded object (document, catalog item) | **no** | `ibValueRecordDataObject` — mutable, holds a lock and an unfinished write |
| register record set | **no** | `ibValueRecordSetObject` — the register's records, keyed and edited up to `Write()` |
| object's tabular section | **no** | `ibValueTabularSectionDataObjectBase` — part of an open object, not a table of its own |
| **every manager** — catalog, document, register | **no** | `ibValueManagerObject` — looks stateless, is bound to the session's runtime, connection and RLS |
| record manager | **no** | `ibValueRecordManagerObject` — owns a record set being edited |
| form and every control | **no** | `ibValueFrame` — mutable, and bound to a wx widget owned by one thread |

**The line is OWNERSHIP, not mutability.** A value table is mutable and travels fine; so does
an array. What cannot travel is a value that is *part of something else which keeps changing* —
a register's record set, an object's tabular section — or one bound to the source session's
runtime. `ibValueModel` therefore does **not** override: being a collection proves nothing
either way, and refusing there would have grounded the value table for no reason.

Where a base does answer — `ibValueManagerObject`, `ibValueFrame`, `ibValueRecordDataObject` —
it is because every subclass shares the same ground, so a new register, a new control or a new
object kind inherits the answer instead of having to remember it.

The escape hatch is always the same shape: copy the data out. `Unload` on a tabular section
gives a value table, and that value table travels.

Pass the **data**, not the holder of it: a reference re-reads on the other side, under the
job's own rights.

**A refusal throws.** `CheckTransferable` raises `ibBackendCoreException` naming the argument
position and its class — passing a mutable value to a job is a programming error in the
configuration, and the author must see it at the call, on their own stack, where the script's
try/except is the natural handling point. A quiet `false` would leave a job that either never
runs or runs on values it must not touch, with nothing saying why.

---

## 2. The three kinds

One manager, three ways a job gets into it — and they differ by **who declares it**, which is
also what decides where its schedule is edited:

| | Declared by | Schedule lives in | Session kind |
|---|---|---|---|
| **Platform** | the engine, in `platformJobs.cpp`, when a database opens | code today; a designer-editable schedule later | `SystemJob` |
| **Scheduled** | the configuration, from a metaobject | the metadata, alongside the object that owns the work | `ScheduledJob` |
| **Background** | script, at the moment of the call | none — it runs once, now | `BackgroundJob` |

The manager itself does not know the difference beyond `ibJobDescription::m_origin`: all three
are a body, an interval (zero for background), and a session. That is the point — a
configuration's job and the engine's fold queue behind the same lease, obey the same
cross-process claim, and show up in the same list.

**Adding one is the same call from anywhere.** Registration belongs to the manager;
`ibApplicationData` is only the way to reach it:

```cpp
ibJobDescription desc;
desc.m_name            = wxT("my.job");
desc.m_body            = [](ibSession* s) { /* ... */ return false; };
desc.m_intervalSeconds = 600;
desc.m_origin          = ibJobOrigin::Configuration;

if (auto* jobs = ibApplicationData::GetJobManager())
    jobs->Register(std::move(desc));
```

`platformJobs.cpp` is not a privileged path — it is that same call, made for the engine's own
jobs at the point where a database opens. Anything else that wants a job registered does
exactly this, whenever it makes sense for it to.

By runtime behaviour rather than by origin:

| | Scheduled | Outlives its initiator | Body | Result goes to |
|---|---|---|---|---|
| **Platform / scheduled** | yes | no initiator exists | `Module.Method` | nobody |
| **Background, from script** | no | **yes** — fire and forget | `Module.Method` + argument array | the job value, or `currentActivity` in the log |
| **Background, for a fetch** | no | **no** — nothing would consume it | name or **lambda** | the form that asked |

The third row is the only one where a lambda is legal, and precisely because it cannot
outlive the session that compiled it: a report computed after its form is gone has no
consumer, so the case does not arise.

Background-from-script is a scheduled job started by hand, under the identity of whoever
started it. That is the whole difference — same machinery below.

### Each is its own session KIND

`ibSessionKind::BackgroundJob` / `ScheduledJob` / `SystemJob` (101–103), outside the run-mode
range like `WebClient`. Three rather than one because an administrator reading Active Users
has a different decision for each: a stuck **BackgroundJob** has a user waiting and a form to
tell; a stuck **ScheduledJob** belongs to the configuration and will return on its interval
whether or not this run is killed; a stuck **SystemJob** is engine housekeeping and is safe to
kill precisely because it is housekeeping. One row type would hide the distinction that
decides whether to wait or to kick.

Which kind a scheduled job gets follows `ibJobDescription::m_origin` — `Platform` or
`Configuration`.

---

## 3. What already exists

Most of this is assembly, not construction.

| Need | Already in tree |
|---|---|
| Execute with a session bound | `ibWorkerPool::Submit(session, task)` — per-session FIFO, cross-session parallel; workers bind via `ibSessionScope` before draining |
| Cooperative cancel | `ibWorkerPool::CancelSession` → `ibBackendInterruptException` at the interpreter's loop boundary |
| Reentrancy | `ibWorkerPoolHeadless::Submit` runs inline when the caller already holds that session's lease — no self-deadlock |
| Create a session | `ibSessionRegistry::CreateSessionWithFactory` → `ibSessionHolder` (ownership; dropping it closes the session) |
| A session's connection | `ibSession::Holder()` — the session **owns** one holder, one connection |
| Identity without a password | `Login` is already split: `AuthenticateUser` (pure check, no session state) + `InstallUser` (commit) — `appData.cpp` |
| Priority below user work | `ibPriority::Background` — declared, currently unused anywhere |
| "What is it doing?" | `ibSession::SetActivity` → `sys_session.currentActivity` → cluster snapshot → Active Users dialog |
| Admin kill | `ibSessionRegistry::Kick` |
| Handler that is a name **or** a lambda | `ibEventDispatcher` — GUI-free, in backend; `ibValueEvent` and `ibValueFunction` both implement it |
| Interval handler, by name | `ibValueForm::AttachIdleHandler(procedureName, interval, single)` — the same verb, one level down |
| Argument array | `CallLambdaWithArgs(fn, ibValue** argPtrs, argCount, ret)` and `CallAsProc(name, params, count)` already take arrays |
| A tick that only *produces* | `ibWebTimer` — "The thread only produces the event; it never runs script code" |
| An existing system job | `ibFirebirdMaintenanceScheduler` — sweep / backup-restore, own thread, own schedule constants |

---

## 4. Anatomy

```
tick source                 job manager                  worker pool
───────────                 ───────────                  ───────────
platform timer  ─┐
RunScheduledJobs ├─ Tick() ─→ due? ─→ session->Submit(task) ─→ worker binds session
compute server  ─┘                     │                        via ibSessionScope
                                       └─ future kept for skip-if-busy
```

**The tick never executes.** Two independent reasons, both fatal if ignored:

1. The registry thread's death is a fail-stop — `Die()` terminates the process, because a dead
   registry means `sys_session` lies to peer processes. Script must not run there.
2. `ibSession::Current()` resolves by **thread binding**. The registry thread is bound to no
   session; a lambda dispatched from it would find no runtime, or — worse — a neighbouring
   session's. Only the pool binds correctly.

**The manager lives on `ibApplicationData`**, alongside `m_connectionPool`, `m_lockManager`,
`m_logger`, `m_sessionRegistry`, `m_helpService` — same shape, `unique_ptr<class X>` behind a
`ib::AppDataCtorToken`. It must be declared **after** `m_sessionRegistry`: destruction runs in
reverse declaration order, and the manager holds session holders whose release goes through
the registry.

**Sessions are held, not re-created.** A session costs `Connect` → auth → `EnsureRoot` →
`CompileRoot`, and the last one compiles the root module. Per tick that is unaffordable, so
the holder lives in the schedule entry and is reused.

**Concurrency is the number of sessions.** A session owns exactly one connection, and
`AcquireFreeConnection` is deliberately not mirrored on it. Two things in parallel means two
sessions — never two holders on one.

**The manager caps its own sessions.** `ibConnectionPool` blocks in `Checkout` once `maxSize`
is reached, so unbounded background sessions would eventually stall interactive work on a
connection wait. The cap belongs to the manager: the pool cannot know who matters more.

---

## 4b. One manager, N of everything — the file base is the degenerate case

The same manager runs everywhere; only the numbers differ.

On a **file base** it lives inside the client that opened it: one process, one tick, a job or
two, and a session that exists only while something is actually running. On a **compute
server** the very same manager starts with the server and holds tens, hundreds, thousands of
runs — same tick, same claim, same per-run session, just more of them.

That is deliberate and it is the same shape sessions themselves took: nothing branches on
"am I a file base or a server". The file deployment is the case where N happens to be one.
A design that asked the question would have grown two schedulers, and the second one is
always the one that rots.

**A sleeping job costs nothing.** No session, no connection, no row in Active Users — the tick
compares two timestamps and moves on. A session is *generated by a run*, exists for its
duration, and is released as the run's last step. Between runs there is nothing to see, which
is why "three sessions hanging" was a bug and not a design: the holder used to die with the
pool's closure, whenever the pool got round to destroying it, instead of when the work
finished.

## 4a. The schedule

`ibJobSchedule` (`backend/job/jobSchedule.h`) — split out because it is the part a user edits
and the part worth testing on its own: every field is a pure function of (last run, now).

**Two independent questions, joined by AND.** The calendar answers *does this moment qualify*;
the interval answers *has enough time passed*. That composition is what makes the common cases
expressible without a cron grammar:

| | |
|---|---|
| every 10 minutes | `interval 600` |
| nightly, off-hours | `interval 24h` + window 02:00–05:00 |
| Mondays only, off-hours | the above + `daysOfWeek = Monday` |
| 10:00–15:00 on Tuesdays in March | window + `daysOfWeek` + `months` |
| 1st of the month, at night | `interval 24h` + window + `daysOfMonth = {1}` |

Neither half is optional. Without the interval a job re-runs on every tick for the whole
window; without the calendar it runs at any hour.

Details that are decisions rather than defaults:

- **Minutes, not hours** — "from 02:30" has to be expressible.
- **The window wraps midnight.** 22:00–05:00 is a night window; read as a plain `start ≤ x <
  end` range it would match no minute at all.
- **An empty day mask means "any"**, not "never" — that is what an untouched control produces,
  and a job that silently never runs is the worst reading of "the user did not choose".
- **A month with no 31st simply has no such day.** "On the 31st" runs seven times a year, which
  is the honest reading; sliding to the 30th would make it run in months nobody named.
- **The calendar reads wall-clock time** (a user saying 02:00 means theirs) while the interval
  is measured on a **steady** clock, so moving the system clock cannot make a job fire twice or
  stall until the date comes back around.
- **`IsValid()` refuses what can never match** — empty window, inverted validity range,
  non-positive interval — at registration, rather than leaving it to be debugged as silence.

`ToString()` renders the sentence a settings list shows ("Every 10 minutes, 10:00-15:00, Tue"),
naming only what was set. `NextAllowedAfter()` computes the next qualifying moment **through
the calendar**, so a Tuesday-only job does not promise tomorrow at 03:00.

## 5. The tick has three sources, one entry

The mechanism does not care where the tick comes from — the same pattern `ibWebTimer` already
uses on the web, where a thread produces an event and the session worker consumes it.

**The manager owns its own tick thread**, started where the platform's jobs are declared. Not a
host's timer: a desktop client, a web server, a compute server and a daemon then keep the same
schedule without any of them arranging it, and nothing scheduled goes near a UI thread. The
thread only asks what is due and hands it to a worker — see §4 for why it must never execute.

Script can force a round through `RunScheduledJobs()`, and `RunJob(name)` runs one regardless
of its schedule. The latter is not a debugging nicety: without it a job can only be exercised
by waiting out its interval, which makes a misbehaving tenant impossible to tell apart from a
misbehaving manager.

**Several processes on one base do not multiply the work**, so there is no "only the lone
process may tick" rule to enforce. Each launch claims `Job.<name>` in `sys_lock` and consults
the shared last-run in `sys_job`; whoever gets there first runs it, the rest record it as
already done. A thin client never opens a database, so it neither schedules nor executes
anything — not by a gate, but because there is nothing there to tick.

`RunScheduledJobs` submits and **returns immediately**. Waiting on the futures would freeze
the form that called it. The futures go into the schedule entries instead, where
`wait_for(0s) != ready` means "previous run still going, skip this tick" — otherwise a
one-minute cadence over a ten-minute job queues ten runs and the FIFO honours all of them.

---

## 6. Starting one from script

```
job = RunBackground("Calculations.RecalcTotals", args);   // args : Array, optional
...
If job.IsComplete() Then
    result = job.Result();
EndIf
job.Wait(5000);        // ms; 0 or omitted waits forever
job.Cancel();          // cooperative — the interpreter unwinds at its next loop boundary
```

**The name is `ModuleName.MethodName`** — a **public** method of a common module. Required in
that form rather than merely accepted: the call is made by name *from another session*, so
which module the author meant has to be in the name, not in whatever was in scope where the
call was written. Visibility needs no separate check — a module value exposes only its public
methods, so a private one is simply not found.

The run adopts the **caller's** identity: it sees what the caller sees, RLS included. No
password is involved — `Login` is already split into `AuthenticateUser` (the check) and
`InstallUser` (the commit), and only the commit is used. Naming a *different* user is
deliberately not offered yet; see §9.

Dropping the value cancels nothing. The run owns its own session and finishes on its own,
which is what makes "start and forget" the default rather than an option.

## 7. The job value in script

`ibValueBackgroundJob`, registered with **`SYSTEM_TYPE_REGISTER`** — vended, not creatable. A
job is born by being started, so `New BackgroundJob()` would refer to nothing; the macro *is*
the difference (`docs/script-value-types.md` § 1).

Surface: `IsComplete()`, `Wait(ms)`, `Result()`, `Error()`, `Activity()`, `Cancel()`. It covers
both paths — wait for it, or forget it and watch `currentActivity` in Active Users.

It is a thin skin over `ibBackgroundRun`, which is shared with the worker. `m_done` is
published **last**, after the result and error are stored, so a watcher that sees
`IsComplete()` finds everything already there.

---

## 7. First tenants

**Totals shard fold.** `ibDerivedState::CollapseAll` already declares itself "THE ENTRY POINT
a scheduled task calls" (`query/derivedStateBuilder.h`), and its header states the manager's
requirements before the manager exists:

- **The holder is required** — "a background job has no ambient answer to that… falling back
  to *the calling thread's* would silently borrow somebody else's." It comes from the job's
  session (`session->Holder()`).
- **The schema comes in** — for a background job "the active configuration" is not a
  well-defined thing. The entry keeps a snapshot; the manager does not go looking.
- **Dosage is the caller's** — "Pace the passes; each key is self-contained, so stopping
  between them is always safe." The callback must be able to answer *work remains* and be
  requeued. Design the callback for this from the start.
- **One fold per table** — two folds racing on one table corrupt the sum. One fold job walking
  the tables serialises by itself through the session lease; parallel-by-table is a later
  option and costs a session each.

Its safety properties simplify the manager considerably: *not running is safe*, there is *no
stored frontier*, and it is *housekeeping, never correctness*. So no catch-up after a restart,
no leader election for correctness, and `lastRun` persistence is convenience rather than a
requirement.

Note the distinction the strategy doc draws and keep it: a **janitor** (repair after an
interrupted bulk / crash) is fired on events; a **patrol** (periodic recomputation of
trigger-maintained totals) is "unnecessary by construction" and must not be scheduled. The
fold is neither — it is compaction, and it is explicitly safe to run unattended.

**Firebird maintenance** — folded in. `ibFirebirdMaintenanceScheduler` used to run its own
thread; now it keeps only the policy (what is due: sweep every 6 h, backup/restore every 7 days
inside 02:00–05:00) and exposes `RunDueMaintenance()`, which the `firebird.maintenance` job
calls once a minute.

Two schedulers in one process meant two answers to "is now a good time" and two teardown orders
to get right — and getting the second one wrong cost a use-after-free (a detached worker
outliving the pool shutdown, `EIP=0xdddddddd` in `WaitForServiceCompletion`, 2026-05-26/-05-29).
The thread and the detach are both gone; what remains is the cancel token, raised by `Stop()`
in `~ibApplicationData` **before** the driver goes down and checked on every service-query poll
iteration, so a cycle in flight unwinds within one tick.

The Standalone gate did not move: it lives at the driver's arming call, because leader mode
would break the backup/restore file swap (sharing violation on Windows, silently re-pointed
inode on POSIX). Unarmed, the body is a no-op — which is why the job registers unconditionally
and no condition is duplicated.

---

## 8. Where a system job is configured

**It already is — and no new property was added.** The obvious move was a fold schedule on
the totals metaobject; both halves of that turned out to be wrong.

`ibValueMetaObjectTotals` is deliberately property-free: "held as a plain owning reference,
not as a property… there is nothing here to show or to edit"
(`metaCollection/partial/accumulationRegister.h`). Hanging a schedule on it would have
reversed that decision for one field.

And the switch exists. `CollapseAll` skips an unsplit table, so **`SplitTotals` IS the
setting**: turn it on and the shards appear and the fold maintains them; turn it off and
there is nothing to fold. A separate "fold enabled" flag would be a second way to say the
same thing, and the two would drift — someone would turn the split off and leave the fold on,
and the fold would keep running over tables that have one row per key already.

What stays platform-owned is the *cadence* (6 h), which is about load rather than about the
configuration, and belongs with the job rather than with the register.

⚠ If a property ever is added here: **write both sides in the same commit.** `SplitTotals` was
declared as a property and never put into `ReadData` / `WriteData`; every reload silently
returned it to off — "the one failure mode that looks like the feature working."

---

## 9. Identity

A job always runs as somebody, but *how* it gets that identity differs:

- **Background from script** — inherits the initiator's identity. Safe by construction: it
  copies who asked, it does not name a third party. RLS follows, which is what a user's own
  background report needs.
- **Scheduled / system** — no initiator. The fold is C++ and needs no runtime at all, only a
  connection through `session->Holder()`, so it needs no authentication either. A *script*
  job does need one, because `m_root` and the lambda runtime are built on the `Authenticated`
  phase — and it gets it without a password through the existing `AuthenticateUser` /
  `InstallUser` split.
- **RLS must be off for the fold** — it reads and writes the totals table itself; a filtered
  view would fold a subset. `ibAccessTrustScope` is the existing mechanism for "this code runs
  privileged".

**Open decision.** Registering a job *under another user's name* from script is a privilege
escalation door if left ungated: a low-privilege user could schedule work as an administrator,
deferred and unattended. Three options, ascending strictness — (1) always run as the
registrant, (2) naming another user requires a flag in the registrant's role, (3) foreign
identity only from metadata, script may only enable/disable. (2) preserves the runtime-
programmatic requirement without opening the door; (1) is the safe minimum and upgrades to (2)
additively.

---

## 10. State

**Landed — step 1**, the pool is reachable from every host:

- `ibSessionRegistry::SetWorkerPool` — install/replace after construction; stops the outgoing
  pool first so pending tasks finish against live sessions.
- `ibSession::GetWorkerPool()` — **virtual**. Which pool runs a session belongs to the session
  kind, not to the process: one process holds an interactive session that must stay on the UI
  thread and background sessions that must not.
- `ibGUISession::GetWorkerPool()` → its own `ibWorkerPoolGUI` — the desktop answer to "where does
  this session's work run" is "the wx main thread", and that pool is that answer as an object.
  Desktop script still runs inline, because the GUI pool runs a task inline when the caller is
  already on the main thread; the `CallAfter` path serves only an off-thread caller handing a
  result back. The registry's pool is deliberately NOT that answer — it serves the background and
  scheduled sessions, whose whole purpose is to stay off the UI thread.
- `ibRegisterPlatformJobs()` (`platformJobs.cpp`) installs `ibWorkerPoolHeadless(2)` on the
  registry when a database opens and the host has none — GUI hosts size their registry pool at 0,
  which was right while a desktop process held exactly one session and stops being right the
  moment a scheduled or reader session exists. Installed there rather than in each host's `main`,
  for the same reason the job list is: whoever opens a database needs it, and none of them should
  have to remember. Two workers — enough that one long job cannot stall another, small enough
  that background work never crowds out interactive sessions; the real ceiling is the connection
  pool (32), since a session owns one connection.

Web needed nothing: `ibWebApplication::PostWork` has always gone through `session->Submit`.

~~**A background READ does not come through here.**~~ **Superseded by step 8 below.** The cost
argument was right — `Connect` plus a runtime bring-up per scrolled page is absurd — and the
conclusion was wrong: a read does not need a runtime, so a rented run does not build one. The
per-window reader session it pointed at (`ibSession::Reader()`) is gone; a session held open
between portions is one that can sit in Active Users holding a connection with nobody able to
tell working from stuck, and a run that ends by construction cannot.

**Landed — step 2**, the manager itself (`backend/job/jobManager.{h,cpp}`):

- `ibJobDescription` (name, body, interval, optional day window) + `ibJobBody`, whose **return
  value is dosage**: `true` means work remains and the job is due again on the next tick.
- `Register` builds the session up front and refuses cleanly — duplicate name, incomplete
  description, cap reached, no application data. Rejections land in front of the caller at
  bootstrap rather than as silence at tick time.
- `Tick` harvests finished runs (logging what a body threw), skips what is still running, and
  launches what is due. It never waits.
- `RunNow` ignores interval and window; `Stop` waits out in-flight runs before dropping each
  session holder.
- Owned by `ibApplicationData` as `m_jobManager`, declared **after** `m_sessionRegistry` so
  reverse-order destruction takes it down first; the dtor stops it ahead of the metadata
  teardown, since a running body may be walking the metadata.
- Progress is visible through the existing path: each run sets `SetActivity("job: <name>")`,
  which reaches `sys_session.currentActivity` and the Active Users dialog cluster-wide.

**Landed — step 3**, the tick:

- `RunScheduledJobs()` / `RunJob(name)` as global script functions (appended at the END of the
  `ibValueSystemFunction` enum — the ordinal is the method index).
- A 1 s `wxTimer` on the Enterprise main frame, started only on a **file** base (see §5).
- `tests/test_jobManager.cpp` — window arithmetic including the midnight wrap, the registration
  contract, and every refusal path.

**Landed — step 4**, the first tenant and the two things a shared base needs:

- `backend/job/platformJobs.{h,cpp}` — the engine's own jobs, kept apart from the manager so
  the manager stays a mechanism and this file stays the list. `totals.fold` runs `CollapseAll`
  every 6 h against a snapshot taken from the open configuration, with `session->Holder()` as
  the connection. Registered from `mainApp` right after the login succeeds — **not** from a
  registry listener, since registering creates a session and `Connect` waits on the registry
  thread, so calling it from there deadlocks.
- **Cross-process synchronisation — two mechanisms, two questions.** The pool's per-session
  lease only rules out a second run inside one process.

  `sys_lock` / `Job.<name>` answers *is somebody running it right now*. `sys_job.lastRun`
  answers *has it already run recently*. Neither alone suffices: without the claim two
  processes start together; without the shared clock each keeps its own and the job fires once
  per process per interval. A refused claim is not an error and is not logged — the job is
  simply skipped until its next tick.

  The claim's owner is the **job's session**, not the process. That is what makes a crash
  survivable: the registry sweeps stale `sys_session` rows by heartbeat and the lock manager
  drops every row whose owner is not among the live sessions, so a claim cannot outlive the
  process that took it. It also gives liveness for free — the session's row is heartbeated
  once a second for as long as the run holds it, so a long job is visibly alive without a
  second liveness channel.

  This is the lease `derivedStateBuilder.h` asks for when it says "serialise at the JOB level",
  and `lockHolder.h` already listed "long-running background job" as a holder kind.

- **`sys_job` goes through L2.** Read and upsert are built with `ibDatabaseQueryBuilder` /
  `ibUpsert`, never raw SQL: the dialects spell upsert differently (Firebird `MATCHING`,
  PostgreSQL / SQLite `ON CONFLICT`, MySQL's implicit key) and closing that difference is what
  that level is for. Hand-rolling an UPDATE-then-INSERT fallback would have put a driver
  question inside a scheduler.
- **Observable state** — `ibJobState` / `Snapshot()`: outcome (`Never` / `Running` /
  `Succeeded` / `Failed` / `Skipped`), last-run wall clock, error text, and a **computed** next
  run. Scheduling itself runs off a steady clock so moving the system clock cannot make a job
  fire twice or stall; the dates are the human-readable projection, not the source. Background
  jobs need none of this — they run once and report to whoever started them.
- The value gate: `ibValue::IsTransferable()` plus the four overrides in §1, and
  `ibJobManager::FindNonTransferable` for the argument array.

**Landed — step 5**, the script-facing layer and the second tenant:

- `RunBackground("Module.Method", args)` → a vended `BackgroundJob` value
  (`ibValueBackgroundJob`, `SYSTEM_TYPE_REGISTER`): `IsComplete` / `Wait` / `Result` / `Error` /
  `Activity` / `Cancel`. Runs on a session of its own under the **caller's** identity, adopted
  through `InstallUser` with no password (only the commit half of `Login`). Arguments pass the
  gate at submission and throw by name if not.
- Three session kinds — `BackgroundJob` / `ScheduledJob` / `SystemJob` — so Active Users shows
  what is running rather than another anonymous row.
- `firebird.maintenance` folded in; the Firebird scheduler lost its thread.

**Landed — step 6**, scheduling and synchronisation finished:

- `ibJobSchedule` (§4a) — interval + window in minutes + weekdays + month days + months +
  validity range, combined with AND, plus `ToString()` and `NextAllowedAfter()`.
- The manager runs **its own tick thread**, so the schedule is host-independent and never on a
  UI thread; `Register` became declaration-only, so the platform's list is declared by
  `ibApplicationData` when a database opens and a job that never comes due costs nothing.
- `sys_job` — the shared last-run clock, through L2's upsert.
- The cross-process claim is owned by the job's session, so it dies with the process and
  heartbeats while the run lives.

**Landed — step 7** (2026-07-31), `StartBackground` split into two overloads
(`jobManager.{h,cpp}`):

```cpp
using ibBackgroundBody = std::function<ibValue(ibSession* session)>;
std::shared_ptr<ibBackgroundRun> StartBackground(ibBackgroundBody body, const wxString& activity);
std::shared_ptr<ibBackgroundRun> StartBackground(const wxString& procedureName,
                                                 const std::vector<ibValue>& args = {});
```

Everything that makes a background run what it is — its own session, the initiator's identity
(and therefore their RLS), the try/catch that turns a throw into a reported error, the handle
somebody may watch or drop — is identical whether the work is a configuration's procedure or the
engine's own C++ reading. Only the payload differs, so the body form is the one implementation
and the named-procedure form is built over it. `activity` is required: a background session with
no activity text is a row an administrator cannot account for. The result is handed back by
`Submit`ting to the session that ASKED — captured by the caller before the run starts, never
resolved inside, where `ibSession::Current()` is this run's own session.

**Landed — step 8** (2026-07-31), **tenancy**: one background run outside, two behaviours
inside.

```cpp
enum class ibJobTenancy { Standalone, Tenant };
std::shared_ptr<ibBackgroundRun> StartBackground(ibBackgroundBody body, const wxString& activity,
                                                 ibJobTenancy tenancy = ibJobTenancy::Standalone);
```

One session kind (`BackgroundJob`), one watched list, one handle — the difference is
whether the run can still be there when whoever asked for it is gone:

| | Standalone | Tenant |
|---|---|---|
| identity | the caller's, via `InstallUser` | none — it acts FOR the caller, not AS them |
| runtime | `NotifyAuthenticated` → `EnsureRoot` + `CompileRoot` | none; a read runs no script |
| session comes from | `registry->CreateSessionOfKind` | minted directly, **past the registry** |
| row in `sys_session` | yes | **no** (`SetUnlisted()`) |
| RLS | its own policy, rebuilt | **borrowed** from the parent |
| outlives the caller | yes — start and forget | no; it has no meaning without the parent |
| waits | the generous defaults | `kTenantWait` = 5 s, registry AND pool |

**What renting actually buys.** A standalone run pays `Connect` + `InstallUser` + a compile
of every common module. That amortises over a report and is absurd per scrolled page — the
paragraph in §10 that said a read must therefore stay off the job manager was right about the
cost and wrong about the conclusion. What a read needs from a session is ONE thing: a
connection, because a session owns exactly one and the caller's is busy with the caller's own
work. Everything else it can borrow, so it does.

**The parent is `Server()`.** No new field: that link already means "the session that hosts
this one" (a web client's server is exactly this relation), so `ibSession::GetAccessPolicy()`
walks it when the session has no policy of its own. That is also why a tenant is refused when
there is no current session — a run with no parent is not a tenant, and running it unrented
would be RLS quietly switching off. The task holds the parent by `shared_ptr` for its whole
life: the policy object must still be there while a read is using it.

⚠ **One policy, two threads.** Borrowing means the parent's `ibRuntimeAccessPolicy` — and
with it the role-module `ibProcUnit`s, which keep their frame in the object — can be entered
from the worker while the parent runs script of its own. `Apply` therefore takes a mutex, so
the role modules run one at a time. It is once per query and never per row, so the cost is
nothing; what it does NOT cover is a role module reaching into the parent's module globals
while the parent is touching them itself (`ibValue`'s refcount is not atomic). Configurations
with no RLS role module never reach the lock at all.

**The connection is taken at the counter** — on the CALLING thread, inside `StartBackground`,
before the task is submitted. A saturated pool is then an exception in front of the caller,
who still has the rows on screen and can read inline instead; discovered on the worker it
would be a failure nobody is positioned to answer. `ibDatabaseConnectionHolder::EnsureConnection`
gained the bound (`ibConnectionPool::Checkout` takes it; the 30 s constant stayed the default).

**And the other half of the same question — a STANDALONE run had no runtime either.**
`ibValueModuleRuntimeManager::AttachRuntime` gated on a POSITIVE list of session kinds
(Enterprise / WebClient / Service), so every kind added after it — `BackgroundJob`,
`ScheduledJob` — passed straight through and got no root module at all: `RunBackground
("Module.Method")` could not resolve anything, and said nothing about why. The gate now names
the three kinds that do NOT execute a configuration (Launcher, Designer, WebServer) and lets
everything else through, so a session kind added tomorrow defaults to working rather than to
silence. A tenant never reaches this code — it is not authenticated and asks for no runtime —
so the gate is not what keeps renting cheap.

**Who starts one is the MODEL, not the control.** `ibValueModel::SubmitFetchAsync` (`model.cpp`)
is the override that rents; the base `ibDataViewModel::SubmitFetchAsync` runs the work on a thread
the model owns, and `ibValueModelStorage` (RAM) goes back to that base — renting a session and a
pooled connection for an in-memory sort buys nothing, and its rows are the live nodes the view
holds. The control hands over a unit of work through one door and is told nothing about where it ran — see
[table-model.md](table-model.md) § "Off the UI thread". A tenant is not a property of the host
either: Designer, thick client, browser tab all rent the same way.

**First tenant — the paged list read** (`ibDataViewCtrl::DispatchPagedFetch`). Along the way:
a portion that THROWS now still reports back, because the "a fetch is out" counter is lowered
on the UI thread when it does — swallowing the throw left that counter raised and the list
never asked again. The reason goes to the journal (`list` / `fetch`), the loaded rows stay,
and the exhaustion flags are left alone so the next scroll retries.

**Pinned by `tests/test_jobTenancy.cpp`** (its own CMake target, `oes_job_tenancy_test` — needs the
appData env plus a pool, so it stays out of the main suite): a rented run with no parent is
refused; it runs on a session of its own, bound to it, with the parent reachable through
`Server()`; it leaves no row the registry can find. And the one that would have caught the outage:
**a pool of ONE connection, eight rented runs in a row.** A tenant that fails to give its
connection back fails that on the second iteration — where the live symptom was a window that
stopped answering after roughly thirty scrolls, with nothing in any log.

⚠ **A task that runs INLINE must still be bound** — found by that test, not by the outage. A
worker takes an `ibSessionScope` around every task it drains; `ibSession::Submit` falls back to
running inline when the session has no pool, and that path did not. For a tenant it undoes the
whole arrangement: it mints a session precisely to get a connection of its own, and unbound it
reads through the parent's — the one that is busy. `Submit` now binds on both paths; where the
binding already holds (a GUI session submitting to itself, a re-entrant submit from inside the
session's own worker) the scope saves and restores what was there.

**Next**:

1. **Background fetch for forms** — report generation off the UI thread. Straightforward now:
   the report has a moment of completion, so it is `RunBackground` plus a notification routed
   back to the initiator's session, and the list case is already decided by `DynamicRead`
   (§ "Which reads can go background"). Paged loading is NOT this and is not planned here.
2. **Naming a different user** than the caller — the remaining half of §9. Needs a role gate;
   the entry point does not change shape, it gains a parameter.
3. **Configuration-declared scheduled jobs** — the manager already takes
   `ibJobOrigin::Configuration` and gives such a job a `ScheduledJob` session; what is missing
   is the metadata that declares one and the read that turns it into a registration.

   Shape it as a **metaobject property**, not as a new top-level metaobject: the work belongs
   to something that already exists (a data processor, a register), and a schedule hanging off
   that object keeps "what runs" next to "what it runs on". `Register` is already cheap enough
   to call from the metadata load — it takes no session and touches no database — so the read
   can happen wherever `RunDatabase` walks the tree.

   ⚠ Both sides of `ReadData` / `WriteData` in the same commit. `SplitTotals` was declared as
   a property and never serialised; every reload silently returned it to off.
4. **A designer-editable schedule for the platform's own jobs** — the cadences are constants
   in `platformJobs.cpp` today (fold every 6 h, maintenance every minute). Turning them into
   settings is the same mechanism as (3), minus the declaration: the jobs already exist, only
   their interval and window would come from somewhere editable.

### Which reads can go background — the existing switch already says

A list reads one of two ways, and the platform already lets the configuration choose
(`ibValueModelCursor::IsDynamicRead`, `model.h`; the dynamic list surfaces it as
**DynamicRead**):

| DynamicRead | How it reads | Background? |
|---|---|---|
| **true** (default) | live keyset cursor, one page per fetch | **every page except the first** — on the reader, not on a job (see below) |
| **false** | `EnsureSnapshot()` materialises the WHOLE result set once, then everything is served from RAM | **yes, all of it** — one long operation with a moment of completion |

⚠ The first row read the other way round when this was written ("the first page only, the pages
after it are needed at once"). The code says the opposite: `DispatchPagedFetch` submits the
forward / backward portions to `ibSession::Reader()`, while `PagedBootstrap` — the first page,
and every reload after a sort or filter change — still runs on the UI thread. The premise was
that a next page is "needed at once"; it is not, because the control already holds a loaded
window and the user can keep reading it while the next portion is out.

So the answer is not "lists can/can't be backgrounded" but **who runs it**: an operation with an
END (a snapshot, a report) is a JOB, because a session per run amortises over one long piece of
work. A portion of a scrolled list is not — it goes to the window's reader, which is one session
for the whole burst.

That also settles where the choice lives: the configuration already makes it per list, for
its own reasons (a small list where stability beats liveness, a sort the cursor handles badly).
Backgrounding follows that switch instead of adding a second one — the same shape as
`SplitTotals` deciding whether there is anything for the fold to do.

~~**Deliberately not now** — asynchronous paging.~~ **Superseded 2026-07-31**, and by a different
mechanism than the one this paragraph was arguing against. Asynchronous paging did NOT need the
fetch contract to change shape or rows to become placeholders: `GetFirstFetch` / `GetNextFetch`
are still synchronous, and the whole call now simply happens on **another session** —
`ibSession::Reader()` — with the result marshalled back through `wxTheApp->CallAfter`. The fork's
control never asks for a row by index it does not already hold; it keeps a loaded window and asks
for the NEXT portion, which is a question that tolerates an answer arriving later. The reader is
also not a job, which is why this stopped being a job-manager item at all. See
[table-model.md](table-model.md) § "Off the UI thread". **The reader session is gone** (step 8 —
a portion is a RENTED run now), and since 2026-07-31 so is the last synchronous read:
`PagedBootstrap` — the first page and every reload after a sort or filter change — dispatches the
same way and applies its answer on the UI thread.

---

## 11. What the first build caught

Recorded because each was a real defect the design discussion could not have found, and two of
them would have been invisible in production rather than loud.

- **`wxLogWarning` on a refusal opens a modal dialog.** Registration failures logged that way
  hung the headless test run — and in a GUI client they would have put a message box on the
  screen during startup because a housekeeping job could not register. Diagnostics for a
  programming error belong in `wxLogDebug`, not in the user's face.
- **`Unregister` reported failure for a job it had just removed.** It read the *session holder*
  as its verdict, which was correct while registration built a session eagerly and became wrong
  the moment sessions turned lazy: a declared job that had never come due holds no session, so
  the removal succeeded and the answer said "no such job". The verdict is whether the ENTRY was
  found.
- **`NextAllowedAfter` did not terminate** on a calendar that names no moment (February 30th).
  It bounded itself by comparing against a deadline *date*; it now counts steps, so termination
  is a property of the loop rather than of date arithmetic behaving.

And what the audit after it caught:

- **The worker pool was installed by one host, the jobs by all of them.** `enterprise.exe` set
  the pool in its own `main` while `ibApplicationData` declares the jobs for every host that
  opens a database — so in designer / daemon / codeRunner there was no pool, `ibSession::Submit`
  fell back to its inline path, and a job would have run **on the manager's tick thread**. No
  error, no crash: just the schedule stalling for the length of every run, and the one
  invariant the tick exists to keep quietly broken. The pool is now installed next to the job
  list, where whoever opens a database gets both.
- Three dead references left by moves — a `<wx/timer.h>` include for a timer that no longer
  exists, an `#include <future>` in the scheduler that no longer has a thread, and two comments
  pointing at `m_lockHolder` after the claim moved onto the session. Each read as live API.

## 12. Stale comments found along the way

Three comments describe machinery that does not exist. They read as API to anyone new:

- `sessionRegistry.h` promised `SetWorkerPool` — **fixed**, it exists now.
- `workerPoolGUI.h` promised "the planned setter" on the registry — **fixed**; the GUI pool
  belongs on the session, not the registry.
- `webApplication.h` refers to `ibWebSession::Tick` routing events; no such method exists.
- `webApplication.cpp` calls the pool `appData->GetWorkerPool()`; the real path is
  `GetSessionRegistry()->GetWorkerPool()`.

The last two are still open.
