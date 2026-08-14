# Metaobject naming — the user-visible taxonomy

> **Status: PLAN — nothing applied,** except § 3.2, which records a label family already in the
> product. Proposal for review.
> Scope: the names a person **sees in the designer tree** and **writes in a script** — the
> metaobject kinds, their inner parts, and the order they appear in. File/folder naming is a
> separate plan ([naming-plan.md](naming-plan.md)); this one is about the vocabulary the product
> speaks to its user.

---

## 1. The criterion

The audience is a bookkeeper or an application developer who has spent years in a comparable
platform. Two things must be true at once when they open the designer:

1. **First glance: this is not the thing I used.** Otherwise it reads as a clone, and a clone is
   judged on feature parity — the axis where a copy wins and a platform loses.
2. **Second glance: I already know my way around.** Otherwise every rename is a tax on the one
   asset that audience brings — fifteen years of muscle memory.

Those pull against each other, so the rule that resolves them:

> **Rename what the domain expert does not think in. Keep what they do.**

A rename that only *differentiates* is jargon churn: it costs recognition and buys a signal. A
rename that *clarifies* pays for itself, and the "this is different" effect arrives as a
**by-product**. If a proposed rename cannot be justified without mentioning any other product, it
does not belong on this list.

---

## 2. What stays — and why it is an asset, not inertia

Three tiers, all of them load-bearing for credibility. None of these is a competitor's invention.

### 2.1 Legislative terms

Named in law. Using them is not imitation — it is speaking the language the accounting profession
is legally required to speak.

| Term | Source |
|---|---|
| **Регистр бухгалтерского учёта** | UA [996-XIV art. 9](https://zakon.rada.gov.ua/go/996-14) · RU 402-ФЗ art. 10 |
| **Реквизит** (обов'язкові реквізити первинного документа) | UA 996-XIV art. 9 §2 |
| **Первичный документ** | UA 996-XIV art. 9 |
| **Синтетический и аналитический учёт**, **двойная запись** | UA 996-XIV art. 9 §1 |
| **План счетов** | RU order 94н; UA НП(С)БО |

### 2.2 Professional canon

Five centuries old (Pacioli, Venice 1494 — `conto`, `giornale`, `libro maestro`), reached Russian
bookkeeping through German (`Konto`, `Buchhalter` → бухгалтер). Owned by nobody.

Справочник · Документ · Журнал документов · Константа · Перечисление · Отчёт · дебет/кредит ·
сальдо · оборот · оборотно-сальдовая ведомость.

### 2.3 Honestly descriptive

Not legislative — these are **our storage taxonomy**, and should be presented as such, never as
legal terms. They survive the criterion because the name says what is inside.

- **Регистр накопления** — it accumulates: приход / расход / остаток.
- **Регистр сведений** — the weakest name on this list ("сведения" is anything), but every
  honest replacement is either equally general or reads like programmer vocabulary to the person
  who has to use it. Not worth the recognition it would cost.

---

## 3. What changes

| Now | Proposed | Why it clarifies | Script-visible? |
|---|---|---|---|
| Субконто / `Subconto1..3` | **Разрез аналитики** / `AccountDimension1..3` | The word means nothing outside one vendor's product; underneath it is the legal *аналитический учёт*. | **yes** — breaking |
| ~~План видов характеристик / `ChartOfCharacteristicTypes`~~ | **stays** — decided 2026-08-06 | Twice revised, twice toward keeping it: a bare `DimensionTypes` breaks the **`План *` family** (план счетов, план видов …, план обмена — a naming system, not a coincidence), and `ChartOfDimensionTypes` keeps the family but still spends recognition to restate what the tree already teaches on the first day. It stays as it is. | — |
| ~~Ярлык дерева `Tables`~~ | **stays** — decided 2026-08-06 | The term `TabularSection` stays, and so does the label. Note for whenever this is revisited: the Russian for it is **«табличные части»**, never «табличные секции» — a transliteration of the English is a worse name than the mismatch it would fix. | — |
| Sub-conto kinds tables | **Таблицы разрезов** / `Dimension tables` | tail of the first row | no |
| Обработка / `DataProcessor` | **Инструмент** / `Tool` — ⏸️ **UNDECIDED** | `DataProcessor` names the *mechanism* (so does a report, so does a document when posted), and `Tool` names the purpose, symmetrically beside `Report`. Held 2026-08-06: *обработка* is defensible — it does process data — and the objection to `Tool` is that it is **general to the point of saying little**. This is the one rename on the list that trades an honest-but-broad name for another broad name, so it is the weakest case here. If it is not clearly better, § 1 says leave it. | **yes** — breaking |
| `dataReport.*` / `dataProcessor.*` | `report.*` / `tool.*` | The `data` prefix is on exactly these two of eleven metatypes — `catalog.*`, `document.*`, `constant.*` carry none. It is an accident, not a family convention, and `Data` distinguishes nothing (a catalog is data too). Symmetry by **subtraction**. | no — file names |

Already applied: **Конфигуратор → Дизайнер**, **Подсистема → Раздел / `Section`**.

### 3.1 Rejected candidates, so they stop coming up

- `Instrument` for Обработка — direct cognate of «инструмент», but *financial instruments* are
  securities. In an accounting product this misreads on sight.
- `Utility` — reads as system maintenance ("Utilities: bank statement import" is wrong).
- `Processor` / `Action` / `Command` / `Operation` / `Task` / `Service` — all taken: `ibProcUnit`,
  the command subsystem, *хозяйственная операция*, scheduled jobs, background services.
- `DataReports` / `DataTools` — symmetric with each other, asymmetric with the other nine
  metatypes. See the last row above.
- **`Table` for Табличная часть — rejected 2026-08-06, and it was on this list as a proposal
  until then.** Two reasons, either one sufficient. **The word is taken:**
  `VALUE_TYPE_REGISTER(ibValueModelTable, "Table", …)` — a table of values, a type a script
  creates on its own. Registering the metatype under the same word would put a standalone thing
  and a dependent one behind one name, the exact ambiguity this document avoids elsewhere.
  **And the adjective is the meaning, not decoration:** it is tabular, but it is a *part* — it
  cannot exist without its owner, and "table" is precisely the word that drops that. The earlier
  argument ("the reader already knows it belongs to an object") had it backwards: the name is
  what carries the dependency once the reader is somewhere else — a signature, an error message,
  a document written by someone who is not looking at the tree.
- Renaming **Регистр накопления / сведений** — descriptive names that work; renaming them
  differentiates without clarifying, which is the thing this document exists to prevent.

### 3.2 The hierarchy kinds — four labels, one family (in the product, not a plan)

Not a rename — a vocabulary that arrived with a new declaration, and it belongs on this list because
the words are the user's. A catalog / chart states WHAT A PARENT MAY BE through the `HierarchyType`
property; its four values are user-visible text
(`ibValueEnumHierarchyType::CreateEnumeration`, `metaCollection/partial/commonObjectEnum.h`).

| Value | Label | Means |
|---|---|---|
| `None` | No subordination | a flat list — no parent field at all |
| `Subordination` | Subordination without hierarchy | a parent that is ordinary DATA; nothing drills, nothing folds |
| `Items` | Hierarchy of items | every element may hold elements |
| `FoldersAndItems` | Hierarchy of folders and items | items live inside folders |

Two decisions worth keeping written down.

- **The four are ONE FAMILY** — each names the arrangement and nothing else. A parenthetical
  explainer (`"subordination (no hierarchy)"` was the alternative) is rejected: an explainer in one
  label makes the other three look like they are missing theirs.
- **The property gets a CATEGORY of its own**, `Hierarchy`, instead of a line under Common. It is
  the single declaration that decides whether the object has a tree, what a parent may be, and
  whether `Parent` and `IsFolder` exist as columns — it governs an area, so it is shown as one, and
  settings on the same subject have somewhere to land next to it.

Against § 1's criterion these are new names rather than renames, and they pass on the same test: each
says what the arrangement IS, so the label teaches instead of differentiating. The engine side
(predicates, stored numbers, what each kind does to `Parent` and `IsFolder`) is in
[metadata-lifecycle.md](metadata-lifecycle.md) § 2a.

⚠ **Untranslated.** `Hierarchy type` and all four labels are absent from `locale/ru.po` and
`locale/uk.po` as of 2026-08-13 — the designer shows them in English. The category label
`Hierarchy` is already there from another context.

---

## 4. Tree order

The current order is inherited and scatters roles: enumerations (reference data) sit after
documents, reports and tools sit in the middle, and nothing tells the reader how the system is
put together.

A principled order groups by **role**, and reads top-down as *how a configuration works*:

| Rank | Group | Role |
|---|---|---|
| 100 | Constants | what the system knows |
| 110 | Catalogs | " |
| 120 | Enumerations | " *(moved up — it is reference data)* |
| 130 | Documents | what happens |
| 140 | Information registers | where it lands |
| 150 | Accumulation registers | " |
| 160 | Dimension types | accounting setup |
| 170 | Charts of accounts | " |
| 180 | Accounting registers | " |
| 190 | Reports | what the user runs |
| 200 | Tools | " |

Two constraints this must not break:

- **Charts of accounts and Dimension types precede Accounting registers** — they configure it.
  (True today as well; keep it true.)
- Order is a **navigation aid**. Reordering for the sake of looking different fails the criterion
  in § 1. This ordering is proposed because it explains the system, and looks different as a
  by-product.

The ranks live in one table: `ibMetaDiffWalker` group ranks, `metaCollection/metaDiff.cpp`.

⚠ **They do not** (corrected 2026-08-14). That table is one of **three** copies of the order — the
Designer's `s_groups`, the compare walker's ranks, and Enterprise's "All operations" dialog — and the
half of this plan that landed had to be applied to all three by hand: **charts before registers, and
the registers last**, because a register is expressed in terms of what stands above it. The sites and
the reason are in [metadata-tree.md § 3.2](metadata-tree.md); the rest of the order above (moving
enumerations up, reports and tools to the end) is still a plan, and doing it means editing three
files or moving the rank onto the metatype first.

---

## 5. What this buys — and what it does not

**Buys.** Four visible differences at the top of the tree (Дизайнер, Разделы, Виды аналитики,
Инструменты) over a completely untouched depth (Документы, Справочники, План счетов, Регистры).
First glance says "different", second says "I know this". Plus one real clarity gain — the two
worst names in the taxonomy (субконто, план видов характеристик) are replaced by names that
describe their contents, and a live code/UI inconsistency (`Table` vs `TabularSection`) is closed.

**Does not buy.** Differentiation is not positioning. The naming makes the product legible on the
first screen; it does not make the case for it. That case is made by the change loop — the buyer
adding an attribute themselves and watching it appear everywhere — not by vocabulary.

---

## 6. Cost and sequencing

The cost axis is one question: **is the name visible from a script?**

- **Free** — kind labels and file names: Таблица, Таблицы разрезов, `report.*` / `tool.*`. Pure
  source and UI churn.
- **Breaking** — metaobject names a script writes: `Subconto1`, `ChartOfCharacteristicTypes`,
  `DataProcessor`. These change what existing configurations and code compile against.

The breaking half costs **almost nothing today** (no shipped configurations) and gets more
expensive with every configuration written — the same trap that now prevents the incumbent from
dropping its own coinages. If this plan is accepted, the breaking half should be done first, not
last.

**Sequencing.** File renames touch `backend.vcxproj`, which the command-interface arc is editing.
Do the file-level part in one pass after that arc converges, together with the
`Subconto` → `AccountDimension` rename, rather than across it.

---

## 7. Execution plan (measured 2026-08-06)

### 7.1 The one fact that makes this cheap

A metatype's identity is its **clsid**, and the clsid comes from an opaque key, never from the
registered name:

```cpp
constexpr ibClassID g_metaDataProcessorCLSID = metadata_to_clsid("MD_DPR");   // identity
METADATA_TYPE_REGISTER(ibValueMetaObjectDataProcessor, "DataProcessor", g_metaDataProcessorCLSID);
//                                                      ^ the name — free to change
```

So a rename does **not** touch serialised configurations, the database, or any persisted blob.
What it does touch is everything that spells the name out: **module texts** written against it,
and the places the platform itself types it as a string. That is the whole cost, and it is why
this gets more expensive with every configuration written and never cheaper.

### 7.2 The five layers a rename passes through

| # | Layer | Where | Breaks what |
|---|---|---|---|
| 1 | **Registry name** | `METADATA_TYPE_REGISTER(..., "<name>", clsid)` | metadata lookups by name |
| 2 | **Script collection** | `moduleManager/globalContextManager.cpp` — `AppendProp` + the `en*` enum + the ctor switch | **module texts** — the breaking layer |
| 3 | **Designer label** | `designer/mainFrame/metaTree/treeConfiguration_impl.cpp` `#define`s, plus `locale/ru.po` + `locale/uk.po` | nothing — display only |
| 4 | **C++ symbols** | class names, `g_meta*CLSID` constant names | nothing — pure source churn |
| 5 | **File names** | `metaCollection/partial/*`, `backend.vcxproj` + `.filters` | build if the project files miss a file |

Layers 1-3 are the rename. Layers 4-5 are hygiene that keeps the code readable afterwards, and
they are what makes the diff large — they can lag behind, but not by much, or the code stops
matching the product.

### 7.3 Volume, per rename

| Rename | Status (2026-08-06) | Files | Occurrences |
|---|---|---|---|
| `Subconto` → `AccountDimension` | ✅ **accepted — the only one** | 11 | 253 |
| `ChartOfCharacteristicTypes` | ❌ stays | — | — |
| `TabularSection` / tree label | ❌ stays | — | — |
| `DataProcessor` → `Tool` | ⏸️ held — probably stays | — | — |

**The plan survived one item out of four, and that is the plan working, not failing.** Each of the
three that fell was measured against § 1 — *rename what the domain expert does not think in, keep
what they do* — and each turned out to be on the "keep" side: a family of names (`План *`), a word
whose adjective carries the meaning (табличная **часть**), and a broad name that would have been
traded for another broad name. What is left is the one term that means nothing outside a single
vendor's product, which is exactly the shape a rename is supposed to have.

### 7.4 Order — smallest blast radius first

Each step is its own commit, builds clean, and keeps the suite green before the next begins.

| Step | What | Why here |
|---|---|---|
| **1** | `Subconto` → `AccountDimension` | 11 files, 253 occurrences. Script-breaking (`Subconto1..3` are written in module texts), free of database consequences (§ 7.1). |
| **2** | Tree order (§ 4) | One table of ranks in `metaCollection/metaDiff.cpp`. Independent — can go first, or alone if step 1 slips. |
| — | `dataReport.*` / `dataProcessor.*` → `report.*` / `tool.*` | **Unscheduled**: these file renames only make sense alongside the type rename, which is held. |

⚠️ With the chart of characteristic types keeping its name, the mechanism is described by two
vocabularies: *виды характеристик* in the catalogue, *разрез аналитики* on the account. That is
survivable — a bookkeeper does say "аналитика по счёту" and "виды характеристик" — but it means the
link between them now lives in the documentation and the UI rather than in a shared word. Worth
knowing before step 1, not a reason to stop it.

### 7.5 Verification after each step

1. `msbuild enterprise.sln /p:Configuration=Debug /p:Platform=x86 /m` — green.
2. The suite (currently 1069) — green.
3. **Open a configuration in the designer and run Syntax control on a module that uses the renamed
   name.** The compiler resolves global-context names at compile time, so a missed layer-2 site
   shows up here and nowhere else — a stale name simply reports "Var is not found".
4. Grep for the old name: zero hits outside `docs/` and this file's history.

### 7.6 Open decisions — needed before step 1

| # | Question | Why it blocks |
|---|---|---|
| 1 | `Subconto1..3` are **numbered** attributes — does the number stay part of the name (`AccountDimension1..3`), or does the rename also collapse them into one collection? | it is the shape of the name, and changing it later is a second breaking rename |
| 2 | Do the Russian/Ukrainian labels ship in the same commit as the rename, or follow? | `locale/*.po` are translated by hand; a rename with stale translations shows the old word in the tree |

### 7.7 What this plan deliberately excludes

**Already-shipped module texts.** There are none — no configuration has shipped, which is exactly
why this is scheduled now. If that changes before the plan runs, add a step: "rename with reference
update" over module texts (§ 6 names it as needed alongside type annotations), because after the
first outside configuration exists, a rename stops being a source edit and becomes a conversation
with someone who owes us nothing.
