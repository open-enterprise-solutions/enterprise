# Accounting register — inventory and the decisions taken

> **Status:** **BUILT 2026-08-13** — read path, write path, account rules and the trigger-maintained
> totals bundle are in the tree, build under `Debug|x86`, and the bundle has been applied to a live
> Firebird 5 base (§ 5f). What the register does today is § 5a / § 5b / § 5c / § 5d / § 5e / § 5f /
> § 5g; what is still absent is § 8, re-checked against the tree on 2026-08-13.
> `tests/test_accountingTotals.cpp` (**14** since 2026-08-14) covers the totals declaration — the
> guards, the sides, the stored grain — plus the cases the accumulation register's parity test earned
> its keep on and this one had none of: a REVERSAL lowers its own side rather than raising the other,
> a delete and an amount edit run BOTH arms of the accumulate, a backdated entry lands in its own day,
> and a fraction survives into the stored figure. Two of the fourteen read the SOURCES rather than run
> them, because the guard-shape defect they pin is a Firebird refusal that SQLite accepts — no live
> check on a SQLite fixture could ever see it. `tests/test_totalsKeyHash.cpp` (13) covers the key
> digest both registers share. The five READINGS are still uncovered, and so is the parity of the two
> ROADS a reading may take (§ 8.3a) — that one has a working precedent to copy in
> `tests/test_totalsNumericParity.cpp`.
>
> ⚠ **§§ 1–3 below are the INVENTORY of 2026-08-10** — the state *before* the work, kept because the
> decisions in § 4 only make sense against it. Read them as history, not as current state; each is
> marked where it has since been answered. The decisions themselves (§ 4) are the owner's and are
> recorded verbatim in intent, not paraphrased into something softer.
>
> Companion docs: [register-totals-strategy.md](register-totals-strategy.md) (the totals
> machinery this register is the reason for), [data-composer.md](data-composer.md) (L5),
> [query-engine-layers.md](query-engine-layers.md) (the L1–L5 floor plan),
> [register-shared-machinery.md](register-shared-machinery.md) (what this register and the
> accumulation one now share, and what is still written twice).

---

## 0. Why this document exists

The accounting stack — `ChartOfAccounts`, `ChartOfCharacteristicTypes`, `AccountingRegister` —
was written earlier by another author and **was never carried to the end**. It then stood still
while the neighbouring registers (accumulation, information) migrated onto the L3 door, so today
it is the last subsystem living in the previous era of the engine. That is the whole diagnosis:
not "half-built", but **built to a different generation of the surrounding code**.

Read the sections below as: what is genuinely finished (most of it), what is wired to the wrong
end, and what is missing outright.

> That diagnosis was written on 2026-08-10 and was answered on 2026-08-12/13: the register was not
> repaired in place but rebuilt on the door its neighbours already stand on (§ 5a, § 5e), so it no
> longer composes SQL as strings and is no longer the raw-L1 holdout its own comment called it.

---

## 1. What is finished

> Inventory of **2026-08-10**. The names are the ones the tree carried then: `Subconto*` became
> `AccountDimension*` on 2026-08-12 (§ 5a), and `MaxSubcontoCount` moved from an attribute on every
> account to a property of the chart-of-accounts metaobject.

| Layer | Evidence |
|---|---|
| Register metaobject — the full posting-line attribute set (Period, Recorder, LineNumber, Active, RecordType, Account, Subconto1-3), dimensions, resources, both modules | `metaCollection/partial/accountingRegister.h` |
| Chart of accounts — hierarchical mutable-ref catalog + AccountType, OffBalance, Quantitative, Currency, MaxSubcontoCount, the `SubcontoKinds` predefined table, the ChartOfCharacteristicTypes binding | `metaCollection/partial/chartOfAccounts.h` |
| Chart of characteristic types (**CCT** below) — `TypesOfCharacteristics` on the metaobject (what values a characteristic may hold) and a `Type` attribute on each element (the per-kind narrowing) | `metaCollection/partial/chartOfCharacteristicTypes.h` |
| Writing movements | inherited `WriteRecordSet` / `DeleteRecordSet` — the same template method every register uses, with key locking |
| The movements table AS A QUERY SOURCE | `ibValueMetaObjectRegisterData` is an `ibBackendQueryableHolder` (`commonObject.h`) — the accounting register inherits it exactly like its siblings |
| Script surface, designer tree, config compare, forms, icons, menus | `AccountingRegisters.<Name>` in the global context; `metaDiff`, `_res.cpp`, `MetadataMenu.cpp` |

**Consequence worth stating plainly:** a trial balance can be produced TODAY with an ordinary
query — `SUM(CASE WHEN RecordType = Debit …)` grouped by account — because the movements table is
already queryable. None of the prepared aggregates below is needed for that.

---

## 2. Wired to the wrong end — the defect

> **Fixed 2026-08-12** (§ 5a), and **restated 2026-08-16**: a value slot no longer copies the
> composition — it is declared as the CHARACTERISTIC itself
> (`GetTypeCtor(pvh, ibCtorObjectMetaType_Characteristic)`, `accountingRegisterMetadata.cpp`), the
> mirror of the kind slot, which is declared as the reference. One name instead of a copy: what the
> characteristic expands to stays the chart's business, so a chart that gains a type does not leave
> every slot describing the old set. The expansion happens where a real type is needed — physical
> layout, pickers, dot-walk branches (§ 5k). The second level — the per-kind narrowing — is applied
> on assignment (§ 4.4). Kept below because it is the reason the slot became a *(kind, value)* PAIR.

The chain "which values may a dimension slot hold" is resolved in
`accountingRegisterMetadata.cpp` (`OnAfterRunMetaObject`), and it resolves to the wrong thing:

| Step | What happens | Correct? |
|---|---|---|
| Account field ← chart of accounts reference | `SetDefaultMetaType(typeDesc)` over the bound charts | ✅ |
| `SubcontoKind` column of the account's table ← **reference to the ChartOfCharacteristicTypes** | `chartOfAccountsMetadata.cpp` — a kind IS an element of the CCT | ✅ |
| `Subconto1/2/3` (the VALUE slots) ← **also a reference to the ChartOfCharacteristicTypes** | `accountingRegisterMetadata.cpp` | ❌ |

So the field a posting writes its **value** into is typed as the field that names the **kind**:
one may store "Contractors" (the kind) where "OOO Romashka" (the value) belongs. The correct type
is the union of `TypesOfCharacteristics` of the bound charts — and the accessor for it already
exists one call away (`ibValueMetaObjectChartOfCharacteristicTypes::GetTypeDesc()`), so the fix
replaces a loop that builds a Reference type with a read of the type that is already there.

**The second level of the same chain does not exist at all.** Once a kind is chosen in the
account's table, a movement row should offer only what THAT kind admits (`Type` on the CCT
element). Nothing reads it: `GetSubcontoKind()` has exactly one caller, the column-type setup.

---

## 3. Missing outright

> As of **2026-08-10**. Most rows are answered by § 5e (2026-08-13): the five virtual tables are
> registered as query sources, each with a RAM `Compute*`; the totals bundle is declared; the balance
> is computed on both sides and folded by the account's own type; correspondence is a register
> property. The hierarchy row is answered on **both** halves since the evening of 2026-08-13 — the
> filter brings the subtree in, and the subtree's rows are then folded into the account that was
> NAMED, so a parent's rolled-up row is produced (§ 5e, "The hierarchy answers"); the language gained
> the word for asking it — `IN HIERARCHY` — the same day, and the walk moved out of this register with
> it (§ 8.5). **Still missing outright:** sequences and period close.
> Tests are no longer absent — `tests/test_accountingTotals.cpp` covers the totals declaration — but
> nothing covers the five readings.

| Missing | What the neighbours have |
|---|---|
| Virtual tables as QUERY SOURCES (`<Register>.Balance` / `.Turnovers`) | `ibAccumRegisterBalanceDescriptor` / `TurnoverDescriptor` / `BalanceAndTurnoverDescriptor` + companion queryables (`accumulationRegister.h`) |
| RAM compute (`ComputeBalance` and family) | present in BOTH accumulation and information registers |
| Totals declaration | "The accounting register does not declare totals yet" — [register-totals-strategy.md § 8](register-totals-strategy.md) |
| Account hierarchy IN AGGREGATES (a parent's balance rolled up from its children) | the hierarchy itself is NOT missing — "subordinate to account" IS the ordinary parent, the chart of accounts derives `ibValueMetaObjectRecordDataHierarchyMutableRef`. What is missing is rolling an aggregate up along it — and even that has its primitive: `ibQueryDimUnfold` (`Elements` / `Hierarchy` / `HierarchyOnly`) is exactly the unfold L5 already applies to grouping |
| Unfolded balance by account type (active / passive / active-passive) | the dead SQL computes a SIGNED, folded balance only |
| Correspondence (debit and credit on ONE line) | the model is one-sided: `RecordType` + one `Account`; a posting is two rows under one recorder |
| Sequences, period close, tests, a doc of its own | — |

### 3a. The read path is built but switched off

> **Gone 2026-08-13** (§ 5e): the file is ~140 lines of thin wrappers over the companion queryables,
> and all four blocks are deleted rather than re-enabled — for the reason stated right below.

`accountingRegisterManager_impl.cpp` is 1039 lines. **About 870 are alive and compose SQL**;
four `#if 0` blocks — `Balance` 207-242, `Turnovers` 451-486, `DrCrTurnovers` 608-649,
`BalanceAndTurnovers` 988-1036 — remove only the execution (prepare + bind + run + read),
so every method returns an EMPTY table while reporting nothing. The comment names the reason:
*"pending the register's migration to the L3 write/read door — accounting is the last raw-L1
holdout"*.

**Do not simply re-enable them.** Raw L1 bypasses the access policy, the dialect layer and
paging, and on PostgreSQL it meets a live hazard of its own (binary result mode returns empty —
`postgresResultSet.cpp`). Enabling a dead path opens territory rather than closing it.

---

## 4. Decisions taken (2026-08-10)

### 4.1 Naming — `Subconto` is out

*Subconto* is a 1C calque. In the world this concept is **financial dimensions** (Dynamics 365),
**accounting dimensions** (ERPNext), **analytic accounts** (Odoo), **segments** (NetSuite/Sage).
None of them is quite it: everywhere else the breakdown is fixed by the schema, while here the
set is declared by the ACCOUNT.

| Concept | Code | UI (ru) |
|---|---|---|
| Register dimension — fixed by the schema | `Dimension` | «Измерение» |
| The dimension a chart of accounts declares | `AccountDimension` | «Аналитика» |
| Its KIND (a reference to a CCT element) | `AccountDimensionKind`, table `AccountDimensionKinds` | «Вид аналитики» |
| Its VALUE (the composite slot in a movement) | `AccountDimension1..N` | «Аналитика 1..N» |

Two different words in the interface for two different things — today both read almost the same,
which is a large part of why the mechanism looks harder than it is. The name in code states the
mechanism (a dimension declared by the account); the caption states how accountants speak
(«аналитика»). The CLSID key string (`MD_SKTB`) stays as it is — it is an opaque body key, not a
name.

⚠ The attribute name is what serialisation keys on (`node.GetProperty(prop->GetName())`), so the
rename changes the key in stored configurations. There are no third-party configurations yet;
this is exactly the window that closes once there are.

### 4.1a What a chart of characteristic types IS — EAV, with the type kept

Worth naming plainly, because everything else in this section follows from it: a chart of
characteristic types is the platform's **EAV** (Entity–Attribute–Value) mechanism.

**And EAV is not anybody's invention of the 1990s business-software world.** It is a model that has
been described and re-described for decades — clinical data repositories used it in the late 1970s
(TMR, HELP), and it was written up as "EAV with Classes and Relationships" long before any of the
platforms that now ship it. The same shape appears under other names wherever a domain has many
sparse properties: Magento and Drupal call it EAV outright, SAP calls it classification
characteristics, PIM systems call it attributes and attribute sets, plain SQL practice calls it a
properties table. A chart of characteristic types is one more spelling of it, so nothing here is
borrowed thinking — it is the standard answer to a standard problem.

What IS particular is the packaging: the model is given METADATA and a TYPE. The attribute is a
metaobject rather than a row of names, and it carries its own `Type`, which is exactly the property
the textbook version loses. That is why everything below can be checked by the engine instead of
being an application convention.

| EAV | here |
|---|---|
| Entity | the object the property is about (an account, an item, a document) |
| **Attribute** | an ELEMENT of the chart — a kind of characteristic («вид характеристики») |
| **Value** | a value whose admissible types the chart declares, narrowed per kind by the element's own `Type` |

What the platform adds to the textbook shape is the one thing textbook EAV loses: **the value stays
typed**. In a typical implementation the value column is a string (or a `value_str` / `value_num` /
`value_date` triple) and only application code knows how to read it back. Here the type is not known
to the code, it is DECLARED in the data — the chart names the contour, the kind names its own `Type`,
and the engine both narrows on write (§ 4.4) and lays the column out from it (§ 5k).

Two shapes of the same model live in the tree, and the choice between them is an applied one:

- **the long table** — an information register keyed by (object, kind) with the value as a resource.
  Any number of properties, added as DATA, no schema change. The classic cost: a row per property, a
  join per read, nothing worth indexing by value;
- **the unfolded one** — the accounting register's N `(kind, value)` slot pairs in a wide movement
  row, and the chart of accounts' unfolded kind columns (§ 5k). The same model materialised into
  columns: no join on read, the kind sits beside its value — at the price of a CEILING
  (`MaxAccountDimensionCount`), which is why that number is schema and lives on the chart.

This is also why a movement stores the kind BESIDE the value rather than deducing it from the
account's table by position: in EAV the attribute is part of the record, or the record stops being
self-describing and its meaning starts depending on the current state of a reference book.

### 4.2 The number of dimension slots is SCHEMA — the chart of ACCOUNTS declares it

Three slots is a hardcode of the CURRENT IMPLEMENTATION — three fixed members in the class — not
a property of accounting anywhere. Systems that do this well declare the maximum on the chart of
accounts, and so should this one. Two different sources answer two different questions, and
neither answers the other's:

| Question | Answered by | Nature |
|---|---|---|
| WHICH VALUES may a dimension hold | chart of characteristic types — `TypesOfCharacteristics`, narrowed per kind by the element's `Type` | the CONTOUR. It governs nothing; it describes |
| HOW MANY dimensions exist at most | **chart of accounts** — the maximum count | the NUMBER the register builds columns from |

```
CCT                   TypesOfCharacteristics = {Contractor, Contract, Item}   ← the contour
  ↓ binding (exists)
Chart of accounts     MaxAccountDimensionCount = 4                            ← the number
                      the account's kinds table: ≤ 4 rows, each a reference to a CCT element
  ↓ binding (exists, through the Account field's type)
Register              4 dimension slots per side, each typed by the contour   ← restructuring
```

The register stores neither the count nor the types — it ASKS for them, and it already walks
this chain. It merely takes the wrong thing from it (§2) and never asks about the count at all.
The chart of characteristic types drives nothing on its own: it hands the register the
information from which the fields are created, and the restructuring is what acts on it.

**Re-confirmed by the owner 2026-08-12**, in one sentence that names both halves: *the chart of
accounts governs the dimensions and their COUNT — and the count changes the schema; the chart of
characteristic types gives the dimension its TYPE, taken from the CCT's own composition.* Two
sources, two questions, neither answering the other's.

### The count is a CEILING on the account's own table (owner, 2026-08-12)

The two numbers of §4.2 meet at one rule. A user opens an account card and adds the kinds they need
— Contractor, Contract, and later Order — and that works because the slots were sown in advance.
So the account's kinds table may hold **no more rows than the declared maximum**: row four has no
fourth slot to be written into, and accepting it would mean accepting a value with nowhere to go.

Two consequences worth stating before it is built:

- **The check belongs to the WRITE, not to the form.** A limit enforced only while adding a row in
  the interface is not a limit — a data processor, an import or a paste writes the same table
  without passing through it. The form may refuse earlier for comfort; the write is what makes it
  true.
- **Lowering the maximum below what accounts already use is the interesting case.** Existing rows
  are data, and the number is schema, so the two can legitimately disagree the moment the number
  drops. The honest reading is that the surplus rows stop being answerable — their slot is gone —
  which is the same statement as "the column dropped at the next restructuring", and the owner
  should be told rather than have rows silently deleted for them.

### As the tree actually stands (verified 2026-08-12)

Three places disagree with that, and each is small:

| What the decision says | What the code does | Where |
|---|---|---|
| the count is SCHEMA, declared on the chart-of-accounts metaobject | `MaxSubcontoCount` is a predefined ATTRIBUTE on every account — data, `Number(1,0)`, default 1 — and `GetMaxSubcontoCount()` has **zero callers** | `chartOfAccounts.h:245-246`, accessor `:50` |
| the register builds N slots from that count | three fixed members, `Subconto1/2/3` | `accountingRegister.h:196-203` |
| a slot's type is the CCT's `TypesOfCharacteristics` (the contour) | the slot is typed `ibCtorObjectMetaType_Reference` over the CCT — i.e. a reference to a KIND, the same type the `SubcontoKind` column carries | `accountingRegisterMetadata.cpp:196-205`; the right accessor is `chartOfCharacteristicTypes.h:109` |

A fourth, found beside them and not part of the decision: changing the register's `ChartOfAccounts`
property re-types **only `Account`** — the dimension slots are untouched in `OnPropertyChanged` and
refresh solely in `OnAfterRunMetaObject`, so they carry the previous chart's type until the
configuration is run again (`accountingRegisterMetadataProperty.cpp:5-29`). The chart of accounts
has the mirror of it: its three property handlers are empty pass-throughs, so re-binding the CCT
does not re-type `SubcontoKind` immediately either (`chartOfAccountsMetadataProperty.cpp:5-18`).

**What each source moves.** The COUNT is a restructuring trigger (columns appear or disappear). The
CCT's composition is a restructuring trigger too, one level down — the contour IS the slot's type,
so editing it changes the physical column set of every register downstream. Neither of those is the
number of kinds a given ACCOUNT uses: that is the row count of its kinds table, ordinary data,
which changes nothing physical.

Consequences accepted with the decision:

- The count must live on the chart-of-accounts **metaobject** (schema), not — as today — as a
  `MaxSubcontoCount` attribute on every account (data): restructuring reads schema, and a number
  sitting in a row cannot decide how many columns a table has. How many dimensions a given
  account actually uses stays visible as the row count of its kinds table.
- Three `ibPropertyContainer` members become a vector built from the count, and the schema
  snapshot must derive the slots rather than read fixed members.
- Changing the count is an ordinary restructuring (`DiffSnapshots` + `ibStructureBuilder`)
  across every register whose chart of accounts sits on that CCT. No second mechanism.
- Two charts of accounts sharing one CCT get the same depth by construction; if different depths
  are wanted, that is a second CCT — the split falls on a natural seam.
- **Width, stated up front:** a slot is a COMPOSITE field — a type tag plus one column per
  admissible type. Four slots over three types is ~16 physical columns, not four. It affects row
  width, never correctness; the remedy, if it is ever needed, is narrowing a kind's `Type` (which
  already exists and nobody reads yet), not fewer slots.

#### Slot names, and the identity that must not move

A slot is a POSITION, not a thing: the same slot holds an item on one account and a counterparty
on another, so it cannot carry a meaningful name. Only a KIND has a meaning, and kinds live in
data. Hence three layers, and the number surfaces in exactly one of them:

| Where | How a dimension is named |
|---|---|
| schema / movements table | numbered, technical — `AccountDimension1..N` plus `…1Kind`; the `Dr` / `Cr` prefix appears ONLY in correspondence mode, where there are two sides to tell apart. In a one-sided register the side is `RecordType`, so the name carries no side at all |
| script | by KIND — `row.AccountDimensionDr[Kinds.Contractor]` (§4.4); the position never appears |
| virtual tables | by the KINDS THE CALLER ASKED FOR (§7) — a column means one specific kind |

**Query access to the slots costs nothing — verified.** `ibRegisterDataQueryable::GetColumns()`
returns the metaobject's generic attribute array, and an attribute IS a column
(`ibValueMetaObjectAttributeBase` derives `ibBackendQueryColumn`); `ResolveColumnByName` is
`FindAnyAttributeObjectByFilter(name)`. So slots created as ordinary predefined attributes appear
in queries by themselves — `SELECT AccountDimension1 FROM AccountingRegister.<Name>` needs no
custom-column support, no conversion layer and no change to the queryable at all. The one
condition is that the slots live in the generic attribute array like every other attribute, which
is what `FillArrayObjectByPredefinedAttribute` already delivers.

**Why a name is needed at all**, since application code never uses it: only the RAW movements
table is addressed by position — a `SELECT … FROM AccountingRegister.<Name>`, the field list in
the query constructor, the columns of a movements form. Those places must have something to
show. Dropping names entirely would require the source to vend fields BY KIND, and it cannot: the
set of kinds is decided by the account in each row, i.e. by data the source has not read yet.

The numbered name is therefore the honest one. It says "slot one" and claims nothing more,
whereas a meaningful name would be a lie — the same slot holds an item on one account and a
counterparty on another. The physical column is `fld<metaID>` regardless, so the language-level
name carries no storage meaning either.

⚠ **The metaID of a slot is fixed once and must never be re-issued.** The physical column name is
derived from the metaID (`fld<metaID>`), so a slot whose id changes is a DIFFERENT column, and the
data that was in the old one is gone. This is not hypothetical: the tree already survived an
incident of exactly this class, where `GenerateNewID` recomputed `max(metaID) + 1`, a deleted
object's number returned to circulation, and a new attribute asked for a column the dropped one
still owned — an index violation that could not be undone. `GenerateNewID` is now a monotonic
counter seeded once per load.

What that demands of the generated slots:

- each slot (both halves of the pair, both sides) is an ordinary persistent metaobject with an
  ordinary metaID, **serialised with the register** — not an id recomputed from the slot number
  at load time, and not derived arithmetically from the register's id, which would trespass on a
  shared id space;
- changing the count **appends or removes at the tail only**; it must never re-create the
  surviving slots. Going 3 → 4 → 3 leaves the first three untouched throughout;
- a removed slot's id never comes back — the monotonic counter guarantees it, and a later
  increase creates a genuinely new slot rather than resurrecting the old one;
- ⚠ **Corrected 2026-08-14:** switching correspondence off used to DROP the `Cr` slots and their
  data with them. It no longer does — the slots stay, their columns stand empty, and a one-sided
  register simply never writes into them. A slot that leaves and comes back comes back under a NEW
  metaID, i.e. as a different column, which is the very thing this section forbids (§ 5i).

---

### 4.3 Correspondence is a PROPERTY of the register, and slots double per side

A register declares whether its line is one SIDE or a whole POSTING:

| Mode | A line carries | Dimension slots |
|---|---|---|
| one-sided (what the code does today) | `RecordType` (Debit/Credit) + one `Account` | N slots |
| correspondence | `AccountDr` + `AccountCr`, one amount | N slots **per side** — `AccountDimensionDr1..N` and `AccountDimensionCr1..N` |

So with a maximum of 1 declared on the chart of accounts, a correspondence register gets exactly
one dimension column on the debit side and one on the credit side; with 4, four and four. The
count comes from the chart of accounts, the types from the CCT contour, the doubling from this
property — three independent answers, none of them stored twice.

Accepted cost: two physical shapes, two families of aggregates, and a branch in everything that
READS the register. Worth it because the two modes are genuinely different accounting, not a
preference — a chessboard and correspondence turnovers cannot be expressed at all without a line
that names both sides.

⚠ **Corrected 2026-08-14: there is only ONE physical shape.** Both accounts, the record type and
both sides' slots are declared whatever the mode says; the setting decides what is WRITTEN into them,
never which columns exist. A column list that followed the checkbox dropped the data in the columns
it removed, and left the two snapshots disagreeing about attributes that had never left the
configuration (§ 5i).

### 4.4 A slot is addressed by KIND, not by position

Physically the slots are numbered columns; in a script they are not. A posting says *which
dimension kind* it is filling and the engine places the value:

```
movement.AccountDimensionDr[Kinds.Contractor] = contractorRef   // by kind — the ordinary way
movement.AccountDimensionDr[1]                = contractorRef   // by index — when position is known
```

The reason is that position is not the author's business: which slot a kind occupies is decided
by the ORDER of the kinds table on the account (the `Order` column already exists there), and the
same kind sits in different slots on different accounts. Addressing by kind is what makes a
posting written once work for every account that admits that kind.

This is also the second half of the type story (§2), and the CCT says it in two tiers:

| Tier | What it is | Where |
|---|---|---|
| the CCT **setting** | the VALUE TYPE — every value the characteristic may ever take. The whole bag | `TypesOfCharacteristics` on the metaobject |
| a CCT **record** (a kind) | a FILTER over that bag — of everything the setting allows, these are the ones this kind takes | the `Type` attribute on the element |

So a kind never declares a type of its own; it SELECTS from what the setting already permits, and
its type is always a subset of the contour. The slot in the movement is typed by the contour (the
bag), and the kind narrows it **at the data level**, when the kind is known — a filter over
values, not a second declaration.

#### Editing the CCT is a SCHEMA change, and it cascades

The dimension slot is an ordinary field whose type is taken from the CCT — the whole set of what
may ever be stored there. Physically that composite becomes a type tag plus one column per
admissible type (each carrying its own storage prefix), so:

```
TypesOfCharacteristics changes on the CCT
   → the type of every slot changes (the contour IS the slot's type)
      → the physical column set of the movements table changes
         → restructuring runs
            → the totals bundles regenerate
```

Two things follow that are easy to miss until a live base meets them:

- **the blast radius is a graph, not one object.** One CCT may be bound to several charts of
  accounts, and one chart to several registers — so adding a type touches every slot, on every
  side, of every register downstream. The width arithmetic of §4.2 multiplies accordingly;
- **it is an ordinary restructuring, not a special path.** `DiffSnapshots` + `ibStructureBuilder`
  compute the difference and apply it, and the totals regeneration is the mechanism that already
  exists for a changed bundle. Nothing bespoke has to be written — but the register's schema
  snapshot must DERIVE its slot columns from the contour, so that the diff sees the change at all.

This is the same lesson as the slot count (§4.2), one level down: the schema is a function of
what the CCT and the chart of accounts declare, and every input to that function is a
restructuring trigger.

#### ⚠ NOT VERIFIED: the type-description editor and where its filter comes from

Raised by the owner 2026-08-12, and the audit above stopped one step short of it. A characteristic's
`Type` attribute is a **specialised value type** (`VL_TYPED`), built the same way the schedule
attribute was: clicking it opens a type-picking window rather than editing text in place. The
mechanism exists and is recent.

What has **not** been checked, and is the actual question:

- **the picker's filter.** The window must offer only what the CHART allows — its
  `TypesOfCharacteristics` — not every type in the configuration. So the editor has to receive the
  contour from the metaobject that owns the element, and it is unverified whether that path exists
  or has to be added (the neighbouring precedent is a control asking its neighbour for a type,
  `ibValueFilterItem::GetRightTypeDescription`);
- **where the chosen type lands.** It is written into the characteristic itself, so an element ends
  up carrying a narrower type than the contour — which is exactly the second tier of §4.4 and the
  thing the movement's value slot should later be narrowed by.

**An empty type is an ERROR, not a default** (owner, 2026-08-12). A characteristic must name a
concrete type; leaving it unset is not "everything the contour allows" and must not be treated as
one. So the attribute is fill-checked and the write refuses — a characteristic with no type would
otherwise let a value of any kind into a slot that was supposed to be narrowed, which is the very
thing the second tier exists to prevent.

Until both are confirmed by running, the two-tier story (contour narrowed per kind) is design, not
behaviour — the storage side is built, the editing side is unproven.

#### Who applies the narrowing: the FRONTEND, and the appliance already exists

The value slot is a composite that mirrors the contour exactly; the CCT element beside it IS the
characteristic. They are linked, and the link is what an editor follows: a control watches which
kind its neighbour holds, walks to that kind's own `Type`, and offers only those — so the
narrower set cannot be stepped around from the interface.

This is not new machinery. The same idea is already implemented one aisle over, in list settings:

```cpp
// listFilter.h — ibValueFilterItem
// The left side decides: a FIELD lends its own type (a reference field gets the quick
// choice, a boolean field the two-item drop-down, a number field a number box)…
ibTypeDescription GetRightTypeDescription() const;
```

A neighbouring cell lending its type to the editor of this one is exactly the kind→value
relation, and the walk to it is an ordinary dot path — the mechanism forms already use for
binding. What may still be needed on the frontend is the SOURCE marker on the control (which
neighbour to ask); the groundwork for that is in place.

⚠ So the check left open in §5 is a frontend question, not a metadata one: it is about wiring an
existing narrowing to a new pair, not about inventing one.

**Every slot is typed identically.** Ask three, and three columns appear on each side, all
carrying the same characteristic type — the whole combination the CCT declares as usable. Slots
do not differ by declaration; what tells them apart is the KIND, which is precisely the separator
of the pair the register really stores: *kind of characteristic* ↔ *value of characteristic*.

**The two halves take DIFFERENT types, and that is the whole distinction** (owner, 2026-08-12):
the first column accepts the **characteristic itself** — an ordinary reference to an element of the
chart of characteristic types, exactly like a reference to a catalog item ("Contractor"); the second
accepts the **value of that characteristic** — the chart's composition, narrowed by what the chosen
kind admits ("OOO Romashka"). The first names WHICH breakdown the figure is filed under and acts as
the filter a reading selects by; the second is what the figure is filed AGAINST.

**A slot is TWO columns, and the kind is stored.** The pair is written to the row as it stands —
the kind of characteristic beside the value of characteristic — rather than the kind being
inferred from the account's kinds table by position. So declaring 3 gives, per side, three
*(kind, value)* pairs.

This is what makes stored movements self-describing: an old row still says what its value was a
kind OF, so re-ordering the kinds on an account later cannot silently change the meaning of data
already written. The cost is one narrow reference column per slot next to the wide composite one
— cheap next to the composite itself, and it removes any need to re-post history after an
account's kinds are edited.

Physical arithmetic, so nobody is surprised at restructuring: `count × sides × 2` logical
columns, where each *value* half is itself a composite (a type tag plus one column per admissible
type in the contour). Three dimensions, correspondence on, three types in the CCT ≈ 3 × 2 pairs,
i.e. 6 kind columns and 6 composite value fields.

---

### 4.5 Posting — the verbs

A posting is written through the document's movement collection, one row at a time:

```
movements = Movements.<AccountingRegisterName>

row = movements.Add()            // correspondence: one row names BOTH sides
row.Period      = doc.Date
row.AccountDr   = Accounts.Goods
row.AccountCr   = Accounts.Supplier
row.AccountDimensionDr[Kinds.Item]       = itemRef
row.AccountDimensionCr[Kinds.Contractor] = contractorRef
row.Amount      = 1000

row = movements.AddDebit()       // one-sided — debit only
row = movements.AddCredit()      // one-sided — credit only
```

**`AddDebit` / `AddCredit` are not a second mode — they are ordinary verbs of the collection**,
and a correspondence register needs them too. An **off-balance** account has no counterpart by
definition: it is recorded on one side and there is nothing to pair it with. So a correspondence
row is allowed to leave a side empty, and that emptiness is the record's meaning rather than an
incomplete entry — which is also why `OffBalance` (§4.6) has to be read by the engine: a missing
counterpart is legitimate exactly when the account declares itself off-balance, and a defect
otherwise.

**What the verb does is set the side for you.** `AddDebit()` is `Add()` with the side already
stated — in a one-sided register that means the `RecordType` attribute is filled in; in a
correspondence register it means the row is born with only the debit half to fill. The author
never writes the flag by hand, so it cannot be forgotten or contradicted by the account that gets
assigned next to it.

The accumulation register has the same IDEA in its own words — `AddReceipt` / `AddExpense` — and
the two sets do **not** meet. Receipt/expense belongs to the accumulation register, debit/credit
to the accounting one; neither vocabulary is a special case of the other, and no shared verb
(`AddWithType(kind)` or any such) should be invented to cover both. They are similar in shape and
separate in meaning, and the shape is not worth a common mechanism.

⚠ State of the tree: the accumulation register carries the `RecordType` attribute (`Receipt` /
`Expense`, present in Balances mode, omitted in Turnovers) but vends only a bare `Add()` — its
own verbs are missing too. That is its own gap, closed on its own schedule, not part of this arc.

### 4.6 What a line is made of — the five kinds of field

| Part | Belongs to | Question it answers |
|---|---|---|
| **Account** (Dr / Cr) | the posting | against what |
| **Account dimensions** (Dr / Cr) | the account — declared by it, per side | in which analytical breakdown |
| **Dimensions** | the register's schema — the same for every line | the standing cut: organisation, tax-accounting separator, and the like |
| **Attributes** | the individual line | detail of THIS record, carried along, never aggregated |
| **Resources** | the register | what is actually measured — amount, quantity, currency amount |

Four of the five already exist on the metaobject; only the doubling per side (§4.3) is new.

#### Accounting flags are TWO DECLARED COLLECTIONS, not fixed booleans

The chart of accounts owns two branches of its own, each a list the CONFIGURATION declares,
sitting in the tree beside `Attributes`:

| Branch | Materialises as | Typical members |
|---|---|---|
| **Accounting flags** | a checkbox on every ACCOUNT | Currency, Quantitative — and whatever a configuration adds (tax, management, …) |
| **Dimension accounting flags** | a checkbox column in every account's kinds table | Sum, Quantitative, Currency |

So "quantitative" is asserted twice and means two different things: *this account keeps
quantities*, and *quantities are kept along THIS dimension of it*. An account card shows the
first as a group of checkboxes and the second as generated COLUMNS of the kinds table — the table
has `№ | kind | turnovers-only | <one column per declared dimension flag>`.

Today the platform has neither branch: it has **three fixed booleans** (`Quantitative`,
`Currency`, `OffBalance`) as predefined attributes, and, like `AccountType`, read by nobody. The
first two are members that belong to the first collection. The account also carries hierarchy
(subordinate to an account — the ordinary parent) and a "do not use" switch.

#### Off-balance is a PLATFORM flag, always present

`OffBalance` is not a member of the declared collection and never becomes one: accounting flags
are vocabulary a configuration invents, while off-balance is built in and exists on every chart
of accounts. In the tree it is an ordinary predefined attribute available to both a folder and an
account (`ibItemMode_Folder_Item`).

What it means is simply: **this account is a separate circuit and does not affect the balance.**
Leased assets, goods on commission, strict-reporting forms — and, in practice, management
accounting, which is commonly kept precisely on off-balance accounts, alongside the books rather
than inside them.

Two behaviours follow, both of them readers the engine currently lacks:

1. **A one-sided entry is legitimate** — an off-balance account has no counterpart by definition
   (§4.5), so a correspondence row with an empty opposite side is correct here and a defect
   everywhere else. Double entry is not a rule this circuit lives under.
2. **It stays out of balance totals** — it is tracked and reported on its own, never summed into
   the balance sheet.

Two things follow, and they are the same shape as the dimension-count decision:

- the vocabulary is **declared by the configuration, not enumerated in C++** — the engine learns
  none of the words, exactly as it learns none of the dimension names;

- the set should be **declared, not enumerated in C++** — a configuration adds "tax accounting"
  or "management accounting" without the platform learning those words, exactly as it declares
  how many dimensions there are;
- a **resource states which flag it belongs to**, and is then meaningful only for accounts
  carrying that flag: `Quantity` for quantitative accounts, `CurrencyAmount` for currency ones.
  Without that link every resource applies to every account, which is why a quantitative column
  today would be filled with zeroes for the accounts that have no quantity at all.

#### `SummaryOnly` — a kind that has turnovers but no balance

Per KIND, on the account, sits a flag meaning *turnovers only*: this dimension takes part in
turnover, and no balance is kept along it. So of four or five dimensions on an account, some are
full breakdowns — a balance is carried per their values — and some are turnover-only cuts. The
classic split: keep a balance per contract, but only turnovers per settlement document.

The flag exists (`m_propertySummaryOnly`, serialised, walked through the whole metaobject
lifecycle) and — like `AccountType` and the three booleans — **has no reader**.

What follows, stated before anyone designs the totals:

> **The balance key is NARROWER than the turnover key, and which slots are dropped is decided by
> DATA** — by the flag on a row of a particular account's kinds table.

**Storage is not affected.** A movement carries all its slots regardless; the flag changes only
HOW THE DATA IS READ — which slots the balance groups by. That is why it is an ordinary
enterprise-mode edit: a user may flip it and nothing breaks, because no column appears or
disappears and no data is reinterpreted. For live aggregation the flag is simply read while the
query is built.

The cost lands in one place only — **materialised** totals, where a key really is a column list
in a real table. There the flag change means regenerating that bundle, which is a mechanism the
totals strategy already has (declare → apply → regenerate), and materialisation is opt-in and
defaults to off. So this is not a redesign, it is a rebuild trigger: the set of things that
invalidate a totals bundle grows by one entry.

Per-kind accounting flags (a quantity carried per THIS dimension) do not exist at all.

⚠ Left as a question rather than a decision: whether the flag set becomes declarable in this arc
or stays three booleans until a configuration needs a fourth.

### 4.7 The account's side — active / passive / active-passive

An account declares which side it lives on: `ibAccountType` = `Active` / `Passive` /
`ActivePassive`, a registered enumeration behind the `AccountType` attribute
(`chartOfAccountsEnum.h`).

**It is declared and read by nobody.** `GetAccountType()` has zero callers in the tree — the user
sets it, the engine ignores it.

What the dead SQL does, precisely — the two halves differ and only one is wrong:

- **turnovers are already correct**: `TurnoverDr = SUM(CASE WHEN RecordType = Debit THEN res ELSE
  0 END)` and its credit mirror keep the two sides APART, and because they sum algebraically a
  negative amount lowers its own side exactly as the rule above requires. Worth keeping as the
  model.
- **the balance is folded**: `SUM(CASE WHEN RecordType = Debit THEN res ELSE -res END)` — one
  signed number per resource, computed without ever asking the account's type, so the
  active-passive case cannot be expressed at all.

The decision: the side is **a rule for reading the opposite side**, not a storage shape. Both
sides are always stored; what the account type decides is what the other one MEANS.

| Account | Typically | Balance | The opposite side means |
|---|---|---|---|
| active | what is SPENT and owned — assets, costs | debit, folded: Dr − Cr | a REVERSAL — it reduces the debit balance, it is not a credit balance |
| passive | where it CAME FROM — capital, liabilities, income | credit, folded: Cr − Dr | the same, mirrored |
| active-passive | settlements with counterparties | BOTH, kept apart | nothing special — the two sides simply accumulate |

An active-passive account is one that can stand on **either side at the same time**: classic
mutual settlements, where the same account owes some counterparties and is owed by others. That
is why folding it is not a display choice but a loss — a receivable of 100 and a payable of 100
are not "zero", and the answer "zero" is wrong in a way no formatting can undo. For active and
passive accounts the fold is legitimate, and the balance collapses into one number.

#### Two ways to decrease, and they are NOT the same

| | Written as | What it lowers | What it does to turnover |
|---|---|---|---|
| **the opposite side** | a credit entry on an active account (or the mirror) | the BALANCE | RAISES turnover on the other side — an ordinary entry, e.g. closing a passive account's credit against a debit at financial-result time |
| **a negative amount** (reversal / "red storno") | the same side, amount below zero | the TURNOVER itself | LOWERS it — the movement is unwound rather than answered by a counter-movement |

For an active or passive account both are available: the side already carries meaning, so an
entry on the opposite side reduces the balance. For an **active-passive** account the opposite
side is no help — both sides are positive by default and accumulate independently — so the only
way to take something back is the negative amount.

Consequences for the engine, and they are small:

- **negative amounts must be allowed**, and must NOT be "normalised" into an entry on the other
  side; that would turn a storno into two inflated turnovers and quietly change what the period's
  reports say;
- aggregates therefore sum **algebraically** — a plain `SUM` over the side already yields the
  correct behaviour for both cases, and no special reversal handling is needed anywhere.

**Active-passive is exactly a turnover accumulation register.** Receipt and expense side by side,
never folded into one number — the same shape, and therefore the same primitives that are already
built and numerically verified for accumulation (`register-totals-strategy.md`). Active and
passive are that shape plus a fold with a sign.

Beside it stands the **extended (unfolded) balance** — a reading that deliberately IGNORES the
account type and returns both sides for every account, active ones included. It is a separate
question asked of the same data, not a different storage: the fold is applied at read time, so
declining to fold costs nothing.

The consequence lands on the shape of the result: a balance source carries **two columns per
resource** (debit, credit), and the folded single number is a projection of them for accounts
that admit folding. Folding an active-passive account into one number destroys the very thing it
exists for — receivable and payable under one account, told apart by their dimension values.
Folding is legitimate only WITHIN one identical set of dimension values, never across an account.

The type is also checkable, which is the other half of reading it: a credit standing on an active
account is either a reversal or a mistake, and the engine is in a position to say which.

`AccountType` and the accounting flags (§4.6) are one step, not two: it is the same moment — when
the engine starts HONOURING what an account declares about itself instead of merely storing it.

---

## 5. Still open — owner's call

Nothing structural remains open. What is left is wiring and verification:

- the frontend end of §4.4 — which neighbour a value control asks for its type, and whether the
  source marker it needs is already on the control or has to be added;
- whether the first version honours all the accounting flags or only the ones the balance needs
  — answered by what landed: the balance-bearing link on a resource is in, the flag COLLECTIONS are
  not (§ 8.1).

---

## 5a. What landed on 2026-08-12 — the groundwork, before the register itself

Written after the fact, from the code. None of it is the accounting register's read path; all of it
is what that path was going to need.

**Naming.** `Subconto` → `AccountDimension` everywhere — engine, project files, `ru` / `uk` / `pot`,
files renamed through `git mv`. The CLSID key string (`MD_SKTB`) deliberately stayed: it is an opaque
body key already sitting in stored configurations, not a name. A stale `fuzzy` translation surfaced
on the way — `Active/Passive` had been auto-filled as «Активные пользователи».

**The dimension count moved from data to schema.** `MaxAccountDimensionCount` was a predefined
attribute on every ACCOUNT (a `Number(1,0)`, default 1, read by nobody). It is now a property of the
chart-of-accounts METAOBJECT — because restructuring reads schema, and a number sitting in a row
cannot decide how many columns a table has. The attribute is gone, so there is no second number.

**The slots became a vector of PAIRS.** Three fixed members (`Subconto1/2/3`) are replaced by slots
created from that count, each one a *(kind, value)* pair — `AccountDimension<i>Kind` typed as a
reference to a characteristic, `AccountDimension<i>` typed by the chart's composition. With
correspondence on the set doubles into `Dr` / `Cr`, and a credit account appears beside the debit
one. Slots are created once and REUSED: lowering the count deactivates from the tail rather than
destroying, because a metaID is the physical column name and a re-issued id is a different column.

**Hierarchy became a declared kind.** `ibHierarchyType` on the hierarchical base, four arrangements
(`FoldersAndItems` / `Items` / `Subordination` / `None`). It is restated on the `Parent` field's
select mode — everything that asks what a parent may be already asks the field — and the three places
that assumed folders (the "add folder" command, the "create inside this node" rule, the parent
picker) now branch on it.

⚠ **Corrected 2026-08-13, from the code:** this paragraph said a chart of accounts states `Items`.
It does not — `chartOfAccountsMetadata.cpp:21` states **`Subordination`**, and the difference was
load-bearing until that day: `Subordination` answered NO to `IsHierarchical()`, so
`GetHierarchyColumn()` handed out nothing and every consumer that only needed *"is there a parent"*
went without one. That accessor now answers `HasParentLink()` — the hierarchy IS the parent, and the
one arrangement with no hierarchy is the one with no parent (§ 8.5).

**The characteristic's own type became storable.** `ibValueTypeDescription` gets a column form: a
blob, by the same arrangement the schedule uses, with `ibTypeDescriptionMemory::WriteBuffer` /
`ReadBuffer` beside the node form so neither is written twice. New column role `_TD`, new persisted
tag appended at the END of `ibFieldTypes` (inserting one renumbers every tag after it and old rows
read as another type). The attribute is fill-checked: a characteristic with no type is an error, not
"everything the contour allows".

**A role is not one place, it is a chorus — and the write driver has to sing.** Adding the `_TD`
slot to `DescribeColumnLayout` is the visible half; the invisible half is `ibColumnSpread::DriveSpread`,
which binds a column's parameters without materialising the slot vector. The layout supplies the
INSERT's COLUMN LIST, the driver supplies its PARAMETERS, and nothing checks that the two agree.
A slot the layout emits and the driver skips does not bind a NULL — it binds **everything after it
one position early**, so the last parameter of the statement lands past the end of the prepared
buffer. Saving any characteristic failed with the Firebird driver's "parameter buffer is not
allocated"; the message was true and pointed at the wrong tier. The same duty falls on the L3-3 wire
codec (`ibDataMover`, the one that carries rows ACROSS a restructuring, chunk id `0x800062`), and on
both read arms. The rule to carry: **when a role is added, sweep every site that enumerates roles**
— the seven `Schedule` mentions are the checklist, and a role with fewer mentions than it has is a
defect that compiles.

**One type picker, two callers.** The dialog moved out of the property editor into
`win/dlgs/typeSelector`. Its contract is what the owner stated: a SHAPE to render (one of the five
selector kinds), an optional FILTER narrowing it, the current description in, the edited one out.
It sorts what it shows into categories by the id itself and asks the registry for referenceable
types, so no caller keeps a list of metatypes. The metadata editor passes the shape; a
characteristic's field passes `reference` plus its own declaration as the filter.

**Guard.** An account may not declare more analytics than the chart allows — checked on WRITE, where
a data processor or an import cannot walk around it.

**Three defects in the neighbours, found while working here and fixed with it.** They are not
accounting, but they sat under it: the accounting register was going to copy the first one.

| Defect | What it did |
|---|---|
| The accumulation totals filed `recordType = 0` into the RECEIPT column, while `ibRecordType` declares `eExpense` first | every receipt counted as an expense and back; balance came out as `Expense − Receipt`. Both spellings now derive from the enum. See [register-totals-strategy.md](register-totals-strategy.md) |
| The information register's slice always filtered `Active = TRUE`, but that column exists only under a recorder | an INDEPENDENT periodic register (the default) built SQL naming a field it has not got; the exception was swallowed and the slice returned EMPTY |
| `Get` was declared with arity 1 while the dispatcher handles `Get(period, filter)` | the periodic form was unreachable from script — the branch existed and could not be entered |

## 5b. Running it — what the first live save found (2026-08-12, evening)

Everything above compiled and read back fine. WRITING was where it came apart, and every defect was
of the same family: **a declaration that only half the machinery had heard about**. They are listed
in the order the live run hit them, because that order is itself the lesson — each one was hidden
behind the previous.

| Defect | What it did | Where the rule now lives |
|---|---|---|
| `ibColumnSpread::DriveSpread` bound no parameter for the new `_TD` slot | the INSERT listed the column and the bind skipped it, so every parameter after it went one position early and the last ran off the prepared buffer — the Firebird driver reported a null parameter buffer, one tier away from the cause | the driver walks the same roles as `DescribeColumnLayout`, and the wire codec (`ibDataMover`) too |
| The analytics-kinds section derived `ibValueMetaObjectTableData` — the base with **no queryable and no physical table** | the write dereferenced a null queryable: an access violation on saving any account with analytics | it derives `ibValueMetaObjectTableDataRef`, the DB-backed variant |
| `GetTableArrayObject` filtered tabular sections by a **hand-written pair of clsids** | the section, registered under its own id, was not a table to any metadata walk: absent from the schema snapshot, absent from the form | one `g_tabularSectionCLSIDs` beside the ids themselves. Still a list and not a type test — this walk runs per child of every traversal, and RTTI there is not free |
| The section was marked `SetFlag(metaDisableFlag)` to mean "cannot be deleted" | that flag is read by `IsEnabled()`, so `IsAllowed()` went false and the section fell out of every walk **exactly when a chart of characteristic types was bound** — i.e. whenever it meant anything. `Table unknown CHARTOFACCOUNTS…_VT…` | `metaPredefinedFlag` ("bound to its parent for life"), set by the SECTION on itself at construction |
| The kinds column took its reference type in `OnAfterRunMetaObject` **only** | between picking a chart and the next configuration run, the metadata described a column the table had never grown; the apply then emitted `ALTER TABLE … DROP fld…_RTRef` and died on a column that does not exist | `ApplyAccountDimensionKindType()`, called on LOAD, on the user's PICK and on SAVE — every point the binding can arrive. An unresolved binding (load order) leaves the column alone; only an empty one clears it |
| `ExecuteWrite` ended in `catch (...) { return -1; }` | every driver error — no table, no column, a constraint — reached the user as "failed to save the object data". Three rounds were spent guessing what it was | an `ibBackendException` propagates; anything else still degrades to -1 |

**And the designer's own share of it.** A source-explorer node for a tabular SECTION inherits the
ROOT's owner, so the section's columns report the RECORD as theirs. Dragging a characteristic's field
into a tablebox therefore asked the record for a value living in the section — it answered "no", the
pinned twin took over, the column landed correctly, and the value map's assert fired on every pass.
The read is now gated by the attribute lookup that already ran (`not one of mine → do not ask`), in
both hop gates. The assert stayed exactly where it was: a warning that shouts on a working path is a
question asked of the wrong object, not a warning to remove.

Two more of the same family: the analytics-kinds section was appended to the chart of accounts'
source BY HAND (a patch for the clsid gap above) and, once the gap closed, arrived twice — a
generated form grew two identical tableboxes; and copying a tabular ROW walked
`GetAttributeArrayObject` ("what did a person add"), so a section whose columns are ALL predefined
copied nothing at all.

**And one decision, not a defect.** A chart of accounts now **requires** a chart of characteristic
types. Its analytics kinds are elements of that chart; with no chart the column has no type, and the
section becomes rows that can name nothing. Refused on save, where an import cannot walk around it.

**The rule these share.** When a fact is declared in one place, find every place that walks it and
make them agree — not the one that happened to fail. A role added to the column layout has seven
sites that enumerate roles; a metatype added to a family has the filters that list ids; a derivation
has every phase its input can arrive in. Each of the six above compiled perfectly while disagreeing.

### Hierarchy: what may be entered

Landed with the above, and worth separating from folders: **an item hierarchy lets you go into any
element; a folders+items hierarchy only into a folder.** The level fetch cannot tell them apart — it
returns rows either way — so the composer no longer answers "does this row have children" (it does
not know, and finding out costs an EXISTS per row). The model decides, from the source's declared
kind:

```
container = a folder  OR  the source is an item hierarchy  OR  children were actually counted
```

`IsItemHierarchy()` is on the queryable and forwards to the metaobject's own declaration — the same
word the metadata uses (`ibHierarchyType::eItems`), so no tier invents a second vocabulary.

⚠ A chart of accounts declares `Subordination`, not `Items`, so it answers **no** here: a summary
account becomes a container by the third clause — children actually counted — rather than by
declaration. Worth knowing before reading a list of accounts and concluding the tree is broken; and
worth asking, separately, whether these two arrangements should stay two now that the parent means a
hierarchy in both (§ 8.5).

## 5c. A structure change can now be refused (2026-08-13)

Declaring a structure and being ALLOWED to reach it are different questions, and the differ could only
answer the first. Two of this arc's own settings quietly delete data when lowered: the analytics
**ceiling** on a chart of accounts (accounts already holding more kinds lose rows) and the **hierarchy
kind** on any catalog or chart (`None` retires Parent, anything but folders+items retires IsFolder —
`ApplyHierarchyType` disables them and the differ drops the columns). Both read like a property edit.

So a table's declaration may now carry its own rule:

```cpp
struct ibSchemaTable {
    std::function<bool(ibRestructureInfo*)> m_beforeChange;   // may REFUSE
    std::function<void(ibRestructureInfo*)> m_afterChange;    // may only SAY
};
```

**Two passes around the diff, not a branch inside it.** A rule can be broken by a change the diff finds
nothing to do about — lowering a declared limit alters no column, so a per-table branch would never be
reached. Every `m_beforeChange` therefore runs BEFORE the first statement; a refusal stops the whole
apply (half a structure is not a state anyone asked for) and every rule still runs, so the user sees
all the objections at once. `m_afterChange` runs when the structure is settled, inside the same
transaction; it returns `void` on purpose — by then there is nothing left to decline.

**Where the rule lives.** With the declaration, attached while the change tree is being formed: the
differ knows table shapes and nothing about accounts, and the chart of accounts knows its ceiling and
nothing about DDL. They meet through one field. The refusal states its own reason into the ledger, and
`ibRestructureInfo::HasErrors()` — declared long ago and read by nobody — now greys the Apply button.

The pass shape inside `DiffSnapshots`, and what a refusal does to the save transaction once the
dialog offers only Cancel: [query-engine-layers.md](query-engine-layers.md) § L3. Where each rule is
attached: `commonObjectSchema.cpp` (hierarchy) and `chartOfAccountsMetadataSchema.cpp` (ceiling) —
the schema aspect file of each metatype ([ARCHITECTURE.md](ARCHITECTURE.md) § `metaCollection/`).

| Rule | Refuses when |
|---|---|
| Analytics ceiling (chart of accounts) | any account holds more kinds than the new number |
| Parent retired (`None`) | any row has a parent — tested on `_RTRef != 0`, since an empty reference is an all-zero guid and not NULL |
| Folders retired | any folder exists |

## 5d. What an account SHOWS (2026-08-13)

`ibValueRecordDataObjectChartOfAccounts::GetSourceExplorer` (`chartOfAccountsObject.cpp`) is what a
generated form is built from. It vends five predefined fields, then the ordinary attribute and
tabular-section loops every record family runs:

| Field | Why it is there |
|---|---|
| **Code** — *editable* | in a chart of accounts the code IS the account (`51`, `60.01`), and it is the first thing a person types when adding one |
| **Description** | the name |
| **Parent** | an account is subordinate to an ACCOUNT (item hierarchy, § 5a) |
| **Account type** | active / passive / active-passive (§ 4.7) |
| **Off-balance** | a statement about the account, not a way of keeping it (§ 4.6) |

**The code is typed in, not handed out.** A catalog appends its code read-only
(`AppendColumn(GetDataCode(), false)`) because there it is a serial the system mints; a chart of
accounts appends it enabled. Copying the catalog's line made an account impossible to name at all.

**Account type had been missing here**, so a generated form showed an account as if it were a plain
catalog item — code, description, parent — with no way to say which side it lives on. It is a declared
TYPE (the `AccountType` enumeration), so the form builds the editor from the attribute itself; nothing in
the explorer spells the three members out.

**Quantitative / Currency are deliberately NOT vended** — and this is the decision, not an omission.
They are two booleans spelling out ONE fact: *how the account is kept*. That fact wants an accounting
**KIND** — declared once on the chart and ticked per account, the same shape the analytics kinds already
have (§ 4.6, "Accounting flags are TWO DECLARED COLLECTIONS"). Putting them on the form as two
checkboxes would fix the bit-encoded form in the interface before the mechanism that replaces it exists,
and the interface is the hardest place to take a shape back out of.

⚠ The analytics-kinds section is **not** appended by hand here. It used to be, because that was the only
way to reach it while its clsid was missing from the tabular-section filter (§ 5b); now that the filter
knows it, the general loop picks it up, and the hand-written line produced two identical tableboxes on a
generated form. A special case that outlives the gap it patched becomes a duplicate.

## 5e. What landed on 2026-08-13 — the register itself

Written from the code, after the fact, and re-checked against the tree at the end of the day. This is
steps 4-7 of § 6 below — the totals bundle landed with the rest rather than after it. What is still
missing is in § 8.

**The read side is five virtual tables, and they stand on the L3 door.** `Balance`, `Turnovers`,
`BalanceAndTurnovers`, `RecordsWithAccountDimensions` are registered for every accounting register;
`DrCrTurnovers` only in correspondence mode, because a one-sided line discards the pairing at write
time and no reading can invent it back (§ 7a). Each is a `ibAcctSourceDescriptor` with a shape, a
call-scoped companion queryable, and a `Compute*` on the metaobject — the shape the accumulation
register already has. The **1074 lines of raw L1 SQL are deleted**, `#if 0` blocks included, and the
manager is ~140 lines of thin wrappers over the same companions.

**Where they read — decided per PASS, not once.** Every reading builds its rows through
`ibDataQueryBuilder` and chooses its source there. The trigger-maintained totals view when the driver
can maintain one (`HasMaterializedViews()`); the MOVEMENTS when it cannot, and the movements again
whenever the question is one a total cannot answer — a filter on the OPPOSITE account in a
correspondence register (a totals row is keyed by ONE account and the other was never stored beside
it), or a fold finer than the stored grain ("And the routing is explicit", below). Both surfaces
publish the same columns under the same metaIDs, so nothing above the reading learns which one
answered.

The figure follows the surface and nothing else does: on the totals the trigger already split the
sides (`<Res>TurnoverDr` / `TurnoverCr`), on the movements the side has to be picked out of the row
(`RecordType`, or which account the pass grouped by). Same sum, two spellings of what is summed.

✅ **All five hand the door a relation** (2026-08-14) — every one overrides `GetSourceRelation`, so
none of them is a RAM reading by construction any more. They divide by WHAT THEY STAND ON, and the
gates follow from that rather than from how far each got:

| Reading | Stands on | Gate |
|---|---|---|
| `Records` | the movements — a projection, already as fine as the data gets | none (`m_reg != nullptr`) |
| `DrCrTurnovers` | the movements — a row that IS a pair cannot come from a key holding ONE account | none beyond the mode it only exists in |
| `Turnovers` | the stored surface | § 8.3 |
| `Balance` | the stored surface | § 8.3 + `FoldOutSummaryOnly` |
| `BalanceAndTurnovers` | the stored surface | § 8.3 + `FoldOutSummaryOnly` + a periodicity |

So a gate is not a measure of unfinished work: **only a reading that stands on the TOTALS can be
asked something the stored key cannot hold**, and those three ask it. The two that group the
movements have every column they name, always.

⏳ What each gate still closes, and why, is § 8.3. The RAM readings stay — they are what a closed
gate falls back to, and the oracle the server road is checked against (§ 8.3a).

**One argument layout, two entrances.** `ibAcctArgs::For(shape, correspondence)` computes which slot is
which, and BOTH the parameter declaration and the call reading derive from it. The signature genuinely
differs by mode (a correspondence register takes an account and a breakdown per side), which a
hand-numbered namespace cannot express — and the neighbour's off-by-one, where a declared periodicity
pushed every call's condition into another slot, is exactly what one shared layout prevents.

**The breakdown carries its KIND.** Asked for by kind, a column means that kind and the value is
selected by a `CASE` over every slot — written **per physical field** and projected under one prefix,
because a slot is a composite (a type tag plus one field per admissible type) and a single-field CASE
would carry the tag and lose the value. That needed one seam in the expression IR:
`ibQueryColumnExpr::ColField(col, field)` — a named field of a column rather than its first
(`dbTableProvider.cpp`); the RAM evaluator refuses it loudly instead of answering with the whole value
under a field's name. Asked for nothing, the slots are projected **as they stand** — which is the
account's own order.

**But the kind is a column of a LINE, not of a total.** Every reading GROUPS by the kind beside the
value — that is how a turnovers-only breakdown is recognised and dropped — and only the readings whose
row IS a movement line PUBLISH it: the movements table itself, and `RecordsWithAccountDimensions` when
nothing was asked for (`AccountDimension<i>Kind`, one per slot). A balance or a turnover reports its
breakdown BY VALUE, because the kind is what the fold ran over; a kind column there would mean
something only while a row happens to carry one kind, which is not a property anyone can rely on. When
a kind WAS asked for, the column already means that kind and there is nothing left to publish.

**What the account declares is now read.** Three flags that had no reader at all:

| Declaration | What it now does |
|---|---|
| `AccountType` (active / passive / active-passive) | the fold at READ time: active collapses into debit, passive into credit, **active-passive does not fold** — a receivable of 100 against a payable of 100 is not zero. Applied after aggregation, so an unfolded reading of the same data stays available |
| `SummaryOnly`, per kind on the ACCOUNT — stored in the account's own kinds table, so the same kind may be a full breakdown on one account and a turnover-only cut on another | the balance key drops that breakdown and the rows that then coincide are **merged** — otherwise one balance is reported once per settlement document that touched it. Turnovers drop nothing: a breakdown with no balance still takes part in turnover. In `BalanceAndTurnovers` the two meet in one row, and the rule is stated outright: a row standing on a turnovers-only breakdown reports the **movement part of the period and no balance at all** — the balance columns stay EMPTY rather than zero, because zero would claim a balance was kept and came to nothing |
| `OffBalance` | a separator, read at the WRITE (`accountingRegisterObject.cpp`): a one-sided line on such an account is skipped by the double-entry check, and a correspondence line on one may name a single side. The readings do not filter by it — an off-balance account is reported on its own row like any other, which is what "kept beside the books" means for a per-account figure |

**The hierarchy answers, and it answers in THREE words rather than one.** An account argument used to
expand its subtree ALWAYS, which conflated two different questions. The vocabulary is the language's
own — `ibQueryDimUnfold`, already the unfold L5 applies to grouping — and the owner's words for it are
those same three:

| Word | What is asked for | Where the figures are reported |
|---|---|---|
| `Elements` | «in» — exactly the values passed, and nothing under them | under each value, as passed |
| `Hierarchy` | the value AND its subordinates | under the NAMED value: the subtree folds into it |
| `HierarchyOnly` | the subordinates WITHOUT the named one | under the named value, which contributes nothing of its own |

So the filter brings the subtree in (a walk down `Parent`, then an ordinary `IN`), and the reading then
folds those rows into the account that was NAMED, **adding** the figures. It assigned before — which
kept whichever group arrived last and silently dropped the rest, since every subordinate arrives from
the server as a group of its own. Asking for 60 therefore reports ONE row for 60, carrying 60.01 and
60.02 in it, which is what a chart of accounts is for. Deliberately not a code prefix: a code is a
presentation somebody may re-number, the parent link is the fact.

**And the subtree walk stopped being about accounts.** It asks the VALUE's own metaobject for its parent
link (`HasParentLink` / `GetDataParent`, in `CollectSubtreeOfValue`) instead of casting to a chart of
accounts, so a catalog or a chart of characteristic types passed in the same argument answers the same
way, and a target with no parent link stands for itself. Nothing in that function knows what an account
is — which is why it did not need to learn what a catalog is either.

⏳ The readings pass `Hierarchy` today. Choosing between the three from a QUERY needs a word the filter
vocabulary does not have yet — § 8.5.

**The write side.** `AddDebit()` / `AddCredit()` beside `Add()` — the verb states the side, so it cannot
be forgotten or contradicted by the account assigned next to it. The breakdown is a MAP of *(kind →
value)* and may be written either way: a whole map assigned at once (`row.AccountDimensionDr = <map>`)
or one pair at a time. A POSITION is imitated where it belongs — the kind is taken from the ACCOUNT,
whose kinds table says which is first, second, third, and the value is then filed under that kind;
nothing in the API takes a slot number, because a number means a different breakdown on a different
account. A dimension is addressed BY KIND
(`row.AccountDimensionDr[Kinds.Contractor] = ref`): the collection finds the slot already holding that
kind or takes the first free one, writes the **pair together**, and adjusts the value to the KIND's own
`Type` — the second tier of the contour, i.e. `AdjustValue`. No free slot raises, naming the declared
count, rather than dropping an analytic silently. The collection holds the line's POSITION, not the
line: a line is a temporary the caller owns.

**Double entry, checked before the write.** Debits equal credits per set, over the **balance-bearing**
resources only — which needed a new declaration: `Balance` on the resource (`metaResourceObject`),
shown only when the owner is an accounting register (`OnPropertyRefresh` + `HideProperty`, the rule
`SelectMode` and `ItemMode` already follow). Without it the check would sum quantities and refuse
perfectly good postings. In correspondence mode the amount balances by construction, so what is checked
there is that an ordinary entry NAMES BOTH SIDES — and an off-balance one need not.

**The totals, and the GRAIN.** A totals row is keyed by the ACCOUNT it is about, so how many tables
are MAINTAINED follows the mode rather than the number of sides: a one-sided register keeps **one**
(the two sides told apart in the accumulated VALUE, exactly as the accumulation register separates
receipt from expense), a correspondence register **two** — one movement is about two accounts, each
under its own breakdown, and two different keys cannot be one upsert. ⚠ **Corrected 2026-08-14:**
both tables are DECLARED whatever the mode is, and both are named after the predefined totals objects
(`…_DebitTotals` / `…_CreditTotals`) rather than after the setting — the names above were a function
of a checkbox, and a renamed table is one the differ can neither find nor drop
([register-shared-machinery.md § 4c](register-shared-machinery.md)). What is stored
is TURNOVER at the full key (account + each *(kind, value)* pair + the register's dimensions), never a
balance: the balance key is narrower and by how much is decided per account by the turnovers-only flag,
i.e. by data the schema cannot see. A balance is that turnover folded up to a moment.

The stored grain is a **DAY**, and every reading says which arm of the view it takes rows from:

```
   ≤ the grain boundary     the stored rows answer alone
   inside the grain         stored rows BELOW the boundary + the movements of that day up to the moment
   an interval              whole grains from the stored rows, each partial END from the movements
```

A stored row has no recorder, so the recorder column is the row's own answer to "which arm am I" — no
flag column was invented. Saying nothing here is not the neutral option but the wrong one: every
movement of the current day would be counted twice, once rolled into the day's total and once as
itself, and the result looks entirely plausible.

**And the routing is explicit**, because the totals cannot answer everything: a question about
CORRESPONDENCE (the other account of the same movement) and any reading finer than the grain — an
hour, per recorder, per line — go to the movements. Answering those from the totals would mean
ignoring the filter or replying at the wrong granularity, both of which produce a plausible wrong
number rather than an error. The live path therefore stays in service — not as a fallback kept warm,
but as the answer to a class of question the stored key cannot hold.

**There is NO parity check, and that is the decision** (owner, 2026-08-13). Nothing on this register
compares the stored figures against re-aggregated movements — not in the manager API, not on the
metaobject. The reason is where integrity is held: the delta runs in the trigger, in the SAME
transaction as the movement it accumulates, so the two cannot part by themselves. Materialising the
figures was done exactly to stop watching them; a routine that keeps re-asking whether the write did
what the write is defined to do puts the surveillance back and calls it safety. What could still part
them is a write that reached the movements WITHOUT the trigger — a restore, a bulk load, direct SQL —
and that is an EVENT with a repair of its own (`ibDerivedState::Regenerate`), not a reason to stand a
check beside every read.

**A refusal has to happen where refusing is free.** The rule "a register uses exactly ONE chart of
accounts" moved out of `OnSaveMetaObject` and onto the table declaration's `m_beforeChange`
(`accountingRegisterMetadataSchema.cpp`), the mechanism § 5c introduced. Raising from the middle of the
save left the configuration write transaction OPEN — the exception unwound past whatever would have
closed it — so the next attempt to save waited on that lock until it timed out as a **deadlock on
`sys_config_save`**, an error naming neither the rule nor the register. Attached to the declaration, the
rule runs in the first pass of `DiffSnapshots`, before a single statement: it costs nothing to refuse,
it states its own reason into the ledger, and every other rule still runs, so all the objections arrive
at once. The general form is worth carrying past this arc: **a check whose subject is a DECLARATION
belongs to the declaration, not to the write that happens to notice it** — the write is halfway through
something by the time it looks.

**A delta guard is an SQL expression, and it has to be a BOOLEAN one.** Both guards on the accumulation
are therefore written as comparisons, and neither spelling is cosmetic:

| Guard | Written | Why not the shorter form |
|---|---|---|
| the movement is IN FORCE (`Active`) | `{row}.fld…_B <> 0` | a boolean attribute is stored as `SMALLINT`, so a bare field in a `WHERE` is a value and not a condition. Firebird answers "invalid usage of boolean expression" and the whole `CREATE TRIGGER` fails — taking the restructuring that emitted it down with it |
| this side NAMES an account (correspondence) | `{row}.fld…_RTRef <> 0` | an empty reference is an all-zero guid, not NULL, so a null test would pass on every empty side. Same comparison the hierarchy rules use for "does this row have a parent" (§ 5c) |

The second guard is what keeps an off-balance line — legitimately one-sided (§ 4.5) — from posting a
row keyed by an EMPTY account into the other side's table: a bucket that is not an account at all, and
one every reading would then have to learn to ignore.

⚠ **And two guards have to COMPOSE.** `ibSchemaMaterialize::Guard` assigned, so a correspondence
register — the first table anywhere to declare BOTH of the rules above — kept only the second, and its
totals accumulated movements that were out of force. Nothing about that result looks wrong: the rows
are all rows, and the figures are merely larger than they should be. It composes with `AND` now, in
both forms — the trigger's TEXT and the regeneration's PREDICATE, which have to state the same rule or
a rebuild disagrees with the trigger that filled the table. The class it belongs to (*a fluent setter a
caller may reasonably call twice must compose, or say in its name that it replaces*) is in
[register-shared-machinery.md § 4b](register-shared-machinery.md); the exact case that was silently
wrong is pinned in `tests/test_accountingTotals.cpp`.

**Regeneration waits for the DDL to become durable — and the CONDITION that used to stand here is
gone.** What was written first was a rule: *skip regeneration when the source table is created in this
apply*. It reads like a design decision and was a dodge around a symptom. A rebuild READS the source,
and an engine that keeps DDL transactional refuses DML against a table whose shape the still-open
transaction is writing — which has nothing to do with whether the apply CREATED that table or merely
ADDED A COLUMN to it. Adding one dimension therefore failed with "Column unknown FLD…" naming a column
added three statements earlier, and the message accused the source table.

`ibSchemaBuilder::Execute` now marks a table as not-yet-durable for EVERY shape-changing statement —
create table, add / drop / alter column, alter table — and the regeneration runs through `RunOrDefer`
(`query/schemaBuilder.cpp`, `query/schemaSnapshot.cpp`), the same barrier door the seed writes already
use: queued past the DDL commit while the table is not durable, run inline when it is. One rule where
there was one exception.

⚠ **This class is invisible to the test suite by construction.** The suite runs on SQLite, and SQLite
has no DDL barrier — a statement is durable the moment it returns — so every deferral path there is the
inline one. Only a live apply on Firebird exercises the barrier at all, which is also why the symptom
arrived from a base and not from a red test.

None of that changes what the three operations over a totals table are, and they are not variants of
one another:

| Operation | What it is | When it runs |
|---|---|---|
| the **trigger** | maintenance: each movement moves its own figures, inside the movement's transaction | every write, from the first one |
| **regeneration** (`ibDerivedState::Regenerate`) | rebuild an EXISTING body of movements into totals — clear and re-aggregate | a changed key or grouping, a restore, the designer's "recompute" |
| **collapse** (`ibDerivedState::Collapse`) | tidying: fold a split table's shards back to one row per key, drop rows that are provably zero | housekeeping, safe beside live writers, safe never to run |

**Split totals are declared here too.** `SplitTotals` (⚠ **on by default since 2026-08-14**, on both
registers — § 5i) puts a shard column into the
KEY — which is what makes several physical rows legal for one logical one — and `Split(8)` says how many.
The mechanism is the accumulation register's, unchanged; the reason to want it is stronger here: a
posting run hits the same few accounts (cash, revenue, VAT) from every document, so the hot key is the
norm rather than the exception. Turning the switch changes the key shape, so it takes an Apply and the
regenerator rebuilds the table — a re-keyed row cannot be migrated in place.

**What both registers say identically now lives in `registerQueryLowering.h`.** The figure words
(`ibRegFigure`), the side words (`ibRegSide`) and the one function that joins them
(`ibRegSidedFigure`) — so `Balance` said of a side cannot drift from `Balance` said whole; the grain
cut for a two-armed view (`ibRegFillArmCut`) and the recorder tuple it compares against
(`ibRegRecorderTuple`), with the accumulation register's copies deleted rather than left beside them;
and `ibValueMetaObjectRegisterTotals`, the metaobject a derived totals table exists to borrow an
IDENTITY from (accumulation keeps the old spelling as `using ibValueMetaObjectTotals =
ibValueMetaObjectRegisterTotals`). Two registers reading a boundary, a periodicity or a grain by two
copies of the same words is how they come to disagree about where a day ends.

**Two lists that must not exist twice.** The manager's method table no longer writes an arity beside a
signature: it asks `ibAcctArgs::For(shape, correspondence).m_count`, the very function that READS the
call. Hand-written, the two drifted on the first day — the layout reserves a slot for the credit-side
breakdown in correspondence mode, the literals did not, and a script's CONDITION landed in the kinds
slot, was read as a breakdown list and silently dropped.

The layout answers which ARGUMENTS exist; which COLUMNS a reading publishes is a second question, asked
of the shape (`PairedRow`, `accountingRegisterMetadataTotals.cpp`), and its answer takes the mode into
account: `DrCrTurnovers` always names two accounts, `Records` does so only in a correspondence
register, and in every other reading the credit account is a FILTER rather than a column — "the balance
of 51" is a question about ONE account even in a register whose lines name two. The side prefix follows
that same answer, so `AccountDimension1` gains its `Dr` / `Cr` exactly where there are two sides to
tell apart.

The same removal is now available on the metadata side: `ForEachOwnAttribute` (`accountingRegister.h`)
is ONE walk over every attribute this register owns — both kind lists, both value lists and the credit
account. The slots are created in a single place and then have to be visited by eight passes (load,
save, delete, before/after-run, before/after-close, and the typing pass), each of which spelled its own
loop, and the credit side is missing from every one of them: a correspondence register's credit slots
are created, serialised, given columns — and never typed. ⏳ The walk exists; moving the eight passes
onto it is § 8.4.

## 5f. The first live apply, and what it cost (2026-08-13, evening)

The bundle of § 5e compiled and declared correctly. APPLYING it to a live Firebird base did not, and
the failure was in the one place the design had not been asked about — how a key is held UNIQUE.

**`too many keys defined for index ACCOUNTINGREGISTER1094_TT_PK`.** A key is LOGICAL columns; an index
is their PHYSICAL fields, and a reference is three of those. The totals key — period, account, a
*(kind, value)* pair per analytic, the dimensions — is about twenty-one segments at four analytics
against Firebird's sixteen. Nothing in it is removable (§ 5e says why the kind is in the key), so the
uniqueness moved into ONE hashed field while the key columns stayed exactly as declared and the delta
went on MATCHING BY THEM. A digest collision can therefore only refuse an insert, never merge two keys
into one row — the direction an accounting engine should fail in. The mechanism is shared with the
accumulation register, which meets the same ceiling at five reference dimensions, and is written up
once in [register-shared-machinery.md § 4a](register-shared-machinery.md).

**The credit slots were never typed** — § 8.4, closed with it. Nine lifecycle passes had spelled nine
loops over the debit vectors; they now walk `ForEachOwnAttribute`, and the credit account's own typing
moved after the call that CREATES it.

**`RecordsWithAccountDimensions` publishes `Active`** (owner, asked and answered the same day). It
deliberately does not FILTER by it — it is a listing of lines, not a total, and a line taken out of
force is still a line that was written — but publishing nothing about it was the one combination that
misleads: all the rows, and no way to tell which ones count. A total has no such column and should not:
the trigger's guard already kept it to what is in force.

**Two defects in the neighbour, found by reading the two schema files side by side.** The accumulation
register's `Active` guard was rendered without a comparison (`{row}.fld…_B` where a condition is
required — the failure mode § 5e describes, still live in that file because nothing had applied that
bundle to Firebird since the guard was added), and its field tree could not name the recorder or the
line number, so it handed them over as synthetic columns. Both are in
[register-shared-machinery.md](register-shared-machinery.md) § 4 and § 2.6.

**And the tests written beside it found what neither the apply nor a reading could.**
`tests/test_totalsKeyHash.cpp` (13) pins the decision to hash, the render, and the backfill's
idempotence; `tests/test_accountingTotals.cpp` (7) pins the guards, the sides and the stored grain.
Writing the second is what exposed the guard that replaced instead of composing (§ 5e): a bundle whose
SQL applies cleanly and then accumulates the wrong rows has nothing wrong with it to read, so only a
test that states what a figure ought to be can see it. ⚠ Both suites run on SQLite, which is why the
DDL-barrier class of § 5e is not among the things they could have caught.

### 5g. A THIRD place the declared precision was not asked for (2026-08-13, from the live apply's log)

The movements table is created from the resource's own declaration — `Number(10,0)` becomes
`NUMERIC(10,0)`. The totals table's accumulating columns were not: `ibRawDBColumn::Number` answered with a
flat `ibTypeNumber(18, 0)`, so a resource carrying a fraction had it dropped **on the way into the
totals**, whatever the movements held. Four sites, in both registers.

They take the resource's own precision and scale now — the same rule the folds already follow
(§ 7.2: *a cast that names a type the schema already declares is a second declaration*), one tier lower.

**The totals column is NOT widened beyond the resource** (owner, 2026-08-13): *"we will control it
ourselves — I will declare 15,3 or 15,2"*. The room for a sum of many movements is part of what the
declaration says, not something the engine adds behind it. A sum that outgrows the declared precision
raises at posting time, which is the loud direction.

### 5h. The call, as an author writes it (2026-08-13)

The five readings' argument lists were rebuilt against the reference shape the owner supplied. Three
things changed, and the first is the one that matters:

**The order is grouped by SIDE, not by kind of argument.** It used to run "both accounts, then both
breakdowns, then the condition, then the periodicity" — tidy in the struct and wrong at the callsite,
because an author writes a side at a time: *this account, broken down like this*. The periodicity sat
LAST for the same reason it was added last, while it belongs immediately after the interval it cuts.

| Reading | Arguments, in order |
|---|---|
| `RecordsWithAccountDimensions` | Begin, End, Condition, **Order**, **Top** |
| `Balance` | **Period** (one moment), AccountCondition, AccountDimensions, Condition |
| `Turnovers` | Begin, End, **Periodicity**, AccountCondition, AccountDimensions, Condition, CorrAccountCondition, CorrAccountDimensions |
| `DrCrTurnovers` | Begin, End, Periodicity, AccountConditionDr, AccountDimensionsDr, AccountConditionCr, AccountDimensionsCr, Condition |
| `BalanceAndTurnovers` | Begin, End, Periodicity, **FillMethod**, AccountCondition, AccountDimensions, Condition |

Two placements are deliberate rather than aesthetic: the general condition sits **between** the two
sides for a turnover (the reading asks "this debit side, so filtered, against that credit side" — the
filter belongs to the reading, not to either side) and **after both** for the matrix, which names two
symmetric sides first and filters the pair afterwards. And a BALANCE takes no opposite side at all:
"the balance of 51 against 62" is not a question, because a balance is not a movement and has no
other end.

**The account became a CONDITION.** A list can only ever say "these"; a condition says what the
author means — `Account IN HIERARCHY (&Accounts)`, `Account IN (&Exactly)`, a comparison against an
account's own attribute. This is also what finally makes the fold reachable from a query (§ 8.5): the
slot is declared `m_consumedBySource`, so the predicate does **not** become a WHERE around the
reading — it arrives inside it, with the unfold word intact.

⚠ Both shapes still work, and they arrive as ONE. A script hands the slot a value (one account or a
list), and `ibAcctParseCall` turns it into the condition it means — with the word `Hierarchy`,
because a bare list of accounts has always brought the subtree and reported it under the account
named. Reading the same call as `Elements` now would quietly narrow every existing script.

⚠ And what a slot in that condition may say is **checked**: only the account, only comparisons joined
by AND. Anything else raises, naming the general `Condition` slot — because this slot never reaches
a WHERE, so a leaf nobody applied would be a filter that vanished, in the direction of reporting more
than was asked.

**Three arguments are new**, and each is implemented rather than merely declared: `Order` and `Top`
on the listing (field names with optional `ASC`/`DESC`, resolved against the source — an unknown name
raises rather than sorting by nothing), and `FillMethod` on the symbiosis, which inserts the periods
nothing moved in **before** the roll, so the balances carried across them come from the mechanism
that already exists instead of a second way of computing the same number.

⚠ **The neighbour was moved to the same order** (`ibRegTurnoverArg`, `ibRegBalTurnArg`): interval,
periodicity, fill method, condition. Two registers whose arguments run in different orders are two
things to remember instead of one. Both layouts are pinned by golden rows in
`tests/test_registerReadRules.cpp`, including the property that no two arguments share a position —
because a positional list that disagrees fails silently, and that is precisely how a declared
periodicity once pushed every call's condition into the next slot.

### ⏳ Next: the accounting flags, and they are FILTERS (owner, 2026-08-13)

Still two hard-coded booleans where there should be two declared collections (§ 4.6), and the owner's
framing sharpens what they are FOR:

| Collection | Materialises as | Members a configuration typically declares |
|---|---|---|
| **Accounting flags** — on the chart of accounts | a checkbox on every ACCOUNT | quantitative, currency |
| **Dimension accounting flags** | a checkbox COLUMN in every account's kinds table | sum, quantitative, currency |

The two lists need not agree: a chart may keep quantitative and currency at the account level while its
dimensions carry sum, quantitative and currency. And their purpose is not documentation — **they are
filters**: they say what an account (or a breakdown along one kind of it) is allowed to take, and they
are matched against the register's own **resources and dimensions**. Every field of them is BOOLEAN —
a declared set of switches, nothing more elaborate.

That is what makes `Quantity` meaningful on some accounts and meaningless on others, and it is the
missing half of the resource link already in place (`Balance` on a resource — whether a balance is kept
in that figure at all, § 5e).

Everything left undone is collected in **§ 8**, so there is one list rather than a note per landing.

## 5i. Applying it, editing it, re-applying it — one root cause, six faces (2026-08-14)

A day of clean applies, dimension additions and correspondence toggles against a live Firebird base,
read back through a per-event trace file. Every defect it found was the same sentence:
**state derived from a SETTING instead of asked of the ENTITY.** The faces below are this
register's; the ones both registers share — the totals table's NAME, the conditional declaration, the
drop of a table that stopped being maintained, and the DDL barrier's second reader — are written up
once in [register-shared-machinery.md § 4c](register-shared-machinery.md).

**The COLUMN LIST followed the correspondence flag.** `FillArrayObjectByPredefinedAttribute` chose
`RecordType` + `Account` in one mode and `Account` + `AccountCr` in the other, so clicking the
checkbox ADDED and DROPPED columns — and a dropped column takes its DATA with it: switch
correspondence off and every credit account ever recorded is gone, switch it back and the column
returns empty. Worse for the engine, the two snapshots then disagreed about a predefined attribute
that had never left the configuration, which is how *column FLDnnnn_RTRef does not exist* reached the
credit totals trigger three edits later. All three are declared always now, and the setting decides
only what is WRITTEN into them: a one-sided register fills `RecordType` and leaves `AccountCr` empty,
a correspondence one fills both accounts and leaves `RecordType` empty. Two spare columns per
register cost nothing; a column that comes and goes costs the data in it.

⭐ **The setting also RENAMES the debit side** (2026-08-15). Turning correspondence on makes a line
name both accounts, and from that moment the inherited `Account` IS the debit one — but it kept its
bare name, so the pair read `Account` / `AccountCr` while its own dimensions right beside it read
`AccountDimensionDr1` / `AccountDimensionCr1`: one fact spelled two ways in a single member list, and
which of the two accounts the unprefixed one was could only be learnt from documentation. The prefix
now follows the setting in both directions:

| Correspondence | Account | Dimension slot |
|---|---|---|
| off | `Account` | `AccountDimension1..N` |
| on | `AccountDr` + `AccountCr` | `AccountDimensionDr1..N` + `AccountDimensionCr1..N` |

Spelled once — `AccountColumnName(prefix)` / `AccountColumnSynonym(prefix)` beside the dimension's
own `AccountDimensionColumnName` — and applied on every `SyncAccountDimensionSlots`, so slots created
under the previous mode are renamed too. Safe by construction: a metaID names the physical column and
is untouched, so this changes what the SCRIPT calls the field, which is what the setting means. The
synonym travels with it through `SetOwnerSynonym` (the second sanctioned exception to "a predefined
attribute's shape is fixed by its constructor", after `SetSelectMode`) — otherwise the asymmetry just
moves from the name to the caption.

⭐ **And the unused side is MARKED, not listed.** The paragraph above about columns is a statement
about STORAGE; what a line offers a script is a different question. A one-sided register showed
`AccountCr` and a whole credit breakdown beside its single account, all writable and none ever
written. They now carry `metaDisableFlag` — the mark the platform already uses for a catalog with no
owner and an independent information register's `Recorder` — and the member walk
(`ibValueRecordSetObjectRegisterReturnLine::FillMembers`) obeys it. The columns stay declared; only
the FIELDS disappear. The same change makes an independent information register stop offering
`Recorder` / `LineNumber` / `LineActive`, and a turnover accumulation register stop offering
`RecordType` — the mark was always set, nothing read it.

⚠ Note the asymmetry that remains, deliberately: `GetGenericAttributeArrayObject` still returns
disabled predefined attributes, because that list is ALSO the schema's — dropping one there means
DROP COLUMN, not "field unavailable". Ordinary (user-declared) attributes already vanish from it when
disabled, since they arrive through `FillArrayObjectByFilter`, which tests `IsAllowed()`. Making the
two agree means splitting the list by QUESTION — "what is this table made of" vs "what fields does
this value have" — which is the arc the note over `FillArrayObjectByPredefinedAttribute` predicts.

⚠ **The same fix one level down, on the credit analytics slots.** `SyncAccountDimensionSlots` used to
CLEAR the `Cr` vectors when correspondence went off, which removed those slots from every walk and
dropped their columns at the next apply. Switching back produced the slots again — empty, under FRESH
metaIDs. The slots stay now; a one-sided register simply never writes into them.

**Slot TYPING was phase-bound, and editing is not a run.** The slots are created the moment a chart of
accounts is picked, and used to be typed only in `OnAfterRunMetaObject`. Save in that window and the
schema is built from typeless slots — a discriminator column each and nothing else — while the
configuration written moments later carries the types the next run supplies. From there the two never
agree again: the following apply sees the slots typed on one side of the diff and empty on the other,
and tries to drop reference columns that were never created. The typing is
`ApplyAccountDimensionSlotTypes()` now, called from BOTH the run and the property change — the same
shape `ApplyAccountDimensionKindType()` already had on the chart of accounts (§ 5b) — and the credit
account's own type moved onto the same rule.

**And the slot NAMES were read under the wrong spelling — the deepest cause of the whole family.**
`ReadData` looked for `AccountDimension%u` while a correspondence register writes
`AccountDimensionDr%u` (the side prefix appears whenever there are two sides to tell apart). The loop
therefore found nothing, broke on its first iteration, and left the debit slots to be RE-CREATED from
scratch — which means new metaIDs, and a metaID is what the physical column is named after. Every load
minted a fresh set: the saved configuration held slots the working one had never heard of, each
container's id counter marched on independently, and a dimension added afterwards was handed a number
a slot already owned. The prefix is now DERIVED on read exactly as it is on write, because nothing
stores the side separately — the presence of `AccountDimensionCr1` in the file is what records that
the register was saved in correspondence mode.

**Defaults, and where two properties live.**

| Changed | To | Why |
|---|---|---|
| `Correspondence` on a new accounting register | **on** | double entry is what an accounting register is FOR; a one-sided one (a register of quantities, a memo book) is the special case. Defaulting to off meant every new register started as the exception and had to be corrected into the rule |
| `SplitTotals`, **both** registers | **on** | a posting run hits the same few accounts — cash, revenue, VAT — from every document, so the contended key is the norm rather than the exception. Kept in step deliberately: two registers differing in this by accident is a difference nobody chose |
| the chart of accounts' `ChartOfCharacteristicTypes` binding and `MaxAccountDimensionCount` | from the **Data** category to **Accounting** | they are what makes a chart an accounting one — which values an account's analytics may hold, and how many slots there are. Under "Data" they were invisible where a reader looks: the Accounting section showed the account's own flags and nothing about its analytics, so it read as if it were the whole story |

**Two new save-time refusals, both REPORTED rather than thrown** — for the reason § 5e states: an
exception out of `OnSaveMetaObject` leaves the configuration write transaction open, and the next
save meets a deadlock on `sys_config_save` naming neither the rule nor the object.

| Refused | Why it is not merely a warning |
|---|---|
| an accounting register with **no chart of accounts** | the account column has no type, the analytics slots have no kinds, and every column below is derived from a binding that names nothing. The register applies cleanly and nothing can ever be written into it |
| a **recorder-subordinate register** whose `Recorder` attribute admits no document (`ibValueMetaObjectRegisterData::OnSaveMetaObject`) | the recorder column's type IS the set of documents that declared they post here. Empty, the column admits nothing — the register can be created, applied and reported on, and will never take a single row |

### How it was found — the per-event trace, and the line that was NOT there

Worth recording once, because the method is the transferable part: **reading the code was wrong three
times in a row about the same two functions**, and the trace found each cause in minutes.

`ibDiagTrace` (`backend/diagTrace.h`) appended one line per EVENT to `oes-diag.log` beside the
executable. Deliberately a file and not the logger: the binary is run without a debugger attached, and
in a GUI build the log is deferred and can lose exactly the tail of a run that ends in an error dialog
([exceptions.md § 8](exceptions.md)). **The file, its project entries and every call site were removed
when the arc closed** — what follows is the record of what it answered, not a facility to reach for.

| Line | Written by | Answers |
|---|---|---|
| `[id] SEED` / `[id] MINT` | `ibMetaData::GenerateNewID`, `ibMetaObject::OnCreateMetaObject` | which id was handed to which object, under which parent |
| `[cols]` | `ContributeTables` (`commonObjectSchema.cpp`) | what the metadata believes a table's columns are |
| `[diff]` | `DiffSnapshots` | the verdict per table — create or alter, and whether the baseline held the id |
| `[ddl]` | `ibStructureBatch` | each add / drop / alter clause, one line before the database refuses it |
| `[sql]` / `[dml]` | the L2 door (`ibDatabaseQueryBuilder`) | the statement as the database received it |
| `[err]` | `ibBackendException`'s CONSTRUCTOR | every exception raised — including the ones that were caught and swallowed, which is the class this trace exists for |

⭐ **The ABSENCE of an expected line is a reading too.** What opened the case was that there was no
`[cols]` line for the register at all — which said, before any code was read, that registers build
their column list through a different path than records do. That is why there are two `[cols]` sites
today, one per path.

⚠ It was temporary by construction, and its own header said so. It is gone: kept beyond the
investigation, it becomes a second logger nobody maintains.

### The last thing it found — a defect that was not in the code at all

The final round of this arc chased a bug that read as pure logic: a chart of accounts saved with a
**description that came back empty**, a fill check that never fired, an account type that would not
hold a value, a type field missing from a chart of characteristic types, an enumeration whose dropdown
did nothing. Reading the code answered nothing, because there was nothing wrong with it.

One trace line per attribute settled it. `Code` and `Description` are declared identically
(`ibItemMode_Folder_Item`), yet arrived as `ibItemMode_Folder`, so `PrepareEmptyObject` correctly
nulled them on an ITEM — the value was never created, and the empty string then travelled honestly all
the way into the database. The tell was one field over: `Ref` reported a select mode of **0**, and
`ibSelectMode` has no zero (`Items = 1`). A value outside a type's range cannot be produced by any
assignment to it.

The cause was a **stale-object skew** — a widely-included header edited under an incremental build, so
part of the tree read the fields one slot along. In the neighbouring declaration
`m_fillCheck, m_itemMode, m_selectMode, m_indexingMode` every member held the previous one's value.
A clean `Rebuild` fixed all of it with no code change.

⭐⭐ **And the same skew forged the schema diff.** The index set is a pure function of
`GetIndexingMode()` (`ContributeAttributeIndexes`, `<table>_<columnId>_IX`), and `m_indexingMode` was
holding `selectMode` — which is `Items = 1 = ibIndexingMode_Index` for nearly every predefined
attribute. So the broken build declared an index on **everything** — deletion mark, data version,
is-folder, off-balance, account type — published that as the baseline, and the next apply died on
`DROP INDEX … Index not found`. It presented as a drifted diff, and the fix nearly taken was to make
`DROP INDEX` idempotent. That would have been [schema-authority.md](schema-authority.md) inverted:
a physical guard does not repair a divergence, it LEGALISES one — and here it would also have muted
the single signal by which the skew was found. **A DDL that fails with "the object is not there" is
first a question about what computed the snapshot.**

Two rules worth more than the bug:

* an out-of-range value in a neighbouring field means you are reading the wrong field — suspect the
  BUILD before the code, and a clean rebuild costs minutes against hours of reading;
* a field written only in constructor initialiser lists, and nowhere else in the tree, cannot hold a
  different value at run time by any argument the code makes. One `grep` establishes that, and it
  rules out the whole family of explanations that looked plausible (the `Create*` helpers differ by a
  single `bool`, and `ibItemMode` converts to it silently — a convincing and entirely wrong theory).

## 5j. Why every apply re-added the same columns — 2026-08-15

Found from a log Max collected, and it is worth reading as a whole because **no layer of it
was wrong on its own**. The symptom was `RDB$INDEX_15 violation` on `FLD1080_TYPE` — the
database refusing to add a column that was already there — and it reproduced on a register
created from scratch minutes earlier, so nothing could be blamed on an old base.

The log said it plainly once the DDL was read in order: `CREATE TABLE AccountingRegister1096`
already contained `fld1108…fld1113`, and eight seconds later `ALTER TABLE … ADD fld1108_TYPE`
asked for the same six slots. One apply, both statements.

The chain, from the bottom:

| # | What | Where |
|---|---|---|
| 1 | A property **absent from the file** was read as the type's zero, so `Correspondence` and `SplitTotals` — both default-TRUE — arrived false | `ibPropertyBoolean::ReadNodeValue`, and every other type alike |
| 2 | Correspondence false ⇒ the credit block of slots is not written | `WriteData` |
| 3 | Reading derived the debit slots' NAME from whether `AccountDimensionCr1` was in the file. No credit block ⇒ look for `AccountDimension1` while the file holds `AccountDimensionDr1` ⇒ the very first `FindProperty` answers null ⇒ the loop breaks on iteration one ⇒ `m_accountDimensionCount == 0` | `ReadData` |
| 4 | The schema snapshot therefore knows of **no slots**, while `ContributeTables` still builds them ⇒ the differ emits `ADD` every time | `DiffSnapshots` |

Step 3 is why the failure was total rather than partial: losing the credit half cost the
**whole** breakdown, debit included.

**Fixed at 1 and 3, which are the two that are wrong in themselves.**

*1* — the empty-value gate moved into the non-virtual door
(`ibBackendProperty::SetNodeValue`), so no property type ever sees an absent value and every
default-true switch in the tree is protected at once, not just this register's two. The
virtuals went `protected` so the gate cannot be bypassed.
See [property-system.md § 6](property-system.md) and `tests/test_propertyDefaults.cpp`.

*3* — each slot is now looked up under **both** spellings, `AccountDimensionDr<N>` then
`AccountDimension<N>`, which is what the comment there always promised ("fall back to the bare
one") and what the code did not do. The kind half derives its name from the slot's own name, so
the pair cannot end up under different spellings. A neighbouring block is no longer evidence
about this one.

⚠ **Diagnostic worth keeping.** *The same `ALTER … ADD` repeating two or three times in one
log, on columns a `CREATE TABLE` in the same run already listed*, means the snapshot and the
DDL disagree about what exists — not that the base is damaged. Look at what the snapshot
walks, not at the differ.

---

## 6. Order of work

> **All seven steps are done** — 1-3 on 2026-08-12 (§ 5a), 4-7 on 2026-08-13 (§ 5e); step 6's hierarchy
> half was finished the same evening, when the subtree stopped merely FILTERING and started folding
> into the account that was named (§ 5e). Kept as written because the ORDER is the argument:
> grooming first, so postings enter and live before anything expensive is built on top of them.

Grooming first — it is genuinely small, and it makes the register usable before any of the
expensive parts:

1. **Names** — `Subconto` → `AccountDimension` / «Аналитика» (14 files in the engine, plus the
   three locale catalogues and the docs).
2. **Value type** — `TypesOfCharacteristics` instead of a reference to the CCT (§2).
3. **Slot count** from the chart of accounts (§4.2) — the three fixed members become a vector
   built from the number, and the schema snapshot derives the slots instead of reading members.
   The correspondence property (§4.3) rides here: it decides whether the vector is built once or
   once per side, and both land in the same restructuring.
4. **Addressing by kind** (§4.4) and the runtime narrowing that comes with it — the movement's
   dimension collection resolves a kind to a slot through the account's kinds table.

After those four, postings enter and live, and a trial balance is a plain query over the
movements table — which is also how the grooming gets verified without touching the read path.

Then, and only then, the expensive half:

5. **The read side onto L3** — three source descriptors + `Compute*`, and the four manager
   methods become thin wrappers over the same sources; the `#if 0` blocks and the raw SQL are
   DELETED rather than revived.
6. **Account hierarchy + unfolded balance** — the only genuinely accounting-specific work here.
7. **Totals** — two guarded accumulations (debit / credit) into the machinery that was built with
   this register in mind (`register-totals-strategy.md` §4a).

---

## 6a. Why it stalled — the foundation did not exist yet

Worth recording, because it changes how the remaining work should be estimated: this register was
written **before the query engine was**. Its own comment says as much ("the last raw-L1 holdout"),
and it explains every shape in it — SQL assembled as strings, aggregates as manager methods,
totals absent. There was nothing underneath to build on, so the author built downward instead.

Nearly everything the register needs has landed since:

| What accounting needs | State |
|---|---|
| A source-agnostic query engine (L3) + composition (L5) | built; the whole ladder L1–L5 |
| Register virtual tables AS QUERY SOURCES | built for accumulation — descriptors + call-scoped companion queryables, i.e. a working template |
| Trigger-maintained totals | built AND numerically verified against re-aggregated movements; designed with a debit/credit split in mind from the start |
| Exact decimal arithmetic | `ibNumber` — 200+ fractional digits, exact; a currency amount does not drift |
| Organisation / tax separators as a standing cut | the register's own **dimensions** (§4.6) — nothing new is needed. ⚠ NOT common attributes: registers are deliberately outside that composition (`IsCompositionAllowed`, [common-attributes.md](common-attributes.md)); a common attribute serves documents and catalogs, while a register states such a cut as a dimension of its own |
| Filtering by organisation without writing it into every query | session parameters + the `Restrict` access policy |
| Restructuring when the schema changes | `DiffSnapshots` + `ibStructureBuilder` — the migration engine already IS one |
| Rolling an aggregate along a hierarchy | `ibQueryDimUnfold` (`Elements` / `Hierarchy` / `HierarchyOnly`) |
| A neighbouring field lending its type to an editor | `ibValueFilterItem::GetRightTypeDescription()` in list settings |
| "Declared once, materialised in many objects" | `ibCompositionObject` — the pattern the accounting-flag collections need |
| Recalculation / period jobs run in the background | the job manager, with schedules and cross-process claims |
| Packing a value for storage or transfer | `ibValue::Serialize(node)` over `ibDataNode` |

So the blocker of the "there is nothing to build this on" kind is **gone**. What remains is work
inside the register itself, plus four platform pieces that genuinely do not exist yet:

| Missing platform piece | Weight | Nearest thing to lean on |
|---|---|---|
| ~~A variable set of predefined attributes~~ — **it already exists** | small — reuse, not invention | `CreateMetaObjectAndSetParent<ibValueMetaObjectAttributePredefined>(…)` is an ordinary factory call, and `FillArrayObjectByPredefinedAttribute` takes a **vector**. Nothing requires a predefined attribute to be a fixed member — today's fixity is only in HOW the vector is filled (by listing members). Building N of them in a loop needs no new machinery, and predefined ACCOUNTS are supported too (`EditPredefinedValues`) |
| **Declared flag collections** materialising as checkboxes on accounts and as COLUMNS of the kinds table | small–medium — the same reuse, one level down | same factory: a flag is a predefined boolean attribute created per declared member |
| **Output schema computed from CALL ARGUMENTS** (`DescribeOutput` over the requested kinds, §7) | small–medium | `CreateQueryable(paParams, lSizeArray)` already receives the arguments; describe must follow them |
| **Totals whose KEY depends on data** (`SummaryOnly` narrows the balance key per account, §4.6) | small for live reads, one more rebuild trigger for materialised ones — storage is untouched | the totals machinery handles two tables with different keys and already knows how to regenerate a bundle |

Two small ones beside them, both inside the register: the double-entry check per recorder (absent,
and it does not apply to off-balance accounts, §4.6), and the link from a resource to the
accounting flag it belongs to.

A full item-by-item audit of §4 against the tree is in **§7.0** — read it before estimating
anything here.

**Net conclusion after checking each one against the code: no HEAVY platform piece remains.**
The variable attribute set turned out to be an existing factory plus an already-vector contract;
the flag collections are that same factory one level down; the per-call output schema rides an
argument list the descriptor already receives; and the data-dependent balance key costs one more
entry in the list of things that invalidate a materialised bundle — with live aggregation it is
just a flag read while the query is built. What is left is work INSIDE the register, on
foundations that are all in place.

---

## 7. The virtual tables — what they must offer

The read side is not four methods on a manager; it is a family of query SOURCES, the way the
accumulation register already vends `.Balance` / `.Turnovers`:

| Source | Gives |
|---|---|
| `Balance` | closing balance per account (+ dimension breakdown), at a moment |
| `Turnovers` | debit and credit turnover over a period |
| `DrCrTurnovers` | turnover BETWEEN two accounts — only expressible in correspondence mode (§4.3) |
| `BalanceAndTurnovers` | opening / debit / credit / closing in one row |
| `RecordsWithAccountDimensions` | the movement lines themselves — period, recorder, line — with the dimension slots widened into a column per kind |
| `AccountDimensions` | the *(kind, value)* pairs a movement carries, decoded — the breakdown a reader joins back to a line |
| chessboard | the correspondence matrix — a fold of `DrCrTurnovers`, not a further source |

⚠ **As built (§ 5e), the register registers five of these**: `Balance`, `Turnovers`,
`BalanceAndTurnovers`, `RecordsWithAccountDimensions`, and `DrCrTurnovers` in correspondence mode.
`AccountDimensions` was not written as a source of its own: asked for no kinds,
`RecordsWithAccountDimensions` already publishes each slot with its `…Kind` column beside it, which is
the same pairs against the same line.

### 7a. Two classes of source, and only one of them is a total (settled 2026-08-12)

The set above is **five virtual tables plus the movements table itself**, and it splits in two by
what it reads. That split is what keeps the expensive half small:

| Class | Members | Reads | Materialised |
|---|---|---|---|
| **folds** | `Balance`, `Turnovers`, `DrCrTurnovers`, `BalanceAndTurnovers` | the totals views | yes — the trigger-maintained bundle |
| **breakdowns** | `RecordsWithAccountDimensions`, `AccountDimensions` | the MOVEMENTS table | **no, by construction** |

⚠ **As built, `DrCrTurnovers` reads the movements unconditionally** (`ComputeDrCrTurnover` — there is
no `useTotals` branch in it), and that follows from the totals' own shape rather than from unfinished
work: a totals row is keyed by ONE account and the other side of the movement was never stored beside
it, so a row that IS a pair cannot come from there. It is a fold by class and a movements reader by
routing — the same rule § 5e states for a balance filtered by the opposite account.

> ⚠ **"Reads the movements" is not "computes in RAM", and the two were conflated here until
> 2026-08-13.** Its GROUP BY and its sums always went to the server; what was in RAM was the ending —
> the rows were materialised before anything could be composed with them. Since `BuildRelation()` it
> hands the door a relation like the other three (§ 8.3a), from the movements, which is where a paired
> row can be assembled at all.

The breakdowns are not an omission from the totals — they are BELOW the grain totals are stored
at. Recorder and line number are precisely what a fold discards
([register-totals-strategy.md § 4d-bis](register-totals-strategy.md)), so a table that reports
them can only be the movements. It therefore needs no trigger, no bundle, no regeneration and no
parity check: it is a projection of a table that already exists and is already queryable.

**Both breakdowns are the same machinery as §7.1, and that is the whole point.** The slot → kind
`CASE` assembly is the one genuinely new piece the accounting queryable owes; adding these two
sources does not add a second one, it gives the first one two more tenants. Four consumers now
share it: the folds' dimension breakdown, the two breakdown tables, and the field tree that
offers the columns.

⚠ `DrCrTurnovers` is the one member of the list gated on a DECISION rather than on work — and the
disabled code shows what happens when the decision is skipped.

`accountingRegisterManager_impl.cpp:603-610` obtains the pair by **self-joining the movements on
the recorder** — `INNER JOIN … ON dr.Recorder = cr.Recorder`, with `dr.RecordType = 0` and
`cr.RecordType = 1`. That reads as correspondence and is not: a recorder carrying M debit lines
and N credit lines yields **M × N pairs**, and each debit amount is then summed N times. It is
exact only for a document whose posting is a single Dr/Cr pair, which is the shape a real
configuration leaves behind immediately (any document that debits two accounts from one payment
already breaks it).

So the choice is not "correspondence or a cheaper equivalent". It is: store both sides on the
line (§4.3), or do not offer `DrCrTurnovers` at all. A join over a recorder cannot recover which
debit answered which credit, because that information was never written down — a one-sided model
discards the pairing at write time, and no read can invent it back.

Two parameters beyond the usual period / account filter:

- **filter by dimension KIND** — restrict to postings carrying a given kind (and value);
- **the breakdown list** — a caller hands in the kinds it wants the result cut by, and the result
  carries a dimension column **per requested kind, in the order they were passed**.

That second one is the structural difference from every other register in the tree: the OUTPUT
SCHEMA of the source depends on the ARGUMENTS of the call, not on the metaobject. The physical
slots (§4.2) are storage; the result columns are a projection chosen per query, and the two need
not agree in count or order.

The mechanism for this already exists and needs nothing new:
`ibQueryableSourceDescriptor::CreateQueryable(ibValue** paParams, long lSizeArray)` builds the
call-scoped queryable FROM the call's arguments — the accumulation register uses it for its
period argument, and it is exactly the seam a per-call column set needs. What the accounting
register adds is that `DescribeOutput` must be computed from those arguments rather than from
fixed members.

### 7.0 Audit — what already exists to pull in

Every decision in §4 was checked against the tree. The result is that almost nothing here is
construction; it is connection.

| What the decision needs | What is already there | Verdict |
|---|---|---|
| A variable number of predefined attributes | `CreateMetaObjectAndSetParent<ibValueMetaObjectAttributePredefined>(…)`, and `FillArrayObjectByPredefinedAttribute` takes a vector | connect |
| **The attribute set depending on a metaobject property** | **exact precedent next door**: the accumulation register does `if (GetRegisterType() == eBalances) array.push_back(RecordType…)` over an `ibPropertyEnum` — the composition of predefined attributes ALREADY varies by a declared property | copy the shape |
| A number declared on the chart of accounts | `ibPropertyNumber` (and `ibPropertyBoolean`) on the metaobject | connect |
| Correspondence as a register property | the same `ibPropertyEnum` + the same conditional fill | connect |
| Different physical shapes per mode | precedent again: `GetRegisterTableNameDB(rType)` gives `_T` / `_Tn` per register kind | copy the shape |
| Flag collections as tree branches | a branch is children of a given clsid + `FillArrayObjectByFilter<…>(array, { clsid… })` — the pattern behind dimensions / resources / attributes | new metatype, standard path |
| Slot type from the CCT contour | `ibValueMetaObjectChartOfCharacteristicTypes::GetTypeDesc()` | one line |
| Slots visible to queries | an attribute IS-A column; `GetColumns()` returns the generic attribute array | free |
| Slot → kind conversion (§7.1) | `ibQueryAstExpr` already has a `Case` node (`CASE WHEN p THEN e … ELSE e END`), so the expression renders to SQL like any other | assemble the expression |
| Output schema without executing | `ibQueryLowering::DescribeOutput(ast, params, schema)` — already used by the dynamic list | use as is |
| Virtual tables as sources | accumulation's descriptors + call-scoped companion queryables | copy the shape |
| Totals, and regeneration when they change | the totals strategy, numerically verified | copy the shape |
| Account hierarchy in aggregates | `ibQueryDimUnfold` (`Elements` / `Hierarchy` / `HierarchyOnly`) | connect |
| Narrowing the value editor by the neighbouring kind | `ibValueFilterItem::GetRightTypeDescription()` + dot-path binding | connect |

**What genuinely has to be written**, and all of it is small and inside the register:

1. the `AddDebit` / `AddCredit` verbs (an `AppendFunc` pair plus the side-setting body);
2. the dimension collection addressed by kind (§4.4) — a script-visible object over the slots;
3. the slot → kind `CASE` assembly (§7.1) — the one real piece of machinery;
4. reading what the account declares: type, accounting flags, off-balance, turnovers-only;
5. the double-entry check per recorder, skipping off-balance accounts.

### 7.1 The conversion: numbered slots → columns by kind

**Why this is the whole point, in the owner's words (2026-08-12).** What a user edits on an account
card is the KIND — the type of the slot is fixed by the platform (the characteristic chart's
composition) and is not theirs to change; the kind they pick travels into the movements with the
value. Reporting is then built **by kind, never by position**: a reader asks for a breakdown by
contractor and contract, and it does not matter that on one account the contractor is the second row
of the kinds table and on another the first. The order in an account's table is data, the requested
order is the reader's, and the engine reconciles them — which it can only do because each stored row
says what kind its value was filed under. Take that column away and the question "which slot holds
the contractor here" has no answer that does not depend on the account's table as it stands TODAY,
including for rows written before it was last re-ordered.


This is the one piece of real machinery the accounting queryable owes, and it is what the stored
kind (§4.4) exists for. Storage is positional; the result is by kind; and the position of a kind
DIFFERS BETWEEN ACCOUNTS — "counterparty" is slot 1 on one account and slot 2 on another. So the
queryable cannot map a requested kind to a fixed column; it has to select among the slots by what
each row says its kind is:

```sql
CASE WHEN AccountDimension1Kind = &Kind THEN AccountDimension1
     WHEN AccountDimension2Kind = &Kind THEN AccountDimension2
     …
END AS <the requested kind's name>
```

One such expression per requested kind, N branches wide, per side where correspondence is on.

Three properties make this the right shape rather than a workaround:

- **it is an ordinary expression**, so it renders to SQL, and grouping / filtering by a dimension
  value is grouping / filtering by that expression — the whole thing pushes to the server instead
  of folding in RAM;
- **it explains the storage decision**: had the kind been derived from the account's kinds table
  by position instead of stored, this conversion would need a join to that table per slot, per
  row, and would break the moment an account's kinds were re-ordered;
- **it is where the per-call schema comes from** — the requested kind list decides both how many
  such expressions exist and what they are called, which is exactly §7's "output schema follows
  the arguments".

### 7.2 A fold must name the resource's own type (2026-08-11)

The dr/cr readings summed money through a bare `CAST(… AS NUMERIC)`. On Firebird a bare `NUMERIC`
**is `NUMERIC(9,0)`** — the fractional part is not rounded away at the edge of a long calculation,
it is dropped on every fold, and a total over nine digits overflows. 24 such casts existed across
the accounting register's queries.

They now go through `ibRegFoldNumeric(resource)`, which spells the type from **the resource's own
declared precision and scale** through the dialect's `m_typeNumberPattern` — the same pattern the
DDL uses to create the column. The cast can no longer disagree with the storage it reads, because
both are the same sentence about the same attribute.

The general rule: **a cast that names a type the schema already declares is a second declaration**,
and the two only stay equal while nobody edits either. Ask the attribute.

---

## 8. Known gaps

### 8.0 The short list, as of 2026-08-14

Everything below is written up in its own section; this is the index, so nothing lives only in a
conversation. **`Debug|x86` is green** (one class of error in the whole batch — a comparer called
without its namespace).

| Open | Where | Why it is not closed |
|---|---|---|
| ~~the LIVE run~~ — ✅ **done 2026-08-14** | § 5i | applied on a clean base, a dimension added to an already-applied register (which is what exercises the DDL barrier), correspondence toggled both ways, entries posted, the totals read through a query tool. Six defects of ONE class, none of which the SQLite suite could have seen |
| `FoldOutSummaryOnly` on the server | § 8.3 | a second join — to the account's kinds table, per slot — plus an outer aggregate. Expressible in what exists; the gate closes the road meanwhile |
| `BalanceAndTurnovers` with a periodicity, on the server | § 8.3 | needs a RUNNING SUM per period, i.e. a **window function** — and the L2-1 IR has no window node at all (`ibQueryExprKind` does not know one). Work in the L2 core, gated per driver by `m_features.m_window` |
| the grain rule stated twice | § 8.3a | collapses only when the predicate cut is DERIVED from the filled read spec instead of written beside it |
| the four extracted bundle helpers are not unit-tested | [register-shared-machinery.md § 2.2](register-shared-machinery.md) | each takes metadata, and the suite builds no metaobjects. Covered by the live apply |
| `Subordination` vs `Items` on a chart of accounts | § 5a, § 8.5 | owner's call: since `GetHierarchyColumn` answers `HasParentLink`, the two differ only in whether an item may be ENTERED by declaration |
| the new figure captions are not EXTRACTED; the ones already there were wrong | § 5h | ⚠ Corrected 2026-08-14 — "no translations" was the wrong diagnosis and the reassuring one. The figure msgids that predate `ibRegFigureCaption` were **present and mistranslated in `ru.po`**, by fuzzy auto-fill: `Opening balance` → «Открытие» (an action, not a figure) and `Off-balance` → «Баланс», the exact opposite of what it means. Fixed with the platform's own vocabulary (owner, 2026-08-14: **`Balance` = «Остаток»**, never «Баланс» — the same word the accumulation register uses): Остаток / Остатки / Начальный · Конечный остаток / Оборот Дт · Кт / Забалансовый. `uk.po` was correct throughout — the broken one was the native language. Still open: the NEW captions are not extracted into the template, and `.po` is stale (18 references point into `accountingRegisterManager_impl.cpp` at lines up to 1029; the file is 117 lines since the raw SQL was deleted). 78 fuzzy entries remain in `ru.po` |
| the pointer catch-all is unproven | `compiler/value.h` | `template <class T> ibValue(T*) = delete` produced **no** compile error across 380 files. That may mean the tree is clean, or that the SFINAE condition excludes more than intended. One deliberate bad call would tell the two apart — until then it is not evidence |
| no tests on: the consumed-condition channel, the two relation endings, `Order` / `Top` / `FillMethod` | — | the argument LAYOUT is pinned (`tests/test_registerReadRules.cpp`); the behaviour behind these three is not |

Checked against the tree on **2026-08-13** after § 5e, and re-read on **2026-08-14** after § 5i.
Each entry is something that is ABSENT, not
something untested; where the gap is already named in a comment, the file is given so the note and the
code stay findable from each other.

### 8.1 The accounting flags are still fixed booleans (§ 4.6)

| What the tree has | Reader |
|---|---|
| `Quantitative`, `Currency` — predefined boolean attributes on every account (`chartOfAccounts.h:256-260`); these are the two that belong in a declared collection | **none**: `GetQuantitative()` / `GetCurrency()` have zero callers |
| `OffBalance` beside them (`chartOfAccounts.h:253`) — a PLATFORM flag that never becomes a collection member (§ 4.6) | the double-entry check, and the account's form (§ 5d) |
| `SummaryOnly` — one per-kind flag on a row of the account's kinds table | read: it narrows the balance key (§ 5e) |

What is wanted instead is **two DECLARED collections of booleans**, and their point is not
documentation:

- one declared on the chart of accounts, materialising as a checkbox on every ACCOUNT;
- one declared for the breakdown, materialising as a checkbox COLUMN in every account's kinds table.

The two lists need not agree, every member is an ordinary boolean, and **they are FILTERS**: a flag is
matched against the register's own **resources and dimensions**, so a resource means something on an
account only where the account — or the breakdown along that one kind of it — carries the flag that
resource belongs to. **Where the accounting is not kept, the figure is zero:** an account that keeps no
quantity contributes nothing to a quantity column, and the column stays where it is for the accounts
that do. That is the missing half of the link `Balance` on a resource already provides (whether a
figure carries a balance at all, § 5e).

Not construction: a branch is children of a given clsid plus `FillArrayObjectByFilter`, the pattern
dimensions, resources and attributes already follow, and a flag materialises as a predefined boolean
attribute created per declared member — § 7.0 lists both. The owner's framing is in § 5e.

### 8.2 A boundary that names a DOCUMENT is honoured by its date half only

> **Partly closed 2026-08-13 — by the ROAD, not by the gap.** `Turnovers` fills the read spec now
> (§ 8.3), and `ibRegFillArmCut` puts the recorder into `m_boundaryTail`, decomposed through the same
> write codec the rows were stored with, so on the server road the lexicographic tuple comparison is
> made and the document IS honoured. Every call a gate sends back to the RAM road keeps the date-only
> behaviour described below, on any of the readings. That is exactly the
> shape the diagnosis at the end of this section predicted: the fix arrives with the SPEC, not with a
> new comparison invented for a boundary.

`accountingRegisterMetadataTotals.cpp`, above the arm cut: the date is applied, the recorder is not,
and the code says so rather than dropping it silently. What it costs is exactly what a point in time
exists for — three postings sharing one instant cannot be told apart, so "the balance as of THIS
document" answers about the moment instead, which is a plausible wrong number rather than an error.

The comparison needs the recorder's field tuple treated as an ORDERING, over the same decomposition
the rows were written through; `ibRegRecorderTuple` (`registerQueryLowering.h`) already builds that
tuple for the L2-2 read spec and is the piece to lean on.

**The boundary is the ROW'S OWN KEY, and that is the whole framing** (owner, 2026-08-13). A movement is
made by a document, and the document OWNS it: while the recorder lives the record lives, and without it
the record cannot exist at all. Beside it the period is always written. So a register's row is ordered
by the triple **period · recorder · line number** — which is literally what the register vends as its
key (`ibRegisterDataQueryable::GetPrimaryKeyColumns`: recorder, line number, period) — and reporting is
going to stand on that triple. "The balance as of THIS document" is therefore not a special comparison
invented for a boundary; it is a comparison over the ordering the rows already have.

That is also why this is not a missing feature so much as a missing ROAD: the accumulation register
already answers it, through the read spec (`m_boundaryTail`), with the tuple decomposed by the same
codec the rows were written through.

**⚠ Why it did not land with the rest (looked at properly on 2026-08-13).** The piece to lean on is on
the wrong side of a type boundary. `ibRegRecorderTuple` produces `(field name, expression)` pairs for
an **L2-2 read spec**, and the accounting readings do not build one — they build an **L3 predicate**
(`ArmCutAtMoment` / `ArmCutOverRange`). And `ibQueryPredicate` speaks COLUMNS: its leaf is
`ibQueryCondition{ col, op, value }`, where `col` is an `ibBackendQueryColumn*`. A recorder's
individual FIELDS are not columns — they are the spread of one column — so a lexicographic tuple
comparison cannot be said in the predicate form at all. Comparing the recorder column against a
reference VALUE with an ordered operator is not the same statement: `ibRegCompositeIR` AND-joins the
same operator across every value field, which is "all fields ≤", not "the tuple is ≤".

So § 8.2 is one of two things, and both are bigger than the gap they close:

- give `ibQueryCondition` an optional EXPRESSION left-hand side (`ibQueryColumnExpr::ColField` already
  names a field of a column, and the RAM evaluator already refuses what it cannot answer) — a change to
  a core type that the DB provider, the RAM evaluator and the access policy all read; or
- give the accounting readings `GetSourceRelation` (§ 8.3) so they fill the read SPEC instead, where
  the tuple is already expressible — which is the collapse § 8.3 describes, arriving from the other end.

The second is the one that pays for itself, and it is the same conclusion § 8.3 reaches: these two gaps
are one arc, not two.

### 8.3 The server road — all five readings are on it; what the gates still send back

> **Partly closed 2026-08-13.** `ibAcctTurnoverQueryable` now overrides `GetSourceRelation` and fills
> the neighbour's read spec, so the aggregation runs on the server and the arm cut comes from the
> shared `ibRegFillArmCut` — which is also how a boundary that names a DOCUMENT starts being honoured
> (§ 8.2). **All five readings hand over a relation as of 2026-08-14** (§ 5e); the gates below belong
> to the three that stand on the STORED surface, and each is a question that surface cannot hold —
> `CanReadOnServer()` is false unless the driver materialises at all, the register is one-sided
> (correspondence keeps two tables, one per side, and a read spec reads one relation), no breakdown
> was asked for BY KIND (that is a `CASE` over the slots where the spec takes column names), the
> condition is empty (its slot half is built per side) and the fold is not finer than the stored
> grain. `Balance` and `BalanceAndTurnovers` add `FoldOutSummaryOnly`, and the latter a periodicity.
> Everything a gate refuses keeps the RAM road, which answers the same numbers.
>
> **THREE FORMS, ONE STORED SURFACE** (owner, 2026-08-13, and the platform's own vocabulary matters
> here: it does not know the word *saldo* — it has REMAINDERS and TURNOVERS, the same words the
> accumulation register uses). What is materialised is the TURNOVERS; the remainders are that surface
> summed up to the moment, and "remainders and turnovers" is the same scan read three ways at once —
> `BeforeFrom` for what was carried in, `InRange` for what moved, `UpToTo` for what remains. The
> neighbour does exactly this and needs **no join and no window** for it
> (`ibBalanceAndTurnoverQueryable::GetSourceRelation`).
>
> **What still holds the remainders back is not the arithmetic — it is what the ACCOUNT says.** Two
> **⚖ THE FORK IS DECIDED (2026-08-13): the spec does NOT grow — the reading builds the relation.**
> `RenderMaterializedRead` already returns an `ibQueryRelPtr`, and its own contract says what that is
> for: *"the caller drops it straight into a FROM, so everything around it (JOIN, outer WHERE, paging,
> RLS) is ordinary SQL the engine handles."* So the materialised read stays what it is — ONE surface,
> with its two arms, its floor and its boundary tuples — and the joins go AROUND it, in the L2-1
> relation tree that already has `ibJoin` / `ibAggregate` / `ibCase` / `ibSubquery`. Three reasons, in
> order of weight: the spec would gain a second subject (a surface AND the meaning of what is in it);
> the joins are wanted by ONE tenant, since the accumulation register has no chart of accounts to
> join to, and a shared shape that serves one caller is not shared; and the relation tree can already
> say all of it, so growing the spec would be a second way to say something the tree says.
>
> The shape, then, is: `RenderMaterializedRead` (key + `<Res>BalanceDr/Cr` as `UpToTo` sums) → `ibJoin`
> to the chart of accounts → `ibProject` whose figure columns are a `CASE` over `AccountType`
> (`Active`: `Dr-Cr`, `0`; `Passive`: `0`, `Cr-Dr`; `ActivePassive`: as they stand). `FoldOutSummaryOnly`
> is the second join — to the account's kinds table, per slot — and the cheapest gate for it is a
> question to the DATA rather than to the schema: *is any kind anywhere marked turnovers-only?* If none
> is, the fold is a no-op and the road opens for every register with analytics too.
>
> ✅ **BALANCE LANDED 2026-08-13** — `ibAcctBalanceQueryable::GetSourceRelation`, exactly that shape.
> The concern about the join's `ON` dissolved rather than being measured away: it is not spelled at
> all. Both sides ask the SAME function for their physical field (`ibRegValueField` — the movement's
> account column, the chart's own row reference), so the equality holds by construction instead of
> because two suffixes happened to match. Writing `_RRRef` by hand would have been a comparison that
> compiles, joins nothing, and reports an empty balance indistinguishable from "no movements".
>
> Three decisions inside it are worth naming:
>
> - **the join is LEFT.** An account row that is missing — a chart edited under a posted register —
>   must not make its figures vanish. The `CASE` then falls through to *do not fold*, which is the
>   answer that loses nothing;
> - **the gate asks the DATA, once per reading.** `FoldOutSummaryOnly` is the fold still without a
>   server form, and it is a no-op unless some kind is actually marked turnovers-only — so the gate
>   asks whether any is, rather than closing the road for every register that merely HAS analytics.
>   The answer is cached on the companion, which is call-scoped, so it lives exactly as long as the
>   reading does;
> - **only balance-bearing resources are projected.** A resource that keeps no balance publishes no
>   balance column at all, so there is nothing to fold and nothing to name.
>
> ✅ **AND `BalanceAndTurnovers` WITH IT** — the same surface read three ways at once: `BeforeFrom`
> for what was carried in, `InRange` for what moved, `UpToTo` for what remains. One pass, no join
> between the three and no window. Two rules it does NOT share with the balance:
>
> - **the fold applies to the balances only.** Opening and closing are folded by the account's type;
>   the turnovers are not, and must not be — a debit turnover and a credit turnover are two things
>   that happened, and netting them would report neither. The RAM reading folds exactly those two
>   pairs and leaves the movement figures alone;
> - **a periodicity closes the road**, and for arithmetic rather than effort. The spec keeps the
>   period OUT of the key — one row per key for the whole interval — which is what makes `BeforeFrom`
>   and `UpToTo` mean "before the interval" and "through its end". Asked for rows per month, each
>   row's opening balance would have to be measured against ITS OWN month: a running sum over the
>   periods, not a conditional sum over the interval. The same spec would answer, with the interval's
>   opening balance repeated on every row — plausible, and wrong on every row but the first.
>
> ✅ **AND THE OTHER TWO LANDED 2026-08-14** — `DrCrTurnovers` and `Records` (§ 8.3a). They were never
> gated the way these three are: they stand on the MOVEMENTS, so no question about the stored grain,
> the driver's materialisation or a breakdown-by-kind applies to them at all. What was RAM about them
> was the ENDING — the rows were materialised before anything could compose with them — and the door
> gained the two non-terminal endings that fixed it (`BuildRelation` / `BuildReadRelation`).
>
> ⏳ Still on the RAM road: every call one of the three gates above sends back. That is the fallback,
> not a leftover — it answers the same numbers and is the oracle the server road is checked against.
>
> Below, the two folds as they stand in the RAM reading — the oracle the server road has to match:
>
> steps read the chart rather than the surface: `FoldSideByAccountType` (active folds into debit,
> passive into credit, active-passive is left alone) and `FoldOutSummaryOnly` (a kind marked
> turnovers-only leaves the key, and the rows that then coincide are merged). Both run per row after
> the aggregation, and a reading that hands the door a finished relation has nowhere to put them. On
> the server they are a JOIN to the chart of accounts (its `AccountType` column, read through a
> `CASE`) and a JOIN to the account's kinds table (its flag deciding whether the slot enters the
> GROUP BY at all) — both expressible in the L2-1 relation tree, and neither expressible in
> `ibMaterializeReadSpec`, which knows one relation and no joins. **That is the next step, and it is a
> step of the SPEC, not of the register.**

### 8.3a The grain rule is still expressed TWICE

**All five** readings hand the door a relation now, and they do it over TWO different sources. Three
stand on the stored surface — `Turnovers`, `Balance`, `BalanceAndTurnovers` (§ 8.3). The other two
read the MOVEMENTS, and not for want of finishing: what they report is what a fold discards.

**`DrCrTurnovers` reads the MOVEMENTS, and that was never the RAM part of it.** A totals row is keyed
by ONE account and the other side of the movement was never stored beside it, so a row that IS a pair
cannot come from there — but the GROUP BY over the movements and its sums have always gone to the
server. What was RAM was the **ending**: the rows were materialised before anything could be composed
with them, so a query joining to this reading or filtering over it worked against a snapshot.

That needed one thing the door did not have: a way to say *"build the aggregate and hand it back
instead of running it"*. `ibDataQueryBuilder::BuildRelation()` is that — and it is the SAME assembly
the execute path uses, stopped one step earlier (`ibDbTableProvider::BuildAggregateQuery`), never a
second lowering beside it. A hand-rolled `ibQueryRel` for this reading would have been exactly that:
a second way of building the same SQL, free to disagree with the first about a join, a spread or a
HAVING.

⚠ The door refuses to hand out a relation for a query carrying an access POLICY. A policy folds its
restriction into the read it guards, and a relation given out unrestricted could be composed past it;
such a reading falls back to rows, which answer the same numbers under the same restriction.

**And `Records` with them — the fifth.** It is a PROJECTION rather than an aggregate (a movement line
is already as fine as this data gets), so it lowers through the read path, not the GROUP BY one:
`ibDbTableProvider::BuildReadRelation`, which is `BuildPageIR` with the page taken off. That absence
is the decision — paging belongs to whoever composes with the relation, and a `LIMIT` baked in here
would cap a join's input to one screenful and read as "the register has forty movements".

So the door has two non-terminal endings, chosen by the SHAPE of the query exactly as the terminal
ones are: a query that names aggregates or group keys folds, one that names neither projects.

Every reading of the neighbour already handed over a relation
(`accumulationRegisterMetadataTotals.cpp`).

⚠ The RAM readings do not go away with that, and are not meant to: every gate that says no falls back
to them, and they are the ORACLE the server road is checked against. Two roads answering the same
numbers is the property; one road is what a bug looks like.

The cost is not only speed. One rule — *take the stored rows BELOW the boundary's grain, and the
movements of that grain up to the boundary* — now exists in two forms:

| Form | Where | Whose |
|---|---|---|
| fills a materialize read spec (floor, head split, boundary tuples) | `ibRegFillArmCut` — `registerQueryLowering.h` | the accumulation readings |
| a predicate over the view's two arms, the recorder column telling them apart | `ArmCutAtMoment` / `ArmCutOverRange` — `accountingRegisterMetadataTotals.cpp` | the accounting readings |

Two expressions of one rule, and the day either changes the other goes on compiling. Collapsing them is
what the server-side reading buys: the accounting reading fills the same spec instead of writing its
own predicate. The three stored-surface readings have made that trade; the second form survives
because every call a gate sends back still takes the RAM road, and the two movement readings never
had an arm cut to express — a movement carries its own moment.

### 8.4 ✅ CLOSED 2026-08-13 — `ForEachOwnAttribute` is what every pass walks

The single walk over everything this register owns had **no callers**: the lifecycle passes in
`accountingRegisterMetadata.cpp` spelled their own loops and every one of them visited the DEBIT lists
only, so a correspondence register saved and reloaded its credit slots and then never typed them —
columns that exist and admit nothing.

Nine passes are on the walk now: `OnCreateMetaObject`, `OnLoadMetaObject`, `OnSaveMetaObject`,
`OnDeleteMetaObject`, `OnBeforeRun` / `OnAfterRun`, `OnBeforeClose` / `OnAfterClose`, and the typing
pass. The walk hands the visitor a ROLE beside each attribute (`ibOwnRole` — kind / value / credit
account), because the typing pass is the one caller that cannot not care: a kind takes a reference to a
characteristic and a value takes the chart's whole composition. Told only the attribute it would have
had to read the role back out of the NAME, which is the classification re-derived by spelling.

**And a tenth site of the same class, found while wiring it.** `SyncAccountDimensionSlots` — which
CREATES the credit account — ran AFTER the account typing in `OnAfterRunMetaObject`, so on the very run
that turned correspondence on, `AccountCr` was born untyped and stayed so until the configuration was
run again. The order is now create-then-declare.

⚠ `WriteData` is deliberately NOT on the walk: it writes the ACTIVE slots only, while the walk visits
every slot the register holds, deactivated tail included. Two different questions — "what does this
register own" and "what belongs in the file" — and the file's answer is the narrower one.

### 8.5 ✅ CLOSED 2026-08-13 — the language has the word, and the walk left this register

The three unfold words existed and the reading honoured whichever it was handed (`ibQueryDimUnfold`,
§ 5e) — but nothing could hand it one from a QUERY, so the readings passed `Hierarchy` because there
was no way to say anything else. `Hierarchy` / `HierarchyOnly` were parsed only as modifiers of a
dimension in a `TOTALS BY` clause.

They now have a **second venue**: the set-valued operator.

```
WHERE Account IN HIERARCHY     (&Accounts)     -- the accounts named, and everything under them
WHERE Account IN HIERARCHYONLY (&Accounts)     -- what is under them, without the accounts themselves
WHERE Account IN              (&Accounts)      -- exactly the accounts named  (Elements, unchanged)
```

Not a new word beside `In` — the SAME three words, in a second position. A report and a filter that
both say "in hierarchy" have to mean the same thing by it, and a second vocabulary is a second chance
to drift.

**Its operand is one `&parameter`, and that is the one way it differs from `In`.** Ordinary `In`
takes a list or a subquery and the parser accepts both; expanding a subtree means walking DOWN from
values, so they have to be in hand before the read starts, and a subquery is not in hand until it
runs. A parameter holding a list is how several are named. The refusal is a parse error with that
reason in it, not a silently ignored word.

**And the walk left this file — through the PROVIDER, which is what made it worth moving.** The first
version asked the VALUE: cast it to a reference, ask its metaobject for a parent link, then one query
per node. It is `ibQueryHierarchyScope` now (`query/queryHierarchy.{h,cpp}`) and it asks the COLUMN —
`GetProvider().ResolveReferenceTarget(source, col)` for the target, the target's own row key and
parent column, and the whole parent map in ONE read. Three things came off it at once: the metadata
cast left a tier that owns no metadata, a round trip per account became a single query, and the
filter now travels the same road `TOTALS BY x HIERARCHY` already did
(`ibQueryComposer::BuildReferenceHierarchy`) — one mechanism for both venues, so the two cannot come
to mean different things by "hierarchy". Nothing below L4 learns the word: the lowering resolves the
subtree into values and emits the ordinary `IN`. Grammar and the decisions behind it:
[query-language-arc.md § 23.3](query-language-arc.md).

**And that road was closed for accounts, which is the defect this uncovered.** A chart of accounts
declares `Subordination`, and `GetHierarchyColumn()` gated on `IsHierarchical()` — the two
arrangements the engine NAVIGATES — so it answered null and the provider road had nothing to walk.
The private walk got its answer only by reaching around it into the metaobject (`HasParentLink`).
Which means `TOTALS BY <account> HIERARCHY` had been degrading to a **flat grouping** all along: the
word was in the language and did nothing on a chart of accounts.

One accessor, two questions, one null. It answers `HasParentLink()` now — *the hierarchy is the
parent, and the one arrangement with no hierarchy is the one with no parent* (owner, 2026-08-13) —
which is also what the enumeration's own comment always said the arrangement was for: *whoever wants
the structure asks for it, a query, a grouping*. Both venues are fixed by that one line.

⚠ **It is a behaviour change beyond this register, and it belongs in its own commit.** A source that
records a parent is now a tree to the level fetch as well, so a list of accounts becomes one in a
tree view (`modelDb.cpp` — whose own comment already called a chart of accounts inherently a tree).
Left open, and worth asking once the tree is seen: with the parent meaning a hierarchy in both,
`Subordination` and `Items` now differ only in whether an item may be ENTERED by declaration — so
either a chart of accounts should simply declare `Items`, or the two arrangements should become one.

✅ **AND IT FOLDS FROM A QUERY TOO, since the account became a CONDITION** (§ 5h). The obstacle was
never the walk — it was that a condition written in a query became a `WHERE` **around** the reading,
and a filter around a fold can only remove rows: under `HIERARCHYONLY` it removes exactly the rows
the fold produced, because the account they are reported under is the one the filter excludes.

Three pieces, and each closes one half of that:

- the slot is declared `m_consumedBySource`, so the predicate **leaves the WHERE entirely** and
  arrives inside the reading (`query-engine-layers.md` § L3);
- the lowering keeps the word (`keepUnfold`): the leaf carries the accounts AS NAMED plus
  `ibQueryCondition::m_unfold`, because a fold cannot be rebuilt from twenty expanded values — none of
  them says which one they roll into;
- `ScopeFromAccountCondition` turns that leaf back into the same `ibQueryHierarchyScope` the argument
  used to produce, so everything downstream is unchanged.

⚠ A script call still hands the slot a VALUE, and it means the same as before: `ibAcctParseCall`
builds the condition from it with the word `Hierarchy`. Both entrances therefore arrive as one form,
which is the only arrangement in which they cannot answer differently.

---

## 5k. The characteristic became a TYPE, and the chart of accounts unfolded — 2026-08-16

Four changes with one shape: a fact is DECLARED once and ASKED wherever it is needed, instead of
being copied into every place that needs it.

### The value slot is a characteristic, not a copy of the contour

A register's value slot used to be typed with the chart's composition copied into it
(`SetDefaultMetaType(GetTypesOfCharacteristics())`). The copy had to be kept in step — that is what
`ApplyAccountDimensionSlotTypes` is for, and the arc's own history (§5i, §5j) is largely the cost of
that. Now both halves of the pair are asked of the registry, and they mirror each other:

```
kind  slot  ← GetTypeCtor(pvh, ibCtorObjectMetaType_Reference)        ChartOfCharacteristicTypesRef.<chart>
value slot  ← GetTypeCtor(pvh, ibCtorObjectMetaType_Characteristic)   Characteristic.<chart>
```

A kind IS an element of the chart; a value IS a characteristic of it. Nothing of the chart's is held
in the register any more, so nothing can go stale.

### The redirection is ONE question, asked where everyone already asks

`Characteristic.<chart>` is a DECLARATION — no value ever carries that class. Rather than teach each
consumer to unfold it, the field answers a second question, and the consumers ask it instead:

```cpp
GetTypeDesc()       what is DECLARED        — inspector, file, writes
GetTypeValueDesc()  what a VALUE may be     — the chart's own list when the declaration is a
                                              characteristic; the SAME object otherwise
```

Declared on the column base and on the type factory, overridden once by the attribute
(`query-language-arc.md` § 22.4b-bis). What that bought, per consumer, without any of them mentioning
characteristics:

| Question | Who asks | What it fixes |
|---|---|---|
| what columns does this hold physically | `DescribeColumnLayout`, `DriveSpread`, `HasReference`, `TagFitsColumn` | otherwise the column is a bare `_TYPE` with nowhere to put the data, and the parameter list shifts |
| what may the user pick | `ibTypeControlFactory::GetDataType` → `ShowSelectType(metaData, GetTypeValueDesc())` | with the declaration the picker saw ONE class and returned early, so it never opened and the cell settled on a class no value carries |
| where can a dot-walk go | `ibSourceExplorer::AppendColumn` → pickers, filters, sort, group | the field was a leaf: no `[+]`, no `Field.Attribute` in a filter |
| what may a CONDITION hold | the filter's field node (`listSettings.cpp`) → `GetRightTypeDescription` → `AdjustValue` | the right-hand cell adjusted every picked value to empty — a chosen reference would not stick, and an existing value was wiped the moment the picker opened, before anything was chosen |
| what value do I create, and what may I hold | `ibCtorMetaValueTypeCharacteristic::CreateObject` / `AllowValue` — the ctor the CHART registers | already there: the ctor answers from the contour, so a characteristic builds the contour's type (one) or an **undefined** (several), never a value of class `Characteristic.<chart>` |

⭐ The last row is the lesson of the arc. The dot-walk was first "fixed" inside the branch resolver
(`ConvertToMetaIds`), the picker by teaching a control to interrogate its bound column, and creation
by routing `CreateValueRef` / `AdjustValue` through the value question — three mechanisms taught the
same fact about characteristics, and all three were deleted. What holds is one answer per question,
given where the question already was: the CTOR says what value gets built, `GetTypeValueDesc` says
what may be held, the explorer node carries both for whoever walks it — and the picker needed exactly
one word changed. A redirection placed where the question is already asked costs nothing; the same
redirection placed at each consumer is a crutch per consumer.

The explorer node keeps BOTH — the declaration (what a generated column and the type picker show) and
the value type (what a branch walks into) — supplied together by the node's constructor, so a node is
complete when it is built (`query-language-arc.md` § 22.4b-bis).

### A chart of accounts unfolds its kinds into list columns

The kinds of an account live in a tabular section — right for the card, invisible to a LIST, which is
where one actually reads which analytics an account carries. So the chart declares
`AccountDimensionKind1..N`, N = `MaxAccountDimensionCount`:

- **created once and reused** (a metaID names the physical column), deactivated from the tail when the
  ceiling is lowered — the same rule the register's slots follow;
- **typed with the section's own kind column**, in the same call;
- **serialised with their ids**, so a reload cannot rename the columns their data sits in;
- **filled on write**: column N ← row N, rewritten whole, so a removed row leaves an EMPTY column
  rather than a stale kind;
- **read-only through `IsReadOnlyAttribute`** — which now answers for the object's own reference too,
  so a caller asks ONE question about writability instead of two. The section is the author; a script
  assigning the copy would be overruled without a word at the next save.

⚠ They are ordinary stored columns, and the copy is refreshed only through `SaveData`. Editing the
section behind the object's back leaves the list showing the previous kinds.

### Sorted by code

A chart of accounts list now sorts by CODE, not description. In a catalog the code is a serial number
and the name is what a person reads; here the code IS the account, and its order is the plan itself —
sorted by name, `51` lands between "Материалы" and "Налоги".

### Also removed: the `Order` column of the kinds section

Nothing read it. The order of the kinds IS the order of the rows — slot N takes row N — so a stored
number beside the row was a second spelling of the row's own position, and the stored one is the one
that can disagree.
