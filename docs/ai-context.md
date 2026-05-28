# AI context — condensate for code-generating sessions

> **Audience:** any LLM-driven session (Claude / Codex / GPT-class) that
> opens this repository cold and is asked to extend a configuration —
> add a metaobject, write a script module, draft a report. **Read this
> file first**, then drill into the topic docs linked at the bottom.
>
> **Last refreshed:** 2026-05-28.
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
embedded by default; PostgreSQL / SQLite / MySQL / ODBC also supported).

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
            │  OnPosting handlers, report queries
            │  → XML / JSON configuration files
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
| Metadata XML / JSON (`docs/configuration-compare.md`) | C++ source under `src/engine/**` |
| OES script (`.module` files inside metadata) | `*.vcxproj`, `CMakeLists.txt`, `Common.props` |
| Form layouts (visual designer XML) | Plugin DLLs (`simplePlugin.dll` pattern) |
| Spreadsheet templates (`SpreadsheetDocument` XML) | `enterprise.sln` solution structure |
| Reference / demo configurations under `examples/` | Build outputs, locale `.mo` / `.po` files |
| Tests against generated configs (codeRunner scripts) | DB driver code under `databaseLayer/` |

**Reason:** OES enforces invariants in C++ (RAII, prepared statements,
TX scopes, throw-by-value). When AI writes C++, those invariants
become advisory rather than structural. When AI writes script +
metadata, the C++ runtime gates everything: a misnamed table fails
fast at compile, a missing access right surfaces a typed exception,
an unsafe SQL is impossible (you do not have access to the raw SQL
layer).

---

## 4. The five invariants you MUST respect

These are non-negotiable. Violating them = the architect rejects the
PR.

### 4.1 No raw SQL

Script-level data access goes through metaobject managers:

```c
// CORRECT — OES script
var found = Catalogs.Products.FindByCode("APPLE-01");
var balance = ChartsOfAccounts.Hozraschetnyi.Sub("62").Balance(EndOfMonth());

// WRONG — you cannot do this in OES script (no raw SQL surface)
//   db_query(...)   ← C++ only
```

When you need a complex query, use **LINQ** (block or chain syntax):

```c
var bigOrders = from o in Documents.Orders
                where o.Total > 10000 and o.Date >= BeginOfYear()
                select o;
```

Full LINQ surface: `docs/linq.md` (31 chain ops + block syntax).

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

### 4.3 Keyword casing — PascalCase, one canonical form

OES keywords have ONE accepted casing — the one in
`translateCode.cpp::s_listKeyWord`. The lexer is case-sensitive. If
you generate `foreach` lowercase or `For Each` two-word, the lexer
silently treats the token as an identifier and the script breaks at
runtime, not compile.

| Correct | Wrong |
|---|---|
| `If ... Then ... EndIf` | `if`, `IF`, `If ... Then ... End If` |
| `Foreach x In coll Do ... EndDo` | `For Each`, `ForEach`, `foreach` |
| `Procedure F() ... EndProcedure` | `Sub`, `procedure`, `Function` (when no return) |
| `Function F() ... EndFunction` | `function`, `Procedure` (when there is a return) |
| `Return` | `return`, `Ret` |
| `Var` (or implicit) | `var` (in VES); `dim`, `let` |

CES (C-style) and VES (Visual-Basic-style) are two surface modes
that compile to the same bytecode. **CES is the default for new
configurations** since 2026-05-10. Pick CES unless the existing
configuration is already VES.

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
  the designer preview. Guard with `if not Session.IsDesigner then
  ...` if the side-effect is undesired in preview.
- Reports and dynamic lists fetch lazily through `Get*Fetch` —
  designer's form preview doesn't run them. You don't have to gate
  this manually; the platform gates it for you.

### 4.5 Const-meta — runtime sees immutable metadata

You can read metadata properties from any script: `meta =
Document.Invoice.Metadata; Message(meta.Synonym);`. You cannot
mutate metadata at runtime. To change a metaobject's properties,
edit the metadata source (Designer or your generated config) and
re-deploy. There is no `SetSynonym(...)` in script.

---

## 5. Metadata — quick reference

The 11 metaobject types and their script-side namespace:

| Type | Script namespace | Use for |
|---|---|---|
| `Catalog` | `Catalogs.<Name>` | Reference lists (products, contractors, units) |
| `Document` | `Documents.<Name>` | Business operations (sales, payments, postings) |
| `Enumeration` | `Enums.<Name>` | Fixed-value lists (status, type discriminator) |
| `Constant` | `Constants.<Name>` | Singleton values (company name, tax ID) |
| `InformationRegister` | `InformationRegisters.<Name>` | Periodic / dimensioned facts (price lists, exchange rates) |
| `AccumulationRegister` | `AccumulationRegisters.<Name>` | Quantitative balances (inventory, AR / AP) |
| `AccountingRegister` | `AccountingRegisters.<Name>` | Double-entry bookkeeping (see `docs/register-totals-strategy.md`) |
| `ChartOfAccounts` | `ChartsOfAccounts.<Name>` | Account hierarchy for AccountingRegister |
| `ChartOfCharacteristicTypes` | `ChartsOfCharacteristicTypes.<Name>` | Subconto-type definitions |
| `DataProcessor` | `DataProcessors.<Name>` | Interactive tooling (utilities, batch operations) |
| `Report` | `Reports.<Name>` | Read-only output (analytics, statements) |

Configuration XML schema: see `docs/configuration-compare.md` +
sample exports under `examples/`.

---

## 6. Concept glossary

Mapping common ERP / business-application concepts to their OES form:

| Concept | OES equivalent | Notes |
|---|---|---|
| Reference list / lookup table | Catalog | Hierarchical or flat; predefined items supported |
| Business transaction document | Document | Posting via `OnPosting` handler |
| Periodic dimensioned fact table | InformationRegister | Dimensions / resources / optional periodicity |
| Quantitative balance ledger | AccumulationRegister | Balance vs turnover modes |
| Double-entry bookkeeping ledger | AccountingRegister | Account + characteristic-type bindings |
| Chart of accounts | ChartOfAccounts | Binds to ChartOfCharacteristicTypes |
| Analytical-dimension type catalog | ChartOfCharacteristicTypes | Per-account dimension definitions |
| Fixed-value enum | Enumeration | Closed set; no runtime add |
| Singleton config value | Constant | One row, one value, typed |
| Interactive utility / batch tool | DataProcessor | Forms + script, no persisted business data |
| Read-only analytical output | Report | Form + LINQ / register query |
| Save an object | `object.Write()` | Record-locks enforced; see `docs/record-locks.md` |
| Add an accounting movement | `document.RecordSets.<RegisterName>.Add()` | AccountingRegister WIP; see `register-totals-strategy.md` |
| Built-in query language | LINQ block / chain syntax | `from ... where ... select`; see `docs/linq.md` |
| Document posting handler | `OnPosting` script in Document module | Writes register movements |
| Document date | `document.Date` | `ibDateTime` type |
| Data-composition / pivot system | — | Not present; reports are hand-written today |
| Form model | Form / VisualHost | Single form layer; same form runs in Desktop and Web |
| Client tier | Desktop frontend / Web frontend | Two parallel DLLs (`frontend.dll` / `wfrontend.dll`) |

---

## 7. Where to look when you need detail

Drill into these only when the task touches the specific area.

| Topic | Canonical doc |
|---|---|
| Project bootstrap, layers, modules | `docs/ARCHITECTURE.md` |
| Build / clone / submodules | `docs/BUILD.md` |
| Configuration text format (XML / JSON) | `docs/configuration-compare.md` |
| OES script — full language | `docs/lambda.md`, `docs/closure-capture.md`, `docs/linq.md`, `docs/eval-scope-refactor.md` |
| Metadata system, CLSIDs, inheritance | `docs/ARCHITECTURE.md` §Metadata System |
| Sessions, threading, registry | `docs/session-registry.md` |
| DB access, connection pool, transactions | `docs/connection-pool.md` |
| Concurrent-write protection | `docs/record-locks.md` |
| Distribution (Firebird mesh / shara) | `docs/firebird-mesh-driver.md` |
| Lists / trees / paging | `docs/paging-design.md` |
| Forms architecture | `docs/backend-frontend-split.md`, `docs/ARCHITECTURE.md` §Form System |
| UI palette / colours | `docs/ui-palette.md` |
| Web frontend | `docs/web/` |
| Accounting registers (WIP) | `docs/register-totals-strategy.md` |
| Syntax helper / inline reference | `docs/syntax-helper-design.md` |

---

## 8. Self-test path before submitting

Before declaring "done", verify your generated configuration:

```
1. classChecker.exe <config-path>
     — metadata consistency: bindings resolve, no orphan refs,
       attribute types valid, predefined-attribute subclass lists
       are additive.

2. codeRunner.exe --module <module> --script "<expr>"
     — execute a script expression against the loaded config.
       Use this to smoke-test postings, computations, reports.

3. designer.exe <config-path>
     — open in Designer for visual review and live debugging.
       Press F5 to launch Enterprise mode against the same DB.

4. (If accounting touched) — verify Balance / Turnovers via
   codeRunner before claiming "complete".
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
> Generate metadata XML/JSON and OES scripts in CES (preferred) or
> VES; never touch C++; verify with classChecker + codeRunner;
> surface anything that needs C++ change or schema migration to the
> architect.**
