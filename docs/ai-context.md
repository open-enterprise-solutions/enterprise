# AI context — condensate for code-generating sessions

> **Audience:** any LLM-driven session (Claude / Codex / GPT-class) that
> opens this repository cold and is asked to extend a configuration —
> add a metaobject, write a script module, draft a report. **Read this
> file first**, then drill into the topic docs linked at the bottom.
>
> **Last refreshed:** 2026-07-13; § 5–§ 8 updated 2026-09-10 (calculation register, chart of
> calculation types, recalculation — built 2026-09-10, committed 2026-09-11).
>
> **Not a substitute for** `CLAUDE.md` (general onboarding) or topic
> docs (deep mechanics). This file is the 5-minute contract.

---

## 1. What this project is

**Open Enterprise Solutions (OES)** — a C++17 cross-platform low-code
enterprise application platform. Developers define business
applications through **metadata** (object types, forms, modules) and a
built-in **scripting language**, not by writing low-level code. The
runtime executes compiled bytecode, renders forms through wxWidgets,
and stores all application data in a relational database (Firebird
embedded by default; PostgreSQL also supported for production, ODBC as the MSSQL base, SQLite for tests and logging).

---

## 2. The workflow you are part of

```
   Business analyst (prose, RU/UK/EN)
            │
            │  "We need invoice posting with 20% VAT;
            │   debit 62 / credit 90.01 for revenue,
            │   debit 90.03 / credit 68.02 for the tax."
            ▼
   YOU (the AI) — generate metadata + script modules
            │
            │  Catalog / Document / Register definitions
            │  Posting handlers, report queries
            │  → configuration metadata (serialized via ibDataNode)
            │  → OES script modules
            ▼
   Architect (human) — reviews via Configuration Compare,
                       runs against test data,
                       merges to develop
            │
            ▼
   End user — runs enterprise.exe / web client
```

Your job is to translate the analyst's intent into **valid OES
metadata + scripts** that compile, run, and produce the expected
business behaviour. The architect validates by running.

---

## 3. What you generate vs what you don't touch

| Allowed | NOT allowed without explicit architect sign-off |
|---|---|
| Metadata definitions (object types, bindings) | C++ source under `src/engine/**` |
| OES script modules inside metadata | `*.vcxproj`, `CMakeLists.txt`, `Common.props` |
| Form layouts (visual designer) | Plugin DLLs (`simplePlugin.dll` pattern) |
| Spreadsheet templates (`SpreadsheetDocument`) | `enterprise.sln` solution structure |
| Test configurations exercised by codeRunner scripts | Build outputs, locale `.mo` / `.po` files |
| Tests against generated configs (codeRunner scripts) | DB driver code under `databaseLayer/` |

**Reason:** OES enforces invariants in C++ (RAII, prepared statements,
TX scopes, throw-by-value). When AI writes C++, those invariants
become advisory rather than structural. When AI writes script +
metadata, the C++ runtime gates the ordinary roads: a misnamed table
fails fast at compile, a missing access right surfaces a typed
exception, and every data access you are meant to write goes through
a manager, a query or LINQ — none of which can express an unsafe SQL,
because none of them lets you write SQL at all.

⚠ **One hatch exists, and it is a hatch rather than a road.** The
`DatabaseLayer` script type (`system/value/valueDatabaseLayer.cpp`)
vends `RunQuery` / `RunQueryWithResults` / `PrepareStatement` straight
onto the session's connection. What it hands over is the raw L1
driver, so a statement written through it goes past the dialect layer
(one spelling per engine), past the L3 door, past the row-level
access policy and past paging — and it is yours to get right on four
databases. It is kept for the service work that genuinely has no
other door; it is **not** a fallback for a query you found awkward to
express, and § 4.1 below is still the rule.

Since 2026-09-15 the hatch has a lock: outside the Designer, a
session without the **Data administration** right is refused at the
first call (`ibBackendAccessException`, "DatabaseLayer runs raw SQL
past the access policy - it needs the Data administration right").
So a user whose rows the policy restricts can no longer read past
the policy through it; an administrator still can, and that is the
honest reading of the right.

---

## 4. The five invariants you MUST respect

These are non-negotiable. Violating them = the architect rejects the
PR.

### 4.1 No raw SQL

Script-level data access goes through metaobject managers:

```c
// CORRECT — OES script
var found = Catalogs.Products.FindByCode("APPLE-01");
var account = ChartsOfAccounts.Hozraschetnyi.FindByCode("62");
var balance = AccountingRegisters.Hozraschetnyi.Balance(EndOfMonth(), account);

// WRONG — do not reach for SQL from script
//   db_query(...)                          ← C++ only, not a script name at all
//   New DatabaseLayer().RunQuery("…")      ← EXISTS, and is not yours to use here
```

The second line is the honest part: the hatch is reachable (§ 3), so
"no raw SQL" is a **rule**, not an impossibility. A statement written
through it is yours to keep correct on Firebird, PostgreSQL, SQLite
and ODBC at once, and it is invisible to the access policy — so
a report built on it shows rows its reader may not see (and fails
outright for a reader without the Data administration right).

When you need a complex query, use **LINQ** (block or chain syntax):

```c
var bigOrders = from o in Data.Documents.Orders
                where o.Total > 10000 and o.Date >= BeginOfYear()
                select o;
```

The source is `Data.Documents.Orders` — the rows of the database. `Documents.Orders` is the object
manager: it has no rows, and walking it fails at run time. The block form reads every row into memory
and filters there (`linq.md` § 0.1); for a filter the database should apply, write the chain
(`Data.Documents.Orders.Where(Function(o) { Return o.Total > 10000; })` — a lambda is a `Function`,
the language has no `=>`) or a query.

Full LINQ surface: `docs/linq.md` (34 chain ops + block syntax — the live count is the
`ibLinqMethod` enum in `compiler/value.h`).

### 4.2 Throw by value, catch by const reference

```c
// In OES script, errors are raised via Raise():
if not document.Valid() then
    Raise("Document is not valid");
endif

// In C++ (you should not be writing this, but if reading it):
//   throw ibBackendCoreException(...);
//   catch (const ibBackendException& err) { ... }
```

Never `catch(...)` to swallow business errors. Cleanup `catch(...)` is
only in destructors and RAII teardown.

### 4.3 Keywords — one spelling, mode-gated fences

Keyword matching is **case-insensitive** (`IsKeyWord` normalizes via
`MakeUpper`), so `if` / `IF` / `If` all resolve to the same keyword. But
each keyword has exactly ONE spelling — the one in
`translateCode.cpp::s_listKeyWord`. A made-up synonym or a two-word form
(`For Each`, `End If`) is not a keyword: the lexer treats it as a plain
identifier and the script breaks at the parser, not at runtime. Use the
canonical spelling shown below.

| Use | Not a keyword (treated as identifier / rejected) |
|---|---|
| `If ... Then ... Endif` | `End If` (two words) |
| `Foreach x In coll Do ... EndDo` | `For Each`, `ForEach` |
| `Procedure F() ... EndProcedure` | `Sub`; `Function` when there is no return |
| `Function F() ... EndFunction` | `Procedure` when there is a return |
| `Return` | `Ret` |
| `Var` (or implicit) | `Dim`, `Let` |

CES (C-style) and VES (Visual-Basic-style) are two surface modes that
compile to the same bytecode. **CES is the default for new configurations**
since 2026-05-10. Pick CES unless the existing configuration is already VES.
A code-style gate hides VES-only block fences (`Then` / `Do` / `End*`) when
CES is active — they read as ordinary identifiers there, so do not mix VES
fences into CES modules.

```c
// CES (default for new configs)
function Compute(x) { return x * 2; }
foreach (item in items) { Message(item.Name); }

// VES (legacy / existing configs)
Function Compute(x) Return x * 2 EndFunction
Foreach item In items Do Message(item.Name) EndDo
```

### 4.4 Designer = compile-only; runtime fetches data

Generated metadata is loaded by the **Designer** to compile and
validate. Runtime (Enterprise / Web / Daemon) is where data flows
happen. When generating control logic, remember:

- A form opened in Designer's **form editor** must not assume data
  exists. Code in `OnOpen` that calls `Items.Fetch()` will fail in
  the designer preview. There is **no script-visible guard for this** —
  no `Session` global and no `IsDesigner` exist; do not emit one.
  Write `OnOpen` so it tolerates an empty source instead.
- Reports and dynamic lists fetch lazily through `Get*Fetch` —
  designer's form preview doesn't run them. You don't have to gate
  this manually; the platform gates it for you.

### 4.5 Const-meta — runtime sees immutable metadata

You can read metadata properties from any script: `meta =
Document.Invoice.Metadata; Message(meta.Synonym);`. You cannot
mutate metadata at runtime. To change a metaobject's properties,
edit the metadata source (Designer or your generated config) and
re-deploy. There is no `SetSynonym(...)` in script.

### 4.6 Captions — every declared language, in one string

A caption (a synonym, a template cell's text, a report variant's name) is written in
**every language the configuration declares**, as one string:
`en = 'Employee'; ru = 'Сотрудник'; uk = 'Працівник';` — an apostrophe inside a text is
doubled (`en = 'the document''s date';`). A caption sent bare fills only the configuration's
own language, and a Russian word sent that way is stored as the English caption; the MCP tools
refuse it while more than one language is declared. What a module builds — a message, a sheet
title, a month name — goes through `Tstr` with the same form (`Tstr(text)` reads the user's
language). Data (a person's name, a predefined item's description) is data, not a caption.

---

## 5. Metadata — quick reference

The 13 metaobject types and their script-side namespace:

| Type | Script namespace | Use for |
|---|---|---|
| `Catalog` | `Catalogs.<Name>` | Reference lists (products, contractors, units) |
| `Document` | `Documents.<Name>` | Business operations (sales, payments, postings) |
| `Enumeration` | `Enumerations.<Name>` | Fixed-value lists (status, type discriminator) |
| `Constant` | `Constants.<Name>` | Singleton values (company name, tax ID) |
| `InformationRegister` | `InformationRegisters.<Name>` | Periodic / dimensioned facts (price lists, exchange rates) |
| `AccumulationRegister` | `AccumulationRegisters.<Name>` | Quantitative balances (inventory, AR / AP) |
| `AccountingRegister` | `AccountingRegisters.<Name>` | Double-entry bookkeeping (see `docs/register-totals-strategy.md`) |
| `ChartOfAccounts` | `ChartsOfAccounts.<Name>` | Account hierarchy for AccountingRegister |
| `ChartOfCharacteristicTypes` | `ChartsOfCharacteristicTypes.<Name>` | The KINDS an account dimension can be («Вид аналитики»), each narrowing what values it admits |
| `ChartOfCalculationTypes` | `ChartsOfCalculationTypes.<Name>` | The calculation TYPES a calculation register records (salary, bonus, sick leave, income tax) and how they interact — three relation sections on every type: `Displacing`, `Base`, `Leading` |
| `CalculationRegister` | `CalculationRegisters.<Name>` | Calculation RESULTS (payroll accruals, deductions): records in force over days, cut by the types that displace them, computed on a base of other types, marked for recalculation when what they were computed from changes. Bound to exactly one chart of calculation types; always subordinate to a recorder |
| `DataProcessor` | `DataProcessors.<Name>` | Interactive tooling (utilities, batch operations) |
| `Report` | `Reports.<Name>` | Read-only output (analytics, statements) |

Two further global namespaces exist for the external (out-of-configuration) variants:
`ExternalDataProcessors.<Name>` and `ExternalReports.<Name>`. The full registered set is the
`AppendProp` list in `backend/moduleManager/globalContextManager.cpp` — 17 namespaces (the 15
metaobject ones plus `ScheduledJobs` and `SessionParameters`). The LINQ door has the same kinds under
`Data.` — `Data.CalculationRegisters.<Name>`, `Data.ChartsOfCalculationTypes.<Name>`.

Configuration is serialized through the `ibDataNode` tree (binary via
`ibBinaryProvider`); the JSON side is the one you read, never the one you load back — its
`Read` is a complete parser but nothing calls it, the view being lossy by design. See
`docs/configuration-compare.md` for the Compare/Merge feature.

### 5.1 Calculation register — what a generator must get right

**Choose it only when the calculation types interact.** State that changes at points in time (a
salary RATE, a position, a tax rate) is an `InformationRegister`; amounts that add up to a balance
(settlements with employees, year-to-date taxes) are an `AccumulationRegister`. A
`CalculationRegister` is for results whose types displace one another over days (a sick leave cuts
the salary), take their base from other types' results (a bonus of 10 % of last month's salary), or
go stale when those change. Design and measurements: `docs/payroll-arc.md` § 11.

**The chart** (`ChartOfCalculationTypes`) — one element per calculation type.

| Property | Values | Meaning |
|---|---|---|
| `UseActionPeriod` | boolean | the types are in force over DAYS. A register may keep action periods only on a chart with this on |
| `BaseDependence` | `None` / `ByActionPeriod` / `ByRegistrationPeriod` | which period of a base record puts it into a base; `GetBase` on a register whose chart says `None` refuses |
| `BaseCharts` | a set of charts | other charts whose types the `Base` and `Leading` sections may name (deductions over accruals) |

| Section (a tabular section of each element) | A row on type T naming N means | N may come from |
|---|---|---|
| `Displacing` | N displaces T — T's days under N's record are cut | this chart only |
| `Base` | N's results are part of T's base | this chart + `BaseCharts` |
| `Leading` | a change to N's records makes T's records stale | this chart + `BaseCharts` |

No priority exists: a type is cut only by the types its own `Displacing` rows name, and the order in
which records are added decides nothing.

**The register** (`CalculationRegister`):

- **Bound to exactly one chart** — property `ChartOfCalculationTypes`, mandatory; the save refuses an
  empty one.
- **Always subordinate to a recorder**: keyed by (recorder, line), written only as a record set. No
  record manager, no record form — list form only. The posting document lists the register among the
  registers it writes into, and its module fills `RegisterRecords.<Register>`.
- **`Periodicity`** — `Day` / `Month` (default) / `Quarter` / `Year`: the grain of the registration
  period. The write moves `RegistrationPeriod` and `ActionPeriod` to the start of the period their
  date falls in.
- **Standard fields**: `CalculationType` (a reference into the bound chart); `RegistrationPeriod` —
  the register's ONE period, held as its first day, required; there is no `Period`. `Storno`
  (boolean). With `UseActionPeriod`: `ActionPeriod` (the month the record is FOR),
  `ActionPeriodStart` / `ActionPeriodEnd` (required). With `UseBasePeriod`: `BasePeriodStart` /
  `BasePeriodEnd`. Plus the recorder family's `Recorder`, `LineNumber`, `Active`.
- **A closed period is corrected, not re-posted**: the current run writes a STORNO line (the paid
  record's position — dimensions, type, action start/end, `ActionPeriod` — with `Storno = true` and the
  figures negated) and the corrected line beside it, both registered NOW and FOR the month corrected.
  A storno takes the latest earlier record of its position out of force (no actual action periods, no
  displacement, no base by action period) and answers the recalculation marks on it. Do not re-post a
  paid month's document.
- **Every period is a date without time, both ends included**: 12.06–16.06 is five days.
- Dimensions, resources and attributes as in any register. Displacement and the base run within
  equal dimension VALUES (one employee's sick leave cuts that employee's salary only); `GetBase`
  pairs the dimensions two registers share BY NAME, so give them the same names.

- **`UseRecalculation`** — boolean, off by default. On, the register keeps ONE table of recalculation
  marks whose columns are its own — `Recorder`, `CalculationType`, `ActionPeriod` (with action periods)
  and every dimension; a dimension added to the register is a column added to the marks. There is no
  `Recalculation` object and nothing else to declare: which types make which stale is the chart's
  `Leading` section. Leave it off where nothing is ever recomputed — a register without it writes no
  marks and publishes no `.Recalculation` source, so a module reading one fails by name.
- **`Schedule`** — with `UseActionPeriod` (hidden without it): the information register a record's days are
  counted against, as ONE value — the register; its RESOURCE holding a number that is the schedule's value
  (hours, work days); its DIMENSION holding a date that is the date; and, for each of its other dimensions, a
  DIMENSION of this register of the same type it links to (an employee's calendar links `Employee` to
  `Employee`). A register keeping action periods saves only a complete schedule, and the refusal names what is
  missing. Set it with `metadata_set_schedule`; it publishes `.ScheduleData`.

**What the database keeps.** The raw movements, and — where `UseRecalculation` is on — the marks. A
document writes its records and nothing else; a correction of an earlier month is a record of the current
one (a storno of what was paid, and the new figures, both registered now and FOR the month they correct).
The virtual tables are readings of the records. Who displaces whom is read from the chart's *Displacing*
section when the fact is read, so a change to the section acts at once on every record already written.

**Query sources:** the actual action period takes `(Period, BeginOfActionPeriod, EndOfActionPeriod, Condition)`,
all optional — as of when, for which days, then the condition. `Period` is a moment of REGISTRATION — the
periods up to the one it falls in, in the register's periodicity: `&EndOfJune` reads June as it stood when
June closed, a July correction of it left out; without it, every period is read, corrections included.
`BeginOfActionPeriod` / `EndOfActionPeriod` choose what is in force some day between the two (a turnover's
interval, of the ACTION period). The condition narrows inside the reading: equalities on the dimensions
narrow everything, on the type, the month, the recorder, and date ranges narrow the rows the table produces.

| Source | Rows |
|---|---|
| `CalculationRegister.<Register>` | the records as written |
| `CalculationRegister.<Register>.ActualActionPeriod` | one row per piece of a RECORD left after displacement, as the register lays a record out: `RegistrationPeriod`, `Recorder`, `LineNumber`, `CalculationType`, `ActionPeriod`, `ActionPeriodStart` / `ActionPeriodEnd` (the piece's bounds), the base period, `Active`, `Storno`, the dimensions and resources; `PositionStart` / `PositionEnd` the record's own action period. A storno is a row of its own with the pieces of the record it reverses and its figures turned round, so a sum nets out. `ActualActionPeriod(, &MonthStart, &MonthEnd, Employee = &Employee)` |
| `CalculationRegister.<Register>.ScheduleData` | where a `Schedule` is bound — one row per active RECORD: its own fields (as the register lays a record out, attributes included) and, for every resource of the schedule holding a number, four sums of it: `<Resource>ActionPeriod` (over the record's days), `<Resource>ActualActionPeriod` (over the pieces displacement leaves — the same pieces `ActualActionPeriod` reads), `<Resource>BasePeriod` (with a base period) and `<Resource>RegistrationPeriod` (over the whole registration period). The same four arguments as the fact: `ScheduleData(&Month, , , Employee = &Employee)`; the condition names the record's fields, a sum is refused there — put it in the WHERE around |
| `CalculationRegister.<Register>.Recalculation` | where `UseRecalculation` is on — the marks: the records to compute again. Columns: `Recorder` (the recorder of the stale record), `CalculationType`, `ActionPeriod` — the month the stale record is for, where the register keeps action periods — and every dimension of the register. The platform writes a mark whenever a set is written whose records lead the record: the Leading section of the chart, the dimensions two registers share matched by name, meeting in time. The platform also answers the marks: when the stale recorder is posted again, or when a storno of the position is written. Never clear marks by hand. A run corrects the marks plus the positions its own stornos already correct, and writes the difference as a storno and a new record of its own period |

**Manager verbs** (`CalculationRegisters.<Name>.…`): `CreateRecordSet()`, `CreateRecordKey()`,
`Get(Filter)` / `Get(Period, Filter)` (a value table of records; the second form narrows to one
registration period), `GetBase(Filter, Resources, Dimensions, Sections)` (the base of the records the
filter selects — the filter names the recorder at least), `Select()`, `GetForm`, `GetListForm`,
`GetTemplate`. A filter is a `Structure` and may name any field of the records —
`new Structure("Recorder", Ref)`; a key that names no field is refused by name.

**The base** — `GetBase` on the manager (above) or on a line of a record set,
`line.GetBase(Resources, Dimensions, Sections)`:

- `Resources` is an `Array` of `"Register.Resource"`. Each item is one column of the answer; several
  resources separated by commas are summed into one column.
- `Dimensions` is a `Structure`: this register's dimension → `"Register.Dimension"`, the base register's
  dimension that must hold the same value. Nothing is paired by name.
- `Sections` (optional) is an `Array` of `"Register.Field"` to break the base down by.

The answer is a value table with `LineNumber`, one column per resource item (named after its first
resource) and one column per section. Only types named in the chart's `Base` section count. The span a
record takes its base over is its `BasePeriodStart` / `BasePeriodEnd`; on a register without
`UseBasePeriod` it is the period the record is registered in, at the register's `Periodicity` (the month
of a monthly register).

`GetBase` reads STORED records. A line of the set being posted is not stored yet, so a posting takes the
base of its own movements from what it has just computed (a bonus is a percent of the salary the same
handler worked out), and does not write, read back and write again.

```c
// the chart: a sick leave displaces the salary; a bonus is computed on the salary
var sick = ChartsOfCalculationTypes.Accruals.CreateElement(); sick.Description = "SickLeave"; sick.Write();
var salary = ChartsOfCalculationTypes.Accruals.CreateElement(); salary.Description = "Salary";
var edge = salary.Displacing.Add(); edge.CalculationType = sick.Ref;
salary.Write();
var bonus = ChartsOfCalculationTypes.Accruals.CreateElement(); bonus.Description = "Bonus";
var feed = bonus.Base.Add(); feed.CalculationType = salary.Ref;
bonus.Write();

// a record set of one recorder — doc: a document that writes into Accrual; employee: a catalog reference
var rs = CalculationRegisters.Accrual.CreateRecordSet(); rs.Filter.Recorder.Set(doc.Ref);
var r = rs.Add();
r.Employee = employee; r.CalculationType = salary.Ref; r.RegistrationPeriod = Date(2025, 12, 1);
r.ActionPeriodStart = Date(2025, 12, 1); r.ActionPeriodEnd = Date(2025, 12, 31); r.Result = 31000;
rs.Write();
```

**A base of an earlier month** — a tax line of the `Deduction` register over the accruals:

```c
var resources = new Array(); resources.Add("Accrual.Result");
foreach (b in tax.GetBase(resources, new Structure("Employee", "Accrual.Employee")))
    tax.Result = Round(b.Result * 13 / 100, 2);
```

### 5.2 Accounting register — the names a query uses

Design and measurements: `docs/accounting-register-arc.md` (§ 7, § 8.3). What a generator must get
right is mostly NAMES, and they depend on the register's mode and on the table being read.

**The line.** `Correspondence` off (one-sided): `RecordType` + `Account` + `AccountDimension1..N`.
On: `AccountDr` + `AccountCr` + `AccountDimensionDr1..N` / `AccountDimensionCr1..N`, and no
`RecordType`. A field the mode does not use is not there at all — naming it is `unknown attribute`.

**A field kept per side.** A dimension or resource with `Balance` cleared (a currency, a quantity) has a
value per side in a correspondence register: `<Field>Dr` / `<Field>Cr` (`CurrencyDr`, `QuantityCr`).
`Balance` on means one value for the whole entry (the amount). It never means "no balance is reported".

**The virtual tables** (`AccountingRegister.<Name>.<Table>(…)`, arguments in this order):

| Table | Arguments | Account and fields are named |
|---|---|---|
| `Balance` | Period, AccountCondition, AccountDimensions, Condition | `Account`, `AccountDimension<i>`, `Currency` — one account per row, in either mode |
| `Turnovers` | Begin, End, Periodicity, AccountCondition, AccountDimensions, Condition, CorrAccountCondition, CorrAccountDimensions | as `Balance` (the two `Corr…` arguments exist in correspondence only and filter; there is no corresponding-account column yet) |
| `BalanceAndTurnovers` | Begin, End, Periodicity, FillMethod, AccountCondition, AccountDimensions, Condition | as `Balance` |
| `DrCrTurnovers` (correspondence only) | Begin, End, Periodicity, AccountConditionDr, AccountDimensionsDr, AccountConditionCr, AccountDimensionsCr, Condition | `AccountDr` / `AccountCr`, `AccountDimensionDr<i>` / `…Cr<i>`, `CurrencyDr` / `CurrencyCr` — the row is a pair |
| `RecordsWithAccountDimensions` | Begin, End, Condition | as the line — order and cap the lines in the query around it (`ORDER BY` / `TOP`, or `orderby … take` in LINQ), not in arguments |

Figures are `<Resource>` + the figure word: `AmountTurnoverDr`, `AmountBalance` (one signed number),
`AmountBalanceDr` / `…Cr` (folded by the account's type), `AmountOpeningBalanceDr`,
`AmountClosingGrossBalanceCr` (before the fold); `AmountTurnover` in `DrCrTurnovers`.

**Filters go INSIDE the table's brackets, never into the WHERE around it** — every register's condition is
applied before the fold, and it takes any predicate (NOT, OR, IN, IN (SELECT), a walk such as
`NOT Account.OffBalance`). `query_check` lists what is left outside as `tableParameters`.

**Conditions are written in the names the table publishes**, not the line's:

```sql
SELECT B.Account, B.AmountBalanceDr, B.AmountBalanceCr
FROM AccountingRegister.Ledger.Balance(&Date, Account IN HIERARCHY (&Accounts)) AS B

SELECT T.AccountDr, T.AccountCr, T.AmountTurnover
FROM AccountingRegister.Ledger.DrCrTurnovers(&Begin, &End, , AccountDr IN (&Debit)) AS T
```

`AccountDr` inside a `Balance` call is refused even in a correspondence register. A figure the row's
account keeps no accounting for (a quantity on a money-only account) reads EMPTY, not zero.

---

## 6. Concept glossary

Mapping common ERP / business-application concepts to their OES form:

| Concept | OES equivalent | Notes |
|---|---|---|
| Reference list / lookup table | Catalog | Hierarchical or flat; predefined items supported |
| Business transaction document | Document | Posting via the `Posting(Cancel, PostingMode)` handler |
| Periodic dimensioned fact table | InformationRegister | Dimensions / resources / optional periodicity — `NonPeriodic`, `WithinSecond`, `WithinDay`, `WithinMonth`, `WithinQuarter`, `WithinYear`; a record's period is kept at the start of its day / month / quarter / year, so a monthly register holds one record per month for a key and `Get(Period, …)`, `Filter.Period`, `SliceLast` / `SliceFirst` find it by any moment of the month. A record written earlier with a time (23:59 of a day) is that day's all the same: every key reads the period's span. An independent register's RECORD MANAGER addresses one record by its dimensions (and period): set them, then `Read()` answers whether a record is there and takes its values; `Write(True)` (the default) makes this the record under the key, replacing one that was there; `Write(False)` only adds and refuses a taken key in words; `Delete()` removes the record read, or the one the fields name. A RECORD SET written with no filter clears the register |
| Quantitative balance ledger | AccumulationRegister | `RegisterType` = `Balances` (the default: receipts and expenses by `RecordType`; reads `Balance`, `Turnovers`, `BalanceAndTurnovers`) or `Turnovers` (movements only summed over a period; reads `Turnovers` only, no `RecordType`) |
| Double-entry bookkeeping ledger | AccountingRegister | Account + characteristic-type bindings |
| Chart of accounts | ChartOfAccounts | Binds to ChartOfCharacteristicTypes |
| Analytical-dimension type catalog | ChartOfCharacteristicTypes | Per-account dimension definitions |
| Payroll earning / deduction types | ChartOfCalculationTypes | One chart per register (accruals, deductions); `Displacing` / `Base` / `Leading` sections say how types interact — § 5.1 |
| Payroll results (accruals, deductions) | CalculationRegister | Bound to one chart, subordinate to a recorder; NOT for rates or balances — § 5.1 |
| Salary rate / position / tax rate over time | InformationRegister (periodic) | State with milestones — not a calculation register |
| Year-to-date taxes, settlements with employees | AccumulationRegister | Amounts that add up to a balance |
| "Records to recompute" list | `UseRecalculation` on the CalculationRegister | Kept by the register's write; read it as `CalculationRegister.<Reg>.Recalculation` and correct the positions it names |
| Days a record is actually in force after displacement | `CalculationRegister.<Reg>.ActualActionPeriod` | One row per piece |
| Hours / work days of a schedule over a record's periods (the norm, what displacement leaves of it, the month's) | `CalculationRegister.<Reg>.ScheduleData` | One row per record; bind the `Schedule` first |
| Base of a dependent calculation (e.g. bonus on salary) | `line.GetBase(Resources, Dimensions, Sections)` / `CalculationRegisters.<Reg>.GetBase(Filter, Resources, Dimensions, Sections)` | Stored records only; types from the chart's `Base` section only |
| Fixed-value enum | Enumeration | Closed set; no runtime add |
| Singleton config value | Constant | One row, one value, typed |
| Interactive utility / batch tool | DataProcessor | Forms + script, no persisted business data |
| Read-only analytical output | Report | Form + LINQ / register query |
| Save an object | `object.Write()` | Record-locks enforced; see `docs/record-locks.md` |
| Add an accounting movement | `document.RegisterRecords.<RegisterName>.Add()` | Built 2026-08-13; see `accounting-register-arc.md`, `register-totals-strategy.md` |
| Built-in query language | LINQ block / chain syntax | `from ... where ... select`; see `docs/linq.md` |
| Document posting handler | `Procedure Posting(Cancel, PostingMode)` in the Document's object module; `UndoPosting(Cancel)` for undoing | Writes register movements. The name is the engine's: a procedure under any other name (`OnPosting`) is never called, and the document posts with no movements and no error |
| What becomes of a document's movements when it is posted again or its posting undone | the document's property `RegisterRecordsDeletion` | `Automatically` (default) — cleared before the posting handler runs and when the posting is undone; `OnUndoPosting` — kept when posted again (a set the handler fills replaces its own), cleared on undo; `Never` — the platform clears nothing on either, the configuration does. A deleted document takes its movements with it whatever the property says. A write is a clearing and a writing: nothing compares the old movements with the new |
| Document date | `document.Date` | `ibDateTime` type |
| Data-composition / pivot system | L5 data composer | Declarative filter/sort/grouping rendered into a query; see `docs/data-composer.md` |
| Form model | Form / VisualHost | Single form layer; same form runs in Desktop and Web |
| Client tier | Desktop frontend / Web frontend | Two parallel DLLs (`frontend.dll` / `wfrontend.dll`) |

---

## 7. Where to look when you need detail

Drill into these only when the task touches the specific area.

| Topic | Canonical doc |
|---|---|
| Project bootstrap, layers, modules | `docs/ARCHITECTURE.md` |
| Build / clone / submodules | `docs/BUILD.md` |
| Configuration Compare / Merge (`.oap`) | `docs/configuration-compare.md` |
| OES script — full language | `docs/lambda.md`, `docs/closure-capture.md`, `docs/linq.md`, `docs/eval-scope-refactor.md` |
| Query language / L2–L5 engine (LINQ → SQL) | `docs/query-language-arc.md` |
| Data composition (L5 filter/sort/group) | `docs/data-composer.md`, `docs/ram-composer-decoupling.md` |
| Row-level security / access policy (`Restrict`) | `docs/access-policy-rls.md`, `docs/data-policy-arc.md` |
| Metadata system, CLSIDs, inheritance | `docs/ARCHITECTURE.md` §Metadata System, `docs/schema-first-metadata.md` |
| Name binding / prepare-names | `docs/name-binding.md`, `docs/preparenames-bind-arc.md` |
| Sessions, threading, registry | `docs/session-registry.md` |
| DB access, connection pool, transactions | `docs/connection-pool.md` |
| Concurrent-write protection | `docs/record-locks.md` |
| Distribution (Firebird mesh / shara) | `docs/firebird-mesh-driver.md` |
| Lists / trees / paging | `docs/paging-design.md`, `docs/dynamic-list.md` |
| Forms architecture | `docs/backend-frontend-split.md`, `docs/ARCHITECTURE.md` §Form System |
| Form attribute binding (dot-path sources) | `docs/form-attribute-binding.md` |
| Doc/View subsystem (desktop + web fork) | `docs/docview-fork.md` |
| Copy / paste across the object model | `docs/copy-paste.md` |
| UI palette / colours | `docs/ui-palette.md` |
| Web frontend | `docs/web/` |
| Accounting registers | `docs/accounting-register-arc.md`, `docs/register-totals-strategy.md`, `docs/value-audit.md` |
| Payroll — calculation register, chart of calculation types, recalculation | `docs/payroll-arc.md` § 11 (as built), § 7.6 (measured) |
| Syntax helper / inline reference | `docs/syntax-helper-design.md` |

---

## 8. Self-test path before submitting

Before declaring "done", verify your generated configuration:

```
1. Load the config (designer.exe / enterprise.exe)
     — metadata consistency: bindings resolve, no orphan refs,
       attribute types valid, predefined-attribute subclass lists
       are additive.

2. codeRunner.exe
     — standalone GUI script runner (NO metadata, NO CLI flags):
       write a script in the editor and run it to smoke-test pure
       computations / language constructs. It cannot reference your
       config's metaobjects.

3. designer.exe <config-path>
     — open in Designer for visual review and live debugging.
       Press F5 to launch Enterprise mode against the same DB; run
       postings / reports there against test data.

4. (If accounting touched) — verify Balance / Turnovers in
   Enterprise mode before claiming "complete".

5. (If a calculation register touched) — post the documents in
   Enterprise mode, then read CalculationRegister.<Reg>.ActualActionPeriod
   (a displaced record comes back as several pieces), GetBase for a
   dependent type, and the recalculation after correcting a leading
   record (it must name only the recorders that read it).
```

If any step fails, do not submit — iterate. The architect's review
trusts that step 1 passed.

---

## 9. STOP signals — when to hand back to the architect

Stop and surface the issue rather than guessing:

| Situation | Why |
|---|---|
| Need to modify C++ source | Out of AI scope; needs human review for invariant preservation |
| Need to add a plugin (.dll) | Same as above |
| Schema change touches an existing populated table | Migration semantics need human judgement |
| Cross-driver SQL portability concern (FB→PG fall-back) | `ibSqlDialect` is partial; per-config workarounds need review |
| Conflicting requirements from BA prose | Loop with the analyst, not guess |
| Existing config has VES syntax — should new module also be VES? | Confirm style consistency before mixing |
| Performance / concurrency design decision | Architect picks (e.g. trigger-maintained totals vs LIVE aggregation) |
| Security / access-rights model — new role definition | Architect approves before propagating |

---

## 10. Conventions for the AI-generated PR

When the architect reviews your generated PR, they look for:

- **Commit messages in English**, `type(scope): summary` style — see
  recent `git log --oneline`. No "co-authored by AI" footers, no
  marketing language.
- **One concern per commit** — refactors, features, fixes separated.
- **Linked rationale** — what BA prompt drove what change. Architect
  should be able to verify "the analyst asked for X" → "metadata
  shows X" → "smoke-test confirms X".
- **No emojis** in code, comments, or commit messages. (UI strings
  and chat are different — see `feedback_english_code` in memory.)
- **No new dependency** without architect sign-off.

---

## 11. One-line summary

> **You are extending a low-code ERP through metadata and script.
> Generate metadata and OES scripts in CES (preferred) or VES; never
> touch C++; verify with codeRunner; surface anything that needs a C++
> change or schema migration to the architect.**
