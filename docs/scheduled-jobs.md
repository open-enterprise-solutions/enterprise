# Scheduled jobs — the metadata over the job manager

> **Status (2026-08-04): BOTH halves are built and running.** Two metatypes, registered under the
> names a configuration sees: `PredefinedJob` (flat, under Common) and `ScheduledJob` (the
> parameterized one — a hierarchical reference object with rows, folders and a card). They share
> the tree branch *Common → Scheduled jobs*, predefined declared first so it renders above.
>
> Exercised live on a file base: per-row scheduling (`session opened kind=ScheduledJob → job
> finished → session closed` per row, each on its own schedule), manual `Execute` as a background
> run (`kind=BackgroundJob`, with its own journal line), a schedule edited from the card through a
> hyperlinked static text, and a script error surfacing as `Job '<name>' failed: <reason>` in the
> registration journal.
>
> **Where the built thing differs from this design, the section says so inline.** The three that
> matter: registration is **per row**, not one-per-metaobject (§ 7); `LastRun` and `NextRun` are
> **generated on read**, not stored columns (§ 5b); `sys_job` became the **register of every job
> there is**, keyed by guid (§ 8).
>
> Still DESIGN, not code: dimensions and the uniqueness index (§ 6), the "run as" property on a
> row, and the job list window (§ 9).

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

**"Runs as nobody" is a legitimate state, not a hole** (revised 2026-08-03). No identity means no
row filter is built, so the job sees everything — and for most unattended work that is exactly
what is wanted; the overwhelming majority of scheduled jobs anywhere run with full rights and
nobody arranges otherwise. Naming a user is the option, not the obligation, and it buys one thing:
the job then sees precisely what that user sees, RLS included. A default ROLE that an unnamed job
inherits is the natural next step, and it slots in without changing any of this.

For the platform's own jobs the same state is a requirement rather than a preference: a totals
fold read through somebody's row filter would fold a subset and write wrong sums.

⚠️ Whatever the identity, the job still needs a RUNTIME — the root module is built on the
Authenticated notification, so an anonymous job that skipped it had nothing to call into and
failed every tick. `OpenRunSession` therefore notifies either way and only the InstallUser half is
conditional.

**No password is involved, by construction.** `Login` is already split into AuthenticateUser (the
check) and InstallUser (the commit), and a job uses only the commit — nobody is typing anything,
and storing a service password to re-verify what the configuration already declared would be a
secret kept for no reason.

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

Beyond its attributes, a parameterized job carries its own scheduling state. **As built, only two
of these are stored** — the other two are answered on read, and the section below the table says
why the design's reasoning did not survive contact with the register:

| Requisite | As built |
|---|---|
| **Active** | STORED — a boolean column; switching one instance off must not require the Designer |
| **Schedule** | STORED — an `ibValueSchedule` in its own column (blob, `ibColumnRole::Schedule`, `_SCH`), written through `ibJobScheduleDescription`'s own buffer codec |
| **LastRun** | GENERATED — read from `sys_job` by the row's guid (§ 8), so there is no second copy to disagree with the register |
| **NextRun** | GENERATED — a pure function of (schedule, last run), computed when asked |
| **Outcome / Error** | NOT BUILT — the verdict lives in the journal (`job/finished`, `job/failed`), which every kind of job already writes to |

Both generated values are answered in one place, `ibValueRecordDataObjectParameterizedJob::
GetValueByMetaID`, which intercepts the two metaIDs before the ordinary column read. A folder
answers the empty date for both: a group is not a job.

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

### ⚠️ NextRun is NOT stored — the design below argued it should be, and the register settled it

**Revised 2026-08-04, after building it.** What follows was the reasoning for a stored `NextRun`
column, and the premise it rests on — *every tick must load every row and evaluate its schedule* —
turned out to be false. Registration is per row (§ 7), so the manager holds each row's schedule in
memory and a tick is a comparison per entry, not a query at all. The indexed `WHERE NextRun <= now`
read had nothing left to make faster.

What remained was the cost of keeping it true: recompute on write, recompute after every run, and
a repair rule for rows that missed one. All of that to store an answer that
`ibJobScheduleRules::NextAllowedAfter` gives for free from two values already in hand. So the
column went and the computation stayed where it always was — read the shared last run, evaluate
the schedule, answer. It cannot go stale after a restart, a clock change or a schedule edit,
because there is nothing to go stale.

The formula below is still exactly the one used (`ComputeNextRun`), and the two ⚠️ notes at the
end of this section still hold — an unsatisfiable schedule reads as the empty date, and a folder
has no next run.

<details>
<summary>The original argument for storing it (kept — the reasoning is sound where a body iterates rows)</summary>

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

</details>

⚠️ Two ways this produces an empty value, and both must be visible rather than silent:

- `NextAllowedAfter` returns an invalid date when nothing matches within a year — a schedule
  naming February 31st. Show it on the card; left alone, "empty means due" turns that row into a
  job that runs on every tick.
- **A folder always has an empty NextRun.** The due-selection must exclude folders explicitly
  (`Active AND NOT IsFolder AND NextRun <= now`), or every folder is picked up as due.

---

## 6. Instances are keyed by REFERENCE

> **As built (2026-08-04): the reference key is there, the dimensions are not.** A row is
> identified by its guid, and that is what the register, the claim and the manager entry are all
> keyed on. Dimensions as a second kind of child, and the uniqueness index over them, remain
> design — a job's attributes are ordinary attributes today. The section below is the plan for
> when that is built; nothing in it is contradicted by what exists.

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
  At the database level that means a partial index (PostgreSQL / SQLite have one, Firebird does it  by expression), so treat the index as a backstop where the driver allows it and do
  the check on write. **Open question, not a settled one.**
- **Empty values.** An empty reference is a zero guid rather than SQL NULL, so behaviour should
  be uniform across drivers — but this must be checked on a live base, because if any driver
  stores it as NULL the check silently stops working (several NULLs are allowed in a unique
  index on most engines).

---

## 7. Registration — one per ROW (revised 2026-08-04)

**The design said one registration per metaobject. What was built is one per row, and the wall it
was avoiding turned out to be in the wrong place.**

The argument against per-row registration was: a registration holds a session, a session owns a
connection, `m_maxJobs` defaults to 4 and the pool caps at 32. Every step of that is true except
the first — a registration is a *declaration*, and `ibJobManager::Register` says so outright: no
session, no database, no metadata. The session is materialised on first launch and released when
the run ends. So a hundred and fifty declared rows cost a hundred and fifty descriptions in a
vector, and nothing else.

That left `m_maxJobs`, which really was counting the wrong thing: it capped how many jobs could
be **declared**. It now caps how many may **run at once** — which is what the resource argument
was always about (a session, a connection), and what a hundred and fifty rows on the same minute
would otherwise do to the pool. They may all be registered; each comes due on its own schedule,
and the tick starts as many as the cap allows and leaves the rest due for the next one.

What per-row registration buys, and what a single iterating body could not have given:

- **A row is a job in its own right.** It has its own key (its guid), its own record in the
  register, its own claim, its own last run and its own entry in the manager — so "run this one
  now", "switch this one off" and "when did this one last run" are all answerable without
  loading anything.
- **The cross-process claim lands on the ROW.** `Job.<rowGuid>`, not `Job.<metaobjectName>` — see
  § 7a, which is the whole reason it matters.
- **The registration census is one query at start-up** (`RegisterJobs` walks the non-folder rows
  once, when the configuration runs) and one call per write afterwards (`RegisterRow` from
  `WriteObject`, `UnregisterRow` from `DeleteObject`). The tick never reads the table.

<details>
<summary>The original one-registration design (kept — it is what a job with true parameters, rather than rows, would still do)</summary>

**the metaobject is registered once**; its body selects the rows that are due
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

</details>

⚠️ **Re-registering means UPDATE IN PLACE, not unregister-then-register.** `RegisterRow` calls
`ApplySettings` first and only falls through to `Register` when the row is not yet known. The
reason is not tidiness: the write path can be reached *from inside a run of that same row*, and
`Unregister` waits for the entry's future — which would be waiting for the run doing the waiting.

---

## 7a. One row, many machines — what makes this safe on a file base

A file base has no server: every copy of the application that opened it registers the same rows
off the same table and ticks its own schedule. Two desks with the configuration open are two
schedulers for one set of jobs, and neither knows about the other.

Three existing mechanisms already answer this, and the parameterized half simply uses them:

| Question | Answer | Where |
|---|---|---|
| may I run this row now? | take `Job.<rowGuid>` in `sys_lock` — refused means a peer has it | `Launch`, gated on `desc.m_exclusive` |
| did somebody just run it? | the shared clock in `sys_job`, read before a session is even opened | `Launch`, `ReadSharedLastRun` |
| who is running what right now? | the cluster session snapshot — every job runs on a listed session of its own | Active Users |

**Each ROW is exclusive; different rows are not.** That is the distinction the flag carries: two
exchanges running side by side is the ordinary case, the same exchange running twice is the thing
that must not happen — and since the claim is keyed on the row's guid, one line of configuration
says exactly that.

A server base changes nothing about the mechanism. There simply happens to be one process holding
the schedule, so the claim is never contended.

⚠️ **`Execute` by hand does NOT take the claim** — it is a background run, deliberately outside
the schedule (§ 8a). Pressing it while the same row is running elsewhere in the cluster runs it
twice. Known and accepted for a person's explicit "run this now"; if it ever needs closing, the
claim is next door and the key is the same.

---

## 8. Schedule — the configuration declares, the base holds

**The Designer's schedule is the starting point: the default for a new row.** The live value
lives in the database.

- **Parameterized** — the row is the value. The metaobject's property seeds a new row.
- **Predefined and platform** — no row exists, so the value goes to **`sys_job`**.

`sys_job` stays deliberately minimal in the sense its comment means: what is **shared** goes
there, while outcome and next-run remain per-process observations in `ibJobState`. A schedule
setting is a shared fact, so it belongs; a status is not, so it does not.

### As built (2026-08-04): `sys_job` is the REGISTER of every job there is

Not a table of overrides for the kinds that have nowhere else to live — **one shape of record for
all three kinds**, platform, predefined and parameterized alike:

| Column | |
|---|---|
| `jobKey` | **guid, primary key** — the metaobject's guid for a predefined job, the ROW's guid for a parameterized one, a fixed literal for each platform job |
| `jobName` | a caption, and only that. It may change; nothing is keyed on it |
| `active` | the switch. A job switched off KEEPS its record — variant chosen deliberately (see below) |
| `schedule` | the schedule as a blob, through `ibJobScheduleDescription::WriteBuffer` |
| `lastRun`, `computer` | the shared clock and who last ran it |

**Why the key is a guid, not the name.** A name is a caption a person edits; identity that moves
when somebody fixes a typo is identity that loses its history. The guid is what the row, the
metaobject and the claim (`Job.<key>`) all already have.

**Deleting versus switching off.** A row that is switched off keeps its record — that is what lets
its card still show when it last ran and when it would run next, which is precisely what one looks
at before switching it back on. Only a **deleted** row loses its record
(`UnregisterRow(forgetState = true)`).

⚠️ **Who owns the switch and the schedule depends on whether there is a card.** For a predefined
or a platform job the register IS the owner — that is the whole point of § 8, since there is no
row to ask. For a parameterized ROW the card is the owner and the register is the projection.
`ibJobManager::Register` adopts whatever `sys_job` holds, so the row states its own opinion
straight after registering (`ApplySettings`). Without that line a row switched off — or given a
new schedule — kept running with the register's old answer until somebody re-saved it, because
the re-save path went through `ApplySettings` and the start-up census did not.

⚠️ **`WriteSharedLastRun` is UPDATE-only.** A run stamps a record that registration already
created; it never inserts one. An insert there would resurrect the record of a job that had been
deleted, on the way out of its last run.

**Orphan sweep, and where it belongs.** Records whose job no longer exists are deleted by
`PurgeSharedState`, called once from `RunDatabase` after the metadata resolve — the single moment
at which every surviving job has declared itself. Deliberately NOT hung off the Designer's delete:
deleting a metaobject there is reversible until the restructuring is applied, and a sweep that
believed the Designer would drop the record of a job that still exists. The sweep declines
entirely when it knows of no live jobs at all, rather than concluding that everything is an
orphan.

---

## 8a. Execute — a background run, always

`Execute` on a job's card or in its list is not "the schedule, early". It is a one-off run asked
for by a person, so it goes where one-off work goes: `ibJobManager::StartBackground`, a session of
its own, started and forgotten.

That answers three things at once, and each of them was a reason:

- **Active or not makes no difference.** A switched-off job still runs when a person asks — the
  schedule was never consulted. (This is why every row is registered whatever its switch says: the
  switch is read by `IsDue`, not by the registration.)
- **The window does not freeze** for the length of an exchange.
- **The work is not on the caller's session.** A job's work belongs on a job session with its own
  connection, its own identity, its own row in Active Users and its own line in the journal. Run
  inline it would borrow the window's session and appear nowhere.

The two steps inside the run are the same ones the scheduled body performs, in the same order —
run the handler, stamp the last run — so a run by hand and a run by calendar leave the row in
identical states. The card's dates move when the run finishes and the card is next refreshed.

⚠️ **A forgotten run must still report.** A background run keeps its error inside its handle, and
nobody holds this one — so the body journals `job/finished` and `job/failed` itself. Without that
line a broken handler is indistinguishable from a job that ran and did nothing, which is the one
failure mode a scheduled job must never have.

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

**The register makes most of that composition unnecessary now** (2026-08-04): `sys_job` already
holds every job of every kind, with its name, its switch, its schedule and its last run, keyed by
guid and shared across the cluster. The window is a read of that table plus the session snapshot
for "running right now" — the drill-down table below is unchanged.

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

**Where the phases actually stand (2026-08-04):** 1 and 2 done; 3 (run as) done for the predefined
half, not for a row; 4 not built — but the `sys_job` half of it landed as the register (§ 8); 5
done except the uniqueness index, and with per-row registration in place of the iterating body
(§ 7).

---

## 11a. What building the predefined half actually cost

Recorded because none of these were visible from the design, and each cost a live run to find.

- **A silent `false` is the worst possible failure.** The body first reported "cannot run" the same
  way it reports "nothing left to do" — so a job that never executed a line was journalled as
  `completed`, every interval, looking exactly like the feature working. Every refusal in the body
  throws now, and that is what made the next two findings visible at all.
- **A job with no user got no RUNTIME.** `OpenRunSession` installs the identity and only then
  notifies Authenticated, and it is that notification which builds the root module. An anonymous
  job therefore had nothing to call into. Anonymous is a legitimate state (§ 2), so the fix was to
  notify either way and leave only the InstallUser half conditional.
- **A sub-minute interval was rounded up to the minute.** `NextAllowedAfter` zeroed the seconds
  before searching, so "every 4 seconds" ran once a minute on the `:00`. An already-allowed moment
  now comes back untouched; the minute-by-minute walk applies only when the calendar refuses.
  Pinned by two tests.
- ⚠️ **`Register` is on the connection path, and the tick held its mutex.** The Firebird driver
  declares its maintenance job from `Open()`, so every checkout from the pool calls `Register` —
  opening a form reaches it through compile → bytecode cache → query builder → checkout. Meanwhile
  `Tick` held the same mutex across a session teardown (which waits) and a session create (which
  waits on the registry thread). With a job on a short interval the UI simply stopped answering.
  The tick now decides under the lock and works outside it, with an in-flight flag so `Unregister`
  cannot destroy an entry mid-launch. **A mutex that a hot path shares with a slow one is the bug,
  not the symptom** — two earlier attempts (keeping the session alive, skipping a snapshot read)
  treated the symptom and were reverted.
- **A session now deletes its own `sys_session` row on a normal close**, on its own thread and its
  own connection, instead of waiting for the queued Remove. Peers read the table, not intentions;
  and a row left behind is exactly what marks an abnormal exit for the sweep.
- **Do not repeat the job name inside an error.** The journal line already opens with
  `Job '<name>' failed:`; repeating it made entries long enough to be elided in the middle, which
  ate the part that said what went wrong.

---

## 11b. What building the parameterized half cost

Same rule as above: recorded because none of it was visible from the design.

- **A job session has no frame, and writing an object asks the form layer.** Stamping the last run
  through the object meant the run reached for the UI and died with
  `Context functions are not available!`. That guard is CORRECT — a server-side session must not
  reach for a window at all — so the fix was to stop asking: the stamp goes to `sys_job`, which is
  where the shared clock lives anyway. **Relaxing the guard was tried first and reverted.** A
  guard that fires is evidence about the caller, not about the guard.
- **Loading the row a second time to stamp it bumped its version under the open card.** Pressing
  Execute twice produced *"data was changed by another user"* on a card nobody else had touched.
  Splitting run from stamp was not enough; writing the stamp outside the object layer was.
- **The job ran and did nothing, silently.** `RunHandler` looked for the manager module in the
  session's root module manager; a configuration's manager module lives where
  `ibValueManagerDataObject::CallAsProc` looks — `ibSession::EditModuleManagerFor(metaData)`.
  Same failure shape as the predefined half's "no runtime": a job that appears to run.
- **A background run released its session from its own worker.** The last reference died inside
  the run's own task, and `ibSession::Teardown` queues a task on that session's queue and waits
  for it — waiting behind oneself. It also made every background job invisible in Active Users:
  the row existed for a few milliseconds, and the cluster snapshot is taken once a second. The
  manager now holds the run and takes its session back on a tick, exactly as the scheduled path
  does with `m_runSession` / `HarvestFinished`. **The same defect, found twice in one subsystem,
  is a shape to sweep for, not a bug to fix once.**
- **A reader that reports success on nothing.** `ReadBuffer` answered `true` for an empty blob
  (overwriting the caller's schedule with a default whose interval is zero — one that can never
  run) and for a TRUNCATED one (the chain's result was computed and then discarded, so half a
  schedule was handed back as a whole one). Both now refuse and leave the caller's value alone;
  what "no bytes" means belongs to the caller, not to the codec. **Found by writing the test, not
  by watching the product** — the round-trip case had passed all along.
- **`Execute` on a switched-off PREDEFINED job did nothing**, while the same button on a
  switched-off row worked. `Use = false` returned before registering, and script's Execute resolves
  a job by key through the manager — an unregistered job has no name to find. Predefined jobs now
  register whatever the switch says, and restate the withdrawal after registering (the base may
  switch a job OFF, but must not switch on what the configuration has withdrawn).
- **A switched-off row ran anyway — until it was re-saved.** Two halves of one mistake: the row's
  description never SAID it was inactive (it left `m_active` at its default and trusted `Register`
  to adopt the register a moment later), and `Register` adopted a record left over from when the
  row was still on. Writing the row went through `ApplySettings` and therefore worked, which is
  what made it look like "it does not save the flag". **When a fix works on one path and not on
  another, the two paths disagree about who owns the value** — that, not the flag, was the bug.
- ⚠️ **`RunNow` held the manager's mutex across `Launch`** — the very thing `Tick` was rewritten
  to stop doing (§ 11a). Creating a session takes a pooled connection, and taking one goes through
  the Firebird driver's `Open()`, which calls `Register`, which wants that mutex. Found by
  auditing the neighbours of the fix above rather than by a crash.
- **A tree-wide header plus an incremental build is vtable skew.** Adding one virtual to
  `backend_mainFrame.h` produced an access violation in `PrintSpreadsheetDocument` — a function
  nobody had touched. Clean rebuild. (Already recorded under § 12; it cost a dump to rediscover.)

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
- **An empty "run as" means full rights — and that is the intended default**, not a refusal (§ 2).
  What it must NOT mean is a session without a runtime: the root module is built on the
  Authenticated notification, so that notification happens whether or not a user was installed.
  Skipping it produced a job that failed on every single tick with "the session has no runtime",
  and — before the body started throwing — reported those failures as `completed`.
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

---

## 13. Picking this up again — the tails, with their entry points

Written 2026-08-04, when the arc was put down working. Each item says WHAT is missing, WHY it was
left, WHERE the code goes in, and WHAT already exists next door — so the next session starts by
editing rather than by reading.

**Nothing here blocks use.** Both metatypes run, are scheduled per row, survive a restart, and
report themselves in the journal. These are the edges.

### 13a. The job list window — the one gap a user can see

Platform and predefined jobs are visible NOWHERE but the journal: the parameterized half has its
own list (it is a reference object), the other two have no window at all.

- **Go in at** `frontend/docView/templates/` — copy `docViewAuditLog.{h,cpp}`, which is the
  precedent for a tool tab that is not a metaobject (plain template, no CLSID, invisible to
  File → New, opened from the Enterprise menu in `mainFrame/mainFrameEnterpriseEvent.cpp`).
- **The model is already there**: `sys_job` holds every job of every kind (key, name, switch,
  schedule, last run, computer), and `ibJobManager::Snapshot()` adds this process's outcome plus
  `m_key` / `m_active` (the two fields added for exactly this). "Running right now" comes from the
  cluster session snapshot, the way Active Users reads it.
- **Drill-down**: parameterized → the ordinary card; predefined and platform → a small card with
  schedule + active only.
- Reading `sys_job` cross-process is what makes this honest on a server base, where the jobs run
  somewhere else — do NOT build it on `Snapshot()` alone.

### 13b. The schedule cannot be edited in a browser

`ibValueSchedule::ShowValue` asks the frame for `ShowScheduleEditor`, and only
`ibFrontendMainFrame` (desktop) implements it — `frontend/mainFrame/mainFrameParts.cpp`. The web
frame inherits the base `return false`, and in the web build the static text renders as
`ibWebStaticText`, which has no click at all (`visualView/ctrl/statictext.cpp`, the `OES_USE_WEB`
branch).

Two halves, both small, and they are the same shape every other web control needs: a widget that
raises the click, and a web-side implementation of the editor door.

### 13c. `Execute` by hand does not take the row's claim

The scheduled path claims `Job.<rowGuid>` in `sys_lock` before running; the manual one goes
straight to `StartBackground` (`parameterizedJobObject.cpp`, `ExecuteJob`). On a file base that
means a person can start a row that is already running on another machine.

Deliberate so far — "run this now" is an instruction, not a request. If it should change, the
lock and the key are already there; the only real decision is what a busy row answers: wait,
refuse, or run anyway.

### 13d. Dimensions and the uniqueness index (§ 6)

Not built. A job's attributes are ordinary attributes, so nothing stops two rows for the same
organisation. § 6 has the whole design including the three traps (deletion mark holds the key,
no-dimensions degenerates to one row, folders break a plain unique index).

- **Go in at** `ibValueMetaObjectDimension` (`metaCollection/dimension/`) for the child kind, and
  `ContributeTables` for the index — `ibSchemaIndex { m_name; m_columns; m_unique }` in
  `query/schemaSnapshot.h` is the declaration the differ turns into DDL.

### 13e. Smaller edges

| Tail | Where | Note |
|---|---|---|
| "Run as" on a ROW | `parameterizedJobRegistration.cpp`, `RegisterRow` sets `desc.m_runAsUser` from the current user | predefined has the same limit; a property holding a guid + the selection mode of `ibDialogUserList` is the whole job |
| Retry is per metaobject, not per row | `RegisterRow` reads `m_propertyRetryCount` / `m_propertyRetryInterval` off the metatype | one stubborn exchange cannot have its own policy |
| "Reset the schedule to the configuration's value" | `ibJobManager::Register` — the base wins once a record exists (⚠ noted there) | needs a place to press it, i.e. 13a |
| Thin client | `ExecuteJob` assumes the object method runs where a job manager exists | not exercised live |

### 13f. Settled — do not reopen

- **Outcome / Error are NOT columns.** The verdict lives in the journal, which every kind of job
  already writes to; a column would be a second copy of it per row.
- **`NextRun` is not stored** (§ 5b) — the premise for storing it died with per-row registration.
- **`m_maxJobs` caps concurrent runs**, not declarations.
- **The register may switch a job OFF, never ON** what the configuration withdrew (`Use = false`).
- **The row owns its switch and schedule; the register owns them for kinds without a card.**
