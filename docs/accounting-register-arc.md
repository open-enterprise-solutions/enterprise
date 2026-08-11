# Accounting register — inventory and the decisions taken

> **Status:** INVENTORY + DECISIONS, no code written (2026-08-10). Every state claim below was
> verified against source on that date; the decisions are the owner's and are recorded verbatim
> in intent, not paraphrased into something softer.
>
> Companion docs: [register-totals-strategy.md](register-totals-strategy.md) (the totals
> machinery this register is the reason for), [data-composer.md](data-composer.md) (L5),
> [query-engine-layers.md](query-engine-layers.md) (the L1–L5 floor plan).

---

## 0. Why this document exists

The accounting stack — `ChartOfAccounts`, `ChartOfCharacteristicTypes`, `AccountingRegister` —
was written earlier by another author and **was never carried to the end**. It then stood still
while the neighbouring registers (accumulation, information) migrated onto the L3 door, so today
it is the last subsystem living in the previous era of the engine. That is the whole diagnosis:
not "half-built", but **built to a different generation of the surrounding code**.

Read the sections below as: what is genuinely finished (most of it), what is wired to the wrong
end, and what is missing outright.

---

## 1. What is finished

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
- the same holds for switching correspondence off: the `Cr` slots are dropped, and their data
  with them, so this is a warning-worthy operation rather than a checkbox.

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
- whether the first version honours all the accounting flags or only the ones the balance needs.

---

## 6. Order of work

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
| chessboard | the correspondence matrix — a fold of `DrCrTurnovers`, not a fifth source |

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
