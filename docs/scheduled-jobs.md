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
| cost | small — a copy of `CommonCommand` | medium — a copy of Document |

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

- **Platform** — no descriptor at all. The body is a C++ `ibJobBody`; `FoldTotals`
  (`backend/job/platformJobs.cpp`) is a plain function with no script in sight.
- **Predefined** — its own descriptor, a literal copy of `ibValueCommandDataObject`:
  `InitializeRuntime()` → `Compile()` → `Run(true)` → `ExecAsProc("Execute")`. `Run(true)`
  executes the module's top level, so **module variables are initialised there and live for the
  whole run** — which is what makes a start hook unnecessary.
- **Parameterized** — **nothing to write.** `ibValueRecordDataObject` already *is* an
  `ibRuntimeModuleDataObject` (`metaCollection/partial/commonObject.h`) and its
  `GetMetaForCompile()` already returns the object module. Load the job by reference and it
  arrives with a runtime attached; `ExecAsProc("Execute")` on it makes `ThisObject` the job row
  itself. Exactly how a document runs its posting handler.
- **Tenant** — no runtime by construction. It is not authenticated and asks for no root module;
  that is the whole reason renting is cheap.

### The only argument is the job itself

A parameterized job takes **no parameters**. It is started with a reference to itself, and that
is enough — everything it needs is its own attributes, read on its own side.

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

**Hierarchy is left out on purpose.** Grouping here is done by dynamic-list filters — by
counterparty, by state, by schedule — which beats folders for this data. Adding Parent and
IsFolder later is what the additive predefined-attribute contract is for; removing them later
would be a data migration.

**Two verbs, one right — for now.** A document's Post runs under the same `Write` right as saving
it, and the job inherits that. Worth revisiting rather than assuming: "edit the exchange settings"
and "run the exchange by hand" are plausibly different roles. The mechanism is next door — a
command declares its own `Use` role through `CreateRole`.

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
- **Empty values.** An empty reference is a zero guid rather than SQL NULL, so behaviour should
  be uniform across drivers — but this must be checked on a live base, because if any driver
  stores it as NULL the check silently stops working (several NULLs are allowed in a unique
  index on most engines).

---

## 7. Registration — one per metaobject, the body iterates

A hundred and fifty rows are **not** a hundred and fifty registrations. Each registration holds a
session, a session owns one connection, `m_maxJobs` defaults to 4 and the connection pool caps
at 32 — per-row registration hits that wall immediately.

Instead: **the metaobject is registered once**; its body reads its own active rows, asks each
one whether its schedule is due, and runs them in turn.

This works because `ibJobSchedule` was designed with no clock of its own — its header states that
everything in it is a pure function of (last run, now). So the body can evaluate a *row's*
schedule against that row's own last-run column, with no second scheduler.

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
  name, last run and computer; it gains *active* plus the flat schedule fields.

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
2. **Schedule.** `ibPropertySchedule` + `ibVariantDataSchedule` over the existing `ibJobSchedule`.
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
