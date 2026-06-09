# Query Language — Architecture Arc

> **Status:** **LANDED (experimental working copy) — L1 release, L2 release-candidate,
> L3 read + write + aggregation + dot-walk realized.** The lower sections (§4–§22) are the
> design this converged from; the snapshot below is what is actually in the tree.
>
> **Last updated:** 2026-06-07.
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
>   (subconto); cross-DBMS validation beyond Firebird; the L4 text-query parser.
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
- `GetQueryTableName` / `GetQueryMetaID` — physical layout.
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
the metaobject keeps only **primitives** (`FindObjectByFilter` / `GetTableNameDB` /
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
