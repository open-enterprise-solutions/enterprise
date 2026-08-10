# Query constructor — the shell over an AST we already have

> **Status: BUILT AND GREEN.** Five passes, each driven by Max running the window — §7 (the second),
> §7a, §7b, §7c, §7d. Last verified run: `Debug|x86` clean, **1071 passed / 0 failed / 5 skipped**
> (the skips want a git working copy). §0 maps where each piece lives; the tail of §7d names what is
> still open, including where the frontend is thinly tested.

---

## 0. What was built, and where

| Piece | Where |
|---|---|
| `ALLOWED`, `FOR UPDATE`, `INTO <name>`, `DROP <name>`, and the `;`-separated **package** | `query/queryAst.h`, `queryKeywords.h`, `queryLexer.cpp`, `queryParser.{h,cpp}`, `queryRender.{h,cpp}` |
| execution of a package (temp tables live the whole package; results by position) | `ibQueryLowering::ExecutePackage` (`query/queryLowering.cpp`) |
| `ALLOWED` at the door | `ibDataQueryBuilder::Allowed()` + `ibMakeEmptyQueryResult` (`query/dataQueryBuilder.{h,cpp}`) |
| the temp-table store + `TempTablesManager` + the INDEX lookup | `query/queryTempStore.{h,cpp}`, `ibValueTempTablesManager` |
| the script surface | `Query.Execute()` / `Query.ExecuteBatch()` / `Query.TempTablesManager` (`system/value/valueQuery.{h,cpp}`) |
| the MODEL — source catalogue + fields of a source | `query/queryConstructorModel.{h,cpp}` |
| the SHELL — nine tabs, live text, engine verdict | `frontend/win/dlgs/queryConstructor/queryConstructor.{h,cpp}` |
| the ONE expression editor (conditions, joins, aggregates, totals) | `.../queryExpressionDialog.{h,cpp}` |
| the grid models (links, conditions, unions + the field map) | `.../queryLinkModel`, `queryConditionModel`, `queryUnionModel` |
| the join diagram | `.../queryJoinDiagram.{h,cpp}` |
| host 1 — the dynamic list's *Arbitrary query* tab | `frontend/win/dlgs/listSettings/listSettings.cpp` |
| host 2 — the code editor's context menu, over a string literal | `frontend/win/editor/codeEditor/codeEditor.{h,cpp}` |
| the CASE builder - `CASE WHEN` as the ordered LIST it is | `.../queryCaseDialog.{h,cpp}` |
| tests | `tests/test_queryL4Parser.cpp` (language + package round trip), `tests/test_queryConstructor.cpp` (model), `tests/test_queryNaming.cpp` (naming, the composer wrap, DescribeOutput, grouping, the parameter table, aggregate types), `tests/test_frontendQueryConstructor.cpp` (the literal, read-only, the CASE round trip) |

**The rule the whole thing hangs on** (Max, 2026-08-06): *the constructor never judges a query.*
It renders the package and hands the text to `ibQueryParser::ParsePackage` — the same parser the
runtime uses — and shows whatever it throws, **verbatim**. There is no error reinterpreter, no
lenient pre-pass, no second definition of a valid query. Three places make that concrete:

- a **live verdict line** under the text pane, refreshed on every edit (`AskEngine`);
- **OK** re-parses the rendered package and hands back *what the parser read*, so a caller can
  never receive text the engine has not accepted;
- the **condition / aggregate** editors COMPOSE TEXT (`Price >= 100`, `SUM(Qty)`) and parse it
  through `ParseExpression`. `SUM(*)` is not rejected by a rule written in the dialog — the
  grammar already says only `COUNT` takes the star, and the parser's own words come back.

That is also why the "arbitrary condition" mode is not a weaker path: it is the same path with
the text typed instead of composed.

---

## 1. What a constructor actually is

Not a code generator. A **projection of one AST onto tabs**, in both directions:

```
selected text ──parse──▶ AST ──fill──▶ tabs
                          ▲             │ user edits
                          └──collect────┘
AST ──render──▶ text ──▶ back into the module, replacing the selection
```

Everything on the left and right of the tabs exists. What is missing is the middle: a
window that shows an `ibQuerySelect` and takes edits.

**Two consumers, and they are why this matters** (both named by Max, 2026-08-06):

1. **an arbitrary query in a module** — reached from the code editor. Not "over a selection":
   the cursor is simply somewhere inside the query, and the tool works out the rest —
   **a query in code lives in a STRING LITERAL**, usually multi-line. So the entry point has
   three jobs before any tab is drawn:
   - find the literal the cursor sits in (its start and end, across continuation lines);
   - extract the query TEXT from it — strip the quoting and the continuation markers, so the
     parser sees the language and not the C++/script spelling of it;
   - on OK, put it back the same way: re-quote, re-wrap the lines, replace exactly that
     literal and nothing around it.

   With an empty literal (or none) it opens on a blank AST — building from nothing is the
   same path as building from something, just with a shorter start.

   **How it is reached: right-click, *Query constructor*** — a context-menu item on the code
   editor, enabled only when the cursor is inside a string literal, greyed otherwise. The
   editor already does exactly this for its first item (`ibCodeEditor::OnContextMenu`):
   `GetIdentifierUnderCursor()` decides whether *Look up in Syntax Helper* is enabled. So the
   new item is a sibling with a `GetStringLiteralUnderCursor()` of its own — same shape, same
   place, and the cursor test doubles as the source of the text to parse.

   Related but separate: the query text inside such a literal deserves its own syntax
   highlighting (its keywords, not the host language's). That is B6 — the editor's dialect
   support — and it is what makes the literal readable when the constructor is closed.

2. **a report's query** — the same object, reached from the composer, and later extended
   with composition settings (L5).

**The first place to wire it, because it already exists** (Max, 2026-08-06): the dynamic
list's settings, tab *Arbitrary query* — a checkbox plus a multi-line text field
(`frontend/win/dlgs/listSettings/listSettings.cpp`, `BuildQueryPage`). The text already
travels the normal road (`composer.FromText` → parser → lowering), so the round trip is
proven there the day the button appears. Nothing to invent: take the field's text, parse,
show, render back into the field. That makes the constructor testable against a real list
rather than a mock-up, and the editor entry point above is then the SECOND host of the same
window.

One shell, two hosts. That is the whole argument for building the shell before the report
tiers rather than after.

---

## 2. The reference, tab by tab (from the incumbent, read off the screenshots)

What a person coming from that platform expects, and what each tab maps to in OUR AST.
This is the value of the reference: it names the surface, and the mapping shows how much
of it we already carry.

| Tab | What it holds there | Our AST |
|---|---|---|
| **Tables and fields** | left: the database tree (Constants / Catalogs / Documents / Enumerations / Registers); middle: chosen tables; right: chosen fields. `>` `>>` `<` `<<` move between them | `m_from`, `m_joins`, `m_projections` |
| **Grouping** | grouping fields + summed fields with a function; "use several groupings" | `m_groupBy` + aggregate `Func` in projections |
| **Conditions** | numbered rows: field, operator, value; a checkbox marks a row as *arbitrary* (free expression) | `m_where` (and `m_having` for post-aggregate rows) |
| **Advanced** | first N rows, distinct, "allowed only"; query kind (select / create temp table / drop temp table); lock rows for update | `m_top`, `m_distinct`; **temp tables and the lock: not in the AST** |
| **Unions / Aliases** | list of union branches, per-branch "no duplicates"; field-name mapping across branches | `m_unions`, `m_unionAll`; aliases per projection |
| **Order** | fields + direction; "auto-ordering" | `m_orderBy` |
| **Totals** | grouping field / totals kind / alias; grand totals; totals field + expression | `m_hasTotals`, `m_totalsBy` (with `HIERARCHY`), `m_totalsAggregates` |
| **Query batch** | several statements executed as one package | `ibQueryPackage` — the shell's top level |

**All of it is representable now.** The two that were absent when this was a plan — the batch,
and temp-table kinds + row locking — landed with the language (§0). The shell has **nine** tabs
rather than eight: *Links* is its own tab, because a join carries a kind and an ON of its own and
cramming that into the tables pane made both unreadable.

One consequence of the batch worth stating, because it shaped the window: the package is a
**strip beside the tabs**, not a tab. The tabs edit ONE statement; the strip says which. A tab
would have made the package a peer of *Order* and *Totals*, which it is not — it is the thing
they belong to.

Entry point matches too: the incumbent's editor menu offers *Query constructor*, *Query
constructor with result processing*, *Format-string constructor*, *Multilingual-string
constructor* — the same four small constructors this roadmap plans (B2, B4, C6), reached
from the same place. That is the argument for a SHARED shell rather than four windows.

---

## 3. What international practice adds

Beyond the tab-wizard, the mainstream of SQL builders (DataGrip, dbForge, Access QBE,
LibreOffice Base) converges on things worth stealing selectively:

- **A diagram of the chosen tables with join lines** — drag a field onto another to make a
  join, click the line to set its kind. Reads faster than a "joins" list and is the one
  place the incumbent's design is genuinely dated. Cheap for us: a join is
  `ibQueryAstJoin{ source, kind, on }`, and the line IS the `on`.
- **Query-by-example grid** — condition rows as a grid, one per criterion. Same shape as
  the Conditions tab; no reason to differ.
- **Live text preview beside the tabs, not behind a button.** The round trip is the
  feature; showing it continuously is what teaches people the language instead of hiding it.
- **Edit-in-text at any moment, re-parse on focus loss.** This is the property that decides
  whether a constructor stays useful: the moment hand-edited text can no longer be read
  back, the tool is in the way. We can afford it — the parser is the same one the engine
  uses.

Deliberately NOT taken: schema auto-layout, visual subquery nesting, and "explain plan"
panes. All three are large, and none is on the path to a report.

---

## 4. Where it lives

| Half | Where | Why |
|---|---|---|
| **the model** — the AST being edited, plus what may go into it (available tables, their fields, types) | **backend** | it is metadata, and metadata is the backend's. The catalogue of tables is a walk over the metaobjects that vend a queryable; the field list is the source explorer that already exists |
| **the shell** — tabs, grids, the diagram, the preview | **frontend / designer** | wx code, no metadata knowledge beyond what the model hands it |
| parse / render | **backend**, already there | `queryParser`, `queryRender` |

So the constructor is a **frontend view over a backend model**, exactly like the composition
editors built on 2026-08-06 ([designer-editors.md](designer-editors.md) § 2a) — and it
should reuse their shape: build the tree by WALKING the metadata and asking each metaobject
whether it can be a source, never by listing metatypes by hand.

The left-hand tree in the reference screenshots is precisely that list — Constants,
Catalogs, Documents, Enumerations, Information registers. Ours must not be a written-out
list, or the next metatype will be invisible in the constructor the way it was invisible in
the role editor.

---

## 5. Build order — done, in this order

Steps 1–3 landed 2026-08-06. **Step 4 (the join diagram) did not** and is the one thing from
this section still open: `m_joins` is edited as a LIST on the *Links* tab, which works and reads
plainly; the diagram is a second view over the same field, worth adding when somebody is joining
enough tables for the list to stop reading. Nothing depends on it.

| Step | What | Why here |
|---|---|---|
| **1** | **The model**: an editable `ibQuerySelect` + the source catalogue (which metaobjects can be queried, their fields and types), asked of the metadata | everything else is a view over it; and this is where the "ask, don't list" rule has to land |
| **2** | **The shell**: a modal with tabs, a live text preview, OK / Cancel, and the two entry points (code editor over a selection; the composer) | this is B2 — reused by the format-string, LINQ and translation constructors later |
| **3** | Tabs in the order that makes the thing usable earliest: **Tables and fields → Conditions → Order → Grouping → Totals → Unions → Advanced** | a constructor that can only do FROM/WHERE/ORDER is already worth opening; totals and unions are what reports need |
| **4** | The join diagram | replaces nothing — it is a second view over `m_joins`, added once the list works |

**The two items that had to be decided before step 1 — both BUILT.** What follows is the
contract they were built to; §5c records how each one is spelled and where it is executed.

**Query batch — IN SCOPE, decided 2026-08-06.** Not a UI list: a **transport decision**.
Several statements are written as one package, sent to the server in ONE trip, and the call
returns an ARRAY of result tables — indexed by position, in the order the statements were
written (Max, 2026-08-06). The alternative is issuing each query separately and paying a
round trip every time, which is what makes it worth having rather than merely tidy.

**Semantics, as stated 2026-08-06 — this is the contract, not an implementation choice:**

- statements run **in written order**, one after another;
- **each statement sees what the previous ones left** — which in practice means temp tables:
  statement 2 selects INTO a temp table, statement 3 selects FROM it by name;
- the returned array is **heterogeneous by position**: a plain select yields its table, a
  select-into-temp yields **the row count** instead (there is no table to hand back — it went
  into the temp);
- a statement may **drop a temp table** explicitly. Ours die on their own when the query
  finishes — `ibTempSourceScope` is RAII-scoped to one execution — so the explicit form is
  about releasing EARLY inside a long package, and about saying so out loud rather than
  relying on scope. ⚠ It also forces a decision the current scope does not have to make: for
  a batch, the temp scope must live for the **whole package**, not for one statement.

Three parts, and only the first is authoring:

| Part | What it means | State |
|---|---|---|
| AST | a LIST of statements, not one `ibQuerySelect` | absent — the AST holds one |
| runtime verb | `Execute` returns one result; a batch needs its sibling returning an array addressed by index | absent — nothing in `databaseLayer.h` exposes a multi-result execute |
| what statements DO for each other | statement 1 materialises a temp table, statement 2 selects from it by name | **the resolving half is built** — `ibTempSourceScope`, see below |

That last row is why the batch and temp tables are one arc rather than two: a package whose
statements cannot feed each other is only a transport saving, while one whose first statement
leaves a named table for the second is how reports are actually written.

Sequenced BEFORE the constructor's shell gains the tab — otherwise the tab would edit
something that cannot run.

**ALLOWED — "give me what I may see, do not refuse me" — IN SCOPE (2026-08-06).**

Today a query over something the user has no right to read RAISES. That is the correct
default and the comment on the door says why (`query/dataQueryBuilder.cpp`): *a refusal is
told, never mimed as an empty selection*. A report that silently shows fewer rows because of
rights is a report that lies quietly, so the platform makes the refusal loud.

But there is a case where the opposite is right: a list, a choice form, a report over a
composite type — where the user is MEANT to see the part they are allowed to and nothing
should stop the whole query because one branch is closed. That is what the flag says:
**skip what is not permitted instead of throwing.**

Mechanically it is one flag on the AST and one branch at the door: where
`CheckSelect(...)` currently returns false and the code calls
`ibBackendAccessException::Error(_("reading"))`, an allowed-query instead drops that source /
narrows the selection and carries on. Nothing new is needed underneath — the policy already
folds a restriction into the query rather than mimicking one; this only changes what happens
when it says NO outright.

⚠ It must stay explicit, never a default. The flag turns a loud "you may not read this" into
a quiet "there is nothing here", and those are different sentences — the second is only
honest when the author asked for it.

**Row locking ("lock the rows I am about to change").** What the checkbox means: the SELECT
does not merely read, it **holds** the rows it returned until the transaction ends, so nobody
else can change them in between. Without it the classic loss happens — two sessions read the
same balance, both compute from it, the second write silently overwrites the first. With it,
the second reader waits (or is refused, if the dialect can say NOWAIT).

The rendering half is already in the driver layer: `m_rowLockSuffix` — `" FOR UPDATE"` on
Postgres and MySQL, `" WITH LOCK"` on Firebird, appended after ORDER BY / LIMIT, with
`m_lockNoWait` for the non-blocking form (empty on Firebird, which expresses it through the
transaction instead). And the platform already locks records on its own path at write time
([record-locks.md](record-locks.md), lock + DataVersion check).

So the missing part is small and authorial: a flag on the AST saying "this SELECT takes the
lock", plus the tab's checkbox. Worth doing when the first configuration needs read-then-write
inside one transaction — it does not block reports, and the balance-safety it buys is
invisible until two people work at once.

**Temp tables.** Half of this already exists, and in the right place:
`ibTempSourceScope` (`query/queryable.h`) is a per-query registry of transient queryables
that `ResolveSource` consults FIRST, before the metaobject factory — which is how the
composer already feeds RAM tables and temp tables into a query by name. What is missing is
only the AUTHORING side: a statement that says "materialise this result under name X", i.e.
the query-kind radio on the Advanced tab. The plumbing under it is built.

---

## 5a. What the model can stand on (verified, not assumed)

| The constructor needs | Already in the tree |
|---|---|
| **the catalogue of sources** for the left-hand tree | `ibQueryableFactory` — sources register per kind (`HasNamespace(ns)`), and `ResolveSource` resolves `<Kind>.<Name>` through it, descending to the global factory for plugin sources. So the tree is a WALK over the factory, not a list of metatypes |
| **the fields of a chosen source** | `ibQueryableSourceDescriptor::FillSourceExplorer` — the same call the dynamic list uses to fill its columns. The constructor asks the source, exactly as a form does |
| **parameterized sources** (a register's balance / turnover / slice) | already descriptors with `CreateQueryable(params)`, registered under the composite name `<Kind>.<Object>.<Table>` — so virtual tables appear in the tree with their arguments, no special case |
| **transient sources** (RAM tables, temp tables) | `ibTempSourceScope`, consulted before the factory |
| text ⇄ AST | `queryParser` / `queryRender` |

---

## 5b. Two shapes the left-hand tree must carry (Max, 2026-08-06)

**A created temp table becomes a source in the same tree.** The moment a statement declares
"materialise this under name X", that name appears **at the bottom of the left-hand list**
alongside the metaobject sources, and later statements pick it exactly like a catalog. This
is not a UI convenience — it is what makes a package readable: step 2 leaves a table, step 3
sees it in the list and selects from it.

Nothing new underneath: `ibTempSourceScope` already resolves such a name BEFORE the
metaobject factory, so the tree just has to SHOW what the package has declared so far. Which
means the tree is fed from two places — the factory (permanent sources) and the package
being edited (temp ones) — and the second is per-package state, not metadata.

**Nested tables (a subquery as a source).** Instead of picking a table, the author inserts a
NESTED table and edits it separately: it is a query of its own, sitting where a source would
be, and the outer query selects from its result. Our AST already carries this —
`ibQuerySource::m_subquery` (`std::shared_ptr<ibQuerySelect>`) — and `ibRenderQuery` already
renders it, so the machinery is there; what is missing is the authoring gesture:

- in the sources list, "add nested table" creates an empty inner `ibQuerySelect`;
- opening it re-enters the SAME constructor one level down (the shell must therefore be
  re-entrant over a sub-AST, not tied to one top-level statement);
- its result appears as a source with its own alias and its own field list, taken from the
  inner query's projections rather than from a descriptor.

That last point is the one design consequence worth stating: for a nested table the field
list comes from the **inner query**, so the tree's "fields of a source" step has two
sources of truth — `FillSourceExplorer` for a real source, the projections for a nested one.
Both are answers to the same question, and the shell must ask the SOURCE which it is rather
than branch on a type.

**Both landed, and the re-entrancy is what a union branch reuses.** `ibShowQueryConstructorFor`
opens the same window over ONE select — a nested table and a union branch are both that, so
neither needed an editor of its own. `ibQueryConstructorModel::GetFields` is the single door:
a subquery answers from its projections, a temp table from the projections of the statement
that made it, a real source through its descriptor, and the caller does not branch. Two rules
the model enforces that the tree alone would not:

- **only what came BEFORE this statement is offered.** A table statement 5 makes does not exist
  for statement 2, and offering it would produce a query that reads a name nothing has filled —
  one that PARSES, which is what makes it worth guarding (`GetTempSources(package, before)`).
- **a DROP takes the name back out.** "Release early" means gone; still offering it would offer
  something the engine will refuse.

## 5c. How each of them is spelled, and where it runs

The grammar the tabs write and the parser reads (`queryParser.h` carries the same lines):

```
package  := statement { ';' statement }            (a trailing ';' is allowed)
statement:= DROP name
          | selectCore { UNION [ALL] selectCore } [ORDER BY …] [TOTALS …] [FOR UPDATE]
selectCore := SELECT [ALLOWED] [TOP n] [DISTINCT] selList [INTO name] FROM source { join } …
```

| Clause | Where it is EXECUTED | What it does there |
|---|---|---|
| `ALLOWED` | `ibDataQueryBuilder::Execute` | the access policy's refusal returns `ibMakeEmptyQueryResult()` instead of raising. The flag sits on the door beside `Distinct()`; L3 vends the empty table and nothing more |
| `FOR UPDATE` | `ibQueryLowering::ExecuteImpl` | sets `ibReadPageRequest::m_lockForUpdate`; the driver dialect appends its own clause (`FOR UPDATE` / `WITH LOCK`) — the half that already existed |
| `INTO <name>` | `ibQueryLowering::ExecutePackage` | drains the result into a snapshot, registers it under that name, and yields the ROW COUNT |
| a bare name as a source | `ResolveSource` | a temp table has no `<Kind>.` prefix — the auxiliary registry is asked first, and only then the factory |
| `DROP <name>` | `ExecutePackage` | removes the name from the registry. Dropping one the package never made RAISES: that is a typo or a statement that did not run, not a shrug |

**Two properties of the package worth keeping in view.** The temp registry lives for the WHOLE
package — a single query's `ibTempSourceScope` is RAII-bound to one execution, so statement 3
would never see what statement 2 left. And the scope holds a POINTER to the map, so entries
added mid-package are visible to every statement after them, which IS the batch contract.

### `TempTablesManager` — who keeps a temp table alive

`ibQueryTempTableStore` (`query/queryTempStore.{h,cpp}`) is that registry as an owning type, and
the reason it is a type is that "how long does this table live" is a question about **ownership**,
not about queries:

- **no manager** — the package owns a store for its own run, and the tables die with the call.
  That is the right default: a temp table nobody is keeping is one nobody can accidentally read.
- **a manager** — a script value the author makes and holds:

```
manager = New TempTablesManager();

first = New Query("SELECT Ref INTO Sales FROM Document.Orders");
first.TempTablesManager = manager;
first.Execute();                      // the ROW COUNT; the table stays in `manager`

second = New Query("SELECT Ref FROM Sales");
second.TempTablesManager = manager;   // the SAME tables, a different query
second.Execute();

manager.Close();                      // and they are gone
```

One verb, because there is one decision to make about a set of temp tables. Setting the property
to anything that is not a manager RAISES — a query told to put its tables somewhere that cannot
hold them would otherwise fail much later, at a statement reading a name nothing filled.

### The statement's KIND

Select / *create a temporary table* / *drop a temporary table* is a **kind**, not a checkbox, and
the name is asked because the kind asks for it. It lives on the **package strip**, beside the
statement list, because it says what the statement IS — while the tabs say what its query selects.
Changing the kind keeps the query behind it, so a kind chosen by mistake costs nothing to undo.

**The script surface is one mechanism with two verbs** (Max, 2026-08-06): every query runs as a
package — an ordinary one is a package of one — so `Query.ExecuteBatch()` returns the array and
`Query.Execute()` is simply *the first result*. No branch about which kind of text was handed in,
and no verb that sometimes returns a table and sometimes a number.

---

## 5d. Read-only, and where the answer comes from

The constructor opens **for reading** when the configuration cannot be changed: every verb is
off, the buttons grey together, the text is not editable, OK becomes Close, and a line at the top
says so. The point is that the query stays VISIBLE — refusing to open it would hide the only view
of it there is.

Two rules, both deliberate:

- **The metadata is asked, not told.** `IsMetaDataReadOnly` reads `ibBackendMetadataTree::IsEditable()`
  — the same question the designer already answers before it lets anything be edited. No tree at
  all means a runtime host with no designer surface (a dynamic list's own query), which is not
  read-only. The `readOnly` parameter can only ADD the restriction, never lift it.
- **Greying is the sign; `CanEdit()` is the guarantee.** Every mutating handler stops at its first
  line, because a double-click reaches one with no button in the way.

**Metadata is handed in, never looked up.** `ibShowQueryConstructor` takes the config as a
required argument and the dialog passes it down to a nested constructor. A default that quietly
fell back to the active configuration would resolve a copied query's tables against somebody
else's config — the same class of defect the source catalogue avoids by walking rather than
listing.

---

## 5e. How the window is arranged, and why

Read off the incumbent's own screens (Max, 2026-08-06) and then argued from what each shape buys:

- **Move buttons BETWEEN the panes, not under them.** A `>` under the left pane says "do something
  to this list"; the same `>` between two panes says "send it there", which is what it does.
- **Splitters between every section.** How much room the table list needs against the field list is
  a judgement about the query being written, not about the window.
- **Tool bands (16×16) above each pane**, the shape `listSettings` already uses — a row of text
  buttons under a list grows with every verb until it wraps.
- **Every tab shows what there is to CHOOSE from, on its left.** A tab listing only what has been
  chosen sends the author to another tab to remember a field's name.
- **The package is a TAB, last.** As a side strip it cost every other tab a fifth of the window for
  a list that is usually one line. Statements are named plainly — `Query 1`, `Query 2`: a summary
  of what a statement does is a second description that goes stale.
- **The tab set follows the statement's KIND.** A drop has no query, so the query tabs are removed
  rather than greyed; *Links* appears only from the second table, because a join is a relation
  BETWEEN two sources.
- **One refill, one frame.** Every edit refills every tab — that is what keeps the views from
  drifting — so the refill runs inside `Freeze()` + `SetEvtHandlerEnabled(false)`, and the source
  tree under its own `wxWindowUpdateLocker`. Without it the window looks like it is thinking
  rather than answering.

⚠ **Arrows are built from code points** (`wxUniChar(0x2191)`), never written as literals: this
tree is UTF-8 without a BOM, MSVC then reads it in the system codepage, and a literal `↑` reached
the button as mojibake (seen live).

---

## 7. The second pass (2026-08-06) — what running it changed

Everything below came from Max using the window and is now IN, unless marked open. The list is
kept because the reasoning is the durable part.

**Collapsed rather than added.** Three dialogs — condition, join, aggregate — became ONE
`ibDialogQueryExpression`. All three were writing an expression in the query language, so all three
were the same window with different labels. The condition editor's *simple mode* went with them:
it was a second, weaker way to write the same text, and what it was actually FOR — "I do not
remember what the field is called" — is the field tree on the left of every tab. Adding a condition
from a selected field opens the editor with `Field = ` already written.

The editor carries the fields, the **language** (functions and operators built from
`ibQueryKeywordText` — a palette with its own spelling of `LIKE` is a second dictionary and is
wrong the day a localized table is installed), and a **nested query** button that opens the whole
constructor one level down.

**Grids on `ibDataViewCtrl` models**, because the cells are edited IN PLACE:

| Grid | Model | What is edited in place |
|---|---|---|
| Links | `queryLinkModel` | *table · all · table · all · condition* — the two boxes ARE the join kind (neither = inner, left, right, both = full). The kind stays an enum in the AST; the boxes are how it is read |
| Conditions | `queryConditionModel` | the AND chain, one row each. "Arbitrary" is INERT — an observation ("this cannot be read as field · comparison · value"), not a switch |
| Unions / Aliases | `queryUnionModel` + `queryUnionFieldModel` | the branches and their "no duplicates"; beside them the FIELD MAP, one column per branch. The branch columns show what already lines up (our lowering matches BY NAME); the FIRST column is the output field's **alias** and is typed into — see §7a |
| everything else | `queryGridModel` | one generic model: fields, grouping keys, aggregates, order, index, totals ×2, the package. Told how to READ a cell and how to WRITE one; the AST stays the single copy |

**Rules the window now holds to, each from a defect:**

- **What you act on decides the scope.** Standing on a kind moves every table under it; standing
  on a table moves every field of it. There is no "move all" button because "all of these" is
  already a thing you can point at. Drag does the same — same verb, two ways to reach it.
- **A tree, not a list, on every left pane.** A flat list of qualified names hid the thing a person
  is looking for: which table a field belongs to.
- **Two output columns cannot share a name.** A duplicate is NUMBERED on its alias, the way a DBMS
  numbers one — otherwise the clash surfaces at run time, and a UNION's branch map (which lines up
  by name) breaks quietly.
- **Rebuild only what changed.** `SyncNotebookPages` compares the wanted tab set with the current
  one and returns early when they match. Tearing the notebook down on every edit cost two visible
  bugs: the selected tab jumped, and the tab strip repainted over itself into `Fielbles and fields`.
- **A branch is a query**, so the union branches are a strip down the right edge and the ordinary
  tabs edit whichever is picked — not a second window.
- ⚠⚠ **A tree drag is EITHER the tree's or yours — never both.** `wxEVT_TREE_BEGIN_DRAG` arrives
  vetoed, and there are two correct answers. `Allow()` it and let wxTreeCtrl run the native drag,
  finishing in `EVT_TREE_END_DRAG` (what `formEditor` does); or leave it vetoed and run
  `wxDropSource::DoDragDrop` yourself (what `listSettings` and `helpTreeView` do). Doing both makes
  MSW refuse the second `ImageList_BeginDrag`, which surfaces as
  `dragimag.cpp(282): assert … BeginDrag failed` out of `wxTreeCtrl::MSWOnNotify`. An earlier note
  here said the opposite; it was wrong, and the three handlers that followed it are fixed.

**`INDEX BY` — and it is real.** `SELECT … INTO Sales … INDEX BY Ref` parses, renders and
round-trips; the tab appears only for a create-temp-table statement; and `ibQueryTempTableStore`
BUILDS the lookup (value → rows) so a later read filtering an indexed column is answered from the
map instead of by walking the table. A clause that only wrote itself into the text would be a
control over nothing. `INDEX BY` without `INTO` is refused: there would be no table to index.

**Open as of §7 — one settled since:** a temp table as a query PARAMETER landed in §7d as
`FROM &Name`, readable only INTO a temporary table. The `Query builder` tab is still not built.

---

## 7a. The third pass (2026-08-06) — one grid, one place for a name

**Every list in the window is now the same control.** Six `wxListBox` and two `wxListCtrl` stood
beside three dataview grids, and the mixture is what made the window read as several dialogs
stitched together. The look was the smaller half: a listbox cannot be edited in place, so each of
those panes needed a dialog to change one word. `queryGridModel` is one model told how to read a
cell and how to write one; the per-pane knowledge stays in the dialog, where it belongs.

⚠ **The fork's text renderer is `wxDATAVIEW_CELL_INERT` by default.** Every grid in the window
opened read-only for that reason alone — "I cannot get into the field" was the exact symptom.
`TextColumn(title, col, width, editable)` is the one place that asks, so a new column cannot
forget to.

**A cell over a closed set is a CHOICE, not a typed word.** The unfold, the sort direction and the
aggregate function are each a fixed set the language already names, so their cells offer exactly
those words (`ChoiceColumn`, built from `ibQueryKeywordText` — the keyword table's spelling, not a
copy of it). Three handlers went with this: `OnCycleUnfold` (press it three times to get back where
you started), `OnEditFieldAlias` and `OnAddLink` — all three unreachable once the cell or the tab
took the job over, and removed rather than left as dead doors.

⚠ **Open, and the better answer:** `listSettings` already solves this with `ibRowValueCellRenderer`
— a cell holding an **ibValue**, where the VALUE's registered type decides how it is chosen (the
grouping kind there is `ibValueEnumGroupKind`, which is the very enumeration `ibQueryDimUnfold`
is registered as). That is the mechanism; `ChoiceColumn` is a weaker spelling of it. The renderer is
coupled to its dialog only in the field-picker path — the enumeration path needs nothing from it —
so lifting it into a shared header and using it here is cheap, and it makes the two dialogs
identical to look at as well as to reason about.

**An output field has ONE place its name is typed: the Unions / Aliases tab.** Adding a field
generates the name (the projection's own leaf, numbered on collision — `ibQueryEnsureUniqueName`,
which moved into the engine in §7b), and
the field map is where it is changed. The Fields tab has no alias column any more: two controls
over one word is how they drift apart, and the map is where the name MATTERS, because that is what
the branches are lined up by.

**Totals, in the shape the clause has.** Fields on the left; the dimension grid above
(*grouping field · totals kind · alias*, all typed into, moved up and down because levels apply IN
ORDER); the aggregate grid below (*totals field · expression*). The **grand-totals** box sits
between them, because that is where the level sits in the query.

**`BY OVERALL` — the level above every dimension (2026-08-07).** The box used to be shown *checked
and disabled*, on the reasoning that "our totals tree has a root and the root IS the grand total".
That was true of the FOLD and false of the RESULT: the root always carries the whole-result
aggregates (`ibQueryComposer::BuildDimensionTree` — *grand total in-place*), but whether it is
WALKED as a row is a choice, and the box was refusing to make it while looking like it already had.
The mechanism was finished; only the box was not connected.

It is a **flag on the select** (`ibQuerySelect::m_totalsOverall`), not an entry in the dimension
list: a dimension is a column and a place in an order, and the overall is neither — it has no
column, and there is nothing above "everything". Written first in the `BY` list, which is where it
is read from:

```
TOTALS
    COUNT(Actions.Board)
BY
    OVERALL,
    Actions.Author
```

`BY OVERALL` **on its own is a whole totals query** — one row over everything, no dimensions. Two
places assumed otherwise and were relaxed: the terminal's "TOTALS needs at least one BY dimension"
(now *…or `BY OVERALL`*), and the selector's `Build()`, which fell through to the manual fallback
when the level list was empty and folded by a null column. The dimension tree handles zero levels
exactly — `FoldDimLevel` returns at once, and the root still gets the aggregates, which IS the row
that was asked for.

**`TOTALS … BY <dim> [HIERARCHY] [AS <name>]` — new in the language, not in the window.** A level
needed a name of its own: two levels over the same column (Date by month, Date by day) are two
output columns, and without it the second answers to the first's name. Parser, renderer and the
lowering (`OutputColumn::m_name`) all carry it; round-trip tested both ways, with and without `AS`.

**The union branches are TABS down the right edge** (`wxNB_RIGHT`, empty pages — the control is
there for its strip). A branch is not something picked out of a list, it is the query the tabs are
currently showing, and that is what a tab means.

⚠⚠ **`Fielbles and fields` — the actual cause, after two wrong ones.** A page window is built as a
CHILD of the notebook. A child that was never added — or that `RemovePage` detached, because
**RemovePage detaches without hiding** — stays a plain visible child at its default position,
`(0, 0)`. That is the top-left of the notebook, which is where the TAB STRIP is. The **Index** page
is normally absent (only a statement that makes a temp table can be indexed), so it sat there
painting its own left label, `Fields`, over the first tab's caption, `Tables and fields`. Overlay
the two and you get `Fielbles and fields`, letter for letter.

`SyncNotebookPages` now hides every page not in the wanted set, and does it BEFORE the
set-unchanged early-out, so a stray page is hidden on the very first pass.

The two earlier explanations were wrong and are recorded because the lesson is: **a repaint
artefact that survives a `Refresh()` is not a repaint artefact.** Setting the font first and
dropping the update locker were both reasonable and both changed nothing.

**Same defect, same class, in the form engine** (`visualView/ctrl/notebookPage.cpp`): a page with
`Visible = false` was never added in `OnCreated` and detached in `OnUpdated`, and in neither path
was it hidden — so an invisible page painted over the tab strip of any user form that had one.
Fixed in both paths.

---

## 7b. The fourth pass (2026-08-06/07) — the engine judges, and it says so out loud

Everything here came from Max running the window. The pass has one theme: **anything the window
was deciding for itself moved to the engine**, because a second opinion about what a legal query is
is the failure mode this whole arc is built to avoid.

### The engine answers, the window shows the answer

| Question | Who answers it now | Where |
|---|---|---|
| is this a legal NAME? (no spaces, no punctuation, one identifier) | the LEXER — tokenize it and demand exactly one `Ident` spanning the whole text | `ibQueryLexer::IsIdentifier` |
| do these names RESOLVE — every field, every table, every order key? | the lowering, resolving with no execution | `ibQueryLowering::CheckNames` |
| do two output columns share a name? | the same | `CheckNames` |
| what is this column CALLED? | one answer, everywhere | `ibQueryProjectionName` |
| what name should a new column get? | the engine, at the moment of adding | `ibQueryEnsureUniqueName` |
| what name should a new table alias get? | the same | `ibQueryUniqueSourceAlias` |

The last two moved OUT of the dialog in this pass, and the argument is not tidiness. `CheckNames`
**refuses** a query whose output names collide; the generators are what make sure it never has to.
Two halves of one rule — and while one half lived in a dialog and the other in the engine, they
could only be kept in step by copying, which is how they drift. They are also not a desktop
feature: the web front assembles queries, a script does, a report's composer does, and if each
numbered a duplicate its own way, the same click would produce different text in different hosts.

**A bad name shows a WARNING and reverts.** Not a line at the bottom of the window — a message box
naming what was wrong, and the cell goes back to what it held. The check runs on the way IN (as the
cell commits), so a name the engine would refuse never reaches the AST.

### Names are checked by resolution — and so are paths

**`PruneUnresolved`** is the same question asked destructively: walk the package, drop everything
that no longer resolves — projections, group keys, order, index fields, totals levels, join
conditions, WHERE links. It runs after a table is removed.

⚠ The point is that **nothing chases the deletion**. Removing a table does not hunt down what
referred to it; the window re-asks "what still resolves?" and whatever does not, goes. A cascade
written by hand is a list of cases, and the case nobody thought of is the bug. `PruneUnresolved`
never touches a select it *cannot verify* — an unresolvable source means "we do not know", and
"we do not know" must never delete a person's work.

### The language grew where the window needed words

- **`FROM` is optional.** `SELECT 1` is a legitimate query — it returns one row, one column — and
  writing `FROM` with nothing after it was the window inventing syntax. The parser accepts a select
  with no source; the renderer omits the keyword; `ExecuteSourceless` builds the one-row RAM table.
  A COLUMN in such a query is refused with *"this query reads no table"*, which is the truth.
- **Query text is written one item per line**, keyword alone on its line, items indented. The
  package separator is `;` **on a line of its own**. A query is read far more often than it is
  written, and a diff of one is unreadable when a clause is one long line.
- **A table has an alias, and it is generated**: `Catalog.Nomenclature AS Nomenclature`, a nested
  query as `NestedQuery1`. Editable in the tree's own label editor — one renaming mechanism, not a
  dialog beside it.
- **`ExecuteBatch`** (renamed from `ExecutePackage`) returns an array by position: a create-temp
  statement yields a result of **one column, one row** holding the row count; a drop yields
  `Undefined`. A statement that produced nothing readable used to produce nothing at all, which
  made the array's positions meaningless.
- **Batch statements are TABS down the side**, the same shape union branches use — a statement is
  the query the tabs are showing. A drop statement leaves only *Advanced*; a create-temp statement
  gains *Index* and **loses Totals** (a temp table is flat; `TOTALS` yields a tree, and the parser
  refuses the combination outright).

### The query text is a code editor now

`wxStyledTextCtrl` with `wxSTC_LEX_SQL`, in all three places query text appears: the constructor's
preview, the expression editor, and the dynamic list's arbitrary query. `ibStyleQueryText` is the
one styling function — line numbers on, and the font and colours **inherited from the engine's own
settings** (`ibFontColorSettings`), so the query pane looks like the code editor because it *is*
styled from the same place. The keyword list is `ibAllQueryKeywords()` — the whole active table,
never a literal, so a localized keyword table highlights in the language it parses.

### What the audit collapsed (2026-08-07)

Duplicates, in the order they were found. Each was one rule with several copies:

| Was | Is | Copies |
|---|---|---|
| "what is this projection called" | `ibQueryProjectionName` — **backend** (`queryRender.h`) | 3 (two byte-identical) |
| the WHERE `AND` chain, flattened and folded | `ibQueryFlattenAnd` / `ibQueryFoldAnd` — **backend** (`queryRewrite.h`) | 3 |
| a dotted path → a `Column` node | `ibQueryColumnFromPath` — **backend** (`queryRender.h`) | 6 |
| name generation on add | `ibQueryEnsureUniqueName` / `ibQueryUniqueSourceAlias` — **backend** (`queryRewrite.h`) | 1 each, in the wrong layer |
| a field tree's row: its node, its lazy reference walk, its drag | `queryFieldTree.h` — frontend, shared by the constructor and the expression editor | 2 node classes that had already begun to drift (one carried the table path, the other did not) |
| a drop target that is a callback | `callbackDropTarget.h` — one class, two constructors (the payload-free selection drag; the position-aware text drop) | 3 |

Dead code removed in the same pass: `OnToggleUnionAll` (bound to nothing), `IsSimpleCondition`
(declared, never defined), `queryCellRenderer.h` (never referenced, never in the project file),
`OnCycleUnfold` / `OnEditFieldAlias` / `OnAddLink` (superseded by the cells and tabs that took
their jobs).

### And the window was cut in four

`queryConstructor.cpp` had passed 4.2K lines, which is a READING problem before it is a size one:
building a page, filling it from the AST, and acting on what the user does to it are three
different jobs, and reading any one meant scrolling past the other two. Four files now, by the
question each answers:

| File | Question | Lines |
|---|---|---|
| `queryConstructor.cpp` | **what is there** — the window, its tabs, its grids and trees | ~1.4K |
| `queryConstructorFill.cpp` | **what it shows** — AST → tabs, one `Fill` per pane, and which statement / branch is currently being shown | ~0.8K |
| `queryConstructorEdit.cpp` | **what it does** — every verb: add, remove, move, rename, edit | ~1.5K |
| `queryConstructorText.cpp` | **what it is** — the text pane, the parse gate, OK, and the two entry points the product opens it by | ~0.2K |

`queryConstructorInternal.h` holds what was the file-local part: the include list and the window's
own small vocabulary (`TableLabel`, `TextColumn` / `IconColumn` / `ChoiceColumn`, `MakeGrid`,
`EditOnActivate`, the glyphs). The include list is deliberately the WHOLE one rather than a
per-file minimum — these four files are one window cut in four, and four lists would be four
things to keep in step.

The cut also made four dead helpers visible that a 4K-line file had hidden: `JoinKindText` and
`SourceText` (both superseded by `TableLabel` and the tree's own labels) and `ArrowUp` / `ArrowDown`
(the move buttons became toolbar tools). Removed.

---

## 7c. The dynamic list, and the naming audit (2026-08-07)

### The query lives ON the main table, and the main table never goes away

Max's model, and it settles what the arbitrary query IS: **the main table is what the list IS** — the
source of the commands (open a row, create, mark for deletion), of the icon and caption, of the
value a choice hands back. The arbitrary query lives OVER it, an implicit join. It changes what is
READ; it cannot change whose rows they are.

So the source is serialised **always**, next to the query, not instead of it. It used to be an
either/or, and that was the mistake: a query allowed to stand in place of the source took the list's
identity away along with its data.

| | |
|---|---|
| ticking the box | **generates** a starting query over the main table (`SeedArbitraryQuery`) — a real query, rendered by the same renderer, openable in the constructor on the spot |
| unticking it | clears the text — the main table alone is the read |
| editing the text | applied AT ONCE: columns, the three field pickers and the error line all follow, without closing the dialog |

⚠ **The seed qualifies every field** (`Catalog.Products AS Products`, then `Products.Code`). A bare
`Code` is unambiguous only until a SECOND table joins, and at that moment every field written before
it becomes ambiguous at once — the engine refuses them, `PruneUnresolved` drops what no longer
resolves, and the whole field list disappears from a gesture that had nothing to do with those
fields. Seen live. The constructor already held to this rule when IT added a table; the seed did not.

### The composer stopped refusing settings over an author's query

`ibDataDBComposer::RenderText` raised *"settings over an author's query text are not supported yet"* —
so a filter, a sort or a grouping over an arbitrary query failed outright. The seam its own comment
predicted is now wired:

```
SELECT * FROM (<the author's query>) AS AuthorQuery WHERE … ORDER BY … TOTALS …
```

The author's text is never edited. A WHERE injected INTO it would run before their aggregates, their
DISTINCT and their TOP, and would quietly answer a different question than the one typed into the
filter. Nothing new was needed: a subquery source round-trips, the lowering realises it, and the
optimizer's FROM-subquery flattening folds the plain case back into ONE server-side SELECT.

### `ibQueryLowering::DescribeOutput` — the schema without the read

The third question in the `CheckNames` family: *do the names resolve* / *drop what stopped* / **what
does this query produce**. The door is populated (which is where names become columns) and simply
not asked for rows. A UNION is described by its first branch, a TOTALS query by its detail.

That is what lets the list offer the query's columns in its filters, sorts, grouping and column
chooser — offering them by RUNNING the query would mean reading the table to find out what the table
is called.

### ⚠ The output schema said HOW to read, never WHAT it is

`OutputColumn` carried `m_col` / `m_alias` / `m_byAlias` — three fields about the READ. Whenever a
value is read by alias (which is EVERY column of a `SELECT … INTO Tmp` over a JOIN) `m_col` was null,
so the temp table's columns were built with **no type at all** — and a reference with no type is not
a reference. `Tmp.Supplier.Region` then had nothing to walk into: *"the temp table does not inherit
the type"*, exactly as reported.

`OutputColumn::m_type` splits the two questions. Empty means "unknown" (a computed column, a CASE, a
literal), never "no type".

### Naming — three concepts had five near-synonyms

| answers | entry |
|---|---|
| what is this column CALLED (the language reads it back by this) | `ibQueryOutputName` |
| what is this SOURCE called | `ibQuerySourceName` |
| what is this TOTALS LEVEL called | `ibQueryDimensionName` |
| what SHOULD a new column be called | `ibQueryProposedName` |
| assign it, and number a duplicate | `ibQueryEnsureUniqueName` |
| what should a new TABLE alias be | `ibQueryUniqueSourceAlias` |
| what to call it at execution when there is no proposal | `OutputNameFor` (file-local) |

The first three are READS and live in `queryRender.h` (the names things HAVE); the rest are EDITS and
live in `queryRewrite.h` (the names we GIVE). `ibQueryProjectionName` / `ibQueryProposeName` used to
sit side by side with nothing in the names saying which was which.

**`ibQuerySourceName` was written out eight times** — the model, the render, the alias generator and
four places in the constructor. It is load-bearing: every qualified field is written against it, the
alias numbering compares against it, and the table rows are labelled with it.

**A walk is named by its whole path, glued:** `Reference.PredefinedName` → `ReferencePredefinedName`.
The leaf alone is wrong for the case that matters — every step of a walk ends in the same word, so
`PredefinedName` and `Reference.PredefinedName` both asked to be called `PredefinedName`. A leading
segment that names a SOURCE is dropped: the qualifier says which table, which is where the column is
read from, not what it is.

⚠ **And the name is WRITTEN DOWN.** `ibQueryEnsureUniqueName` used to set an alias only on a
collision, so a walk kept its leaf name in the text and everything downstream went on using it. A
proposed name that differs from the natural one is stored as an alias immediately.

### ⚠⚠ A verb must not call another verb's handler

`AddCatalogueFieldToSelect` added a missing table by synthesising a `wxCommandEvent` and calling
`OnAddTable`. The day `OnAddTable` grew a case that called back into the verb ("standing on a field
means the field"), the pair recursed with no bottom — which arrives as a **stack overflow**, not as
anything readable.

A handler reads the window and decides WHAT; `AddTableSources(paths)` does it. Two verbs share the
second half; neither calls the other's first half. Swept: every other `wxCommandEvent e; OnX(e)` in
the product is *gesture → verb* (an activation, a drop, a menu item), which cannot cycle.

**Open as of §7b — both settled since:** aggregate TYPE checking landed in §7d as
`ibQueryLowering::AggregatesFor`, read here as a refusal and by the constructor as what to offer.
`CAST` landed in §7d as a NARROWING (`CAST(Recorder AS Document.Order).Number`); conversion to a
primitive is refused, and that section says why.

---

## 7d. The fifth pass (2026-08-07) — the type decides what is offered

### The engine's list, read twice

`ibQueryLowering::AggregatesFor(type)` answers *which folds a value of this type admits*:

| | |
|---|---|
| SUM / AVG | a NUMBER. There is no sum of dates and no average of references |
| MIN / MAX | anything ORDERED: number, date, string. A reference keys by guid, so folding one would rank rows by an internal identity |
| COUNT | anything at all — counting asks nothing of the type |

⚠ **`CheckNames` reads it as a refusal; the constructor reads it as what to OFFER.** The Grouping
tab's *Function* cell lists exactly these for the field on that row — a string field shows no `SUM` —
so a person is never shown a choice their own engine then rejects. Written twice, the two would
drift, and the drift is a dropdown that offers something the query cannot do.

An UNKNOWN or COMPOSITE type yields the full set. Which type a row holds is a fact about the ROW,
and narrowing on "it might be a string" would be the answer inventing one.

### A cell over a closed set is a list; a cell over an expression is not

Two different cells, and the difference is whether the set is closed:

- **Grouping / Function** — a plain dropdown. The five aggregates are all there is.
- **Totals / Expression** — a **combo**: the ready calls over this row's field (filtered as above),
  free typing, and a **`...`** into the arbitrary-expression editor. `SUM(Qty)` belongs in a list;
  `SUM(CASE WHEN … END)` fits in no list, and offering only the list took the language away.

⚠ **A per-row choice renderer must measure itself by its VALUE.** The fork's choice renderer sizes a
cell by the widest of its FIXED choices — and a per-row list has none at construction, so the cell
reported a near-zero width and drew **nothing at all** over perfectly good aggregates.

### ⚠⚠ A cell must not rebuild the expression as text

The function cell used to render the argument back to text, glue the chosen word around it, and
re-parse the result. Two things were wrong and both bit: a round trip through text can only ever
LOSE, and a word that is not an aggregate produced a nonsense expression the parser blamed the whole
query for — seen live as `SUM(Products.Name)  + 1 AS Field1` with *"unexpected text after the
query"*. The function IS a field of the node; setting it is one assignment that cannot fail and
cannot touch the argument.

### TOTALS is per LEVEL, so there has to be a level

`TOTALS SUM(x)` with no `BY` is a clause the language does not have. The window now refuses to
*build* it: a measure cannot be added before a level exists, and the last level leaving takes the
whole clause with it — said out loud, because losing the measures is a real consequence and the
author is the one who decides whether that was meant.

The old guard fired only when BOTH lists were empty, which left exactly the broken state: the last
level removed, the measures still there, and a query that would not parse from that moment on,
complaining about a line nobody had touched.

### A condition over a folded value is a HAVING, wherever it was written

The Conditions tab offers the aggregate fields beside the plain ones — they are all fields of the
result — so a condition over `SUM(Qty)` gets written where every other condition is written. Left
in `WHERE` it reached the ROW filter, which has no aggregates to filter by, and came back as an
error about a query the window itself composed.

It is a **rewrite rule** (`queryRewrite`), not a window habit: the move is per AND-term, because
that is the granularity at which the two filters compose. `WHERE Warehouse = &W AND SUM(Qty) > 100`
is a row filter and a group filter written together, and splitting them changes nothing about what
the query says. An `OR` **across** the two is left whole — it cannot be split without changing
which rows survive, so the engine's own check speaks instead.

The lowering was reading only the whole `HAVING` expression, so two group filters side by side were
refused as "not a comparison", which they each plainly were. It now lowers one `Having()` per
AND-term — the builder AND-folds them, which it always could.

**It moves WHEN IT IS WRITTEN, not when the query runs.** `ibQueryRewrite::Rewrite` applies the rule
on a CLONE at execution time — right for a query typed by hand, wrong for one being built: the
constructor shows the author's own AST, so the condition just written stayed visibly in `WHERE`
while the engine quietly ran it as `HAVING`. A window that shows a different query from the one that
runs teaches people to distrust it. The rule is public
(`ibQueryMoveAggregateConditionsToHaving`) and the Conditions model applies it in its one write
door; the rewrite stays as the safety net for text that arrives some other way.

The tab therefore reads **both clauses**. It is "the conditions of this query", not "the contents of
`WHERE`" — which clause each lands in is the engine's call, answered in the query text. Reading
`WHERE` alone made the row vanish from the grid the moment the rule moved it, leaving a condition
plainly in the query and nowhere to edit or remove. Writing back sends every row through `WHERE`
again and lets the rule re-split, so a condition edited from `SUM(Qty) > 1` to `Qty > 1` comes home
by the same door it left by.

Two limits of the old shape went with it:

* **`HAVING` is its own clause.** The parser read it only as a tail of `GROUP BY`, so
  `SELECT … FROM … HAVING SUM(x) = &v` died as *"unexpected text after the query"* — pointing at a
  keyword plainly visible in the text the window had just produced. With no `GROUP BY` the whole
  result is ONE group, which is exactly what `TOTALS … BY OVERALL` says in the other clause. The
  renderer had always written it independently; only the reader was asymmetric.
* **`=` and `<>` work.** `HavingItem` carries a full `ibQueryFilterOp` and always did — the four
  ordered operators were the lowering's switch, not the mechanism's, and `HAVING COUNT(x) = 1`
  ("exactly one") is as ordinary as "more than one".

### `COUNT(DISTINCT x)` — a property of the CALL

`COUNT(DISTINCT Board)` asks how many DIFFERENT boards, not how many rows have one. It is **not**
the statement's own `DISTINCT`, which asks whether two whole ROWS are the same — different
questions, and a query may want either or both, so it is a flag on the aggregate node
(`ibQueryAstExpr::m_distinctArg`) rather than anything to do with `ibQuerySelect::m_distinct`.

Read where SQL puts it (before the argument), rendered the same way, and carried down to both
engines: the DB path emits `COUNT(DISTINCT col)` through one line in the L2 renderer, and the RAM
fold counts each different value once, **keyed by `ibValue::GetHashKey()`** — the identity, never the
display string, because two references print alike far more often than they are alike.

`COUNT(DISTINCT *)` is refused with a sentence — a star names nothing to be distinct about — rather
than as a syntax error about a star the parser would otherwise have taken.

### Grouping completeness reads the whole expression, not its top

Both halves of the grouping rule used to look at what a projection **is** — a bare column, or an
aggregate. Right for `Qty` and for `SUM(Qty)`, and wrong for everything in between:
`SUM(Price * Qty) / COUNT(*) * 1.2` is neither, belongs in no list the constructor shows, and was
skipped whole — so the free `Price` in `SUM(Qty) / Price` was held to no rule at all.

One walk (`CollectFoldedAndFree`) now answers both: a column under an aggregate is folded, a column
outside every aggregate has to be a group key. That is the whole rule, and it is one question about
one tree. The mirror is `ibQueryLowering::AggregatedColumns` — the same one-door-two-readings shape
as `UngroupedProjections`: the check reads it as a refusal (a folded column standing in `GROUP BY`
for no reason but a window that put it there), the constructor reads it as "do not offer this one".
The refusal is **narrow**: a column the query also selects plainly is exempt, because the
completeness rule demands that very key and the two must not disagree one paragraph apart.

### CASE WHEN — the one construction that is a LIST

`queryCaseDialog.{h,cpp}`. The language has always read, written and run `CASE`; what was missing
was a way to WRITE one without typing it, and the palette's skeleton is not that — it drops five
keywords into a text box and leaves the author to keep the WHENs and THENs paired by hand.

It earns its own window for a reason no other expression has: **it is ordered**. Swap two WHENs and
the answer changes, so it is edited as rows with move buttons. Each cell still opens the ONE
expression editor; this window owns the shape, never any syntax of its own. Reached from the
expression editor's `CASE` button, over the selection, and written back in its place.

### A table handed in as a parameter

```
SELECT * INTO Goods FROM &GoodsTable;      -- materialise it, once
SELECT … FROM Catalog.Products … JOIN Goods …   -- from here it is an ordinary table
```

`&` already means "this came from outside" everywhere else in the language, so it means the same on
a source. Without the mark, `FROM Goods` could be a temp table the package made, a bound table or a
metaobject, and which one would be decided by the order the resolver happens to look.

⚠ **Only INTO.** Refused at the parse otherwise, and not as ceremony: a value table lives in RAM, so
every statement naming it directly stitches the read in memory AGAIN. Materialised once, it is a
table the engine can promote and join server-side — the discipline is what makes the rest of the
package fast.

Nothing new was needed for it: `ibTempTableQueryable` already wraps a runtime table as a source (the
same wrap `Data.From(table)` uses) and parameters already reached the lowering. What was missing was
that resolving a SOURCE never looked at them.

### `CAST` — narrowing, and why that is the whole of it

```
SELECT CAST(Recorder AS Document.Order).Number FROM AccumulationRegister.Goods
```

⚠ **Two different things share the word.** `CAST(Recorder AS Document.Order)` **narrows**: the value
already IS of that type, and the cast only says WHICH of a composite reference's types is meant.
`CAST(Code AS Number)` would **convert**, and that is a different feature.

Narrowing is what the engine was actually missing. A composite reference has no single set of fields
behind it, so `ResolvePath` refuses to walk through one — correctly, because there is no one answer.
The cast supplies the answer, and from that point on **it is an ordinary dot-walk**.

⚠ **The shape is what makes it cheap.** `CAST(x AS T).A.B` parses as a **Column** whose path is
`{A, B}` and whose ROOT (`m_arg`) is the cast. The resolver already walks a column's path from a
starting queryable; the cast only says which queryable to start on. So the chain it builds is
`[the reference column, …, the leaf]` — exactly the shape `SelectPath`, `ExpandDotWalkJoins` and the
RAM join already consume. **Nothing downstream learned a new trick.** The type is resolved by the
same `ResolveSource` a `FROM` goes through, so a cast cannot name what the language could not have
named after FROM.

Two places had to learn the shape, and both are the same class of mistake: a cast-rooted column's
path names fields of the TARGET, not of this source. The optimizer's FROM-flattening must not
substitute an inner output name into it, and the name checker must collect it whole rather than
descend into it.

**Conversion to a primitive is REFUSED, with a message that says why.** It needs an operation the
door does not have — the expression IR is Column / Const / Arith / Case / PeriodTrunc — and it would
have to exist in the SQL provider AND the RAM one, in every dialect, with the rounding and the
failure mode spelled out. That is its own piece of work; a query that parses and then answers
something nobody chose would be worse than a refusal. Nobody has asked for it yet, which is the
other half of the argument: the narrowing is what the composite reference needed, and converting a
number to a string is a want nobody has expressed.

### Still open

The `Query builder` tab has never been built.

### What the tests pin

`tests/test_queryNaming.cpp` (34) covers the ENGINE side of every rule these passes added: the
proposed name and its being written down, the source / output / dimension names, the composer
wrapping an author's query, `DescribeOutput`, the grouping door read both ways, the parameter table,
the aggregate/type door, `CAST`, and the model's one-walk-two-readings. `tests/test_compiler.cpp`
adds three for the settings lists in both modes. `tests/test_frontendQueryConstructor.cpp` pins the
literal scan, read-only, and the CASE builder's round trip.

⚠ **The rest of the frontend is thinly tested, and honestly so** — the combo cell, the per-row
choice, the context menus, the tree expansion are verified by Max running the window and by nothing
else. The GUI harness exists (`FrontendRuntimeFix`, skipped headless), so the gap is work not yet
done rather than a limit of the setup.

---

## 5a. Pass 8 (2026-08-10) — sources that answer, and a window that stops tidying

Everything here was found by running the window, and every fix but one turned out to be a source
declining to answer a question it already knew.

**"What columns do you have" moved to the QUERY half of a descriptor.** `FillSourceExplorer` lived
on `ibMetaCommandDescriptor` — the *list* descriptor. A source that is only queryable (a constant,
first among them) therefore inherited the base's empty default: it stood in the catalogue as a table
with no fields, addable to a query and offering nothing to select. It now lives on
`ibMetaQueryDescriptor`, because asking what a source holds has nothing to do with whether it can be
shown as a list. The same gap, in its own places:

| Source | Was | Now |
|---|---|---|
| a package's TEMP table | a childless leaf — the catalogue never asked the model, which could answer (`GetFields` resolves a temp name to the projection of the statement that makes it) | fields, draggable like any table's |
| register virtual tables (`.Balance` / `.Turnovers` / `.BalanceAndTurnovers`, the slices) | the descriptors never overrode `FillSourceExplorer` | answered from the VIEW's shape (`GetViewQueryable`) — metadata only, no companion built, no database touched. ⚠ deliberately NOT the companion's `GetColumns()`: in RAM mode a companion navigates through the register itself and would report the MOVEMENT columns |
| tabular sections | same | their own attributes |
| a constant | same, plus the column was named after the constant (`Constant1.Constant1`) | `Constant1.Value` — the table is the constant, the column is its value; caption is the system word `Value`, untranslated, so it cannot say one thing while the text says another |

Registration follows the same rule: a turnover-only accumulation register no longer registers
`.Balance` / `.BalanceAndTurnovers` at all. Its view carries no opening / closing / expense column,
so those were two tables in every catalogue answering with dimensions and not one resource column.

**Pictures are asked of the column, never deduced.** `ibBackendSourceColumn::GetColumnIcon()` —
default is the plain attribute picture, and a column that IS a metaobject answers with the one its
metatype registered. So a dimension stops looking like a resource without anybody keeping a list of
kinds: a new kind arrives dressed the day it overrides an icon. Carried into every tree AND every
grid in the window (a grid asks per row now, not per column); an aggregate wears its argument's
picture, an expression keeps the plain one.

**The window stopped tidying behind the author.** `FillAll` ran `PruneUnresolved` before showing
anything, and the verdict line — filled afterwards, by asking the engine about the already-tidied
package — therefore always answered "the engine reads this query" while a column had quietly gone.
Silence plus a clean bill of health is the worst combination a window can have. Now:

- **what is written stays written**, and the engine says what is wrong with it, in its own words at
  its own position;
- name checking no longer swallows an unresolvable SOURCE. A one-segment name is a temp table this
  check genuinely cannot see (silence is right); a qualified `Kind.Name` that the factory does not
  know means the object is GONE — deleted or renamed — and that is reported. (Deletion itself
  already worked: `RemoveMetaObject` → `OnBeforeCloseMetaObject` → `UnregisterSource`. Marking for
  deletion IS unloading the piece; nothing needed a second "am I alive" flag.)
- the ONE thing still removed is a table nobody uses — an unfinished gesture, not a mistake — and
  only **on OK**. Between adding a table and picking its first field it is unused by definition, so
  tidying on every refill would delete it under the hand that just added it. Use counts as: a field,
  a condition, a `HAVING`, a grouping, an order, an `INDEX BY`, a totals aggregate, or **the `ON` of
  any neighbouring join** — the last one matters, or `A JOIN B ON B.Ref = A.Ref` would lose B and
  change what the query returns. The `FROM` is never removed; `SELECT *` disables the sweep.

**Renaming a table carries its references.** An alias is what the rest of the query calls a table
by, and the AST stores those paths as written — so changing it alone turned every one of them into a
name resolving to nothing. The rewrite mirrors the unused-table walk exactly (same rule about which
paths name a source), takes the old name BEFORE the edit (clearing an alias is a rename too), and
stays in THIS select: a union branch means its own table by the same word.

---

## 5f. Pass 9 (2026-08-10/11) — the window offers, the engine judges

The longest run of the window so far, dictated defect by defect while it was open. The engine half
is written up in [query-language-arc.md](query-language-arc.md) (the product as a comma, links that
contradict one another, a nested table's output schema); what follows is what changed in the shell,
and the through-line is one sentence: **the window offers what is possible and never decides what
was meant.**

### Every place a value is written has the same door

Conditions and Totals had a cell you could type in and no way to reach the expression editor. Now
all three doors lead to the same control the rest of the window already uses: a double-click opens
the CELL, the `…` button opens the expression editor, and the toolbar verb is the keyboard route to
the same place. Not a second editor "for conditions" — the same one, because a condition is an
expression and the moment there are two of them they start disagreeing.

`ISNULL(, )` joined the palette beside `IS NULL`, so both are reachable without knowing they exist.

### An aggregate is CHOSEN, not written

Dragging a field into Grouping or Totals opened the arbitrary-expression window and asked the author
to write `SUM(…)` by hand — over a field whose type already says which folds are possible. The row
now lands as the fold its type allows and the **Function** cell changes it afterwards, from the list
the ENGINE gives for that type. Writing an expression is still available; it is no longer the toll
for the ordinary case.

### The Links tab is about LINKS

Three separate confusions, one cause — the tab was quietly acting on tables:

* adding a table CREATED a link (an empty one, which then went red);
* deleting a link DELETED the table;
* a link could not be added by hand at all in some states.

A link is now added, re-pointed and deleted by the author alone. Two tables mean only that the tab
has something to show. **Copying** a link is the copy verb the window already had, not a new button.

Deleting a TABLE, by contrast, cascades: every field, condition, grouping, order and link written
against it goes with it. Nothing tracks that — the walk is the same one the unused-table sweep and
the rename rewrite use, which is why it agrees with them.

### Unions — a column is mapped, and unmapping is not deleting

A union branch's column is a dropdown over what that branch can offer; empty means `NULL`, which is
the honest reading of "this branch has no such field". The one that had to be got right is
**clearing** a cell: the field is not deleted, it is UNLINED — it leaves the shared row and becomes
a row of its own, keeping its own name (numbered on collision). Deleting it would throw away work
the author did in another branch, which is the opposite of what "remove this from the mapping" says.

TOTALS over a union is over the WHOLE union: the tab carries no branch strip, and the totals are
built over the alias table the union publishes.

### `SELECT *` is expanded when the query is opened

A star is a request for every column the query can see, so the window shows them — otherwise the
grid is empty on a query that selects everything, and the author's first act is to re-type what the
text already said.

### Virtual tables ask for their parameters properly

The parameter dialog is one editor plus choice rows: the periodicity is a DROPDOWN whose list comes
from the source (not from a list kept in the window), and the alias offered for a virtual table
carries the register's name — `AccumulationRegister1Turnovers`, not a bare `Turnovers` that says
nothing once two registers are in the query. Which fields the table then has follows from the
periodicity: see [register-totals-strategy.md](register-totals-strategy.md).

### One dialog, not two, when a query cannot be read

Opening a broken query showed an error window and then a second window asking what to do. Now it is
a single gate: the engine's message, and "open on an empty query?" — Yes / No.

### Small ones, all found by running it

| symptom | cause |
|---|---|
| "cannot delete" on a row that was plainly selected | `Reset()` cleared the grid selection on every refill; the selected item is kept and restored across `FillAll` (11 grids) |
| a condition's value vanished when typed by hand | a read-only combo whose value was not in its list committed an EMPTY string, which the model read as "delete this row" — the held value is always in the list now, and an empty commit from a read-only combo is refused |
| the verdict line kept a stale tail | `wxST_NO_AUTORESIZE` |
| pages jumped when switching between statements of a package | the tab that had been open was not remembered (`m_wantedTab`) |
| an em dash arrived on screen as `вЂ”` | a non-ASCII character inside `_()`; the sources have no BOM and MSVC reads them in the system codepage. ASCII only in literals |

### What this pass did NOT do

Left open on purpose, and each one is a piece of work rather than a polish: reordering a run of
inner joins so a link may name a table added later; the periodicity shorthand on turnovers;
periodicity and fill method as registered enumerations instead of a word list declared by the
source; a filter structure written in script code converting into the same condition.

---

## 6. What this must not become

- **A generator that cannot read.** The moment a hand-edited query stops being loadable,
  the constructor is abandoned. The round trip is the product; the tabs are the packaging.
- **A second query language.** Everything the constructor edits must be expressible in the
  text, and everything in the text must survive a trip through the constructor. The AST is
  the single source; a "constructor-only" setting would be a second currency.
- **A hand-written list of metatypes.** See § 4.
