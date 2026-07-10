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
query.Execute()                         dataQueryBuilder.cpp — the L3 door
  └─ m_policy->ApplyReadAccess(guarded)  copy-apply-execute (the guarded copy carries no policy ->
       └─ ibRuntimeAccessPolicy::Apply      no re-entrancy) — runs on EVERY query
            └─ ApplyToSource                per source, over the cached role procUnits
                 └─ proc->CallAsFunc("OnAccessRead", result, decorator, Operation)
                                            ^ THE role module runs here — breakpoint this
```

- The door pulls its policy from the session (`session->GetAccessPolicy()`), the same seam it pulls
  the holder from — so a user-facing builder enforces by default; a trusted / internal read passes a
  null policy (`WithAccessPolicy(nullptr)`), which also dissolves re-entrancy.
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
- `Join(inner, leftKey, rightKey [, op])` — folds a join to `inner` (a full classic
  `Data.Catalogs.X` / `Data.Registers.Y`, its own `.Where(user=me)` riding along, or a computed value
  table temp-promoted) into the query. Optional comparison op → theta ON.
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
  materialisation runs during `ApplyReadAccess`, before the main query executes — sequential, not
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
queryable), `Operation` (a per-type string — `"Read"` / `"Post"` / `"Delete"` / …), and **`Allowed`** (a
by-ref verdict). The handler **folds** its restriction into `Source` — a side effect (`Source.Where(…)`
/ `Source.Join(…)` / the `restrict` keyword) — and then **sets `Allowed = True`** to grant access under
that restriction.

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

`Insert` / `Upsert` / `Delete` on the builder call `ApplyWriteAccess(guarded, "Write" / "Delete")` →
the same decorator mechanism with the `OnAccessWrite` handler. Object writes route through those
builder writes (`commonObjectRefQuery`, tabular section, record set, constant), so the handler runs on
every write.

**DELETE is enforced by row count.** `ExecuteWrite` returns the number of rows affected (lifted from
the driver's `RunQuery`). A restricted `DELETE` folds the RLS `Where` into its conditions, so a delete
of a row the user may not touch matches **0 rows** — and under an **active policy** `0 affected` means
"no accessible row" → `ibBackendAccessException` (fail-closed, loud — mirrors the read door). No extra
existence query. (With no policy, `0 affected` is a normal no-op — no throw.)

**Save (INSERT / UPSERT) is NOT yet restricted** — a known gap. The upsert SQL is built from a dialect
template (`{table}{columns}{values}{keys}{update}`) that has **no `WHERE`**; Firebird's
`UPDATE OR INSERT … MATCHING` cannot carry a condition on its update. So the folded `Where` is dropped
for INSERT/UPSERT and the write lands unrestricted (always 1 row → the count gate can't fire). Two ways
to close it: **(a)** rewrite the upsert to `MERGE … WHEN MATCHED AND <rls> THEN UPDATE WHEN NOT MATCHED
THEN INSERT` (count-based deny, cross-driver template work) — **planned next**; **(b)** a `WITH CHECK`
of the post-image (uniform for insert + update; a join-based restriction needs one probe). A brand-new
`INSERT` can never be a 0-row count (it adds 1 by nature) → it inherently needs `WITH CHECK`, not a count.

---

## Current state & limitations

Read-restriction **confirmed working live** — both the explicit `Source.Where(…)` form and the
concise `restrict … where` keyword form filter the query (single role). **Fail-closed contract landed**
(see *The handler*): handlers are procedures with a by-ref `Allowed` verdict (default deny),
presence-gated (absent → allow), and a handler that throws / swallows / never grants → deny; multi-role
OR is fail-closed (a failed role never widens; all failing → deny). **DELETE write-restriction landed**
— the 0-affected-row gate under an active policy throws access-denied. Functional verification of the
fail-closed paths and the `restrict` join path pending.

- **Save (INSERT / UPSERT) not restricted yet** — the folded `Where` is dropped by the upsert template
  (no `WHERE`); MERGE-based enforcement is planned, `WITH CHECK` the alternative. See *Write path*.
- **AOT bytecode cache must be invalidated after a compiler change.** The role module's compiled
  bytecode is cached in `sys_bytecode_cache` (a DB table; `Load` keys on `descriptor_id` and the AOT
  `kAOTFormatVersion`, NOT the compiler version or the stored `bytecode_version`). A `restrict` module
  compiled before the pushdown-AST recording was correct is served AST-less → `Where`/`Join` throws
  "cannot be lowered", which a module `try/except` swallows → the query runs unrestricted. Fix:
  `kAOTFormatVersion` was bumped (16 → 17) so stale blobs are rejected and recompiled. A future
  compiler change that alters bytecode output for unchanged source needs the same bump (or an
  `ibByteCodeCache::InvalidateAll`). The recorded AST *is* AOT-serialised (v14), so once recompiled it
  survives caching.
- **Primary-source only** — the primary `From` is restricted; joined tables in the *user's own* query
  are not yet (needs per-leaf handling over the query tree; `GetSources` is in place for it).
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
| `query/dataQueryBuilder.{h,cpp}` | `ibAccessPolicy` interface; policy injection; copy-apply-execute; **DELETE write-deny** (policy active + `ExecuteWrite` returns 0 affected → `ibBackendAccessException`); `AdoptOwnedSource` / `GetSources` / `GetWherePredicate` |
| `session/session.{h,cpp}` | `GetAccessPolicy`; concrete `ibRuntimeAccessPolicy` — **fail-closed** contract (`RoleOutcome` NoHandler/Failed/Succeeded via `CallAsProc` + by-ref `Allowed`, exception→deny, multi-role OR where a failed role never widens); ctor-cached role procUnits; policy built in `CompileRoot` |
| `system/value/valueQueryable.{h,cpp}` | `ibValueQueryDecorator` — Join/Where fold straight into the target query; `Join` accepts a single ON predicate (splits the `Compare` AST) or explicit key selectors |
| `compiler/codeDef.h` + `compiler/translateCode.cpp` | `KEY_RESTRICT` / `"Restrict"` keyword (lock-step) |
| `compiler/compileCode.{h,cpp}` | `CompileRestrictExpression` (the `restrict` block) + `EmitRestrictBody` (a clause → a 1-or-2-param lambda via `EmitFunctionBody`); triggered on `KEY_RESTRICT` in `GetExpression` and the statement switch |
| `compiler/lambdaQueryAst.{h,cpp}` | body → L4 pushdown AST — `ibBuildLambdaQueryAst` (LINQ, single alias) / `ibBuildRestrictedQueryAst` (`restrict`, bare span, 2-alias join ON); `in (list)` / `in <array>` parsing shared by both |
| `query/queryLowering.cpp` | `In` lowering — expands a captured array / collection item into its elements |
| `compiler/byteCodeAOT.cpp` + `compiler/cache/byteCodeCache.{h,cpp}` | AOT (de)serialisation of the pushdown AST (`m_lambdaExprAst`, v14) + the `sys_bytecode_cache` DB DAO; `kAOTFormatVersion` bump invalidates stale AST-less blobs |
| `compiler/procUnit.h` | `CallAsProc` / `CallAsFunc` comma-args variadics return **bool** (found?) — the presence gate the door uses without a separate lookup |
| `query/queryProvider.{h,cpp}` + `query/dbTableProvider.{h,cpp}` | `ExecuteWrite` returns the **affected-row count** (lifted from `RunQuery`); the door reads 0-under-policy as a write-deny |
| `frontend/.../codeEditor/codeEditorInterpreter.{h,cpp}` | `ibPrecompileCode::CompileRestrictExpression` — the intellisense-side walker (aliases → autocomplete, no bail on `restrict`) |
