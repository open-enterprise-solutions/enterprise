# Metaobject naming — the user-visible taxonomy

> **Status: PLAN — nothing applied.** Proposal for review.
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
| План видов характеристик / `ChartOfCharacteristicTypes` | **Виды аналитики** / `DimensionTypes` | The least readable name in the taxonomy. Nobody thinks in it — people say "where do I add a subconto". It is the catalogue of what may serve as a dimension; the new name says that, and pairs with `AccountDimension` so the two read as one mechanism. | **yes** — breaking |
| Табличная часть / `TabularSection` | **Таблица** / `Table` | The tree **already** shows `Tables` (`g_metaTableCLSID`) while the code says `TabularSection` — the inconsistency exists today. "Part of an object" needs no adjective: a tabular section cannot exist outside its owner, appears only under it in the tree, and is always written through it (`Документ.Товары`). The adjective charges syllables for information the reader already has. | no — kind label |
| Sub-conto kinds tables | **Таблицы разрезов** / `Dimension tables` | tail of the first row | no |
| Обработка / `DataProcessor` | **Инструмент** / `Tool` | `DataProcessor` names the *mechanism* (so does a report, so does a document when posted). The metaobject is *a thing the user runs to get something done*. `Tool` names the purpose, and sits symmetrically beside `Report`. | **yes** — breaking |
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
- Renaming **Регистр накопления / сведений** — descriptive names that work; renaming them
  differentiates without clarifying, which is the thing this document exists to prevent.

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
