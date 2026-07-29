# Attribute indexing + register key uniqueness

**Status: LANDED (2026-07-14, Debug|x86 green).**

Two related pieces:
1. **A per-attribute `Indexing` mode** that declares a secondary DB index on any field.
2. **The register key index is now UNIQUE** — the DB-level guarantee that fixed a
   duplicate-rows bug that the app-level probe alone let through.

---

## 1. `ibIndexingMode` — the per-attribute flag

`enum ibIndexingMode { DontIndex, Index, IndexWithAdditionalOrder }`
(`metaCollection/attribute/metaAttributeObjectEnum.h`), wrapped by
`ibValueEnumIndexingMode` and registered like its siblings:

```cpp
ENUM_TYPE_REGISTER(ibValueEnumIndexingMode, "IndexingMode", enum_to_clsid("EN_INMO"));
```

Naming mirrors `ItemMode` (`EN_ITMO`) / `SelectMode` (`EN_SEMO`) exactly: the registered
name is the enum name, the clsid key is `EN_` + the first two letters of each word
(**IN**dexing + **MO**de). Accessor `GetIndexingMode()`, members `m_indexingMode` /
`m_propertyIndexingMode`, in lock-step with `GetItemMode` / `m_itemMode` / `m_propertyItemMode`.

- **`Index`** — a plain single-column index on the attribute.
- **`IndexWithAdditionalOrder`** — the attribute **plus the row reference**, so ordered
  list browsing (dynamic lists) reads its order straight off the index.
- **`DontIndex`** — the default; index only what is searched / joined / filtered, never
  booleans or low-cardinality fields (they slow writes and grow the DB).

Where the mode lives per attribute kind:
- **Plain attribute** (`ibValueMetaObjectAttribute`) — a real property
  (`m_propertyIndexingMode`, `ibPropertyEnum`) in the *Attribute* category, editable in the
  inspector, serialized like the other properties. Dimensions and resources inherit it (they
  are-a plain attribute), so they carry the flag too.
- **Predefined attribute** (`ibValueMetaObjectAttributePredefined`) — a plain member
  (`m_indexingMode`, NSDMI `DontIndex`), a `GetIndexingMode()` override, manual save/load
  (`SetValue`/`GetValue<int>("Indexing")`, absent key → `DontIndex`), and a **ctor parameter**
  `ibIndexingMode indexingMode = DontIndex` threaded through the `Create*` factory helpers
  (`metaObjectComposite.h`, `CreateMetaObjectAndSetParent` perfect-forwards it). So a predefined
  column is indexed at creation time, e.g.:
  `CreateString(wxT("Code"), ..., ibItemMode_Folder_Item, ibSelectMode_Items, ibIndexingMode_Index)`.

## Emission — `ContributeAttributeIndexes`

A free function in `commonObjectMetaQuery.cpp` (anon namespace) declares one secondary index
per flagged attribute during `ContributeTables`:

```cpp
void ContributeAttributeIndexes(ibSchemaTable& t,
    const std::vector<ibValueMetaObjectAttributeBase*>& attributes,
    const ibBackendQueryColumn* orderRef = nullptr);
```

- `Index` → `t.Index("<table>_<attrId>_IX", { attr })` — non-unique, metaID-keyed name.
- `IndexWithAdditionalOrder` → `{ attr, orderRef }`.

Called for every table that holds attributes:
- **Object main table** — `orderRef = GetDataReference()` (the row Ref); covers plain +
  predefined attributes.
- **Tabular section** — `orderRef = nullptr` (browsing is per-owner by line, no single Ref);
  covers the section's own columns.
- **Register table** — `orderRef = nullptr`; covers `GetGenericAttributeArrayObject()`, which
  is **dimensions + resources + attributes + predefined** (dimensions/resources are-a plain
  attribute, so they must be included, not just `GetAttributeArrayObject()`).

## Applied defaults

The standard predefined columns ship indexed:
- **Catalog family** (`commonObject.h`): `Code`, `Description`, `Parent` → `Index`.
- **Document** (`document.h`): `Number`, `Date` → `Index`.
- **Enumeration** — none (the table is a tiny uuid-keyed seed set; a scan is instant).
- **Register** — none by default; indexing is manual per field (see below).

Boolean predefined columns (`IsFolder`, `Posted`) are deliberately left **un-indexed**: a
standalone index on a low-cardinality boolean taxes every insert for little read benefit — the
planner usually ignores it. Index a boolean only inside a composite when a real query needs it.

---

## 2. Register key index is UNIQUE

`ibValueMetaObjectRegisterData::ContributeTables` builds the register key index over
(recorder + line) for a subordinate register, else the dimension columns (period + dimensions
when periodic). It is now created **UNIQUE**:

```cpp
t.Index(t.m_name + wxT("_INDEX"), idxCols, true);   // was: no `true` → non-unique
```

**Why:** an independent information register was showing duplicate rows for a single dimension
key. The list was faithful — the rows were genuinely distinct in the DB. The only guard against
same-key duplicates was the app-level `ibValueRecordManagerObject::ExistData()` probe, which is
fragile (it swallows exceptions and relies on exact string equality). When the probe missed, a
permanent duplicate slipped in. A unique index makes duplicates **impossible** at the DB level,
regardless of the probe — exactly how objects/enums already key on a unique uuid.

Recorder access stays fast without a separate index: the recorder is the **leading column** of
the composite key index, so `WHERE recorder = X` (post-time delete + movement reads) uses its
prefix.

---

## Differ — same-name index rebuild

`schemaSnapshot.cpp` used to diff indexes by **name only**, so toggling a flag that changed an
index's columns (e.g. `Index` → `IndexWithAdditionalOrder`) or its uniqueness under the same
name was ignored. `FindIndex` + `SameIndex` (compare `unique` + columns by `GetColumnId()`) now
detect a shape/uniqueness change and **drop + recreate** the index. Pure toggles
(`DontIndex` ↔ `Index`) already migrate because the index name appears / disappears.

### Migration caveat

The differ's baseline is built **from code** (both the saved config and the edited config run
the *current* `ContributeTables`). A code-only DDL change — like flipping the register key to
unique — therefore produces **no delta** on an existing database (both sides report unique).
Plus a unique index cannot be created over data that already holds duplicates. To land such a
change on an existing DB, **recreate the table structure** (`ibStructureBuilder::Recreate`),
which also clears the offending rows.

---

## Register composite indexes — half landed

The per-attribute flag declares **single-column** indexes. Register aggregate access needs
**composite** indexes the flag cannot express:

- **Totals** (accumulation) — **LANDED.** `accumulationRegisterSchema.cpp` declares the totals
  key `(period, dimensions…[, shard])` as a unique `_PK` index on the derived totals table,
  built alongside the maintenance bundle. The declaration arrived with the totals metaobject
  (2026-07-29), exactly where this section predicted it belonged.
- **Slice of last** (information register) — still open: `(dimensions, period)`,
  dimension-leading (the reverse of the period-leading key), to fetch the latest record per
  dimension as of a date.

The mechanics were ready all along (`t.Index(name, { col1, col2, ... })` + the content-aware
differ handle any composite); what was missing was the declaration, and it belongs with the
register's virtual tables rather than with a per-column flag. See
[register-totals-strategy.md](register-totals-strategy.md).
