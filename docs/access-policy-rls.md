# Access Policy — debuggable Row-Level Security (RLS)

Runtime access control (row-level security) authored as a **debuggable script module** on a Role,
enforced at the L3 query door. The kill-feature departure from template-based RLS: instead of an
opaque SQL template woven into every query (undebuggable — you cannot breakpoint "why doesn't this
user see this row?"), the restriction is a real role module the existing per-session debugger steps
into.

---

## Layering — author on L4-3, enforce on the L3 door

| Where | What |
|---|---|
| **L4-3 (authoring)** | The Role's `OnAccessRead` / `OnAccessWrite` module — sibling of L4-1 (text query) and L4-2 (LINQ). You write the restriction here; the debugger breakpoints it. |
| **L3 (enforcement)** | `ibDataQueryBuilder` (the query door). Every read/write funnels through it, so RLS **cannot be bypassed** — it is not "RLS is applied", it is "RLS cannot be avoided". |

The restriction is not a source (L4-1/L4-2/L5 produce queries); it **decorates** any query — text,
LINQ, composer, dynamic list, hand-built — so no query escapes it. RLS is the **first** decorator;
multi-company / soft-delete / audit are other **policies** over the same decorator.

---

## Flow (one read)

```
query.Execute()                              dataQueryBuilder.cpp — the L3 door
  └─ m_policy->CheckSelect(guarded, Table)   copy-apply-execute (the guarded copy carries no policy
       └─ ibRuntimeAccessPolicy::Gate           -> no re-entrancy) — runs on EVERY query
            ├─ TablesAllowed                 1. the RIGHT on every table — BEFORE any filter
            │    └─ Unwind: From + each Join/Union leaf, stopping at the first refusal
            └─ ApplyToSource                 2. the row FILTER, per source, over the cached procUnits
                 └─ proc->CallAsFunc("OnAccessRead", result, decorator, Operation)
                                                ^ THE role module runs here — breakpoint this
```

**Two stages, one call per stage.** The policy exposes one method per operation — `CheckSelect` /
`CheckCreate` / `CheckUpdate` / `CheckDelete` — and each is asked twice, the `ibAccessStage`
argument saying which question:

| stage | when | question |
|---|---|---|
| `Table` | before the statement | may this role touch these tables at all? the right, then the folded filter |
| `Value` | after it ran, with `affected` | did the write happen? |

A refusal is a **false** return, never a silent empty result: the door raises
`ibBackendAccessException` on it, so a denied read and an empty one never look alike. The policy may
also throw itself when it can say more (it knows *which* source refused; the door does not — a join
or a subquery may be the one that closed the door, not the primary `From`).

**The right comes first, and a refusal stops everything.** `TablesAllowed` walks the sources and asks
each metaobject's own generic predicate — `AccessRight_Show` / `AccessRight_Modify` /
`AccessRight_Erase`, which already fold in `IsFullAccess` and the roles behind them. The first table
that refuses ends the walk and the row filter never runs. This is what the query language was missing:
`From Catalog.X` used to read a table the role has no Read right on, because that right was only ever
consulted when an *object* was opened.

- The door pulls its policy from the session (`session->GetAccessPolicy()`), the same seam it pulls
  the holder from — so a user-facing builder enforces by default; a trusted / internal read passes a
  null policy (`WithAccessPolicy(nullptr)`), which also dissolves re-entrancy.
- **The role module runs PRIVILEGED (`ibAccessTrustScope`).** A handler often reads the very source it
  restricts (e.g. to compute an allowed set before the `Restrict`) — that inner read must NOT re-enter
  RLS or it recurses forever. The policy wraps the `CallAsProc` in an `ibAccessTrustScope`, an RAII guard
  that sets a per-session TRUSTED flag; while set, `GetAccessPolicy()` returns null **even though the real
  policy exists**, so every query the handler's body builds bypasses enforcement. The scope saves/restores
  the prior value (nested trusted windows compose) and its dtor runs on every exit **including a handler
  throw**, so enforcement is always restored. The bypass is CONSTRUCTIVE — only the scope sets the flag,
  so it never masks a *forgotten* policy (an absent policy stays fail-closed). The flag is **per-session**,
  never process-global: a trusted window on one web session must not lift enforcement on another. Because a
  builder snapshots the policy in its ctor, a query *built* inside the window carries its null forward even
  if it executes after the window closes — the scope need only be active at build time.
- **Runs once per query, never per row.** The module builds the *rule*; the DB applies it *per row*.
  Runtime cost scales with the number of tables in the query, not rows.

---

## The query decorator — `ibValueQueryDecorator`

The policy hands the module a **base query decorator** (`system_to_clsid("VL_QDEC")`), NOT a classic
`ibValueQueryable`. It holds a **pointer to the query being executed** and implements **only `Join`
and `Where`** — a decorator narrows / shapes, nothing else. Both fold **straight into that query**:
one builder, no separate template, no copy-merge, no subquery wrap. The real source stays the query's
`From`, so the query still **pages** and **pushes down**. Constructed only by a policy — unreachable
elsewhere.

RLS is **not a decorator subtype**: it is a **policy** (`ibRuntimeAccessPolicy`) that *uses* the base
decorator. Multi-company / soft-delete / audit are other policies over the same decorator.

- `Where(predicate)` — folds a boolean predicate into the query. Reference **dot-walk** paths are
  supported (`x.Ref.Field = v`): the leaf carries the path and the read side auto-joins the reference.
- `Join(inner, leftKey, rightKey [, op])` — attaches `inner` (a full classic `Data.Registers.Y`, its own
  `.Where(user=me)` riding along, or a computed value table) as a **SEMI-JOIN**: the row passes iff a
  permitting row EXISTS in `inner`. It does NOT emit a projection JOIN (that would multiply); it folds a
  correlated `EXISTS` into the WHERE. Optional comparison op → the correlation op. See *Semi-join* below
  for the dispatch (single-source register → direct EXISTS; multi-source / value table → temp-promote).
- The module **side-effects** the query through these (the fold happens as it runs); it then signals a
  clean completion by setting its by-ref **`Allowed`** verdict to `True`. The door reads `Allowed` — a
  handler that folds a restriction and sets `Allowed = True` narrows the query; one that only sets
  `Allowed = True` (no fold) grants full access; one that never sets it (threw / swallowed / fell
  through) is a **fail-closed deny**. See *The handler*.

> **Why direct-fold, not a source swap.** Wrapping the source in a subquery (the earlier
> `ReplaceSource`) made it a RAM-computed source: it materialised the whole table, broke keyset paging
> (duplicate rows), and lost push-down. Folding into the real query keeps all three. Re-entrancy is
> handled by the guarded copy already carrying a null policy, so folding + running does not re-enter.

---

## Multi-role — OR

The user's role procUnits are resolved once (see *Hot path* below). One restricting role folds
straight into the query. Several roles **OR** their restrictions (a user sees a row allowed by **any**
of their roles):

- A **Where-only role** contributes its predicate; the predicates are OR-combined and added as one
  `query.Where(P1 OR P2 …)` — the real source is kept, so the query still pages.
- A **join-based role** cannot be OR-folded as SQL (the SQL IR has `IN (list)` but no `IN (subquery)`),
  so it is reduced by **materialising** the source keys it admits: run the join once, collect the
  admitted keys, fold `key = v OR …` (mirrors the query language's `key IN (subquery)` lowering). The
  materialisation runs during the Table stage of `CheckSelect`, before the main query executes — sequential, not
  nested.
- A role that **succeeds** (sets `Allowed = True`) with **no restriction**, or a role with **no handler**,
  grants full access → the whole OR is left unrestricted.
- A **failed** role (threw / swallowed / never set `Allowed`) contributes **nothing** — it does *not*
  widen the OR (a failed role must not read as "allow all"), and if **every** role fails the door denies
  outright. The `Allowed` verdict is what distinguishes "grant full access" from "failed" — without it a
  failed role in an OR would silently open everything.

---

## The handler

`OnAccessRead` / `OnAccessWrite` are **procedures** (the OES idiom — the verdict comes back through a
by-ref out-param, like `BeforeOpen`'s `Cancel`, not a Function return). Params: `Source` (the source as a
queryable), `Operation` (the op string — `"Read"` for a read, `"Create"` / `"Write"` / `"Delete"` for the
three write events, picked from the object's `IsNewObject()`), and **`Allowed`** (a by-ref verdict). The
handler **folds** its restriction into `Source` — a side effect (`Source.Where(…)` / `Source.Join(…)` /
the `restrict` keyword) — and then **sets `Allowed = True`** to grant access under that restriction. It can
branch on `Operation` (deny creates, say) and on `Source` (its full-name string) for a per-source rule.

**Fail-closed at the door** — the door owns the safe default, NOT the handler's error handling:
- Handler **absent** (a role with no such procedure) → this role imposes no restriction → **ALLOW**
  (migration-safe; existing configs keep working). Detected by lookup — `CallAsProc` returns *false*
  without throwing and without running anything — not by an exception.
- Handler present but does **not** set `Allowed = True` (it fell through, swallowed its own exception, or
  set `False`) OR it **throws** → the role **FAILED** → **DENY**. A mistake thus over-restricts
  (visible), never exposes. `Allowed` is a grant-flag (default `False`): more informative than a
  deny/cancel one — the positive `Allowed = True` says exactly what happened.

```c
Procedure OnAccessRead(Source, Operation, Allowed)
{
    // simplest — a Where on the source (dot-walk paths work: Source.Where(x => x.Contract.Company = …))
    Source = Source.Where(Function(x) { Return x.Code = "13"; });
    Allowed = True;
}
```

Join form via the concise `restrict` keyword (assign back to `Source` so the intent to narrow is
visible — a bare `restrict …;` statement folds too, but reads like an orphan):

```c
Procedure OnAccessRead(Source, Operation, Allowed)
{
    Source = restrict s in Source
                 join a in Data.Registers.WarehouseAccess.Where(Function(a) { Return a.User = CurrentUser(); })
                     on s.Warehouse = a.Warehouse;
    Allowed = True;
}
```

> **`=` is the comparison operator** in OES (not `==`). A source is identified by its canonical
> **full-name string** (`Source = "Catalog.Product"` / `"Document.Receipt"`), not
> `Data.Catalogs.X`.

### Concise form — the `restrict` keyword

`restrict <s> in <source> [ join <a> in <T> on <s.k> <op> <a.k> ]* [ where <cond> ]` compiles to the
**same** `Source.Join(…).Where(…)` push-down chain (`OPER_CALL_LINQ` → `ibValueQueryDecorator::
DispatchLinqMethod` → the DB) and returns the patched source. There is **no `select`** — a restriction
carries conditions only. It is entered on its own `restrict` keyword (a real keyword `KEY_RESTRICT`, by
analogy with `from`), so it is not RAM iteration — each clause folds straight into the query.

```c
Function OnAccessRead(Source, Operation)
{
    Return restrict s in Source
               join a in Data.Registers.WarehouseAccess.Where(Function(a) { Return a.User = CurrentUser(); })
                   on s.Warehouse = a.Warehouse
           where s.Code in (myCodes);
}
```

- **join** — one ON predicate `(s, a) => s.k <op> a.k` (the operator `= <> < <= > >=` comes from the
  shared expression grammar, not a hand-read); `ibValueQueryDecorator::Join` splits the `Compare` AST
  into the left key (vs source) + right key (vs inner) + op. The inner `<T>` is **any expression** — a
  `Data` source, a **computed value table** (the decorator temp-promotes it), or a source carrying its
  own `.Where(…)` (as above: pre-filter the joined table by the current user; the trailing `where` sees
  only `s` — see limitations).
- **where** — one predicate `s => cond`; every operator, `and` / `or` / `not`, dot-walk, and a captured
  **local** (→ a `Param` the decorator resolves at fold time, pushed into the query) come from the
  shared lambda machinery (`EmitRestrictBody` reuses `EmitFunctionBody` — no hand-rolled frame).
- **`in`** — `col in (v1, v2, …)` (a literal / expr list) or `col in <array>` (a captured array / value
  table / computed set, expanded into its elements at fold time → `Or(col = each)`). Added to the
  shared `ibBuildLambdaQueryAst`, so it also works in a plain LINQ `.Where(x => x.f in …)`.
- **Editor** — `restrict` is a real keyword, so the editor highlights it and lists it in keyword
  autocomplete (`s_listKeyWord`). The precompile walker (`ibPrecompileCode::CompileRestrictExpression`,
  the intellisense mirror of the backend `CompileRestrictExpression`) consumes the whole clause and
  registers the source / join aliases, so `s.` / `a.` autocomplete inside the `on` / `where` clauses and
  the walker does not bail on the keyword.

---

## Real-time, hot path

- **Real-time — the result is never cached.** The restriction is re-woven on every query: the module
  reads the current constants / settings each time, so a toggled setting sends it down a different
  branch → a different restriction (or none) immediately. Caching the *result* would freeze it; the
  future optimisation is **JIT-compiling** the hot module, not caching its output.
- **Role resolution IS cached (not the result).** The user's role → role module → procUnit resolution
  is done **once in the policy ctor** and held for the session's life (roles are fixed while the
  session lives) — re-resolving it per query would only cost. The policy is built in `CompileRoot`,
  **between module compile (`CreateMainModule`) and run (`AttachRuntime`)**: the module manager is
  live, yet no query has fired, so the policy is in place before anything it must guard.
  `DestroyRoot` / `ClearRoot` reset it, so a reload rebuilds it against the recompiled modules.

---

## Write path

Every builder write asks its own stage method, and the object-write layer picks the verb from the
object's own state — so `Operation` in the handler is one of `Read` / `Create` / `Write` / `Delete`,
and the handler can branch on it. Each is **ONE gated SQL statement**; `ExecuteWrite` returns the
affected-row count (lifted from the driver's `RunQuery`), and the door hands that count straight back
to the policy — it does not interpret it.

**What `0 affected` means is DECLARED, not guessed.** It depends on what the object says its rights
control (`ibAccessObject::IsAccessPerRecord`):

- **per RECORD** — the default, and what a catalog / document / constant is: the row exists as itself,
  so nothing written means the folded filter kept the statement off a row that is there → **refused**;
- **per TABLE** — a **register**, which overrides the default: its set is addressed by its recorder and
  may legitimately be empty, so the count carries no verdict at all and the write passes. This is the
  fix for the oldest confusion here — un-posting a document that never had movements used to report
  "not enough access rights" to a **full-access** user, because a `DELETE` touching nothing was read
  as a denial.

The verdict is also skipped outright when nothing was restricting in the first place (no role carries
an RLS module, or the user has full access): if nothing could have been kept from the write, an empty
result is just an empty result. DB errors THROW, so a negative count is a database failure, never an
access decision.

The object save (`ibValueRecordDataObjectRef::SaveData`) reads `IsNewObject()`:
`m_newObject ? Insert() : Update()`; DELETE is its own op (`DeleteData`). The three writes never merge.

- **Create → `Insert()` → `"Create"`.** Under a policy the new row is inserted IFF it satisfies the
  restriction, in one statement — a guarded **`INSERT … SELECT`** (`WITH CHECK`):
  `INSERT INTO t (cols) SELECT * FROM (SELECT <val> AS f … [FROM <dual>]) src WHERE <RLS over src>`.
  The derived one-row relation carries this row's own values (reusing the write value-spread, so the bytes
  match a plain INSERT), the RLS folds over its alias `src`, and **0 rows inserted → refused on a record-controlled object**. `<dual>` is the
  dialect's source-less-SELECT table (Firebird `RDB$DATABASE`; PG / SQLite / MySQL none —
  `ibDialectDictionary::m_selectFromDual`). With NO folded predicate it is a plain INSERT (creation
  unrestricted for that source).
- **Rewrite → `Update()` → `"Write"`.** ONE `UPDATE … SET <cols> WHERE <pk> AND <folded RLS>`
  (`ibQueryStatement::Kind::Update` + `SetWherePredicate`). The row is updated iff it passes the row filter;
  **0 rows → refused on a record-controlled object**. A plain UPSERT has no WHERE and would ignore the folded predicate — that is why an
  existing-row rewrite is an UPDATE, not an UPSERT.
- **Delete → `Delete()` → `"Delete"`.** `DELETE … WHERE <row-key> AND <folded RLS>`, built via `BuildWhere`
  (both the row-key conditions AND `m_predicate`). **0 rows → refused on a record-controlled object; on a register, no verdict.**

**Dot-walk on writes — correlated `EXISTS`.** A reference-path RLS condition (`s.Ref.Field = v`) cannot
lower to a JOIN in a write WHERE, so it rides as a correlated subquery:
`… AND EXISTS (SELECT * FROM <target> ex0 [JOIN … ex1 ON ex0.<ref1> = ex1.<selfref>] WHERE ex0.<selfref> =
<outer>.<Ref> AND <leaf>)`. Built by `ibMetaIRBuilder::BuildDotWalkExists` (a new `ibQueryExprKind::Exists`);
the `pathAsExists` flag on the write-path builders turns a dot-walk condition into EXISTS, while **reads**
still pre-resolve the path to a JOIN in `BuildPageIR` (`pathAsExists=false`). The correlation qualifier is
the write table (DELETE / UPDATE) or the derived-row alias `src` (create). Single-target reference hops; a
composite mid-path throws (as on reads).

**Physical ops stay RAW (not RLS-filtered).** Row locks (`LockByKeys`, the object version-read),
existence probes (register `ExistData`) and the tabular-section wipe run with the policy stripped
(`WithAccessPolicy(nullptr)`): they are concurrency / existence machinery, not user reads, so they must see
the raw rows — the RLS decision is made at the actual write. (A *guarded* existence probe would hide
excluded rows → skip the delete → the re-insert DUPLICATES them; a guarded lock would fail to lock them.)

**Tabular sections inherit the owner.** A tabular section is NOT an independent RLS subject: its writer is
de-policied (the owner's `OnAccessWrite` already gated the save), and it always `DeleteData`-wipes then
`Insert`s — never `Upsert`. (A tabular row has no primary key, so a native UPSERT emits an empty
`MATCHING ()` → SQL error, and post-wipe there is nothing to match anyway.)

---

## Semi-join — RLS joins as correlated EXISTS (the filter-not-JOIN principle)

**RLS is a FILTER, not a multiplying JOIN.** A filter selects a subset of rows; it must not change any
row's multiplicity. A `restrict s in Source join a in Permission on s.k = a.k` is SEMANTICALLY a
**semi-join** (⋉): it asks "does a permitting row EXIST?", not "give me the permission columns" and not
"multiply by the matches". Rendering it as a projection JOIN was the bug — a 1:N permission duplicated the
protected row, silently poisoning aggregates (a `SUM` under the policy doubles), invisible because RLS is a
decorator the query never sees. The fix: render every RLS restriction as a **correlated `EXISTS`** — a
FILTER that passes a row once/zero, never multiplies, idempotent to a dirty (duplicated) permission source.

### Read→EXISTS for a dot-walk `Where` (landed)

A `Where(x => x.Ref.Field = v)` RLS condition (a reference dot-walk) already lowered to a correlated EXISTS
on WRITES (a write cannot JOIN). Reads used to fold a real 1:1 JOIN — correct (a reference dot-walk is 1:1,
no multiply) but asymmetric. `ibQueryCondition::m_asExists` (a behavioural render tag next to `m_path`)
unifies it: the decorator's `Where` marks each lowered dot-walk leaf (`ibMarkRestrictExists`), and the
provider renders it as the same `EXISTS` on reads — `BuildConditionExpr` (`(pathAsExists || c.m_asExists) &&
!c.m_path.empty()`), `BuildFilterPredicate` (do not skip a flagged dot-walk), `BuildPageIR` (do not build a
join for a flagged leaf). One mechanism for read and write.

### Semi-join `EXISTS` for a `join` (landed — the RLS core)

`restrict s in Source join a in Permission on s.k = a.k where a.User = CurrentUser()` — a join to a
permission REGISTER — is the heart of programmable RLS: attach a permission source, it answers can/cannot.
It renders as `EXISTS(SELECT * FROM Permission a WHERE <a's own Where over a> AND a.k = s.k)`:

- **`ibSemiJoinExists`** (`queryable.h`) carries the correlation: the inner queryable, its own WHERE
  predicate (`m_where` — dot-walk and captured runtime Params ride along), the outer + inner key columns,
  the op. Built by the decorator's `Join` (`ibValueQueryable::GetWherePredicate` surfaces the inner Where).
- **A payload CONDITION folded into the WHERE PREDICATE TREE (`m_predicate`), NOT `m_conditions`** — the
  write UPDATE / CREATE paths render only `m_predicate`, so putting it there is what makes the semi-join
  ride EVERY WHERE path (read single / co-located / aggregate + write DELETE / UPDATE / CREATE). No missed
  site can leave a write or a join-query unrestricted. `AddSemiJoin` AND-folds it into the tree.
- **The provider renders it FIRST** in `BuildConditionExpr` (and `BuildColocatedPredicate` for a
  multi-source main query) → `BuildSemiJoinExists` → the correlated EXISTS. The inner's Where lowers over
  the `sj` alias via `BuildPredicateExpr(pathAsExists=true)`, so an inner dot-walk becomes a NESTED EXISTS.
  The outer correlation column is qualified by the source's table (an empty qualifier on the single-table
  write path would bind ambiguously inside the subquery — the same guard `BuildDotWalkExists` uses).
- **Multi-role OR is automatic.** The semi-join lives in `m_predicate`, so a role's `GetWherePredicate()`
  surfaces it and `ApplyToSource` OR-folds the roles' predicates: `EXISTS(role1) OR EXISTS(role2) …`. The
  role also reports `GetSources() == 1` (the semi-join inner is NOT in `m_root`), so the old
  key-**materialisation** (`scratch.Execute` → admitted keys → `key IN`) is BYPASSED — no separate
  pre-query. Everything server-side, in the one statement.

The correlation on a reference key compares the single `_RRRef` field (byte-identical). The ON is a single
column (`lcols.size() == 1`), so composite keys are a non-issue.

### Inner kinds — dispatch, and the Firebird temp-table wall

The decorator's `Join` DISPATCHES on the inner:

| Inner | Render |
|---|---|
| **Single-source register / catalog** (a real table) | Direct `EXISTS` over its table + its Where — ALL DBs, leak-safe read + write. |
| **Multi-source register** (`RegA ⋈ RegB`) OR a **generated value table / JSON set** | The inner is a COMPUTED subquery (`AsSource` → `ibSubqueryQueryable`) or a RAM value table (`ibTempTableQueryable`); the decorator MATERIALISES it (`ComputeRows` → `ibTempTableManager::Materialise` onto the session holder) and folds a **semi-join over the temp** into `m_predicate` — a server-side `EXISTS`, filter-not-multiply, on read **and** write, on **SQLite / PostgreSQL**. **Firebird has no DB temp tables** (`Materialise` → null) → INNER-JOIN fallback in `m_root` (read filters, but a row with N inner matches DUPLICATES; no write EXISTS) — the deferred pure-SQL `EXISTS` is the FB fix. |

A **multi-source-inner GATE** (`ibValueQueryable::IsSingleSource`) is the safety seam: the register-direct
fast path captures only the primary + its Where via `GetWherePredicate`, so a multi-source inner MUST take
the full-subquery path — dropping to register-direct would silently omit the inner's own joins and WEAKEN
the restriction (a leak). The multi-source / value-table inner now materialises to a temp and folds its
semi-join into `m_predicate` (not `m_root`), so on SQLite / PG it too FILTERS-not-multiplies and rides the
write path like the register-direct case; only the Firebird INNER-JOIN fallback stays in `m_root` (read-only,
and may duplicate). The materialise is decorator-eager — it runs when the policy folds the source (the inner
builder already carries the session holder, and `AsSource` nulls its policy, so `ComputeRows` neither lacks a
connection nor re-enters RLS).

**Future optimization:** a multi-source ALL-DB register inner needs no temp — it can render as a pure
SQL-subquery `EXISTS(SELECT * FROM RegA JOIN RegB WHERE … AND correlation)`, which works on Firebird too
(and enforces on writes). The temp-promote path covers it on SQLite / PG today; the pure-SQL render is the
Firebird-and-write path for it.

### Verify (live — a leak is silent)

Confirmed only by running under a role: a register-restricted list filters by existence, no duplicates; the
same restriction denies a WRITE (create / update / delete) of an unpermitted row; multi-role sees the OR;
fail-closed (empty EXISTS → 0 rows → deny). SQLite (the test DB) HAS temp tables, so the gtest suite covers
the register AND the temp-promoted value-table / multi-source cases; Firebird is the RAM-fallback path.

---

## Current state & limitations

Read-restriction **confirmed working live** — both the explicit `Source.Where(…)` form and the
concise `restrict … where` keyword form filter the query (single role). **Fail-closed contract landed**
(see *The handler*): handlers are procedures with a by-ref `Allowed` verdict (default deny),
presence-gated (absent → allow), and a handler that throws / swallows / never grants → deny; multi-role
OR is fail-closed (a failed role never widens; all failing → deny). **Write-restriction landed for CREATE,
WRITE (rewrite) and DELETE** — each is ONE gated statement keyed off the object's `IsNewObject()`: CREATE =
guarded `INSERT … SELECT` (`WITH CHECK`), WRITE = guarded `UPDATE … WHERE pk AND rls`, DELETE = guarded
`DELETE … WHERE … AND rls`; all deny on `0 affected` (see *Write path*). **Dot-walk on writes landed** — a
reference-path condition lowers to a correlated `EXISTS`. **Confirmed live on Firebird 5** (create /
rewrite / delete under a `code = "…"` restriction, including documents with tabular sections; PostgreSQL is
source-compatible). Live verification of the dot-walk write path and multi-role still pending.

**Read→EXISTS + `join` semi-join implemented** (pending build + live verify; see *Semi-join* above). The
dot-walk `Where` now lowers as the SAME correlated `EXISTS` on reads (`m_asExists`), and a `restrict … join`
is a semi-join (`ibSemiJoinExists` folded into `m_predicate` → `BuildSemiJoinExists`) that FILTERS without
multiplying and enforces on read + write, ALL DBs, for a single-source register inner. Multi-role OR is
automatic (the semi-join rides `GetWherePredicate`, materialisation bypassed). A multi-source / value-table
inner MATERIALISES to a temp and semi-joins over it (server-side, read + write) on SQLite / PG; Firebird (no
temp tables) falls back to an INNER JOIN (read-only, may duplicate) — the deferred pure-SQL EXISTS is its fix.

- **Multi-role OR — handler-less role is NEUTRAL (fixed in code, pending live verify).** A role with NO
  handler for the operation used to `return` (grant full access), so in the OR a single handler-less role
  assigned to the user OPENED the whole thing and discarded a restricting role's filter. Now a handler-less
  role does NOT participate in the OR: it neither widens (no silent "grant all") nor denies. Only an
  EXPLICIT full-grant (a handler that runs, succeeds, folds no restriction) opens the OR. An `anyParticipated`
  flag keeps the terminal honest: **no** participating role → migration-safe ALLOW (matches the single-role
  NoHandler path); participating roles that **all** fail → fail-closed DENY. See `ApplyToSource` in
  `session/session.cpp`.
- **One handler, every source.** `OnAccessRead` / `OnAccessWrite` run for EVERY source (catalog, document,
  register, …); a hard-coded `Restrict s.code = …` THROWS on a source without `code` → deny. Branch by
  source — `Source` compares to its full name with `=`: `If Source = "Catalog.X" Then Restrict … EndIf;` —
  and grant the rest (`Allowed = True`).
- **Dot-walk write is single-target** — a composite reference mid-path (e.g. a register recorder) throws,
  as on reads. A dot-walk `IS NULL` on a write still lowers flat.
- **AOT bytecode cache must be invalidated after a compiler change.** The role module's compiled
  bytecode is cached in `sys_bytecode_cache` (a DB table; `Load` keys on `descriptor_id` and the AOT
  `kAOTFormatVersion`, NOT the compiler version or the stored `bytecode_version`). A `restrict` module
  compiled before the pushdown-AST recording was correct is served AST-less → `Where`/`Join` throws
  "cannot be lowered", which a module `try/except` swallows → the query runs unrestricted. Fix:
  `kAOTFormatVersion` was bumped (16 → 17) so stale blobs are rejected and recompiled. A future
  compiler change that alters bytecode output for unchanged source needs the same bump (or an
  `ibByteCodeCache::InvalidateAll`). The recorded AST *is* AOT-serialised, so once recompiled it
  survives caching. (The constant has moved on since — 18 as of the shortLet-peephole fix; always read
  `byteCodeAOT.cpp:136` rather than a number written here.)
- **Every source in the query is restricted (fixed in code, pending live verify).** `Apply` walks
  `GetSources()` (descending Join / Union) and fires the role handler ONCE per distinct table (dedup by
  `GetQueryTableId()`), not just the primary `From` — so a joined table is gated too, and the handler
  branches on `Source`'s full name. A single-source query has only the primary, so the old behaviour is a
  strict subset. Caveat: the restriction still folds into the query's top-level WHERE, correct for the
  primary and for INNER joins / semi-join (`restrict … join` → `ibSemiJoinExists` = cardinality-safe
  correlated EXISTS on any leaf); a bare column `Source.Where(x => x.col = v)` on a LEFT-joined table
  collapses the outer join to inner — folding into the join's own ON is the deeper leaf-scoped step. See
  `Apply` / `ApplyToSource` in `session/session.cpp`.
- **Multi-hop `ON` inside a `Join`** — the decorator `Join` takes single-column keys; a multi-hop key
  is rejected with a message pointing to dot-walk `Where` (`x.Ref.Field = v`), which already handles
  multi-hop conditions. A true multi-hop `IN (subquery)` predicate would let join-roles also OR without
  materialisation.
- **`restrict where` sees only the source alias** — its predicate lowers against the source's columns,
  so `where a.Field = …` (a *join* alias) does not resolve (the ON is one `s.k <op> a.k` equality, not a
  place for arbitrary conditions either). Filter a joined table by pre-`.Where(…)`ing its `<T>` source
  (`join a in T.Where(a => …)`), whose filter rides into the join subquery. Lowering a `where` against
  the joined column set is a separate change.
- **`restrict … in <array>` expands to `Or(col = each)`** (same as the text query's `IN (list)`); a
  large captured set should materialise as a temp table (`array → temp`, an IN-subquery join) — a
  separate lowering feature. For big sets, `join a in <valueTable> on …` already does this.
- **Over-length string compare** — comparing a value longer than a `CHAR(n)` column raises a Firebird
  `-303` rather than "no match" (blocked: the column length is not exposed at lowering, and clamping
  the bind is forbidden — it broke value-table save).

## Key files

| File | Role |
|---|---|
| `metaCollection/metaRoleObject.{h,cpp}` + `metaRoleObjectMenu.cpp` | Role is module-bearing; `OnAccessRead`/`OnAccessWrite` default **procedures** (args `Source`, `Operation`, by-ref `Allowed`); "Open role module" tree menu |
| `query/dataQueryBuilder.{h,cpp}` | `ibAccessPolicy` interface — ONE method per operation (`CheckSelect` / `CheckCreate` / `CheckUpdate` / `CheckDelete`), each asked twice with an `ibAccessStage` (`Table` before the statement, `Value` after it with the affected-row count); policy injection; copy-apply-execute; a false verdict raises `ibBackendAccessException` **here**, naming the operation only — which SOURCE refused is the policy's to say, since a join or a subquery may be the one that closed the door; `WithAccessPolicy(nullptr)` (physical ops / tabular); `AdoptOwnedSource` / `GetSources` / `GetWherePredicate`; `AddSemiJoin` folds an `ibSemiJoinExists` into `m_predicate` (so it rides read + write) |
| `session/session.{h,cpp}` | `GetAccessPolicy` (returns null inside a trusted window); `ibAccessTrustScope` — RAII per-session TRUSTED flag wrapping the handler `CallAsProc`, so the module runs privileged (its own reads bypass RLS, dissolving re-entrancy); concrete `ibRuntimeAccessPolicy` — `Gate` (the table right, then the row filter) and `Verdict` (what `0 affected` means) behind the four stage methods, both walking the ONE `Unwind` (From + every Join / Union leaf, deduped by table, stopping at the first refusal); **fail-closed** contract (`RoleOutcome` NoHandler/Failed/Succeeded via `CallAsProc` + by-ref `Allowed`, exception→deny, multi-role OR where a failed role never widens); ctor-cached role procUnits; policy built in `CompileRoot` |
| `roleHelper.h` | The rights themselves. `ibAccessObject::AccessRight(role)` falls back to the right's OWN default (`ibRole::GetDefValue`) instead of a hard-wired "allowed", and re-reads it per role, so one role's denial no longer sticks to the next. `IsAccessPerRecord` — do this object's rights control a record or the whole table (record by default; a register overrides) |
| `metaCollection/genericData.h` | `AccessRight_Show` / `_Modify` / `_Erase` — the generic rights the policy asks. Each metaobject maps them onto its own Read / Write / Delete role with `IsFullAccess` folded in; `_Erase` is the newest of the row and completes it |
| `query/queryable.h` | `ibSemiJoinExists` (the semi-join spec: inner + `m_where` + correlation keys + op); `ibQueryCondition::m_asExists` (dot-walk render tag) + `m_semiJoin` (the semi-join payload) |
| `system/value/valueQueryable.{h,cpp}` | `ibValueQueryDecorator` — `Where` marks dot-walk leaves for EXISTS (`ibMarkRestrictExists`); `Join` DISPATCHES: single-source register → `AddSemiJoin`, multi-source / value table → temp-promote path. `ibValueQueryable::GetWherePredicate` (inner's Where) / `IsSingleSource` (the gate) |
| `query/dbTableProvider.cpp` | `BuildSemiJoinExists` — the correlated `EXISTS(SELECT * FROM inner sj WHERE <inner Where over sj> AND correlation)`; the semi-join / `m_asExists` branches in `BuildConditionExpr` + `BuildColocatedPredicate` so it rides every WHERE path |
| `compiler/codeDef.h` + `compiler/translateCode.cpp` | `KEY_RESTRICT` / `"Restrict"` keyword (lock-step) |
| `compiler/compileCode.{h,cpp}` | `CompileRestrictExpression` (the `restrict` block) + `EmitRestrictBody` (a clause → a 1-or-2-param lambda via `EmitFunctionBody`); triggered on `KEY_RESTRICT` in `GetExpression` and the statement switch |
| `compiler/lambdaQueryAst.{h,cpp}` | body → L4 pushdown AST — `ibBuildLambdaQueryAst` (LINQ, single alias) / `ibBuildRestrictedQueryAst` (`restrict`, bare span, 2-alias join ON); `in (list)` / `in <array>` parsing shared by both |
| `query/queryLowering.cpp` | `In` lowering — expands a captured array / collection item into its elements |
| `compiler/byteCodeAOT.cpp` + `compiler/cache/byteCodeCache.{h,cpp}` | AOT (de)serialisation of the pushdown AST (`m_lambdaExprAst`) + the `sys_bytecode_cache` DB DAO; `kAOTFormatVersion` bump invalidates stale AST-less blobs |
| `compiler/procUnit.h` | `CallAsProc` / `CallAsFunc` comma-args variadics return **bool** (found?) — the presence gate the door uses without a separate lookup |
| `query/queryProvider.{h,cpp}` + `query/dbTableProvider.{h,cpp}` | `ExecuteWrite` returns the **affected-row count**; per verb — CREATE = guarded `INSERT … SELECT` over a one-row derived relation (`WITH CHECK`), WRITE = guarded `UPDATE` (`Kind::Update` + `SetWherePredicate`), DELETE = `BuildWhere`; `BuildDotWalkExists` + the `pathAsExists` flag lower a reference-path write condition to a correlated `EXISTS` |
| `databaseLayer/databaseQueryBuilder.{h,cpp}` | `ibQueryStatement::Kind::Update` (+ `SetWherePredicate`) — SET non-key, WHERE key AND the RLS predicate; `ibInsertSelect` + source-less SELECT (`m_selectFromDual`, FB `RDB$DATABASE`) for the create `WITH CHECK`; `ibQueryExprKind::Exists` / `ibExists` — the correlated-subquery expr for dot-walk writes |
| `frontend/.../codeEditor/codeEditorInterpreter.{h,cpp}` | `ibPrecompileCode::CompileRestrictExpression` — the intellisense-side walker (aliases → autocomplete, no bail on `restrict`) |
