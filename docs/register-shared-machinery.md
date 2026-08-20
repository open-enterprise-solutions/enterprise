# Register shared machinery — what the three registers share, and what is still written twice

Scope: `InformationRegister`, `AccumulationRegister`, `AccountingRegister` —
`src/engine/backend/metaCollection/partial/`. This document is an inventory of the code the three
have in common: what already lives in one place, what is still written two or three times, and what
must stay separate. It exists because the accounting register was rebuilt on 2026-08-13 by copying
the accumulation register's shape, and a copy is only safe while somebody remembers which parts were
meant to be copies.

> Verified against the tree on **2026-08-13**. Line numbers are orientation only, and
> `accountingRegisterMetadataTotals.cpp` grew by ~130 lines during the audit itself — that file is
> therefore cited by SYMBOL, with no line. Everywhere: the **symbol name is the anchor**, the line is
> a hint.

Companion docs: [register-totals-strategy.md](register-totals-strategy.md) (the materialisation the
totals bundle is made of), [accounting-register-arc.md](accounting-register-arc.md) (that register's
own decisions and gaps), [query-engine-layers.md](query-engine-layers.md) (the L1–L5 floor plan the
tier names below refer to).

---

## 1. What is already shared, and what each piece removes

Everything below lives in **`registerQueryLowering.h`** and is included by all three registers
(`informationRegister.h:10`, `accumulationRegister.h:13`, `accountingRegister.h:12`).

| Symbol | Line | What it is | The defect class it removes |
|---|---|---|---|
| `ibRegBound` | 30 | a boundary: a date, the document at it, and which side of it is meant | a boundary re-assembled per callsite, so `Balance(d)` and `Balance(moment)` end up meaning different things in two readings |
| `ibReadRegisterBound` | 41 | reads that boundary out of a `PointInTime` / `Boundary` / bare date | two parsers for one argument — a script's moment and a query's moment drifting apart |
| `ibRegisterUnits` | 82 | the ten calendar words, **ordered by coarseness** | one dictionary in three places (a `switch` in the reader, a table in the schema builder, a choice list in `DescribeParameters`); drift shows up as a column nobody can name. The order is load-bearing: the schema offers only units coarser than what is stored (`u > GetTotalsPeriodUnit()`) |
| `ibRegGranularity` + `ibRegFold` + `ibReadRegisterFold` | 115–186 | the periodicity as a **type** with five answers, plus the word → type reader | the old `(unit, unitGiven)` pair had room for four answers and five were needed: `Period` and "left out" both read as `unitGiven == false` while meaning different things |
| `ibRegFigure` / `ibRegSide` / `ibRegSidedFigure` | 227–247 | the figure words (`Turnover`, `Receipt`, `Expense`, `Balance`, `Opening…`, `Closing…`), the side suffixes (`Dr`/`Cr`), and the built pair | `Resource1_Turnover` against `Resource1Turnover` — three spellings of one column name (view, reading, manager), each compiling, one answering with nothing |
| `ibRegFillArmCut` + `ibRegRecorderTuple` | 268–343 | the GRAIN rule for the L2-2 read spec: stored rows below the boundary's grain, movements of that grain up to the boundary | counting the current grain twice, in a result that looks entirely plausible; and the recorder tuple decomposed by the same codec the rows were written through, so the comparison rides the index |
| `ibRegFilterPredicate` | 439 | a script's `Structure` → the same predicate the parser produces from `WHERE …` | the structure was unwrapped BY HAND in five places, each able to express equality on a dimension and nothing else |
| `ibRegFlatLeaves` | 471 | the flat AND-leaves of a predicate, in order | five hand-rolled walks over the same predicate; deliberately narrow (Equal leaves under And), so a richer condition has *no* leaves rather than being half-applied |
| `ibRegCompositeIR` | 500 | (column, value, op) → per-field IR with the values bound as `Const`, asking `DescribeColumnLayout` which fields TAG and which CARRY | position-and-spelling guessing (`i == 0`, `EndsWith("_RTRef")`) re-deriving the column layout where a lettering change would never arrive |
| `ibValueMetaObjectRegisterTotals` | 203 | a metaobject that declares nothing and holds an **identity** for a derived table — and, since 2026-08-14, its NAME as well (§ 4c) | `metaID ^ 1` was the neighbouring metaobject's id (metaIDs are small sequential integers), and `ibSchemaSnapshot::Shared` matches on id alone — the totals declaration poured its columns into an unrelated table. `GenerateNewID` makes the id unique by construction and a save keeps it |
| `ibRegFieldsOf` / `ibRegValueField` / `ibRegTypeField` / `ibRegFieldList` / `ibRegQualifiedList` / `ibRegAliasedList` / `ibRegJoinEq` / `ibRegQualifiedEqParams` | 350–414 | thin register-side wrappers over the column-layout tier | hand-rolled walks over the physical field list, one per register |

The accumulation register keeps the totals holder under its old name —
`using ibValueMetaObjectTotals = ibValueMetaObjectRegisterTotals;` (`accumulationRegister.h:184`) —
so its own code reads as it did. There is one class, two names, and the alias is the only difference.

---

## 2. Written twice or three times — with the sites

### 2.1 ✅ DONE 2026-08-13 — the derived-surface builder was THREE copies, now one

> `ibRegSurfaceCache` (`registerQueryLowering.h`) holds the cache, the signature check, the retired
> list and the id band; `ibRegSignAttribute` builds the signature; `ibRegAttributeColumn` publishes an
> attribute as itself. Each builder keeps only what genuinely differs — **which columns**, and **what
> the key is** — and hands the columns over as a lambda.
>
> Three things stayed the caller's on purpose, and they are three DIFFERENT strings that a single
> "name" parameter would have quietly merged: the cache KEY (a view's name where one name is one
> surface; the whole call where the output schema follows the arguments), the RELATION the surface
> reads (`<table>_<shape>` for the per-call shapes), and the column set.
>
> The inventory below is what was there before.

One function shape, written three times: build a `ibDbTempTableQueryable` describing a surface, cache
it, and hand out the pointer.

| Copy | File | Function |
|---|---|---|
| A | `accumulationRegisterMetadataSchema.cpp:310` | `GetViewQueryable(viewName, shape)` |
| B | `accountingRegisterMetadataSchema.cpp:334` | `GetTurnoverViewQueryable(creditSide)` |
| C | `accountingRegisterMetadataTotals.cpp` | `GetShapeQueryable(shape, kindsDr, kindsCr, fold)` |

Six parts, present in all three:

| Part | A | B | C | Why it is there |
|---|---|---|---|---|
| cache keyed by the surface's name / key | 356 | 360 | `m_shapeSources.find(key)` | surfaces are read far more often than metadata changes |
| a **signature of what it was built from** (attribute names + clsids, in order) | 342–354 | 341–358 | the `sign` lambda | a surface asked for before the register's attributes were read stayed empty for the life of the session; counting attributes caught an ADDED column and missed a RE-TYPED one, so the signature is names and types |
| retired list — the old surface is moved aside, never destroyed | 363 (`m_retiredViews`) | 364 (`m_retiredShapes`) | `m_retiredShapes` | a reader may hold a pointer handed out earlier; a surface that dies under a live reader is an access violation |
| synthetic column-id band `0x50000000` | 368 | 369 | `ibMetaID synthetic = 0x50000000u` | derived columns need ids clear of every metaID |
| an attribute published **as itself** (its own metaID and type) | 400–413 | 374–411 | the period / account / slot / dimension pushes | the surface stays interchangeable with the register as a source instead of becoming a parallel vocabulary |
| a logical/physical name **pair** built from one suffix | `add`, 420 | `addFigure`, 415 | `addFigure` | a query writes `Resource1Turnover`, the table keeps `Resource1_Turnover`; spelled twice they drift |

The id bands are a fourth thing to notice: `0x50000000` (derived column) and `0x40000000` (the second
stored column of a resource, and the aggregate receivers) are re-declared as a local constant at each
site — `accumulationRegisterMetadataSchema.cpp:165`, `accountingRegisterMetadataSchema.cpp:228`,
`query/queryLowering.cpp:1016`, `query/queryProvider.cpp:1582/1727/1740/2196`,
`query/dbTableProvider.cpp:1630`, `backend/tabularModelDb.cpp:371`,
`frontend/visualView/ctrl/formCommand.h:32`. No one place owns the bands; every new one is claimed by
reading a comment.

### 2.2 ✅ DONE 2026-08-13 — the totals bundle: the identical parts moved, the key did not

> **What moved** (`registerQueryLowering.h`), and each of these had two copies:
>
> | Piece | Why it could move |
> |---|---|
> | `ibRegSplitIntoKey` | the shard column joins the KEY, carries an identity rather than being scaffold, and is named by the totals object — three decisions, no register-specific part |
> | `ibRegGuardInForce` | *this movement is in force* means the same on both, down to the ` <> 0` — **and one copy shipped without it**, so the totals of that register accumulated retracted movements while looking entirely normal |
> | `ibRegAccumulatorColumn` | precision and scale come from the RESOURCE; declared flat it was `NUMERIC(18,0)` and kopecks were lost on the way into the totals. Three sites had this arithmetic and one was wrong |
> | `ibRegSelfSourceFromDeclaration` | the table as a source, built from its own scaffold + columns; without it both L3-4 operations return success having touched nothing |
>
> **What did NOT move, deliberately:** the KEY (a period plus dimensions, against a period, an
> account, a *(kind, value)* pair per analytic and the dimensions), what a movement CONTRIBUTES, the
> second guard a correspondence register declares, and the views. A single procedure over all of that
> would take the union of both signatures and branch inside on which caller it has — which is one
> function serving two callers separately while reading as if it served them together. The output of
> this code is a schema DIFF; a wrong one is a `DROP`, so the bar for "the same" is that the two
> texts are identical, not that they rhyme.
>
> ⚠ **These four are not unit-testable in the current harness** — every one of them takes metadata
> (an attribute, a totals metaobject, a schema table), and the suite builds no metaobjects. What
> covers them is the live apply, which is also the only thing that ever covered the DDL barrier.
>
> The inventory below is what was there before.

`accumulationRegisterMetadataSchema.cpp:36` and `accountingRegisterMetadataSchema.cpp:47` (whose body
is the `declareSide` lambda at 97, called once or twice). Statement for statement:

| Step | Accumulation | Accounting |
|---|---|---|
> Since 2026-08-13 one step of it is genuinely shared: the UNIQUE index line became
> `ibDeclareDerivedKey(t, totalsName, keyCols, id)` in both files (`query/schemaSnapshot.cpp`), because
> HOW a key is held unique depends on the ENGINE and not on the register — see § 4a.

| scaffold the period column | 72 | 108 |
| logical key columns (period + dimensions / + account + breakdown pairs) | 76–79 | 111–135 |
| shard column joins the KEY when `SplitTotals` is on | 94–99 | 145–150 |
| UNIQUE index on the key | 104 | 155 |
| `t.Derived(GetQueryable())` + `m.Split(...)` | 106–107 | 157–158 |
| `m.Period(field, attr, "{row}."+field, GetTotalsPeriodUnit())` | 127 | 163 |
| `m.Guard(...)` — the movement is in force | 142 | 175 (+ a second guard at 196) |
| `m.Key(...)` per key attribute | 145–146 | 201–210 |
| `m.Accumulate(...)`, the CASE pair over the record type | 196–205 | 262–273 |
| `t.SelfSource(ibSchemaTableQueryable{...})` built from the table's own declaration | 220–226 | 281–287 |
| `m.View(...)` with `m_withMovements` — the second arm | 231–256 | 294–314 |

Both bodies also carry the same two prologue rules: warn-and-skip when the register has no resources
(`:55` / `:85`) and a stored grain of `Day` (`GetTotalsPeriodUnit()`, declared once per register
class).

What genuinely differs is only the KEY (an account plus its breakdown pairs, twice, versus the
dimensions) and how many tables are declared. The declaration order, the shard rule, the unique
index, the guard placement and the self-source are the same procedure.

### 2.3 The reading rules — the same six answers, expressed twice

Accumulation sites are in `accumulationRegisterMetadataTotals.cpp` (lines given); accounting sites are
in `accountingRegisterMetadataTotals.cpp` (symbols only).

| Rule | Accumulation | Accounting |
|---|---|---|
| seed the returned table **from the published surface** (ids, names, types) | `SeedRamTableFromView`, `:41` | `SeedFromShape` |
| re-point the filter's leaves at the surface the reading stands on | `:141`, `:258`, `:390` — three inline copies of one loop | `WhereCondition` |
| the stored arm / the movement arm, told apart by the recorder being NULL | `RestrictToStoredArm`, `:74` | `StoredArm` / `MovementArm` |
| group by the periodicity: a calendar unit TRUNCATES, `Period` groups the column as it stands, whole groups by neither | `:275–283`, `:445–450` | inside `ComputeTurnover`, and again in `ComputeBalanceAndTurnover` |
| the key of a row as the TUPLE of its values (`ibValueSeqHash` / `ibValueSeqEqual`) — a string joined by `\x1f` until 2026-08-15, when the rendered identity was removed | `ibBalanceOpening`, `queryRamTable.h` | `ibAcctKey` — same shape |
| a row of all zeros is **not a row**, and the rule is "any figure non-zero" | `:169–178`, `:305–316`, and `m_dropZeroRows` at `:637/678/731` | `PourRows` |

The last one is worth stating once for both: receipt 10 against expense 10 nets to zero and MUST stay
(something happened); only a row where every figure is zero goes. Written twice, that distinction is
one edit away from becoming "the net is zero" in one of them.

### 2.4 ✅ DONE 2026-08-13 (first half) — "is the database open" is one sentence

| Site | Line |
|---|---|
| `accountingRegisterManager_impl.cpp` — `RequireOpenBase()` | 65 |
| `accumulationRegisterManager_impl.cpp` — `Balance` | 50 |
| `accumulationRegisterManager_impl.cpp` — `Turnovers` | 103 |
| `informationRegisterManager_impl.cpp` — `Get(filter)` | 21 |
| `informationRegisterManager_impl.cpp` — `Get(period, filter)` | 63 |
| `informationRegisterMetadataSlice.cpp` — `ComputeSlice` | 88 |

The information register's three wrote it as a two-branch `if / else if` raising the same message
twice; the accumulation register's two wrote it as one condition; the accounting register named it
(`RequireOpenBase`) — but named it in its own file. Same sentence, four spellings.

All six now call `ibRequireOpenBase()` (`registerQueryLowering.h`). The accounting register had the
right half of the answer (a name) and the wrong half (a name of its own).

Beside it, the row-dressing wrapper, five functions of one shape:

| Function | File | Columns come from |
|---|---|---|
| `SelectionToBalanceTable` | `accumulationRegisterManager_impl.cpp:24` | the register's dimensions + `ibRegFigure::Balance` per resource, listed **again** here |
| `SelectionToTurnoverTable` | `accumulationRegisterManager_impl.cpp:62` | ditto, with Turnover / Receipt / Expense |
| `SelectionToTable` | `accountingRegisterManager_impl.cpp:39` | **the shape's own columns** — nothing listed twice |
| `SelectionToTable` | `informationRegisterManager_impl.cpp:115` | every generic attribute, by metaID |
| `SelectionToRecord` | `informationRegisterManager_impl.cpp:136` | ditto, into a `Structure` |

The accounting one is the shape the other four want: ask the source what it publishes, then copy
those columns. The accumulation pair re-lists the figures, which is the third spelling of a name the
view and the reading already agree on.

⚠ **CHECKED 2026-08-13, and the straight copy would LOSE something.** The two lists do not merely
disagree about spelling — they carry different amounts. The accumulation pair builds each column with
a real PRESENTATION (`res->GetSynonym() + " " + _("Balance")` — "Amount Balance", in the user's
language), while a surface column is an `ibTempColumn`, which does not override `GetSynonym()` and so
answers with its own NAME. Moving the runtime table onto the surface as it stands would turn every
figure column's caption into `Resource1Balance`, in English, in a window an accountant reads.

So the step is one item longer than it looked, and in the right order it is an improvement rather
than a trade:

1. `ibTempColumn` carries a SYNONYM (it already carries a name, a type and a physical field; a
   presentation is the fourth thing a published column owes its reader);
2. the derived-surface builders set it — which needs the figure words to have LOCALISED captions
   beside the logical `ibRegFigure` spellings, in ONE place rather than in each manager;
3. then both `SelectionTo*Table` and the accounting `SelectionToTable` become one function over
   `shape->GetColumns()`, and the accounting readings GAIN captions they never had.

That third line is why this is worth doing rather than skipping: today the register whose readings
are "the good copy" is the one whose columns have no presentation at all.

### ✅ DONE 2026-08-13 — and it landed in that order

| Piece | Where |
|---|---|
| a published column carries a THIRD name — the caption (`GetSynonym()` override; empty = the name, which is the base's own answer) | `ibTempColumn`, `query/tempTableQueryable.h` |
| the figure words gained localised captions beside their logical spellings, plus the sided form | `ibRegFigureCaption` / `ibRegSidedCaption` / `ibRegFigureColumnCaption`, `registerQueryLowering.h` |
| an attribute published AS ITSELF is one call — name, field, type, metaID **and** caption, all from the attribute | `ibRegAttributeColumn`, same header; it replaced 16 hand-written pushes across the three builders |
| the accounting figure builder takes **(figure, side)** instead of the spelled suffix | `GetShapeQueryable` — the name and the caption are both built from the pair, so the side is never recovered by reading the last two letters |
| one dressing function over `shape->GetColumns()` | `ibRegSelectionToTable`, called by all three managers |

Two consequences worth stating, because they are behaviour rather than shape:

- **the accumulation register's turnovers table gains a column it was missing.** The hand-written
  list enumerated dimensions and figures; asked for a periodicity, that reading also publishes a
  `Period`, and the old list did not know it existed. The shape does, because the shape produced it;
- **the accounting readings gain captions they never had.** Their copy handed `GetName()` in as the
  presentation, so a column read `Resource1BalanceDr` where the neighbour read "Amount Balance".

### 2.4a ✅ DONE 2026-08-13 — the ARGUMENT ORDER is now one rule for both registers

The accumulation register declared its arguments as *interval, condition, then the refinements*, on
the argument that an option nobody states belongs where leaving it out is free. That is true of a
DEFAULT and false of an ORDER: an author writes the interval and immediately says how to cut it, and
the accounting register's own tables (and every reference implementation) put the periodicity third.

Both now run: **interval → how it is cut → what to do with an empty period → condition.**

| | Before | After |
|---|---|---|
| `ibRegTurnoverArg` | Begin, End, **Filter**, Periodicity | Begin, End, **Periodicity**, Filter |
| `ibRegBalTurnArg` | Begin, End, **Filter**, Periodicity, FillMethod | Begin, End, **Periodicity**, FillMethod, Filter |
| `ibRegBalanceArg` | Period, Filter | unchanged — a moment and a condition, nothing to reorder |

The `static_assert`s moved with it: they now guard the new rule (*the cut follows the interval*, *the
fill method is about the periods too*, *the condition is last*) rather than the old one. Both
`DescribeParameters` bodies were re-sequenced to match, because that list is read **positionally** —
two lists agreeing on names and differing on order is a call whose condition lands where the
breakdown was expected, which is the defect this register has already paid for once.

⚠ **This re-reads every existing positional call**: the third argument of `Turnovers(…)` is now the
periodicity, not the condition. The window for that is open only while there are no third-party
configurations.

### 2.5 ✅ DONE 2026-08-13 — the descriptor helpers moved to `registerQueryLowering.h`

These are generic by content and reachable only by including the accumulation register:

| Helper | Line | What it answers |
|---|---|---|
| `ibRegArg(paParams, size, slot)` | 824 | one argument slot: present, non-null, in range — or empty |
| `ibRegisterFoldOfArgs(args, slot)` | 939 | the fold of a declared argument list |
| `ibFillRegisterIntervalParameters` | 955 | `BeginOfPeriod` / `EndOfPeriod`, typed by the register's own period attribute |
| `ibAppendRegisterConditionParameter` | 975 | the `Condition` slot |
| `ibAppendRegisterPeriodicityParameter` | 991 | `Periodicity`: the four shape words (`Period` / `Record` / `Recorder` / `Auto`) plus every unit from `ibRegisterUnits()`, and no default — empty is its own answer |
| `ibRegisterViewColumnFits` / `ibRegisterFoldOffersColumn` | 668 / 706 | does this granularity produce that column |
| `ibFillExplorerFromRegisterView` | 718 | the field tree of a surface, attributes handed over as attributes |

What the other two registers do instead:

- **Accounting** re-writes `ibAcctSourceDescriptor::DescribeParameters` inline
  (`accountingRegisterMetadataTotals.cpp`), including a second copy of the periodicity block with the
  same four words and the same `ibRegisterUnits()` loop; and a second `ArgAt`, in two overloads, that
  is `ibRegArg` and `ibRegisterFoldOfArgs` under another name.
- **Information** writes the `Period` + `Condition` pair by hand (`informationRegister.h:384`) and
  indexes `paParams[0]` / `paParams[1]` directly (`:366–370`).

> **Moved.** `ibRegArg` (both overloads — the accounting register's `ArgAt` was the same function under
> another name), `ibRegisterFoldOfArgs`, `ibFillRegisterIntervalParameters`,
> `ibAppendRegisterConditionParameter` and `ibAppendRegisterPeriodicityParameter` now live in
> `registerQueryLowering.h` and both registers call them. The interval helper takes the period TYPE
> rather than a register, which is the one part that genuinely differs per register and the reason it
> could not be shared before. `ibRegisterViewColumnFits` / `ibFillExplorerFromRegisterView` stayed with
> the accumulation register: they answer about ITS view shapes.

So the *(period, condition)* parameter pair WAS declared four times, and the argument reader three.
The order rule that the accumulation register states to the compiler
(`static_assert`s at `accumulationRegister.h:799–804`: the condition follows the period, refinements
come last) protects only the register that declares it, although it is the rule that stopped a
declared periodicity from pushing every call's filter into the wrong slot.

### 2.6 ✅ DONE 2026-08-13 — "Which attribute is this column?" is one question on the base

Both derived-surface builders publish some columns under an attribute's own metaID, so both field
trees need the inverse lookup:

| Copy | Where | Covers |
|---|---|---|
| `attributeById` lambda | `accumulationRegister.h:742` | period, dimensions, resources |
| `AttributeById` | `accountingRegisterMetadataTotals.cpp` | period, recorder, line number, record type, account, credit account, dimensions, resources — six hand-written accessors, then two loops |

⚠ The two already differ, and the difference reaches the window. The accumulation register's turnovers
view publishes the **recorder and the line number** under their own metaIDs
(`accumulationRegisterMetadataSchema.cpp:408–413`), but its lookup does not know them — so wherever
the granularity offers those columns (`Auto` / `Recorder` / `Record`) the field tree appends them as
plain synthetic columns (`explorer.AppendColumn(col)`, `accumulationRegister.h:767`) instead of as the
attributes they are. That is exactly the loss the accounting copy's own comment names: a synthetic
triple carries no picture and does not say it holds a REFERENCE, so the same field unfolds on the
register one node up and refuses to unfold here.

**Landed as proposed** — one question on the base, no list on either register:

```cpp
// ibValueMetaObjectRegisterData (commonObject.h)
const ibValueMetaObjectAttributeBase* FindAttributeByColumnId(const ibMetaID& id) const;
```

Both copies are now one call to it. The accounting list turned out to be short by the same class it
was written to fix — nothing in it named the ANALYTICS SLOTS, which a `Records` reading publishes
under their own metaIDs — so the walk removed a hole in the copy that was supposed to be the good one.
⚠ It is a **behaviour change** for the accumulation register's field tree (the recorder and the line
number now arrive as attributes, with their picture and their reference-ness) and belongs in its own
commit for that reason.

The base can answer it whole today: `GetGenericAttributeArrayObject()` already walks the predefined
attributes, the common attributes, and every dimension / resource / attribute
(`commonObject.h:1277`), and `FillArrayObjectByPredefinedAttribute` is where each register declares
what it owns — the accounting register's slots included
(`accountingRegister.h:541`). One walk, and a register that grows an attribute is found by it
without anybody adding an accessor to a list. Both copies above then become one call, and the
accumulation register's field tree gains the recorder as an attribute without a separate fix.

---

## 3. What must NOT be shared, and why

Six things that look like the same question and are not. Sharing any of them would mean one register
answering another's question.

| Not shareable | Why |
|---|---|
| **The accounting register's output schema follows the CALL** | the requested kinds decide how many breakdown columns exist and what they are called (`GetShapeQueryable(shape, kindsDr, kindsCr, fold)`), so its cache key holds the arguments — shape, fold kind, fold unit, then each kind's hash. Every other register answers the same question from its metaobject alone. A shared builder would have to take the wider signature and every caller would pass empties, which is a shared function serving one caller |
| **Two totals tables, one per side** | a totals row is keyed by the account it is ABOUT; in correspondence mode one movement is about two accounts under two breakdowns, so two keys and two upserts (`GetTotalsTableNameDB(creditSide)`). The accumulation register's two tables are a different fact — one per register KIND. ⚠ Since 2026-08-14 both registers DECLARE both tables always, named after the totals metaobjects rather than after the setting, and only the maintenance follows the mode (§ 4c) — "only one of them is ever declared" no longer holds of either |
| **The fold by account type, and `SummaryOnly`** | `FoldSideByAccountType` reads a flag on the ACCOUNT — active folds into debit, passive into credit, active-passive does not fold. `FoldOutSummaryOnly` drops a breakdown from the BALANCE key and merges the rows that then coincide (turnovers drop nothing), and a `BalanceAndTurnovers` row standing on such a breakdown keeps its balance columns EMPTY rather than zero. Both read data — a row of the account's own kinds table — and neither has a counterpart in a register with no chart of accounts (`accountingRegisterMetadataTotals.cpp`) |
| **The information register has no materialisation, and will not get one** | a slice is "the record nearest a MOMENT", so the answer depends on the question; a SUM does not (`informationRegisterMetadataSlice.cpp:11–14`). There is nothing for a stored surface to hold, so the shared totals machinery has two tenants, not three |
| **The argument layout: computed vs fixed** | `ibAcctArgs::For(shape, correspondence)` COMPUTES which slot is which because the signature genuinely differs by mode (`accountingRegister.h:93`). The accumulation register's `ibRegBalanceArg` / `ibRegTurnoverArg` / `ibRegBalTurnArg` are fixed namespaces with `static_assert`s. Collapsing them would replace a compile-time statement with a runtime one for the two registers whose signature never varies |
| **The arm cut exists in two FORMS for two consumers** | `ibRegFillArmCut` fills an `ibMaterializeReadSpec` (L2-2: floor, head split, boundary tuples) for a server-side read; `ArmCutAtMoment` / `ArmCutOverRange` build an L3 predicate over the view's two arms for a RAM read. Same RULE, different currencies. The **rule** should be stated once (and is, in the header comment above `ibRegFillArmCut`); the two functions cannot be one until the accounting readings gain `GetSourceRelation` and stop needing the predicate form — see [accounting-register-arc.md § 8.3](accounting-register-arc.md) |

---

## 4. ✅ FIXED 2026-08-13 — the accumulation register's `Active` guard had no comparison

> Repaired at the site below (`+ wxT(" <> 0")`), and the class swept: three `Guard` calls exist in the
> tree and all three now render a comparison. The `Accumulate` expressions were checked in the same
> pass and are correct as they stand — a bare field there is a VALUE, which is what an accumulation
> wants; only a guard lands in a `WHERE`. Kept below because the reasoning is the durable part.

`accumulationRegisterMetadataSchema.cpp:141`, as it was:

```cpp
if (const ibValueMetaObjectAttributeBase* active = GetRegisterActive()) {
    m.Guard(wxT("{row}.") + ibRegValueField(active),
        ibQueryPredicate::Leaf(ibQueryCondition{ active, ibQueryFilterOp::Equal, ibValue(true) }));
}
```

`accountingRegisterMetadataSchema.cpp:174`, the same place, with the comparison written out and the
reason recorded above it:

```cpp
m.Guard(wxT("{row}.") + ibRegValueField(active) + wxT(" <> 0"),
    ibQueryPredicate::Leaf(ibQueryCondition{ active, ibQueryFilterOp::Equal, ibValue(true) }));
```

The first argument of `Guard` is **SQL text**, and it lands in a `WHERE`: the delta statement
(`databaseMaterializeBuilder.cpp:250`, `WHERE <guard>`) and the movement arm of the view
(`:401`, the same guard so the arm reads what the trigger accumulated). A boolean attribute is stored
as `SMALLINT`, so `WHERE {row}.fld…_B` is a value where a condition is required — Firebird answers
*invalid usage of boolean expression* and the whole `CREATE TRIGGER` fails, taking the restructuring
that emitted it down with it.

Facts around it, so the fix is not mis-scoped:

- the guard was added to the accumulation register on **2026-08-11** (`ac1fdf24`, *registers: below
  the grain, and what is in force*), i.e. after the last live Firebird apply of that bundle
  (2026-07-29) — nothing has run it against Firebird since;
- the second predicate argument (`ibQueryPredicate::Leaf(...)`) is the regeneration path and is
  correct in both; only the rendered text differs;
- the accounting register hit this on 2026-08-13 and fixed its own site, which is how the divergence
  arrived: one site repaired, the neighbour it was copied from left as it was.

The repair is `+ wxT(" <> 0")` at that one site. The larger question is that the guard's TEXT is
spelled by each register while its meaning — "this movement is in force" — is the same for both, which
is why it belongs to the bundle extraction (§5 step 6) rather than to a rule anybody has to remember.
A "boolean" that is a `SMALLINT` in the physical schema is a fact of the WRITE codec, not of the
register, and the register should not be the layer that knows how to compare one.

---

## 4a. A key an index cannot hold — the totals identity moved into one field (2026-08-13)

The first live apply of an accounting bundle failed on its own key:

```
too many keys defined for index ACCOUNTINGREGISTER1094_TT_PK
```

**A key is LOGICAL columns; an index is their PHYSICAL fields.** A reference is three of those
(`_TYPE` / `_RTRef` / `_RRRef`), and the accounting totals key is the period, the account, a
*(kind, value)* PAIR per analytic and the register's dimensions — about twenty-one segments at four
analytics, against Firebird's sixteen. Nothing in that key is removable: the kind belongs in it for the
reason § 3 gives (the same slot is a counterparty on one account and an item on another), and the value
half is a composite by construction. The accumulation register reaches the same ceiling later, at five
reference dimensions.

So the KEY stays as declared and its UNIQUENESS moves:

| | Where the identity lives | What the delta matches on |
|---|---|---|
| index holds the key (PG, SQLite, and any key under the ceiling) | the key columns, UNIQUE — unchanged | the key columns |
| key past the ceiling (Firebird) | `keyhash_`, a digest of the key's own values, UNIQUE | **still the key columns** |

Three properties are worth stating, because each one is a decision:

- **the match is not the digest.** Firebird's MERGE goes on comparing the key columns, so a digest
  collision cannot merge two different keys into one row — it can only refuse an INSERT, loudly. The
  engines that name their conflict target by column list (`ON CONFLICT (…)`) have no such option and
  name the digest column; they are also the ones whose ceiling nobody reaches, so it stays theory;
- **a part is hashed before the parts are joined.** A key field may be a binary reference key or an
  unbounded string, and casting either into text to concatenate it is a character-set error or a silent
  truncation. `HEX_ENCODE(CRYPT_HASH(<field> USING MD5))` is 32 ASCII characters whatever it holds.
  ⚠ That form is **Firebird 4+**; on FB 3 the trigger would not compile — but a register whose key
  passes sixteen segments cannot be created on FB 3 at all, so nothing that used to work stops;
- **a rebuild's rows get the digest too.** The trigger fills it as it inserts; `ibDerivedState::Regenerate`
  writes through the ordinary door and cannot, so it finishes with `ibFillKeyHashes` — one UPDATE over
  the rows that have none, digesting the table's own key columns through the same expression
  (`KeyColumnValues` walks one list for both currencies). Left empty it would be worse than absent: a
  UNIQUE index over NULLs enforces nothing on any engine, and where the upsert conflicts on that column
  the next movement for a rebuilt key would insert a SECOND row beside the first.

⚠ **And the digest column's WIDTH is part of the mechanism, not a detail.** The first live apply got as
far as the index and stopped at *"key size exceeds implementation restriction"*: the column had been
declared as an ordinary raw string, which meant `VARCHAR(255)`, and in a UTF8 database Firebird sizes an
index key from the DECLARED length — 1020 bytes, past the ceiling (about `page_size / 4`) before a single
row exists. It is `VARCHAR(32)` now, the exact width of a hex MD5, and `ibRawDBColumn::String` learned to
carry a length so the next indexed raw column can say its own. The same apply DID create all three
triggers, so the FB4 digest form (`HEX_ENCODE(CRYPT_HASH(… USING MD5))`) is confirmed live.

⚠ **The same defect, the other way round, on the ACCUMULATING columns.** `ibRawDBColumn::Number` gained
precision and scale for exactly the reason above: declared flat, a totals column came out `NUMERIC(18,0)`,
so a resource carrying a fraction lost it *on the way INTO the totals* — not rounded on display, stored
rounded, and the movements it was folded from still hold the kopecks that no longer add up. Both registers
now take the numbers from the RESOURCE's own declaration (`GetPrecision()` / `GetScale()`, 18 when it names
none) and leave only the WORD (`NUMERIC` / `DECIMAL`) to the dialect —
`accumulationRegisterMetadataSchema.cpp:175`, `accountingRegisterMetadataSchema.cpp:244`. The width and the
scale are the same lesson: **a raw column that has a reason to name its own type must name it**, because the
layout tier's default is a guess that becomes a CREATE TABLE.

Where each piece lives: `m_maxIndexSegments` on `ibDialectDictionary` (a DDL fact — FB 16,
PG 32, SQLite none), the three hash templates on `ibMaterializationDialect`, `ibKeyNeedsHash` /
`ibFillKeyHashes` in `databaseMaterializeBuilder.*`, and `ibDeclareDerivedKey` in
`query/schemaSnapshot.cpp` — the one place that turns "this is the key" into "this is what the database
is given to enforce it with", called by both registers.

⚠ Past the ceiling the table also gets a **plain index over the leading key columns that fit**. The
unique index answers "is this key already here", not "where is it": the match runs over the columns, and
an index over a digest cannot serve a comparison of the fields it was made from. Without that second
index every posting would scan the whole totals table.

---

## 4b. ⚠ A SECOND GUARD REPLACED THE FIRST — found while writing the tests (2026-08-13)

`ibSchemaMaterialize::Guard` **assigned**:

```cpp
ibSchemaMaterialize& Guard(const wxString& expr, const ibQueryPredicatePtr& regenExpr = nullptr)
{
    m_guard = expr; m_guardExpr = regenExpr; return *this;   // ⚠ the second call wins
}
```

A correspondence accounting register declares two of them — *the movement is in force* (`Active`) and
*this side names an account* — so the second overwrote the first and **every totals table of every
correspondence register accumulated inactive movements**. Nothing about the result looks wrong: the
rows are all rows, and the figures are merely larger than they should be.

It now composes with `AND`, in both forms (the trigger's TEXT and the regeneration's PREDICATE, which
have to state the same rule or a rebuild produces different numbers than the trigger did). Composing is
also what the caller means each time: every guard names a reason this movement does not belong in this
table, and reasons accumulate.

The class is worth carrying: **a fluent setter that a caller may reasonably call twice must compose,
or say in its name that it replaces.** `Key`, `Accumulate` and `View` all append; `Guard` and `Period`
assign. `Period` is right to assign (a table has one period column); `Guard` was not.

Covered now by `tests/test_accountingTotals.cpp` — an inactive line must move nothing even with a
second guard declared, which is the exact case that was silently wrong.

---

## 4c. ✅ FIXED 2026-08-14 — a NAME, a DECLARATION and a BARRIER that followed a setting

A day of applying, editing and re-applying both bundles against a live Firebird base. Every failure
in it was one sentence: **state that belongs to an OBJECT was derived from a SETTING instead of
asked of the object.** Three faces are in machinery the two registers share and are below; the
accounting register's own — its column list, its slot typing, its slot names — are in
[accounting-register-arc.md § 5i](accounting-register-arc.md).

### The totals table's NAME came from the mode

| | Was | Is |
|---|---|---|
| accounting | `…_Tt` one-sided, `…_TtDr` / `…_TtCr` in correspondence | `AccountingRegister1005_DebitTotals` / `_CreditTotals` |
| accumulation | `…_T` for balances, `…_Tn` for turnovers | `AccumulationRegister1017_BalanceTotals` / `_TurnoverTotals` |

Spelled from the setting, the name was a **function of a checkbox**: toggling it renamed both tables
at once and the old name became *unspellable* — nothing could compute it any more, so the differ
could neither find the tables nor drop them, and the maintenance was rebuilt against a name that no
longer existed (*Table unknown …_TTDR*, with the tables already gone).

The side, and the kind, are properties of the **totals metaobject** — there are two of them, they
are predefined, they are called `DebitTotals` / `CreditTotals` and `BalanceTotals` /
`TurnoverTotals`, and those names do not change when a checkbox does. `GetTotalsTableNameDB` and
`GetRegisterTableNameDB` take the name from the object, which buys two things at once: a name that
survives any toggling, and one that says what the table IS to whoever opens the base with a query
tool. `AccountingRegister1005_DebitTotals` reads; `AccountingRegister1005_Tt1012` does not.

⚠ **One-time rename, not a migration the engine performs.** Nothing reads the old spelling any
more, so a base applied before 2026-08-14 must be re-created.

### Both sides — and both kinds — are DECLARED always; only the MAINTENANCE follows the setting

Each register declared only the table it was currently using. Both totals objects are predefined and
**never leave the configuration**, so they are visible to both sides of every diff: declaring the
table conditionally made baseline and target disagree about an object neither could lose. Switching
the setting dropped one table; switching back produced *"alter"* rather than *"create"* — the
baseline still knew the id — so nothing re-created it, and the maintenance was built against a table
that was gone.

Both tables are now declared whatever the setting says — and, after the correction below, so is
their maintenance.

### ⭐⭐ THE SETTING IS ANSWERED BY THE ROW, so the declaration answers nothing

The first three attempts at the paragraph above all kept the setting in the SCHEMA and tried to make
the schema move safely: declare the idle side without its `Derived()`; then, since a trigger left
behind would keep writing, drop the bundle when a table stops being maintained; then, since the rows
it had already accumulated were now frozen and unreadable-as-current, drop and re-create the table
empty rather than pay `DELETE` per row on something that can hold millions.

Each step was sound about the step before it, and each produced its own failure — a side declared
without the block that also declares its resource columns (*Column unknown T.FLD…_N_CR*), a drop
that a surviving view refuses (*there are 1 dependencies*). The pattern is the tell: **when every fix
uncovers the next one, the thing being fixed is not the defect.**

The defect was keeping the answer in the declaration at all. The delta already had the exact
question, written for a different purpose — *does this row name an account on this side?*

```
{row}.fld<AccountCr>_RTRef <> 0
```

A one-sided register never fills `AccountCr`, so that guard is false for every movement it will ever
write and the credit side accumulates nothing — **by the data, not by a setting**. Turn correspondence
on and the same guard starts letting rows through. Nothing in the schema moves in either direction:
both sides are declared whole, always, with the same columns, the same key, the same triggers.

Everything the earlier attempts needed then went away with them — the early return, the
stop-maintaining transition, the drop-and-re-create, the extra barrier case. The code is smaller than
it was before the day started, which is the usual sign that the question was finally asked of the
right thing.

### ⚠ The DDL barrier has more readers than the seed writes — and a rebuild reads TWO tables

On Firebird a statement cannot see a shape its own transaction created. Two consequences, both fixed
by sending more work through the door that already existed:

| Reader | Why it is a barrier reader | Fix |
|---|---|---|
| installing the materialisation | the `CREATE TRIGGER` body names `NEW.<movement column>`, so it reads a shape this apply may have just altered | `ApplyMaterialization` goes through `RunOrDefer` like the seed writes |
| the totals rebuild | it **READS the movements and WRITES the totals** — either one being new in this transaction makes it unusable until the DDL commit | `RunOrDefer` gained a TWO-TABLE form; keying it on one of the two lost half the cases (*Table unknown …_DEBITTOTALS* on a base whose movements were long durable) |

The general shape is worth carrying: a barrier keyed on one relation is right only for work that
touches one relation. Derived state touches two by definition — that is what "derived" means.

## 4d. ✅ CLOSED 2026-08-14 — why the switches "worked every other time"

The failures recorded here the day before (a refused `DROP TABLE …_CREDITTOTALS`, deadlocks on
re-apply) were **symptoms of one root cause with four amplifiers**. All are fixed; the shape is
worth keeping because each layer HID the next one.

> ⭐⭐ **The rule everything below is a consequence of** was stated the same day and lives in
> **[schema-authority.md](schema-authority.md)**: the diff between two configurations carries ALL the
> information and matches the physical schema exactly, so the ONE way this mechanism can be wrong is
> a baseline that does not describe what is in the base. Every defect in this section is that defect.
> The same doc records why the answer is never "read the physical schema and reconcile".

### The root: a setting that builds the schema was never saved

`Correspondence` and `SplitTotals` were declared on the accounting register, edited, and used to
build the schema — and appeared in neither `ReadData` nor `WriteData`. They lived only as long as
the designer held the object in memory and came back at their DEFAULTS whenever the saved
configuration was re-read. That re-read produces the BASELINE the next apply diffs against, so:

* turning a switch OFF applied correctly, then the baseline came back saying ON;
* turning it ON again diffed ON against ON, emitted **nothing**, and left the physical table without
  the column the maintenance was about to be written for — `Column unknown T.SHARD_`;
* and it looked INTERMITTENT, because breakage depended on which way the setting happened to differ
  from the default that round.

The accumulation register serialises its own `SplitTotals` and therefore never showed any of it.
**That difference between the two registers is what named the bug** — it was reported as "the
accumulation register works, the accounting one does not", and it beat six measured hypotheses.

> RULE: a property that PARTICIPATES IN THE SCHEMA must be serialised. The check is mechanical —
> compare the `m_property*` names declared in the header against those named in the metatype's
> `ReadData` / `WriteData`. Anything missing is a setting that cannot survive a save, and therefore
> a baseline that lies. (Predefined ATTRIBUTES — Period, LineNumber, Code — are exempt: they are
> serialised as attribute objects, not as properties.)

### The amplifiers, in the order they hid each other

1. **A refused commit was not rolled back.** Firebird compiles views and triggers AT COMMIT, so a
   bad bundle is refused there — and a failed `isc_commit_transaction` leaves the transaction ALIVE.
   The driver dropped its handle regardless ("FB invalidates it either way" — it does not, only on
   success) and the layer zeroed its depth before calling the driver, so `RollBack` returned at its
   null check. Locks were held until the connection died; the next apply met that as a **deadlock**
   naming tables that had no quarrel with each other. Fixed in `ibDatabaseLayer::Commit` (roll back a
   refused commit, once, for every driver) and in the Firebird driver (take the handle value BACK
   from the API, which clears it only on success).
2. **The maintenance barrier asked about one table.** `RunOrDefer` was keyed by the SOURCE table
   because the trigger reads it — but the trigger WRITES the totals table, and re-keying the totals
   is exactly what the switches do. The install therefore landed inside the transaction that had just
   recreated the table it addressed. Now keyed by both.
3. **The key-hash fill ran outside the barrier.** `ibFillKeyHashes` went straight at the connection,
   i.e. DML against a table created in the same transaction — refused at COMMIT with `expression
   evaluation not supported`, naming nothing. Only registers whose key exceeds the index ceiling
   carry that column, which is why the accounting register showed it and the accumulation one did not.
4. **The deferred queue could not survive nesting.** `Flush` walked the queue by reference while a
   deferred item (the regeneration) deferred work of its own; the vector reallocated under the walk
   and the process died inside `std::function::operator()`. Drained in batches now.

### Two ordering rules the engine forces (both cost an apply to learn)

* **Indexes down, columns second, indexes back up.** A column an index stands on cannot be dropped
  (`column SHARD_ ... is referenced in index ..._PK`). Turning split totals OFF is exactly that
  shape. Only an ALTERed table can meet it — a table that is re-created wholesale takes its indexes
  with it, which is why the accounting register never showed this and the accumulation one did.
* **The movements table has dependents too.** Triggers and the view hang on the SOURCE and name its
  columns, so dropping a dimension is refused (`there are 4 dependencies` — three triggers and a
  view) unless the maintenance comes down first. Ask the BASELINE which tables are maintained, not
  the target: a register that switches its totals kind no longer calls the old table derived, and the
  objects that actually hold the columns were declared by the old schema.

### Reporting is not free either

`ApplyMaterialization` announced `Rebuild totals maintenance` for **every** register on **every**
apply — the intent to report only real changes was written in the comment and never implemented. So
adding an attribute, which no totals table carries, printed a wall of rebuild lines and buried the
one line that mattered. It now compares the rendered bundles (`ibMaterializationEquivalent`, the same
test the installer uses to decide it has nothing to do) so "unchanged" has ONE definition.

### ⭐⭐ Follow-up, later the same day — WHEN the active configuration is published, and WHOSE connection the apply runs on

Two more defects of the same family as the root: something declared the apply finished before it
was, and something ran the apply on a connection nobody had agreed on.

**1. The ACTIVE configuration is published LAST.** The `config` table used to be written in
`ibMetaDataConfigurationStorage::OnSaveDatabase` (`metadataConfigurationQuery.cpp`), in the same
transaction as the DDL. It is now written in `OnAfterSaveDatabase`, after the restructuring
transaction has committed and the deferred phase has drained, sharing its transaction with the
baseline re-read (`ibDelete(config_table)` + `ibInsertSelect(config_table, …, ibScan(config_save_table))`,
then `LoadDatabase` / `RunDatabase`).

`config_save` still travels with the DDL, and must: it holds the edited configuration, and that pair
is atomic. `config` is a **different thing** — it is the ACTIVE configuration, the one a re-read
produces the BASELINE from. Publishing it beside the DDL declared the schema finished while a phase
of it had not run: on Firebird the maintenance (triggers, views) and the seeds cannot be built in
the same transaction as the tables they address, so they are deferred past that commit. If the
deferred phase then failed, the active configuration was already durable and **ahead of** the
physical schema — the differ compared the new configuration against itself, emitted nothing, and the
missing objects could never be built again. That is not recoverable by diffing harder: a differ over
two configurations is right to stay silent when they agree. It is the same failure §4d opens with,
reached by a different road — there the baseline lied because a property was never saved, here
because a correct baseline was published too early.

With publication last, a failure anywhere before it raises and never reaches that line, leaving
`config` at the PREVIOUS configuration — which is what makes a failed apply **repeatable**: the next
diff sees real work to do. Publication and the re-read share one transaction deliberately (both land
or neither does), and that transaction has one abort path, a `TxGuard` whose destructor rolls back if
the load or the module compile raises — an exception walking out of there otherwise leaves the
transaction ACTIVE, holding locks on the config tables until the connection dies, and the next apply
meets that as a deadlock naming `sys_config`.

**2. The restructuring runs on the SESSION's holder.** `ibStructureBuilder::Conn()` and
`OnBeforeSave` now take `ibSession::Current()->Holder()` (`query/structureBuilder.cpp`); `db_query`
stays as the fallback for callers that have no session at all (codeRunner, tests). `SetHolder`
existed and was **called from nowhere**, so the apply ran over `db_query` while the batches, the
per-save barrier and the deferred drain used the holder they were handed — two owners of one
process. The trace says exactly that: the restructuring commit arriving at **depth 2** (nested inside
somebody else's transaction, so it only decremented) and the deferred drain running at `active=1` —
inside a transaction that had not committed yet, so `CREATE TRIGGER` could not see the table the same
apply had just created (`Table unknown`).

A holder owns a connection and pins it: while its transaction is open nobody else can take that
connection, and everyone who asks later sees the committed result. The designer's session IS the DDL
session, so there was nothing to invent — only something to ask for. The pool-side half of the same
day's fix (a connection with an open transaction is no longer servable, and a released one is rolled
back before parking) is in [connection-pool.md](connection-pool.md).

---

## 5. Order of work — cheapest first

Each step is separable and none of them requires the next.

| # | Step | Risk | Note |
|---|---|---|---|
| ~~1~~ | ✅ Fix the `Active` guard (§4) — **done 2026-08-13** | none | one token; makes an accumulation bundle applicable on Firebird again |
| ~~2~~ | ✅ `FindAttributeByColumnId` on `ibValueMetaObjectRegisterData` (§2.6), both copies call it — **done 2026-08-13** | low | pure addition; the accumulation field tree gains recorder / line number as attributes, which is a **behaviour change** and belongs in its own commit |
| ~~3~~ | ✅ Move the descriptor helpers out of `accumulationRegister.h` into `registerQueryLowering.h` (§2.5) and call them from the other two — **done 2026-08-13** | low | mechanical; the periodicity choice list and the *(period, condition)* pair stop having four spellings. `ibRegArg` (the accounting register's `ArgAt`, twice, under another name) went with them |
| ~~4~~ | ✅ **BOTH HALVES DONE 2026-08-13** — `ibRequireOpenBase()` for the family (§2.4), and the three `SelectionTo*` copies are now one `ibRegSelectionToTable` over `shape->GetColumns()` | medium | the check the note asked for changed the shape of the step: a column had to learn a CAPTION first, or the collapse would have traded the accumulation register's presentations for names. See §2.4 |
| ~~5~~ | ✅ **DONE 2026-08-13** — the derived-surface builder is `ibRegSurfaceCache::Obtain` (§2.1): cache, signature check, retirement and the id band in one place, with the columns supplied by the caller as a lambda | medium | the cache key stayed the caller's (a view name for A and B, the whole call for C) and so did the RELATION's name, which is a third thing again — C reads `<table>_<shape>`, not the table |
| ~~6~~ | ✅ **DONE 2026-08-13, and NOT as one procedure** (§2.2): the four pieces that are word-for-word the same moved out — `ibRegSplitIntoKey`, `ibRegGuardInForce`, `ibRegAccumulatorColumn`, `ibRegSelfSourceFromDeclaration`. The KEY, the accumulate expressions and the views stayed with each register | medium | a single "declare the bundle" would have taken the union of two signatures and branched inside on which caller it had — one function serving two callers separately, written as if it served them together. The output is a schema diff, and a wrong one is a `DROP` |
| 7 | Give the accounting readings `GetSourceRelation`, then collapse the two arm-cut forms into one (§3, last row) | high | it is a new capability, not a refactor: the numbers move from RAM to the server. [accounting-register-arc.md § 8.3](accounting-register-arc.md) states the same step from the other end |

**A behaviour change does not travel inside a refactoring commit.** Steps 2, 4 and 7 each alter what a
caller sees — which columns a field tree offers, which columns a runtime table carries, where the
aggregation runs. A diff labelled "extract the shared builder" that also moves a number is a diff
nobody can review: when the number turns out to be wrong, the extraction and the change are one
commit and the bisect stops at both. Land the extraction with the answers unchanged, then change the
answer on its own.

And the reason to do any of it: every item in §2 compiles today. A copy does not fail when it drifts —
it goes on answering, in its own way. §4 is one such drift, found by reading the two sites side by
side, and §2.6 is a second.
