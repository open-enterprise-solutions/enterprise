# Register totals — trigger-maintained strategy (proposal)

Architecture proposal for OES register storage that would move the
"totals" maintenance burden into the database via triggers, while
preserving the read latency profile of the classic denormalized
totals-table pattern (as used by comparable enterprise platforms).
Status: **the READING works; the materialisation is declared, applied on a live Firebird, and
survives a register kind switch** (2026-07-29). **The maintenance side landed 2026-07-31** — the
derived table became an ordinary readable source, the shard fold and the parity check exist, and
both sit behind one call a scheduled job makes (§6a, § *What remains* 1–1b). Nothing is wired to a
scheduler yet, and the parity check has not been run against real traffic.

Read the two halves separately, because they landed in the opposite order to this document:

**Working today — through LIVE aggregation.** The accumulation register vends THREE virtual
tables — `.Balance`, `.Turnovers`, `.BalanceAndTurnovers` — registered as query sources, so both
the text query language (L4-1) and LINQ (L4-2) reach them; they lower into the one L3 door, so
there is no second engine. `BalanceAndTurnovers` takes (begin, end, **periodicity**, filter) and
reports opening / receipt / expense / turnover / closing per resource. Periodicity is a QUERY
parameter, not a schema property: one register serves monthly, weekly or quarterly readings of the
same data. It is computed from two server-side aggregates — the balance entering the interval, and
turnovers grouped by the truncated period — folded forward by `FoldBalancesForward`
(`query/queryRamTable`), which is shared rather than owned by the register precisely so the
accounting register reuses it. Correct at any scale; slow at large ones.

**Applied on a live Firebird (2026-07-29).** The bundle is created for real: the totals table with
a receipt/expense pair per resource, its unique key, the three triggers and the read views. Three
things had to be fixed to get there, each worth remembering because none of them was visible in the
generated SQL:

1. **A register with no resources** rendered an empty `UPDATE SET` — valid-looking text that no
   engine parses. Such a register is a legitimate intermediate state, so it now warns and skips the
   bundle rather than failing the update.
2. **The driver splits statements on `;`**, which tore the trigger body apart at `BEGIN … ; … END`
   and produced "unexpected end of command". Rendered text now goes through `RunStatement`, which
   neither splits nor printf-formats it — the latter would also have eaten the `%` in SQLite's
   `strftime`.
3. **A failed DROP rolls back the transaction.** Speculatively dropping a view that was never
   created destroyed the entire restructuring — the first apply killed itself, and the damage
   surfaced later as an error in the trigger. Drops are now guarded by an existence probe
   (`RDB$RELATIONS` / `RDB$TRIGGERS`); a probe is a read and cannot poison the transaction.
   Swallowing the exception, which is what the code did first, hid the symptom and kept the cause.

Still unverified: that the triggers produce CORRECT numbers under real movements, and the parity of
the materialised readings against the live aggregation.

**The register KIND SWITCH — a full lifecycle, not an alter (2026-07-29).** Turning a balance
register into a turnover-only one (or back) is the change that exercised every seam at once, and
each seam had to be repaired:

- **Different structures deserve different tables.** Balances keep a receipt/expense pair per
  resource; turnovers keep one column. The physical names already differed (`…_T` / `…_Tn`), but
  the schema differ matches tables by IDENTITY, not by name — so it read the switch as "the same
  table, renamed" and took the ALTER path against a table that did not exist
  (`CREATE INDEX … Unknown columns`). The identity now moves with the kind, which turns the switch
  into what it actually is: a DROP of one table and a CREATE of the other.

  **Where that identity comes from is the sharper half of the lesson.** The first attempt derived it
  arithmetically from the register's own metaID — `metaID ^ 1` / `metaID ^ 2` — which produced the
  id of the NEIGHBOURING metaobject, because metaIDs are small sequential integers.
  `ibSchemaSnapshot::Shared` matches on id alone and hands back whatever table already carries it,
  name ignored, so the totals declaration poured its columns into an unrelated table. The symptoms
  were duplicate fields and indexes over columns nobody had added, on registers whose neighbours
  happened to exist and in an order that depended on the tree — a fault that reads as "sometimes it
  slips through" while being perfectly deterministic. Moving to a HIGH bit made the collision
  unreachable, but kept the shape of the mistake: a private numbering convention that every future
  totals table would need its own band of.

  **A totals table is now a METAOBJECT**
  (`ibValueMetaObjectAccumulationRegister::ibValueMetaObjectTotals`), one per kind, created in the
  register's constructor as a predefined child and held by a plain `ibValuePtr` — not by a property,
  because there is nothing here to show or edit and the reference is the whole of what is needed.
  Two properties fall out rather than being arranged. Its id is unique BY CONSTRUCTION, because
  `GenerateNewID` walks every child in the tree and a predefined child really is one; and it is
  stable across saves, because the register's own `WriteData` writes each object's whole node — id
  included — as a sub-node (`BalanceTotals` / `TurnoverTotals`) and `ReadData` reads it back. That
  hand-written pair is not ceremony: the generic child walk only descends what `ResolveChild`
  admits, and these are deliberately not in it.
  Declaring only the active kind is what makes the other table absent, so
  the DROP-plus-CREATE needs no rule stating it. The next totals table (the accounting register wants
  several) costs one more child, not one more bit. `Shared` still asserts when a name disagrees with
  an id it already holds, so any future collision announces itself where it happens.
- **The maintenance does not go down with the table.** The triggers hang on the MOVEMENTS table and
  merely MENTION the totals by name, so dropping the totals table leaves them behind, firing on
  every subsequent write into a table that is gone — the movements stop being writable, and the
  failure surfaces nowhere near the change that caused it. The drop path now removes the old
  bundle first, rendered from the OLD spec (`ibDropMaterialization`), which is the only thing that
  still knows the old names.
- **Losing the last resource must still drop.** The bundle used to render as EMPTY when there was
  nothing to accumulate — which is exactly the case where a surviving trigger does most damage,
  because the same apply drops the columns it writes. Drops are now rendered unconditionally; only
  the creates are skipped.
- **The new table has to be filled.** A created totals table starts empty, and empty is not a
  neutral state — it reads as "no stock of anything". `NeedsRegeneration` returns true for a table
  with no baseline, so the switch regenerates. It also returns true when the number of
  accumulations changed, because gaining an expense side changes what the receipt side MEANS: the
  column stops holding every movement and starts holding one direction, so every stored figure is
  wrong even though the column that held it still exists.
- **Accumulating columns carry an identity.** `ibRawDBColumn` gained an optional model id; zero
  still means "scaffold" (created with its table, never migrated). Without one, a totals table
  whose resources changed would keep columns shaped for the old ones. The shard column needed the
  same treatment for the same reason.
- **An untouched register is left alone.** The bundle is still replaced WHOLE when it changes, but
  "whole" is not "always": the apply renders the baseline declaration too and skips a register
  whose rendered text is identical — after PROBING that its objects are actually there, so a
  half-failed earlier apply still heals. Before this, editing one register announced a totals
  rebuild for every register in the configuration, which reads as "everything was recomputed" and
  buries the one line that matters.

**The trigger-maintained totals are now DECLARED.** `ibMaterializationDialect` + the L2-2 renderer
(`databaseLayer/databaseMaterializeBuilder`) + the derived bit and its declaration in the schema
snapshot + L3-4 regeneration (`query/derivedStateBuilder`) are wired into the accumulation
register's `ContributeTables` (`accumulationRegisterSchema.cpp`), and the three queryables read the
views through `RenderMaterializedRead` when the driver materialises, falling back to live
aggregation when it does not. Nothing above L3 changed: the fallback and the materialised path
vend the same relation shape.

The rest of this document describes that half in detail.

---

Earlier status line, kept for the record: **design, with the FIRST slice landed 2026-07-28 — the
materialization dictionary (§4) exists in code.** Landed:
`ibMaterializationDialect` in `databaseLayer/databaseLayer.h`, vended per driver
through `ibDatabaseLayer::GetMaterializationDialect()` (nullable — presence IS
the capability), with dictionaries for Firebird / PostgreSQL / SQLite / MySQL,
a deliberate `nullptr` for ODBC, and structural pins in
`tests/test_materializationDialect.cpp`. Still absent: `totals_*` tables,
trigger generation, totals views, the L3-4 regeneration door, and any read-path
change — the registers continue to serve Balance / Turnover from the live
aggregation (`ComputeBalance` / `ComputeTurnover`). Everything below §4 remains
a proposal; treat every "OES does X" outside §4 as "OES would do X".
Design discussed 2026-04-30, extended 2026-07-22. The 2026-07-22 session added the engine-
integration design (§ *Engine integration* below): the source/derived
table classification driving L3-2/L3-3/L3-4, the per-driver
materialization dictionary, reconciliation-by-cheapest-op, and totals-row
sharding as a per-register mode. Still design-only. Registers today persist movement lines only — there
is no totals/aggregation layer yet, denormalized or otherwise. This
doc describes the *intended* design for when registers/reporting get
built; treat every "OES does X" below as "OES would do X under this
proposal." Expected first target: PostgreSQL production, with FB
embedded sharing the same pattern at smaller scale.

> **TL;DR** — keep the totals table; let SQL triggers maintain it
> instead of OES C++. Read path goes through views, runtime is
> totals-agnostic. No periodic recalculation. No drift. Same read
> performance as the explicit-totals pattern. Slight write overhead
> (~5–15% per movement). Cross-DB via per-driver trigger templates.

---

## Why move maintenance into the database

The classic explicit-totals pattern (other platforms with `mov +
totals` tables — and the shape OES would otherwise grow into) writes
to `mov_X` (the movements table) and then explicitly updates
`totals_X` rows from application C++. Two problems compound — and they
are exactly the trap to avoid when OES builds its totals layer:

1. **Drift.** If the process crashes between the `mov` insert and the
   `totals` update, or if a code path forgets to update `totals`, or
   if a backdated movement is inserted without recalculating
   subsequent periods, totals diverges from movements. The traditional
   compensation is a "totals recalculation" command — periodic full
   rebuild from movements. Operationally heavy, prone to inconsistency windows.
2. **Maintenance code in C++.** Logic for "this kind of register's
   totals update for this kind of dimension/resource" lives in
   `*Query.cpp` files, hand-written per type. Schema changes
   require coordinated changes in multiple places. Bug-prone.

A trigger-maintained totals removes both:

- TX-atomic: every `mov` write fires a trigger that updates `totals`
  in the same transaction. Either both happen or neither.
- Code lives in the database. OES generates the trigger SQL at
  Apply, then forgets about it.

The periodic "totals recalculation" command becomes physically unnecessary —
nothing can drift.

---

## Architecture sketch

```
┌──────────────────┐
│   Runtime (C++)  │
└──────┬───────────┘
       │ INSERT INTO mov_X            (writes only here)
       │ SELECT FROM vw_balance_X     (reads only via views)
       ▼
┌────────────────────────┐    AFTER INSERT/UPDATE/DELETE     ┌─────────────────┐
│  mov_X                 │ ───────────  trigger ───────────► │  totals_X       │
│  (period, dim*, res*)  │                                   │  (period, dim*, │
│  PRIMARY KEY (id)      │                                   │   res*)         │
│  INDEX (dim*, period)  │                                   │  PRIMARY KEY    │
└────────────────────────┘                                   │  (period, dim*) │
                                                             └────────────────┘
                                                                     ▲
                                                                     │ SELECT
                                                ┌────────────────────┴──────────┐
                                                │ vw_balance_X / vw_turnover_X /│
                                                │ vw_slice_last_X (read API)    │
                                                └───────────────────────────────┘
```

For the runtime, `totals_X` is implementation detail. Views are the
public read API. `mov_X` is the public write API. Strategy can swap
later (live aggregation, partitioning, MSSQL indexed views, etc.)
without touching runtime code.

---

## Engine integration (design extension — 2026-07-22)

The sketch above is generic. This section is how the totals layer plugs into the OES query
engine (L1–L5, see [query-engine-layers.md](query-engine-layers.md)) so that the read path
stays coherent and the maintenance stays out of C++.

### 1. One bit: source vs derived

Every table a metaobject contributes (via its `ContributeTables` snapshot) is classified
**source** or **derived**. This single classification drives three floors correctly:

| Table | Class | L3-2 (structure) | L3-3 (data mover) | L3-4 (regeneration) | Read |
|---|---|---|---|---|---|
| `mov_X` | **source** | generate + migrate | round-trip (dump **and** load) | — | physical scan |
| `totals_X` + triggers + views | **derived** | generate + migrate | **never touched** | regenerate | co-locatable queryable |

`totals_X` is *derived state*: never dumped (regenerated at the destination), never migrated
(dropped + regenerated), never hand-maintained (the trigger owns it). One bit — "derived" —
says all three: *don't move it, regenerate it, keep it consistent by trigger.*

### 2. Three actors, three verbs — do not conflate them

- **Trigger — *updates*.** Per-movement delta, in the DB, TX-atomic, **unbypassable**
  (fires for any writer to `mov_X`: runtime, plugin, script, restore), **never invoked**.
  This is steady state.
- **L3-4 — *regenerates*.** Rebuilds totals from source, **invoked** on discrete events
  (Apply / structure-change, bulk import, migration). Not per-movement. **Not a cadence.**
- **L3-2 — *builds structure*.** Generates and migrates the whole bundle: `mov_X`, `totals_X`,
  the three triggers, the three views.

The seam that rusts: "update" is the trigger's job, never L3-4's. The moment L3-4 (or the
compute server) runs *per-movement* totals updates, you are back in the bypassable
managed-code pattern and drift returns. Keep the verbs apart: **trigger updates, L3-4
regenerates.**

### 3. Read side — totals become an ordinary queryable

Reads go through `vw_balance_X` / `vw_turnover_X` / `vw_slice_last_X`. The register's queryable
points at the view, so `IsComputedInRam()` flips **false** (was true for the RAM-computed
`ComputeRows` path). Now totals are an **ordinary physical co-locatable source**: they JOIN,
UNION, temp-promote, keyset-page and RLS-restrict through the *existing* L3 machinery, with no
special case above the view. This is the migration in one seam: `IsComputedInRam → false`,
`GetQueryable → view`; the read path (L3-1/L4/L5) does not change.

**Keep the view thin** — `SELECT … FROM totals_X`, not `GROUP BY` over `mov_X`. A thin view
co-locates and stays O(log N); a fat live-aggregation view JOINs syntactically but recomputes
every read and does not break the scale ceiling. JOINability comes from the thin table shape;
speed comes from the trigger-maintained table under it. Two separable properties, both needed.

### 4. The materialization dictionary — a *second* per-driver dictionary

The trigger bodies are imperative and **per-engine divergent in structure**, not just in
spelling. They therefore do **not** fit the L2-1 **query** dialect dictionary (which substitutes
tokens over a *shared* structure — `LIMIT`, quoting, type mapping). Forcing them into L2-1 would
either pollute the query IR with imperative nodes (`RETURN`/`IF`/`NEW`/`OLD`) or add a raw-SQL
escape hatch that breaks L2-1's "never raw SQL" invariant.

Instead, each driver vends a **second dictionary — the materialization dialect** — parallel to
`GetDialect()`:

- **L2-1 query dictionary** renders the *declarative skeleton*: `CREATE TABLE mov_X`/`totals_X`,
  indexes, the view's inner `SELECT`, the rebuild `INSERT … SELECT … GROUP BY`, `ALTER`.
- **Materialization dictionary** renders the *imperative delta*: trigger shell + upsert idiom +
  hot-update hints. Used only at Apply / regeneration, never on a read.

Shape it as a 2-D table, **`kind × engine`**:

- **`kind`** (turnover `+=` / running-balance `+=` + bump-later-periods / slice-last) is the
  **delta-clause parameter** — the same concept across engines.
- **`engine`** is the **key**, and it splits into two template **families** by *execution model*:
  - **per-row** — Firebird PSQL, PostgreSQL PL/pgSQL, MySQL SP, SQLite triggers. All are
    `FOR EACH ROW`, use `NEW`/`OLD`, and reduce to a handful of plain SQL statements. Design to
    the **SQLite floor** (SQL-statements-only, no procedural block) and the same logic fits all
    four; only the upsert idiom (`ON CONFLICT DO UPDATE` for PG/SQLite, `ON DUPLICATE KEY` for
    MySQL, `MERGE` for Firebird) and the shell (PG needs a separate `FUNCTION`; the rest inline)
    differ — those are the per-engine *slots*.
  - **set-based** — MSSQL/T-SQL (`inserted`/`deleted` pseudo-tables, batch `MERGE`). This is a
    *different algorithm*, not a different word, which is exactly why a separate dictionary
    holds it where the query dictionary could not. **Deferred to the MSSQL port** — not one of
    the four native engines; adding it later is one more engine key, not a reshape.

Both families implement one interface (`RenderTotalsTrigger(kind, dims, res) → SQL`); the L3-2
generator is engine-agnostic and only fills parameters. The dictionary is also the natural home
for the future pluggable strategy (MSSQL `INDEXED VIEW`, Oracle `MV REFRESH ON COMMIT`) — a
different entry per engine, no generator change.

#### 4a. As landed (2026-07-28) — and two shapes the accounting register forced

`ibMaterializationDialect` is pure declarative facts plus the `ibTriggerFamily` discriminator,
mirroring `ibTempTableDialect` exactly: vended nullable, presence = capability, behaviour in the
generator that reads it. Firebird carries the structural outlier the doc predicted — its
accumulate is a `MERGE`, because `UPDATE OR INSERT … MATCHING` cannot read the target's current
value and can therefore only *replace*. That is valid SQL producing silently wrong totals, so it
is pinned by test.

Two slots go **beyond** the sketch above, both because the accounting register is the
destination and both cheap to add now / expensive later:

- **The delta upsert is an `INSERT … SELECT … WHERE`, not `INSERT … VALUES`.** An accounting
  movement carries a debit side and a credit side; each feeds a *different* totals table and
  either may be absent, so the delta must be **conditional**. `VALUES` cannot carry a predicate,
  and the alternative — a procedural `IF` — breaks the SQLite floor and splits the per-row family
  in two. `INSERT … SELECT … WHERE` says the same thing declaratively on all four engines. An
  unconditional delta (an accumulation register) renders the guard empty and pays nothing. This
  is also why MySQL's dialect now sets `m_selectFromDual = DUAL`: a FROM-less `SELECT` there
  cannot carry a `WHERE`.
- **A trigger body is N statements, not one.** One movement can feed several totals tables
  (debit turnover, credit turnover, later per-subconto breakdowns). The shell takes a rendered
  multi-statement body, so the multi-table case inherits the same TX atomicity as the
  single-table one and needs no coordination above.

Neither shape costs the accumulation register anything, which is the point: the register we
harden on and the register we are aiming at share one mechanism rather than two.

#### 4b. Periodicity is a READ parameter, not a schema property

The shape to build against — settled 2026-07-28, and it changes what the virtual tables are:

**Periodicity is chosen per QUERY**, alongside the period bounds and the filter, not declared
once on the metaobject. One register therefore serves monthly, weekly and quarterly readings of
the same data with no schema change and no second source.

**It selects the COLUMN SET, not just the grouping.** The virtual table's fields are a function
of the requested periodicity: `Period` plus one column per unit **coarser than or equal to** it
(`PeriodDay`, `PeriodWeek`, `PeriodMonth`, `PeriodQuarter`, …). Ask for monthly and the daily
column is not merely empty — it is *absent*, because the information does not exist at that
grain. Two consequences worth stating plainly:

- The queryable's column contract becomes **parameter-dependent**. Today a queryable vends the
  metaobject's fixed columns; here `GetColumns()` is a function of the periodicity argument. The
  seam already exists — the descriptor's `CreateQueryable(params…)` passes read parameters into
  the queryable's constructor (that is how `ibBalanceQueryable` already takes period + filter),
  so periodicity rides the same road.
- **Periodicity also picks the SOURCE.** Coarser-or-equal to the stored grain → read the totals
  view. Finer than stored — including the two non-time granularities, per-recorder and
  per-record — → read the **movements** and aggregate live, which is also where `Recorder` /
  `LineNumber` columns come from. `MakeProvider` already chooses backing once per query; this is
  that decision, driven by one comparison on the ordered `ibTotalsPeriod` enum.

So `ibTotalsPeriod` is ordered coarsening BY CONTRACT: a projection is derivable exactly when its
unit is `>=` the stored unit, and the generator must refuse rather than invent when it is not.

**Storage granularity** stays a schema decision — one unit per register, the floor on what can be
read back. It only has to be fine enough to serve the finest periodicity anyone will ask the
totals for; everything below that floor is served by the movement table anyway. Note this is *not*
the existing `ibPeriodicity` (`eNonPeriodic` / `eWithinSecond` / `eWithinDay`), which is the
granularity of a record KEY — a different question that happens to share a word.

**The truncation map covers every unit on every engine.** `m_periodTrunc` maps `ibTotalsPeriod` →
an expression template, and the same expression serves both roles — keying a totals row inside a
trigger and projecting a coarser column inside a view — so the stored key and the read column
cannot drift apart. Coverage is total on all four engines (`Second … Year`, including `Week`,
`TenDays`, `HalfYear`), and the tests enforce it: partial coverage would make a query's answer
depend on which engine the deployment happens to run. Three details are pinned because they are
easy to get subtly wrong: the week starts **Monday** everywhere (Firebird and MySQL need opposite
corrections to get there), the ten-day offset is **capped at 2** so day 31 folds into the third
period instead of opening a fourth one-day bucket, and Firebird truncates sub-day units
**textually** rather than through `EXTRACT` + `DATEADD`, whose fractional seconds would round.

#### 4bis. What L3-2 materialises is a SUBSYSTEM, not a table

Worth stating before the mechanics, because it decides what "done" means. The unit L3-2 builds is
not a totals table — it is the **apparatus that answers totals questions**: the table, its three
maintenance triggers, and the read view, created and dropped as one. The table alone answers
nothing; the triggers alone maintain nothing; the view alone has nothing under it.

What that apparatus has to serve is four readings, all parameterised by a period the caller
supplies at query time:

| Reading | Asked as | Answered by |
|---|---|---|
| Balance **as of a date** | one date | `Σ(receipt − expense)` over periods `<= date` |
| Balance at the **start** of an interval | interval | the same fold over periods `< begin` |
| Balance at the **end** of an interval | interval | the same fold over periods `<= end` |
| **Turnover** over an interval | interval | `Σ receipt`, `Σ expense` over `begin … end` |

All four are range folds over ONE stored form — per-period receipt and expense buckets. That is
the property to protect: adding a reading must not add a stored form. `BalanceAndTurnover` is not
a fifth thing to store, it is the first three and the fourth reported side by side, which is
exactly why it can exist at all without a second table.

The date is a QUERY parameter, so it cannot live in the view — a view has no arguments. The
division is therefore: L3-2 materialises what is date-independent (the buckets and their upkeep),
and the three queryables fold them at read time with the caller's dates. Push a date into the
materialised side and you get one view per question and no way to ask a new one.

#### 4c. What BalanceAndTurnover needs — and why net storage is not enough

The third virtual table (`BalanceAndTurnover`, alongside `Balance` and `Turnover`) reports, per
period and per resource: **opening balance, receipt, expense, turnover, closing balance**. That
list dictates the storage shape, and it corrects the first sketch of it.

**Store receipt and expense SEPARATELY — not one signed net column.** Net is receipt − expense,
and from a net you cannot recover either side; a period that took in 100 and paid out 100 is
indistinguishable from one with no movement at all. `ComputeTurnover` already reports the three
columns (`_Turnover` / `_Receipt` / `_Expense`) for a balance register, so a net-only totals table
could not serve the reading that exists today. Two stored columns per resource, and every reported
column derives from them:

| Reported | Derived as |
|---|---|
| Receipt / Expense | stored directly, summed over the interval |
| Turnover | `receipt − expense` over the interval |
| Opening balance | `Σ(receipt − expense)` over every period **strictly before** the interval |
| Closing balance | opening + turnover |

A turnover-only register (`eTurnovers`, no record type) has no sign and therefore **one** stored
column per resource. So the column count of the totals table is a function of the register type —
a generator concern, not a dictionary one: the delta expression stays an unconditional
accumulate, it is only its *value* that carries the `CASE WHEN recordType…` that splits the sides.
No new dictionary slot; this is why §4a's shape holds.

**This is also what settles the storage question.** Per-period receipt/expense buckets give all
five reported columns from one table: the opening balance is an aggregate over periods before the
interval, which is an index range scan, not a walk of movements. That is what makes a stored
running balance unnecessary in v1 — and a backdated movement still touches only its own period's
row, with no bump of later periods, because no later row contains a carried total.

#### 4d. The fill method — boundary rows, not a calendar

Without filling, a period with no movements produces no row. The consequence is not cosmetic: at
the edges of the requested interval you lose exactly the numbers the report is usually for — the
balance as it stood entering the interval and as it stood leaving it. Whatever happened inside is
bounded by those two, and without them the reading is open-ended.

Filling adds rows at the **interval boundaries** — the period containing the start and the period
containing the end — carrying zero turnover and the balance carried in. Worth stating because it
determines the cost: this is **two synthetic rows unioned in**, not a generated calendar joined
against the totals. No `generate_series` / recursive-CTE dialect slot is needed, and the fill
therefore costs the same on the embedded engine as on the production one.

(Filling every empty period inside the interval, rather than only the boundaries, is a separate
and more expensive mode. Not in scope; if it is ever wanted, it is the one that needs the
calendar.)

#### 4d-bis. Not yet served: recorder and line number

A reading may legitimately ask for granularity FINER than any period — per **recorder** (the
document that produced the movement) or per **record** (its line). These are not truncations of a
period; they are the movement row itself. Totals cannot answer them at all: folding movements into
daily rows is precisely what discards the recorder.

They therefore belong to the MOVEMENTS, and the register already stores both columns. What is
missing is the ROUTING — a virtual table asked at recorder / record granularity must read the
movement table instead of the totals surface, the same way a *periodised* balance-and-turnover
reading already falls back to the live path. Until that lands these two granularities are simply
not offered, which is the honest state; the failure to avoid is accepting the parameter and
answering at the wrong grain.

This is the boundary `ibTotalsPeriod` draws — everything in the enum materialises, everything
finer is read from the source — and the enum deliberately has no member for either.

#### 4e. A zero balance is an ABSENT row — and the filter runs after the fold

No stock of an item means no row for that item. Not a row of zeros: **no row**. `ComputeBalance`
already encodes this as a `HAVING` over the aggregate, OR-ed across resources — the row survives
if *any* resource is non-zero, so an item with quantity 0 but amount 5 still reports. The
trigger-maintained path must produce the same observable, and that is a read-side filter, not a
storage rule.

**The filter applies to the FOLDED value, not to the stored rows.** An item that took in 100 and
paid out 100 has two non-zero totals rows and a balance of zero — so it must not appear in a
balance reading, even though its storage is not empty. Hence: aggregate over the interval first,
filter after. Applying it to stored rows would be both wrong and pointless.

This is the invariant most likely to break the parity test in a way that looks like a rounding
bug, because the two paths fail differently: the live aggregation never emits the row, while a
naive view over the totals table emits it filled with zeros. Same numbers, different row count —
and a report that lists every item ever traded instead of the ones on hand.

**It also settles when a totals row may be deleted: essentially never.** A zero *balance* does not
mean zero *turnover* — the receipt and expense that cancelled out are themselves reportable
figures for that period, and deleting the row would destroy them. Only a row whose receipt AND
expense are both zero carries no information, and such a row is never created in the first place.
So the totals table has no cleanup pass, and no trigger-side delete: rows accumulate as history,
and emptiness is expressed by the read filter alone.

> **ODBC has no materialization entry.** ODBC is generic — it does not know the backend, so it
> cannot template engine-specific triggers. An ODBC-backed register stays on the
> live-aggregation / RAM fallback (`IsComputedInRam` stays true); it never gets the trigger
> upgrade. Not a blocker — ODBC is a marginal target — but it is the one driver the dictionary
> does not cover.

### 5. Reconciliation by the cheapest sufficient operation

The "What OES Apply generates" section below always full-rebuilds on a structure change. That
is correct and simple, but on large registers the full `mov_X` scan is minutes-to-hours. Most
structure changes do **not** need it — their effect on totals is *derivable*:

| Change | Reconciliation | Full `mov` scan? |
|---|---|---|
| Add a registrar | nothing (future movements → trigger) | no |
| Remove a registrar | delete its `mov` rows → trigger reverses their deltas | no (delta) |
| New resource column | `ALTER totals ADD res DEFAULT 0` (no history → 0) | no |
| New dimension, uniform default | `ALTER totals ADD dim = default` (grouping unchanged) | no |
| Drop a dimension | **coarsen** — re-aggregate from the *existing* `totals`, not `mov` | no |
| Full data load (restore / migrate) | totals start empty → rebuild | **yes** |
| Grouping changes un-derivably (backfill dim with per-row values, finer period) | rebuild | **yes** |
| Bulk import | aggregate **only the imported batch** and merge (`… GROUP BY … FROM batch`) | no (delta of the batch) |

So reframe L3-4: it is not "the full-rebuild door", it is **"reconcile derived state by the
cheapest sufficient operation"**, and the full scan is its *fallback mode*. Preference order:
trigger delta (steady state) → subset delta-aggregate (import, removed registrar) → in-place
totals transform (add-default-column, coarsen) → full `mov` scan (empty totals / un-derivable
regroup).

**Correctness gate (same asymmetry as everywhere): a delta / in-place path is valid ONLY for
change kinds whose totals-effect is provably derivable.** Misclassify — delta where a scan was
needed — and you get *silently wrong totals*. When unsure, full-scan. Full-scan is the safe
default; deltas are the optimization applied to provably-derivable kinds. Ship the full-scan
fallback first; add the delta paths when a real big-register Apply window forces it.

### 6. Split totals — sharding a hot row (a per-register mode)

Hot-row write contention on a single `(period, dim)` (a popular SKU in the current period,
dozens of concurrent postings) is mitigated by **sharding the totals row** N-ways. Comparable
platforms offer the same switch; what differs here is the substrate. Ours is a **trigger**, so a
shard cannot drift from the movements it summarises — the split changes only how many rows one
logical key occupies, never who maintains them. Where the maintenance lives in application code
instead, it is bypassable, and a periodic full recalculation becomes a standing operational chore.

A per-register spec parameter, `sharded: N`:

- `N > 1` → the totals PK gains a shard column `(period, dim1..dimN, shard)`; the trigger's
  upsert writes `shard = hash(connection / tx)` chosen **locally**; the view sums shards
  (`SELECT … SUM(res) … GROUP BY period, dim`).
- **The view absorbs sharding** — the read path above the view is unchanged, co-locates like any
  table. Sharding does not leak upward; it is contained in `table + trigger + view`.

Two properties make it as autonomous as the base deltas — no coordination, no extra state:

1. **Local shard key.** `hash(current connection/tx)` — not a shared counter (which would itself
   contend). Concurrent writers hash to different shards.
2. **Sum-invariance.** An insert's `+delta` and a *later* delete's `−delta` may land in
   **different** shards; because the read **sums all shards**, the total is exact regardless of
   distribution: `Σ shards = Σ inserts − Σ deletes = net`. So the trigger stores **no memory**
   of which shard a movement used — it just hashes the current context. Individual shards hold
   partial/odd values; the read-sum is always exact. Each shard row is still trigger-maintained
   TX-atomically → **drift-proof is preserved.**

**Tradeoff:** read `O(1)` → `O(N)` (sum N shards). Sharding is **per-register** — the read tax
hits *all* its rows, not just the hot one — so shard registers profiled hot on write, not everything
by reflex.

> **Amended 2026-07-31 (§6a).** The read tax is no longer permanent: the fold collapses closed
> periods back to one row per key, so only the OPEN period stays spread. That changes the arithmetic
> behind the default — a widely-enabled split now costs a handful of adjacent rows on the current
> period rather than N rows across all history. Widely-enabled is defensible once the fold runs on a
> schedule; until it does, the tax is still standing and the switch stays off by default.

#### When it actually helps — and one guess this document got wrong

Splitting cures **contention for one row**, not size. The question is never "is this register
big?" but **how many writers converge on the SAME key at the same time**. Two things make them
converge, and the second one was missing from this section entirely:

- the write STREAM narrows to one key — a revenue / VAT account every posting touches, a
  fast-moving SKU, a dominant counterparty;
- the write must first READ the row under a lock — which turns even a moderate stream into a queue,
  because each writer holds the row while it decides what to write.

| Shape | Split? | Why |
|---|---|---|
| An account like revenue / VAT with no or coarse subconto | **yes** | every posting in the company lands on one key per period |
| Retail with a fast-moving SKU | **yes** | hundreds of tills, one product row |
| Settlements with one dominant counterparty | **yes** | same row, many writers |
| Any register with fine-grained dimensions and no read-back | **no** | movements already land on different rows |
| A rarely-posted register | **no** | no concurrency to relieve |

> **Correction (2026-07-31).** This table used to list **batch (lot) accounting** as the trap — heavy
> in rows, light in contention, because each delivery has its own key and two tills rarely write the
> same lot. That reasoning covers the RECEIPT side and quietly assumes the ISSUE side works the same
> way. It does not. FIFO issue draws from the OLDEST open lot, so the entire sales stream for a
> product converges on ONE lot row until it is exhausted — and each issue must read the remaining
> quantity under a lock before it can decide how much to take. Batch accounting is therefore heavy in
> rows AND contended, which is the case for splitting, not against it.
>
> The reasoning had checked half the traffic. What decides the answer is not the kind of accounting
> but whether the issue is computed **synchronously at posting**: an online FIFO is hot, the same
> lots settled by a periodic close are not — one writer, no convergence. Kept here rather than
> quietly rewritten, because the shape of the mistake is the lesson: a contention claim that only
> examines writes and not read-backs is only half-checked.

**The measurable symptom** of real contention: posting time grows NON-LINEARLY with the number of
concurrent users at unchanged document size — five cashiers fine, twenty four times slower rather
than evenly slower. If it is equally slow with one user and with twenty, the bottleneck is
elsewhere and shards will not touch it.

This is also why the switch defaults to OFF even for an accounting register: the decision belongs
to a specific account under a specific load, not to a metaobject KIND. Cleanest for turnover (`+=`); running-balance shards ride the same
sum-invariance but the subsequent-period bump is inherently fiddlier (it is fiddly without
shards too).

**Turning the switch is a structure change, not a setting (2026-07-29).** The shard column joins
the KEY, so flipping it re-keys every stored row. Two attempts got this wrong before the third one
stopped trying to be clever:

1. **Scaffold.** Declared with the table and never migrated, so enabling the split rebuilt the
   unique index around a column that had never been added (`Unknown columns in index`).
2. **A migratable column.** Better, but it only moved the problem: an existing database could
   already hold the field from the scaffold era, and `ALTER TABLE … ADD` on an existing field does
   not merely fail — on Firebird it rolls the whole restructuring back.
3. **Don't migrate it at all.** A derived table holds no information of its own; the regeneration
   recomputes every row anyway. So when `NeedsRegeneration` says the shape changed, the differ
   DROPS the table and creates it fresh — which takes the columns and the indexes with it and
   removes the entire class of migration edges at once. The column still carries an identity,
   because the declaration must state what the table holds; it simply no longer has to be reachable
   by an ALTER. That identity is the totals METAOBJECT's own metaID — there is exactly one shard
   column per totals table and it belongs to that table, so no second id has to be invented for it,
   and a table id and a column id are matched in different places to begin with.

**The switch also has to SURVIVE a save.** It did not: `SplitTotals` was declared as a property and
never written into (or read back from) the register's node, so every reload silently returned it to
off — the one failure mode that looks like the feature working, since the totals stay correct while
the contention it was turned on for comes back. Fixed with the totals metaobjects, which travel the
same road: a property that is not in `ReadData` / `WriteData` does not exist past the session that
set it.

Regeneration with shards is trivial and stays so: the bulk rebuild writes one consolidated row per
key into **shard 0** (a rebuild is a single writer, and there is nothing to spread), and the
trigger distributes everything that follows through the hash. The reads never notice — the view
sums the shards, which is the sum-invariance the whole scheme rests on.

#### 6a. Folding the shards back — what makes the split affordable (landed 2026-07-31)

A split trades write contention for read width: one logical key occupies N rows forever, and every
read of it sums N rows forever. That standing tax is what kept the switch off by default and made
"split everything" a bad idea rather than a lazy one.

**The fold removes the tax where it earns nothing.** `ibDerivedState::Collapse` re-packs a key's
shards back into one row — reading the TOTALS table, never the movements, so it costs a fraction of
a rebuild. Applied to a period nobody writes to any more, it is permanent: a closed month folds to
one row per key and stays there. Only the open period stays spread, which is where the split is
actually doing its job. History reads at N = 1, and the shards on top of the current period are a
handful of rows sitting adjacently in the index (the shard column is the key's last segment, so
reading them is one range scan, not N lookups).

Three properties make it safe to run while people work, and all three come from moving figures with
ARITHMETIC rather than rewriting them:

- **`col = col + delta`, computed by the DB** (the door's `AddValue`). A movement landing mid-fold
  composes with the adjustment instead of being overwritten by it — the same reason the trigger can
  accumulate without coordinating with anyone.
- **A drained row is dropped only when it is provably empty.** One that took a delta mid-fold stays,
  its contribution intact, and the next pass folds it.
- **One transaction per KEY** — the finest scope that is still atomic (the add / subtract pair), so a
  row lock lives three statements rather than a table's worth of them.

The frontier is DERIVED, never stored: everything strictly before the current stored period, worked
out from the table's own unit and the clock. There is no "how far have we folded" state to keep and
therefore none to get out of step — the fold sees what is folded by counting rows per key.

**One fold per table at a time.** Concurrent writers are fine; two folds racing on one table are not,
and the failure is worth stating because it nearly works. Both move the same figure twice, which the
sum survives (it skews the distribution and the next pass straightens it) — until one drains a row to
zero and deletes it while the other still holds that row's value: its addition lands, its subtraction
hits nothing, and the key's total grows. Serialise at the job level; different tables are independent.

**Not running it is safe.** Shards read exactly right at any distribution, so a skipped fold costs a
slightly wider read and nothing else. This is housekeeping, never correctness — which is precisely
why it can run unattended.

### 7. Who invokes L3-4 (deployment)

Same L3-4 operation; different **dispatcher**:

- **Server mode** (compute server present): a **compute-server job** — chunked, resumable,
  under exclusive/Apply. Fits the [compute-server-tiering.md](compute-server-tiering.md) arc:
  heavy server-side regeneration is exactly such a tier.
- **File / embedded mode** (no compute-server process; single session → already exclusive): an
  in-process **background task**. Keep it a **janitor** (detect-and-repair an *incomplete*
  bulk/crash: trigger missing / totals stale after an interrupted operation → rebuild), **not a
  patrol** (periodic recalculation of trigger-maintained totals — the standing chore the trigger
  makes unnecessary by construction). Fire it on **events**, not on a cadence.

---

## What OES Apply generates per register

On `createMetaTable`:

1. `CREATE TABLE mov_X (id, period, dim1..dimN, res1..resM)` with
   indexes on `(dim*, period)` for write-side lookup.
2. `CREATE TABLE totals_X (period, dim1..dimN, res1..resM)` with
   `PRIMARY KEY (period, dim1..dimN)` and `fillfactor = 80` on PG
   (enables HOT updates).
3. `CREATE TRIGGER tr_mov_X_ai/au/ad` — three triggers (after
   insert / update / delete), updating `totals_X` with delta.
4. `CREATE VIEW vw_balance_X / vw_turnover_X / vw_slice_last_X` —
   read paths. Trivial views over `totals_X` initially, can grow
   smarter (multi-level snapshots, etc.) later.

On `updateMetaTable` (structure change):

1. `DROP TRIGGER` on `mov_X` (old column list is stale).
2. `ALTER TABLE mov_X` for added/dropped/renamed/retyped columns.
3. `DROP TABLE totals_X` (it's derived state, regenerable from `mov`).
4. `CREATE TABLE totals_X` with new schema.
5. `INSERT INTO totals_X SELECT period, dim1..dimN, SUM(res1)... FROM
    mov_X GROUP BY period, dim1..dimN` — one-time rebuild.
6. `CREATE TRIGGER` with new column list.
7. `CREATE OR REPLACE VIEW` for the three views.

The rebuild step is the only "expensive" part of a structure change.
Cost on PG with proper indexes:

| `mov_X` size  | Rebuild time |
|---------------|--------------|
| 1 M           | ~1–3 s       |
| 10 M          | ~10–30 s     |
| 100 M         | ~5–10 min    |
| 1 B           | ~1–2 h (needs partitioning + parallel) |

Apply runs in OES exclusive mode anyway, so taking that wall-clock
for a structure change is acceptable.

---

## Trigger shape (PostgreSQL example)

For an accumulation register (additive resources):

```sql
CREATE FUNCTION fn_mov_X_ai() RETURNS trigger AS $$
BEGIN
  INSERT INTO totals_X (period, dim1, dim2, qty)
    VALUES (date_trunc('month', NEW.period), NEW.dim1, NEW.dim2, NEW.qty)
    ON CONFLICT (period, dim1, dim2)
    DO UPDATE SET qty = totals_X.qty + EXCLUDED.qty;
  RETURN NULL;
END;
$$ LANGUAGE plpgsql;

CREATE TRIGGER tr_mov_X_ai AFTER INSERT ON mov_X
  FOR EACH ROW EXECUTE FUNCTION fn_mov_X_ai();
```

Update / delete triggers symmetric — apply `OLD.qty` reversed,
then `NEW.qty` (update) or just reverse `OLD.qty` (delete).

For Firebird the same shape with `UPDATE OR INSERT MATCHING`
instead of `INSERT ... ON CONFLICT`.

---

## Performance profile

### Read path (balance / turnover / slice-last queries)

Identical to the existing explicit-totals path — `SELECT * FROM totals_X
WHERE period <= ? AND dim1 = ?` is an index lookup, O(log N).
Fitness for ~10 K concurrent readers asking for current balance:

- ~1000–2000 SELECT/sec sustained even on a single PG instance —
  buffer cache holds the index hot, each lookup is 4–6 page reads.
- Hot index pages live in `shared_buffers` permanently for an active
  register. Cold reads of historical periods touch disk but are rare.
- 10 K nominal users → ~500–1000 active at peak; well below PG's
  ceiling for indexed-lookup workloads.

### Write path (movement INSERT triggering totals UPDATE)

For 100 active writers (typical document-posting workload):

- ~5–10 movements per posted document, ~1 doc/min per writer →
  500–1000 mov-writes/min → ~10–20 trigger fires/sec.
- Per-row UPDATE on `totals_X` with `fillfactor = 80` triggers PG
  HOT update path: same page, no index modification. ~0.1–0.5 ms.
- Total CPU/IO on totals_X: trivial. Hot-row contention only
  becomes visible if many writers concentrate on a single
  `(period, dim1, dim2)` combination — unusual for typical
  accounting workloads, lethal for warehouse-stock single-SKU
  scenarios. Mitigation if needed: shard the totals row (N rows
  per combination, hash by connection id, aggregate on read).

### Backdated movement

INSERT with old period → trigger updates the totals row for that
period. For running-balance registers the trigger also bumps
`qty` on subsequent periods; that's O(K) where K = number of
later periods affected (typically tens, not millions). Same cost
as an app-side rebuild for the same scenario.

### Bulk import

Each row fires a trigger. 1 M-row import = 1 M trigger calls. Slow
(50–500× slower than triggerless append). Workaround: temporarily
`DROP TRIGGER`, bulk insert, `INSERT INTO totals_X SELECT ... GROUP
BY ... FROM mov_X`, recreate trigger. This is exactly the
"manual rebuild" we wanted to avoid — but only for bulk-import,
not steady state.

---

## Cross-DB strategy

| Engine     | Materialisation strategy                  | Notes                                          |
|------------|-------------------------------------------|------------------------------------------------|
| PostgreSQL | Triggers + totals table + views           | Production target. PG `MV` is manual REFRESH only. |
| Firebird   | Triggers + totals table + views           | Embedded / SMB. Same pattern as PG, PSQL syntax. |
| MSSQL      | Same — or `INDEXED VIEW WITH SCHEMABINDING` | Native indexed view = best, future optimisation. |
| Oracle     | Same — or `MV REFRESH FAST ON COMMIT`     | Native MV with log = best, future optimisation.  |
| MySQL      | Triggers + totals table + views           | No native MV; triggers are the only path.        |
| SQLite     | Triggers + totals table + views           | No materialised views. Single-process scope only. |

Implementation plan:

1. **Step 1 — single strategy**: triggers + totals + views,
   per-driver SQL templates. Works everywhere, cleaner code.
2. **Step 2 (optional)**: pluggable `ibRegisterTotalsStrategy`
   interface. MSSQL strategy emits `INDEXED VIEW`, Oracle emits
   `MV REFRESH ON COMMIT`. Other drivers stay on the trigger
   path. Add only if profiling shows it matters.

Don't proliferate strategies prematurely — the trigger pattern
covers all five engines uniformly.

---

## What this would give OES

(All prospective — none of this exists yet; see status note at top.)

- `mov_X` ↔ `totals_X` cannot drift; no `RebuildTotals` command ever
  needs to be written.
- Periodic "totals recalculation" is unnecessary by construction.
- Backup / restore: `mov_X` is the source of truth. `totals_X` is
  cache, regenerable with one SQL statement.
- The totals-maintenance logic never enters business-path C++ — the
  trigger owns it, so the explicit-totals UPSERT code OES would
  otherwise hand-write per register kind is avoided up front.
- Schema changes go through `INSERT INTO totals_X SELECT ... GROUP
  BY ...` — one SQL statement, no per-case rebuild logic per
  register kind.

## What this costs

- ⚠️ Per-driver trigger SQL templates — 5 dialects to maintain.
  Single template per register kind, parameterised by dimension /
  resource list. Maybe 200–500 lines of generator code total.
- ⚠️ Bulk import paths need to opt out (drop-trigger / rebuild)
  pattern. One helper, used by import-specific code paths.
- ⚠️ Trigger errors surface as obscure SQL exceptions instead of
  C++ stack traces. Operational investigation cost goes up slightly.
- ⚠️ Schema-change `INSERT...SELECT` rebuild is an Apply-window
  long-task on big tables (minutes for 100 M rows). Acceptable for
  Apply, painful if accidentally triggered at runtime.

---

## What this is NOT

- **Not "remove the totals table".** Pure live aggregation through
  `CREATE VIEW ... GROUP BY` works only for tiny registers (< 1 M
  movements). For OES production scale (10⁷+ movements over years,
  10 K active readers), live aggregation requires CPU resources that
  scale with data volume; it doesn't make sense as the primary
  storage model. The trigger-maintained totals proposal is a
  refactoring of *who maintains totals*, not a removal of totals.
- **Not "use materialised views" on PG.** PG's materialised views
  have only manual `REFRESH MATERIALIZED VIEW`. They're snapshots,
  not auto-updating. Triggers are the actual auto-updating
  equivalent on PG.
- **Not 1000 user scale on FB embedded.** Heavy production deploys
  on PostgreSQL. FB embedded targets ~20–50 users. The
  architecture is the same on both; scale comes from the engine
  choice, not from the schema.

---

## Required PG-side configuration for production scale

Independent of this architecture, but essential to get the
performance numbers above:

- `fillfactor = 80` on `totals_X` — enables HOT updates, avoids
  index-update cost on every UPDATE. 3–5× write throughput
  improvement.
- Tuned autovacuum on `totals_X`:
  ```sql
  ALTER TABLE totals_X SET (
    autovacuum_vacuum_scale_factor = 0.02,
    autovacuum_analyze_scale_factor = 0.01
  );
  ```
- `pgbouncer` in transaction-mode — 10 K logical sessions →
  50–100 PG backends. PG will not handle thousands of direct
  connections without pooling.
- Optional: partition `mov_X` (and `totals_X`) `BY RANGE (period)`
  on registers that grow past ~50 M rows. Current-period queries
  see only the active partition; historical archive can DETACH +
  cold-store.
- Optional: streaming read replicas if reporting dashboards push
  the master beyond 70 % CPU. Standard PG ops, orthogonal to OES
  schema design.

---

## Implementation roadmap (when this gets picked up)

1. **PoC on `accumulationRegisterQuery::CreateAndUpdateTableDB`
   for the simplest register type** (one dimension, one resource,
   per-period sums — i.e. turnovers, no running balance). Generate
   `mov_X`, `totals_X`, three triggers, three views. Smoke-test on
   PG and FB.
2. **Read-path migration**: replace `RegisterSet::GetBalance`-style
   methods to read through `vw_balance_X` instead of computing
   from `mov_X` + existing totals.
3. **Generalise to other register kinds**: balance registers
   (running totals), accounting registers, information registers
   (slice-last). Same trigger pattern, different aggregate
   columns.
4. **Schema-change handling**: extend `updateMetaTable` to drop
   triggers / drop totals / rebuild. Handles every kind of column
   change uniformly.
5. **Per-driver SQL diallect**: generator outputs PSQL for FB,
   PL/pgSQL for PG, T-SQL for MSSQL, etc. Templated.
6. **Migration tool**: existing OES installations need a one-shot
   "rebuild totals from current mov_X, swap to trigger-maintained"
   pass. One SQL per register, runs at first Apply after upgrade.

---

## What remains (2026-07-29)

The maintenance is live and the switches work. What is NOT yet proven or built, in the order it
matters:

1. **Numeric parity — measured GREEN under a live engine 2026-08-02; unmeasured against REAL traffic.**

   Writing the check paid for itself immediately: `ibMaterializeSql::Apply` **could not install a
   bundle on a clean SQLite database at all**, for two independent reasons that only show up on a
   first apply, and neither of which the render-only tests could see.

   - **A driver reports failure by THROWING, not by a return code.**
     `ibDatabaseErrorReporter::ThrowDatabaseException` is what a failed statement calls, which
     makes the `return DATABASE_LAYER_QUERY_RESULT_ERROR` inside SQLite's `DoRunQuery` dead code.
     `Apply` tested that return code, so the exception from an unguarded DROP — the ordinary
     "nothing to drop yet" of a first apply, exactly what the guarded-drop loop exists to
     tolerate — sailed straight out of `Apply`.
   - **`DATABASE_LAYER_QUERY_RESULT_ERROR` is 0, and `RunStatement` returns the affected-row
     count** — which for DDL (`CREATE TRIGGER`, `CREATE VIEW`) is legitimately 0. So with the
     throw caught, every SUCCESSFUL create then read as a failure and `Apply` returned false
     having installed the bundle perfectly.

   Both are fixed: `Apply` now treats an exception as the failure signal and ignores the row
   count. Firebird masked this because its drops carry existence guards, so the throwing path
   was never taken there.
   `tests/test_totalsNumericParity.cpp` installs the REAL rendered bundle
   (`RenderMaterialization` → `Apply`) on an in-memory SQLite and compares the maintained
   totals with the movements re-aggregated directly — the same key-by-key, both-directions
   comparison `VerifyLastPeriod` makes, with the live aggregation as the oracle. It covers
   accumulation, month truncation, updates (quantity, side, and across both key columns),
   deletes, backdated entries, fractional quantities, and a mixed run. It also pins the
   storage behaviour that is NOT a disagreement: an emptied key leaves a **zero row**, not a
   missing one. What it does not touch is production traffic volume or Firebird's trigger
   family — the shapes differ per dialect, so a green SQLite run narrows the risk, it does
   not retire it.

1-bis. **The production CHECK exists; its verdict against real traffic is still unmeasured.**
   `ibDerivedState::VerifyLastPeriod` re-aggregates one elapsed period from the movements and compares
   it key by key with what the totals hold for it, counting disagreements in both directions (a key
   the movements know and the totals do not, and a figure nothing accounts for). Written as a routine
   rather than a one-off test, precisely because a full check costs exactly what a full rebuild costs
   — the same scan, the same grouping — so it can never be the cheap half of a decision. One period
   is a day's movements, which is affordable on a schedule.
   Still open: actually running it against real traffic, including updates, deletes and backdated
   entries. The live path remains in place and is the oracle.

1a. **The maintenance pass** — `MaintainTotals(snapshot, holder)` — is the single call a scheduled
   job makes: verify each derived table's last elapsed period, then fold its shards. No modes, no
   policy, no arguments beyond its context. It deliberately does NOT rebuild: regenerating a register
   is a full history scan and must not start by itself while people work, so it belongs to the
   Designer's *recompute totals* command (whose body is `RegenerateAll`). The pass therefore never
   fixes a disagreement — it REPORTS one, and that report is what sends an administrator to the
   Designer. Nothing is wired to a scheduler yet; the job dispatcher is a separate piece.

1b. **A derived table is now READABLE as an ordinary source.** It is declared by a metaobject but is
   not one, so nothing vended a queryable for it — and both L3-4 operations gate on exactly that,
   meaning regeneration had been returning success without touching anything since it was written.
   `ibSchemaTableQueryable` (bound via `ibSchemaTable::SelfSource`) closes it. Note what this implies
   about the parity question above: until now there was nothing to compare against, because both
   sides of the comparison were silent.
2. **Storage granularity is Day, and it is not a setting.** The periodicity of a READING is a query
   parameter — the caller asks for daily, weekly or monthly rows, or for none at all and gets the
   movements — so nothing about it belongs in the metadata. What Day fixes is the FLOOR under those
   readings, an internal storage decision: coarser is derivable, finer is served from the movement
   table anyway. The mechanism still READS the value rather than assuming it
   (`GetTotalsPeriodUnit()`), so a register that one day needs a different floor changes one place
   plus a regeneration — but it is not a knob to hand the user.
3. **The accounting register does not declare totals yet.** It was the reason for building the
   primitives this way — a debit and a credit side are two guarded accumulations into one table —
   and four `#if 0` blocks mark where its declaration goes.
4. **Recorder / line-number granularity** is served only by the MOVEMENTS table. That is the
   boundary the period enum draws deliberately, but nothing in the read path yet ROUTES a sub-day
   or per-recorder request to the movements automatically; it falls back wholesale.
5. **The Fill method** (boundary rows: opening figures for keys with no movement in the interval)
   is not implemented.
6. **The split has no automatic mode**, by decision — it is a property, because the choice depends
   on a specific key under a specific load rather than on a metaobject kind. See §6 for the shapes
   where it helps and the one that misleads (batch accounting: heavy in row count, light in
   contention).
7. **DROP + CREATE of one table name inside a single Firebird transaction** is the one path in the
   replace-don't-alter rule that has not been exercised end to end. If it turns out FB refuses, the
   release valve already exists — `m_ddlCommitBeforeData`, the barrier that already defers data
   writes past the DDL commit.

## The READ side, as the constructor found it (2026-08-10/11)

Everything above is about maintaining the totals. This section is about the other half — the virtual
tables a query names — and it is written from a run of the query constructor against them, which is
the first time anybody asked them the ordinary questions.

**A virtual table answers about its own columns from the VIEW's shape.** `FillSourceExplorer` on the
register's query descriptor reads `GetViewQueryable(...)` — the column set built from the register's
own dimensions and resources, metadata only. No companion is constructed and no database is touched,
so the answer costs nothing and works on a base that has never been opened. Deliberately NOT the
companion's `GetColumns()`: in RAM mode a companion navigates through the register itself and would
report the MOVEMENT columns, which is the one answer that would mislead.

**A dimension in a view IS the dimension.** The view's columns are plain (name, type, id) triples,
which is right for what is genuinely derived — `PeriodMonth`, `Resource1Turnover` have no metadata
behind them. It is wrong for a dimension: handed over as a synthetic triple it lost its picture and,
visibly, the fact that it holds a REFERENCE — so the same dimension could be unfolded on the register
one node up and not inside the view. The metaobject is handed over where there is one, keyed by the
id the view builder promised to keep.

**⭐ The periodicity decides which columns EXIST.** It is not a filter applied afterwards; it is the
grouping key of the fold, so it determines what the table has to show:

| periodicity | the table's period columns |
|---|---|
| *(nothing)* | none — the interval is read WHOLE: one row per key, begin to end, with no date on it |
| `Auto` | every projection the table can make (`Period`, `PeriodSecond` … `PeriodYear`) — nothing decided, the author picks |
| a unit (`Month`) | one column: `Period`, rolled to that unit. Months asked for, months given — the finer projections are not part of that reading |
| `Recorder` | the period and the document it came from |
| `Record` | the period, the document, and the line within it |

The distinction that had to be got right is **nothing** vs **Auto**: they are opposites. Nothing
asked for means no period at all, and showing a `Period` column over a reading that carries no date
is the window promising a value the rows will not have. An argument written as a PARAMETER counts as
undecided, like `Auto` — the shape a query is drawn against must be the widest one it might turn out
to have, never a guess at which.

**A register offers only the tables its type can fill.** A turnover-only accumulation register no
longer registers `.Balance` / `.BalanceAndTurnovers` at all — its view has no opening, closing or
expense column, so those were two tables in every catalogue answering with dimensions and not one
resource. The mirror on the information register: **no periodicity means no slices** — there is no
"as of" without a date to be as-of (`SyncSliceSources`, run from the metaobject's own run / reload).

**The parameters are declared by the SOURCE.** `DescribeParameters` gives the begin / end /
periodicity / [fill method] / condition set per virtual table, with the periodicity as a CLOSED set
of choices; the constructor's dialog renders that, rather than keeping a list of its own. An argument
written as a bare word (`…Turnovers(&From, &To, Month)`) is read as the word it is, not as a column
named `Month` that no table has.

**Still open here:** the periodicity shorthand on turnovers is accepted by the source and still
refused by the lowering; `Recorder` / `LineNumber` need the movements-level read path; and the
periodicity and fill-method word lists want to be REGISTERED enumerations, edited by quick choice,
rather than words a source declares.

## The demolition (2026-08-11) — one place for a complex query, and it is not here

The read side stopped computing. `ComputeBalance` / `ComputeTurnover` / `ComputeBalanceAndTurnover`
used to build the whole aggregate by hand in L2 IR — a signed `CASE` per resource, a `HAVING`, a
`GROUP BY` over physical field names, a `CAST` to pin the result type. Every line of it recomputed,
from the movements, what the trigger had already computed and stored.

**Why it was there at all** is the only thing that made it defensible: it was written when there was
no way to materialise totals and no query engine. It computed them the simplest way available at the
time. That time has passed.

### The rule

**A complex query is legitimate in exactly one place: the materialisation.** That is **L3-2**
(structure — the derived table and its triggers) and **L3-4** (regeneration). The algorithm lives in
the trigger, which maintains and regenerates. Everyone else **sees views** — and a view is an
ordinary named relation, indistinguishable from a table to the engine, joinable like one. So reading
a total needs nothing but the door.

### What each reading is now

| reading | what it does |
|---|---|
| **Turnovers** | `From(turnovers view)` → period range → condition → `GROUP BY` dimensions (+ the truncated period when a granularity was asked for) → `SUM` of the view's own figures |
| **Balance** | reads the **turnovers** view folded up to a moment and publishes the **balance** shape: `<Resource>Turnover` summed up to the date IS `<Resource>Balance` |
| **BalanceAndTurnovers** | two reads of the same view (before the interval, and inside it) plus the forward roll — the one thing still computed here, because a period opening where the previous closed is sequential by nature and cannot be a query |

The honest boundary is stated rather than worked around: **no view, no totals.** A driver that cannot
maintain derived state has no trigger, so it has nothing to read; the gate is `HasMaterializedViews()`.

### What died with it

The signed `CASE`s, the `HAVING` folded through `OR`, the grouping over `ibRegFieldsOf`, the physical
name spelling, and the numeric pin. That last one is worth naming: it existed to stop a hand-built
`CAST` from narrowing money — a bare `NUMERIC` is `NUMERIC(9,0)` on Firebird — and it is unnecessary
the moment nothing casts, because the view stores each figure in the resource's own declared type.

Five includes went with them, **L2-1 among them**: a reading that reads a view has nothing to build.

### Two names, one place each

A view column carries both spellings — `Resource1Turnover` for a query, `Resource1_Turnover` for the
table — and the pair is built once, in the schema. So the readings ask rather than spell: the logical
suffixes are constants (`ibRegFigure`), the physical name comes from the column
(`ibRegPhysicalOf`). The **period** is deliberately not in that list: its column is named after the
register's own period attribute, so the name belongs to the metadata. That distinction was found by
making the list — the readings had been writing `"Period"` as a literal.

### Argument order — what is always given, then what is sometimes, then what is almost never

`Balance(period, filter)`, `Turnovers(begin, end, filter, periodicity, fillMethod)` — the shape is
the same for the manager call and the virtual table, and the order is a frequency ranking rather than
a taxonomy: **the period is the question** (a balance without a moment is not a balance), the
**filter narrows it**, and the **refinements past that are the ones nobody writes** — a periodicity
or a fill method is asked for by a caller who wants a series instead of a number.

Named slots (`ibRegArg`) hold the positions and `static_assert`s pin the ordering, so a slot cannot
be renumbered in one register and not another. This is a user-visible contract: the position is what
a script writes.

### A filter is a structure that converts into a predicate

One converter (`ibRegFilterPredicate`, templated in `registerQueryLowering.h`) for the accumulation
register, the information register, and the accounting register when it comes. Five hand-written
structure walks became none. Past the converter there are no structures, only conditions.

The predicate has **two producers**: the query text, where parsing IS the conversion
(`WHERE Warehouse = &Warehouse`), and this converter. One form, two entrances. Carrying the structure
instead of converting it would have made a second currency only one entrance could spend.

### The information register, by the same rules

Its slice moved to `informationRegisterMetadataSlice.cpp` and its filter to the shared converter.
It gets **no view**, and that is a decision rather than an omission: a slice is *the record nearest a
moment*, so the answer depends on the moment asked for. A materialised surface can hold what does not
depend on the question; a sum does not, a slice does. The register's table is its source.

Its slice also stopped being parameterised by two strings (`"MAX"`/`"MIN"` and `"<="`/`">="`) that
could disagree, and whose parser defaulted silently on anything it did not recognise. One enum —
`ibSliceEnd::Last` / `First` — and both halves are derived from it.

## Below the grain: the movement arm (2026-08-11)

A maintained total is complete only down to the grain it is stored at. The trigger keeps the CURRENT
period's row up to date, so "no boundary at all" is never stale — but the row is a whole day, and a
question that stops inside that day has no column to read. Everything that happened between the
grain's start and the moment asked for lives in the MOVEMENTS and nowhere else.

That gap is what made two ordinary accounting questions unanswerable:

- **the balance as of a DOCUMENT.** Three documents can carry one date; a balance that cannot
  separate them answers about a moment nobody asked for.
- **the balance at 12:00**, which the daily totals could only answer to the nearest day — silently.

### One relation, two arms

The turnovers view now carries both halves: the stored rows `UNION ALL` the movements, each movement
contributing exactly what it contributed to the total. The contribution is not re-derived — it is
`spec.m_deltas`, the same expressions the trigger accumulates through and the regenerator rebuilds
from, so a movement counted in the tail and the same movement counted into the total are the same
arithmetic BY CONSTRUCTION rather than by two files agreeing.

The movement arm keeps the RAW instant as its period (truncating it would answer at the very grain
the arm exists to go below) and carries the recorder and line number, which are NULL on the stored
arm — that is also how a reader tells the arms apart, with no flag column invented for it.

### The cut, and why it is the one place this can lie

A reader takes **stored rows below the floor** and **movement rows from the floor onward**, where the
floor is the start of the grain holding the boundary (`ibTruncateToPeriod`, the RAM twin of the
truncation the trigger renders — a floor disagreeing with the stored key by one second would drop or
duplicate a whole grain). Each movement is then counted exactly once: by the trigger below the line,
by itself above it. Drop the floor and the current grain arrives twice, which reads as a balance
quietly too large.

An interval cuts at BOTH ends: the stored arm takes only WHOLE grains (from the first grain at or
after the lower boundary — `ibNextPeriodStart` — up to the floor of the upper one), and the movements
supply the two partial ends. Written as one range minus the middle rather than two ranges, because
two ranges overlap when the whole interval fits inside a single grain, and an overlap here is a
movement counted twice.

⚠ **Every reader must name an arm.** There is no safe default: a reading that says nothing gets the
current grain twice, and it is wrong in the direction that looks plausible. Interval readings take
the stored arm explicitly (`RestrictToStoredArm`); boundary readings take the cut (`FillArmCut`).

### When the arm sleeps

Ask at the grain or coarser and the movement arm is excluded outright by one predicate the planner
prunes — a turnover per day over last month costs exactly what it cost before. It wakes only when the
boundary reaches below the grain: a time inside it, or a moment naming a document.

### Inside one instant

Documents sharing an instant are separated by a tuple: `period < to OR (period = to AND recorder
<= to.recorder)`, the recorder's fields decomposed through the same write codec the rows were stored
with — so the comparison rides exactly the fields the index is built on, and in the same order
`ibValuePointInTime::CompareValueLS` uses. One order, so a balance computed on the server and one
computed in memory cannot disagree about which of two documents came first.

⚠ **The index this needs.** A subordinate register's key is `(Recorder, LineNumber)`, which answers
"the lines of this document" and nothing else. Every reading here asks the opposite question — the
movements of an INTERVAL — so the movements table now declares `(Period, Recorder)`, in that order
because it is the order a moment is compared in. Without it each sub-grain read is a full scan
bounded by the register's whole history rather than by the day asked for.

### The boundary

`Boundary(position, Including | Excluding)` wraps a date or a moment and says whether the row sitting
exactly there is in or out. Excluding moves the OPERATOR, not the value: nudging the instant by a
second instead would guess at the storage grain and would still include a document recorded in that
second — the very row the caller said to leave out. The side travels down to the tuple, so inside one
instant the named document itself falls out while everything before it in that instant stays.

## A reading refuses the column its granularity does not produce (2026-08-11)

The fold is a READ parameter, not a schema property, and it decides which columns the reading has -
the same rule the field tree offers by (`ibRegisterViewColumnFits`), now also asked of the reading
itself (`ibRegisterFoldOffersColumn`, accumulationRegister.h). The turnover and
balance-and-turnover companions override `ResolveColumnByName` and `GetColumns` with it.

Why the companion and not the view: a VIEW's vocabulary holds every projection the surface can make
- that is what a view is - so resolving through it accepted `Period` on a reading that folds the
interval whole, and the reading then produced no such column. Nothing raised. Refusing at the
companion, which is the thing that CARRIES the fold, makes the resolver say `unknown attribute`
naming the field, which is what an author who changed the periodicity and left the old fields behind
needs to be told.

The window-side half (fields dropped when the arguments narrow the table, and the name check
actually reaching this refusal over nested and temp sources) is in
[query-constructor.md](query-constructor.md) §5g.

## Open questions

- **Seed / predefined data on Apply.** Whatever idempotent seed path
  the schema-driven data layer settles on (`ContributeTables` is the
  current single source of DDL + data + seed — see
  `query-language-arc.md §23`), trigger-maintained registers fit it
  cleanly: initial population is `INSERT INTO totals_X SELECT ...
  GROUP BY ... FROM mov_X`, naturally idempotent. The two seed paths
  could unify around a single "regenerate derived state from
  source-of-truth" idiom. (The earlier draft cited a
  `ProcessPredefinedValue` probe — no such symbol exists in code;
  ignore that reference.)
- **Multi-level snapshots** (per-month checkpoints + live
  aggregation for current period) is a midpoint between current
  totals + live, useful for very large registers where even
  totals_X grows huge. Not in scope for v1; document as a future
  optimisation.
- **Cross-register transactions** that touch multiple registers in
  one document posting work as today (one TX, multiple
  triggers fire, all atomic). No new concurrency surprises beyond
  the existing explicit-totals approach already had.
