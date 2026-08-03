# Scheduled jobs — the metadata over the job manager

> **Status (2026-08-03): DESIGN.** The engine underneath is built and running — see
> [job-manager.md](job-manager.md). What this file describes does **not** exist yet: the
> metaobjects that DECLARE a job, the runtime that runs their module, and the window that
> lists them. Written so the work can start without re-deriving the model.
>
> Where something is a decision rather than code, it says so.

---

## 1. The problem this solves

A platform that offers one kind of scheduled job gets configurations with a hundred of them —
near-identical, differing by an organisation or a period, ungroupable because each is its own
metaobject with its own method. The usual escape is a catalog of arbitrary code, which the
platform then supports in no way at all.

The fix is not a better editor. It is a second axis: **a job's unit of multiplication is a ROW,
not a declaration.** Code is declared once; instances are data.

### What it replaces

The canonical case is an exchange with a web shop. Elsewhere that costs three things — a catalog
of exchange settings, a scheduled job, and a handler module — held together by nothing but
convention: the job knows the catalog by name, the catalog knows nothing of the job, and deleting
one leaves the others running.

Here they are one object. The settings **are** the job: it carries what it needs in its own
attributes, and it runs itself. Data that exists only to be executed stops being a catalog and
moves to where it belongs.

---

## 2. The four kinds — and the question that separates them

**What does this job serve?** That one question decides where a job lives, whether it has
instances, and whose rights it runs under.

| | Serves | Instances | Runs as | Session kind | Listed |
|---|---|---|---|---|---|
| **Platform** | the engine and the database | none | nobody — sees everything | `SystemJob` | yes; schedule only |
| **Predefined** | the configuration | none | a user, always | `ScheduledJob` | yes; schedule + active |
| **Parameterized** | the data | rows, keyed by reference | a user, may come from the row | `ScheduledJob` | yes; full card |
| **Tenant** | nothing — someone else's read | none | borrows its parent's | `BackgroundJob` | **no** |

The first three are one scale: a schedule plus a body. The fourth is not a job at all — it is a
rented run serving a paged read, it has no name and no schedule, and it must stay out of the
list (a list scroll creates them by the dozen; in a job window they would be pure noise).

**Anonymity is a platform privilege, not a property of "having no instances."** A totals fold
read through someone's row filter would fold a subset and produce wrong sums, so it runs with no
user at all. A *configuration's* predefined job is application code and must not inherit that:
running it unattended with no identity is RLS quietly switched off. The two axes — what it
serves, and whether it has instances — are independent, and the metatype fixes only the second.

---

## 3. Two metatypes, not one with a flag

Predefined and parameterized are different natures, not different settings. One has no table, no
reference and no card — the metaobject *is* the job. The other has all three. A flag switching
between them would mean a metaobject that creates and drops its own table on a property change.

The platform has solved this twice already and both times the same way — Form / CommonForm,
Command / CommonCommand. The rule is stated in the code
(`metaCollection/metaCommandObject.h`): *common versus object scope is the **metatype**, not a
property*.

| | Predefined | Parameterized |
|---|---|---|
| lives | under **Common**, beside common commands | in the main branch, like a Document |
| base | `ibValueMetaObject` — flat, like a command | `ibValueMetaObjectRecordDataMutableRef` |
| instances | none | rows, created in the enterprise |
| reference / card / list / filters | no | yes |
| cost | small — a copy of `CommonCommand` | ~600–800 lines, mostly mechanical (below) |

**What they share is not duplicated.** Both hold a module and both answer one verb. That is
lifted into a small data-free interface — "I have a job module" — exactly as the command
already mixes in `ibBackendCommandItem` / `ibBackendQueryableHolder`. No shared base with state,
no diamond.

**Placement rule for the developer:** serves the configuration → Predefined; serves the data →
Parameterized. When Predefined starts filling up with near-identical entries, they should have
been Parameterized — and the two adjacent sections make that visible at the moment of writing
the tenth one.

---

## 4. One verb, three initiators

`Execute` is declared **on the job**, and three callers reach it: the scheduler on its
schedule, a user through the `Execute` command in the list, and — later — a business process
holding the job's reference. None of the three brings a mechanism of its own.

This is the shape a command already has: it overrides the global call and runs its handler,
knowing nothing about who pressed it.

The user-facing command is a copy of Post. A document declares its own command ids on top of the
inherited writeable set (`metaCollection/partial/document.h`): `ePostValue`,
`eClearPostingValue`; `CallAsCommand` loads the document by key and writes it in posting mode. A
job declares `eExecuteValue` the same way — load by key, run the module, record the outcome.

---

## 5. Runtime — three kinds, three answers, two already written

The descriptor is `ibRuntimeModuleDataObject` (`backend/moduleInfo.h`) — the base that gives a
value its own `ibProcUnit`, compilation and named calls.

How many modules each kind has follows from whether it has an object at all:

**Execution always lives in the MANAGER module**, for both declaring kinds. What differs is
whether there is a second half:

| | Modules |
|---|---|
| Platform | none — the body is C++ |
| Predefined | manager (execute) |
| Parameterized | manager (execute) + object (write) |
| Tenant | none |

A predefined job has no data object, so there is nothing for a write module to hang on — not a
missing feature, an absent question.

- **Platform** — no descriptor at all. The body is a C++ `ibJobBody`; `FoldTotals`
  (`backend/job/platformJobs.cpp`) is a plain function with no script in sight.
- **Predefined** — a manager module, and therefore probably **no descriptor of its own**. Manager
  modules register with the module manager (`metaCollection/metaCommandObject.cpp` says so while
  explaining why a command's private module does not), so this one is compiled with the session's
  modules and execution is a call on the job's manager rather than a transient compile. That is
  the difference from a command, whose module is deliberately private and needs
  `ibValueCommandDataObject` to run at all.

  ⚠️ **Confirm this with one check when implementing.** A manager value vends its module
  (`metaCollection/partial/catalogManager.h`), but the exact C++ path for invoking it was not
  traced. If it turns out awkward, the fallback is the command's descriptor, copied verbatim.
- **Parameterized** — **two modules, and the split is not arbitrary.** The **manager module**
  holds the execution: `Execute(Ref)`, taking the job's own reference. The **object module**
  holds the write logic (may this be saved?), exactly as a constant's `RecordModule` does.
  Nothing new is written for either — `ibValueRecordDataObject` already *is* an
  `ibRuntimeModuleDataObject` (`metaCollection/partial/commonObject.h`) for the object side, and
  the manager module is an ordinary declared module on the metaobject
  (`m_propertyManagerModule`, typed `ibValueMetaObjectCommonModule`).

  Why execution belongs to the manager: the scheduler knows the **metaobject**, not a row, so
  the entry point is a type-level operation and the reference is its argument. Writing is always
  about one instance, so it stays on the object.

  Three consequences, all wanted. Manager modules **register with the module manager** — the
  comment explaining why a command's private module does not says so outright
  (`metaCollection/metaCommandObject.cpp`) — therefore it is compiled **once per session**, not
  once per row: a hundred and fifty exchanges cost one compile and a hundred and fifty calls. Its
  module-level variables live for the whole session, which is the "initialise once" behaviour
  without holding a session between runs. And it is resolvable by name, so the same entry can be
  called by hand while debugging instead of waiting for a tick.

  **No state in the manager module** — it is where the call is implemented, not where anything is
  kept. The rule is not cosmetic: the module lives for the whole session while a row lives for one
  iteration, so anything stored there is shared by **every** row the run touches and leaks into
  the next one. Per-row state belongs in procedure locals.
- **Tenant** — no runtime by construction. It is not authenticated and asks for no root module;
  that is the whole reason renting is cheap.

### The only argument is the job itself

A parameterized job takes **no parameters**. It is started with a reference to itself, and that
is enough — everything it needs is its own attributes, read on its own side.

**So the signature never changes.** `Execute(Ref)` stays the same whether the job has one
dimension or ten attributes and a tabular section: nothing is passed, everything is read. No call
site — the scheduler, the list command, a future business process — knows or needs to know how a
job is parameterized. Contrast the usual arrangement, where a job's parameters are a positional
array kept in step with the metadata by hand, and the drift shows up in production.

It also empties the transfer gate of work: the one value crossing the session boundary is a
reference, which always travels. "Passed a value table into a job" stops being expressible.

This is not a simplification, it is what the value gate already requires: a reference travels
between sessions, a loaded object does not, and the comment on that refusal names the remedy
outright — pass the reference, let the job re-read the object under its own rights. Rights and
RLS then apply where the code runs rather than where somebody pressed a button.

It also disposes of "but what if it needs a table passed in": if a job needs tabular data, that
is either its own tabular section (a reference object has them) or a query it runs itself.
Handing a live table into a background session is the case where two threads edit one object.

The consequence worth keeping: **a parameterized job is self-contained.** Adding a synchronisation
with some web shop is adding one job — schedule, settings and logic arrive together, and the host
configuration only has to agree on types. That is also what makes such a job a plausible unit for
an extension to bring (see the extension model arc), rather than three loosely related objects.

⚠️ **A loaded object does not cross session boundaries.** `ibValueRecordDataObject::
IsTransferable()` returns false, and the comment says what to do instead: pass the **reference**,
and let the job re-read the object on its own side under its own rights. So the body queries its
references and loads its objects **inside its own session** — loading outside and handing them in
is not available.

Cost: one runtime bring-up per processed object. That is the same cost as posting a batch of
documents, and the AOT bytecode cache (keyed by descriptor + source hash + metadata version)
keeps a re-compile from re-parsing. Not worth optimising ahead of evidence.

---

## 5a. What a parameterized job IS — a catalog entry with a second verb

Read it as a catalog record that, next to **Write**, also has **Execute**. That is the shape, and
it is why the settings and the schedule live in one object rather than in a catalog plus a job
that knows the catalog by name.

But the shape is not literal inheritance. `ibValueMetaObjectCatalog` sits on the hierarchical
base and brings Code, Parent, IsFolder and PredefinedName — four columns in every job table, none
of them doing any work here. Exactly one of the catalog's columns is wanted: **Description**, so a person
recognises the row in a list. So the ancestor is the shared one
(`ibValueMetaObjectRecordDataMutableRef`) and the predefined set is the job's own:

| From the ancestor | The job adds | Deliberately absent |
|---|---|---|
| Reference, DeletionMark, DataVersion | Description, Active, Schedule, LastRun, Outcome | Number, Date, Posted (a document's — a setting has no date) |
| | | Code, Parent, IsFolder, PredefinedName (a catalog's) |

**Hierarchy IS taken** (revised 2026-08-03). Folders are how a person groups their own jobs, and
they are not dead weight here: the dynamic list already has a hierarchical view mode, and the
query engine already reads a source's hierarchy for grouped totals. So one declaration buys
grouping in the list *and* grouping in reports.

That also settles the ancestor: inherit from the **hierarchical** catalog base
(`ibValueMetaObjectRecordDataHierarchyMutableRef`) rather than from `MutableRef`, and folder
forms, the AddFolder command and the parent machinery arrive already written. Code and
PredefinedName come along unused — an acceptable price for what is saved.

**Two verbs, one right — for now.** A document's Post runs under the same `Write` right as saving
it, and the job inherits that. Worth revisiting rather than assuming: "edit the exchange settings"
and "run the exchange by hand" are plausibly different roles. The mechanism is next door — a
command declares its own `Use` role through `CreateRole`.

---

## 5b. What a job row carries — and why it is queryable

Beyond its dimensions and attributes, a parameterized job stores its own scheduling state as
ordinary columns:

| Column | Why it is a column and not a computed value |
|---|---|
| **LastRun** | the schedule is a pure function of (last run, now), so the row needs its own |
| **NextRun** | see below — this is what makes "who is due" an indexed query |
| **Schedule** — an `ibJobScheduleDescription` value, written through its own serialiser | editable in the card and settable from script; see the note below on what is queryable |
| **Active** | switching one instance off must not require the Designer |
| **Outcome / Error** | the last verdict, next to the row it belongs to |

**A folder has none of these, and that costs nothing to say.** Attribute usage is an existing
property — `ibItemMode` (`Items` / `Folders` / `FoldersAndItems`,
`metaCollection/attribute/metaAttributeObjectEnum.h`), the same one a catalog uses to mark Code,
Description and Parent as `Folder_Item`. `Items` is the default, so schedule, active, LastRun,
NextRun, outcome and every dimension are unavailable on a folder without a line being written; a
folder's card shows Description and Parent, like any catalog group.

⚠️ That is a MODEL and form fact, not a storage one: items and folders share one table and the
columns exist on a folder row, merely empty. Uniqueness still has to be filtered by `IsFolder` —
attribute usage does not do that job.

### The schedule is stored as a description, and NextRun is its queryable projection

Revised 2026-08-03, after `jobSchedule.{h,cpp}` was reworked. The structure now holds **fourteen**
fields, not six — month days counted from the end, the Nth weekday of the month, every-N-weeks /
every-N-months with a fixed anchor, and a stop-after minute — and it arrives with its own storage
door, `ibJobScheduleDescriptionMemory::ReadNode / WriteNode`, shaped like every other description
in the tree ([descriptions.md](descriptions.md)).

Two consequences for us:

- **The schedule property needs no serialisation of its own.** `ibPropertySchedule` holds an
  `ibJobScheduleDescription` inside its variant data and delegates `ReadNodeValue` /
  `WriteNodeValue` to that door. Less work than planned, and it cannot drift from the engine's own
  format.
- **Do not spread fourteen columns across the job table for the sake of filtering.** The projection
  people actually query is *when will this run next* — and that is `NextRun`, which is a column
  already. Ordering a list by "soonest", finding everything due in the next hour, spotting rows that
  have stalled: all of it goes through `NextRun`, not through the weekday mask.

Script still edits the schedule directly (`job.Schedule.Interval = 600; job.Write()`), because the
description is a plain value on the object — the write is what recomputes `NextRun`.

### ⚠️ NextRun is stored here, and the engine deliberately does not store it

`ibJobState` computes the next run and says why storing it is wrong: one more thing that can
disagree with reality after a restart or a clock change (`backend/job/jobManager.h`). That
reasoning holds for a **process observation**. A job ROW is a different case, and the reason to
store it is not the report:

With the column, the body selects `WHERE Active AND NextRun <= now` — an indexed read of the rows
that are due. Without it, every tick must load every row and evaluate its schedule. At a hundred
and fifty exchanges that is the difference between an index seek and a full scan, per tick.

So this is a **move of the computation**, not a duplicate of it: the schedule is evaluated once,
after a run, instead of N times per tick. It stays correct only under one rule, and the rule is
not optional:

- recompute `NextRun` **when the row is written** (a schedule edit must take effect), and
- recompute it **after every execution**;
- treat an empty `NextRun` as due — so a row that somehow missed a recompute runs and repairs
  itself rather than going quiet forever.

The computation itself is already written:

```
NextRun = ibJobScheduleRules::NextAllowedAfter(schedule, LastRun + interval)
```

Not simply `LastRun + interval`: a schedule is interval **and** calendar joined by AND, so the
interval says "no earlier than" and the calendar moves that to the next permitted window and day.
Without the second half, an hourly job with a 02:00–05:00 window would be scheduled for 14:00.

This is deliberately the **same formula the manager uses** for a job as a whole
([job-schedule-parameters.md](job-schedule-parameters.md), "the due moment"): count from the last
run plus the interval, then let the calendar carry it forward. A time of day therefore reads as
*not before*, never as *only at* — a night run missed because the machine was off happens late in
the morning instead of vanishing. Rows must inherit that property, not invent a stricter one.

⚠️ Two ways this produces an empty value, and both must be visible rather than silent:

- `NextAllowedAfter` returns an invalid date when nothing matches within a year — a schedule
  naming February 31st. Show it on the card; left alone, "empty means due" turns that row into a
  job that runs on every tick.
- **A folder always has an empty NextRun.** The due-selection must exclude folders explicitly
  (`Active AND NOT IsFolder AND NextRun <= now`), or every folder is picked up as due.

---

## 6. Instances are keyed by REFERENCE

Because a parameterized job is a reference object, the question "what tells two instances apart"
is already answered — the reference does. No composite primary key has to be assembled.

But two roles among the job's own fields remain, and the platform already has names and classes
for both. `ibValueMetaObjectDimension` is a **subclass of the attribute**
(`metaCollection/dimension/metaDimensionObject.h`), differing by role rather than by data type —
the same way Resource does. So the job accepts two kinds of child, and it costs one line in
`ResolveChild`:

| Child | Means | Example |
|---|---|---|
| **Dimension** | tells two jobs apart; part of the uniqueness index | Organisation |
| **Attribute** | a setting the body reads; not part of identity | API address, token, depth |

The term carries over honestly. A register's dimensions form its *primary* key while a job's form
an *additional unique index* — but that difference is in the implementation, not in the meaning:
to whoever declares one, both say "this is what distinguishes the records."

⚠️ Do NOT express this as a fourth value of `ibIndexingMode`
(`metaCollection/attribute/metaAttributeObjectEnum.h`). That enum is shared by every attribute in
the platform, and a value meaningful only to jobs would be imposed on all of them.

The primary key stays the guid deliberately: a key built out of the attributes would change the
job's identity when someone edits the organisation, and a business process holding the reference
would be left pointing at nothing.

**Uniqueness over the dimensions is a separate, additional index** — declared through the
same schema contract (`ibSchemaIndex { m_name; m_columns; m_unique }` in
`query/schemaSnapshot.h`), assembled by the metaobject in `ContributeTables`, turned into DDL by
the differ. Nothing new is built.

Three traps that come with it, all real:

- **A row marked for deletion still holds the key.** Mark the job for "Organisation 1", create a
  new one for the same organisation → the unique index refuses until the old row is physically
  deleted. Either include the deletion mark in the index, or say so plainly in the message.
- **No dimensions at all** degenerates uniqueness into "one row per table". Either require at
  least one, or declare no index when there are none.
- **Per-field uniqueness is wrong here** — one composite index over all dimensions, not a unique
  constraint per column. Marking Organisation unique on its own would forbid a second exchange for
  the same organisation (a shop and a marketplace), which is an ordinary case. Nor does a dimension
  need a "check uniqueness" property: declaring it a dimension already says that.
- **Folders break a plain unique index**, since every folder has empty dimensions and therefore
  collides with every other folder. The same predicate that decides what runs decides what is
  checked: `NOT IsFolder`, a flag inherited from the hierarchical base rather than invented here.
  At the database level that means a partial index (PostgreSQL / SQLite have one, Firebird does it
  by expression, MySQL cannot), so treat the index as a backstop where the driver allows it and do
  the check on write. **Open question, not a settled one.**
- **Empty values.** An empty reference is a zero guid rather than SQL NULL, so behaviour should
  be uniform across drivers — but this must be checked on a live base, because if any driver
  stores it as NULL the check silently stops working (several NULLs are allowed in a unique
  index on most engines).

---

## 7. Registration — one per metaobject, the body iterates

A hundred and fifty rows are **not** a hundred and fifty registrations. Each registration holds a
session, a session owns one connection, `m_maxJobs` defaults to 4 and the connection pool caps
at 32 — per-row registration hits that wall immediately.

Instead: **the metaobject is registered once**; its body selects the rows that are due
(`Active AND NextRun <= now`, § 5b) and runs them in turn.

This works because the schedule has no clock of its own — since the 2026-08-03 rework the data
(`ibJobScheduleDescription`) and its meaning (`ibJobScheduleRules`, static and pure) are separate
classes, precisely so a rule can be asked about any moment without a manager standing behind it. So
the body evaluates a *row's* schedule against that row's own last-run column, with no second
scheduler.

Consequences, all of them wanted:

- one cross-process claim per job (`sys_lock`, `Job.<name>`), while the per-row last-run lives in
  the row;
- dosage is already in the contract — the body returns `true` for "work remains" and the manager
  re-queues it on the next tick instead of waiting out the interval, so a batch of a hundred rows
  is paced rather than drained in one pass;
- organisations are processed sequentially inside one run. That is the right default — they share
  one database anyway — and parallelism, when genuinely needed, is a second job rather than a
  second row.

**A row write must re-register.** Adding or deactivating a job in the enterprise has to reach the
manager: unregister + register that name at write time. The alternative — making the tick re-read
the table — would turn a tick that currently costs two comparisons into a database round trip.

---

## 8. Schedule — the configuration declares, the base holds

**The Designer's schedule is the starting point: the default for a new row.** The live value
lives in the database.

- **Parameterized** — the row is the value. The metaobject's property seeds a new row.
- **Predefined and platform** — no row exists, so the value goes to **`sys_job`**: it is already
  keyed by job name and already shared across processes (`appDataQuery.cpp`). Today it carries
  name, last run and computer; it gains *active*, a *NextRun*, and the schedule written through
  `ibJobScheduleDescriptionMemory` — one field, not fourteen columns (§ 5b).

`sys_job` stays deliberately minimal in the sense its comment means: what is **shared** goes
there, while outcome and next-run remain per-process observations in `ibJobState`. A schedule
setting is a shared fact, so it belongs; a status is not, so it does not.

What the enterprise overrides is the schedule and the *active* flag — and **switching a
misbehaving job off is the main case**, not an edge one. Without a base-side value that requires
opening the Designer and changing the configuration on a production base, which is what nobody
should be doing at 3 a.m. A parameterized job needs none of this: each row carries its own
schedule and its own active flag already.

One mechanism, two payoffs: this is also how the **engine's own** jobs stop having their cadence
frozen in `platformJobs.cpp` (fold every 6 h, maintenance every minute). For them there is no
configuration to edit at all, so a base-side value is the only place their schedule can live.

⚠️ **Seed on first sight, and say so.** The row is created from the metadata default the first
time the job is registered; afterwards the base wins. That means a schedule changed in the
Designer does **not** reach a base that already has the row — correct for a default, surprising
in practice. The list window needs a "reset to the configuration's value" action, or the first
support call is about exactly this.

---

## 9. The list — a docview tab, not a metaobject

The precedent exists: the **registration journal** is a standalone tool tab, not a
configuration object — its header says so outright (`frontend/docView/templates/
docViewAuditLog.h`), it registers a plain template with no CLSID, is invisible to File → New, and
opens from the Enterprise menu (`mainFrame/mainFrameEnterpriseEvent.cpp`). "Active users" is the
same idea one step simpler.

So the job window is `docViewScheduledJobs.{h,cpp}` plus a menu entry. Paging, the AUI toolbar,
the filter strip and refresh are all already written in the journal and are copied, not invented.

**`ibJobManager::Snapshot()` is the model.** It already returns every registered job without
distinguishing origin — name, outcome, last run, computed next run, error, schedule as a
sentence.

⚠️ `Snapshot()` is **this process's** state. On a file base that is the whole truth. On a server
base the jobs run on the server and a thin client cannot see its local state. Compose the list
instead: declared jobs from metadata and tables (shared), "when did it last run" from `sys_job`
(shared), "is it running now" from the cluster session snapshot — a run sets its session's
activity to `job: <name>`, which is exactly what Active Users already reads.

Drill-down, three row kinds:

| Row | Double click |
|---|---|
| parameterized | the ordinary object card — everything editable |
| predefined | platform card: schedule and active, nothing else — the code is in the configuration |
| platform | the same card; there is neither a metaobject nor a row behind it, only a name |

The last one matters: a window promising "every job" that shows two engine jobs you cannot open
reads as unfinished.

---

## 9a. Reporting comes free — for the parameterized half

A parameterized job is an ordinary reference object with its own table, so it is an ordinary
source for the query engine. "How often did the exchange run per organisation", "which jobs fail
most", "whose last run is older than a day" are reports over ordinary data, with nothing
contributed by this subsystem. The same reason the list and its filters cost nothing.

**Predefined jobs are NOT part of that yet.** Their state lives in `sys_job`, a service table the
query engine does not know; there is no precedent in the tree for exposing a system table as an
L3 source (the journal, for instance, is read by its own reader rather than by a query). Making
it one is small work, but it is work. Until then: full reports over parameterized jobs, window
and journal for the rest.

### Why not store predefined jobs the way constants are stored

Tempting, and wrong. A constant lives as a **column** of the single-row `sys_const`
(`fld<metaID>`), so every new constant is an ALTER, the table grows sideways, and "show them all"
means pivoting columns into rows.

Predefined jobs already live as **rows** in `sys_job`, one per name. A new job is an INSERT, "all
jobs" is a SELECT, and the shape matches the parameterized half — so one window and one report can
read both uniformly. The constant layout would be a step back from what already exists.

---

## 10. Journal — already written

No document-journal metatype exists in the platform, and none is needed here. The manager already
audits `job/finished`, `job/failed` and `job/blocked` (`backend/job/jobManager.cpp`), and the
journal viewer with filters and paging already exists. A "job journal" is a filter over it — a
whole metaobject saved.

---

## 11. Phases

1. **Predefined, minimal.** Metatype under Common, inner module, one `Execute`, registration on
   `OnBeforeRunMetaObject` / removal on `OnBeforeCloseMetaObject`, name = metaobject name.
   *Check:* visible in the tree, module opens in the code editor, `RunJob("Name")` from script
   runs it, `job/finished` lands in the journal.
2. **Schedule.** `ibPropertySchedule` + `ibVariantDataSchedule` over `ibJobScheduleDescription`,
   with node I/O delegated to `ibJobScheduleDescriptionMemory` (§ 5b — no serialiser of our own).
   Write the dialog as a **standalone `wxDialog`** from the start — it has two consumers, the
   inspector adapter and the button on a job card. Building it inside the adapter (as
   `advpropGeneration` does) guarantees rewriting it for the second one.
3. **Run as.** Property holds a guid; selection mode added to `ibDialogUserList` (OK/Cancel
   buttons, `GetSelectedGuid`, cursor placed on the current value). Users are `sys_user` rows via
   `ibUserInfo`, not a catalog.
4. **The list window** — the docview tab, plus `sys_job` gaining active + schedule.
5. **Parameterized.** The reference metatype, the `Execute` command, the uniqueness index, the
   body that iterates rows.

Phase 1 is a working vertical slice; nothing in it is rewritten by what follows.

---

## 12. Traps

- **`ReadData` and `WriteData` in the same commit as the property.** `SplitTotals` was declared
  and never serialised; every reload silently returned it to off — "the one failure mode that
  looks like the feature working."
- **`metaObject.h` is a wide header.** Adding a CLSID there and building incrementally produces
  value *and size* skew in unrelated subsystems. Clean rebuild.
- **The Designer must not execute jobs.** `OnBeforeRunMetaObject` fires there too; gate on the run
  mode (`appData->DesignerMode()`, as `ibValueCommandDataObject` already does) or the Designer
  starts running the configuration's code on a schedule.
- **An empty "run as" means anonymous, which means seeing everything.** Fine for a platform job,
  wrong for a configuration's — refuse registration instead.
- **Additive predefined-attribute contract.** Every override calls its parent first; a level that
  forgot once made a column vanish from the runtime silently.
- **Writing the outcome is a database write under the job's user.** A user who may run a job but
  may not write it will execute and then fail to report.
- **A job must be ONE thing.** The constant's header records what the alternative costs
  (`metaCollection/partial/constant.h`): it used to be a column *and* an object at once, the object
  half obtained by casting it to a class it does not derive from, so virtual calls landed in a
  foreign vtable and a fully privileged user got a read-only form. A job is an object that can
  execute — not part job, part catalog entry held together by casts. The record module is named
  after that precedent: `RecordModule`, the same property a constant declares.
