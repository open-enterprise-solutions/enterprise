# Query Language — Architecture Arc

> **Status:** **LANDED (experimental working copy) — L1 release, L2 release-candidate,
> L3 read + write + aggregation + dot-walk realized.** The lower sections (§4–§22) are the
> design this converged from; the snapshot below is what is actually in the tree.
>
> **Last updated:** 2026-06-11.
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
>   `ibBackendQueryColumn` — auto-joins a reference path, e.g. `Номенклатура.Производитель.
>   Наименование`). Backing-blind: a polymorphic `ibMetaResultSource` (DB cursor OR RAM
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
>   `ibComputedRegisterQueryable<TReg>` forwarding base (`query/computedRegisterQueryable.h`)
>   and one lowering header (`metaCollection/partial/registerQueryLowering.h`:
>   `ibRegFieldsOf`/`ibRegValueField`/`ibRegCompositeIR`).
> - **Open (next arcs):** the L3 door's read path still trafficks in
>   `ibValueMetaObjectAttributeBase`, not `ibBackendQueryColumn` — the coupling is via the
>   attribute's **metaID** (materialize / RAM read / sort-dedup / value-binding spread),
>   which the column deliberately lacks (§22.4b). Fully decoupling = the **column-based
>   lowering** arc (identity metaID→`GetPhysicalName`, materialization + binding derived from
>   `(physical, type)`). Also pending: balances/turnovers as DB-backed virtual tables (no RAM
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
>   with `_RRRef` as two link keys until cleaned. (DDL `_RRRef` unique index ships on
>   createMetaTable only — existing tables need a migration step.)
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
> - **Still open:** the DDL migration gap above (existing tables need the `_RRRef` unique index);
>   balances/turnovers as DB-backed virtual tables (totals-table arc); the accounting register
>   (subconto); cross-DBMS validation beyond Firebird.
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
>   **register RECORDER (Регистратор)** of 15+ document types where the pulled field exists on one
>   (one `LEFT JOIN`, not 15; `COALESCE` across the matching branch(es)). Wired into projection,
>   WHERE (flat + boolean tree), and ORDER BY.
> - **Aggregation over dot-walk** — single-source `GROUP BY Producer.Region` and `SUM(Producer.Weight)`
>   via the same chain (`GroupBy(path)` / `Aggregate(fn, path, alias)`).
> - **Hierarchical TOTALS BY dot-walk** — incl. a self-referential dimension (`Parent.Code`) and
>   **computed / constant measures** (`SUM(1 AS test)`), both via **synthetic scalar columns** owned by
>   the output schema (the metaID-keyed totals fold reads them like real resources).
> - **Const CAST** — a bare projected constant is wrapped `CAST(? AS <type>)` (FB `-804` otherwise).
> - JOIN kinds completed: INNER / LEFT / RIGHT / FULL / CROSS (`ON TRUE`).
> - **Still open:** TOTALS over a JOIN / UNION (multi-source); a TOTALS BY a reference / composite
>   dot-walk leaf (scalar today); a composite NON-scalar leaf beyond the last segment; arithmetic /
>   CASE as an aggregate input (a dot-walk leaf input works); aggregate subqueries.
>
> ### Update 2026-06-11 (2) — L4 executable subset: TOP, computed WHERE / aggregate inputs,
> ### aggregate subqueries, UNION dedup
>
> The "parsed but not yet executed" list shrank. Landed:
>
> - **`SELECT TOP n`** (keyword `Top`, 1С «ПЕРВЫЕ») — a row limit on the SELECT core. Lowering:
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
> - **Composite NON-scalar dot-walk leaf** — `SELECT Регистратор.Контрагент` (a composite at ANY
>   segment, the leaf itself a reference / enum / composite) executes: the recursive branch walk
>   (same fork + peek as the scalar case) collects the leaf occurrences, each branch contributes
>   the leaf's FULL field spread, and the spreads merge PER SUFFIX with `COALESCE` under the alias
>   prefix — `GetColumnObject` reassembles the object off the merged spread exactly like a
>   single-target projection (a row matches at most one branch). Suffix alignment rides the
>   representative (first) branch. The previous per-type single-field COALESCE — which the reader
>   could not reassemble — is gone. A dot-walk WHERE / ORDER BY on such a leaf now THROWS
>   (previously the condition / sort key silently DROPPED — wrong rows / wrong order).
>
> **Still open (the deep tail, each its own arc):** TOTALS over a JOIN / UNION (two totals mechanisms
> + snapshot seq-keying), TOTALS BY a reference / composite dot-walk leaf (scalar today), UNION
> branches carrying JOIN / TOTALS, RAM DISTINCT over the stitch (DedupeRows is ready — needs the
> door wiring), and WHERE / ORDER on a composite non-scalar leaf (projection works; the predicate
> needs per-branch DecomposeEquality OR-folded).
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
> engine — met through a ~250-line adapter; none of the 31 RAM pipeline ops changed.
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
   per call site with per-driver `if`s — e.g.
   `metaCollection/partial/list/listSqlBuilder.cpp:14`
   (`if FIREBIRD → "SELECT FIRST N" else "LIMIT N"`), plus `BYTEA` vs `BLOB`,
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

## 14. Level 3 — metadata surface (placeholder, not yet designed)

> Deliberately a stub. There is no point designing L3 before L2 exists. Recorded
> here only so the shape L2 must serve is not forgotten.

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
class does not drag in the full `queryable.h` / `tableInfo.h` weight.

A **constant** is therefore both: a column (it derives `ibValueMetaObjectAttribute`)
**and** a queryable (`ibValueMetaObjectConstant` now also derives
`ibBackendQueryable` for its single-row `sys_const` table). `GetConstValue` reads
through the door — column detail = the attribute, table detail = the queryable.

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
  through the door — and the next level recurses inside each value). `GetParentColumn()` on the
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

So a runtime call and a composed query hit one identical path — `…Цены.СрезПоследних`
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
select   := SELECT [DISTINCT] selList FROM source { join }
            [WHERE predicate] [GROUP BY exprList [HAVING predicate]]
            [ORDER BY orderList] [TOTALS aggregate {',' aggregate} BY totalDim {',' totalDim}]
proj     := (aggregate | columnPath) [ [AS] alias ]
aggregate:= (SUM|MIN|MAX|AVG) '(' columnPath ')' | COUNT '(' ('*'|columnPath) ')'
predicate:= andE {OR andE};  andE := notE {AND notE};  notE := NOT notE | comparison
comparison := primary [ cmpOp primary | [NOT] LIKE primary
                      | [NOT] IN '(' … ')' | IS [NOT] NULL | [NOT] BETWEEN primary AND primary ]
totalDim := columnPath [HIERARCHY | ELEMENTS]
```
`TOTALS … BY …` is the hierarchical-subtotals clause (1С `ИТОГИ … ПО …`): aggregates
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
  When the join can't co-locate (RAM stitch), a boolean WHERE **errors clearly** — the stitch
  materialises each leaf with its own flat conditions, so a cross-leaf `OR` can't be pushed per leaf;
  erroring (vs dropping an OR branch) keeps the result honest. A post-join RAM filter would lift it.
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
- **Parser is COMPLETE; the lowering realizes the executable subset.** The parser now accepts
  **arithmetic** (`+ - * / %`, standard precedence), **CASE** (`CASE WHEN … THEN … ELSE … END`),
  **UNION [ALL]**, and **IN (subquery)** — but the column-based L3 door does not execute computed
  expressions or set operations yet, so the lowering throws a clear "parsed but not yet executed" for
  these (they are an L3-expression-layer / composer-wiring concern, sibling to L4-2). The AST carries
  them (`ibQueryExprKind::Arith` / `Case`, `ibQuerySelect::m_unions`, `In.m_subquery`) so the future
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
  it `AS alias` in `BuildPageIR`. Read back by alias. **Single-source non-aggregate** only (a JOIN /
  aggregate computed column errors clearly); arithmetic / CASE in a WHERE / an aggregate argument still
  errors (the predicate / aggregate take a column, not an expression — a later step).
- **UNION EXECUTES.** `SELECT … UNION [ALL] SELECT …` runs: each branch's core is wrapped as an
  `ibSubqueryQueryable` (`WrapSelectAsQueryable`), stacked with `From(b0).Union(b1)…`, and the composer
  realizes the stack (RAM union today; columns matched BY NAME across branches). The trailing `ORDER BY`
  applies to the whole. A branch may not use JOIN / TOTALS yet; UNION-vs-UNION-ALL dedup is a later step.
- **IN (subquery) EXECUTES** — `WHERE x IN (SELECT y FROM …)` materialises the (uncorrelated) inner
  SELECT's single column eagerly into a value list, then lowers as `Or(x = v …)` (the same path as a
  literal IN list; dot-walk leaf supported). An empty result → a contradiction leaf (matches nothing);
  `NOT IN ()` → everything. The inner runs once through a local `ibSubqueryQueryable`.
- **Parsed but not yet executed (clear error):** arithmetic / CASE in a WHERE or an aggregate argument
  (a reference dot-walk LEAF as an aggregate input now DOES execute — §23.9 — but an *expression* there
  does not), and **aggregate subqueries** (`FROM (SELECT cat, SUM(x) AS s … GROUP BY cat)`). Access is
  **SELECT-only** for user text (§14). The aggregate subquery is the one **provider-level** gap left —
  `ibSubqueryQueryable` must expose the aggregate-alias columns (synthetic raw columns) + run
  `SelectAggregate` in `ComputeRows` + a RAM post-filter for the outer's pushed-down conditions; the
  door does not yet surface an aggregate's OUTPUT schema, so it wants a focused door + provider change.

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
`LowerLambdaColumnPath` are thin wrappers over the SAME file-local `BuildWherePredicate` /
`ResolvePath` the text language lowers through (sources = the one queryable, dot-walk allowed
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
  `OnAfterRunMetaObject` — `ibRegisterQueryableSource(&m_queryable)` (a light hook) after checking
  `!(flags & onlyLoadFlag)` (the designer's saved baseline loads its objects load-only — skip) —
  and `ibUnregisterQueryableSource(&m_queryable)` in `OnBeforeCloseMetaObject`. Wired in the
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
  path; an unresolvable path leaf throws (no dropped OR branch). `IN` / `IS NULL` nodes stay plain.

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
to N target types (a register's **Регистратор / recorder** is the headline case: 15+ document types). A
field pulled through it (`SELECT Регистратор.SomeField`) commonly exists on only ONE type. The resolution
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

**Open (totals):** TOTALS over a JOIN / UNION (multi-source — two totals mechanisms + snapshot seq-keying),
and a TOTALS BY a reference / composite dot-walk leaf (today scalar leaves only).

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
catalog value's seed sets its **own data-reference** (`_RRRef` = [its guid][this metaID], guid == uuid)
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
