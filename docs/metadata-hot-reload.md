# Metadata hot reload — change classes and the tombstone door

> **Status:** **PROPOSAL — design discussed 2026-07-15, NOTHING implemented in code.**
> Verified against the tree the same day: no schema-version marker in any `sys_*` table, no
> tombstone gate on the L3 provider, no policy composite. What exists today is the
> evict-and-reload path described in §1 — treat every "OES does X" below as "OES would do X
> under this proposal."
> Companions: [access-policy-rls.md](access-policy-rls.md) (the door and its decorator),
> [register-totals-strategy.md](register-totals-strategy.md) (§5 depends on it),
> [const-meta-refactor.md](const-meta-refactor.md) (the precondition),
> [schema-first-metadata.md](schema-first-metadata.md), [temp-db.md](temp-db.md)
> (the fallback pattern this reuses), [web/open-issues.md](web/open-issues.md).

---

## 1. What happens today

There is no hot reload. `POST /admin/sessions/<guid>/reload` flips `m_reloadRequested`; the
watcher evicts every session, then runs `CloseDatabase(forceCloseFlag)` + `LoadDatabase()` +
`RunDatabase()`, and the next login gets a fresh compile. That is a **restart with a
euphemism** — correct, but unconditional.

It is unconditional because the mechanism is single: one reload path serves every kind of
change, so it must be tuned for the most hostile one. The rest of this document is about
*not* being single.

**On the desktop the problem is already solved and cost zero lines.** A thick client is a
process per user: each `enterprise.exe` loads metadata into its own `m_activeMetaData` at
startup and holds it until exit — own snapshot, own bytecode cache, own version. That is
exactly the RCU model (old readers keep the old tree; new readers get the new one),
implemented by the OS process manager rather than by us.

**The web takes that service away.** One `wes`, one `m_activeMetaData` behind `s_instance`,
N cookie sessions — no process boundary between users, so everyone must be on one version,
so the only available answer is eviction. This is the real cost of the web tier, and it is
not the controls: the controls are volume. The cost is the isolation the OS used to give
for free.

---

## 2. Four classes of change — only one needs eviction

Classifying by DDL alone is wrong: **DDL-additive is not semantically additive** (§5).

| Class | Example | Correct answer |
|---|---|---|
| **Additive** | new attribute, new form, changed body of an ordinary module | do nothing — the old version never looks at it |
| **Destructive** | attribute removed, type narrowed | tombstone (§4): gate the column, return typed empty, window until drain |
| **Derived-invalidating** | new register dimension | totals as cache (§5): fall back to aggregate over movements, rebuild in background |
| **Retroactive** | a check added to `BeforeStart` / `OnStart` | **re-login — and only here is it honest** |

**Retroactive is impossible, not hard.** `StartMainModule` fires once per session at login
(web: `ibWebApplication::OnInit` under `m_runtimeMutex`). A user already inside has had that
event; a new snapshot brings a new `BeforeStart` body with nobody left to call it. Snapshots,
RCU and expand/contract all synchronise *state* — this asks to replay the *past*. Today's
eviction is the right answer for this class; the defect is that it is applied to the other
three as well.

**Retroactivity is a property of the hook point, not of the change.** Compare: a Role's
`OnAccessRead` runs **per query**, so changing it reaches live sessions on their very next
read — free, no eviction, no snapshot. `OnStart` runs **once**, so it never reaches them.
Same reload mechanism, opposite outcome. Hence the design rule:

> A check that must affect users already working cannot be a startup check. It belongs on the
> L3 door (next to RLS) or on an operation boundary.

**Conservative default = today's behaviour.** Anything unclassified falls to *retroactive* →
evict → exactly what happens now. So the classifier can land one rule at a time without
regression: start with "attribute `OnlyInRight` → touch nobody", leave the rest evicting.
Each subsequent rule removes one class of downtime.

---

## 3. What already stands

Little of this needs building; most of it needs connecting.

| Piece | Where | Why it matters |
|---|---|---|
| `const`-meta | landed 2026-05-04, 133 files — "runtime never holds non-const `ibValueMetaObject*`" | **the precondition.** A snapshot is only possible over an immutable tree |
| Bytecode cache placement | `ibMetaDataConfiguration` holds the compile cache per module descriptor; **runtime instances live in sessions** | a metadata snapshot already carries its own bytecode — "sync the code" is not a separate mechanism |
| Diff walker | `metaCollection/metaDiff.{h,cpp}` — `ibMetaDiffWalker`, GUID pairing, `{Same, Reordered, Changed, OnlyInLeft, OnlyInRight}` | the classifier's input |
| Deleted-column notion | `ibBackendSourceColumn::IsAllowed()` → attribute routes it to `IsEnabled() && !IsDeleted()` | **the tombstone already half exists** |
| Schema snapshot | `query/schemaSnapshot.{h,cpp}` — `SnapshotOf`, `DiffSnapshots(baseline, target)` → DDL | knows declared-vs-actual already |
| Signal channel | `sys_session` `signal` column, admin kick/reload dispatcher, `m_reloadRequested` | the notification path exists — it only needs a new meaning |
| The single door | `ibDataQueryBuilder` — every read and write funnels through it | one interception point, provably unavoidable (RLS stands on this) |

**The one missing piece: a schema-version marker.** Nothing in `sys_config` / `sys_db`
records "which schema version is live" in a form an old session can check cheaply. That is
the whole of the new work. It must be cheap: one number or hash, compared at an operation
boundary — match means zero overhead; mismatch means snapshot once, mark the vanished
columns, carry on.

**Also required before any of this:** close `GetAnyArrayObject<T>()` and the 13 remaining
`const_cast`s. A snapshot over incomplete immutability is a race that reproduces once a week
at a customer site and is never caught. This is the real cost of the arc — narrow, addressed,
and prior to everything else.

---

## 4. The tombstone

Old session, old metadata (attribute still live in its snapshot), new schema (column already
dropped). The query is built from the old description, hits the DB, and dies on
"column not found".

**The fix:** the door does not go to the DB for a column its version no longer has — it
returns a **typed empty** derived from `GetTypeDesc()`. This is schema resolution, the same
contract Avro and Protobuf give a reader whose schema predates a field removal.

- **Read → typed empty.** Nothing crashes; the old form shows a blank.
- **Write → silently skip the column.** *Not* an exception. The value the user typed into a
  removed requisite is already condemned by the removal; the field is on screen only because
  the form is old. Throwing would block all work to protect data that must not exist.
- **The gate is `IsAllowed`, reaching further than today.** Currently only the source explorer
  consults it ("a deleted or access-denied field is not [usable]"), so a removed attribute is
  hidden from the picker — but code addressing it *by name* still reaches the DB. The work is
  to extend that same gate down to the provider.
- **Log every tombstone hit.** Otherwise the degradation is silent. With a log the operator
  sees "old session read a dropped column 40× in a minute" and knows the drain is overdue.
- **Window, not mode.** Degradation lives from apply until the last old session drains. Keep
  it short.

### 4.1 The tombstone is NOT a policy

Tempting, since [access-policy-rls.md](access-policy-rls.md) already advertises the slot —
"RLS is the first decorator; multi-company / soft-delete / audit are other policies over the
same decorator". **It does not belong there.** `ibAccessTrustScope` blanks
`GetAccessPolicy()` wholesale, so a privileged role module would bypass the tombstone too,
address the dropped column directly and crash — in the code that must always work.

The reason is deeper than the bug: **a policy is a rule that can be lifted; a tombstone is
physical reality that cannot.** The column is gone. No privilege resurrects it. RLS lives in
the policy layer because it is bypassable *by design*; the tombstone lives below it, on the
column (`IsAllowed`) and in the provider, because it is unconditional.

This also settles the name: L4 tiers are **authoring** tiers (L4-1 query text, L4-2 LINQ,
L4-3 RLS — each has a human writing something). A tombstone has no author; it is derived
from a snapshot diff. It is a property of the door, not a tier of the language.

### 4.2 Where tombstone meets RLS

If a role's rule references a column that has been tombstoned, the rule cannot be applied.
**Fail closed** — per the existing principle ("an absent policy stays fail-closed"). Never
drop the condition and proceed: a silently weakened access rule is a data leak, and this is
the one place where "on the user's responsibility" does not transfer.

### 4.3 When policies do become plural

Not an array on the door — a **composite on the session**, implementing the same
`ibAccessPolicy` interface (two pure virtuals). The door keeps its single `m_policy`;
`WithAccessPolicy(nullptr)` and the trusted scope keep working unchanged.

Policies **do not commute**: if one narrows what another relies on, the result depends on
application order, and the difference between "the user sees nothing extra" and "the user
sees it" lives there. Fix the order explicitly in the composite and say why in a comment —
never leave it to vector insertion order.

---

## 5. Derived-invalidating changes, and why totals are the crux

Adding a register dimension is `ADD COLUMN` — DDL-additive — yet it kills every total,
because the aggregation key changed. This is the class a DDL-shaped classifier misses.

**Today it costs nothing, because there are no totals**: registers persist movement lines
only; `Balance` / `Turnovers` are computed live as L2 aggregate IR. There is nothing to
recompute because there is nothing to drift. The problem that forces comparable platforms
into exclusive mode is not solved here — it is **absent**, because the mechanism that creates
it was never built.

**Building totals buys that problem — unless the status is right.** The difference is not
whether totals exist, but whether they are **truth or cache**:

- Total as **truth** (the classic pattern): readers trust the totals table, movements are
  archive. An incomplete total is therefore never acceptable — it shows a wrong balance.
  Hence: stop the world, rebuild, let users back in. Exclusive mode follows *from the status*,
  not from bad engineering.
- Total as **cache over movements-as-truth**: an incomplete total is acceptable. A reader
  hitting an invalid segment aggregates it from movements — slower, still correct.
  Background fills. Nobody is evicted.

The escape is already in the strategy doc's own TL;DR: **"read path goes through views,
runtime is totals-agnostic."** A view is a level of indirection, and indirection is where
substitution happens:

1. Dimension added → `CREATE OR REPLACE VIEW balance_X AS SELECT … FROM mov_X GROUP BY …` —
   everyone reads slowly but **correctly**.
2. Background builds the new `totals_X` under the new key; the trigger keeps it in sync for
   new movements as it goes.
3. Done → point the view back at `totals_X`. Fast again.

Nobody was evicted; the whole protocol lives inside the DBMS. **Triggers cure drift, not
migration** — they maintain, they do not rebuild; the one-shot rebuild is what the view swap
covers.

> **The invariant that must not be lost:** the runtime reads the *view*, never `totals_X`.
> The moment someone optimises away "the pointless extra layer" and reads the totals table
> directly, the entire exclusive-mode protocol is bought back for a few percent on reads.
> That line in the TL;DR is not stylistic — it carries the migration story.

Per-driver: totals are a feature of **volume**, and volume means PostgreSQL, where
`CREATE OR REPLACE VIEW` is atomic and transactional. On SQLite / FB-embedded, live
aggregation *is* the right answer. So "per-driver trigger templates" means one real template
plus graceful degradation to nothing — the `GetTempTableDialect()` shape, where **presence of
the capability is its declaration** and absence is a silent fallback to the honest slow path.

---

## 6. Order of work

Steps 1–3 deliver most of the value and need neither snapshots nor A/B.

1. Close `GetAnyArrayObject<T>()` + the 13 `const_cast`s (§3) — **before** anything else.
2. Per-property granularity in `metaDiff` — **LANDED** (verified 2026-07-29). `WalkPair` emits a
   synthetic Properties group with one status-bearing row per property
   (`metaDiff.cpp:259-300`; carrier fields `m_propertyName` / `m_leftValue` / `m_rightValue` +
   `IsProperty()` at `metaDiff.h:98-103`). What remains is *classifying* those deltas — a renamed
   synonym (harmless) versus a narrowed type (catastrophic) are now both visible, but not yet
   told apart. That classification is step 3.
3. Classifier: diff → `{additive, destructive, derived-invalidating, retroactive}`; additive
   applies the DDL, publishes, touches nobody. Everything unclassified evicts, as now.
4. Schema-version marker in a `sys_*` table + check at an operation boundary; re-purpose the
   `signal` from "leave" to "**re-verify**" — snapshot, diff, mark tombstones, carry on.
5. Extend the `IsAllowed` gate from the explorer down to the provider; typed empty on read,
   skip on write, log every hit.
6. Only then: refcounted snapshots (`shared_ptr<const>` published RCU-style) and deferred
   contract for the destructive tail.

**A snapshot is not a copy.** N sessions on one version = **one instance and N pointers**; a
second instance exists only while two versions are genuinely live, and dies with the last old
reader. The "a copy per session is too expensive" objection applies to copying, not to this.

---

## 7. Why this is worth writing down but not worth building yet

While the client is thick, the process manager donates the whole mechanism (§1) and eviction
is an acceptable deploy protocol — the industry lives this way, including the platforms this
one is compared against. The arc pays for itself only where downtime costs money: a cloud
with tenants, or a warehouse where fifty handhelds are mid-shift.

What *is* due now is smaller: stop calling the restart a hot reload. A name that contradicts
its behaviour has been fixed here before — `ibMetaQueryBuilder` became `ibDataQueryBuilder`
once it turned out the door was not metadata-bound.

**The common denominator of all three solvable classes: never let a derived thing become
truth.** Old-session metadata is derived from the schema — hence the tombstone works. A total
is derived from movements — hence the cache works. Platforms that evict users do so because
their model declares the derived thing to be truth, in two places at once. This tree has not
done that yet. The work is to keep not doing it.
