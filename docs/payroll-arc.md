# Payroll — Architecture Arc (design)

> **Status:** DESIGN — **no code yet** (2026-08-02 session). Nothing below is implemented.
> Not scheduled: payroll waits for a client who asks for it, and for the web input surface
> (`ROADMAP.md` §4.2) — a payslip is a form with a tabular section, so it hits the same wall
> the warehouse vertical does.
> Companions: `informationRegister.h` (the slice descriptor this arc extends),
> `register-totals-strategy.md` (the balances it reads), `query-language-arc.md` (L3/L4),
> `accounting-roadmap` neighbourhood.
>
> **One sentence:** payroll needs **one platform addition — a reading source that slices a
> register's history into intervals** — and everything else is an ordinary configuration
> built from metaobjects that already exist; there is **no calculation-register metatype**
> in this design.

---

## 1. The thesis

The obvious move — add a "calculation register" metatype carrying period-of-validity,
priority-based displacement, base-period lookup and a recalculation table — is rejected.
Those are not primitives. Decomposed, payroll is four things, and three of them already ship:

| Primitive | What it is | State in this tree |
|---|---|---|
| Date-effective data | master data that changes at points in time | information register with periodicity — **exists**; the interval *reading* is what §3 adds |
| Elements / rules | what is paid, in what order | script + classification reference — **exists** |
| Balances | accumulators with period dimensions (period / month / year-to-date) | accumulation register + register totals — **exists** |
| Immutable run results | a payroll run is a snapshot, never edited | document + its movements — **exists** |

International payroll engines (SAP HCM, Oracle Fusion, Workday, Odoo) are all assembled
from these four. None of them has a "calculation register": interval competition lives in
the *master-data* layer (delimiting / date-tracking), and the arithmetic lives in *rules*
(PCR, Fast Formula, expressions) — never in a storage metatype.

### 1.1 Against the metatype — both columns

| | Dedicated calculation-register metatype | This design |
|---|---|---|
| Engine surface | metatype + virtual tables (factual period, base, schedule) + a recalculation queue + designer UI + serialisation + migrations | `ComputeSlice` generalised by one grouping key; one tenant of the existing trigger generator |
| Displacement policy | fixed by the platform (a priority graph) | a configured table — a client's non-standard rule is data, not a platform patch |
| Execution | virtual tables of a fixed shape | an ordinary query — optimised, paged and RLS-filtered like any other, with no special case |
| Retro | recalculation queue maintained by the platform | movement in the current period; registration by trigger — fires for scripts and manual edits too |
| Pays outside payroll | no | yes — prices, rates, deferred reserve |
| **Costs more** | — | **the first configuration writes the displacement query itself**; a metatype would have shipped it |
| **Loses** | — | no "factual period as of an arbitrary date" on read — the factual stretch is computed once per run and stored; asking for another date is a recompute |
| **Risks** | — | the set-based run shape (§7.2) is a **convention**; a metatype's virtual tables would have enforced it |
| **Costs in people** | — | implementers trained on the register vocabulary will not find familiar objects; documentation and training are a real line item |

### 1.2 Target shape — metatypes, but *after* the first configuration

§1.1 is not an argument against metatypes forever. It is an argument against declaring them
**before** one payroll has been lived through. The end state very likely does carry two
metatypes, and the tree already holds the pattern for both:

| Proposed metatype | Existing structural twin |
|---|---|
| **Chart of calculation types** | **Chart of accounts** — a catalog plus predefined attributes plus a *predefined tabular section* (`ibValueMetaObjectAccountDimensionKindsTable`). A calculation type needs exactly that shape: priority, a *Displaces* section, a *Base* section, a "subject to displacement" flag |
| **Calculation register** | **Information register** — plus predefined attributes (calculation type, validity start / end, registration period) and, most of the value, a generated `.FactualValidity` companion rendering §6.2's day-grain query from the chart's settings |

Folding them in later **removes four of the five costs in §1.1**: the displacement query stops
being hand-written, the set-based shape becomes structural rather than conventional, the
factual period becomes readable at an arbitrary date, and the vocabulary becomes the familiar
one. The trigger of §5.4 gets generated the same way totals triggers are.

**Why not first.** The field list is not derivable by reasoning — it is discovered. How many
periods a record really needs (validity, registration, base), whether a "leading type"
relation is required separately from base, whether displacement is ever needed at hour grain:
each of these is answered by writing one payroll, not by designing one.

This tree already carries the price of the other order, in the neighbouring metatype: the
chart of accounts declares `Quantitative`, `Currency` and its dimension ceiling, and the
accounting register **forces none of them** — attributes declared ahead of the scenario that
would have shaped them. The accounting register itself is declared and does not execute
(`ROADMAP.md` §4.1). A calculation register declared on the same schedule would join them.

So the order is: phase 0 (§9) → one payroll configuration where displacement is written by
hand → **fold the repetition into these two metatypes, with the field list known**. That is
the build-then-remove move this codebase already uses deliberately: the concrete case is the
tutor, and folding it is the mechanism, not the penalty.

**Fold criterion** — collapse a piece into a metatype when it has been written twice
identically in two different configurations (or twice in one for two registers), not when it
merely looks general.

---

## 2. Two kinds of interval — do not merge them

This is the distinction the whole design rests on.

| | Example | Storage | Recorder |
|---|---|---|---|
| **State with milestones** | salary, position, department, tax rate, exchange rate | a **point** record, as today. The end of the interval is **derived** — it runs until the next record for the same dimension key | HR data: yes. Tax rates: no. **Orthogonal** |
| **Event with its own duration** | vacation 10th–20th, sick leave, a contract with an end date | two ordinary date attributes on the record | usually yes — these are a document's movements |

The second kind needs **no platform support at all**: two `Date` attributes and an overlap
predicate, which L2 renders today.

### 2.1 Why the end of a derived interval is NOT stored

Storing an interval end and subordinating the register to a recorder are **incompatible by
invariant**, and this is the trap that kills the naive design:

- document *Hire* writes "salary 30000 from 01.03, end open";
- document *Salary change* writes "35000 from 01.06" and would have to **close a record
  belonging to a different recorder**;
- re-posting *Hire* rewrites its own record set wholesale — the interval is open again, and
  the June record does not know.

A record set belongs to its recorder. Delimiting requires editing someone else's set.
Therefore: **no delimiting, no stored end.** Each record stays a point, each recorder owns
only its own rows, re-posting is safe. A gap ("terminated") is expressed by a terminator
record with an empty value — standard practice, not a workaround.

---

## 3. The one platform addition

Not a flag, not a storage mode, not delimiting. **One queryable companion** next to the
existing `SliceLast` / `SliceFirst`:

```
<Register>.Intervals(from, to)
```

Returns the register's history **cut at its milestones** inside the requested period: rows
carrying interval start, interval end, and the values in force over that stretch.

Implementation shape: a fourth `ibInfoRegisterSliceDescriptor` instance in
`informationRegister.h` — the same template that already registers
`"<Register>.SliceLast"` / `".SliceFirst"` as call-scoped queryables handed to L3 through
`From()`. A new *instance* of an existing mechanism, not a new mechanism.

**It pays for proration for free.** "Salary changed on the 16th" comes back as two rows with
two durations; the worked-share factor is a division, not a displacement engine. This is what
Oracle calls proration and SAP calls factoring — derived from data instead of built as
machinery.

**It also pays outside payroll:** prices, exchange rates, and the deferred-reserve idea
(a movement with a `[from, to)` range) want the same reading.

---

## 4. Application object inventory (~30 objects)

Nothing here needs engine work.

### Catalogs
- **Persons** and **Employees** — kept separate (one person may hold several employments;
  merging them breaks on secondary employment)
- **Organisations**, **Departments**, **Positions**, **Work schedules**
- **Earning and deduction types** — with a **classification** reactive attribute: earning /
  pre-statutory deduction / statutory deduction / voluntary deduction. **The classification
  is what orders the calculation** — it is load-bearing, not descriptive

### Chart of characteristic types
- **Calculation indicators** — the input values of an earning type (amount, percentage,
  tariff, coefficient, day count). A chart of characteristic types types the *value*, which a
  catalog cannot

### Information registers — derived-interval reads (§3)
- **Employment history** — organisation, department, position, rate, hire / termination
- **Planned earnings** — "salary 30000 from 01.03", "bonus 10% from 01.06"; the main consumer
  of `Intervals`
- **Employee tax parameters** — allowances, status, flags
- **Tax and contribution rates and thresholds** — **rates are data with a validity date, never
  constants in code**. A law changes → a row is written → past periods still recompute correctly
- **Exchange rates**

### Information registers — plain periodic
- **Time worked** — employee × period × days / hours; arrives from timekeeping already computed
- **Absences** — vacation, sick leave, unpaid (by day)

### Documents
- HR: **Hire**, **Transfer**, **Termination**
- Events: **Vacation**, **Sick leave**, **One-off earning / deduction**
- Payroll: **Payroll run**, **Payment sheet**, **Adjustment / reversal**

### Accumulation registers
- **Earnings and deductions** — the run result: employee, earning type, **calculation period**
  and **period paid for**, amount, base, days / hours
- **Employee settlements** — what the company owes, cleared by the payment sheet
- **Taxes and contributions** — assessed amount and base, with **year-to-date** totals
  (progressive scales and thresholds read exactly these)

### Reports
Payslip, earnings summary, statutory reporting, employee card.

---

## 5. Three decisions that carry

**5.1 A run result is immutable.** The *Payroll run* document carries a period and a **run
number**. A recalculation does not edit a posted document — it produces the next run.
Without this there is no answer to "what did we actually pay in March", which is the first
question any audit asks.

**5.2 A movement carries two periods.** *Calculation period* (when it was posted) and
*period paid for* (which month it belongs to). Retroactive change is a movement **in the
current period** naming the past one in a dimension — the closed period is never touched,
reporting still reconciles, and the difference is visible *as* a difference. This is what
SAP and Oracle do, and the accumulation register does it today without a single change; the
discipline is in not re-posting March.

**5.3 Rule order lives in script, the working table lives in memory.** A run: read planned
earnings in force on the date → apply the worked share → compute earnings in classification
order → tax base → taxes against year-to-date balances → deductions → net → write movements.
The intermediate table is an ordinary `Table` value — the equivalent of SAP's `IT` / `RT`,
except written in a real language with a debugger instead of a rule dialect.

Adjacent, and it costs money when wrong: **rounding policy and remainder distribution** —
the parts must sum to the whole. `ibNumber` is exact; the policy is the configuration's.

**5.4 Retro must be *registered*, and this is the design's thin spot.** §5.2 says how a
retroactive difference is *booked*; it does not say how the run learns that March needs
recomputing at all. A payroll engine needs a to-recompute list: change a March timesheet
today, and the next run must know.

The tree has the hook but not the mechanism: `BeforeWrite` / `OnWrite` exist on every object
(`commonObject.h`), but there is **no subscription metaobject** — nothing lets one place
declare "watch every write that touches a closed period". So the list is an ordinary
information register (`employee × period × reason`) that each document writes to on posting.

That works, and it is a real risk: it is **discipline, not enforcement**. One document that
forgets to register silently does not recompute, and the symptom appears a month later as
money.

**The trigger generator closes this properly.** `databaseMaterializeBuilder.cpp` already
renders after-insert / after-update / after-delete triggers per dialect for the totals
bundle. A to-recompute row is the easiest possible tenant of that machinery: the payload is
an insert of `(employee, period)`, it is additive, it is local to the row that changed, and
it fires **whatever wrote the row** — a document, a data-processor script, a fix applied by
hand in the designer. That turns §5.4 from discipline into enforcement, which is exactly what
this thin spot needed.

Discipline in a common-module procedure remains the fallback if the trigger route is not
taken; a subscription metaobject is not needed either way.

**Does that trigger need maintaining?** Yes — and the maintenance model already exists and
already separates its actors (`derivedStateBuilder.h`): the **trigger** maintains steady
state per movement and is never invoked; **L3-2** builds the structure; **L3-4** regenerates
on discrete events (restore, migration, a first Apply over a base that already holds rows) and
never per movement. Nothing new is invented for payroll.

But a to-recompute list is a **much cheaper tenant than totals**, because its error is
asymmetric:

| | Totals | To-recompute list |
|---|---|---|
| A spurious row | wrong numbers | **harmless** — the recompute reproduces the same result, the delta is zero |
| A missing row | wrong numbers | money, discovered a month later |

Everything follows from that asymmetry. Every doubt resolves toward *marking more*:

- **No regeneration in the strict sense.** After a restore or migration, mark the whole open
  horizon as to-recompute rather than trying to reconstruct what changed. Coarse and correct.
- **No sharding, no fold.** The write contention that forces totals to split across shards
  (`register-totals-strategy.md` §6) does not arise: rows differ by employee, and duplicates
  are acceptable — so the trigger inserts blindly, with no uniqueness check and no read-before-write.
- **Cleanup, not reconciliation.** The run deletes what it has processed; a scheduled job
  sweeps leftovers. `ibJobManager` already runs `totals.fold` this way.

What does have to be handled, and is known ground:

- **Apply / structure change** — the derived table is replaced rather than altered, its
  attachments are dropped against the **old** spec, and on Firebird a failed DROP rolls back
  the whole transaction, so probe rather than `try`/`catch` (`project_register_totals_arc`).
- **Driver coverage** — the materialization dialect is filled for Firebird, PostgreSQL,
  SQLite and MySQL, but **not ODBC**; there the fallback is the common-module discipline.
- **The Apply window** — rows written while the trigger is absent are missed. Apply is
  exclusive (`project_apply_exclusive_ux`), so there should be no writers then; **verify
  rather than assume**.
- **Write cost** — one extra insert per timesheet or HR row. Negligible, and unlike totals it
  does not queue on a shared aggregate row.

The honest framing: the trigger is not another moving part to feed. It moves the
responsibility from thirty application call sites, each of which can forget, onto one
generated object whose failure modes are known and whose worst case is redundant work.

---

## 6. Displacement — configured, not built in

Displacement is real and must work: vacation eats the salary's days over the stretch it
covers. The design keeps it, and keeps it **configurable** — it just refuses to freeze one
policy into the engine.

Three different things travel under this one word; separate them:

| | Meaning | Where it is solved |
|---|---|---|
| **Record displacement** | a newer record supersedes an older one | milestones (§2) — nothing to build |
| **Time displacement** | vacation takes days away from salary | §6.1 — interval difference during the run |
| **Base dependency** | a bonus computed from what was earned | rule order (§5.3) — not displacement at all |

### 6.1 The setup the user configures

On the **Earning and deduction types** catalog:

- **Priority** — a number; lower wins
- **Displaces** (tabular section) — *which* types this one takes time away from. An explicit
  list, not "everything below me"
- **Subject to displacement** — a flag; some earnings are computed from actual facts and must
  not be cut (a hazard-pay supplement, a per-piece earning)
- **Calculation method** — by days / fixed / percentage of base
- **Base** (tabular section) — which types feed the base, for percentage methods

That table *is* the policy. Nothing about it lives in the engine, so a client whose sick
leave displaces salary but not the hazard supplement is a data change, not a platform patch.

### 6.2 The mechanics during a run

Per employee, per period, on segments:

```
segments = []                     // claimants
for each earning in force:
    segments += its stretches     // from Intervals(from,to) or from event dates
sort segments by (priority, start)

for each segment S:
    if S.type is subject to displacement:
        blockers = union of segments whose type displaces S.type
        S.factual = S.interval - blockers      // interval difference
    days(S) = working days in S.factual        // from the work schedule
```

Interval difference over sorted segments is linear and trivial in volume — tens of segments
per employee per month. The factual stretch then feeds days, and days feed the worked share.

**The set-based form is better, and it is the one to build.** Payroll counts in days (or
hours) anyway, so take the day as the grain and interval algebra collapses into ordinary
relational operators:

```
claim(employee, day, type)     -- stretches expanded against the working calendar
                               --   join ON calendar.day >= seg.begin AND <= seg.end
occupied(employee, day, type)  -- the same, restricted to displacing types

factual days =
  SELECT employee, type, COUNT(*)
  FROM claim c
  WHERE NOT EXISTS (SELECT 1 FROM occupied o
                    WHERE o.employee = c.employee AND o.day = c.day
                      AND o.type displaces c.type)
  GROUP BY employee, type
```

Every operator here already exists in the IR and **co-locates to the server**: the calendar
expansion is a column-keyed theta join (`ibJoinCompareOp::Ge` / `Le`, co-located since
2026-07-16), the displacement test is `ibSemiJoinExists` (built for RLS), and the rest is
`GROUP BY`. No window functions, no recursive CTE, no interval algebra to write.

Volume: 1000 employees × 31 days × a few types ≈ 100k intermediate rows per run — nothing for
a DBMS, and it never travels to the client.

A **working calendar** (day × working / holiday, per schedule) becomes a required object.
It is needed for day counts regardless, so this costs nothing extra.

There is a fourth place displacement shows up, and it is *not* the run: refusing to enter two
overlapping vacations. That is a posting check on the document — again configuration.

### 6.3 Where the claimants come from — the point that confuses

The usual model stores *declared* rows in a calculation register and derives the *factual*
period on every read. Here there is no such register, so the question "what is being
displaced, if nothing is stored?" is the right one to ask.

**Claimants are not rows of a result register — they are assembled in memory at run time from
two sources:**

| Source | Gives | Example |
|---|---|---|
| Planned earnings, read through `Intervals(from, to)` | stretches of *state* | "salary 30000 in force 01.03 → open" ⇒ segment 01.03–31.03 for March |
| Event documents (vacation, sick leave, absence) | stretches of *events* | vacation 10.03–20.03 ⇒ segment 10.03–20.03 |

Displacement runs over that in-memory set, **before** anything is written. What lands in the
accumulation register is the *result*: factual stretch, days, amount. So the difference from
the store-declared-rows model is only **when** the factual period is computed — once per run
here, on every read there.

The price of computing it once is exactly the retro machinery of §5.2 and §5.4: if a
displacing event appears for a closed month, the month must be recomputed. That is not a
weakness of this design — the store-declared model needs the same recomputation, which is
why it carries a recalculation queue.

### 6.4 Worked example — March, one employee

Setup: salary 30000 in force from 01.03 (written by *Hire* into planned earnings); vacation
10.03–20.03 (a *Vacation* document); March has 21 working days. Types configured as: Vacation
priority 10, displaces {Salary}; Salary priority 100, subject to displacement.

```
1. collect claimants for March
     Intervals(planned earnings, 01.03, 31.03) -> [01.03..31.03] Salary 30000
     event documents                           -> [10.03..20.03] Vacation

2. sort by priority                              Vacation(10), Salary(100)

3. interval difference, per type
     Vacation: nobody displaces it            -> [10.03..20.03]
     Salary:   blockers = {Vacation stretch}  -> [01.03..09.03] + [21.03..31.03]

4. working days from the schedule
     Salary   14 days of 21
     Vacation  7 days

5. amounts
     Salary   30000 * 14/21 = 20000
     Vacation average * 7    (by the type's own method)

6. write movements — two rows: factual stretch, days, amount, run number
```

If the *Vacation* document is entered in April, after March is closed:

```
posting registers (employee, March) in the to-recompute register        (§5.4)
April run recomputes March in memory  -> Salary 20000 instead of 30000
writes IN APRIL, marked "for March":  -10000 salary + vacation pay
March movements are never touched                                        (§5.2)
```

### 6.5 Why not a maintenance trigger

The obvious next thought — "we already generate triggers, let the trigger do it" — is right
about the destination (the server) and wrong about the vehicle. The totals trigger machinery
(`databaseMaterializeBuilder.cpp`) rests on two invariants that displacement does not have:

| Invariant the trigger needs | Displacement |
|---|---|
| **Additive**: a change is `-old +new` on an aggregate | inserting a vacation does not add to salary — it makes salary's amount a **different function** of a different day count. There is no delta to subtract on rollback, only a recompute |
| **Local**: a row affects only its own key | a vacation row changes the result of *other types* for the same employee — the blast radius is the whole month, not the row |

Two further consequences seal it: the policy lives in configured data (priorities, the
*Displaces* list, the flags), so a trigger would either bake it into DDL — meaning **changing
a priority becomes a schema migration on a live database** — or read those tables inside every
trigger firing. And displacement is only the first of several run steps; average earnings,
base and year-to-date taxation are not expressible as triggers at all.

So: **the arithmetic goes to the server, but as part of the run's query (§6.2), not as a
maintenance trigger.** The trigger generator earns its keep on the other half of the problem
— registering what must be recomputed (§5.4), which *is* additive and local.

### 6.6 Why not a dedicated engine mechanism

A platform displacement engine fixes one policy: everyone displaces by priority along one
graph. Real payrolls do not agree on that — sick leave displaces salary but not the hazard
supplement; a bonus lands on top of everything; a per-piece earning is never cut. A table plus
a query covers all of them; a mechanism covers only what its author foresaw.

---

## 7. Performance shape — what actually costs

The question "will it hold at volume" has a counter-intuitive answer: **the register's size
is not the problem; the shape of the run is.**

### 7.1 Volume is not the constraint

1000 employees × 30 earning types × 12 months ≈ **360k movement rows per year**, 3.6M over a
decade. With indexes on (employee, period) and on the recorder, that is unremarkable for
Firebird or PostgreSQL. Payroll is a *narrow-and-deep* workload, not a big-data one.

### 7.2 The actual killer is per-row database access

A run written as "for each employee: read planned earnings, read time, read year-to-date"
issues thousands of round trips. At even 1 ms each that is seconds of pure latency before any
arithmetic, and it degrades linearly with headcount.

**The run must be set-based on the read and write sides, row-based only in between:**

1. one query per input for the *whole* run — planned earnings, time, absences, tax parameters,
   year-to-date balances — each returning all employees at once;
2. all arithmetic in memory, over the working table (§5.3);
3. one batched write of movements.

This is a requirement on the configuration, not on the engine, and it is the single decision
that separates a run measured in seconds from one measured in tens of minutes.

### 7.3 `Intervals` and the window node

The natural SQL for milestone slicing is `LEAD(period) OVER (PARTITION BY key ORDER BY
period)`. **The IR has the node as of 2026-08-20** — `ibQueryExpr::m_over` +
`ibRenderOverClause`, gated by `m_features.m_window`
([query-engine-layers.md](query-engine-layers.md) § L2-1). What is still true is that nothing
in `query/` emits one: the existing `ComputeSlice` reaches the same answer through an
aggregate subquery self-joined back to the table, materialising an `ibQueryRamTable`.

Given §6.2, this is not a problem — and the day-grain form makes it disappear entirely.
"Which salary was in force on day D" for every day of the month is the **existing**
`ComputeSlice` shape with one extra grouping key: aggregate `MAX(period)` grouped by
(dimension key, **day**) over `history.period <= calendar.day`, self-joined back for the
record. Same two-level construction that is already written — one more key, no new
mechanism, no window node.

That reframes phase 0: `Intervals` is not a new companion so much as **`ComputeSlice`
generalised from one date to a set of dates**. Whether the two fold into one implementation
is exactly the open question in §10.

**Do not open the window-function arc for payroll.** If windows arrive for other reasons,
this is a good first tenant; the dependency does not run the other way.

### 7.4 What already works in your favour

- **Year-to-date balances** — register totals are trigger-maintained
  (`register-totals-strategy.md`); a run reads them once for everyone instead of aggregating
  history per employee. This is the one place where the totals arc pays payroll directly.
- **Background execution** — `RunBackground` + the worker pool exist today; a run splits into
  batches (by department), each its own session, running in parallel and reporting progress.
  No new infrastructure.
- **Immutability enables archival** — a closed period never changes, so old movements can be
  folded or partitioned without correctness risk. That property comes from §5.1, not from an
  optimisation.

### 7.5 Known sharp edge

If a run query joins a register against an in-memory table, multi-source dispatch may route it
to the RAM path (`RamCompositionNotYet` neighbourhood,
`reference_multisource_execution_boundary`). For a run that is acceptable — the data is in
memory anyway — but it should be a known choice, not a surprise found under load.

---

## 8. What this arc deliberately does NOT build

- A calculation-register metatype — decomposed into interval reads + rule order + totals
- Displacement as *platform machinery* — the behaviour stays (§6), the policy moves to a
  configured table and the arithmetic to the run
- Base-period lookup as a virtual table — that is rule order plus the working table
- A recalculation queue — retro is a movement in the current period (§5.2)
- Timekeeping inside payroll — a separate subsystem; it enters as finished days and hours

---

## 9. Phasing and honest cost

| Phase | Content | Size |
|---|---|---|
| 0 | `Intervals(from, to)` companion (§3) | days — a fourth instance of an existing descriptor. **Worth doing regardless of payroll** |
| 1 | One vertical: hire → planned earning → time → run → payslip → payment sheet. Salary, vacation, sick leave, taxes. One organisation | weeks |
| 2 | Retro, reversal, off-cycle payments, secondary employment | weeks |
| 3 | Statutory reporting and export formats | **unbounded** — a permanent obligation, not a project |

Phase 3 is the one to weigh before starting: statutory maintenance per country, forever, and
it does not scale without a second person.

---

## 10. Unverified / open

- `Intervals` needs a decision on **milestone sources**: cutting at the register's own records
  is clear; whether the caller can inject extra cut points (period boundaries, absence edges)
  is not designed. Oracle models these as proration events.
- Interaction with **register totals** (`register-totals-strategy.md`) is unexamined — a
  year-to-date balance read per employee per run is the hot path of a payroll run.
- Whether `SliceLast` can be expressed as `Intervals` restricted to one point (one mechanism
  instead of two) is worth testing before adding the third companion; the subtractive answer
  may be to fold, not to add.
- Whether the segment arithmetic of §6.2 stays cheap at real headcount is unmeasured. The
  volume argument (tens of segments per employee-month) is reasoning, not a benchmark.
- Access policy over payroll data (salary is the archetypal row-level-secured table) is not
  considered here; `access-policy-rls.md` presumably covers it, unverified.
