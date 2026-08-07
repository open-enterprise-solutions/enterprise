# Query Language — Architecture Arc

> **Read the floor plan first:** [query-engine-layers.md](query-engine-layers.md) — the L1–L5
> taxonomy (which floor is which, L3 = {L3-1/2/3}, L4 = {L4-1/2/3} on top of L3, the shared L2↔L3
> technical floor, what feeds metadata). This doc is the detailed L2 / L3 / L4 arc **under** that map.
>
> **Status:** **LANDED (experimental working copy) — the full ladder is in the tree.**
> L1 (drivers ×5) release · L2 (`ibDatabaseQueryBuilder`, structured IR + dialect dictionary)
> release-candidate · L3 (`ibDataQueryBuilder` source-agnostic door: read + write + aggregation
> + dot-walk) realized · L4-1 (text query language, `query/queryLexer|Parser|Lowering`) +
> L4-2 (LINQ push-down via `ibValueQueryable` + the `Data.*` source root) landed · L5
> (declarative composer, `composition/`) landed — see [data-composer.md](data-composer.md).
> The lower sections (§4–§22) are the design this converged from; §23–§24 + the dated update
> blocks below are what is actually in the tree.
>
> **Last updated:** 2026-07-27 (semi-join key reduction in the RAM stitch — a materialised side's join
> keys are pushed into the other side's read as `key IN (…)`, so a leaf fetches only rows that can join;
> + the set-valued `ibQueryFilterOp::In` it rides on — see the final dated-update block). The dated
> update blocks run newest-last; read §23–§24 for the realized L4/L5 mechanism.
>
> ### Landed snapshot (2026-06-07)
>
> - **L1** `ibDatabaseLayer` ×5 — release; each driver vends its `ibDialectDictionary`
>   (`GetDialect()`), zero central type-switch.
> - **L2** `ibDatabaseQueryBuilder` (`databaseLayer/databaseQueryBuilder.{h,cpp}`) —
>   release-candidate. Structured `ibQueryIR` (never raw SQL), generic renderer driven by
>   the dialect dictionary. Full vocabulary: Scan/Filter/Project/Sort/Limit/**Join**
>   (+ aliased `ibScan(table,alias)`, qualified `t.col`, `t.*`)/Aggregate(GroupBy/Having)/
>   Subquery/Distinct/Union; expressions CASE/IN/IS NULL/NOT/BETWEEN/**Cast**/arithmetic;
>   DDL CreateTable/DropTable/Add/DropColumn/AlterColumn(SQLite throws)/Create/DropIndex;
>   DML Insert/Update/Delete/Upsert + `ibQueryStatement` (deferred bind-capture).
> - **L3** `ibDataQueryBuilder` (`query/dataQueryBuilder.{h,cpp}` — **renamed from
>   `ibMetaQueryBuilder`**: the door reads through `ibBackendQueryable`, the source may be a
>   temp table, so it is NOT metadata-bound). Reads every family (catalog / document /
>   register / constant / tabular) through the queryable; write-core (`Upsert`/`DeleteByKey`/
>   `WriteRow`); aggregation `SelectAggregate` + `Having`; **dot-walk** (`SelectPath` over
>   `ibBackendQueryColumn` — auto-joins a reference path, e.g. `Product.Manufacturer.
>   Name`). Backing-blind: a polymorphic `ibMetaResultSource` (DB cursor OR RAM
>   table) chosen once in `MakeProvider`; consumers read uniformly.
> - **Dot-walk physical foundation** — each catalog/document row now stores its OWN
>   reference (the data-reference predefined attribute's `_RTRef`/`_RRRef` column, unblocked
>   in DDL + written on save), byte-identical to any reference TO it, so a join equates
>   `source.<ref>_RRRef = target.<selfref>_RRRef`.
> - **Register consumers migrated off raw SQL** — information register (`Get` ×2 → L3;
>   `ComputeSlice` → L2 self-join IR) and accumulation register (`Balance`/`Turnovers` →
>   L2 aggregate IR) carry NO hand-concatenated SQL. The slice / balance / turnover are
>   **call-scoped companion queryables** (filters in the ctor, handed to `From()`,
>   RAM-computed via `ComputeRows` → the register's `Compute*`), sharing one
>   `ibComputedRegisterQueryable<TReg>` forwarding base (`query/queryable.h:575` — it never got a
>   header of its own)
>   and one lowering header (`metaCollection/partial/registerQueryLowering.h`:
>   `ibRegFieldsOf`/`ibRegValueField`/`ibRegCompositeIR`).
> - **Open (next arcs):** ~~the L3 door's read path still trafficks in
>   `ibValueMetaObjectAttributeBase`, not `ibBackendQueryColumn`~~ — **[SUPERSEDED: the
>   column-based lowering arc LANDED 2026-06-09 (see the Update below). The provider read
>   path carries NO `static_cast` to `ibValueMetaObjectAttributeBase`; materialization +
>   binding derive from `(physical, type)` via `ibColumnCodec::Read/WriteValue`. Thin
>   `Set/GetValueAttribute` adapters remain only for register-lowering callers — not a
>   coupling.]** Still pending: balances/turnovers as DB-backed virtual tables (no RAM
>   round-trip) with role-columns = the **totals-table** arc; the accounting register (subconto
>   unfinished); running the golden tests on the CMake side.
>
> ### Update 2026-06-09 — write-door + reference-as-key (landed, FB-validated)
>
> - **Fluent write door** — descriptors write/delete through the same door, mirroring L2:
>   `From(queryable).SetValue(col,value)*.Insert()/Upsert()/Delete()`; terminals take no
>   args. `WriteRow`/`ForKey`/guid-in-terminal removed. `ibRawDBColumn` (typed static
>   factories `String/Number/Binary/Date/Boolean`) = a direct physical column the provider
>   binds raw (vs attribute decomposition), dispatched on `IsRawColumn()`.
> - **Key model = one authority.** `GetRowKeyColumn` / `IsReferenceAttribute` /
>   `GetReferenceKeyColumn` **removed**. `GetPrimaryKeyColumns()` owns the UPSERT match +
>   dot-walk self-reference (record → data-reference `_RRRef`, register → composite,
>   constant → `RECORD_KEY`); `GetIdentitySort()` owns the read keyset as **real columns**
>   (catalog uuid, no null sentinel). uuid is a rudiment (PK + read/DELETE key) coexisting
>   with `_RRRef` as two link keys until cleaned. (A code-declared index — `_RRRef` unique, a
>   register's dimension key — now reaches EXISTING tables through the normal schema diff: the differ
>   introspects the physical indexes (`ibDialectDictionary::m_indexListQuery` → FB `RDB$INDICES`,
>   SQLite `sqlite_master`, PG `pg_indexes`) and creates only the ones the DB is MISSING, via the
>   normal `CreateIndex` — plain `CREATE INDEX`, since Firebird has no `IF NOT EXISTS`. A UNIQUE index
>   is preceded by a duplicate-key dedup: `ibSchemaBuilder::Execute` keeps one row per key via the
>   dialect's physical `m_rowIdColumn` (`RDB$DB_KEY` / `rowid` / `ctid`). No per-index special path.)
> - **`ibDataResultSource`** (renamed from `ibMetaResultSource`) — backing-blind result;
>   `GetGuidString` removed (the row guid reads as the uuid identity column). The DB result
>   source + provider extracted to `query/dbTableProvider.{h,cpp}`; `ibComputedRegister
>   Queryable` moved into `queryable.h`. Version-lock read moved onto the door (record-locks).
> - **Column-based lowering (LANDED).** Value materialisation + binding are now fully
>   column-based: `ibDbTableProvider::GetValueColumn` / `SetValueColumn` assemble / decompose off
>   the column's `GetTypeDesc()` + a metadata context (`ibBackendQueryable::GetMetaData()`, threaded
>   through the result source / write spec) for reference & enum reconstruction — **no static_cast
>   to `ibValueMetaObjectAttributeBase`** anywhere in the provider. The physical field spread
>   (`WriteFieldsOf`: TYPE + per-contained-primitive + `_RTRef/_RRRef`) is computed straight from
>   the type descriptor, no attribute roundtrip. Thin `Set/GetValueAttribute` adapters (attribute IS
>   a column, metaData = `attr->GetMetaData()`) forward to the core for register-lowering callers.
> - **Temp-DB foundation (LAID 2026-06-09).** The capability seam for DB temporary tables is in:
>   `ibTempTableDialect` + `ibDatabaseLayer::GetTempTableDialect()` (nullable — **presence = the
>   capability**, `nullptr` ⇒ RAM floor). Temp tables are the **optimisation layer** (materialise
>   an intermediate so the DBMS optimiser gets real cardinality), and source-agnostic
>   L3 makes a temp table just an ordinary DB source, so the read path is reused and a runtime
>   failure transparently falls back to RAM. Manager / adapter / planner / per-driver dialects
>   pending — full design contract in [temp-db.md](temp-db.md).
> - **Still open:** the index retrofit above covers dialects that can introspect (FB / SQLite / PG);
>   MySQL / ODBC keep the metadata-only diff until they gain an `m_indexListQuery`. Balances/turnovers
>   as DB-backed virtual tables (totals-table arc); the accounting register (subconto); cross-DBMS
>   validation beyond Firebird.
> - **L4-1 — text query language (in progress, §23):** the greenfield text-query front-end
>   (lexer → parser → lowering → `Query`/`QueryResult` value objects) + a queryable-source
>   factory on `appData`. Written, golden-tested on the front-end, pre-build. L4-2 (LINQ
>   push-down) is designed as a seam only.
>
> ### Update 2026-06-10 — L4 dot-walk + aggregation crown (landed, FB-validated)
>
> The L4 read path matured well past first-run. Realized mechanism in **§23.8 / §23.9**:
> - **Reference dot-walk** — the join chain extracted to a shared `ibRefJoinChain`; typed-empty
>   (not SQL NULL) through an empty / broken reference; a **COMPOSITE reference at ANY segment**,
>   resolved by a recursive branch-per-target walk with a peek optimisation — the headline case is a
>   **register RECORDER** of 15+ document types where the pulled field exists on one
>   (one `LEFT JOIN`, not 15; `COALESCE` across the matching branch(es)). Wired into projection,
>   WHERE (flat + boolean tree), and ORDER BY.
> - **Aggregation over dot-walk** — single-source `GROUP BY Producer.Region` and `SUM(Producer.Weight)`
>   via the same chain (`GroupBy(path)` / `Aggregate(fn, path, alias)`).
> - **Hierarchical TOTALS BY dot-walk** — incl. a self-referential dimension (`Parent.Code`) and
>   **computed / constant measures** (`SUM(1 AS test)`), both via **synthetic scalar columns** owned by
>   the output schema (the metaID-keyed totals fold reads them like real resources).
> - **Const CAST** — a bare projected constant is wrapped `CAST(? AS <type>)` (FB `-804` otherwise).
> - JOIN kinds completed: INNER / LEFT / RIGHT / FULL / CROSS (`ON TRUE`).
> - **Still open (as of THIS update):** ~~TOTALS over a JOIN / UNION (multi-source)~~; ~~a TOTALS BY a
>   reference / composite dot-walk leaf~~; a composite NON-scalar leaf beyond the last segment;
>   ~~arithmetic / CASE as an aggregate input~~; ~~aggregate subqueries~~. All but the composite
>   mid-segment leaf landed in the updates below — struck per this doc's convention.
>
> ### Update 2026-06-11 (2) — L4 executable subset: TOP, computed WHERE / aggregate inputs,
> ### aggregate subqueries, UNION dedup
>
> The "parsed but not yet executed" list shrank. Landed:
>
> - **`SELECT TOP n`** (keyword `Top`) — a row limit on the SELECT core. Lowering:
>   single-source / JOIN reads ride the page request (`m_count`); a UNION's FIRST core limits the
>   WHOLE union (like the trailing ORDER BY), a later branch's TOP limits that branch; a subquery's
>   TOP limits its materialised rows (`ibSubqueryQueryable` ctor). TOP over aggregates / TOTALS
>   errors clearly (the aggregate terminal is unpaged). The rewrite pass refuses to flatten a
>   subquery carrying TOP (the limit would be lost).
> - **Arithmetic / CASE in WHERE** — `WHERE Qty * Price > &V` executes: `ibQueryCondition::m_expr`
>   (a computed lhs) + door verbs `WhereExpr` / `WhereExprCompare`; `BuildConditionExpr` lowers it
>   FIRST (before the null-column row-key branch), so flat conditions, the boolean predicate tree
>   AND the aggregate WHERE all inherit it. Single DB source only (gated: JOIN / computed source).
> - **Arithmetic / CASE as an aggregate input** — `SUM(Qty * Price)` executes:
>   `ibAggregateItem::m_expr` + an `Aggregate(fn, expr, alias)` door overload; `ExecuteAggregate`
>   lowers the input via `BuildColumnExpr`. Same single-DB-source gate.
> - **Aggregate subqueries** — `FROM (SELECT Cat, SUM(x) AS s … GROUP BY Cat)` executes:
>   `ibSubqueryQueryable` detects the aggregate shape from the inner builder (new introspection
>   `GetAggregates` / `GetGroupBy`), exposes the GROUP BY keys + one owned SYNTHETIC numeric column
>   per aggregate alias (id range 0x60000000+), runs `SelectAggregate` in `ComputeRows`, and applies
>   the outer's pushed-down conditions as a RAM POST-filter (they reference post-aggregation output —
>   HAVING semantics, so they must not ride the inner WHERE). `IN (SELECT SUM(…) …)` works through the
>   same wrapper.
> - **UNION vs UNION ALL** — the per-branch flag rides end-to-end: AST `m_unionAll` → door
>   `Union(q, alias, keepDuplicates)` → `ibQueryNode::m_partAll` → the RAM stack dedupes the
>   ACCUMULATED rows at each plain-UNION operator (`ibQueryComposer::DedupeRows`, keyed by the
>   identity hash `GetHashKey` of every output cell — pure, unit-tested; the same core a future RAM
>   DISTINCT will use) and the co-located path emits SQL `UNION` vs `UNION ALL` natively (L2 had both
>   spellings; the provider now picks per flag; `PromoteUnionBranches` carries the flags onto the
>   rebuilt root). SQL left-assoc semantics: `A UNION B UNION ALL C` dedupes after B, keeps C's dups.
> - **Computed-source hardening** — `ibComputedProvider::ExecuteRead` now applies the boolean
>   predicate tree (RAM filter), ORDER BY (RAM stable sort) and the page limit on the materialised
>   rows — previously all three silently dropped on a computed source (subquery / slice). A new
>   `ibComputedProvider::ExecuteAggregate` override computes + RAM-folds (the base default silently
>   returned RAW rows) — this is what makes `SELECT SUM(c) FROM (subquery)` correct. Dot-walk
>   projections / filters / GROUP BY / aggregate inputs over a computed source now ERROR clearly
>   (no DB join to ride) instead of mis-pushing the leaf as a plain column; HAVING over a computed
>   source errors (the RAM fold does not apply HAVING).
>
> **Landed same day (follow-up):**
> - **Paged aggregates** — `SELECT TOP n … GROUP BY` executes: door verb `Top(n)` → spec
>   `m_topCount`; the single-source DB aggregate and the co-located join aggregate render the
>   dialect `LIMIT`, the RAM fold (`RamAggregate` — computed sources and the multi-source stitch)
>   truncates the folded groups (first-seen order).
> - **Composite NON-scalar dot-walk leaf** — `SELECT Recorder.Counterparty` (a composite at ANY
>   segment, the leaf itself a reference / enum / composite) executes: the recursive branch walk
>   (same fork + peek as the scalar case) collects the leaf occurrences, each branch contributes
>   the leaf's FULL field spread, and the spreads merge PER SUFFIX with `COALESCE` under the alias
>   prefix — `GetColumnObject` reassembles the object off the merged spread exactly like a
>   single-target projection (a row matches at most one branch). Suffix alignment rides the
>   representative (first) branch. The previous per-type single-field COALESCE — which the reader
>   could not reassemble — is gone. A dot-walk WHERE / ORDER BY on such a leaf now THROWS
>   (previously the condition / sort key silently DROPPED — wrong rows / wrong order).
>
> **Still open (the deep tail, each its own arc):** ~~TOTALS over a JOIN / UNION (two totals mechanisms
> + snapshot seq-keying)~~ — **[landed 2026-06-28 for the TEXT query language: `ibQueryLowering::ExecuteTotals`
> now builds the JOIN chain / UNION stack exactly like `ExecuteImpl`; the flat `b.Execute` → `ExecuteRead`
> realizes the source (RAM-composed for multi-source), `StampResult` stamps TotalBy, and the runtime folds the
> ONE snapshot — no second totals mechanism. Builds clean (Debug|x86, 2026-06-28); launcher / runtime
> validation pending. Worked example: `SELECT o.Amount, c.Region FROM Document.Sales AS o INNER JOIN
> Catalog.Clients AS c ON o.Client = c.Ref TOTALS SUM(o.Amount) BY c.Region`. **Dot-walk over a JOIN landed
> 2026-06-28** — both the TOTALS dimension (`BY c.Owner.Region`) AND the projection (`SELECT c.Owner.Region`,
> which was silently broken over a JOIN too): `ExpandDotWalkJoins` expands the reference path into explicit
> LEFT-join leaves keyed on `(segment ref col, target self-reference)` — N segments, prefix-deduped (one join
> per shared prefix), shared by `ExecuteTotals` and `PopulateBuilder`. Remaining edges, each an honest `Fail`:
> dot-walk over a UNION (union output is not reference-aware), a dot-walk aggregate input (`SUM(c.Owner.Weight)`)
> / `SelectAggregate` GROUP BY, CROSS / non-equi JOIN, and server-side ROLLUP push-down for multi-source
> (RAM-fold today — the perf, not correctness, tail; ROLLUP for multi-source **landed 2026-07-16(2)** —
> co-located JOIN + UNION, see the update block above).]**
> ~~TOTALS BY a reference / composite dot-walk leaf (scalar today)~~ **[non-scalar leaf landed 2026-06-28: a
> single-target dot-walk path whose LEAF is a reference / composite now rides `ExpandDotWalkJoins` (single-source
> too — adding the ref-join makes it RAM-folded, grouping by the leaf's reference VALUE the scalar synthetic
> could not carry); a composite MID-segment still fails (not a single-target reference)]**, UNION branches
> carrying JOIN / TOTALS, RAM DISTINCT over the stitch (DedupeRows is ready — needs the door wiring), and
> WHERE / ORDER on a composite non-scalar leaf (projection works; the predicate needs per-branch
> DecomposeEquality OR-folded).
>
> ### Update 2026-06-28 (2) — TOP + TOTALS (landed, builds clean Debug|x86, launcher pending)
>
> `SELECT TOP n … TOTALS …` no longer fails. TOP caps the DETAIL rows the fold runs over (the first n,
> 0 = all), applied as the page count on the totals terminal read — the subtotal tree itself is not
> row-limited (a subtotal is not a detail row). The prior `Fail("TOP with TOTALS is not yet supported")` is gone.
>
> ### Update 2026-06-28 (3) — non-equi / theta JOIN (landed, builds clean Debug|x86, launcher pending)
>
> `INNER JOIN … ON a.x > b.y` (and `>= < <= <>`) executes. The L4 join ON, restricted to a single
> comparison `leftCol <op> rightCol`, no longer needs the operator to be `=`. The join node carries an L3
> `ibJoinCompareOp m_onOp` (independent of the L4 AST's `ibQueryCompareOp` — L3 stays L4-agnostic); the
> lowering maps the AST op via `MapJoinOp`. `Eq` keeps the **hash-join** fast path; any inequality runs a
> **RAM nested-loop theta** in `JoinRamTables` (per-pair compare via the same `CompareValue*` as `RamEvalLeaf`,
> SQL three-valued NULL = never matches; OUTER keeps unmatched rows). `FlattenInnerChain` excludes non-equi
> from the smallest-first equi-reorder, and `ColocatableJoinTree` rejects a theta join so EVERY co-located
> decider (join / aggregate / union) folds it in the RAM stitch — no silent `=` render. TOTALS over a theta
> JOIN rides it too (the multi-source totals gate already accepts any keyed join). ~~Still open as a perf
> follow-up: a co-located **server-side** SQL non-equi render~~ — **LANDED 2026-07-16** (see the update
> below): a COLUMN-KEYED theta renders server-side (`allColumnKeyed` + `JoinOpToBinOp`), and a computed
> `a.x + 1 > b.y` no longer fails at all — it lowers to `ibJoinOn::m_exprL/m_exprR` and runs the RAM
> nested-loop theta.
>
> ### Update 2026-07-16 — theta JOIN server-side push-down (the co-located follow-up, landed)
>
> The perf follow-up above is closed for the common case. `ColocatableJoinTree` no longer rejects a
> column-to-column theta join: the gate (renamed `allColumnKeyed`) now rejects ONLY a **computed** ON
> (`m_on.m_exprL != nullptr`), so a plain `a.x <op> b.y` inequality is co-locatable. `BuildColocatedFrom`
> renders the join's real operator via `JoinOpToBinOp` (a 1:1 `ibJoinCompareOp → ibQueryBinOp` map — both
> enums list Eq/Ne/Lt/Le/Gt/Ge in the same order) instead of the forced `=`, so a same-DB co-located theta
> JOIN pushes down to SQL (`ON a.period <= :d`, the balance-on-date shape) and the DB serves it by index —
> no RAM stitch. Test: `QueryComposerGate.Join_ColumnTheta_Colocatable`. Still RAM-folds (correctly): a
> **computed** ON (`a.x + 1 > b.y`) and any **cross-DB** join — co-location is a same-source property, not
> "always". `JoinRamTables` remains the fallback for both.
>
> ### Update 2026-07-16 (2) — multi-source TOTALS ROLLUP push-down (the last totals tail, landed)
>
> `ExecuteTotals` pushed `GROUP BY ROLLUP` server-side only for a **single source**; a hierarchical
> TOTALS over a co-located JOIN / UNION always RAM-folded (`Compose` → `BuildTotalsTree`) even though the
> flat GROUP BY over the same JOIN already co-located. That tail is closed:
> - **Shared core `RunRollupTotals(spec, from, colExpr)`** — the projection (`g<i>` / `GROUPING(g<i>)` /
>   aggregates), the GROUPING-level read and the `ibSelectorTree` assembly now live in ONE place; the
>   single-source push, the co-located JOIN push and the UNION push differ only in `from` + how a column is
>   qualified (no duplicated read/tree code).
> - **JOIN** — `ExecuteColocatedRollupTotals` runs ROLLUP over `BuildColocatedFrom` (columns qualified by
>   their owning leaf's table), WHERE via the same `ColocatedWhere` — so an RLS **semi-join rides
>   server-side** on the totals too (`BuildColocatedPredicate` renders the correlated EXISTS).
>   `CanColocateRollupTotals` gates it (colocatable join tree, SCALAR **or REFERENCE** group levels — a
>   reference groups by its spread as ONE composite `ROLLUP((f0,f1,…))` element, reassembled on read — scalar
>   aggregate inputs). Single-source: `CanRollupTotalsShape` likewise takes reference levels AND a resolvable
>   dot-walk group level (via a reference-join chain in `ExecuteRollupTotals`); dot-walk aggregate inputs stay RAM.
> - **UNION** — `BuildUnionRollupFrom` projects each branch's referenced columns under inner aliases
>   (`k<n>`), UNION[/ALL]-stacks them and wraps the result as a subquery `u`; the outer ROLLUP folds over
>   `u.k<n>`. `CanColocateUnionRollupShape` requires every referenced column to resolve BY NAME + SCALAR in
>   every branch, and **gates OFF when a boolean WHERE tree / RLS `m_predicate` is present** (the union
>   branch path renders only flat conditions) → RAM, which applies it — never an under-restricted read.
> - **Testability** — the single-source gate was untested (shape + dialect conflated). Both sides now
>   expose the STRUCTURAL half (`CanRollupTotalsShape` / `CanColocateRollupTotals`) with no dialect probe,
>   so the routing is unit-tested without a DB: `QueryComposerGate.RollupTotals_*` (JOIN scalar, single
>   source, computed leaf, no-levels, UNION scalar, UNION computed-branch, UNION-with-predicate, single
>   shape, multi-source, single computed). The server-side SQL execution + the GROUPING-level parse stay
>   integration scope (a ROLLUP-capable DB — FB5 / PG / MySQL8; SQLite has no ROLLUP → correct RAM fold,
>   not a gap).
>
> ### Update 2026-07-16 (3) — co-located UNION read renders the full WHERE (RLS + boolean tree through the door)
>
> The one server-side terminal that did NOT go through the unified WHERE door: `ExecuteColocatedUnion`
> rendered only the flat `m_conditions` per branch and silently DROPPED `spec.m_predicate` — the boolean
> WHERE tree (OR / NOT / IS NULL) AND the RLS semi-join. A boolean WHERE over a co-located union returned
> too many rows; an RLS restriction (folded into `m_predicate` by `AddSemiJoin`) leaked. Closed by pushing
> the predicate INTO EACH branch — a union's branches are parallel, so (unlike a JOIN's one qualified
> expression over the joined row) the same predicate is rendered against every branch with columns
> resolved BY NAME: new `BuildBranchPredicate` mirrors `BuildColocatedPredicate` for a single by-name
> source and re-correlates an RLS semi-join's outer key to each branch; `CanColocateUnion` co-locates only
> when `UnionPredicateColocatable` confirms every referenced column resolves BY NAME + SCALAR in every
> branch (a dot-walk / computed leaf -> RAM, which applies it). RLS now rides the UNION read server-side,
> not just the JOIN. Tests: `QueryComposerGate.Union_{BooleanPredicate,SemiJoinPredicate}_Colocatable` +
> `Union_{PredicateColMissingInBranch,DotWalkPredicate}_NotColocatable`. (The seam the "one door" thesis
> predicts: the lone gap sat exactly on the lone terminal that bypassed the shared door.)
>
> ### Update 2026-06-28 (4) — named ref-join `JOIN o.Customer AS Cust1` (landed, builds clean Debug|x86, launcher pending)
>
> A reference dot-walk can now be DECLARED once and reused: `JOIN rootAlias.refA[.refB…] AS alias` auto-joins
> the reference chain off an existing source and binds the FINAL target to `alias`, so later `alias.field AS x`
> is a clean qualified column — no more the ugly auto-name a dotted projection produces (`Cust1.Region` →
> `Cust1Region`), and one join shared by every `alias.*`. NO parser change: the grammar already accepts
> `JOIN <dottedName> AS <alias>` with no ON; the lowering disambiguates — if the first segment is a LIVE
> source alias (not a metaobject namespace) and there is no ON, it is a ref-path join. New helper
> `ExpandRefJoinAlias` (queryLowering anon ns) walks the segments (each a single-target reference), LEFT-joins
> each target keyed on `(segment ref col, target self-reference)` — intermediate targets get synthetic
> aliases, the last gets the user's — and pushes the final target into `sources`. Wired into BOTH join loops
> (`ExecuteImpl` + `ExecuteTotals`); `MapJoinKind` extracted to kill the duplicated kind ternary. Example:
> `SELECT Cust1.Region AS reg1 FROM Document.Sales AS o JOIN o.Customer AS Cust1 TOTALS SUM(o.Amount) BY Cust1.City`.
> Edge: a composite (multi-type) reference segment fails inside the expand (not a single-target reference);
> the alias must be explicit (`AS`).
>
> ### Update 2026-06-28 (5) — DISTINCT over the stitch + dot-walk aggregate / GROUP BY over a JOIN (landed, builds clean Debug|x86, launcher pending)
>
> Two remaining functional edges closed:
> - **SELECT DISTINCT over a multi-source compose** — the single-DB path renders SQL `DISTINCT`, but the RAM
>   stitch had none. `ProjectToAliases` now dedups by the FULL output row (selectCols + computed exprs) BEFORE
>   the page limit (so it yields up to `limit` DISTINCT rows); `ibComputedProvider::ExecuteRead` dedups by the
>   output columns while keeping all columns (the sort may key on one outside the select list). UNION DISTINCT
>   stays its own fold at the UNION operator.
> - **dot-walk in an aggregate input (`SUM(c.Owner.Weight)`) and a GROUP BY key (`c.Owner.Region`) over a JOIN**
>   — reuse `ExpandDotWalkJoins` (as the TOTALS dimension does): expand the reference path into LEFT-join leaves
>   and aggregate / group by the qualified leaf, shared dedup across the projection, the aggregate input and the
>   GROUP BY clause. A computed source (subquery / slice) still honest-fails (no DB table to ref-join).
>
> ### Update 2026-06-28 (6) — computed ON in a JOIN (`a.x + 1 > b.y`) (landed, builds clean Debug|x86, launcher pending)
>
> A JOIN ON was restricted to a column-to-column comparison; now either side may be a COMPUTED expression
> (arithmetic / CASE). The join node carries `ibQueryColumnExprPtr m_onExprL/R`; the lowering builds them via
> `BuildColumnExprFromAst` when either side `IsComputedExprAst`. `JoinRamTables` evaluates each side per pair
> in the theta nested-loop via `EvalColumnExprRow` — the LHS over the left table, the RHS over the right
> (lhs→left / rhs→right ordering assumed; SQL three-valued NULL = never matches). A computed ON always RAM-folds
> (the equi hash route is only for plain key columns) — `AllJoinsHaveKeys` accepts an expr-ON node as keyed,
> `FlattenInnerChain` excludes it from the equi reorder, and `ColocatableJoinTree` rejects it (no server-side
> render). Edges: the comparison is `leftExpr <op> rightExpr` (single comparison), no dot-walk inside the
> expression (`BuildColumnExprFromAst` takes plain columns), and the natural lhs→left / rhs→right side order.
>
> ### Update 2026-06-28 (7) — ambiguous-column / duplicate-alias diagnostics (landed, builds clean Debug|x86, launcher pending)
>
> A bare (unqualified) column over a JOIN used to silently resolve to the FIRST source that owned it — a
> silent wrong-source pick. `OwnerOfBareColumn` now walks ALL sources and Fails on ambiguity
> (`ambiguous attribute 'x': it is in more than one source — qualify it with an alias`), wired into the three
> bare-name resolvers (`ResolveColumnSingle`, `ResolvePath`, `RootForPath`); a qualified `a.col` stays
> unambiguous via `SourceForAlias`. `RequireAliasFree` rejects a duplicate FROM/JOIN alias
> (`duplicate source alias 'x'`) in both join loops — synthetic dot-walk / ref-join aliases (`_dw`, `_rj`) are
> unique by construction, so only user aliases collide. Behaviour change: a query that relied on the implicit
> first-source pick now errors (correct SQL semantics).
>
> ### Update 2026-06-28 (8) — L3/L4 unification pass (refactor, no behavior change, builds clean Debug|x86)
>
> After the JOIN / TOTALS feature run, a refactor sweep folded the accumulated duplication:
> - **`BuildSourceTree`** (queryLowering anon ns) — the FROM + JOIN source-tree build (named ref-join / cross /
>   comparison ON / computed ON / auto-join) was duplicated nearly line-for-line in `ExecuteImpl` and
>   `ExecuteTotals`; now ONE helper both call (~85 lines gone — a new JOIN feature lands in one place).
> - **`CanColocateBase`** (dbTableProvider) — the shared co-location preconditions (colocatable join tree, no
>   dot-walk / key-in, single-field keys) factored out of `CanColocateJoin` / `CanColocateAggregate`.
> - **`ibJoinOn`** (dataQueryBuilder.h) — the six scattered join-ON fields on `ibQueryNode`
>   (`m_onLeft/m_onRight/m_onOp/m_onExprL/m_onExprR/m_cross`) grouped into one cohesive struct `m_on`.
> - **`ibDataQueryBuilder::JoinNode`** — the per-overload node-building dance (3 bodies) folded into one
>   private point; the public `Join` / `CrossJoin` overloads are thin adapters that assemble an `ibJoinOn`.
>
> Follow-up (landed): **`JoinRamTables`** now takes the ON op + computed exprs as one `const ibJoinOn& on`
> (the resolved keys still ride `onLeft`/`onRight`), and **`RamTableOf` / `AppendRowByCols`** fold the
> structure-init + row-copy idiom that `ibComputedProvider::ExecuteRead` repeated three times (DISTINCT / sort
> / limit).
>
> Evaluated and DELIBERATELY NOT done (churn >> benefit on implementation inspection): an `ibJoinOn` kind-enum
> (pure gilding — the field inference already works, an enum adds zero behaviour/safety), and folding the
> parallel `m_groupBy`/`m_groupPaths` + `ibAggregateItem` `m_col/m_path/m_expr` into structs — the latter needs
> ~15-20 context-sensitive edits (a blind `.m_col` rename is unsafe: `.m_col` lives on ibQueryCondition /
> HavingItem / sort items too) for a single contained sync-risk site. The duplication worth removing is
> removed; the rest is left as-is.
>
> ### Update 2026-06-11 — L4 optimizer pass (landed, 420/420 green, runtime-validated)
>
> The optimizer ladder's first two rungs are in the tree. The stance stays: **no cost-based
> optimizer of our own** — single-source tactics are delegated to the DBMS (temp-db.md §1);
> OES optimizes only what the DBMS cannot see.
>
> - **AST rewrite pass** — `query/queryRewrite.{h,cpp}`, wired into BOTH lowering entries
>   (`ibQueryLowering::Execute` / `ExecuteTotals`), so the text front-end now and LINQ
>   push-down later inherit every rule. Pure AST → AST on a deep clone (the Query value
>   object's cached parse is never mutated). Rules:
>   - *Negation normalization* — `NOT` pushed down until absorbed: comparison inversion
>     (`NOT (a = b)` → `a <> b`), double-NOT elimination, De Morgan, the negated-flag
>     toggle on LIKE/IN/IS NULL/BETWEEN, truthy `NOT col` → `col = FALSE` (exact under
>     typed-empty semantics). Payoff: a Not-free WHERE far more often passes
>     `IsFlatAndWhere` → rides the door's verb conditions (the path that works across
>     JOINs and the RAM stitch, where the predicate tree is still restricted).
>   - *FROM-subquery flattening* — `FROM (SELECT … WHERE p) AS s WHERE q` merges into one
>     SELECT (`WHERE q' AND p`, output names substituted back to inner paths; an aliased
>     dot-walk projection re-expands). Conservative gates: plain inner projection only (no
>     aggregates / DISTINCT / JOIN / UNION / TOTALS / ORDER BY), single-source outer, and a
>     SCOPE GUARD — an outer reference to a column the subquery does not project keeps the
>     wrapped path (and its honest resolution error) instead of silently legalizing
>     against the real table. Nested levels collapse bottom-up.
> - **Smallest-first join order in the RAM stitch** — `ibQueryComposer::PlanInnerJoinOrder`
>   (pure, unit-testable) + `FlattenInnerChain` / `JoinUnitsSmallestFirst` in
>   `queryProvider.cpp`: a pure-INNER chain of 3+ units re-joins greedily smallest-first
>   using the EXACT materialised row counts (the units are materialised anyway — real
>   costs, no estimator); intermediates shrink. Non-inner kinds (LEFT/RIGHT/FULL/CROSS)
>   stay opaque order-preserving units; any anomaly falls back to the tree order —
>   correctness never depends on the reorder.
> - **Temp decision tidied** — the pre-manager `ChooseMaterialisation` seam retired; the
>   decision is split where its inputs live: `WorthDbTemp(rowCount)` SHOULD-gate at the
>   promote sites, the CAN-gate (presence + probe + fallback) inside
>   `ibTempTableManager::Materialise` (temp-db.md §9 updated to the landed state).
> - **Tests:** `test_queryRewrite.cpp` (16 — parse → rewrite → tree shape, incl. the
>   original-AST-untouched guarantee) + `QueryComposerPlan` (6 — chain / star /
>   disconnected / malformed / tie-break determinism). Full `oes_tests` 420/420; full
>   `Debug|x86` clean; launcher + live queries validated. (The previously documented x64
>   gtest AV in `QueryL4Parser` no longer reproduces — the suite is green.)
> - **Next rungs (by need, not now):** DB-leaf cardinality estimates feed the planner's
>   second argument once federation / 3+-source loads exist (wants the PG stand); further
>   rewrite rules (IN → semi-join, unused-column pruning) are one function each in the
>   existing pass.
>
> ### Update 2026-06-11 (3) — L4-2 LINQ push-down v1 (landed, 444/444, runtime-validated)
>
> The second front of L4 is in the tree: a lambda chain (and the block form) over a DB source
> executes as SQL through the same L3 door the text language uses. The two parallel lines —
> the RAM LINQ surface (`OPER_CALL_LINQ` + `DispatchLinqMethod`) and the backing-blind L3/L4
> engine — met through a ~250-line adapter; none of the 32 existing RAM pipeline ops
> changed (this arc added the 33rd, `ToTable`).
> Realized mechanism in **§23.5**:
>
> - **Lambda recorder** (`compiler/lambdaQueryAst.{h,cpp}`) — at compile time the
>   single-parameter lambda body's LEXEMES re-parse into a ready L4-1 `ibQueryAstExpr`
>   (Column / Literal / Param / Arith / Compare / Logical / Not). Conservative bail-out:
>   anything outside the subset records `null` → the RAM path (a false "translatable"
>   would mean wrong rows). Logical operators are the `And`/`Or`/`Not` KEYWORDS in both
>   code styles (`&&` / `||` do not exist in the language — a bare `|` is the lexer's
>   string-continuation marker); paired compares (`<=`, `>=`, `<>`, `!=`, `==`) fuse only
>   when source-adjacent.
> - **AOT v14** — the recorded AST persists on `ibByteFunction` (presence byte + node
>   payload, write-gated by `AstSerializable`, read-validated with a depth cap); an AOT
>   cache hit KEEPS the push-down (a hit is the production norm, so the feature survives it).
> - **`ibValueQueryable`** — inert by contract (no property/method surface; watch/`GetString`
>   never executes); `DispatchLinqMethod` is the only entry. `Where` lowers the lambda AST
>   through the SAME `BuildWherePredicate` the text language uses (captured outer locals =
>   named Params resolved from the captured frames at dispatch); `OrderBy[Descending]` lowers
>   a column path; `Take` min-folds; terminals `Count`/`Any`/`First[OrDefault]`/`ToArray`/
>   `ToTable` execute through the door; ANY other op materialises the folded prefix and
>   re-dispatches on the stock RAM machinery (the RAM floor). Each op clones the link —
>   the chain is immutable. Rows: single-PK source → references; otherwise (registers,
>   `Data.From`) a structure of columns — register iteration unlocked.
> - **The `Data` global** — the queryable-source root, an exact mirror of the `Metadata`
>   unit: nine queryable kind namespaces (`Data.Catalogs.X` …) vending leaves through
>   `ibBackendQueryableHolder::GetQueryable()` — the SAME source registry the text language
>   reads; `Data.From(valueTable)` wraps the existing `ibTempTableQueryable` (an in-memory
>   table as a first-class source). Block LINQ `From s in Data.Catalogs.X select {…}`
>   validated live.
> - **`ToTable`** — a new pipeline terminal materialising a Queryable into a value table;
>   a clear error on plain iterables.
> - **Tests:** `test_lambdaRecorder.cpp` (16 — text → lexemes → AST shape + bail-outs, no DB).
>   Full `oes_tests` 444/444; full `Debug|x86` clean; block-LINQ runtime smoke green.
> - **Open (phase D):** golden parity RAM-vs-pushed. Deferred post-v1: manager-as-LINQ sugar
>   (`Catalogs.X.Where(λ)` hitting the DB), source arguments in the text language, rooted
>   `FROM Data.*`, plural factory aliases, `Metadata.*` as queryable relations.
>
> Design history + open decisions remain in [§15](#15-open-decisions); the per-section
> design (§4–§22) is preserved as the rationale this converged from.

---

## 1. Motivation

Two facts about the current tree drive this arc:

1. **LINQ executes in RAM, not in SQL.** `compiler/procUnitLinq.cpp`
   materialises the source through iterator states and filters / sorts /
   joins in C++ (`OrderBy` → `std::stable_sort`, `GroupBy` → `std::map`
   buckets, `Join` → in-memory hash index). There is no SQL push-down.
   `from o in Documents.Orders where o.Total > 10000` pulls the **whole
   table** into memory and filters there.

2. **Dialect logic is smeared as inline branches.** SQL is hand-assembled
   per call site with per-driver `if`s — e.g. the old
   `metaCollection/partial/list/listSqlBuilder.cpp:14`
   (`if FIREBIRD → "SELECT FIRST N" else "LIMIT N"`; that file and its
   `ibListSqlBuilder` are gone from the tree — the migration below happened),
   plus `BYTEA` vs `BLOB`,
   `DATE` vs `TIMESTAMP` scattered elsewhere. There is no canonical query
   representation and no single translation layer.

The goal of this arc — the "crown" — is a query language built on a clean
three-tier split, with **no dialect forks in business code** and **LINQ (and
later a a text query language) pushed down to the database**.

---

## 2. The three tiers

```
   L3  Metadata surface  ── LINQ + a text query language
       (placeholder, §14)    resolves metadata → physical, builds the IR
            │
            ▼  ibQueryIR (structured, physical table/column names)
   L2  Unifying layer   ── ONE class. Universal query in, normalized result out.
       (this document)     Dialect hidden in the drivers. Starts from a holder.
            │
            ▼  borrows the holder's connection / transaction
   L1  Physical tier    ── ibDatabaseLayer ×5 + holder + pool + transaction.
       (exists, §3)        Reserves and pins the connection.
```

| Tier | Status | Currency | Owns |
|---|---|---|---|
| L1 | exists | dialect SQL text | the connection + transaction |
| L2 | **this arc** | `ibQueryIR` (structured) | nothing — borrows L1 |
| L3 | placeholder | LINQ / text-query | metadata → physical mapping |

---

## 3. Level 1 recap — the floor L2 sits on

L2 adds no new transaction or connection machinery; it rides L1. The relevant
L1 surface (already in the tree):

- **`ibDatabaseConnectionHolder`** (`databaseLayer/connectionHolder.h`) — an
  identity tag the pool keys reservations on. A session owns one holder;
  `db_query` (`appData.h:22`) callers use a per-thread single holder,
  `ses_query` (`session/session.h:903`) routes through the session's holder.
- **`ibConnectionScope`** (`databaseLayer/connectionScope.h`) — RAII gateway.
  Nested scopes for the same holder inherit the parent's connection; only the
  outermost touches the pool. `SafeBeginTransaction / SafeCommitTransaction /
  SafeRollBackTransaction`; the destructor auto-rolls-back an unresolved
  transaction.
- **`ibDatabaseLayer`** (`databaseLayer/databaseLayer.h`) — abstract driver,
  five implementations. Nested transactions collapse to one real driver TX via
  an `m_txDepth` counter + `m_txAborted` poison flag (inner rollback poisons
  the outer commit). `IsBusy()` is true while result sets / statements are
  live; the pool will not hand out a busy connection.
- **`ibPreparedStatement`** (`databaseLayer/preparedStatement.h`) —
  `SetParamInt/Number/String/Date/Bool/Blob(pos, value)` (1-based),
  `RunQuery()` / `RunQueryWithResults()`.
- **`ibDatabaseResultSet`** (`databaseLayer/databaseResultSet.h`) — `Next()`,
  `GetResultInt/String/Number/Date/Bool(field)`, `IsFieldNull(field)`.
- **`ibConnectionPool`** (`databaseLayer/connectionPool.h`) — per-entry record
  `ibConnectionEntry { conn, txHolder, scopeHolder, lastUsed, startedAt,
  inUse, noWait }`. Reservation primitives `ReserveTx / GetReservedTx /
  BindScopeHolder / GetScopeConn / Checkout / Return`.
- **`ibResultSetGuard` / `ibStatementGuard`** (`databaseLayer/databaseLayer.h`
  ~432) — RAII wrappers for the raw result-set / statement pointers. L2 makes
  these mandatory, not optional.

---

## 4. Level 2 — identity and entry

### Tier names (final, 2026-06-06)

All three tier flagships share the `ibDatabase*` prefix — type `ibDatabase`
and the whole stack is in front of you. The bare `ibQuery` family is reserved
for L2's **internal machinery** (IR, renderer, dialect), not a tier door.

| Tier | Class | Role | One word |
|---|---|---|---|
| L3 | `ibDatabaseQuery` | what the developer **writes** (LINQ / a text query); builds IR; lives in the **metadata** world | Query |
| L2 | `ibDatabaseQueryBuilder` | **how** it runs: fluent build + terminal execute, dialect-indifferent | Builder |
| L1 | `ibDatabaseLayer` | **where** it runs: the physical driver (×5) | Layer |

Reads as a sentence: *a **database query** (L3) is run by the **database query
builder** (L2) over the **database layer** (L1).*

> **Rename (2026-06-07):** the REALIZED L3 read door is **`ibDataQueryBuilder`**
> (`query/dataQueryBuilder.{h,cpp}`), renamed from `ibMetaQueryBuilder` — it reads through
> the `ibBackendQueryable` abstraction (a source may be a temp table / computed relation),
> so it is NOT metadata-bound and the "Meta" name misled. `ibDatabaseQuery` in the table
> above stays reserved for the future LINQ / a text query **text** door that lowers INTO
> `ibDataQueryBuilder`. Ladder: *data query builder (L3) → database query builder (L2) →
> database layer (L1)*.
>
> Prose in §5/§9/§12/§16/§17 below still says `ibQuery` for the door — that is
> the old name for `ibDatabaseQueryBuilder` (prose sweep pending).

### The L2 door

**L2 is a unified database layer: one class, not five.** It is a fluent builder
*and* executor in one object (jOOQ / QtSql style) — building and running are
methods of the same class, the terminal op executes. It mirrors the L1 verbs
(`Begin / Commit / Rollback / Execute`) so a caller who knows L1 is immediately
at home. The currency is a structured `ibQueryIR` in, a normalized result out.

It **starts from an existing holder** and **borrows** it — L2 never creates a
connection or a transaction; their lifetime stays with the session / L1.

```cpp
// Sketch, not final signatures.
class ibDatabaseQueryBuilder {
public:
    ibDatabaseQueryBuilder();                                       // current session holder
    explicit ibDatabaseQueryBuilder(ibDatabaseConnectionHolder*);   // explicit holder
    // move-only (see §13)

    // fluent DQL construction — physical names only (metadata→physical is L3)
    ibDatabaseQueryBuilder& From(const wxString& table);
    ibDatabaseQueryBuilder& Select(std::vector<wxString> columns);  // empty = SELECT *
    ibDatabaseQueryBuilder& Where(ibQueryExprPtr predicate);        // AND-folded
    ibDatabaseQueryBuilder& OrderBy(const wxString& column, ibQuerySortDir = Asc);
    ibDatabaseQueryBuilder& Limit(long count, long offset = 0);

    // two terminals
    ibQueryIR     Build() const;                                    // pure: IR, no connection (test seam)
    [[nodiscard]] ibQueryResult Execute(const std::vector<ibValue>& params = {});

    // direct paths: prebuilt IR (the L3 hand-off), DDL, DML
    [[nodiscard]] ibQueryResult ExecuteIR(const ibQueryIR&, const std::vector<ibValue>& = {});
    int Execute(const ibDdlStatement&);
    int Execute(const ibDmlStatement&, const std::vector<ibValue>& = {});

    void BeginTransaction(const ibDatabaseLayer::ibTxOptions& = {});
    void Commit();
    void RollBack();
};

class ibQueryResult {                   // a cursor, like ibDatabaseResultSet
public:
    bool    Next();
    ibValue GetValue(int column);       // already-normalized ibValue
    ibValue GetValue(const wxString& column);
    // holds a shared_ptr to its connection for its whole lifetime — see §9
};
```

Caller rhythm — fluent build, terminal execute:

```cpp
ibDatabaseQueryBuilder q;            // current session holder
q.BeginTransaction();
ibQueryResult r = q.From("Reference17")
                   .Where(ibBinOp(ibQueryBinOp::Eq, ibCol("Code_S"), ibParam(0)))
                   .OrderBy("Code_S")
                   .Limit(50)
                   .Execute({ codeValue });
while (r.Next()) { ... r.GetValue("Code_S") ... }
q.Commit();
```

`Build()` is the seam that keeps the builder honest: it *builds* (returns the
`ibQueryIR`, no connection needed — the golden-test entry) and the same object
*executes* (`Execute` renders + runs). One class, both axes.

**Per-driver behaviour is not in L2.** L2 is single and dialect-blind; the
dialect arrives "from below" through the holder's connected `ibDatabaseLayer`
(the driver), resolved into an `ibDialectDictionary` (§6). Whatever driver sits
in the holder decides the SQL; the caller sees zero forks.

The transaction verbs delegate to `ibConnectionScope`
(`SafeBeginTransaction` etc.) — no new TX machinery.

---

## 5. Input contract — the IR

`Execute` takes a structured **`ibQueryIR`** (a relational-algebra tree),
**never a raw SQL string**. This is the load-bearing decision: if L2 accepted
SQL text it would have to parse it back into structure to retarget the
dialect — wasteful, since L3 already has structure. The only sanctioned raw
SQL is the `NativeFragment` node (§8).

**MVP node set:** `Scan / Filter / Project / Sort / Limit`.
**Phase 2:** `Join / Aggregate (GroupBy) / Distinct / Union`.

**Beyond DQL — DML and DDL families.** The IR above is query-only (SELECT).
Migrating the descriptor / DDL call sites (§17) needs two further IR families
through the *same* `ibQuery` door:
- **DML** — `Insert / Update / Delete` (the record write path).
- **DDL** — `CreateTable / AlterTable / DropTable / AddColumn`. DDL is where the
  Dialect Dictionary's **type-map** finally does its work (`MapType`,
  `boolForm`): the worst per-DBMS forks live here — `BLOB` vs `BYTEA`,
  `DATE` vs `TIMESTAMP`, boolean-as-`SMALLINT` on Firebird. The dictionary
  closes them exactly as it closed `FIRST` vs `LIMIT`. Prime migration targets:
  `accumulationRegisterQuery.cpp` (CreateAndUpdate{Balances,Turnover}TableDB),
  `metaAttributeObjectQuery.cpp` (composite-field column DDL).

  > **Both targets are gone from the tree** (verified 2026-08-08): the migration this paragraph
  > proposed happened, and the DDL it names is rendered by L2 today. Kept as the record of what
  > the ladder was built to replace — do not go looking for the files.
The door, holder binding, dictionary-driven render, RAII and error model are
already built and reused; only the node sets + their render branches + the DDL
type-map dictionary fields are added.

Expressions inside `Filter` / `Project`:
`Col(name) · Const · Param(id) · BinOp(+,-,>,=,AND,…) · Func(name, args)`.

**Pure-translator stance (provisional — see §15.1).** L2 is **metadata-blind**:
it receives *physical* table and column names already resolved by L3
(`Document17`, `Total_N`, `Date_D`). All the "metadata magic" — composite field
naming (`_B/_N/_D/_S/_E/_R`), reference auto-joins
(`Item.Supplier.Name` → JOIN chain), register virtual tables (Balances /
Turnovers) — lives in the L3 binder. This keeps L2 a thin, testable
translator: "give me a physical relational IR, I return correct SQL for the
connected driver and execute it."

Every IR node carries an optional **source-provenance span** back to the L3
text (used by the error model, §10).

---

## 6. Rendering — a Dialect Dictionary drives one generic renderer

L2 is **DBMS-indifferent**. It does not know or care whether Firebird or
PostgreSQL is underneath — all dialect knowledge is supplied to it as a
**Dialect Dictionary** that the driver provides at L1. L2 has **one generic
renderer**, fully driven by that dictionary. Adding a new DBMS means supplying
a new dictionary, **not** editing L2 and **not** scattering
`if FIREBIRD … else PG …` patches across the code. This is the heart of the
arc: L2 **replaces** the per-DBMS `ibDatabaseLayer` SQL-building with a single
entry point — same parameters, same calling code, no per-descriptor patching —
and the dictionary closes the difference.

The dictionary is **data-first**: the large majority of divergence is
declarative and closes with plain fields.

```cpp
struct ibDialectDictionary {            // provided by the L1 driver
    // --- declarative facts (close the large majority) ---
    ibParamStyle  paramStyle;           // ? | $N | :name
    ibPagination  pagination;           // FIRST/SKIP | LIMIT/OFFSET | OFFSET..FETCH | TOP
    ibBoolForm    boolForm;             // TRUE/FALSE | 1/0 | SMALLINT
    /* identifier quoting, type map (BLOB/BYTEA, DATE/TIMESTAMP, decimal),
       supported features (window? cte? fullOuter? ilike?), reserved words … */

    // --- behaviour slots (the small tail that data cannot express) ---
    // emulation rewrites for missing features, e.g. FULL OUTER → LEFT UNION RIGHT
    ibRewrite     emulate[/* by feature */];
};
```

The generic renderer walks the IR and consults the dictionary at each node —
"how do you paginate", "how is a boolean spelled", "do you support windows".
Whether the answer comes from Firebird's or PG's dictionary is irrelevant to
the renderer: **the dictionary closes everything.** Most of it is pure data;
the small remainder that data cannot express — emulation *rewrites*, which are
behaviour, not a value — sits in the dictionary as strategy slots. L2 reads
only the dictionary.

The existing inline dialect branches (`listSqlBuilder.cpp:14`, `FIRST` vs
`LIMIT`, …) are migrated **into the dictionary** and vanish from business code.

**"Identical behaviour" strategy (provisional — see §15.2).** Drivers differ
in capability (SQLite has no `FULL OUTER JOIN`; old Firebird has no window
functions; PG has `ILIKE`, FB does not). The dictionary classifies each:

- **Floor** — the minimum capability a dictionary must declare to connect at
  all.
- **Emulate** — for base operations, a dictionary rewrite makes behaviour
  genuinely match (`FULL OUTER` → `LEFT UNION RIGHT`, etc.).
- **RAM-fallback** — for exotic operations a driver cannot do and that are
  expensive to emulate (e.g. window functions for register virtual tables on
  engines without them), the dictionary declares "unsupported" and the residual
  runs in RAM (the same residual mechanism the LINQ path uses).

The contract is **not** "one SQL for all" but **"L2 guarantees the same
result; the dictionary tells it how to obtain it per DBMS."**

---

## 7. Execution path

| Step | Where | Why |
|---|---|---|
| IR → SQL text | **driver** (hooks over the shared renderer) | the only genuinely divergent part |
| prepare / bind params / run | **L2 common path** | `ibPreparedStatement.SetParam*` is already uniform |
| read result set | **L2 common path** | `GetResult*` is already uniform |
| result type normalization | **driver** | only the FB driver knows `SMALLINT → bool`, its dates, decimals, blobs |

Only what is genuinely the driver's — rendering and result-type
normalization — moves into the driver. Everything else (execution, reading,
transaction through the holder) is the shared L2 path over L1.

**Parameters flow structurally** (IR param-nodes → a param plan →
`SetParam*`). SQL text is never concatenated with user values → **SQL
injection is impossible by construction.**

**Output** is an `ibQueryResult` cursor yielding normalized `ibValue` rows,
identical regardless of the underlying driver.

---

## 8. Escape hatch — `NativeFragment` (post-MVP)

An **optional** language extension: a way to drop driver-specific raw SQL into
a query, for compatibility or debugging. It is a sanctioned, loud, opt-in,
quarantined exception to the "no dialect forks" rule — not a back door.

It lowers to one more IR node:

```
NativeFragment {
    dialect   : "PG" | "FB" | "MSSQL" | ...     // which driver this is for
    rawSql    : "... raw dialect fragment ..."
    params    : [:p1, :p2]                        // still structural — no injection
    fallback  : <portable IR node>?               // what to do on other drivers
}
```

It may sit anywhere a relation or a scalar expression sits (a whole sub-SELECT
or a single `WHERE` term). The driver renderer handles it:

- `dialect == this driver` → emit `rawSql` verbatim, params still bound
  structurally;
- otherwise → use `fallback`; if absent → a clear "no variant for this driver"
  error.

**Discipline:**
- Loud, opt-in syntax (`Native(...)` / `Dialect { ... }`), not a silent
  `if driver ==`.
- A query containing a `NativeFragment` without full per-dialect coverage /
  fallback is flagged **non-portable** at query-compile time.
- Behaviour on a non-matching driver (provisional): a `default` / `else` is
  **near-mandatory** → silent fallback; without it → an explicit error.
  No silent no-op.
- Doubles as a **diagnostic seam**: force a raw SQL to compare against the
  generated one, or work around a driver bug temporarily.

Post-MVP: the clean universal path ships first; the escape hatch is added once
the base slice works, so it never becomes a crutch before the thing it exists
to back up.

---

## 9. Connection lifecycle — flicker and pinning

`ibQuery` (L2) outlives a single connection reservation. Between two `Execute`
calls the holder may release connection A; the next `Execute` may re-acquire a
**different** connection B from the pool. This is safe **only** when nothing
carries cross-statement state.

**Connection identity is stable only while something pins it:**

```
No active TX, no open cursor   → not pinned. L2 may release between Executes;
                                 the next Execute may get a different connection
                                 — fine, each statement is autonomous (autocommit).

Active TX / open cursor /       → pinned. The pool MUST return the same physical
temp table / row lock            connection; a swap is forbidden.
```

If a connection swapped under cross-statement state, that state would be
silently lost: an open TX would split across connections; a server-side cursor
would read from the wrong connection; a temp table would "not exist"; row locks
would vanish. Hence the pinning rule.

**Already enforced by L1:** `ReserveTx(holder, conn)` pins the connection for
the whole transaction (`GetReservedTx` always returns the same one); the
`IsBusy()` guard prevents the pool from handing out a connection with a live
cursor.

**Rules for L2:**
- `ibQuery` owns a holder reference and borrows a connection per operation,
  pinning only while a TX or cursor is open.
- **`ibQueryResult` holds a `shared_ptr` to its connection for its whole
  lifetime** — the cursor cannot be returned to the pool or swapped under
  iteration. Cursor lifetime ⊆ connection-reservation lifetime.
- An autonomous `Execute` (no explicit TX) is **autocommit per statement**;
  a connection swap between such statements is normal, documented behaviour.
- `ibQuery` is **short-lived** — never held across a UI wait (the long-TX
  trap). The RAII destructor is the canonical "close".

---

## 10. Error model — layered, with provenance

The native driver error is **preserved verbatim and never swallowed or
generalized**; each level only *adds* a context frame as the error rises.

```
L1 (driver)  — the REAL error, verbatim:
               native code (FB isc/gds | PG SQLSTATE | MySQL errno),
               native message, Kind (Syntax/Constraint/Deadlock/Timeout/...).
        ▲
L2           — attaches: which driver, the rendered dialect SQL,
               the parameter values (gated, see below), the failing IR node id.
        ▲
L3           — attaches: source span (line:col) in the original query text,
               the logical operation, the metaobject involved.
```

The message then reads top-down: "you wrote *this* query (L3, line N) → it
became *this* PG SQL (L2) → PG answered *this* (L1)" — pinpointing whether the
divergence is in our IR→SQL or in the database itself.

**Classified by origin — the exception type alone names the tier.** Errors are
always attributable to a side, and the *type* of the thrown exception tells you
which:

- **DBMS-side fault** → the `ibBackendDatabaseException` family
  (`ibDatabaseLayerException` with native code / SQLSTATE / Kind). The database
  said no. Even when L2 wraps it with its own context frame (rendered SQL,
  failing IR node), the inner type stays a database exception — the origin is
  never lost.
- **L2-side fault** → a distinct **query-layer** exception, *not* a database
  exception: a malformed or unsupported IR node, a missing dialect form with no
  emulation and no RAM fallback, or pool exhaustion / a lifecycle leak
  (`ibConnectionPoolExhaustedException`, §12). These mean "our layer could not
  build / translate / run the query", categorically separate from "the DB
  rejected it".

So at a glance — by catch type — you know whether to look at the database or at
our translation layer. (Provisional names: an `ibQueryException` base for the
L2 family; the pool-exhausted exception is one of its subtypes.)

**Already half-present:** L1 produces a rich native error —
`ibDatabaseLayerException` carries `GetDriverErrorCode()` + `GetSqlState()`,
and per-driver `ClassifyDatabaseError(nativeCode)` maps it to a `Kind`. The
arc only ensures it is **not lost** on the way up. Propagation uses the
standard wrap-annotate-rethrow pattern; the native object stays reachable via
`dynamic_cast`, `Kind` stays available for `IsRetryable()`.

**Requirement on the IR:** nodes carry a source-provenance span (§5) so L3 can
report "where it arose" precisely. This is compiler-style source-mapping for
queries.

**Gating:**
- Full detail (rendered SQL + param values + native code + span) → log /
  developer / debug channel.
- Sanitized message (metaobject + line + Kind, no raw SQL, no values) → the
  end user.
- Rendered SQL is small and invaluable → **captured always**. Parameter
  **values** are sensitive (PII) and bulky → **debug / flag-gated**
  (provisional — see §15.3).

---

## 11. Observability — on the pool table

The pool already records, per entry, the raw material:
`holder, startedAt, lastUsed, txHolder/scopeHolder, inUse, noWait`. What is
missing is a layer that **reads it diagnostically and ties it to meaning.**

Because L2 is the **single door**, it is instrumented **once**: on each
`Execute`, L2 stamps a `currentActivity` (+ `activitySince`) onto the pool
entry. The pool table then becomes a live view of **"who holds what, doing
what, for how long"** — without touching scattered call sites.

To make it actionable, join `pool entry → holder → ibSession →
(user, computer, pid, currentActivity)` (sys_session already carries
pid / address / currentActivity).

**What it enables:** long-hold detection (`now - startedAt` with no activity →
leak signal), TX-age watchdog, pool saturation (live/idle/max), checkout
contention (waiters).

**Surfaces:**
- a **watchdog job** on the existing registry snapshot tick (~3s), warn-only;
- the **`/admin/diag`** endpoint, extended with a pool view (live/idle/waiting
  + long holders with attribution);
- **error enrichment** (§10) in debug — "held N s by holder H (session S,
  user U)";
- structured **metrics** for the monitoring playbook.

**Discipline:** cheap (no new hot-path locks beyond the entry lock the pool
already takes); gated detail; no parameter values in always-on logs.

---

## 12. Failure model — three lines

> A leak is the **only** failure that kills the pool: a query executed but the
> holder / cursor not released. It is a **resource-lifetime error, not a logic
> error** — and it is **fixed at the source, not tolerated**. Unbounded
> leaking eventually drains every free connection and the program hangs; that
> is a bug to fix, not a condition to live with.

**Line 1 — construction (RAII). The leak cannot be born at the surface.**
- The single RAII door (`ibQuery` / `ibQueryResult`) releases in the
  destructor → correct on normal exit, early return, **and exception unwind**
  (where leaks usually hide). A throw mid-query releases and auto-rolls-back
  for free.
- Move-only handles (one owner per resource); no accidental second owner.
- `[[nodiscard]]` on factories so a handle cannot be silently dropped.
- No raw `new` of these types (private ctor + factory → stack or `shared_ptr`).
- **The raw, leak-prone L1 API** (`RunQueryWithResults` → bare
  `ibDatabaseResultSet*`) is **not exposed at L2** — the historic leak surface
  is removed.

**Line 2 — proof (tests). The leak cannot pass review.**
- A dedicated **lifecycle test suite** whose fixture teardown asserts the
  **pool rest-state invariant**:

  > After any completed unit of work — success **or** failure — the pool is at
  > rest: every connection `idle`, zero reserved (`txHolder` / `scopeHolder`
  > empty), zero `busy`, zero outstanding `ibQuery` / `ibQueryResult`.

- **Fault injection**: a mock driver that throws at every stage — render, bind
  param, run, mid-iteration, commit — and the fixture asserts a clean unwind
  (rest-state) after each. This is where other systems spend years; doing it
  up front, on the existing gtest + `MockDatabaseLayer` harness, compresses it
  to one well-built test fixture.

**Line 3 — prod exposure. A leak that slipped past both is surfaced loudly.**
- A dedicated **`ibConnectionPoolExhaustedException`** — *not* a DB-error Kind,
  so it does not inherit retry semantics (retrying exhaustion would pour more
  connections onto the fire). It belongs to the **L2 query-exception family**
  (§10): its type alone says the fault is in our layer, not the database. Its
  cause is "our code leaked / we are saturated", distinct from "the database
  said no".
- **Payload = a pool snapshot with attribution** — it fingers the actual
  holder (the leaker), not the victim thread that happened to ask for a
  connection: "pool exhausted; N reserved; longest holder H (session S, user
  U, activity Q, T s); M of them stale with no activity".
- **Two-tier trigger:** soft (watchdog warns on "held long with no activity")
  + hard (throw when reserved ≈ `maxSize` **and** M are stale-no-activity).
  The activity signal distinguishes a leak from honest saturation (many
  concurrent real queries → raise `maxSize` deliberately).
- A **checkout timeout** on the *wait* for a free connection throws with the
  same attribution instead of hanging forever — turning a silent, undiagnosable
  freeze into a loud, self-attributing failure. (Provisional — see §15.)
- **Forced release at session teardown + a loud log** survives one leaked
  session without killing the pool, while recording the defect.

**Banned by design** (masking, i.e. "living with the bug"): silent reclaim of
a leaked connection, infinite checkout retry, automatic `maxSize` bumping.

---

## 13. Lifecycle correctness — getting it right immediately

The bar for this arc is a flawless resource lifecycle **from the first
commit**, achieved not by discipline but by **construction + proof** (§12,
lines 1–2). The arc's deliverable is therefore not "implement L2" but
**"implement L2 together with the proof of its leak-freedom."** Concretely the
spec mandates, as first-class work:

1. constructive guarantees (move-only, `[[nodiscard]]`, no raw `new`, no raw
   L1 API at L2);
2. the pool rest-state invariant as a fixture assertion inherited by every
   query test;
3. the fault-injection matrix (throw at every stage → assert clean unwind);
4. the prod echelon (canary + watchdog + checkout-timeout + pool-exhausted
   exception) as the second line, not the first.

---

## 14. Level 3 — metadata surface (SUPERSEDED — realized in §20–§23)

> **Historical.** This was the original L3 stub (recorded only to fix the shape L2
> must serve). L3 has since landed as the source-agnostic `ibBackendQueryable` /
> `ibDataQueryBuilder` door (**§20–§22**); both front-ends landed too — the text
> query language (**§23**, L4-1) and LINQ push-down (**§23.5**, L4-2). The
> provisional lean below ("(1) now, evolving toward (3)") is what shipped: option
> (1), the dual-compile lambda recorder (`compiler/lambdaQueryAst`). Read for
> rationale; the realized mechanism is in §20+.

L3 is the metadata-aware query surface. **Two front-ends lower into the same
`ibQueryIR`:**

- **LINQ** — the existing surface becomes a query language that pushes down.
  A DB-backed source (`Documents.Orders`) plus translatable
  `Where/OrderBy/Take/Join/GroupBy/aggregate` lowers to IR → SQL; non-DB
  sources and untranslatable operations keep the current RAM path; the split
  is a **prefix push-down with a RAM residual** for the untranslatable tail.
  - **The hard problem:** LINQ predicates are compiled lambdas
    (`ibValueFunction`), not expression trees. Pushing `where o.Total > 10000`
    to SQL requires recognizing the lambda body as a *translatable expression*,
    not arbitrary script. Candidate approaches: (1) dual-compile query lambdas
    (bytecode + a light expression-AST when the body is in a translatable
    subset; else RAM only); (2) bytecode pattern-matching (fragile); (3)
    expression-trees as first-class for query lambdas (the .NET model — most
    powerful, biggest compiler change). Provisional lean: **(1) now, evolving
    toward (3)**, with translatability decided **at compile time**.

- **a text query language** — `SELECT ... FROM Catalog.Products WHERE ...` parsed
  and lowered into the **same IR**. This is where the "text-query feel" lives:
  metadata (not tables), reference auto-joins (`Item.Supplier.Name` → JOIN
  chain on `_R` fields), and **register virtual tables** (Balances / Turnovers
  → `GROUP BY` + window aggregation — today aggregated in C++, not SQL). Can
  reuse the existing compiler's lexer/parser idioms and an AOT cache for
  compiled queries.

L3 owns the **metadata → physical mapping** that L2 deliberately does not
(composite field naming, ref joins, register virtual tables, result-column
re-hydration of `_R` columns back into references).

**Access: full DML internally, SELECT-only for end users.** L3 is a high-level query surface but
goes *beyond* a read-only query language: it also expresses writes
(`Insert / Update / Delete`). Those are **gated by capability at parse / compile
time** — end-user-authored query text is restricted to **SELECT** (read-only),
while DML is available only to privileged / platform code. The read-only
guarantee MUST be enforced at **L3** (reject DML before lowering to IR), never
relied upon at L2: L2 faithfully executes whatever IR it is handed, so it is
capability-blind by design — the gate, like SQL-injection safety, lives above
it.

To be designed as its own arc once L2 ships.

---

## 15. Open decisions

These four need an owner's sign-off; the rest of the spec uses sensible
defaults. The provisional lean is noted; nothing below is locked.

1. **Metadata ↔ physical boundary.** *Lean:* **A — L2 is a pure translator**
   (physical IR in; all metadata magic in L3). Alternative B: L2 knows the
   metamodel. A keeps L2 thin and testable without any configuration loaded.
2. **"Identical behaviour" strategy.** *Lean:* **Emulate base operations +
   RAM-fallback the exotic**, with a Floor as the connect-time minimum.
   Alternative: a strict Floor (common denominator only — simpler, poorer).
3. **Parameter values in the error payload.** *Lean:* **debug / flag-gated**
   (security / PII), with the rendered **SQL always** captured. Alternative:
   values always (better prod diagnosability, worse leakage).
4. **MVP slice.** *Lean:* **Firebird + SQLite**, nodes
   `Scan / Filter / Project / Sort / Limit`, the rest Phase 2 — built with the
   lifecycle test harness from commit one.

Also unconfirmed but lower-stakes (spec-level defaults applied): the
`NativeFragment` non-matching-driver semantics (mandatory `default` → fallback,
else error); whether the checkout timeout ships in the first slice; and how much
of the Dialect Dictionary (§6) is loadable data vs compiled-in per driver (the
data-first-with-behaviour-slots shape is chosen; only the data/compiled split is
open).

---

## 16. First vertical slice (MVP)

Smallest end-to-end path, built **with the lifecycle test harness, not "covered
later"**:

```
Catalog/Document.List  →  Where(simple predicate) + OrderBy + Take
   → ibQueryIR (Scan/Filter/Sort/Limit)
   → driver RenderQuery + hooks  (Firebird, SQLite)
   → execute through the session holder (ibQuery)
   → ibQueryResult (normalized ibValue rows)
   → RAM residual for anything not translatable
```

Migrate the existing inline dialect branches (`listSqlBuilder.cpp:14`,
`FIRST` vs `LIMIT`) into the **Dialect Dictionary** (§6) as the first concrete
step — it removes the first fork from business code and yields a working slice
on one or two drivers. Then grow the node set and operations outward.

---

## 17. Migration — retiring the per-DBMS forks

Once the L2 prototype works (the §16 slice green on Firebird + SQLite), the
follow-on programme is to **migrate every existing query that currently forks
by DBMS to route through the single L2 door** — replacing scattered
`if GetDatabaseLayerType() == …` / per-driver string assembly with one
`ibQuery` + `ibQueryIR` path, the dialect closed by the dictionary.

This is a mechanical, incremental sweep, not a rewrite:

1. **Inventory the fork sites.** Grep `GetDatabaseLayerType()` and the inline
   dialect branches (starting with the known ones: `listSqlBuilder.cpp`,
   `registerSqlBuilder.cpp`, the `metaAttributeObjectQuery` composite-field /
   type-mapping paths, `accumulationRegisterQuery.cpp`, …). Each site is a unit
   of migration.
2. **One call site at a time.** Rewrite a site to build an `ibQueryIR` and run
   it through `ibQuery`; move whatever dialect knowledge it encoded into the
   Dialect Dictionary. The fork disappears from business code permanently.
3. **Lock it with the lifecycle invariant.** Every migrated site inherits the
   pool rest-state fixture (§13) and the per-dialect "IR → expected SQL"
   golden-test — so a migration that leaks or changes generated SQL fails the
   build, not production.
4. **No big-bang.** The old per-driver path and the new L2 path coexist during
   the sweep; sites flip over individually. When the last fork is gone, the
   per-DBMS SQL-building surface in `ibDatabaseLayer` can be narrowed to the raw
   primitives L2 itself uses.

The end state: **zero `if FIREBIRD … else PG …` in business code** — every
query is universal, the dictionary is the only place a DBMS difference is named.
And **L1 becomes encapsulated**: L2 is the *only* consumer of the raw
`ibDatabaseLayer` SQL surface (`db_query` / `ses_query` raw queries); L3 rides
on L2; the **holder** is the shared substrate both L2-direct and L3 queries
reserve, so a transaction spanning an L2 query and an L3 query on the same
holder shares one connection. Completion criterion (checkable, lint-able): zero
raw-SQL / L1 access outside the L2 directory.
This sweep is also the real-world proof that the dictionary genuinely closes the
difference: if a migrated query needs a fork the dictionary cannot express, that
is the signal to extend the dictionary (or, rarely, to use the `NativeFragment`
escape, §8) — never to reintroduce an inline branch.

**Tiered migration — split by whether the query has a metaobject.** The sweep
is not "all L1 SQL → L2 IR by hand". It splits in two:
- **No metaobject → L2 directly** (hand-built IR): system / service tables
  (`sys_*`, bytecode cache, session registry) and **all DDL**. There is no
  metadata to express them through, so they target L2.
- **Metaobject-bound → L3**: catalog/document list fetch, register
  Balances/Turnovers reads, etc. These express at **L3** (LINQ / text-query),
  which owns the metadata→physical mapping (composite fields, ref auto-joins,
  register virtual tables) and lowers to IR → L2. Hand-building physical IR at
  every such site would duplicate that mapping in every call site.

Sequencing is **two-phase** (architect's call): **Phase 1** migrates *all* L1
queries to L2 — including metaobject-bound ones, with hand-built physical IR —
so the dialect forks disappear immediately, without waiting for L3. **Phase 2**
then *promotes* the metaobject-bound subset from L2 up to L3 (expressed via
LINQ / text-query) once the L3 binder lands, replacing the hand-built IR with the
metadata surface. The interim cost — Phase 1 hand-builds physical IR at
metaobject sites and briefly duplicates a little metadata→physical logic — is
accepted in exchange for removing every fork up front. This is also the
strongest argument for keeping L2 dumb (physical IR) and putting the metadata magic
in L3: otherwise it spreads across call sites permanently.

---

## 18. Worked example — `ibListSqlBuilder` is the L2 acceptance test

**Definition of done for L2 (2026-06-06):** L2 is "successful" when
`ibListSqlBuilder` genuinely runs on it. That builder
(`metaCollection/partial/list/listSqlBuilder.cpp`) is the hand-rolled
SQL-fragment assembler behind every paged Fetch — the densest real fork in the
codebase — so reproducing it on L2 is the proof the layer carries its weight.

### Why it is the perfect specimen — it conflates two tiers

`ibListSqlBuilder` is metadata-aware (`meta->IsDataReference`,
`FindAnyAttributeObjectByFilter`, `GetCompositeSQLFieldName`, `GetSQLFieldData`)
— it is already half **L3**. But it also hand-codes the dialect, so it does
**L2** work too, with the disease this arc cures: `BuildSelectHead` /
`AppendLimit` literally branch
`if GetDatabaseLayerType() == DATABASELAYER_FIREBIRD` to emit `SELECT FIRST n`
vs `... LIMIT n`. **It is L3 and L2 fused in one class.**

The clean refactor splits it along the **metadata-dependency boundary** (§14):
the L3 half (metadata → physical names) stays up near metadata but now *emits
IR*; the L2 half (physical → dialect SQL) routes through
`ibDatabaseQueryBuilder`, and the fork dies — it lives once, as data, in
`ibDialectDictionary`.

### Method-by-method mapping

| `ibListSqlBuilder` produces | On L2 becomes | Fork? |
|---|---|---|
| `BuildSelectHead` (`SELECT FIRST n` ⟂ `SELECT *`) | `.Limit(n)` + dictionary pagination | **gone** |
| `AppendLimit` (` LIMIT n` ⟂ ∅) | same `.Limit(n)` | **gone** |
| `BuildFilterWhere` (`=` / `<>`) | `.Where(ibBinOp(Eq/Ne, ibCol, ibParam))` | n/a |
| `BuildOrderBy` (multi-field ASC/DESC) | `.OrderBy(col, dir)` ×N | n/a |
| `BuildAnchorPredicate` (keyset OR-of-AND) | one `.Where()` of nested `ibBinOp(Or/And/Gt/Lt/Eq…)` | n/a |
| `BindFilterParams` / `BindAnchorSortParams` (manual positional) | renderer's bind plan from `Const`/`Param` nodes | **gone** |
| composite `_N/_S/_R` field names, ref → `guidName` | the **L3 half**: metadata→physical, emits `ibCol`/IR | stays L3 |

Two things get strictly *better*, not just equal: the manual positional binding
disappears (the renderer derives it), and the keyset's last-row guid — which
`ibListSqlBuilder` inlines as a string literal `'%s'` — becomes a **bound
parameter** (`ibConst`), closing a tiny injection-shaped seam by construction.

### Two readings of "done" — one now, one build-gated

- **(A) Architectural — provable now.** Every fragment `ibListSqlBuilder`
  emits, including the intricate keyset cursor, is reproducible as L2 IR and
  renders dialect-correct across FB/SQLite/PG. Proven by golden tests
  (`tests/test_queryBuilderShowcase.cpp`) with no database, no build. This is
  the experiment-stage milestone.
- **(B) Operational — build-gated.** The live paging path actually calls
  `ibDatabaseQueryBuilder`, `ibListSqlBuilder` is deleted, validated by a build
  and a running app. This is a Phase-1 migration item (§17) and the keyset
  cursor is correctness-critical (it had subtle K≥2 mixed-ASC/DESC bugs), so it
  flips only with a build — when the experiment stage closes, on command.

**The Phase-1 emitter exists:** `metaCollection/partial/list/listIRBuilder.{h,cpp}`
(`ibListIRBuilder`) is the metadata→IR half of the split — `BuildFilterPredicate`
/ `BuildSortKeys` / `BuildAnchorPredicate` reproduce `ibListSqlBuilder`'s build*
methods but return `ibQueryExpr` / `ibQuerySortKey` instead of SQL text, with
values as `Const` (the renderer binds them). It is metadata-dependent, so it has
no pure unit test; it validates when the live `objectListQuery` fetch is cut
over to `ibDatabaseQueryBuilder` (the (B) step) — build-gated.

---

## 19. Performance — L2 is on the hot path

L2 sits under every paged Fetch and every scroll tick (the same path that
already carries `BuildVisibleView` caching debt). **It must not regress against
the raw-SQL path it replaces.** The contract:

1. **Dialect = immutable per-driver singleton, reached polymorphically.** Each
   driver owns its dialect as a static accessor (`ibDatabaseLayer<Driver>::Dialect()`,
   magic-static built once) and exposes it via the virtual
   `ibDatabaseLayer::GetDialect()`. L2 calls `conn->GetDialect()` and holds the
   dictionary by `const&` — never a per-`Execute` rebuild/copy, and **no
   `GetDatabaseLayerType()` type-switch anywhere**. Adding a DBMS adds a driver,
   nothing central. *(done)*

2. **Repeating queries: build once, rebind many.** A scroll re-runs the *same
   query shape* with only the cursor anchor changing. The fast pattern is to
   build the IR **once**, render **once**, cache the prepared statement, and
   per fetch only **rebind** the changed values — not rebuild the IR tree,
   re-render the SQL, and re-prepare each tick.
   - Values that **vary per fetch** (the keyset anchor) ride as **`Param`**
     (external vector), so the IR/SQL stay byte-identical and the prepared
     statement is reused; only the bound values change.
   - Values **stable across the scroll** (the active filter) may be `Const` —
     they only change when the user edits the filter, which rebuilds anyway.
   - Note: `Const` and `Param` both render to `?`/`$n` (a `Const` is *bound*,
     never inlined), so SQL text is stable either way — but `Param` additionally
     lets the caller skip the IR rebuild + re-render, which is the real saving.

3. **IR nodes stay lightweight.** `shared_ptr` tree allocation is paid once per
   distinct shape (point 2 keeps it off the per-tick path). No metadata, no
   virtual dispatch in the node walk; the renderer is a single forward pass.

4. **Bind plan is O(params)**, built in placeholder order in the same pass — no
   second walk, no map lookups.

**Target:** L2's steady-state cost across a scroll is *one* prepare + *N*
rebinds — strictly ≤ the raw path, which tends to re-prepare. The build-once /
rebind-many shape is what turns the abstraction into a *speedup*, not a tax. A
future `ibDatabaseQueryBuilder` "prepared query" handle (render once → execute
many with fresh params) is the explicit home for this; the `Param` + external
vector plumbing already in place is its foundation.

## 20. Level 3 — the `ibBackendQueryable` door (realized)

§14 was the placeholder; this is the realized shape. L3 is the metadata read
surface: `ibMetaQueryBuilder` (`backend/query/metaQueryBuilder.{h,cpp}`) is the
fluent entry — `From / Where / OrderBy / GroupBy / WhereKey / WhereKeyIn /
Select` — mirroring L2's verbs at the metadata level. It generates L2 by
**substituting names** (metaobject → table, attribute → physical field, row-key /
reference → column); the name-substitution primitives (`ibMetaIRBuilder`, folded
into the same module) emit the generic IR machinery — AND/OR folding,
lexicographic keyset, `Const`/`Param` emission — and the resulting IR is
metadata-free, so L2 stays metadata-blind.

### The interface — one door, two families

`ibBackendQueryable` (`backend/query/queryable.h`) is the **data-navigation**
interface L3 reads a metaobject through. Two families implement it:
`ibValueMetaObjectRecordDataRef` (catalog / document / charts / **enums** — for
free, via inheritance) and `ibValueMetaObjectRegisterData` (information /
accumulation / accounting). It is a pure mixin (no `ibValue`), listed as the
**second base** so `ibValue` stays at offset 0 (the first-base PMF rule).

The contract digests a metaobject and emits everything needed to build a query:

- `ResolveAttribute(name | id)` — digest an attribute reference by string (L4
  text) or metaID (L3) into the resolved attribute; null = "no such requisite",
  which is also how L3 validates a name against the metadata tree.
- `GetQueryTableName` / `GetQueryTableId` — physical layout.
- `GetIdentitySort()` — the identity / keyset-tiebreaker columns as query-native
  sort items, **all REAL columns** (no null sentinel): catalog returns `{ uuid }`,
  a register its real identity attributes (recorder+line, or period?+dimensions),
  a tabular section `{ line number }`. L3 appends them to the user sort
  (`EffectiveSort`, deduped by column pointer) → one uniform total order, **no
  catalog-vs-register fork** in the keyset code. The provider reads a single-key
  source's row-key field off the unique tail (`RowKeyField` =
  `GetIdentitySort().back()`); the uuid is a **rudiment** kept as this read keyset
  + DELETE-by-key column until it is cleaned.
- `GetPrimaryKeyColumns()` — the **ONE key authority** for the write UPSERT match
  AND the dot-walk self-reference. A record returns its `{ data-reference }` (the
  row's own `_RRRef` reference blob — unique; the provider reads its Reference field
  for the join, all its fields for the match — `_RTRef` is constant for a
  monomorphic self-reference, so the match is effectively on the unique `_RRRef`); a
  register its composite (recorder+line+period / period+dimensions); a constant
  `{ RECORD_KEY }`. There is **no** `GetRowKeyColumn` / `IsReferenceAttribute` /
  `GetReferenceKeyColumn` — all three are derived from this single authority. uuid +
  `_RRRef` coexist as two link keys until uuid is cleaned. (provider helpers:
  `RowKeyField`, `ReferenceFieldOf`, `SelfReferenceField`.)
- Value / reference materialisation lives in the DB provider (`GetValueAttribute`),
  which static_casts a column to the metaobject attribute — **guarded by
  `IsRawColumn()`**, because a raw column (a `ibRawDBColumn` parent-uuid / row-key
  filter) is not an attribute (an unguarded cast crashes — see record-locks).
- `HasVirtualTables` / `GetVirtualTables` — L3 learns whether a metaobject has
  derived selections (catalog: none; registers: balances / turnovers / slices).
  Descriptor population follows as each virtual table is wired.

### Acceptance — both hand-rolled SQL builders retired

Just as `ibListSqlBuilder` was L2's acceptance test (§18), **`ibRegisterSqlBuilder`
is L3's**: the register list (`ibValueListRegisterObject`) now reads through the
same door as catalogs — `From(meta) / Where / OrderBy`, identity tail appended by
`GetIdentitySort`, anchor via `EffectiveSort` + `Param`. The string builder
(`registerSqlBuilder.{h,cpp}`: `BuildSelectHead / BuildFilterWhere / BuildOrderBy
/ BuildAnchorPredicate / Bind*`, plus `IdentityAttrs / EffectiveOrder`) is gone —
`IdentityAttrs` became `GetIdentitySort`, `EffectiveOrder` became
`ibMetaQueryBuilder::EffectiveSort`.

### Performance — L3 must not be inferior to L1

The §19 contract carries straight through L3: the metadata resolution (table,
identity, attribute fields) is cheap and rebuilt only on filter/sort change, not
per tick; the cursor anchor rides as a `Param` so the SQL text is byte-identical
across scroll ticks and the driver prepared-statement cache hits; the holder is
borrowed per fetch and released by the selection's dtor (no connection pinned
across ticks). The per-row cost is `MaterializeAttribute` → `GetValueAttribute`,
the same call the raw path made. **L3's steady-state cost is one prepare + N
rebinds — it does not regress against the hand-rolled SQL it replaces.**

## 21. L3 hardening — read-core, write-core purity, the column, the provider seam

This pass took L3 from "reads/writes go through the door" to "the door's public
surface names no L2 type, and every record-CRUD site that *can* go through it
does". It is **unbuilt at time of writing** — the batch below is large and touches
a fundamental header via multiple inheritance, so a green `Debug|x86` build is the
gate before the next arc (the provider, §21.7).

### 21.1 Read-core — composite-key reads via the write decomposition

A register / record read by a multi-field key (dimensions, composite reference)
must match **every** physical field, not just the first. The trick: the value →
per-field decomposition already exists as `SetValueAttribute`, and `ibQueryStatement`
captures each `SetParam*` as an `ibConst`. So `BuildFilterPredicate` runs the value
through a **capture-only** statement (`CapturedValues()`, never `RunQuery`) and
AND-folds the captured Const nodes — `Where(attr, Eq, value)` on a multi-field
attribute now filters on all its fields, symmetric with how `WriteRow` writes it.
No new per-type logic; the write decomposition is reused for the read predicate.

Sites moved onto this: register selector `Read`, `ibValueRecordManagerObject::ExistData`,
record-set `ExistData()` / `ReadData(key)` / `ReadData()`, information-register
non-periodic `Get(cFilter)`, chart-of-characteristic-types `FindBy*`.

### 21.2 Write-core purity — `WriteKind`, no L2 statement on the surface

`WriteRow`'s public signature took `ibQueryStatement::Kind` — an L2 type leaking
into the L3 surface. Replaced with an L3-native `ibMetaQueryBuilder::WriteKind`
(`Insert / Upsert / Delete`), translated to `ibQueryStatement::Kind` only inside
the `.cpp`. All seven call sites (door `Upsert`/`DeleteByKey`, record-set ×2,
tabular ×2, constant) pass the L3 enum.

### 21.3 No `Raw()` — every consumer reads via the selection

`ibMetaQueryResult::Raw()` (the transitional raw cursor) is **removed**. Its last
users were the catalog tree-fetch (`BuildTreeRowFromResultSet`); they became
`BuildTreeRowFromSelection`, reading through `GetValue(attr)` and the row guid via
the uuid identity column (`GetValue(GetIdentitySort().back().m_col)`; the former
`GetGuidString()` special accessor is gone — the row-key is read as a column like
any other). The public surface of `ibMetaQueryResult` now names no L2 result set.

### 21.4 Header purity — the L3 header includes no L2

`metaQueryBuilder.h` no longer `#include`s `databaseQueryBuilder.h` and names no L2
type by value:
- `ibMetaIRBuilder` (all methods returned L2 IR) moved entirely into the `.cpp`
  (file-local; only that TU lowers to L2);
- `ibMetaQueryResult::m_cursor` is pimpl'd (`std::unique_ptr<ibQueryResult>`,
  forward-declared); move-ctor/assign + dtor are out-of-line where `ibQueryResult`
  is complete;
- `ibRenderedPageCache` is opaque — forward-declared, defined in the `.cpp`, created
  through `ibMetaQueryBuilder::NewPageCache()` (the list model holds it via
  `shared_ptr`, never naming its layout);
- `BuildPageIR` forward-declares its `ibQueryIR` return.

The one direct-L2 caller that lost the transitive include (`tabularSectionQuery.cpp`)
got its own `#include`.

### 21.5 tabular → `ibBackendQueryable`, via an adapter (not a meta split)

The tabular section's persistence is already split at the **data-object** layer
(`ibValueTabularSectionDataObjectRef` = persistent, `…DataObject` = transient
report/data-processor). The tabular **meta** is shared and structurally identical,
so it is *not* made queryable and is *not* split into a `…Ref` variant (that would
be a parallel hierarchy for a non-structural difference). Instead the persistent
data-object builds a small `ibTabularQueryable` **adapter** at `LoadData` time
(uuid row-key, line-number identity). Queryability follows persistence, which is
already where it lives. `LoadData` reads through the door + `GetValue`; the
L2-direct `RawResultSet` path is gone.

### 21.6 The column — `ibBackendQueryColumn`, an attribute IS a column

`ibBackendQueryColumn` (`query/queryColumn.h`, a light header) is the column
counterpart of `ibBackendQueryable`: a **pure L3 descriptor** of one logical field —
`GetName()` (logical name), `GetPhysicalName()` (db field base), `GetTypeDesc()`
(its `ibTypeDescription` — CLSIDs + number/string/date qualifiers). No physical
field list, no statement, no cursor, no positions: the physical multi-column split
(`_TYPE / _N / _S / _RRRef`) and the binding are **lowering**, derived from
`(physical, type)` below the L3 surface.

`ibValueMetaObjectAttributeBase` **derives** from it — an attribute *is* a query
column (third base, after the `ibValue`-deriving `ibValueMetaObject` first base, so
offset 0 holds). `GetName`/`GetPhysicalName` on the base; `GetTypeDesc` is the
attribute's existing accessor, **reused as-is** to satisfy the interface (no copy,
no separate method). No adapter in the builder — the attribute carries the column
identity directly. It lives in its own light header so the fundamental attribute
class does not drag in the full `queryable.h` / `model.h` weight.

A **constant** used to be both: a column (it derived `ibValueMetaObjectAttribute`) **and** the
single-row `sys_const` table it lives in. **It is neither now — it HAS both (2026-07-29).** The
table face moved out first (a vended `ibConstantQueryable`), and the column face followed: a nested
`ibValueMetaObjectConstantColumn`, reachable only through `GetValueColumn()`. `GetConstValue` still
reads through the door — column detail = the nested column, table detail = the queryable.

Being both was not a shortcut, it was a contradiction, and it was paid for at the form layer. Every
form asks its source for a `CompositeData` (`srcObject.h`), which a constant-as-attribute is not, so
the object half was produced by C-casting the constant into an unrelated class — a
`reinterpret_cast` whose virtual calls landed in a foreign vtable. The visible symptom was a
constant opening READ-ONLY under full rights (`AccessRight_Modify` read out of a wrong slot); the
invisible one was that it could have jumped anywhere. `ibValueMetaObjectConstant` now derives
`ibValueMetaObjectGenericData` and answers the composite questions itself: one attribute, and it is
its value column.

Two invariants hold the physical schema still, and both live on the nested column: `GetColumnId()`
returns the CONSTANT's metaID (columns are matched baseline↔target by id, so an id of its own would
read as drop-plus-add and empty every constant), and `GetPhysicalName()` keeps the `fld<metaID>`
rule keyed on the same id. The type stays a property of the constant — the column reads it back — so
what the user edits and what the DDL renders remain one value.

### 21.7 NEXT (after a green build) — the provider seam

The one remaining coupling: the door's verbs (`Where` / `OrderBy` / `SetValue` /
`WriteRow`) still name `ibValueMetaObjectAttributeBase`. Making them name
`ibBackendQueryColumn` is blocked by a single registry dependency: deriving the
reference fields (`_RTRef / _RRRef`) needs `ContainMetaType(Reference)` →
`m_metaData->GetTypeCtor(clsid)->GetMetaTypeCtor() == Reference`, which a *pure*
column cannot answer (everything else is `ContainType` = type-desc, which it can).
A bare `column → attribute` downcast would defeat the abstraction (it exists
precisely for **non-attribute** columns — keys, computed, virtual).

The resolution is a **query/data provider** — the data-source strategy under a
query:
- **DB-table provider** — direct L2 `SELECT` over the real table (today's implicit
  path). The registry-backed lowering (field decomposition, reference resolution
  via `GetTypeCtor`) lives **here**, where the execution context and metadata
  already are — not on the column;
- **temp-table provider** — materialise a set, then query it;
- **virtual-table provider** — *compute* (register balances / turnovers / slices via
  the Aggregate IR / subqueries); never touches physical reference columns.

The queryable advertises its providers — `ibBackendQueryable::GetVirtualTables()`
(`Balance / Turnovers / SliceLast / SliceFirst / Records`) is the existing seam;
the main metaobject table is a DB-table provider. The door selects a provider from
the queryable and delegates execution. With the lowering inside the provider, the
column stays pure and the door's verbs become `ibBackendQueryColumn`-typed cleanly
— no downcast. **This is the body of the virtual-tables arc and is to be designed
on a green base.**

## 22. The query provider — design sketch

§21 landed **green** (`Debug|x86`, 0 errors) and is **runtime-validated**: writes
(catalog / document / register / tabular), constants (read+write), tabular
(read+write), flat list, hierarchical tree, information register all work through
the door. This section sketches the next arc — the provider — for review *before*
code.

### 22.0 Generality — a domain-free federation, registers are the first consumer

The decisive realization of the whole arc: this is **not** "a query system for
registers" — it is a **general relational engine over pluggable sources**. The three
abstractions are **domain-free** — none knows about accounting or even about the
engine's metaobjects:

- **queryable** — a relation (rows + columns), whoever vends it;
- **column** — a typed field (name / type / role), blind to "register or not";
- **provider** — *how* to realize the relation: real table / computed / temp / RAM /
  **anything else**.

So the same machinery serves far past registers' balances / turnovers (the headline
case): chart-of-accounts movements; a catalog's change-history (→ versioning is just a
JOIN with the history companion — "as-of T" is a temporal filter, "diff" is a
self-join; no versioning subsystem); a document's movements; **and beyond the built-in
metaobjects** — an external source (another database, a REST API, a file) as a
provider; an in-memory set as a RAM provider; a plugin that vends its own queryable +
provider ([[plugin-extensible-metadata]]). A **queryable can be metadata-defined in
the configurator** with a source pointing at another database — its provider opens a
second `ibDatabaseLayer` (L1 already speaks five backends + connection strings), and
L3 composes it with native data: declarative federation / ETL **in metadata, no code**.

Crucially, cross-source federation needs **no special case** — it is exactly the
§22.1a "mixed, not all SQL-capable on one connection" path: a native queryable (DB-1)
and an external queryable (DB-2) cannot fold into one `SELECT`, so the composer pushes
down to each provider what it can do (filters / projections into its own database) and
**materialises the rest** (the external side into a temp / RAM relation), then joins.
Federation **fell out of** "materialise what doesn't compose into one SQL" — it was
not designed for separately.

This generality is not luck: it is the payoff of cutting along domain-free seams. The
whole tower is source-agnostic top to bottom — L1 (any driver) → L2 (any physical
query) → L3 (any source via a provider) — so it carries arbitrary data, not just the
engine's own. **Grounding:** the infrastructure *supports* all of this; registers will
*prove* it (first consumer); external / plugin / computed sources are *unlocked
potential* realised per case — but the door they enter is already the right shape.

### 22.1 The model — providers are relations, L3 is the composer

**`L3 = surface + engine`.** The surface is the door (`ibMetaQueryBuilder`) + the
vocabulary (`ibBackendQueryable` / `ibBackendQueryColumn`): you express a query in
metadata terms, *not knowing where the data comes from*. The engine is the
**provider**: it realizes a source. L2 stays *under* the providers (the DB provider
lowers to L2; the virtual provider is L2 + compute; the RAM provider is neither).

The decisive shape: **a provider is a relation** — a FROM-able source with its own
columns. One L3 query **composes several providers** — the classic multi-source
query that joins a real table, a computed register virtual table, and a temp table:

```
SELECT … FROM   Catalog.Item                       ← provider 1 — DB        (real table)
         JOIN   AccumulationRegister.Goods.Balance  ← provider 2 — virtual   (L2 + aggregate)
         JOIN   TempTable.Prices                     ← provider 3 — temp/RAM  (materialised)
         ON …
```

- each provider yields a **relation** with **columns** (`ibBackendQueryColumn`);
- the **column is the join/projection currency** — L3 joins providers BY columns and
  projects columns, uniformly, blind to whether a source is real or computed;
- a **virtual table stops being a special case** — it is just a relation the provider
  *computes* (aggregate subquery) instead of *reads*.

**Two regimes:**
1. **single-source** (≈ everything today) — the whole query lands on one provider;
   L3 just picks it and runs. This is the current door, refactored behind the
   interface.
2. **mixed** — the query spans sources (a DB catalog filtered by a RAM guid-set;
   real movements beside computed balances; `DB × temp` join). L3 **orchestrates**:
   push down to each provider what it can do (predicates, projections), materialise
   the rest, and stitch.

### 22.1a Realization — one SQL where possible, materialise the rest

The provider's relation has two flavours that decide how L3 stitches:
- **SQL-capable on one connection** — DB (real table), virtual (a subquery), temp
  (a driver temp table). These **compose into ONE `SELECT`** — the join runs in the
  DBMS, push-down maximal (server-side temp tables hold the intermediate sets);
- **RAM-only** — a set already in memory. L3 first **materialises it into a temp
  table** (then it joins in SQL like any other), or, as a fallback, joins it in C++.

So L3 is a **relational composer over provider-relations**: assemble one plan from
their SQL fragments; materialise non-SQL sources to temp first. `ibMetaIRBuilder` +
`ibQueryStatement` (today the door's internals) become the **DB provider's** lowering;
the door holds only the metadata composition.

### 22.1b The result tier — aggregate, totals-by, hierarchical output

Composition (§22.1a) yields *rows*. On top of them L3 has a **result-shaping tier**:
- **aggregate** — `SUM` / `COUNT` / `MAX` / … as projected columns;
- **totals-by** — subtotal rows grouped by one or more fields;
- **hierarchical output** — the result is a **tree**: group → subgroup → detail, with
  a subtotal row at every grouping level plus a grand total, not a flat list.

So a query result is not always a flat `ibMetaQueryResult` — with totals it is a
**hierarchical selection** (a walk over groupings, each yielding its subtotal and its
nested rows). Realization mirrors §22.1a:
- **push to SQL** where the DBMS can — `GROUP BY` for the aggregate, `ROLLUP` /
  `GROUPING SETS` for the per-level subtotals (the Dialect Dictionary closes the
  spelling across the five backends);
- **shape in L3** the parts SQL can't carry portably — group the base rows, fold the
  aggregates per level, and build the tree (the portable fallback; also the path when
  a grouping key comes from a non-SQL provider).

This is the reporting face of the crown — totals-by + hierarchical selections. It
reuses the Aggregate IR the virtual providers already need (§22.5 step 3); the
totals tier is that IR plus the per-level `ROLLUP` shaping and the hierarchical
result walker.

L3's engine is therefore two stages: **compose** provider-relations into a base
relation, then **shape** it (aggregate / totals-by / hierarchy) into the result.

**A LEVEL CAN BE NAMED (2026-08-06).** The grammar is
`TOTALS <aggregates> BY <expr> [HIERARCHY | HIERARCHYONLY] [AS <name>] , …`. The unfold belongs to
the DIMENSION, the name belongs to the LEVEL it produces — which is why the name is written after
it. Empty means "the column's own name", right for the ordinary single-level case; the alias
becomes `OutputColumn::m_name` in the lowering. Without it, two levels over the same column (Date
by month, Date by day) answer to one name and one of them wins. The bare form (an identifier with
no `AS`) is accepted too — after a dimension, an identifier can only be its name.
`ibQueryTotalDim::m_alias`; round-trip tested in `tests/test_queryL4Parser.cpp`.

#### 22.1b LANDED — Selection / Selector engine (2026-06-09, experimental, pre-build)

The result-shaping tier above is implemented as a **two-step, two-axis** model. The query
returns RAW data; a separate Selector walks it.

```cpp
ibDataQueryResult result = q.Execute(req);          // raw selection (snapshot inside)
ibSelector        s      = result.Select(kind);     // a Selector: HOW to walk
while (s.Next()) {                                   // cursor — pre-order
    s.GetValue(col);   // value on a leaf / SUBTOTAL on a group node (in-place column)
    s.Level();         // depth (0 = root); s.HasChildren(); s.GetTotal(col) = grand total
    ibSelector sub = s.Select(kind);                 // sub-selection of the CURRENT node (recurse)
}
```

- **Two axes.** `ibSelectKind` (traversal): `Direct` (rows as-is) / `ByGroups` (by levels) /
  `ByGroupsHierarchy` (levels + dimension hierarchy). `ibDimensionKind` (a TotalBy field unfold):
  `Elements` / `Hierarchy` / `HierarchyOnly`. Axis 1 is the `Select(kind)` argument; axis 2 lives on
  each `TotalBy(field, dim)`.
- **Door verbs.** `Group()` / `Totals()` switch the aggregate context (a common GroupBy set and a
  common TotalBy set — a level has no own aggregates); `TotalBy(field, dim)` adds a dimension level
  (levels apply IN ORDER); `Sum/Min/Max/Avg/Count(col)`; `Distinct()` → `SELECT DISTINCT`.
- **Aggregate IN-PLACE.** A subtotal is written into the aggregate's OWN column — `GetValue(col)` is
  the row value on a leaf and the subtotal on a group; `GetTotal(col)` is the grand total. `COUNT(*)`
  uses a synthetic receiver read by `GetColumn(alias)`. A column missing at a level reads as `NULL`.
- **One snapshot.** Subtotals roll from the SAME snapshot the door stamped (Select list + aggregate
  inputs + TotalBy fields) — never a second query, so detail and total cannot skew.
- **Fold engines** (`ibQueryComposer`): `BuildTotalsTree` (multi-level grouping), `BuildHierarchyTree`
  (a catalog's OWN row-keyed hierarchy: row = node, parent-ref), `BuildReferenceHierarchy` (a single
  cross-catalog reference dimension), and the GENERAL value-keyed combiner `BuildDimensionTree`
  (N levels in order; a `Hierarchy` level unfolds the field's target-catalog parent-map — read
  through the door — and the next level recurses inside each value). `GetHierarchyColumn()` on the
  queryable vends the parent attribute. The DB push-down stays `GROUP BY ROLLUP` (`ExecuteRollupTotals`).
- **Lazy navigation.** `s.Select()` (no fold needed) re-Executes one node's direct children with the
  inherited filter — the recursion `selection -> sub-selection`. A plain hierarchical list (no totals)
  needs only this; subtotals are the only reason to materialise a whole snapshot.
- **Types.** Raw rows = `ibQueryRamTable` (a flat array); the folded tree = `ibSelectorTree`
  (`querySelectorTree.h`); the cursor/traversal = `ibSelector` (`querySelector.h`). Mirror into a
  runtime tree model via `ibValueModelRamTreeBase::PopulateFromTree`.
- **Identity key — `ibValue::GetHashKey()`.** Grouping / hierarchy linking needs a value's IDENTITY,
  not its display string. `ibValue::GetHashKey()` (virtual) is that key: the base delegates through the
  reffer chain to the target object, which overrides it (a reference keys by its **guid**, so two cells
  pointing at the same row share a key — a child's parent-ref matches the parent's row-ref; the display
  string does NOT, so two distinct rows with the same name no longer merge). A plain value keys by its
  string; extend per type. The composer speaks only `ibValue` — no runtime type, no object address.
  Used by the fold engines, by RAM `GROUP BY` (`RamAggregate` / `FoldTotals`), and as the RAM hash-join
  key (`JoinRamTables`, O(n+m)). Future RAM `DISTINCT` / `UNION` dedup will key by it too.

### 22.2 The interface (proposed)

```cpp
class BACKEND_API ibBackendQueryProvider {
public:
    virtual ~ibBackendQueryProvider() = default;

    // Read: run the accumulated query (columns + conditions + sorts + page) and
    // open the L3 selection. The provider owns HOW (direct SELECT / temp / aggregate).
    virtual ibMetaQueryResult ExecuteRead(const ibMetaQuerySpec& spec,
                                          const ibReadPageRequest& page) = 0;

    // Write: only the DB-table provider implements it (virtual/temp throw or no-op).
    // The provider owns the field decomposition + the dialect UPSERT/DELETE.
    virtual bool ExecuteWrite(ibMetaQueryBuilder::WriteKind kind,
                              const ibMetaWriteSpec& spec) { return false; }
};
```

`ibMetaQuerySpec` is the door's accumulated state lifted into a value object
(columns by `ibBackendQueryColumn`, conditions, sorts, key-in, parent-ref) so the
door no longer reaches into L2 — the provider lowers it. `ibMetaWriteSpec` is the
write counterpart (key column + assignments by column + match keys).

### 22.3 Where lowering moves — and why the column stays pure

The field machinery (`GetSQLFieldData` / `SetValueAttribute` / `GetValueAttribute`,
including the `ContainMetaType(Reference)` → `GetTypeCtor` registry hop) moves out
of the door and into `ibDbTableProvider`, which has **both** the execution holder
**and** the queryable's metadata. So:
- the door's verbs take `ibBackendQueryColumn` (pure: name / physical / type-desc);
- the **provider** turns a column into physical fields — for the DB-table provider
  using the registry (reference detection), for the virtual-table provider not at
  all (it projects aggregates);
- no `column → attribute` downcast: the provider resolves columns through its own
  source-specific machinery + the queryable's metadata.

This is the §21.7 wall dissolved: the registry dependency lives with the provider
that already owns the execution context, not on the column.

### 22.4 Provider per source — the door ↔ queryable handshake

Each source in a query (the `FROM` and every `JOIN`) gets **its own** provider from
its queryable; L3 composes them (§22.1). The queryable vends a provider per table
kind:

```cpp
// on ibBackendQueryable:
virtual std::unique_ptr<ibBackendQueryProvider> CreateProvider(
    ibVirtualTableKind kind = ibVirtualTableKind::Records,   // Records = the main table
    const ibVirtualTableParams& params = {}) const = 0;
```

- `From(queryable)` (no kind) → the main table → a DB provider over
  `GetQueryTableName()`;
- `… JOIN queryable.Virtual(Balance, {period, dims})` → `GetVirtualTables()` is
  consulted; if `Balance` is advertised, the queryable builds a virtual provider
  configured for balances;
- a temp/RAM source vends a temp provider over its materialised set.

Each provider is created per query (cheap), holds the holder + its queryable, exposes
its relation's **columns**, and dies with the `ibMetaQueryResult` (RAII). For a
single-source query the composer is a no-op pass-through (today's path); for a mixed
query the composer stitches the providers' relations (one SQL where all are
SQL-capable, else materialise-then-join, §22.1a).

### 22.4a A virtual table IS a queryable — a metaobject vends a family

The decisive structure: a register metaobject does not toggle a "virtual mode" — it
**vends a family of queryables**, each a full `ibBackendQueryable` with its OWN
columns and its OWN provider:

```
AccumulationRegister.Goods            — records   : DB queryable      (cols = its attributes)
AccumulationRegister.Goods.Balance     — balances  : virtual queryable (cols = dimensions + resource-balances)
AccumulationRegister.Goods.Turnovers   — turnovers : virtual queryable (cols = dimensions + resource-turnovers)
AccumulationRegister.Goods.SliceLast   — slice     : virtual queryable (cols = dimensions + last values)
```

L3 reads any of them uniformly (`From(balanceQueryable)`) and composes them (the
records JOIN their own Balance). The consequence that makes the whole arc cohere:

- a **balance column is NOT an attribute** — it is a *computed* column (name
  "QuantityBalance", type number, physical = a `SUM(...)` expression). It implements
  `ibBackendQueryColumn` its own way. This is exactly why the column is a pure
  interface and **not** an attribute base type — and why a `column → attribute`
  downcast is fatal: a balance column has no attribute behind it;
- so the **lowering lives in the provider**, per kind: the DB provider of the main
  table decomposes attribute-columns through the field machinery (`_TYPE / _N /
  _RRRef`); the balance provider **computes** (aggregate), touching no physical
  reference field at all.

`GetVirtualTables()` is therefore a vend of virtual queryables (each with columns +
a computing provider), not a flag. The main metaobject table is the DB queryable of
the family.

### 22.4b A column is name + type + role — NOT a metaID

A column is **not 1:1 with a metadata id**. One resource (`Quantity`, a single
metaID) yields **four or five** columns on a virtual table — OpeningBalance,
ClosingBalance, Turnover, Receipt, Expense. So `GetMetaID()` does **not** belong on
`ibBackendQueryColumn`: a column is identified by its **name + type + role**, not a
metaID:

- attribute column — `(name, type, role = attribute, source = the attribute)`;
- virtual column   — `(name "QuantityTurnover", type = number, role = Turnover,
  source = the Quantity resource)`.

The shared interface carries only name / physical / type / role. The metaID-keyed
logic (the self-reference key via `GetPrimaryKeyColumns`, identity dedup,
`ContainMetaType`) is **attribute** machinery and lives in the **DB provider**,
never on the column (no per-column primary-key / reference flag).

**Resolution, not downcast.** A condition / sort references a column (by handle or
name); each provider **resolves** it through *its own* queryable — the DB queryable
resolves to an attribute (`ResolveAttribute`, where the metaID / `ContainMetaType`
lowering runs); a virtual queryable resolves to a computed column (the provider
aggregates by role). No `column → attribute` downcast — every source resolves its
own columns. A virtual queryable **generates** its columns from the register's
resources (one resource → N columns by role); the virtual provider computes each
(`Turnover → SUM(receipt − expense)`, `ClosingBalance → SUM(...)` as-of the date).

This reshapes step 2 (§22.5): it is **not** "reparameterise the field machinery onto
a metaID-carrying column" — that would drag attribute identity into the interface.
It is **role on the column + resolution at the queryable**: the surface speaks
columns, each provider resolves them to its concrete form, and the attribute field
machinery stays inside the DB provider.

### 22.4c The companion family — main is `this`, subclasses add, generic over a source

A metaobject can't *be* four queryables through inheritance (one object, one base
queryable). So the family is: **the main queryable is always the metaobject itself**
(the current `: public ibBackendQueryable` stays — no migration of catalog / document
/ register-records / constant); a **subclass adds the rest** as owned companion
members. The base vends one — itself; only who has more (registers) adds more.

```cpp
// on ibBackendQueryable (the main is `this`, always):
virtual ibBackendQueryable*               GetVirtualQueryable(ibVirtualTableKind) const { return nullptr; }
virtual std::vector<ibVirtualTableInfo>   GetVirtualTables() const { return {}; }       // enumerate kinds
```

- accumulation register — main (records) + `{ Balance, Turnovers, BalanceAndTurnovers }`;
- chart of accounts — main (accounts) + `{ AccountMovements / balances by account }`;
- catalog / document — main today, but the model is **universal**: a catalog with an
  audit trail vends a `{ Changes / History }` companion (read changes by an element) —
  so a catalog, too, can have **two** queryables;
- data processor — not even a main (does not derive the interface).

The companion family is **not register-specific** — registers are only the first
obvious case. The base vends the main (`this`); *anyone* may add companions, so
`GetVirtualQueryable(kind)` / `GetVirtualTables()` live on the **base interface**, not
a register class. `ibVirtualTableKind` grows past `Balance` / `Turnovers` to
`Changes` / `History` / `AccountMovements` …; the kind says what relation the
companion computes. "Read the changes of X" becomes first-class — any metaobject with
a journal vends a change-queryable, and L3 composes it like any relation (an element
JOIN its history).

**Companions are owned members** (stable pointers vended next to `this`), and each
takes the metaobject in its ctor through a **unified source contract**, NOT a concrete
register class:

```cpp
// the unified material a companion needs from any register-like source:
class ibQuerySource {
public:
    virtual wxString                           MovementTable() const = 0;
    virtual std::vector<ibBackendQueryColumn*> Dimensions()   const = 0;
    virtual std::vector<ibBackendQueryColumn*> Resources()    const = 0;
    virtual ibValueMetaObjectAttributePredefined* Period()    const = 0;
};

// ONE balance companion for ALL register families — generic over the source:
class ibBalanceQueryable : public ibBackendQueryable {
public:
    explicit ibBalanceQueryable(const ibQuerySource* src) : m_src(src) {}
    // columns = Dimensions() + per-Resource role columns (Opening/Closing/Turnover…);
    // MaterializeAttribute + its computing provider all read from m_src.
private:
    const ibQuerySource* m_src;
};

class ibValueMetaObjectAccumulationRegister : /* … */ , public ibQuerySource {
    ibBalanceQueryable  m_balance { this };   // `this` AS the unified source contract
    ibTurnoverQueryable m_turnover{ this };
};
```

This is **dependency inversion, not a template**: the companion depends on the
abstraction `ibQuerySource`; `this` supplies it. One `ibBalanceQueryable` serves the
information / accumulation / accounting register families — no parallel per-family
implementations. The tabular adapter (`ibTabularQueryable`) already proves the shape
(takes its meta in the ctor, pulls through a public API); here the contract widens
from one tabular section to `ibQuerySource`.

The computing provider of a companion shares a skeleton across Balance / Turnovers /
Slice via a **virtual base + template-method hooks** (CRTP-over-the-interface — a
template base that inherits the non-template `ibBackendQueryProvider` and dispatches
hooks statically to `Derived` — is a viable alternative only if the hooks come to need
compile-time traits; for plain SQL-fragment hooks the virtual form is simpler at zero
perf cost, since hooks fire at query-build time, not per row).

### 22.4d Slice queryable — a transient configured relation handed to From()

Concrete shape of the information-register slice (the first virtual table). Key
insight: **a slice returns real records** (the latest / earliest row per dimension
key), so its columns are the register's own — NOT computed columns. Only the *which
rows* differs, by the period bound: last = MAX / "<=", first = MIN / ">=".

**The slice queryable is a self-contained, call-scoped relation.** Its own filters —
the as-of PERIOD and the dimension FILTER — ride in the CONSTRUCTOR ("the filter
before the Where"). You construct one, hand it to `From()`, and L3 reads it like any
source. It does NOT persist on the register (no `m_sliceLast` member, no kind enum, no
vending) — it lives for the one call that built it. Two derived types fix the bound:

```cpp
class ibSliceQueryable : public ibBackendQueryable {            // base — shared slice logic
    ibSliceQueryable(const ibValueMetaObjectInformationRegister* reg,
                     const ibValue& period = {}, const ibValue& filter = {});
    virtual wxString AggregateFn() const = 0;                  // "MAX" / "MIN"
    virtual wxString CompareOp()   const = 0;                  // "<=" / ">="
    bool    IsComputedInRam() const override { return true; }
    ibValue ComputeRows(const std::vector<ibQueryCondition>& extra) const override;  // -> the slice table
    // the 8 navigation methods forward to the register (a slice = register records)
protected:
    const ibValueMetaObjectInformationRegister* m_reg;
    ibValue m_period, m_filter;                                // baked-in scoping
};
class ibSliceLastQueryable  : public ibSliceQueryable { /* MAX / "<=" */ };
class ibSliceFirstQueryable : public ibSliceQueryable { /* MIN / ">=" */ };
```

**One compute, on the register, reached by both runtime and the door.** The slice SQL
(self-join: `MAX/MIN(period) GROUP BY dims` ⋈ the table) is the REGISTER's own knowledge
— a private `ibValueMetaObjectInformationRegister::ComputeSlice(period, filter, agg, cmp)`
method (the ~530-line 4-method duplication is gone). The slice classes are declared in
`informationRegister.h` beside the register (a friend), not a separate header.
`ComputeRows` calls `m_reg->ComputeSlice` with the ctor filters. So:

- **runtime** — the manager's `SliceLast` / `SliceFirst` / `GetLast` / `GetFirst`
  construct a configured slice and read it through L3:
  `ibMetaQueryBuilder().From(&slice).Select(...)`, then materialise the selection
  (`SelectionToTable` for Slice*, `SelectionToRecord` for Get* — the first row).
- **a materialised query / JOIN** — feeds the SAME configured slice into `From()`;
  the door's computed provider runs the SAME `ComputeRows`.

So a runtime call and a composed query hit one identical path — `…Prices.SliceLast`
is just `ibSliceLastQueryable(reg, period, filter)` in `From()`. L4 will construct it
the same way. Discovery (which virtual tables a metaobject offers) is a later concern;
nothing persistent is vended today. (A runtime Slice* re-materialises the small RAM
table once through the selection — accepted for the uniform From()-based path.)

**LANDED (computed virtual table through the one door — RAM-set decision §22.6).**
Reading a slice goes through the **one universal door** and yields ready `ibValue`
rows exactly like a catalog — and **L3 is blind to RAM vs DB, top to bottom**. The
shape:

- `ibBackendQueryable` gains `IsComputedInRam()` / `ComputeRows(extra)` (default
  physical / empty). A slice overrides them — `ComputeRows` builds the slice table
  from the filters baked into the instance's ctor (period + dimension filter); `extra`
  is any further door conditions to compose on top (post-filter, empty for now).
- **The selection's backing is polymorphic, never mixed.** `ibMetaQueryResult` is a
  thin value handle over a `unique_ptr<ibMetaResultSource>`; two source classes —
  `ibDbResultSource` (walks the L2 cursor, materialises through the queryable) and
  `ibRamResultSource` (walks a RAM `ibValueModelTable`, reads ready ibValues by
  metaID) — implement one `Next() / GuidString() / Value()` contract. The result
  forwards and **never branches on the backing** (no `m_isRam` flag, no DB+RAM members
  side by side). Because the `ibValue` now lives inside the RAM source (behind the
  unique_ptr), the result's move is `noexcept` again.
- **The door is blind too.** `Select` / `Upsert` / `DeleteByKey` go through one
  file-local factory `MakeProvider(...)` → a `ibBackendQueryProvider`
  (`ibDbTableProvider` for a physical source, `ibComputedProvider` for a computed one)
  and call `ExecuteRead` / `ExecuteWrite`. The RAM-vs-DB choice lives in **exactly one
  place** — that factory — so the door asks for a provider and runs it without knowing
  the backing, and a computed source's provider is read-only (`ExecuteWrite` = false).
  (The build-once render-cache `Select` overload is the one explicit DB entry — it
  caches rendered SQL, which only physical list-scrolling uses; a computed table has
  no SQL to cache.)

That RAM-backed selection IS the join input the composition step needs — next: the
JOIN node that takes a physical scan + a computed selection.

### 22.4e LANDED — metaobjects VEND a queryable (HAS-A), no longer ARE one

The metaobject families stopped inheriting `ibBackendQueryable`. Instead they implement
a small **interface** `ibBackendQueryableHolder` (`virtual const ibBackendQueryable*
GetQueryable() const = 0`) and **vend a queryable adapter held as a stable member**. The
adapter — one per family, declared beside the metaobject — **owns the L3 navigation**;
the metaobject keeps only **primitives** (`FindObjectByFilter` / `GetPhysicalTableName` /
`IsDataReference` / `guidName`, register `HasRecorder` / `GetRegister*` / dimensions,
constant `GetName`, tabular `GetNumberLine`). The adapter is a `friend` so it can reach
protected primitives.

| family | metaobject | adapter (owns the navigation) |
|---|---|---|
| catalog / document / charts / enums | `ibValueMetaObjectRecordDataRef` | `ibRecordQueryable` |
| information / accumulation / accounting registers | `ibValueMetaObjectRegisterData` | `ibRegisterDataQueryable` |
| constant | `ibValueMetaObjectConstant` | `ibConstantQueryable` |
| tabular section | `ibValueMetaObjectTableData` | `ibTabularQueryable` |

- the interface lives on **the families, NOT on `ibValueMetaObject`** (a plain metaobject
  is not a holder; L4 reaches a queryable through the family / a cast to the interface);
- every door call site reads `From(meta->GetQueryable())` (≈30 sites converted);
- the door internals (`ibMetaIRBuilder`, `ibDbTableProvider`, `ibDbResultSource`) speak
  only the `ibBackendQueryable` they got from `From` — i.e. the adapter;
- a **slice** is the register's records, so its navigation forwards to the register's
  own main queryable (`m_reg->GetQueryable()->…`), where that logic now lives;
- tabular sections fit the SAME interface — the meta vends `ibTabularQueryable` (the
  adapter is parent-agnostic; the parent uuid is a `WhereKey` filter), so a transient
  (data-processor / report) parent simply never queries it.

### 22.5 Migration order (incremental, each step green)

1. **DONE — extract `ibDbTableProvider`** = today's path verbatim (`BuildPageIR` /
   `BuildExternal` / `ExecuteIR`), behind `ibBackendQueryProvider`. The door's
   `Select` delegates; the read lowering left the door header for the provider. Pure
   relocation, green. (`ExecuteWrite` seam added; the write path wires next.)
2. **Door speaks columns — by role + resolution, NOT a metaID reparam (§22.4b).**
   The verbs reference columns; the column carries name / type / **role**, no
   metaID. Each provider **resolves** a referenced column through its queryable: the
   DB provider resolves to an attribute (`ResolveAttribute`) and runs the attribute
   field machinery (metaID / `ContainMetaType` stay there); a virtual provider
   resolves to a computed column. So `ibValueMetaObjectAttributeBase` leaves the
   door's surface without dragging attribute identity into the column interface and
   without any `column → attribute` downcast.
3. **Aggregate IR** — `SUM` / `MAX` / `GROUP BY` / period `>=/<=` / nested
   subqueries in the IR, exercised by a first `ibVirtualTableProvider`
   (information-register `SliceLast` is the simplest specimen).
4. **Retire the raw register-manager SQL** — accumulation / accounting balances &
   turnovers move onto `ibVirtualTableProvider`; the hand-rolled `sqlQuery +=
   GetCompositeSQLFieldName(...)` strings in `*RegisterManager_impl.cpp` go.
5. **`ibTempTableProvider`** — when a query needs a materialised intermediate
   (value-set joins, report scratch).

### 22.6 Open decisions (for review)

- **Write through a provider, or keep writes on a dedicated path?** Writes only
  ever hit the real DB table; `ExecuteWrite` on the DB-table provider is tidy, but
  a separate `ibMetaWriter` is an option if read/write providers diverge.
- **`Virtual(kind, params)` as a door verb vs. a distinct `ibMetaQueryBuilder`
  flavour** (e.g. `ibMetaBalanceQuery`) — the params differ enough per kind
  (period, dimensions, condition on resources) that a typed builder per virtual
  kind may read better than one polymorphic `params` bag.
- **Does `ibTempTableProvider` own a real temp table (driver `CREATE TEMP`) or a
  RAM-materialised set queried in C++?** Driver temp tables vary across the five
  backends; a RAM set sidesteps the dialect matrix but loses SQL-side filtering.

---

## 23. L4 — the text query language (L4-1) + the LINQ seam (L4-2)

§14 named two L4 front-ends lowering into the one L3 path. **L4-1 — a text query
language** was built first (greenfield, simpler than LINQ, and it lays the
foundation L4-2 reuses); **L4-2 — LINQ push-down** landed on top of it (§23.5).

### 23.1 Decisions
- **Syntax:** canonical English `SELECT/FROM/WHERE/JOIN/GROUP BY/ORDER BY/TOTALS`, but
  keywords live in a **localizable table** (`query/queryKeywords.h`) — a UK/RU spelling
  set is added later mapping the SAME `ibQueryKeyword` enum, so the grammar never
  changes (mirrors VES/CES → one path). Metaobject / attribute names are taken verbatim
  (often Cyrillic). The query keyword table is **separate** from the script lexer's
  `s_listKeyWord`, so query keywords never collide with the script language.
- **Parser:** a hand-written recursive-descent parser on the OES lexer idioms — **no new
  dependency**. (hyrise/sql-parser was a grammar reference only; Flex/Bison + a new
  dependency were rejected.)
- **Runtime entry:** a `Query` value object — `New Query(text)`, `q.SetParameter(name,
  value)`, `q.Execute()` — parsed once (AOT-cacheable), executed through the L3 door.

### 23.2 The pipeline (all in `backend/query/`)
```
text ─▶ ibQueryLexer ─▶ tokens ─▶ ibQueryParser ─▶ ibQuerySelect AST
                                                        │  (names unresolved)
                              SetParameter values ──────┤
                                                        ▼
                              ibQueryLowering.Execute ─▶ ibDataQueryBuilder (L3 door) ─▶ ibDataQueryResult
                                                        │   (names → queryable/columns)
                                                        ▼
              Query.Execute() ─▶ QueryResult ─▶ .Select() ─▶ QuerySelect ─▶ Next()/Select()(drill)/Next()…
                                 (ibValueQueryResult)        (ibValueQuerySelect — flat cursor OR TOTALS tree)
```
| Module | File | Role |
|---|---|---|
| keywords | `queryKeywords.h` | localizable `ibQueryKeyword` table |
| lexer | `queryLexer.{h,cpp}` | `ibQueryLexer : protected ibTranslateCode` — reuses the UTF-8 char primitives; own token stream + `&param` |
| AST | `queryAst.h` | `ibQuerySelect` + expression tree (L2/L3-free) |
| parser | `queryParser.{h,cpp}` | recursive descent; `OR<AND<NOT<compare` precedence |
| lowering | `queryLowering.{h,cpp}` | names → queryable / columns (source via the **factory** `query_sources`, columns via `ResolveColumnByName`); AST verbs → door verbs; runs |
| source factory | `queryableFactory.{h,cpp}` | `ibQueryableFactory` on `appData` — descriptors that CREATE a queryable per namespace (§23.6) |
| runtime | `system/value/valueQuery.{h,cpp}` | `ibValueQueryExec` ("Query", `Execute`) → `ibValueQueryResult` ("QueryResult", `Select`) → `ibValueQuerySelect` ("QuerySelect" — the selection cursor, flat OR TOTALS): `Next`/`Reset`/`Field`/`HasChildren`/`Select`(drill)/`Total`/`Level` |

> The C++ class is `ibValueQueryExec` — **`ibValueQuery` is already the LINQ
> chain wrapper** (`compiler/procUnitLinq.cpp`). Script-visible names: `Query` /
> `QueryResult` / `QuerySelector`.

### 23.3 Grammar
```
package  := statement { ';' statement }            (a trailing ';' is allowed)
statement:= DROP name                              -- release a temp table the package made
          | select [FOR UPDATE]
select   := SELECT [ALLOWED] [TOP n] [DISTINCT] selList [INTO name] FROM source { join }
            [WHERE predicate] [GROUP BY exprList [HAVING predicate]]
            [ORDER BY orderList] [TOTALS aggregate {',' aggregate} BY totalDim {',' totalDim}]
proj     := (aggregate | columnPath) [ [AS] alias ]
aggregate:= (SUM|MIN|MAX|AVG) '(' columnPath ')' | COUNT '(' ('*'|columnPath) ')'
predicate:= andE {OR andE};  andE := notE {AND notE};  notE := NOT notE | comparison
comparison := primary [ cmpOp primary | [NOT] LIKE primary
                      | [NOT] IN '(' … ')' | IS [NOT] NULL | [NOT] BETWEEN primary AND primary ]
totalDim := columnPath [HIERARCHY | ELEMENTS]
```
**The four clauses added 2026-08-06 with the query constructor** ([query-constructor.md](query-constructor.md) §5c):
`ALLOWED` — an access refusal yields an EMPTY read instead of raising (never a default: a report
that quietly shows fewer rows is a report that lies quietly). `FOR UPDATE` — the select HOLDS the
rows it returned until the transaction ends, rendered by the driver's own `m_rowLockSuffix`.
`INTO <name>` — materialise the result as a temp table and yield the ROW COUNT instead of a table;
later statements of the same package select `FROM <name>` (a temp table is a BARE name — it has no
metaclass, so `ResolveSource` asks the auxiliary registry before the factory). `DROP <name>` —
release one early. A package runs through `ibQueryLowering::ExecutePackage`, whose temp registry
lives for the WHOLE package rather than one execution, and answers with results BY POSITION;
the script surface is `Query.ExecuteBatch()`, with `Query.Execute()` returning the first of them.

`TOTALS … BY …` is the hierarchical-subtotals clause: aggregates
between `TOTALS` and `BY` roll in-place at each level, the `BY` list is the dimension
levels in order → door `Totals().Sum(col).TotalBy(col, dim).SelectTotals()`. Distinct
from flat `GROUP BY` (aggregates in `SELECT`, one row per group).

### 23.4 Lowering — MVP subset vs. the door
The parser accepts the full grammar; the lowering realizes the subset the L3 door can
execute **today** and throws a clear "not yet supported" (with the source span) for the
rest, growing as the door grows:
- **Supported:** single source `Kind.Name` (catalog / document / register records),
  projected columns + reference **dot-walk** (`SelectPath`), the **full boolean WHERE**
  (`= <> < <= > >= LIKE BETWEEN` plus `OR / AND / NOT / IN / IS NULL` and their negations),
  ORDER BY, DISTINCT, flat **GROUP BY + aggregates + HAVING** (ordered ops), a
  **subquery source** (`FROM (SELECT …) AS s`, nested, via `ibSubqueryQueryable`), and
  **JOIN** (`[INNER|LEFT|RIGHT|FULL] [OUTER] JOIN Kind.Name AS j ON a.x = j.y`, plus a CROSS join
  via `ON TRUE`; auto-join by reference when `ON` is omitted). Full read (no `LIMIT`) via
  `Execute(ibReadPageRequest{})` (`m_count<=0` = unbounded), streamed by `QueryResult.Next()`.
  Reference **dot-walk** is realized across projection / WHERE / ORDER / GROUP BY / aggregate inputs,
  including a COMPOSITE reference at any segment and the register RECORDER — see **§23.8 / §23.9**.
  - **Register virtual tables resolve as sources** too — `FROM AccumulationRegister.Goods.Balance`
    / `.Turnovers`, `FROM InformationRegister.Prices.SliceLast` — through the same factory
    (the companion descriptor registers under the composite `Object.Table` name).
    **Source-call args** carry the period / filter: `FROM …Goods.Balance(&Period, &Filter)` —
    the parser collects them on `ibQuerySource.m_args`, the lowering evaluates them to `ibValue`
    and hands them to the descriptor's `CreateQueryable(paParams, n)` (the companion copies them
    by value). No args = default (empty) period / filter.
  - **Multi-source column resolution.** A column is `alias.col` (the aliased source) or a bare
    `col` (the first source that owns it, primary first). JOIN / subquery queries project plain
    columns explicitly (`Select(col, alias)`); a single source reads them straight off the result.
  - **WHERE is a predicate TREE, lowered through L2.** L4 builds an L3 `ibQueryPredicate`
    tree (`queryable.h`: Leaf / And / Or / Not / IsNull; IN → `Or(Eq …)`, BETWEEN →
    `And(>=, <=)`, the negations wrap in `Not`) and hands it to the door via
    `ibDataQueryBuilder::Where(tree)`. The provider lowers the tree to the L2 IR
    (`ibMetaIRBuilder::BuildPredicateExpr` → `ibBinOp(Or/And)` / `ibNot` / `ibIsNull`),
    AND-folded with the flat verb conditions. So the door — not just L4 — now carries full
    boolean WHERE; LINQ (L4-2) lowers through the same tree.
- **Boolean WHERE with JOIN — co-located only.** `OR / NOT / IN / IS NULL` in a WHERE that has a
  JOIN executes when the join **co-locates** to one server-side SELECT: the lowering routes a flat
  AND-of-simple WHERE to the door's per-leaf verb conditions (works in both join paths) and a boolean
  WHERE to the predicate tree (`Where(tree)`); the provider's `BuildColocatedPredicate` lowers the
  tree per-leaf-qualified (`ColocatedOwner`, so an `OR` spanning two leaves is ONE server expression).
  When the join can't co-locate (RAM stitch), the boolean WHERE is applied as a **POST-COMPOSE RAM
  filter** over the joined rows (`RamFilter`, `queryProvider.cpp`) — the per-leaf push the stitch cannot
  do is simply done after the join instead. Only a **dot-walk LEAF inside** such a tree still errors: it
  needs a join the composed table does not carry, and erroring beats dropping an OR branch (which would
  silently WIDEN the filter).
- **Reference dot-walk in WHERE / ORDER BY** — `WHERE Producer.Region = &R`, `ORDER BY Producer.Name`
  — reuses the L3 dot-walk crown: the same auto-join `SelectPath` builds (the `_RRRef` self-reference
  LEFT-join chain, aliased `dw0/dw1…`, deduped by path prefix). The door gained `Where(path, …)` /
  `WhereCompare(path, …)` / `OrderBy(path, …)` (the condition / sort carries `m_path`, the leaf in
  `m_col`); `BuildPageIR` builds one join chain for projection + filter + sort paths and qualifies each
  leaf by its join alias (filters via `BuildConditionExpr` on the target queryable, sorts inline in
  effective order). **Single-source** — a multi-source (JOIN) dot-walk filter / sort errors clearly (the
  composer has no per-leaf dot-walk join yet). A single-source AGGREGATE now DOES support dot-walk in
  `GROUP BY` and aggregate inputs (`ExecuteAggregate`, **§23.9**); a dot-walk WHERE / ORDER stays on the
  non-aggregate read path. Composite-at-any-segment + the register recorder: **§23.8**.
  - **Dot-walk works inside a boolean WHERE too.** `WHERE Producer.Region = &R OR Producer.Kind = 3`
    executes: the predicate-tree leaf (`ibQueryCondition`) carries `m_path`, and `BuildPageIR` lowers the
    tree path-aware (`lowerTree`) — a path leaf joins via `resolvePath` and qualifies by its alias, a
    plain leaf by `mainQual`, the OR folded over both as ONE server expression. An unresolvable path leaf
    throws (never a dropped OR branch). Single-source non-aggregate only — same gate as the flat filter.
  - **Dot-walk also in `IN` and `IS NULL`.** `Producer.Region IN (&A, &B)` (every Eq shares the path →
    one join) and `Producer.Region IS NULL` (the `IsNull` tree node carries `m_path` too); the
    provider's `lowerTree` resolves both via `resolvePath`.
- **Parser is COMPLETE; the lowering realizes the executable subset.** *(State AT THIS MVP write-up —
  all four have since landed; see §23.4.)* The parser accepts **arithmetic** (`+ - * / %`, standard
  precedence), **CASE** (`CASE WHEN … THEN … ELSE … END`), **UNION [ALL]**, and **IN (subquery)** — but
  the column-based L3 door did not yet execute computed expressions or set operations, so the lowering
  threw a clear "parsed but not yet executed" for these. The AST carried them
  (`ibQueryExprKind::Arith` / `Case`, `ibQuerySelect::m_unions`, `In.m_subquery`) so the later
  work — and LINQ — reuse the same tree.
- **Hierarchical TOTALS EXECUTE — one unified selection.** `SELECT … TOTALS SUM(Qty) BY Warehouse,
  Goods` runs: `ibQueryLowering::ExecuteTotals` builds the door's `Totals()`/`TotalBy()`/in-place
  aggregates, reads ONE snapshot, and `result.Select(ByGroupsHierarchy)` folds it into a walkable
  `ibSelector`. The crucial point (the user's): **a flat list and a TOTALS tree are the SAME selection
  type** — `Query.Execute()` → a `QueryResult` (`ibValueQueryResult`); `res.Select()` → a `QuerySelect`
  (`ibValueQuerySelect`), the walkable selection. A `QuerySelect` wraps the forward cursor for a plain
  SELECT (dot-walk projections read by alias) OR the `ibSelector` for TOTALS, and the script walks
  EITHER the same way: `Next()` / `Reset()`, output columns read DIRECTLY as attributes (`s.ColumnName`
  — no `Field()`), `Level()` (a method — node depth), `HasChildren()`, `Select()`
  (descend a node → a child `QuerySelect`, recursive), `Total(name)` (grand total). A flat list is just
  a single-level selection. (docs §22.1b — one read, one snapshot; detail and subtotal cannot skew.)
- **Computed columns EXECUTE — arithmetic & CASE in the projection.** `SELECT Qty * Price AS Total,
  CASE WHEN Qty > 100 THEN 'bulk' ELSE 'unit' END AS Kind FROM …` runs: the lowering builds an L3
  `ibQueryColumnExpr` tree (`queryable.h` — Column / Const / Arith / Case, WHEN reusing the predicate
  tree), the door's `SelectExpr(expr, alias)` carries it, and the provider's `BuildColumnExpr`
  (`dbTableProvider.cpp`) lowers it to the L2 IR (`ibBinOp` Add/Sub/Mul/Div/Mod, `ibCase`) and projects
  it `AS alias` in `BuildPageIR`. Read back by alias. **Non-aggregate only:** a single DB source projects
  it server-side, a JOIN / computed source evaluates it per joined row in the composer
  (`EvalColumnExprRow`); only a computed column **over aggregates** errors. Arithmetic / CASE in a WHERE
  and as an aggregate argument execute too (single source → SQL, computed source → RAM per row).
- **UNION EXECUTES.** `SELECT … UNION [ALL] SELECT …` runs: each branch's core is wrapped as an
  `ibSubqueryQueryable` (`WrapSelectAsQueryable`), stacked with `From(b0).Union(b1)…`, and the composer
  realizes the stack — ONE server-side `UNION` / `UNION ALL` when every branch is a real DB table
  (`CanColocateUnion`), else the RAM stack (columns matched BY NAME across branches). Each branch carries
  its own UNION-vs-ALL flag, so plain `UNION` dedupes at its operator and `UNION ALL` keeps duplicates.
  The trailing `ORDER BY` applies to the whole. A branch may not use JOIN / TOTALS yet.
- **IN (subquery) EXECUTES** — `WHERE x IN (SELECT y FROM …)` materialises the (uncorrelated) inner
  SELECT's single column eagerly into a value list, then lowers as `Or(x = v …)` (the same path as a
  literal IN list; dot-walk leaf supported). An empty result → a contradiction leaf (matches nothing);
  `NOT IN ()` → everything. The inner runs once through a local `ibSubqueryQueryable`.
- **Aggregate subqueries EXECUTE** (`FROM (SELECT cat, SUM(x) AS s … GROUP BY cat)`).
  `ibSubqueryQueryable` detects the aggregate shape from the builder, exposes the group keys plus a
  synthetic column per aggregate alias, runs `SelectAggregate` in `ComputeRows`, and RAM-post-filters the
  outer's pushed-down conditions.
- **Parsed but not yet executed (clear error)** — the residual tail only: an arithmetic / CASE expression
  in a WHERE, an aggregate argument or a projection **across a JOIN's leaves** (`GateComputedExpr` — the
  RAM stitch has no cross-leaf expression evaluator), a computed column **over aggregates**, and a
  dot-walk LEAF inside a boolean WHERE over a non-co-located JOIN. Access is **SELECT-only** for user
  text (§14).

### 23.5 L4-2 — LINQ push-down (**LANDED v1, 2026-06-11**)

The seam became the second front. The pipeline, end to end:

**Compile time — the lambda recorder** (`compiler/lambdaQueryAst.{h,cpp}`):
`CompileLambdaExpression` (compileCode.cpp) remembers the lambda body's lexeme span; for a
single-parameter lambda, `ibBuildLambdaQueryAst(lexems, from, to, rowParamName)` re-parses
those lexemes into a ready **L4-1 `ibQueryAstExpr`** stored as
`ibByteFunction::m_lambdaExprAst`. Grammar: `[ '{' ] Return expr [ ';' ] [ '}' | EndFunction ]`,
expression subset = Column (the row parameter's member chain, real-cased via the lexeme's
`m_valData`), Literal (incl. folded unary minus), Param (a captured outer identifier),
Arith (`+ - * / %`), Compare (`< > <= >= = == <> !=` — pairs fuse only when source-adjacent),
Logical (`And`/`Or` keywords — `&&`/`||` do not exist in the language; a bare `|` is the
lexer's string-continuation marker), Not (`Not` keyword or CES `!`). **Conservative
bail-out:** anything else — method calls, member access on a captured value, a bare row
parameter, multiple statements — records `null` (a false "translatable" would mean wrong
rows; `null` just means the RAM path).

**AOT v14** (`byteCodeAOT.cpp`): the recorded AST serialises with the function (presence
byte + recursive node payload). Writing is gated by `AstSerializable` (outside the subset →
absent), reading validates node kinds and caps depth at 256 (failure = a clean cache miss).
An AOT hit KEEPS the push-down — a hit is the production norm, so the feature survives it.

**Lowering reuse** (`query/queryLowering.{h,cpp}`): public `LowerLambdaPredicate` /
`LowerLambdaColumnPath` / `LowerLambdaColumnExpr` are thin wrappers over the SAME file-local
`BuildWherePredicate` / `ResolvePath` / `BuildColumnExprFromAst` (gated by `GateComputedExpr` —
single physical source) the text language lowers through (sources = the one queryable, dot-walk allowed
on a DB source only); any lowering error returns `nullptr` → RAM. Captured outer identifiers
are Param nodes resolved BY NAME from the lambda's captured frames at dispatch time.

**`ibValueQueryable`** (`system/value/valueQueryable.{h,cpp}`, CLSID `VL_QRBL`) — the value
the chain flows through. Inert by contract: no property/method surface, watch / `GetString`
NEVER executes (renders `"Queryable(source | ops)"`). `DispatchLinqMethod` is the only
entry; every op clones the link (the chain is immutable, shared prefixes are safe):
- **Folded into the door:** `Where` (lambda AST → predicate), `OrderBy[Descending]`
  (column path), `Take` (min-fold).
- **Terminals:** `Count` (door aggregate), `Any` (page-1 probe), `First`/`FirstOrDefault`,
  `ToArray`, `ToTable` (a NEW pipeline method — materialises into a value table; a clear
  error on plain iterables).
- **Anything else** = `MaterialiseThenRam`: execute the folded prefix, wrap the rows in an
  Array, re-dispatch the op on the stock RAM machinery — the RAM floor. The dual-compile
  promise holds: translatable prefix server-side, residual in RAM.
- **Row shape:** a single-column-PK source yields references; otherwise (registers,
  `Data.From`) the row is a structure of the source's columns — register iteration unlocked.
- `CreateIterator` streams pages (Foreach over a Queryable never materialises the set).

**The `Data` global** (`moduleManager/moduleManagerDataUnit.cpp`) — the queryable-source
root, the exact mirror of the `Metadata` unit: nine kind namespaces (`Constants`,
`Catalogs`, `Documents`, `Enumerations`, `InformationRegisters`, `AccumulationRegisters`,
`ChartsOfCharacteristicTypes`, `ChartsOfAccounts`, `AccountingRegisters`) vend
`Name → Queryable` through `ibBackendQueryableHolder::GetQueryable()` — the SAME source
registry the text language reads (one world). `Data.From(valueTable)` wraps the existing
`ibTempTableQueryable`: an in-memory value table as a first-class source, joinable against
DB sources through the composer/promotion machinery. Vending is lazy by contract — building
the namespace reads nothing. Block LINQ `From s in Data.Catalogs.X select {…}` validated
live.

**Tests:** `tests/test_lambdaRecorder.cpp` — 16 cases, pure text → lexemes → AST shape +
bail-outs (no DB, both code styles). Full suite 444/444; full `Debug|x86` clean.

**Open — phase D:** golden parity RAM-vs-pushed (the same chain over `Data.X` and over a
materialised array must agree row-for-row). **Deferred post-v1:** manager-as-LINQ sugar
(`Catalogs.X.Where(λ)` delegating to the queryable), source arguments in the text language,
rooted `FROM Data.*` + plural factory aliases, `Metadata.*` as queryable relations.

### 23.6 The queryable-source factory (`query/queryableFactory.h`)

Source resolution is NOT a hard-coded `switch` in the lowering — it goes through a
**factory of descriptions of how to create queryables**, owned by `ibApplicationData`
(`GetQueryableFactory()`, the `query_sources` macro; nullptr pre/post-appData — same
ownership pattern as `ibLockManager`, token-gated ctor). This is the source-agnostic
federation seam (§22.0): the built-in metaobject families and any plugin / external-DB /
temp source register here without touching the lowering.

**The query language covers ONLY the relational metaclasses:** records with a data-reference
(catalogs / documents / charts of characteristic types & accounts / enumerations), registers,
and constants. Reports & data processors register no descriptor → a query against them simply
fails to resolve.

- **`ibQueryableSourceDescriptor`** — a source DESCRIPTION identified by `(namespace, name)`:
  `GetNamespace()` / `GetName()` + `CreateQueryable(ibValue** params, long count)` which CREATES
  the queryable from the params (the `ibValue::Init` idiom) — no separate `Init()` setup step.
  The factory is a **non-owning** registry of descriptor pointers — `Register(descriptor*)` /
  `Unregister(descriptor*)`; the descriptor is OWNED BY the metaobject.
- **`ibMetaSourceDescriptor<TQueryable, TMeta>`** (the template, per metadata) — **REPLACES the
  metaobject's former plain `ibXxxQueryable m_queryable{this}` field**: it CONTAINS the queryable
  (built from the metaobject as before) AND carries the L4 source identity. The metaobject holds
  ONE field — this descriptor — and `GetQueryable()` forwards to it (`m_queryable.GetQueryable()`);
  `CreateQueryable` (the factory path) returns the same; `GetNamespace`/`GetName` come from the
  metaobject.
- **Base descriptor registration (wired):** the family base registers its descriptor field in
  `OnAfterRunMetaObject` — `m_metaData->RegisterSource(&m_queryable)`, a **facade on the metaobject's
  OWN config** (registration is per-config, into `ibMetaData`'s own `ibMetaQueryableFactory`, NOT one
  global registry — see "Per-config source factory" in `dynamic-list.md`) — after checking
  `!(flags & onlyLoadFlag)` (the designer's saved baseline loads its objects load-only — skip) —
  and `m_metaData->UnregisterSource(&m_queryable)` in `OnBeforeCloseMetaObject`. Wired in the
  bases: `ibValueMetaObjectRecordDataRef` (catalogs / documents / charts / enums chain up here),
  `ibValueMetaObjectRegisterData`, `ibValueMetaObjectConstant`, and **`ibValueMetaObjectTableData`
  (tabular sections)** via the custom `ibTabularSourceDescriptor` — a tabular is a sub-object, so
  its `(namespace, name)` is PARENT-QUALIFIED (`Document.Expense.Goods`); a transient parent
  (report / data processor) yields an empty namespace, so it is never registered.
- **Custom descriptors per concrete register (LANDED for information / accumulation)** — a register
  ADDITIONALLY owns CUSTOM virtual-table descriptors registered at EACH CONCRETE register level
  (each kind has different virtual tables): the **information register** owns
  `ibInfoRegisterSliceDescriptor<ibSliceLastQueryable>` / `<ibSliceFirstQueryable>` (registered as
  `<Register>.SliceLast` / `.SliceFirst`); the **accumulation register** owns
  `ibAccumRegisterBalanceDescriptor` / `ibAccumRegisterTurnoverDescriptor` (`<Register>.Balance` /
  `.Turnovers`). They are register FIELDS, registered in the concrete register's `OnAfterRun`
  (alongside the base records descriptor) and dropped in `OnBeforeClose`. Reached as a 3-segment
  source `AccumulationRegister.Goods.Balance` (the lowering joins segments[1..] into the name).
  `CreateQueryable(paParams, count)` BUILDS the call-scoped companion (`ibBalanceQueryable` /
  `ibSliceLastQueryable`) from the params (as-of period / [begin,end] range / dimension filter) and
  OWNS it in a `unique_ptr` member (valid through the synchronous Execute, which materialises the
  companion's RAM rows into the result). **Open:** parsing SOURCE ARGUMENTS in the text query
  (`…Goods.Balance(&Period)`) — until then the companion builds with empty params; and the shared
  descriptor's single-companion ownership is not safe for concurrent virtual-table queries (a
  result-owned companion is the hardening). Accounting register: base records descriptor only so far.
- **External sources register themselves separately** via `Register(descriptor*)` at their own
  load path — independent of the metaobject lifecycle.

### 23.7 Status (2026-06-10, BUILT + first runtime validation)
> The dot-walk + aggregation/totals crown has since advanced well past this snapshot — its **realized
> mechanism** is documented in **§23.8** (reference dot-walk: single / typed-empty / composite-at-any-
> segment / register recorder) and **§23.9** (GROUP BY + aggregate inputs over dot-walk, TOTALS BY
> dot-walk with synthetic columns, computed measures, the const CAST). This subsection is the original
> first-build status, kept for the journey.

**Builds GREEN** — a clean full-solution `Debug|x86` (`/t:Rebuild`) is green after two fixes the
unbuilt arc had latent: (1) `queryableFactory.h` included `clsid.h` BEFORE wx (clsid.h uses
`wxLongLong_t`/`wxString`) → it now leads with `backend/backend_core.h` (wx set up first); (2)
`queryLowering.cpp` used `OutputColumn` unqualified in its anonymous namespace → a `using
OutputColumn = ibQueryLowering::OutputColumn;`. NOTE on incremental builds: this arc touches widely
included headers (`queryAst.h`, `queryable.h`, `dataQueryBuilder.h`) — an INCREMENTAL build leaves
some DLL/exe TUs on the old struct layout → an ABI split at the DLL boundary (the symptom: the
Designer did not list the `Query` type, and a runtime `New Query` mis-read). A **clean** rebuild
fixes it; after one, the Designer registers `Query` and **`New Query("SELECT … FROM Catalog.X").Execute()`
runs at runtime without crashing** — the first real end-to-end validation (parse → lower → door →
provider → result).

Tests: lexer + the L3 vocabulary (`tests/test_queryL4Lexer.cpp`, `test_queryL3Vocab.cpp` — predicate
tree + column-expr factories) pass green. The L4 **parser** golden tests (`test_queryL4Parser.cpp`)
parse correctly but the **x64 gtest harness** AVs in the destructor of a default `ibValue` embedded in
`ibQueryExpr` (`ibQueryExpr::~ibQueryExpr` → `wxMemoryBuffer::DecRef`) — a harness-only quirk of that
build (the x86 product builds the same AST nodes via `New Query` and destroys them cleanly; sibling
tests `QueryTotals`/`QueryComposer` that hold backend-built `ibValue`s pass). So: parser LOGIC is
proven by the runtime; the unit-test crash is a separate x64/gtest investigation, not a product bug.
Registered in `backend.vcxproj` + the CMake test list.

**Landed since the MVP write-up (still pre-build):**
- **Full boolean WHERE on the L3 door, lowered through L2** — `ibQueryPredicate` tree
  (`queryable.h`) + `ibDataQueryBuilder::Where(tree)` + `ibMetaIRBuilder::BuildPredicateExpr`
  (`dbTableProvider.cpp`, shared per-leaf body `BuildConditionExpr`, combined via `BuildWhere`).
  L4 builds the tree in `BuildWherePredicate` (`queryLowering.cpp`). `OR / NOT / IN / IS NULL`
  + negations now execute — no longer a "not yet" error.
- **Subquery source** `FROM (SELECT …) AS s` — parser (`ParseSource` / `ParseSelectStatement`),
  lowering (`ResolveFrom` recursion + `PopulateBuilder` shared with the top level, wrapped in
  `ibSubqueryQueryable`, owned for the Execute lifetime). Aggregate subqueries still error.
- **JOIN** — `[INNER|LEFT] JOIN Kind.Name AS j ON a.x = j.y` (single-equality ON; the provider
  qualifies each key by its owning leaf, so operand order is free) + auto-join by reference when
  `ON` is omitted. The resolver went **multi-source**: `ibSourceBinding` list (1 = single source,
  N = JOIN), `ResolveColumnSingle` / `ResolvePath` pick the source by alias prefix or by ownership.
  JOIN / subquery queries project plain columns explicitly via `Select(col, alias)`; a single
  source still reads them off the result column directly. JOIN WHERE lowers to the door's flat
  verb conditions (`LowerFlatWhere`) — AND-chain of comparisons / LIKE / BETWEEN; the boolean
  tree stays a single-source feature for now.
- **Register virtual tables as a source** — `AccumulationRegister.X.Balance` / `.Turnovers`,
  `InformationRegister.X.SliceLast` resolve through the factory (companion descriptor under the
  composite name). **Source-call args** `…Balance(&Period, &Filter)` parse onto `ibQuerySource.m_args`
  and lower to the descriptor's `CreateQueryable(paParams, n)` (companion copies by value); no args =
  default period / filter.
- **Boolean WHERE across a co-located JOIN** — `BuildColocatedPredicate` (`dbTableProvider.cpp`)
  lowers the predicate tree per-leaf-qualified into the one server-side SELECT; the lowering routes a
  flat AND-of-simple JOIN WHERE to the verb conditions and a boolean one to `Where(tree)`
  (`IsFlatAndWhere`). The RAM-stitch composer path **errors** on a predicate tree it can't push per
  leaf (`queryProvider.cpp`) — honest, not a silent under-filter.
- **Reference dot-walk in WHERE / ORDER BY** — `ibQueryCondition` / `ibQuerySortItem` gained `m_path`;
  the door gained `Where(path,…)` / `WhereCompare(path,…)` / `OrderBy(path,…)`. `BuildPageIR` factors a
  `resolvePath` join-builder shared by projection + filter + sort (one `_RRRef` LEFT-join chain, prefix
  deduped); path filters lower via `BuildConditionExpr` on the leaf's target queryable qualified by the
  join alias, path sorts emit inline in effective order. `BuildFilterPredicate` / `BuildSortKeys` /
  `BuildAnchorPredicate` skip `m_path` items (handled by `BuildPageIR`). Single-source non-aggregate
  read only.
- **Dot-walk inside a boolean WHERE** — the predicate-tree leaf (`ibQueryCondition`) carries `m_path`;
  `BuildPageIR.lowerTree` (a recursive `std::function`) lowers the tree path-aware — a path leaf joins
  via `resolvePath` + qualifies by alias, a plain leaf by `mainQual` — so `…Producer.Region = &R OR
  Producer.Kind = 3` is one server-side SELECT. `PredicateHasPath` flags such a tree into the dot-walk
  path; an unresolvable path leaf throws (no dropped OR branch). `IN` / `IS NULL` carry the dot-walk
  path too (§23.4).

**Not yet built** — expect minor signature fixups on the first `Debug|x86`. Open wiring
point: the metadata open/close hook → `RegisterBuiltins()` / `Clear()`.

---

### 23.8 Reference dot-walk — the realized resolution mechanism

> This is the crown of the read path: `A.B.C` (a chain of reference attributes ending in a leaf
> attribute) becomes a chain of `LEFT JOIN`s on the `_RRRef` self-reference, the leaf read off the
> joined target. All of projection, `WHERE`, `ORDER BY`, `GROUP BY`, and aggregate arguments funnel
> through ONE builder so the joins are built once and shared.

**The join builder — `ibRefJoinChain` (`dbTableProvider.cpp`).** A small class extracted from
`BuildPageIR` and shared with `ExecuteAggregate`, so the read and the aggregate resolve a path the SAME
way (no duplication):
- `Resolve(path, &alias, &target)` — walk the reference segments (all but the leaf), appending one
  `LEFT JOIN <target> AS dwN ON owner.<ref>_RRRef = dwN.<selfRef>_RRRef` per segment, **deduped by path
  prefix** (a prefix joined once is reused across projection / filter / sort). Returns the leaf's join
  alias + its target queryable. Fails on a non-single-target (composite) segment.
- `AddLeftJoin(table, leftQual, leftField, rightField)` — one raw (non-deduped) join, returns its alias;
  used by the composite fork (below).
- `From()` — the assembled FROM tree (handed to `q.From()` once, after all paths are resolved — the
  builder MUTATES the tree, so it must run before From).

**Typed-empty, not SQL NULL.** A dot-walk through an EMPTY or BROKEN reference does not match the LEFT
JOIN → the joined column is SQL NULL. But a typed-empty reference has EMPTY attributes (the object-model
rule `EmptyRef.Attr` = the attribute type's empty value), so a **scalar** leaf is wrapped
`CASE WHEN <col> IS NULL THEN <typed-empty-const> ELSE <col> END` (the const is
`ibValueTypeDescription::AdjustValue(leaf->GetTypeDesc())`). A **reference / enum / composite** leaf is
projected as its FULL field spread (`<alias>_TYPE/_RTRef/_RRRef/…`) under the alias prefix and reassembled
by `GetValueColumn` on read — a non-matching join leaves the fields null, which `GetValueColumn` reads as
the type's typed empty on its own (and an untagged `_TYPE`=0, e.g. a data-reference, yields the column's
typed empty — never UNDEFINED, and never reads a sub-field the column lacks).

**Composite reference at ANY segment — the register RECORDER.** A composite (multi-type) reference points
to N target types (a register's **recorder** is the headline case: 15+ document types). A
field pulled through it (`SELECT Recorder.SomeField`) commonly exists on only ONE type. The resolution
is RECURSIVE (`pathCompositeScalarExpr` in `BuildPageIR`, mirrored by `ExecuteAggregate`):
- Walk from the main table. A SINGLE-target ref → one `LEFT JOIN`, continue. A COMPOSITE ref → **FORK**:
  one `LEFT JOIN` + a recursive tail per target type. Each segment is re-resolved BY NAME per branch (the
  path columns were resolved against the *representative* first type at lowering — `ResolvePath` allows
  composite at any segment).
- Each branch contributes its RAW leaf field (NULL on a non-matching join); the whole is
  `COALESCE(branch1, …, branchK, <typed-empty>)`. The `_RRRef` join key carries the target's metaID, so a
  row matches AT MOST one branch's table → reads that branch's value, else the typed empty.
- **PEEK optimisation:** a composite branch joins ONLY a target type that actually HAS the next attribute
  (`tq->ResolveColumnByName(path[seg+1])`). So a field on 1 of 15 recorder types = ONE join, not 15.
- `ibRecordQueryable::ResolveReferenceTargets` (`commonObjectMetaQuery.cpp`) supplies the target
  queryables — it iterates the column's `GetTypeDesc().GetClsidList()` and keeps the Reference-kind
  CLSIDs (a composite may mix `Catalog.A | String | Number` — only the reference alternatives produce
  joins; a string / number / empty value matches none and falls to the typed empty).

**Where it is wired.** PROJECTION (`SelectPath`, the `m_dotWalks` loop), flat `WHERE`
(`COALESCE(...) <op> value`, op from `condOp`), the boolean predicate TREE leaf (`lowerTree`), and
`ORDER BY` (the composite leaf's expr stored in `sortExpr`, single-target's alias in `sortAlias`). A
PURE single-target path keeps the deduped `chain.Resolve` + `BuildConditionExpr` path; a composite scalar
leaf takes the COALESCE expression; a composite NON-scalar (reference/enum) leaf stays the prior per-type
single-field path (last ref only). Single-source READs and single-source AGGREGATES (§23.9) support
dot-walk; a multi-source (JOIN) dot-walk filter / group is the open item.

### 23.9 Aggregation, GROUP BY, and hierarchical TOTALS — over dot-walk

**Single-source aggregate (`ExecuteAggregate`).** `SELECT Producer.Region, SUM(Qty) … GROUP BY
Producer.Region` runs server-side: the door carries `GroupBy(path)` and `Aggregate(fn, path, alias)`
(`m_groupPaths` parallel to `m_groupBy`; `AggregateItem.m_path`), and `ExecuteAggregate` joins each path
through an `ibRefJoinChain` and qualifies the grouped / aggregated leaf by its join alias (a plain key /
input qualifies by the main table once joins exist). A dot-walk group leaf is GROUPED + projected under
its own physical name and read back by the leaf column (no `mainTable.*` in an aggregate → no self-ref
collision). JOIN / multi-source dot-walk aggregates error clearly.

**Hierarchical TOTALS BY dot-walk.** `… TOTALS SUM(Qty) BY Producer.Region` folds in RAM from one snapshot
(`ExecuteTotals` → `result.Select(ByGroups…)`). The totals fold is **keyed by metaID** (the snapshot is
built from the door's stamped materialise-columns by `GetColumnId()`, read via `Value(col)`). Two things
break that and are solved by **synthetic scalar columns** (`ibSyntheticScalarColumn` — a `ibRawDBColumn`
with a unique id `≥ 0x50000000`, clear of real metaIDs and the COUNT(*) receivers at `0x40000000`):
- A **self-referential dot-walk dimension** (`Parent.Code`) — the leaf shares a metaID with the row's own
  attribute, and projecting it under the leaf's physical name COLLIDES with `mainTable.*`. So the provider
  projects it under a DISTINCT alias (`dimN`) and the synthetic column reads that alias under its own
  unique id; `TotalByDotWalk(path, dimCol, alias, dim)` carries it.
- A **computed / constant measure** (`SUM(1 AS test)`, `SUM(a*b)`) — has no metaID. The door projects the
  expression (`SelectExpr`) and the totals aggregate reads it through a synthetic measure column.

The synthetic columns live in `OutputColumn.m_ownedCol` (the schema travels with the selection, so the
result's stamped raw pointers stay valid through `Select()` even after the `ibDataQueryResult` is consumed).
COUNT(*) and `SUM(<selected real column by alias>)` in TOTALS are also handled (alias-resolved, named for
read-back).

**Constants in SQL need a CAST.** A bare projected constant reaches the DB as an untyped placeholder
(`SELECT ? AS x`) — Firebird (and other strict engines) reject it (`-804 Data type unknown`).
`BuildColumnExpr`'s `Const` case wraps it `CAST(? AS <type>)` derived from the `ibValue`
(`NUMERIC(18,6)` for numbers — a bare `NUMERIC` on FB is `NUMERIC(9,0)` and would truncate; `VARCHAR(n)` /
`TIMESTAMP` / `BOOLEAN` otherwise). The cast-type spelling is FB/PG/MySQL-portable; a fully dialect-correct
spelling (esp. SQLite date affinity) belongs in the L1 Dialect Dictionary — TODO.

**Open (totals):** a composite MID-segment dot-walk dimension, and dot-walk over a UNION (union output
is not reference-aware). ~~TOTALS over a JOIN / UNION~~ and ~~TOTALS BY a reference / composite dot-walk
leaf~~ both landed — the multi-source ROLLUP push-down (`CanColocateRollupTotals` /
`BuildUnionRollupFrom`) runs them server-side.

---

## §23 — One declaration drives DDL + data: the L3-3 data mover (landed 2026-06-15)

The structure inversion is complete: a metaobject **declares** its physical schema once in
`ContributeTables(ibSchemaSnapshot&)`, and that single declaration feeds **both** ends —
- **L3-2** (`ibStructureBuilder` / `DiffSnapshots` / `ibSchemaBuilder`) generates / migrates the tables
  and applies the seed, and
- **L3-3** (`ibDataMover`, `query/dataMover.{h,cpp}`) dumps / restores the *rows*.

No metaobject writes dump code any more. The per-object `DumpTable` / `RestoreTable` virtuals are **gone**;
`ibMetaDataConfigurationStorage::DumpDataToBuffer` / `RestoreDataFromBuffer` build the whole config's
`ContributeTables` snapshot and drive `ibDataMover::Dump` / `Restore` over every table, framed by the
table's metaID. A table with no value columns (an enum: its values are SEED rows) is skipped.

### Mode + key are READ OFF THE STRUCTURE
`ibDataMover` takes a `const ibSchemaTable&` and resolves everything from it:
- **key column** — the row-identity scaffold the structure declared (a `uuid` for catalogs / enums),
  found as the scaffold covered by a UNIQUE index; **never hardcoded**. A register keys on floating
  fields (recorder+line / dimensions) and has no unique scaffold → no key → plain INSERT (it also has
  no seed). `sys_const` (External, single row) keys on its primary-key column (`RECORD_KEY`).
- **mode** — UPSERT on a unique key (the mutable main record; sys_const), else INSERT (a section's
  repeating owner uuid; a register).

### Codec tier — one spread driver, shared
`WriteValue` (value→statement) and `BinaryToStatement` (wire→statement) share one allocation-free
role-driver `ibColumnSpread::DriveSpread` (`query/columnSpread.h`), walking the same load-bearing role
order `DescribeColumnLayout` lays out — so the field SET and ORDER are byte-identical across the value
codec, the wire codec and the DDL. The binary-wire codec (`Binary{To,From}*`) moved from `ibColumnCodec`
to its L3-3 home `ibDataMover` (its only callers are the mover + the constant's single-cell dump).

### Seed = a value table keyed by uuid
`DiffSeedInto` / `WriteSeedRow` (schemaSnapshot.cpp) UPSERT a seed row matching on the table's declared
unique **scaffold key (uuid)** — NOT the queryable's data-reference (a seed never sets it, and an enum
table has no such column; matching on it raised `-206 FLDxxxx_TYPE does not belong`). A predefined
catalog value's seed sets its **own data-reference** (`_RRRef` = its pure guid, == uuid; `_RTRef` = the catalog's clsid)
so the value is referenceable by its key AND each row has a UNIQUE `_RRRef` (no `_REF_UQ` collision on
the NULL default). Predefined values are correctly non-deletable at runtime as a result.

### Restructure ledger shows the user name
The apply-change ledger reports `ibBackendQueryable::GetQueryName()` (the metaobject's name, e.g.
`Enumeration3`), not the physical `ClassNNNN`.

### Renames (this arc)
| was | now | what it is |
|---|---|---|
| `GetModelID` | `GetColumnId` | a column's per-source read key (== metaID for an attribute) |
| `GetSQLFields` | `GetValueFields` | a column's data-only physical fields (no `_TYPE`/`_RTRef`) |
| `GetFieldNameDB` | `GetPhysicalName` | a column's physical field base name |
| `GetTableNameDB` | `GetPhysicalTableName` | a metaobject's physical table name |
| `GetQueryMetaID` | `GetQueryTableId` | a queryable's table/source metaID |

The FB DDL/DML barrier (`DdlCreatedTables` / `DdlDeferredWrites`) moved from process-wide statics onto
the connection holder (per-connection, resolved via the explicit holder or `ibConnectionPool::ThreadHolder`).

## §24 — L1 bypass closed: raw SQL lifted onto the L2/L3 doors (landed 2026-06-16)

The plumbing that reached *down* to raw driver SQL (`db_query->RunQuery` / `PrepareStatement` /
`ibDatabaseResultSet`) now goes through the proper tier. This is **tier hygiene**, not a move "up":
the tiers always existed; a lot of DAO/infra code skipped them. The dividing line is **does the data
have a metaobject/queryable?** — yes → L3 (`ibDataQueryBuilder` over `GetQueryable()`); no (system
table, DDL, structure) → **L2 is the ceiling** (the dialect-neutral door over physical tables).

### Lifted to L2 (8 files pure-L2 — zero L1 includes)
`debugClientQuery` (breakpoints), `constantMetadataQuery`, `appDataQuery`, `structureBatch`,
`userInfo` (+ new `ibUserInfo::Delete`), `userList` (frontend → backend `ibUserInfo`, no raw SQL),
`wfrontend` (meta-watch `sys_config` read), `constantObject` (RECORD_KEY row-lock + open/table-exists
gate; its *value* read was already L3). ~13 hand-written dialect forks collapsed onto the dictionary.

### L2 door additions
- **Introspection** on `ibDatabaseQueryBuilder`: `TableExists` / `GetColumns` / `IsOpen` /
  `IsActiveTransaction` (delegate to the borrowed connection) — lets a DAO stay on one door for its
  whole job, gating a CREATE / reading a live column set / checking state without raw `ibDatabaseLayer`.
- **`ibInsertSelect(table, columns, source)`** — `INSERT INTO t [(cols)] <SELECT …>`. The source is any
  relation tree; empty `columns` = `INSERT INTO t SELECT …`. Standard SQL, no per-driver fork.
- **NOWAIT lock-hint**: `ibQueryIR::m_lockNoWait` + dialect `m_rowLockNoWaitSuffix` (default `" NOWAIT"`,
  FB/SQLite empty — their non-blocking acquire rides the TX `noWait`/TPB). `lockManager` and the constant
  row-lock now express the pessimistic SELECT as `ir.m_lockForUpdate` / `m_lockNoWait`. The per-driver
  `ibDatabaseLayer::RowLockHint()` / `NoWaitClause()` virtuals are **deleted** — the row-lock clause lives
  solely on the dialect dictionary (`m_rowLockSuffix` / `m_rowLockNoWaitSuffix`). See record-locks.md.

### Stays L1 by design (not leftover)
Connection / transaction **lifecycle** legitimately lives below L2: `sessionRegistry` and `lockManager`
keep `databaseLayer.h` for TYPES only (zero raw query code — `ibDatabaseConnectionHolder` /
`EnsureConnection` / `ibTxOptions`); `metadataConfigurationQuery` keeps it for the save TX
(RollBack / Begin / IsActive / Commit, owned by `ibSchemaBuilder` — cannot route through a query scope).
`sessionRegistry`'s write path runs on `q(&m_writeHolder)`; its conn acquisition switched
`AcquireFreeConnection` → **`EnsureConnection`** so the holder *binds* its connection and the door
resolves to exactly `m_writeConn` (the registry's failover-managed handle).

### Dead-code removed
The whole pessimistic-row-lock-for-liveness legacy — `TryProbeRowLock` (virtual + 4 driver impls +
registry `ProbeSessionRowLock` + **`m_probeConn`/`m_probeHolder`, i.e. one fewer held connection per
process**) and `HoldRowLocks` / `ReleaseRowLocks` (virtuals + firebird impl + `m_rowLocksHeld`). All were
replaced long ago by snapshot + heartbeat-on-`lastActive` liveness; only comments referenced them.

### Remaining (by-demand)
`UPDATE … RETURNING` (the FB/PG sequence counter) — needs a DML-with-result-set execution path + a
dialect `RETURNING` clause. The accounting register read/write is the L3 lift target (still `#if 0`).

## Update 2026-06-22 — DB/RAM NULL parity + comparison/identity aligned to SQL

The RAM floor and the SQL push-down now agree on NULL, ordering and reference identity, so one set of
semantics serves both paths (the DB/RAM-boundary LINQ trap, closed for these axes). 471/471.

### NULL parity (three-valued / Kleene)
- **Filter** — a comparison with a NULL operand yields UNKNOWN; the WHERE keeps a row only on a definite
  TRUE. `NOT(UNKNOWN)=UNKNOWN`, `AND`/`OR` propagate UNKNOWN. Covered in both the L3 RAM fold
  (`RamEvalPredicate`/`FilterRows`) and the script LINQ-to-objects floor (under a scoped flag so general
  script comparisons stay two-valued). UNKNOWN is represented as **TYPE_NULL** (SQL: unknown ≡ null).
- **Aggregates** — `AggregateOne` skips NULL operands (`SUM`/`AVG`/`COUNT(col)`), as SQL does.
- **Join** — a NULL-valued join key never matches (`NULL = NULL` is unknown): INNER drops it, an OUTER
  join keeps it unmatched on its own side. A keyless cross (no key column) is still cartesian.
- **Sort** — `ibQueryComposer::RamSortCompareKey` (both RAM ORDER BY comparators) places NULL as the
  smallest value → NULLS FIRST on ASC / NULLS LAST on DESC, deterministically.
- Parity harness (`tests/test_queryParity.cpp`) locks every case RAM-vs-real-SQLite. A SQL NULL is
  modelled as `ibValue(TYPE_NULL)` — what the driver yields — NOT an unset/Undefined (TYPE_EMPTY) cell.

### ibValue comparison — one three-way ordering primitive
`CompareValueLS` is now the single three-way ordering primitive (`int`: <0 / 0 / >0); `CompareValueGT`
is the second int hook (the `>` direction). `operator<` / `operator>` and `GE`/`LE` derive from them;
`GT/GE/LE` stay virtual so a class can retune one operator, but normally a class overrides only
`CompareValueLS` (references do — four overrides collapsed to one). A value with NO scalar payload
(TYPE_NULL or TYPE_EMPTY) sorts to the bottom → a TOTAL order, so `std::sort` / `std::set` / `std::map`
over `ibValue` (OrderBy / GroupBy / Distinct / Array.Sort / the RAM ORDER BY) are correct for NULL,
where the old two-valued `<` returned false both ways and left them unordered. (Ordering consequence:
`Undefined < 5` is true — distinct from the three-valued filter logic.)

### SQL NULL = TYPE_NULL strictly
`TYPE_NULL` is the SQL/explicit null (the `Null` literal; a NULL DB column → `ibValue(TYPE_NULL)`).
`ibValue::IsNull()` added. `TYPE_EMPTY` (Undefined) is the no-type default of a composite value, NOT a
SQL null; an empty reference (type chosen, no guid) is "not filled" yet NOT a null. `RamIsNullValue`,
`IsNullOperand` and the UNKNOWN sentinel all key on TYPE_NULL. Script builtins `IsNull(value)` and
`ValueIsFilled(value)` (the latter = `!IsEmpty`, the "value is filled" predicate) expose this.

### Reference identity = the DB key
A reference's `GetHashKey` is now `metaID + guid` (the DB key pair `_RTRef` + `_RRRef`), not the guid alone — a reference
column can target several types, so the metaID disambiguates. Runtime grouping / join / dedup over a
reference now match the database key.

### Remaining
- **cross-dialect parity** — the harness tests vs SQLite only; FB/PG/MySQL/ODBC need a live stand.
- **two LINQ paths** — the L3 composer (push-down + temp-db) and the script `.Join()` RAM floor share
  value semantics now, but are still structurally separate; merging them is the remaining debt.

## Update 2026-06-23 — L4-2 JOIN push-down (the two-paths merge, started) + SQLite temp dialect + ANALYZE

The headline operator of the "two LINQ paths" debt — `Join` — now lowers into the L3 door instead of
always RAM-joining. A script `.Join()` where a `Data.*` queryable participates runs server-side
(co-located / temp-promoted / RAM-stitched — the composer decides); the RAM hash-join
(`ibValueJoinState`) stays the explicit fallback for everything outside the slice.

- **`ibValueQueryable::JoinPushDown`** (`system/value/valueQueryable.cpp`) — the new `case M::Join` on a
  queryable receiver. Reduces inner to a leaf (another queryable, OR a RAM value table wrapped as
  `ibTempTableQueryable`), lowers both key selectors to one column each (the same recorded-lambda AST as
  Where/OrderBy, via `LowerLambdaColumnPath`), builds `m_builder.Join(...).Select(...)`, and runs the
  (outer, inner) result-selector in RAM over the reconstructed rows (a reference for a single-key source,
  a structure of columns otherwise — matching `RowValue`). Read back BY ALIAS, robust across the
  co-located / temp-promoted / RAM-stitch paths.
- **`ibValueQueryable::TryJoinThroughL3`** (static) — the RAM-receiver entry, called from the base LINQ
  dispatch (`procUnitLinq.cpp`, `case M::Join`) BEFORE `ibValueJoinState`: when `.Join()` is dispatched
  on a value table but the inner argument is a real DB queryable, the receiver is wrapped as a computed
  leaf and re-uses `JoinPushDown`. So BOTH directions (`Data.X.Join(table)` and `table.Join(Data.X)`)
  reach the one L3 join executor; `procUnitLinq` already depends on `system/value`, so no layering break.
- **Host API** — `InvokeLambda(callable, argPtrs, n, retVal)` (one array form; `InvokeLambdaWithArg`
  forwards). Type-probes use the canonical non-throwing `ibValue::ConvertToValue(T*&)` (a type mismatch
  is a legitimate RAM-fallback, not an error — `ConvertToType`/`CastValue` throw under
  `_USE_CONTROL_VALUECAST` and are wrong for a gate).
- **SQLite temp dialect + ANALYZE** — see [temp-db.md](temp-db.md) (2026-06-23). The SQLite driver now
  vends `GetTempTableDialect()`, so a heterogeneous join (RAM ⋈ DB) temp-promotes server-side on the
  embedded DB; ANALYZE became a first-class L2 statement (`ibDdlKind::Analyze` / `ibAnalyzeTable`).
- **`ibTempTableQueryable::IsComputedInRam()` fixed to `true`** — it vends the computed provider but
  reported `false`, so a multi-source join with it mis-routed in the co-location gate (empty table name).
  Now consistent with `ibSubqueryQueryable`.
- **Tests** — `tests/test_queryJoinParity.cpp` (RAM join core vs live SQLite JOIN: inner / left /
  NULL-key / multi-match) + `tests/test_tempDbSqlite.cpp` (temp-promote round-trip + ANALYZE-ran, via an
  appData + SQLite-pool fixture brought up with `wxInitializer`). Full `oes_tests` 476/477 (the one red,
  `QueryDdlRenderer.Firebird_CreateTable`, is a pre-existing FB type-map CHAR-vs-VARCHAR mismatch,
  unrelated to this work).

### Open (after this)
- **the rest of the merge** — `Where` / `OrderBy` / `Take` / terminals / `Join` / **`Select`** (2026-07-02,
  the dated block below) now lower; `GroupBy` / `Distinct` / `Skip` / set-ops stay on the RAM floor and the
  boundary is now VERIFIED by code (they can't lower correctly in v1 — not merely unimplemented). The full
  single-lowering (array-of-structures receiver, reentrancy / snapshot-vs-guard) is the larger Layer-3 arc.
- **join harden** — composite (multi-column) join keys, outer `OrderBy` / `Take` before the join,
  projection push-down to SQL (needs a two-arg lambda recorder), a lazy iterator instead of the eager
  Array, end-to-end parity (script push-down == RAM) once a script-level DB test harness exists.

## Update 2026-07-02 — Layer-3: `Select` projection lowered on a queryable + the safe-lowering boundary verified

`Data.X.Select(x => x.Field)` and `Data.X.Select(x => x.Ref.Field)` now lower server-side instead of pulling
the whole source into RAM. `ibValueQueryable` gained a scalar-projection state — `m_projectCol` (a plain
column, read by pointer via `sel.GetValue`) and `m_projectAlias` (a dot-walk leaf projected onto the door as
`SelectPath(path) AS alias`, read via `sel.GetColumn(alias)`). The shared `RowValue` helper, when a projection
is set, yields that scalar per row instead of the whole reference / structure — threaded through every read
path (`ToArray` / `First` / the iterator / `MaterialiseThenRam` / `CreateIterator` / `CloneLink`). The
`case M::Select` in `DispatchLinqMethod` reduces the lambda via `LowerLambdaColumnPath` /
`LowerLambdaColumnExpr`: one plain column → `m_projectCol`; a dot-walk path → `SelectPath` + `m_projectAlias`;
an arithmetic / CASE projection (`x => x.A * 2`, `x => IIF(c, a, b)`) → `SelectExpr` + `m_projectAlias`
(a server-side computed column, reaching parity with the text `SELECT Qty * Price AS Total` path); a structure
(`x => new {A, B}`), a computed-source dot-walk, or a projection chained onto an existing scalar projection →
the RAM floor (unchanged, correct). Zero regression for un-projected reads (the projection
defaults to none → the prior full-row path).

**The safe-lowering boundary is now VERIFIED by code (not guessed) — the remaining RAM-floor ops stay there
on purpose, they cannot lower correctly in v1:**
- **`Union` / `Concat`** — `RamUnion` (`queryProvider.cpp`) materialises each branch with `condsByName = true`,
  so a folded outer `Where` is applied to EVERY branch by name; LINQ `X.Where(a).Concat(Y)` = `(X where a) ∪ Y`,
  which diverges. The only safe case (both sides bare + same source) is `X ∪ X` — useless. Plus a heterogeneous
  union would mis-reconstruct references (`RowValue` keys off the OUTER's reference column). → RAM.
- **`Distinct`** — a server-side `SELECT DISTINCT` is only meaningful over a RESTRICTED projection, but a
  single-source read ignores an explicit select-list (it projects all columns), so the v1 projection cannot
  restrict the SELECT → `DISTINCT` would dedupe by the whole row (the unique uuid makes it a no-op). → RAM.
- **`Skip`** (keyset has no offset), **`Reverse` / `Last`** (need a preceding `OrderBy`), **`Aggregate`-fold /
  `Intersect` / `Except` / `*Indexed`** — RAM by nature, not gaps.

So the cleanly-lowerable queryable ops are now exhausted (`Select` was the last meaningful one). The remaining
Layer-3 scope is the harder structural work — an array-of-structures receiver (the Join path accepts only an
`ibValueModelTable` today) and the join key-selector reentrancy semantics (`EnsureIndexed` builds the hash
inline, no snapshot-before-lambda guard) — separate arcs that need a script-level test harness. (Built green
Debug|x86 2026-07-02; the script path has no gtest, so runtime behaviour is live-run verified.)

## Update 2026-07-17 — computed-source dot-walk / expressions / HAVING (RAM) + server-side temp-promote

A **computed source** (register `Balance` / `Turnover` slice, a subquery) is `IsComputedInRam()` — it has no
server table, so historically dot-walk / arithmetic / HAVING over it threw "unsupported". Now, two layers:

**RAM (the always-works floor, `ibComputedProvider`).** `ResolveComputedDotWalks` materialises each reference
hop's TARGET and LEFT-joins the leaf in (`MaterialiseLeaf` + `JoinRamTables` — the RAM analog of the physical
`ExpandDotWalkJoins`), so a reference dot-walk (`Balance.Item.Name`) resolves for projection / WHERE (flat +
boolean OR/NOT) / ORDER BY / GROUP BY / aggregate input. Arithmetic / CASE evaluate per row (`EvalColumnExprRow`
— `SUM(Qty*Price)`, `WHERE Qty*Price > N`, a computed SELECT column). `RamAggregate` applies **HAVING**
(`PassesHaving`). The `GateComputedExpr` computed-source bail is removed (the mechanism existed — it just was
not wired to the computed path). Tests: `tests/test_queryComputedDotWalk.cpp` (8), `tests/test_queryLinqExec.cpp`
(L4-2 lambda→predicate→rows execution parity).

**Server-side (the optimisation, `PromoteSingleComputed`, docs/temp-db.md §9).** A single computed source with a
big result (≥ `kTempTableMinRows`) whose read AGGREGATES is materialised into a DB temp table and run as ONE
server-side SQL `GROUP BY` — the DBMS does the fold. A **reference GROUP key** groups by its full blob spread; a
**dot-walk key / input** (`Balance.Item.Name`, `SUM(Item.Weight)`) remaps its FIRST path segment onto the temp and
the deeper segments JOIN the target catalog server-side through the ordinary dot-walk join chain (`ibRefJoinChain`)
— the temp is a real DB source, so it inherits the physical dot-walk machinery, no new SQL. The provider projects
every group key by its FULL spread (`ColumnFieldNames`) and the lazy cursor is DRAINED into RAM (metadata-blind,
`GetValue`) while the temp manager is alive (a RAM-backed result outlives the temp DROP) — the reference-typing
stays in the provider, not the reader. Only small results stay RAM. SQLite-validated (`tests/test_queryComputedServer.cpp`
— the scalar promote plus a LINQ-lowered WHERE pushed to SQLite). Push what you can to the DBMS; RAM is the fallback.

**Reference-target resolution — no cast.** The dot-walk resolver (`column → target queryable`) moved off the
queryable onto the ONE metadata-owning provider (`ibDbTableProvider::ResolveReferenceTarget`); the query-provider
layer names no metadata (base returns null, the computed provider forwards). It is CAST-FREE: the clsid KIND
(`IsReference`, a bit read) gates, then `ibCtorMetaValueType::GetQueryable()` dispatches VIRTUALLY to the reference
ctor (whose metaobject is the typed queryable holder) — no `dynamic_cast`, no RTTI on the lowering path. The virtual
generalises: object / manager / tabular ctors can vend their queryable the same way.

**Grouping a table by a reference — fixed.** `CanPageGroupLevel` claimed "PLAIN scalar dimension" but did not
check scalarity, so a reference group key slipped into the keyset-paged `ExecuteGroupLevelPage`, whose projection
(`ColumnValueFields`) dropped the TYPE field the read reconstructs from → "field `<col>_TYPE` not found in the
resultset". The gate now rejects any multi-field-spread dimension (reference / variant), routing it to the unpaged
full-spread `ExecuteAggregate`. Regression guard: `QueryComposerGate.GroupLevelPage_ReferenceDim_NotPageable`.

## Update 2026-07-17 — `value(...)` literal reference constant + expression ORDER BY + constant JOIN ON

**`value(<Kind>.<Name>.<Member>)` — a literal reference constant.** New keyword `VALUE`
(`queryKeywords.h` / `queryLexer.cpp`), parsed in `ParsePrimary` into a new AST kind `Value` carrying the
dotted meta-path (`queryAst.h`). The name is NOT resolved at parse time (the AST stays metadata-free) — resolution
happens at LOWERING, where the config is in scope, exactly like a source name. `value(Catalog.Currencies.EmptyRef)`
→ the catalog's empty reference; `value(Catalog.Currencies.Dollar)` → the reference to that predefined item.

- **Resolution lives on the metaobject, reached through the queryable.** `EvalValue` (the leaf node→`ibValue`
  evaluator — the same one that turns a `&parameter` into its bound value) resolves the meta-path through the SAME
  factory a FROM source uses (`md->GetSourceFactory()->Resolve(ns, name)`), then reads the member straight off the
  resolved queryable's `GetSourceMetaObject()`. Nothing is added to the queryable — it already vends the metaobject.
- **`ibValueMetaObjectGenericData::ResolveQueryConstant(member, out)`** — a virtual try-resolve returning `bool` +
  the value in `out` (NOT throwing, NOT returning): the generic base has no constants → false; the record level
  (`ibValueMetaObjectRecordDataRef`) resolves `EmptyRef` via `ibValueReferenceDataObject::Create(this)`; the
  hierarchy level (`…HierarchyMutableRef`) adds predefined items via `FindPredefinedValue(member)` →
  `Create(this, guid)`. Cast-free — each override uses its own `this` type. An unknown member returns false and
  **the query engine raises the exception** (in `EvalValue`, so the error carries the query source span).
- **Works everywhere a value is expected — one seam (`EvalValue`).** `WHERE / HAVING / IN(list) / BETWEEN / LIKE`
  RHS already route through `EvalValue`, so `value(...)` and `&param` work there for free. Plus a `Value` /
  `Param` case in the projection (`SELECT value(...) [AS x]`, `SELECT &param`) and in `BuildColumnExprFromAst`
  (inside a computed column / CASE).

**Constant JOIN ON — `col <op> value/&param` is an INNER-join filter.** `JOIN b ON b.x <op> &p` is, by SQL, a
cross join filtered on `b.x` — so the lowering emits `CrossJoin` + `Where(col, op, value)` when exactly one side of
the ON comparison is a constant (literal / `&param` / `value(...)`). Gated to INNER joins: filter-in-ON differs from
filter-in-WHERE only for outer joins (it pre-filters the null-padded side), which errors clearly ("put it in WHERE").

**Expression ORDER BY — `ORDER BY CASE … END` (sort by a condition).** New `ibDataQueryBuilder::OrderByExpr(expr,
asc)` + `ibQuerySortItem::m_expr` (an `ibQueryColumnExpr`, mirroring the computed-WHERE `m_expr`). The L2 sort key
already sorts by an EXPRESSION (`ibQuerySortKey::m_expr`), so `BuildSortKeys` just lowers the L3 expr to L2 through
the existing `BuildColumnExpr` and emits it — no new SQL. The lowering routes an `Arith` / `Case` (or a bare
constant) ORDER BY item to `OrderByExpr`; a plain column / dot-walk keeps the column sort (it can keyset). Single DB
source only (like the computed WHERE side; `computedPrimary` and JOIN error clearly). A computed sort is NOT a
keyset key — the anchor / keyset builders already skip `m_col == null` items, so it materialises only in the ORDER
BY; the text-query full read (`Query.Execute()`, never the path-based list composer) is the user.

---

## Update 2026-07-27 — semi-join key reduction (sideways information passing) in the RAM stitch

**The problem.** The RAM stitch reads each leaf WHOLE and joins afterwards, so a leaf pays for every row
it holds even when almost none of them can join. On a mixed tree (a DB table joined to a register slice)
the DB table is scanned in full and then thrown away row by row in the nested loop.

**The move.** Once ONE side of a join is materialised its join-key values are KNOWN, so they are pushed
into the other side's read as `key IN (…)`. It is a **pure reduction**: a row the filter removes could not
have appeared in the result, so the answer is identical and only the read shrinks. Three pieces landed —
the condition that carries the keys, the reduction that produces them, and the enforcement that makes a
pushed condition actually bite on a computed source:

### 1. A set-valued condition — `ibQueryFilterOp::In` + `ibQueryCondition::m_values`

- `queryable.h` — `In` appended LAST to the op enum (runtime-only, never serialised, but the ordinals stay
  stable), plus `std::vector<ibValue> m_values`. It is the ONE op that reads `m_values` instead of `m_value`.
  Placed after `m_value` so the existing aggregate initialisers `{ col, op, value }` keep working.
- **Door** — `ibDataQueryBuilder::WhereIn(col, values)`. NULLs are stripped on the way in: a NULL key matches
  nothing in an equi-join and `IN (…, NULL)` is the classic SQL trap, so an all-NULL set becomes the EMPTY set.
- **SQL render** (`ibMetaIRBuilder::BuildConditionExpr`) — branches BEFORE `FilterOpToBinOp`, which would
  answer `Eq` and compare against an unset `m_value`. Two shapes: a single-field lhs (raw column, or the
  row key) renders one native `ibIn`; a METADATA column OR-folds the SAME composite equality `Eq` uses, once
  per value — load-bearing for a VARIANT (a native IN on one primitive field ignores the `_TYPE` tag and would
  match rows of the wrong variant) and for a REFERENCE (whose constant must be the encoded `_RRRef` blob, not
  a bare `ibConst`). The empty set is NOT special-cased here: L2 already renders `x IN ()` as `1 = 0`
  (`QueryRenderer.In_EmptyListIsConstantFalse`), so "matches nothing" stays decided in ONE place. The
  metadata fold IS guarded on non-empty — an empty OR-fold returns null, i.e. NO predicate, i.e. the whole table.
- **RAM eval** (`RamEvalLeaf`) — branches before the scalar null guard, which would read the unset `m_value`
  as NULL and answer UNKNOWN for every row. SQL semantics: a NULL probe is UNKNOWN, an empty set is FALSE.

### 2. The reduction itself — in both stitch paths

`MaterialiseNode` gained an `extra` parameter carrying reductions down the recursion, routed by column
OWNERSHIP at the Source node (a caller hands a set to a whole subtree without knowing which leaf holds the key).

- **Two-leaf join.** One side is materialised first; its distinct keys reduce the other.
- **3+ unit INNER chain** (`JoinUnitsSmallestFirst`) — before reading a unit, every edge to an already-read
  unit hands down its keys. Transitive: a unit reduced early hands on a narrower set. The counts fed to
  `PlanInnerJoinOrder` stay EXACT — measured after the reduction, which is what the planner should see.

**WHICH side drives is the trick.** A computed source is built in memory whatever we do — the register's
`ComputeBalance` / `ComputeTurnover` / `ComputeSlice` runs in full and a filter can only trim its *result*,
never avoid the compute. Reducing it therefore saves downstream join work but no I/O, while reducing a DB
read saves the scan itself. Therefore: when exactly one side is a computed leaf, it is materialised FIRST
and its keys shrink the other's scan — the DB⋈RAM case. In the N-way chain the same rule orders the reads
(computed units first).

**DIRECTION IS A CORRECTNESS GATE, not a preference.** For a LEFT join only left→right is sound: the right is
the null-producing side, so a right row matching no left key contributes nothing either way, while the
preserved left side must never lose a row. Driving from the right is therefore INNER only. Never applied to
RIGHT / FULL / cross, to a computed ON (no column key), or to a THETA ON — an equality key set says nothing
about which rows satisfy `a.x > b.y`. (`FlattenInnerChain` already admits only INNER / non-cross / equi-key /
non-computed ON, so the N-way path is sound unconditionally.)

**The cap is the whole cost model.** `kSemiJoinMaxKeys = 512`, alongside `kTempTableMinRows`. Beyond it the IN
list costs more than the read it saves, and a key set that large is rarely selective. Above the cap the
reduction simply does not happen — correctness never depends on it.

**One trap closed on the way.** `MaterialiseLeafToRam` re-created each condition as `Where(m_col, m_op,
m_value)` — three of seven fields. For an `In` that means an EMPTY set, i.e. "matches nothing". Fixed at the
root rather than by special-casing `In`: a new `ibDataQueryBuilder::Where(const ibQueryCondition&)` forwards a
condition VERBATIM, and the composer uses it. `m_path` / `m_expr` / `m_asExists` / `m_semiJoin` stopped being
dropped too, and any future field rides for free.

### 3. `ComputeRows(extra)` made ADVISORY — the provider enforces

The reduction has to reach a computed leaf too (it is the receiving side whenever the driving one is a real
table), and it did not: every register compute takes the parameter and drops it —
`ibBalanceQueryable::ComputeRows(const std::vector<ibQueryCondition>& /*extra*/)`, same for turnover and
slice. Invisible while `extra` was always empty; the reduction is its first real user.

Fixed at the CALLER, not per implementation: `ComputeRowsResolved` now applies the plain conditions over the
rows `ComputeRows` returned. Re-filtering a source that DID honour them is idempotent, so one place serves
both kinds — present and future. Two guards:

- **Only conditions whose column is IN the returned table.** A column the source does not vend reads back as
  an empty cell, which the three-valued evaluator takes for NULL and uses to drop EVERY row.
- **Applied BEFORE the dot-walk resolution**, so the reference joins run on fewer rows.

The interface contract in `queryable.h` was rewritten to match: `extra` is **advisory** — an implementation
may push it into its own compute to build fewer rows, but the caller applies it regardless, so ignoring it
costs speed, never correctness. Note what this does *not* buy: filtering after the fact trims the result, it
does not avoid the register's compute — which is exactly why the drive-order rule above prefers to reduce the
side that pays per row.

**Blast radius: none on existing behaviour.** The L4-1 text `WHERE` lowers into the predicate TREE
(`b.Where(BuildWherePredicate(...))`, `queryLowering.cpp`), and the tree was always applied on the computed
path (`if (spec.m_predicate) rows = FilterRows(...)`). Only the FLAT-condition path was unenforced, and the
register companion queryables carry their filters in the ctor rather than through the door — so that path was
effectively empty until the reduction started using it. No report moves.

**Tests (19).** A pure reduction cannot change the answer, so results alone cannot tell whether it fired.
`RecordingComputedQ` (`tests/test_queryComposer.cpp`) records what was pushed into `ComputeRows` and then
IGNORES it — exactly like the real registers — so the cases assert BOTH that the reduction fired and that the
provider's enforcement makes the answer right anyway:

- **Reduction** — `InnerJoin_PushesDistinctKeysToTheOtherSide` (keys dedup; the driving side receives
  nothing) · `LeftJoin_KeepsUnmatchedLeftRows` (**the direction gate** — flip the direction and it fails) ·
  `ThetaJoin_IsNotReduced` · `EmptyDrivingSidePushesNothing` · `InnerChainOfThree_ReducesDownTheChain`.
- **Enforcement** — `QueryComputedFilter.IgnoredConditionIsStillEnforced` (the source returns everything, the
  provider filters) · `IgnoredEqualityIsStillEnforced` (not an `In`-only patch) ·
  `ConditionOnUnvendedColumnDoesNotEmptyTheResult` (the guard).
- **The operator** — `tests/test_queryParity.cpp` (RAM vs real SQLite: match / single-value / NULL probe /
  unmatched value / empty set) · `tests/test_queryComputedServer.cpp` (through the door onto a live SQLite:
  native IN, NULL probe, empty set, all-NULL keys, and both metadata-column shapes — `BuildConditionExpr` is
  TU-local to `dbTableProvider.cpp`, so the door IS the seam).

**Honest remainder.** Not built, not measured — everything above is reasoned from the code. The DB-side win
is bounded by indexing: `key IN (…)` without an index on the join key still scans, it only transfers less.
And `kSemiJoinMaxKeys = 512` is a guess wearing the same "tune against real numbers" label as
`kTempTableMinRows` — neither has real numbers yet.

---

## Update 2026-08-07 — L4 grew what the constructor needed, and it stayed in the engine

The query constructor is a shell over this AST ([query-constructor.md](query-constructor.md)); the
pass that made it usable pushed six things back into the language, because a window that answers a
question the engine should answer is a second opinion about what a legal query is.

### `FROM` is optional — `SELECT 1` is a query

```
SELECT 1
```

parses, renders (no `FROM` keyword written — a clause with nothing after it is invented syntax),
and RUNS: `ibQueryLowering::ExecuteSourceless` builds a one-row RAM table from the synthetic
columns the projections describe. A `Column` inside such a query is refused with *"this query reads
no table"* — the truth, rather than an empty result that looks like data.

Why it matters beyond the constructor: `SELECT <expr>` is how a caller evaluates an expression in
the query language without inventing a second evaluator, and it is the shape a `VALUES`-style
literal source will grow from.

### Names, resolved without running — `CheckNames`

```cpp
BACKEND_API static void ibQueryLowering::CheckNames(const ibQueryPackage&, const params&);
```

Resolve every name a package mentions — sources, projections, group keys, ORDER BY, index fields,
totals dimensions, join conditions — and **execute nothing**. Raises with the engine's own wording
on the first thing that does not resolve, plus one rule the resolver alone can see: **two output
columns may not share a name**.

Deliberately SILENT where it cannot verify. A select over a source the resolver does not know is
not an error, it is an unknown — and reporting unknowns as errors is how a checker becomes the
thing people switch off. (`ORDER BY` is the one lenient list: it may name an output column that is
not itself a projection.)

### The same question, destructively — `PruneUnresolved`

```cpp
BACKEND_API static int ibQueryLowering::PruneUnresolved(ibQueryPackage&, const params&);
```

Walk the package and DROP what no longer resolves; return how many went. This is what a host calls
after removing a table, and the design point is that **nothing chases the deletion**: a
hand-written cascade is a list of cases, and the case nobody thought of is the bug. Re-asking "what
still resolves?" has no cases.

Same guard as the check, and it is the important one: a select whose source cannot be resolved is
left **entirely alone**. "We do not know" must never delete somebody's work.

### `TOTALS … BY <dim> [HIERARCHY] [AS <name>]`

A totals LEVEL has a name of its own (`ibQueryTotalDim::m_alias`). Two levels over the same column
— `Date` by month and `Date` by day — are two output columns, and without a name the second
answers to the first's. Parser, renderer and `OutputColumn::m_name` all carry it; round-tripped
with and without the `AS`.

### `ExecuteBatch` — every statement yields a position

Renamed from `ExecutePackage`, and the semantics are now total: the result is an array by
statement position, where a create-temp statement yields a result of **one column, one row**
holding the row count, and a drop yields `Undefined`. Previously a statement that produced nothing
readable produced nothing at all, which made the positions meaningless.

### Four answers that were being given more than once

Each of these was written out in two or three places, and each is load-bearing enough that a
disagreement between the copies would be a silent wrong answer rather than a compile error:

| Answer | Entry | Was |
|---|---|---|
| what is this projection called (alias, else the path's leaf, else nothing) | `ibQueryProjectionName` (`queryRender.h`) | 3 copies, two byte-identical. A union lines its branches up by this, a temp table's columns and index are these names, a totals level answers to one, and two projections may not share one |
| a dotted path as the AST node a query carries | `ibQueryColumnFromPath` (`queryRender.h`) | 6 hand-written splitting loops. The inverse of rendering a column, so it lives beside the render |
| a predicate's top-level `AND` chain, and its inverse | `ibQueryFlattenAnd` / `ibQueryFoldAnd` (`queryRewrite.h`) | 3 copies — the conditions model, the constructor, the lowering. "What counts as one condition" is one rule |
| the name a NEW column or table alias gets | `ibQueryEnsureUniqueName` / `ibQueryUniqueSourceAlias` (`queryRewrite.h`) | in the desktop dialog. This is the other half of `CheckNames`: the check refuses collisions, these make sure it never has to. Every host that assembles a query without typing it — the web front, a script, a report's composer — has to number a duplicate the same way |

Plus `ibQueryLexer::IsIdentifier(text)` — *is this a legal name?* answered by tokenizing it and
demanding exactly one `Ident` spanning the whole string, and `ibAllQueryKeywords()` — the whole
active keyword table as one string, for a syntax highlighter that must not carry its own copy of
the spellings.

### Two engine defects the pass turned up

⚠ **The temp-table index was keyed by presentation.** `ibQueryTempTableStore` built and probed its
value→rows map with `ibValue::GetString()`. For a reference that is the object's *presentation* —
two different objects can present identically, and the same object can present differently after an
edit — so an indexed lookup silently returned the wrong rows or none. Both sides now key on
`GetHashKey()`, which is identity (a reference keys by its guid). **`GetString()` is presentation
and must never be an identity key.**

⚠ **The access policy was not consulted on three aggregate doors.** `SelectAggregate`,
`SelectAggregatePage` and `SelectTotals` went straight to the composer, so a policy that filtered
rows out of `Select` did not filter them out of `SUM` over the same table — a row-level leak by
arithmetic. All three now take the same guarded path the row doors take (build a trusted copy with
`WithAccessPolicy(nullptr)`, ask `CheckSelect`, and on refusal either return a zero aggregate or
raise, per `m_allowed`). Eight tests pin it; the coverage table for all nine door terminals is in
[access-policy-rls.md](access-policy-rls.md).

---

## Update 2026-08-07 (b) — `CAST`: a narrowing, not a conversion

```
SELECT CAST(Recorder AS Document.Order).Number FROM AccumulationRegister.Goods
```

⚠ **Two different things share the word, and only one of them landed.**

`CAST(Recorder AS Document.Order)` **NARROWS** — the value already is of that type, and the cast
says WHICH of a composite reference's types is meant. `CAST(Code AS Number)` would **CONVERT**, and
that is a separate feature (refused, see below).

Narrowing is what the engine was missing. A composite reference names several types, so it has no
single set of fields behind it and `ResolvePath` refuses to walk THROUGH one — correctly, because
there is no one answer to give. The cast supplies the answer.

### The shape is the design

`CAST(x AS T).A.B` parses as a **Column** whose `m_path` is `{A, B}` and whose ROOT (`m_arg`) is the
Cast node. A bare `CAST(x AS T)` is a Cast node.

That is not a trick — it is what the construction means, and it is what made this small. The
resolver already walks a Column's path from a starting queryable; the cast only says which queryable
to start on. So the chain `ResolvePath` builds is `[the reference column, …, the leaf]` — **exactly**
the shape `SelectPath`, `ExpandDotWalkJoins` and the RAM join already consume. Nothing downstream
learned a new trick, and the target type is resolved by the same `ResolveSource` a `FROM` goes
through, so a cast cannot name what the language could not have named after FROM.

### Two places had to learn the shape — the same class of mistake in both

A cast-rooted column's path names fields of the **target**, not of the source the query is reading:

- `queryRewrite`'s FROM-subquery flattening substitutes an inner projection's output name into outer
  column paths. Applied to a cast-rooted path it would rewrite a field of the target into a column
  of the wrong table — so the walk descends into the cast's ARGUMENT instead (that one IS this
  source's column).
- `CheckNames`' collector takes a cast-rooted column WHOLE. Descending would hand the checker a path
  with no source to start on.

### Conversion is refused, and the message says why

The door's expression IR is Column / Const / Arith / Case / PeriodTrunc — there is no cast op, and
adding one means implementing it in the SQL provider AND the RAM one, in every dialect, with the
rounding and the failure mode spelled out. A query that parses and then answers something nobody
chose is worse than a refusal, so `CAST(Code AS Number)` raises:

> CAST narrows a reference to one of its types (Catalog.Products), it does not convert values:
> 'Number' is not a table

**Tests**: `tests/test_queryNaming.cpp` — `QueryCast.*` (the two shapes, the round trip inside a
whole query, the render carrying the root back, and a cast with no type refused).

## Update 2026-08-07 (c) — `BY OVERALL`, and a condition that knows which filter it is

Three things the constructor surfaced by being used, all of them holes in the LANGUAGE rather than
in the window.

### `BY OVERALL` — connect, do not write

The grand-total row looked like a feature to build and was a wire to connect. `BuildDimensionTree`
has always ended with `ApplyAggregates(tree.Root(), …)` — *grand total in-place* — and the walk
(`FlattenPreOrder`) has always started at the root's CHILDREN. So the number existed and the row did
not; the window's checkbox had been sitting there **ticked and disabled**, explaining that the grand
total "is always produced", which was true of the fold and false of the result.

What landed is the keyword, the flag it sets (`ibQuerySelect::m_totalsOverall`), and one line in the
walk. Two assumptions had to be relaxed for `BY OVERALL` **alone** — a whole totals query with no
dimensions at all: the terminal refused a `TOTALS` with an empty `BY`, and `ibSelector::Build()` fell
through to the manual fallback when the level list was empty, folding by a null column. The
dimension tree handles zero levels exactly right: `FoldDimLevel` returns at once, and the root still
carries the aggregates.

A flag rather than a level in `m_totalsBy`: a dimension is a column plus a place in an order, and the
overall is neither.

### A condition over a folded value is a HAVING

`WHERE` filters rows, `HAVING` filters groups — a real distinction, and one the author of a query
should not have to carry. The Conditions tab offers aggregate fields beside plain ones (they are all
fields of the result), so a condition over `SUM(Qty)` was written into `WHERE` and reached the row
filter, which has no aggregates to filter by.

Now a rewrite rule, per AND-term. An `OR` across the two is left whole — splitting it would change
which rows survive. And the lowering learned to emit one `Having()` per AND-term instead of reading
the whole expression as a single comparison; the builder AND-folds them, which it always could.

### The grouping rule reads the tree, not its top

Both halves of it asked what a projection **is** — a bare column, or an aggregate. Correct for `Qty`
and `SUM(Qty)`, and blind to everything between: `SUM(Price * Qty) / COUNT(*) * 1.2` is neither, and
was skipped whole, so a free `Price` beside a folded `Qty` in one expression was held to no rule at
all. One walk (`CollectFoldedAndFree`) answers both halves now, and `AggregatedColumns` is the mirror
of `UngroupedProjections` — same door, two readings.

**Found and NOT closed: `COUNT(DISTINCT x)`.** The language has `DISTINCT` at `SELECT` level only;
the aggregate grammar takes `*` or an expression. Closing it is grammar + render + the RAM fold
(small) **plus the SQL push-down** — `AggregateFnName` feeds `ibFunc(name, args)` at five projection
sites, and the L2 IR has no DISTINCT modifier. Half-landing it would mean correct numbers in RAM and
silently wrong ones against a database, which is the one failure mode worth refusing.

### Update 2026-08-07 (d) — the same rule, applied where the AST is held

Three follow-ons, each surfaced by running the window with (c) in it.

**The move has to be visible.** `Rewrite` works on a clone at execution time, so the constructor
showed a `WHERE` while the engine ran a `HAVING`. The rule is public now
(`ibQueryMoveAggregateConditionsToHaving`) and the Conditions model calls it in its one write door.
The tab reads BOTH clauses in consequence — it is "the conditions of this query", and which clause
each lands in is the engine's answer, shown in the text. Writing back re-folds everything into
`WHERE` and lets the rule split again, so the trip is reversible.

**`HAVING` is its own clause, not a tail of `GROUP BY`.** The renderer had always written it
independently; the parser read it only inside the `GROUP BY` block. So the shape the constructor now
produces by itself — `SELECT … FROM … HAVING SUM(x) = &v`, no grouping — came back as *"unexpected
text after the query"*, about a keyword sitting in plain view. With no `GROUP BY` the whole result is
one group, which is the same statement `TOTALS … BY OVERALL` makes in the other clause.

This is the third time in two days that the WRITE side of the round trip was complete and the READ
side lagged. It is worth naming as a class: the renderer is exercised by every screen refresh, the
parser only by text that actually takes that shape — so an asymmetry hides until something starts
generating the shape.

**`OVERALL` is a level, and the window's guards had to learn it.** "Add a grouping level first"
kept firing with Grand totals ticked — the tick said *there is a level* and the window answered *add
a level*, which is the window arguing with itself. Its mirror (`DropTotalsIfLevelless`) would have
thrown the measures away when the last dimension left. The rule did not change; what counts as a
level did, and both guards had to be told.

**`=` and `<>` in `HAVING`.** `HavingItem` carries a full `ibQueryFilterOp` and always did; the four
ordered operators were the lowering's switch. `HAVING COUNT(x) = 1` is as ordinary as `> 1`.

### Update 2026-08-07 (e) — `COUNT(DISTINCT x)`, and an estimate that was wrong

(c) closed with this written down as *found and deliberately not closed*: "the L2 IR has no DISTINCT
modifier… half-landing it would mean correct numbers in RAM and silently wrong ones against a
database". The fact was right and the COST was wrong. The L2 renderer emits a function as
`name(args…)`, so the modifier is **one field on `ibQueryExpr` and one line in that renderer** —
`DISTINCT` inside an aggregate is standard SQL, spelled identically by every dialect this layer
writes for, so it rides the generic path instead of becoming a per-driver branch.

The whole chain, once measured rather than guessed: keyword (already in the table), parser (read it
before the argument, where SQL puts it), renderer, the AST flag, the L3 aggregate item, a defaulted
`distinct` parameter on the three `Aggregate` verbs, four projection sites in `dbTableProvider`, and
the RAM fold — where DISTINCT counts by `ibValue::GetHashKey()`, the identity, never by the display
string (two references print the same far more often than they ARE the same).

`COUNT(DISTINCT *)` is refused with a sentence rather than a syntax error: a star names nothing to be
distinct about.

**The lesson is about the estimate, not the feature.** "Needs the IR to change" was true and became
"that is a separate arc" without anyone opening the IR. One `grep` at the SQL emitter would have
shown a five-line change. An estimate made from the SHAPE of a dependency, without reading it, is a
guess wearing a number.

### Update 2026-08-07 (f) — what the first RUN found

The suite had not been executed since 1071/0/5 — two days and three arcs earlier. Running it turned
up four defects, and **three of them were in code written during those arcs and never executed**.
Worth recording in that shape, because "it compiles and the window works" had covered all three.

**`Document.Order` did not parse.** After a `.` the reader demanded an Ident, and `Order` lexes as a
keyword — as do `Group`, `Index`, `Value`, `Update`, `Count`, `Elements`. A configuration names its
documents and attributes without consulting our keyword table, and should not have to. Position
decides: nothing but a name can follow a dot, so a keyword there IS a name. Four `QueryCast` tests
had been failing on this since the day they were written — they were testing `CAST(Recorder AS
Document.Order)`, and the language could not read the type.

**`SUM(x) / COUNT(y)` did not parse.** `ParseProjection` branched on the first token — an aggregate
keyword went straight to `ParseAggregate`, which read the call and stopped, so the slash was
"unexpected text after the query". The branch was never needed: `ParsePrimary` already routes an
aggregate, so the ordinary expression grammar reads both. A special case for *starts with* is nearly
always a parser deciding a shape by its first token instead of parsing it.

Note the order this happened in: `CollectFoldedAndFree` was written to handle free columns inside a
composite aggregate — a shape the parser could not produce. The rule was finished before the grammar
that needed it, and only running the tests showed the gap.

**`CAST(x AS T).Field` never worked.** The dot was *recognised* and not *consumed*, so the path
reader met it as its first token and said "expected a name" about the punctuation it had just
matched. One `AcceptPunct` instead of `IsPunct`.

**And one test was wrong, not the code.** `WHERE A = 1 OR SUM(x) > 5` moves to `HAVING` ENTIRE —
anything naming an aggregate can only be evaluated after the fold, so leaving it behind is not an
option. The comment said "left whole", meaning *not split*; writing the test I read my own words as
"left in WHERE".

The lesson is not "run the tests" — it is that the three code defects were all in paths **the window
cannot reach by clicking**. A cast written by hand, a ratio of two folds, a keyword-named metaobject:
the constructor generates none of them, so the manual loop that found so much this week was blind to
exactly these. That is the argument for the property test in
[[reference_query_render_parser_asymmetry]], stated by evidence rather than by worry.

### Update 2026-08-07 (g) — the property test, and what it found in one run

`tests/test_queryRoundTrip.cpp`: a seeded generator walks the AST, and the property is the one the
whole constructor rests on — `render(parse(render(ast))) == render(ast)`. Both sides are RENDERED
because there is no deep equality on the AST and text is the contract anyway; a query that survives
one trip but changes on the second is exactly a query the constructor would corrupt on a second
open.

It found two more defects on its first run, both of which every hand-written case had missed.

**A string literal could not end a line.** The shared lexer's string reader treats a newline as the
start of a CONTINUED literal (`"line one` / `|line two"`). It ran that branch even when the string
had already CLOSED, clobbering `next_pos` back to the literal's own start — so the lexer resumed
INSIDE the string it had just read and everything after tokenised as garbage, surfacing as a lexical
error some lines later.

It survived because it needs a string to be the LAST TOKEN ON A LINE. In script text a quote is
nearly always followed by `;` or `)`; query text was only ever tested on ONE line. But the renderer
prints a clause per line, so `WHERE` / `Code = "A"` / `GROUP BY …` — an entirely ordinary query the
constructor writes by itself — could not be read back. Fixed in `translateCode.cpp`: a closed string
ends at its quote.

**`CAST(x AS T).Order` still failed** after the dot-keyword fix, because the walk after a cast calls
the path reader from a FRESH position — so its first segment hit the first-segment rule, not the
after-a-dot one. The dot had already been consumed; the rule is the same. `ParseDottedName` now
takes `firstMayBeKeyword` and the cast-walk passes it.

**And the generator caught itself.** `TheGeneratorReallyProducesTheAwkwardShapes` asserts that the
shapes which actually broke — a reserved word as a name, a walk after a cast, arithmetic over calls,
`BY OVERALL` — all OCCUR across the seeds. It failed: `BY OVERALL` had never been generated once in
400 seeds. The cause was the LCG's low bits, which barely vary, read directly by `% 2` and `% 4`; the
output is mixed now. The round-trip property had passed on all 400 of those seeds, which is the
whole danger — **a generator that quietly produces less than it claims makes a green run mean less
than it looks.** A property test needs a test of its own coverage, or it is a ritual.

**A limit recorded, not smuggled in:** a reserved word as the FIRST segment of an UNQUALIFIED path
(`SELECT Order`) is still refused. Qualified (`Products.Order`) works, and the constructor always
qualifies. Widening it would mean telling a bare keyword apart from every clause that starts with
one — a grammar decision, not a bug fix, and not one to make under a generator run.
