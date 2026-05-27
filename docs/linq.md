# LINQ — language-integrated queries (experimental)

> **Status**: experimental landing on top of the lambda + iterator
> infrastructure from 2026-05-09..05-10. Block + chain syntaxes both
> production-grade on RAM arrays as of 2026-05-12. Major pivot risks
> closed (chain syntax now self-sufficient with closure capture from
> Phase A/B/C/D landed 2026-05-11..12); remaining risks documented
> at end-of-doc with chosen direction.
>
> Surface complete:
> - Block: `from / where / let / skip / take / select / distinct /
>   orderby (asc/desc) / join / group by [into g]` (terminal + non-
>   terminal — `into g` opens a continuation with `g` bound to
>   `Structure{Key, Values}`).
> - Chain: 31 ops — pipeline (Where / Select / Distinct / OrderBy /
>   OrderByDescending / GroupBy / Join / Skip / Take / SkipWhile /
>   TakeWhile / Reverse / Concat / Union / Intersect / Except /
>   WhereIndexed / SelectIndexed), terminals (Count / ToArray /
>   First / FirstOrDefault / Last / LastOrDefault / Single /
>   SingleOrDefault / Any / ElementAt / ElementAtOrDefault /
>   Contains / SequenceEqual / Aggregate).
> - Aggregations: `Sum / Min / Max / Average` no-arg + lambda-selector
>   overloads (closure capture works in selectors).

---

## Surface — two parallel syntaxes

The same end-to-end pipeline can be expressed through two different
script-side surfaces. Both ride the same backing infrastructure
(iterator states, lambdas, parent-chain context lookup); only the
syntax and execution strategy differ.

### Block syntax (eager inline)

```c
var arr = New Array;
arr.Add(1); arr.Add(2); arr.Add(3); arr.Add(4); arr.Add(5);

var q = from o in arr                // source binding (`from` is KEY_FROM)
        var doubled = o * 2          // let-clause (KEY_VAR or implicit `name = expr`)
        where doubled > 4            // filter (KEY_WHERE) — multiple allowed
        skip 1                       // KEY_SKIP — drops leading rows
        take 2                       // KEY_TAKE — limits row count
        select doubled               // projection (KEY_SELECT)
        distinct;                    // KEY_DISTINCT — post-projection dedup

Message("count: " + q.Count());
Foreach (x In q) { Message(x); }
```

Block compiles to a real `OPER_FOREACH` loop emitting `__r.Add(...)`
into an accumulator array. The accumulator's slot lives in the
caller's compile-context so it survives the LINQ scope teardown; it
gets copied into the user variable (`q`) via the surrounding
`OPER_LET` from `CompileDeclaration`.

### Chain syntax (lazy pipeline)

```c
var pred = Function(x) { Return x > 2; }
var proj = Function(x) { Return x * 10; }

var q = arr.Where(pred).Select(proj).Distinct();
Message("count: " + q.Count());

// OrderBy / GroupBy / Join — added 2026-05-12. Closure capture in
// lambda selectors works (Phase D — `arr.Where(λ closing over k)`).
var sorted   = arr.OrderBy(Function(x) Return x EndFunction);
var sortedD  = arr.OrderByDescending(Function(x) Return x EndFunction);
var grouped  = orders.GroupBy(Function(o) Return o.Country EndFunction);
var joined   = customers.Join(sales,
                              Function(c) Return c.Id EndFunction,
                              Function(s) Return s.CustId EndFunction,
                              Function(c, s) Return c.Name + ": " + s.Amount EndFunction);
```

Chain emits `OPER_CALL_LINQ` opcodes — compile-side detects LINQ
method names via `ibValue::FindLinqMethodByName` at chain-method emit
(see `compileCode.cpp` `IsNextDelimeter('.')` branch in
`PushCallFunction`); the opcode carries the `ibLinqMethod` enum id
directly in `m_param3.m_numIndex` (NOT a const-pool index), so the
runtime skips `FindMethod` / const-string lookup entirely and goes
straight to the virtual `ibValue::DispatchLinqMethod`. Per-class
methods (Add / Insert / Contains / ...) still emit `OPER_CALL_METHOD`.
Pipeline operators return a wrapped `ibValueQuery` whose
`CreateIterator()` produces a lazy state:
- `ibValueWhereState`, `ibValueSelectState`, `ibValueDistinctState` —
  per-element streaming.
- `ibValueOrderByState`, `ibValueGroupByState`, `ibValueJoinState` —
  materialise-on-first-MoveNext (sorting / grouping / join indexing
  need full or partial view of upstream).

Terminal operators (`Count / ToArray / First / Any`) drive the
pipeline to completion.

**Hash-based optimisations** (landed 2026-05-12):
- `ibValueJoinState` uses `std::map<ibValue, vector<ibValue>, KeyCmp>`
  for inner index — O(log N + bucket size) lookup per outer row vs
  earlier O(N×M) linear scan. Multi-match (one outer → multiple
  inner) supported via stateful `m_curBucket` / `m_bucketIdx` cursor
  across `MoveNext` calls.
- `ibValueGroupByState` uses hybrid `std::map<key, size_t, KeyCmp>` for
  O(log N) lookup + `std::vector<pair<key, vector<elem>>>` for
  insertion-order preservation. Previous O(N²) linear key-equality
  scan replaced.

**2-arg lambda invocation** for Join's projection — new
`CallLambdaWith2Args(fn, arg0, arg1, retVal)` helper mirrors
`CallLambdaWithArg` with both param slots bound by reference,
trailing params from compile-time defaults.

### Cross-pollination

The two syntaxes compose freely: a block-syntax result is an
`ibValueArray`, so `.Where(λ).Count()` works on it via chain
infrastructure. A chain-syntax result is an `ibValueQuery`, so
`Foreach x In q` iterates it.

---

## Grammar

Detected at expression start by the KEY_FROM keyword (added to
`s_listKeyWord` in `translateCode.cpp`). All other LINQ keywords
(`KEY_WHERE / KEY_SELECT / KEY_DISTINCT / KEY_SKIP / KEY_TAKE /
KEY_ORDERBY / KEY_ASCENDING / KEY_DESCENDING / KEY_GROUP / KEY_BY /
KEY_INTO / KEY_JOIN / KEY_ON / KEY_EQUALS`) are also reserved in
the lexer so the code editor highlights them uniformly. KEY_IN is
reused from the existing `Foreach ... In` construct.

```
linq-block := KEY_FROM <id> KEY_IN <expr>
              { from-clause | where-clause | let-clause }
              [ skip-clause ] [ take-clause ]
              [ select-clause ]
              [ KEY_DISTINCT ]

from-clause   := KEY_FROM <id> KEY_IN <expr>
where-clause  := KEY_WHERE <expr>
let-clause    := KEY_VAR <id> '=' <expr>
              |  <id> '=' <expr>                ; implicit (OES auto-declare)
skip-clause   := KEY_SKIP <expr>
take-clause   := KEY_TAKE <expr>
select-clause := KEY_SELECT ( <expr> | '{' field { ',' field } '}' )
field         := <id> '=' <expr>
```

The where/let-clauses are processed in a **unified while-loop** —
each iteration looks for either keyword and consumes whichever
appears next. This matches C# LINQ grammar where `let` and `where`
can interleave freely between `from` and the terminal `select`.

Where the keyword is also a valid method name (`obj.Where(...)`,
`obj.Select(...)`), `GETIdentifier(..., acceptKeyword=true)` accepts
KEYWORD lexems in property-access positions — this is what makes
chain syntax work alongside the block-syntax keywords.

---

## Architecture

### Compile-time

`ibCompileCode::CompileLinqExpression(callerCtx)` is the entry
point — called from `GetExpression`'s KEY_FROM branch and from
`CompileBlock`'s statement-level switch (both behave like `KEY_NEW`).

```
CompileLinqExpression(callerCtx):
    1. Allocate result + counter slots in callerCtx.
       — m_resultArray, m_skipCounter, m_takeCounter, m_constOne.
       — Emitted OPER_NEW Array → m_resultArray.
       — Emitted OPER_CONSTN 0/0/1 inits for counters.
    2. Create RETURN_BLOCK-kind child context (linqCtx).
       — linqCtx->m_linqData = make_unique<ibLinqContextData>().
       — data.m_resultArray + counters copied in (caller-context slots).
    3. CompileLinqBlock(linqCtx).
    4. Return data.m_resultArray.

CompileLinqBlock(linqCtx):
    1. data = *linqCtx->m_linqData.
    2. Consume <id> KEY_IN <source-expression>.
       — Binding registered via linqCtx->GetVariable(...).
       — RETURN_BLOCK delegates allocation to parent via
         CreateVariable chain, so slots land in host frame.
    3. Emit OPER_LET @in_ := source; OPER_FOREACH.
    4. If next is KEY_FROM:
         GETKeyWord; CompileLinqBlock(linqCtx);   ← recursive
       Else:
         while next is WHERE or let-clause:
           emit OPER_IF or OPER_LET accordingly.
         Optional SKIP / TAKE check chains.
         Optional SELECT (single-expr or `{ ... }` anon-row).
         Optional DISTINCT (Contains-check + IF + back-patch).
         Emit __r.Add(addValue).
         Back-patch all WHERE skip-IPs.
    5. Emit OPER_NEXT_ITER + back-patch foreach.m_param4.
    6. FinishLoopList — patches break/continue jumps emitted by
       SKIP (continue-list) / TAKE (break-list).
```

Key types — see `compileContextLinqData.h`:

```cpp
struct ibLinqBinding {
    enum Origin { FromSource, FromLet, FromJoin, FromGroup };
    wxString    name;
    Origin      origin;
    ibParamUnit valueSlot;       // current iter value
    ibParamUnit iterInSlot;      // @in_
    ibParamUnit iterItSlot;      // @it_
    ibClassID   typeHint;
    int         foreachStartIp;
};

struct ibLinqContextData {
    std::vector<ibLinqBinding> m_bindings;
    ibParamUnit m_resultArray;    // accumulator (caller-context slot)
    ibParamUnit m_skipCounter, m_takeCounter, m_constOne;
    std::vector<LinqFunction> m_functions;   // Phase 2 lazy chain — empty in Phase 1
    const ibLinqBinding* FindByName(const wxString&) const;
};
```

`ibCompileContext` carries `std::unique_ptr<ibLinqContextData> m_linqData` —
non-null only on the RETURN_BLOCK-kind context that
`CompileLinqExpression` allocates. Two predicate helpers on
`ibCompileContext`:

```cpp
bool IsLinq() const   { return m_linqData != nullptr; }
bool IsInLinq() const { /* walks parent chain */ }
```

### Runtime

Chain syntax dispatches through the virtual `ibValue::DispatchLinqMethod`,
called by the `OPER_CALL_LINQ` handler in `procUnit.cpp`. The opcode
carries the `ibLinqMethod` enum id directly in `m_param3.m_numIndex`
(set by compile-side at chain-method emit time via
`ibValue::FindLinqMethodByName`). Runtime steps:

1. Read `enumId = m_param3.m_numIndex`, `argCount = m_param3.m_numArray`.
2. Build an `ibRunContextSmall` of size ≥ argCount and load parameters
   from the inline `OPER_SET` / `OPER_SETCONST` tape that follows
   (identical to `OPER_CALL_METHOD`'s param-load loop).
3. Dispatch through the receiver's virtual `DispatchLinqMethod`. The
   base impl walks the `TYPE_REFFER` chain and delegates to the
   file-scope `ibValueLinqDispatchImpl` state-class machinery;
   subclasses (e.g. `ibValueArray`) can override for kind-specific
   short-circuits (`Count` / `ElementAt` / `Contains` direct on a
   concrete vector).

`ibValue::ibLinqMethod` (declared in `value.h`) is the strongly-typed
enum. `ibValue::GetLinqMethodTable()` is the single source of truth
for metadata: each entry carries `{ enum id, name, helper }`. The
table services `FindLinqMethodByName` (compile-side resolver); it is
NOT an IntelliSense source — chain methods are hidden from
autocomplete (see "Editor integration" §). The old `kLinqMethodMask`
fallback inside `OPER_CALL_METHOD` was retired once compile-side emit
became the single authoritative source — AOT v10 forces a recompile,
so no legacy blobs reach the runtime with a LINQ name on
`OPER_CALL_METHOD`.

Iterator state classes (in `procUnitLinq.cpp`): each holds a
`shared_ptr<ibValueIteratorState> m_upstream` and exposes
`MoveNext/Reset/PeekSample`. Lambdas inside states are invoked via
`CallLambdaWithArg(fn, arg, retVal)` / `CallLambdaWith2Args(...)` —
thin shims over the unified `CallLambdaWithArgs(fn, argPtrs, argCount,
retVal)` which mirrors Phase 1/2 of `OPER_CALL_LAMBDA` minus the
bytecode tape walk (args come from C++ pointers). Intersect and Except
share a single templated `ibValueSetOpState<keepIfInOther>` — the only
difference between the two is which side of the set-membership test
emits.

Block syntax doesn't go through these states — it emits regular
`OPER_FOREACH` + `OPER_IF` + `OPER_CALL_METHOD` inline. Same opcodes a
hand-written for-each + Add chain would produce.

---

## Closure capture (block syntax — free)

Because block-syntax LINQ compiles into inline opcodes in the
caller's bytecode (with RETURN_BLOCK ctx delegating var allocation
to the host frame), **closure capture works automatically**: any
outer-scope variable is visible inside `where / let / select`
expressions via the standard `context->GetVariable` parent walk.

```c
var threshold = 3;
var arr = New Array; arr.Add(1); arr.Add(2); arr.Add(3); arr.Add(4); arr.Add(5);

var q = from o in arr
        var doubled = o * 2
        where doubled > threshold     // ← `threshold` is outer local
        select doubled;
```

This is the **major advantage of block syntax** over chain syntax —
chain syntax uses lambdas which don't currently capture outer
locals (see `lambda.md` "Closure capture — rejected"). For chain
syntax to do the same, the lambda would need closure support
(deferred work).

---

## Editor integration

### Fold parser (`codeEditor.h::ibFoldLevelParser`)

A `FoldLinq` kind was added to `FoldKind`. `KEY_FROM` opens a fold,
`KEY_SELECT` or `KEY_GROUP` closes it. Multi-line block-syntax queries
collapse to their first line in the gutter:

```c
var q = from o in orders            // header (open) — fold marker here
        where o.Total > 100         //   body
        let doubled = o.Total * 2   //   body
        select { N=o.N, V=doubled } ; // close — `select` triggers
```

Limitations:
* **Multi-from with single select** opens twice (once per `from`),
  closes once. The per-kind counter absorbs the imbalance — the
  `hint_ignore_fold` trim in `UpdateFoldLevel` hides the trailing
  unclosed open from rendering.
* **`distinct` is not a fold-close** — it's a modifier on `select`,
  not a standalone terminator. The fold closes on `select`; `distinct`
  trails inside the unfolded tail (typically the same line as
  `select`).
* **Non-terminal `group ... into g <continuation>`** — `group`
  alone would mis-close the fold (the continuation after `into`
  must stay folded). The lex-walker in `CalcFoldLevel` peeks
  forward from `group` for `KEY_INTO` before any other LINQ
  open/close keyword; if found, `group` is treated as
  non-terminal and the fold stays open through the continuation
  until `select`. Terminal `group X by K` (no `into`) still closes
  on `group`.

### IntelliSense / autocomplete

After `.` on any object, the codeEditor's `AddKeywordFromObject`
appends only the receiver's class-native methods (read from
`vObject.GetNMethods()`). The LINQ chain surface is intentionally
**hidden** from per-object autocomplete — users drive queries
through block syntax (`from / where / group / join / select`), so
sprinkling `Where / Select / SingleOrDefault / Any / ...` on every
non-iterable receiver (scalars, forms, documents, buttons) was pure
noise. Chain methods remain reachable in the language — anyone who
types `arr.Where(...)` manually still compiles, `OPER_CALL_LINQ`
dispatches, runtime throws on non-iterable. `GetLinqMethodTable()`
stays as the single source of truth for compile-side
`FindLinqMethodByName`; it just isn't an autocomplete source
anymore.

LINQ block-syntax keywords (`from / where / let / skip / take /
select / distinct / orderby / group / join / by / equals / into /
ascending / descending`) are registered in `s_listKeyWord`
(`translateCode.cpp`) and surface in the global Ctrl+Space keyword
list automatically — no contextual filtering yet, they appear
alongside `If / Then / While / ...`.

Block-syntax intra-query context — minimal walk wired
2026-05-12. `ibPrecompileCode::CompileLinqExpression` parses the
query at IntelliSense time, registers from/let/join/group-into
bindings in `m_activeContext`, and consumes clauses so the host
parser resumes cleanly at the next statement. The caret-inside-
block discipline mirrors `CompileForeach`: bindings survive only
when the caret landed inside the query's lex range, otherwise
they're dropped post-walk. **Element type is not inferred** —
bindings carry empty type, so `o.` inside `from o in arr` shows
class-native methods and global keywords only, no `o`-element-
specific properties yet. Type inference is a future iteration
(would walk the source expression's `m_paramType`, peek the iterator
sample like `CompileForeach` does).

---

## Diagnostics

`linq.log` and the `LinqLog` / `LinqCompileLog` / `LinqLambdaTag` /
`LinqValueTag` helpers were retired 2026-05-12 after the LINQ surface
stabilised — they served their purpose during compile + runtime
development. If diagnostics are needed again, prefer `wxLogDebug` at
coarse-grained entry points (CompileLinqExpression enter/exit,
DispatchLinqMethod), not per-row writes; the previous per-row file
I/O was a real perf cost on 10K+ row queries.

---

## Nuances and known landmines

### IsEmptyValue(NUMBER 0) is true

`ibValue::IsEmpty()` returns `true` for TYPE_NUMBER value 0
(`m_fData.IsZero()`). This makes `OPER_NOT(tmpFind)` mis-fire when
`Find` returns index 0 (a legitimate "found at position 0" answer).
Block-syntax `distinct` originally hit this — fixed by switching
from `Find/NOT` to `Contains/NOT` (Contains returns bool, which
IsEmpty distinguishes unambiguously).

**Implication**: any future LINQ feature that returns a "position
or empty" from a method must not rely on `NOT` for the empty check.
Either use a boolean predicate method (`Contains`) or compare
against an explicit TYPE_EMPTY value.

### RETURN_BLOCK reuse for LINQ scope

Originally added `RETURN_LINQ` enum value as a marker, then removed
it. The linq context is allocated as RETURN_BLOCK kind to inherit
the existing chain-delegation logic for `CreateVariable / AddVariable`
(vars in host frame). LINQ-distinction is carried by `m_linqData !=
nullptr`, not by the enum tag.

**Risk**: if RETURN_BLOCK acquires LINQ-specific behaviour later
that doesn't apply to plain `{ }` blocks, this reuse becomes a hack.
Re-introduce RETURN_LINQ at that point.

### m_linqData lifetime

Owned by `ibCompileContext` via `unique_ptr` — destroyed automatically
when the context dies. Result/counter slot references inside
`ibLinqContextData` point at slots in the **caller's** compile
context, not the linq context — they survive the linq scope teardown
and are returned to `CompileDeclaration`'s `OPER_LET` via
`CompileLinqExpression`'s return value.

### Unified where/let-clause loop

Multiple WHEREs allowed (each emits its own OPER_IF, all back-patch
to the same post-Add IP). Multiple let-clauses interleave with
WHEREs. Order between `from` and `select / skip / take` is free.

### "current row" alias — NOT a feature

`from o in arr1` does **not** rebind `arr1` to mean "current row"
inside the block. `arr1` remains the source Array. Use the
explicit binding name (`o`) to reference the current row.

Pascal-style implicit `with`-clause (`from o in arr1` rebinds
properties of `arr1` to local scope) is a **deferred Phase 1.5
feature** — not in Phase 1.

---

## Pivot risks — status after 2026-05-12 closure capture landing

1. ~~**Two syntaxes (block + chain) might be wrong.**~~ **Resolved
   keeping both**. Chain became self-sufficient with Phase D closure
   capture (`arr.Where(Function(x) { Return x > threshold; })` works).
   Block remains for `let` clause, multi-from / cartesian product,
   transparent identifiers — features chain doesn't have. Users
   choose surface per use case.

2. **Eager block-syntax doesn't scale.** Open. Materialising into
   `__r` per LINQ block eats N ibValues for N-row queries. Chain
   syntax is the lazy alternative; users hitting RAM pressure
   switch to chain. SQL pushdown for DB-backed sources is a separate
   massive arc. Deferred 2026-05-12 — revisit on concrete user
   pressure.

3. **RETURN_BLOCK reuse for linq scope** — still in tree; no
   refactor needed. Behaviour stable.

4. **`Contains/NOT` distinct check** — still in tree; no IsEmpty
   semantics changes prompted a re-think. Stable.

5. ~~**Chain syntax closure capture absent.**~~ **Closed 2026-05-11..12
   via Phase A/B/C/D**. `arr.Where(Function(x){return x > threshold;})`
   captures `threshold` from outer scope. Per-frame heap-promotion via
   `make_shared<ibRunContext>` + `enable_shared_from_this`; lambda
   value holds `vector<shared_ptr<ibRunContext>> m_capturedFrames`;
   OPER_CALL_LAMBDA wires shim's `m_pppArrayList[1..N]` per-invoke.
   See `docs/closure-capture.md`.

---

## Nested-LINQ — historic bug class, closed 2026-05-12

Two failure modes were predicted for nested-FROM with inner-level
GROUP or JOIN:

* **GROUP at inner level** — `groupsContainer` slot persists across
  outer iters. **Closed**: GROUP's per-row Container.Property
  (lookup-or-create) pattern semantically flattens groups across
  outer iters correctly (`Red: [Apple, Wine], Yellow: [Banana, Beer]`).
  Validated by `test_linq_nested_group.txt`.

* **JOIN at inner where T references outer** — `hashSlot` built once
  from iter-1's outer.Bucket, stale for iter-2. **Closed via
  m_needsReset**: `CompileLinqJoin` scans T's lex range for outer
  binding identifiers (`data.m_bindings.size() > 1` → compare against
  all but the last entry); on match marks `pj.m_needsReset = true`
  and emits per-row `OPER_LET hashSlot = empty` so the IsEmpty guard
  falls through to trampoline rebuild every row. O(M²) trade-off
  for correctness. Validated by `test_linq_nested_innerjoin.txt`.

Tests renamed: `test_linq_nested_group.txt`,
`test_linq_nested_innerjoin.txt` (previously had `_BUG` suffix).

---

## Deferred to Phase 1.5+

* ✓ **`orderby <expr> [ascending|descending]`** — landed via `SortByKeys`
  helper on `ibValueArray` (compile-time emits parallel key-array
  populated lockstep with `__r.Add`, post-loop call to `SortByKeys`).
* ✓ **`join b in Y on a.K equals b.K`** — landed 2026-05-11 PM via
  trampoline pattern (mirrors `m_listCallFunc`'s deferred resolution).
  Per-iter lookup inline in outer body; hash build trampoline after
  outer `NEXT_ITER`, reached once on first iter via absolute-ip
  GOTO. T and K2 saved as lex-range and replayed inside trampoline.
  Hash dict = `ibValueContainer` (Insert/Property). Records on
  `ibLinqContextData::m_pendingJoins`. Multiple joins at same level
  supported. See session-2026-05-11.md "LINQ JOIN landed".
* ✓ **`group X by K`** — landed 2026-05-11 PM (terminal form, no
  `into`). Per-row aggregation into `groupsContainer` (Container,
  IsEmpty-guarded one-shot init), lookup-or-create bucket via
  Property + Insert. Post-loop expansion after foreach m_param4
  patch — inner foreach over groupsContainer emits one
  `New Structure("Key, Values", pair.Key, pair.Value)` into
  resultArray per group. Mutually exclusive with SELECT. Result:
  `Array<Structure{Key, Values:Array}>` — usage `g.Key /
  g.Values.Count() / foreach x in g.Values`. See
  session-2026-05-11.md "LINQ GROUP BY landed".
* ✓ **Chain-syntax `OrderBy / OrderByDescending / GroupBy / Join`** —
  landed 2026-05-12. Lazy `ibValueIteratorState` subclasses:
  `ibValueOrderByState` (materialise + stable_sort),
  `ibValueGroupByState` (hybrid std::map for O(log N) lookup +
  vector for insertion-order preservation), `ibValueJoinState`
  (std::map hash bucket for inner index, stateful inner-bucket
  cursor for multi-match across MoveNext calls). 2-arg lambda
  invocation via new `CallLambdaWith2Args` helper for Join's
  projection. See session-2026-05-12.md.
* ✓ **Chain surface complete — 21 more ops landed 2026-05-12**:
  pipeline `Skip(n) / Take(n) / SkipWhile(λ) / TakeWhile(λ) /
  Reverse()`; set ops `Concat / Union / Intersect / Except`;
  terminals `Last / LastOrDefault / Single / SingleOrDefault /
  FirstOrDefault / ElementAt(n) / ElementAtOrDefault(n) /
  Contains(value) / SequenceEqual(other)`; reducer
  `Aggregate(seed, λ)`; indexed `WhereIndexed(λ) / SelectIndexed(λ)`
  (lambda takes `(elem, index)`). 31 methods total on chain
  surface. `OrDefault` semantics fix: dispatcher always writes
  ret (even on empty paths) — `CopyValue(ret, ibValue())` resets
  caller's slot from stale leftover. Smoke
  `test_linq_chain_ops.txt` (10 sections) green.
* ✓ **Lambda-selector aggregations on `ibValueArray`** —
  `Sum(λ) / Min(λ) / Max(λ) / Average(λ)` landed 2026-05-12.
  Uses host-API `InvokeLambdaWithArg` (`procUnit.h`). Closure
  capture in selector body works: `arr.Sum(Function(x) Return
  x * k EndFunction)` closes over outer `k`.
* ✓ **Average decimal trim** — reverted 2026-05-12.
  `ibValueArray::Average` / `AverageWithSelector` no longer round
  to 6 digits. Behaviour now matches manual `Sum() / Count()` —
  caller formats via `ToString(format)` or explicit `.Round(n)`.
  Removes the silent inconsistency between `arr.Average()` (was
  trimmed) and `arr.Sum()/arr.Count()` (no trim).
* ✓ **`group X by K into g`** — non-terminal group, landed
  2026-05-12. Compile-side: CompileLinqBlock's GROUP branch detects
  KEY_INTO + records `m_groupIntoName`. CompileLinqExpression's
  post-block expansion opens a fresh foreach over
  `m_groupsContainer`, binds `<g>` to `Structure{Key, Values}` per
  pair, and re-enters CompileLinqBlock through a new pre-bound
  overload that skips the from-clause-parse and starts directly
  at the leaf machinery. Continuation clauses (where / orderby /
  select / distinct on `g`) flow through the same WHERE/SELECT/Add
  + NEXT_ITER path as a regular leaf — no parallel implementation.
  Fold parser's CloseKindFor was updated to NOT close on `group`
  blindly; the lex walker peeks for KEY_INTO before the next LINQ
  open/close to keep the fold open across the continuation. No
  separate `ibValueGroup` value-class — reused `ibValueStructure`
  with Key/Values fields.
* **`let` with non-trivial expressions and intermediate scoping** —
  currently emits plain OPER_LET to caller's frame; for nested
  `from` with `let` interleaving, transparent-identifier rewriting
  may be needed.
* **`with`-clause for implicit property scope** — `from o in arr`
  optionally rebinds `o.X` properties to bare `X` in subsequent
  clauses (Pascal-style `with`). Not strictly LINQ but useful.
* **SQL pushdown** — translate the bytecode visitor into SQL for
  DB-backed sources (catalogs / documents / registers). Massive
  effort, separate phase.
* **Dispatch refactor — virtual methods on ibValue**
  (architectural arc, deferred 2026-05-12): replace LinqHelper +
  realNum + ibValueLinqDispatch switch with virtual methods on
  ibValue base; each subclass overrides as needed. Closes
  central-registry debt; ~400-600 LoC; future session.
  Memory: `project_value_dispatch_via_virtual.md`.

---

## Closure capture in lambdas (chain syntax pre-requisite)

Per-frame heap-promotion mechanism — see `docs/closure-capture.md`.
Quick reference for LINQ contexts:

- Chain syntax lambdas (`Where(λ)`, `Select(λ)`, `OrderBy(λ)`,
  `GroupBy(λ)`, `Join(λL, λR, λproj)`) capture outer-scope vars
  via Phase A/B/C/D infrastructure. `m_needsHeapFrame` is set
  lazily on the enclosing fn's compile-side `ibFunction` when
  GetVariable resolves an identifier past a lambda boundary.
- Block syntax body sees enclosing scope through standard
  RETURN_BLOCK chain delegation (no closure machinery needed —
  scope is lexical at compile time).
- Lambda-selector aggregations (`arr.Sum(λ)`) trigger lambda
  invocation from C++ host code via `InvokeLambdaWithArg`. The
  lambda's `m_capturedFrames` is built at OPER_LFUNC materialise
  time and persists for the value's lifetime, so host-fired
  lambdas see their closure correctly.

---

## Files touched

| File | Role |
|---|---|
| `src/engine/backend/compiler/codeDef.h` | 15 LINQ keyword enums (KEY_FROM..KEY_INTO; KEY_LET removed). `OPER_CALL_LINQ` opcode. |
| `src/engine/backend/compiler/translateCode.cpp` | Keyword strings in `s_listKeyWord` |
| `src/engine/backend/compiler/compileContext.h` | `m_linqData : unique_ptr`, `IsLinq() / IsInLinq()` helpers, includes new header |
| `src/engine/backend/compiler/compileContextLinqData.h` | **New file** — `ibLinqBinding`, `ibLinqContextData`, `ibLinqPendingJoin` definitions |
| `src/engine/backend/compiler/compileCode.h` | `CompileLinqExpression / CompileLinqBlock` declarations |
| `src/engine/backend/compiler/compileCode.cpp` | LINQ compile path — `CompileLinqExpression / CompileLinqBlock`, KEY_FROM hooks in GetExpression + CompileBlock, `GETIdentifier(acceptKeyword)` extension, `ibValue::FindLinqMethodByName` call at chain-method emit site → `OPER_CALL_LINQ` |
| `src/engine/backend/compiler/procUnit.cpp` | `OPER_CALL_LINQ` runtime case (no `FindMethod` walk, enum id straight from `m_param3.m_numIndex` → `pVariable2->DispatchLinqMethod(...)`) |
| `src/engine/backend/compiler/procUnitLinq.cpp` | **New file** — split out 2026-05-12. LINQ runtime — 16 distinct state classes (`Where / Select / Distinct / OrderBy / GroupBy / Join / Skip / Take / SkipWhile / TakeWhile / Reverse / Concat / WhereIndexed / SelectIndexed` + templated `ibValueSetOpState<>` aliased to `IntersectState` / `ExceptState`; Union composes Concat + Distinct). `CallLambdaWithArgs` (unified arity-N invocation) + arity-specific shims, `InvokeLambdaWithArg` (BACKEND_API), `ibValueQuery`, `ibValueLinqDispatchImpl` switch, `ibValue::DispatchLinqMethod` default impl, `ibValue::GetLinqMethodTable()`, `ibValue::FindLinqMethodByName`. |
| `src/engine/backend/compiler/procUnit.h` | `InvokeLambdaWithArg` declaration (BACKEND_API) |
| `src/engine/backend/compiler/value.h` | `ibLinqMethod` enum, `ibLinqMethodInfo` struct, `GetLinqMethodTable()` static accessor, `FindLinqMethodByName` static, virtual `DispatchLinqMethod` |
| `src/engine/backend/compiler/value.cpp` | Per-class method resolver (`FindMethod / GetNParams / HasRetVal / GetMethodName / GetMethodHelper`) — LINQ surface lives entirely on the `OPER_CALL_LINQ` path |
| `src/engine/backend/system/value/valueArray.h` | `Contains` method + `enContains` enum, aggregation declarations (Sum/Min/Max/Average + selector overloads), `DispatchLinqMethod` override |
| `src/engine/backend/system/value/valueArray.cpp` | Aggregation impls; selector dispatch in CallAsFunc; `InvokeLambdaWithArg` integration; `DispatchLinqMethod` override (7 short-circuits — Count / ToArray / ElementAt / Contains / Last / First / Any direct on the underlying vector) |
| `src/engine/backend/compiler/byteCodeAOT.cpp` | `kAOTFormatVersion = 10` (opcode-shift after `OPER_CALL_LINQ` insertion) |
| `src/engine/backend/backend.vcxproj` + `.filters` | New files registered (compileContextLinqData.h, procUnitLinq.cpp, procUnitValues.h) |
| `src/engine/frontend/win/editor/codeEditor/codeEditor.h` | `FoldLinq` kind in `ibFoldLevelParser`; `KEY_FROM` opens, `KEY_SELECT` / `KEY_GROUP` close |
| `src/engine/frontend/win/editor/codeEditor/codeEditorLoader.cpp` | `AddKeywordFromObject` lists class-native methods only — LINQ chain surface intentionally hidden from `.`-dropdown (block syntax is the front door for users) |
| `src/engine/frontend/win/editor/codeEditor/codeEditorInterpreter.{h,cpp}` | `ibPrecompileCode::CompileLinqExpression` — minimal block-syntax walker. `KEY_FROM` hook in `GetExpression` + statement-level `CompileBlock` switch. Registers from/let/join/group-into bindings in `m_activeContext`, drops them post-walk when caret outside the query (mirrors `CompileForeach` discipline) |

Build clean Debug/Win32 as of 2026-05-12. Working copy uncommitted —
experimental discipline maintained; commit grouping not pursued.
Pivot risks #1, #5 closed via 2026-05-11..12 closure capture arc;
risk #2 (lazy block-syntax) deferred per session decision.

---

## 2026-05-12 audit — landed simplifications + remaining gaps

**Unifications applied this session:**
- `GetLinqMethodTable()` — single source of truth for `{ enum id,
  name, helper }`. Drives compile-side `FindLinqMethodByName`.
  Replaces the previous 32-line `IB_LINQ_TRY(...)` macro chain.
  (Was also wired into codeEditor autocomplete briefly — reverted:
  chain methods hidden from `.`-dropdown, see "IntelliSense" §.)
- `CallLambdaWithArgs(fn, argPtrs, argCount, retVal)` — unified
  arity-N lambda invocation. `CallLambdaWithArg` and
  `CallLambdaWith2Args` are now ~5-line inline shims over it. Saves
  ~80 LoC of near-duplicate prologue/epilogue.
- `ibValueSetOpState<bool kKeepIfInOther>` — templated set-op state
  class. `ibValueIntersectState` = `<true>`, `ibValueExceptState` =
  `<false>`. Saves ~40 LoC of duplicate ctor/MoveNext/EnsureBuilt.
- Editor fold support for block-syntax LINQ (`FoldLinq` kind,
  `KEY_FROM` opens, `KEY_SELECT` closes; `group` closes only when
  no `into` follows — peek-ahead in `CalcFoldLevel`).
- Editor autocomplete intentionally **does not** show chain LINQ
  methods after `.` — they polluted dropdowns on every non-iterable
  receiver. Users drive queries through block keywords; chain remains
  a valid hand-written surface in the language.
- `ibPrecompileCode::CompileLinqExpression` — minimal precompile
  walker. Bindings (from/let/join/group-into) visible in autocomplete
  inside the query's lex range; dropped when caret outside.
- **`group X by K into g <continuation>`** — non-terminal group.
  Compile-side: GROUP branch detects `KEY_INTO`, records
  `m_groupIntoName`. CompileLinqExpression's post-block expansion
  opens a new foreach over `m_groupsContainer`, binds `g` to
  Structure{Key, Values}, re-enters CompileLinqBlock through a
  new pre-bound overload that skips from-parse and dives into the
  leaf machinery. Smoke: `test_linq_group_into.txt`.
- **Average decimal-trim reverted** — `Average` and
  `AverageWithSelector` no longer round to 6 fractional digits.
  Behaviour matches manual `Sum() / Count()` — caller formats
  explicitly via `ToString(...)` or `.Round(n)`.
- **linq.log diagnostics stripped** — `LinqLog` / `LinqLambdaTag` /
  `LinqValueTag` / `LinqCompileLog` definitions + ~88 call sites
  removed. If diagnostics are needed again, prefer `wxLogDebug` at
  coarse entry points, not per-row writes.

**Dead-code / stale-reference scan — clean:** no `OPER_CALL_M`,
`OPER_CALL_VAL`, `kLinqMethodMask`, `GetLinqHelper`, `s_linqHelper`
references in active code. (Session/handoff logs preserved
verbatim as history.)

**Remaining gaps — IntelliSense:**
- Block-syntax bindings are registered without type inference. `o`
  appears in autocomplete inside `from o in arr ...`, but `o.`
  shows global keywords only — no element-type-specific
  properties/methods. Fix: walk source expression's `m_paramType`,
  peek the iterator sample (mirror of `CompileForeach` at
  codeEditorInterpreter.cpp:1449-1453).
- Contextual keyword filtering (after `from x in expr ` only
  suggest `where / let / select / orderby / group / join / skip /
  take`) unimplemented. Currently all global keywords appear.
- Type-hint chain — `var q = arr.Where(λ).Select(λ);` Ctrl+Space on
  `q.` doesn't propagate iterable-returning-iterable through the
  chain, so class-native methods of the result drop. Chain methods
  are hidden anyway, so the only loss is the receiver's own
  per-class surface.

**Remaining gaps — feature:**
- ~~`group X by K into g <continuation>` (non-terminal group)~~ —
  landed 2026-05-12. Smoke: `test_linq_group_into.txt`.
- Non-terminal LET with nested-from scoping (transparent
  identifiers).
- Pascal-style `with`-clause for implicit prop scope.

**Remaining gaps — perf:**
- Lazy block-syntax (#11 above, deferred — chain syntax serves as
  the lazy alternative).
- SQL pushdown for DB-backed sources (massive separate arc).

**Remaining gaps — architectural:**
- Virtual-dispatch refactor (split central switch into 32 per-method
  virtuals on `ibValue`). Discussed 2026-05-12 — deferred until
  concrete override pressure (≥2 specialising subclasses or SQL
  pushdown design). Memory note `project_value_dispatch_via_virtual.md`.
- Lexer-level LINQ token tagging — add `LINQ` to the token-type
  enum in `codeDef.h` so that LINQ keywords / method names get
  tagged at lex time (`lex.m_lexType == LINQ`). Would replace the
  `lex.m_numData == KEY_FROM || KEY_WHERE || ...` enumeration in
  fold parser / precompile clause loop with a single type test.
  Source-of-truth options: just s_listKeyWord LINQ keywords
  (KEY_FROM..KEY_INTO, 15 names) or extended via
  `ibLinqMethodInfo` table (32 chain-method names). Discussed
  2026-05-12 — deferred (current code does specific KEY_*
  checks which work fine; lexer change carries naming-conflict
  risk where chain-method names overlap with block-syntax keywords).

**Remaining gaps — cosmetic:**
- ~~`linq.log` strip (51+ call sites)~~ — done 2026-05-12.
