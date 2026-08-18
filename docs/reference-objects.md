# Reference objects — the reference, the object, and how a row is found

> **Scope:** what a *reference* is and why it exists, how it differs from the *object* it points at,
> how identity travels between the tiers, how predefined items and enumeration values get into the
> database, and how quick choice works.
> Companions: [reference-key-metaid.md](reference-key-metaid.md) (the key's byte layout),
> [source-object.md](source-object.md), [dynamic-list.md](dynamic-list.md),
> [data-transfer.md](data-transfer.md), [schema-authority.md](schema-authority.md).

---

## 1. Two things, not one

| | what it is | what it costs | where it lives |
|---|---|---|---|
| **Reference** | *which* row: a guid **plus its type**, taken from the metadata | 16 bytes + a type id | in any cell that points at the row, and in the row itself |
| **Object** | *what is in* the row: every attribute, its tabular sections, its lock | a read | materialised on demand |

A reference answers **identity**; an object answers **content**. Everything in this file follows from
keeping those apart. A reference is cheap enough to store in millions of cells and to compare in SQL;
an object is not, and is never implied by holding one.

**Only the reference family has references.** Catalogs, documents, charts of accounts, charts of
characteristic types and enumerations are keyed by their own reference. A register has no reference:
its identity is composite (recorder + line, or period + dimensions), which is why it takes none of
the single-key paths. A tabular section's line has no reference either — it is identified by its
OWNER plus its line number.

## 1a. The roles one metaobject vends

"Catalog.Goods" is not one type — it is a family of them, and each answers a different question.
They are declared per metatype as a capability set (`ibValueMetaObject::s_features`) and registered
per configuration when it runs (`registerReference()` / `registerManager()` / `registerObject()` …);
the mechanics live in [factories.md](factories.md) § 3a, and this table is only what each role is
FOR:

| role | script name | answers | holds |
|---|---|---|---|
| **Reference** | `CatalogRef.Goods` | *which* row | guid + type. Comparable, storable, joinable |
| **Object** | `CatalogObject.Goods` | *what is in* the row | every attribute + tabular sections; writes and locks |
| **Manager** | `CatalogManager.Goods` | questions about the KIND | find-by-code, empty ref, predefined values, forms |
| **Selection** | `CatalogSelection.Goods` | a walk over rows | a cursor, not a container |
| **RecordSet** | `InformationRegisterRecordSet.X` | a register's rows for one key | reads and writes as a SET |
| **RecordKey** | `…RecordKey.X` | a register's composite identity | the dimensions that identify a record |

The set a metatype claims is exactly the set it registers — that pairing is the invariant. An
**enumeration** stops at `Reference | Manager`: its values are declared, so there is no Object to
write and no Selection to walk. A **register** has `Manager | RecordSet | Selection` and **no
reference at all** — its identity is composite, which is why nothing in this file about references
applies to it.

Why the split matters in practice: holding a reference costs 16 bytes and never touches the
database, while asking the same thing "as an object" reads a row. Most of the defects this document
records are one role doing another's work — a declaration materialising objects, a list reducing a
reference to a guid and rebuilding it, a seed writing identity as text.

## 2. The reference is the row's identity, and it is stored ONCE

Every reference object's table carries its own reference as a column (`_TYPE` / `_RTRef` / `_RRRef`),
covered by a unique index. That column IS the primary key: `GetPrimaryKeyColumns()` returns it, the
UPSERT matches on it, a dot-walk join equates it, and the cursor's keyset tail is it.

There used to be a second copy — a scaffold column holding the same sixteen bytes, spelled
`Row_RRRef`. It is gone from every reference object; what survives under that name is a tabular
section's **owner** reference (`ibOwnerRefField`), which is a different fact and now says so.

**Identity never travels as text.** A reference has a presentation — a description, a code,
"Not found <…>" when the row is gone — and it is *not* a spelling of the guid. Every place that
needed identity and reached for the text was a defect: the seed that wrote 16 characters of a guid's
text into a `BINARY(16)` column, the quick choice that parsed a guid out of a description, the six
manager reads that took `GetIdentitySort().back()` (a SORT tail) as the key and then read its text.
Ask the object: `GetGuid()`. Ask the source: `GetPrimaryKeyColumns()`.

## 3. How a row is fetched

Three doors, in order of how much they cost:

1. **By key** — `q.From(queryable).WhereKey(guid)`, one row. This is what materialises an object
   (`ibValueReferenceDataObject::ReadData`). A row that is not there is an ordinary answer
   (`false`); a read that *failed* is said out loud through the logger — the two were one silent
   `catch (...)` and a missing table looked exactly like a deleted object.
2. **A page** — the list fetch: settings → composer → SQL → keyset page. The keyset compares the
   reference by its real `_RRRef` blob, so `_RRRef OP <blob>` agrees with `ORDER BY _RRRef`.
3. **A scan** — quick choice and the like, always bounded (§6).

`CreateRaw` vs `Create`: **raw** builds a reference carrying identity only; **Create** materialises
the object behind it. Schema declaration must use `CreateRaw` — using `Create` there sends the
declaration to READ the database (once per predefined item), which both violates the schema-authority
rule and asks for columns the apply has not created yet.

## 4. Predefined items — declared in the configuration, seeded into the base

A predefined item is a row the *configuration* guarantees: it is declared in metadata, and the apply
writes it. Mechanically it is a **seed row** of the table's declaration
(`ibSchemaSeedRow`, `ContributeTables`), diffed by uuid:

- declared and absent → written;
- declared and present → re-asserted (an upsert is idempotent, and repeating it IS the repair);
- gone from the declaration → deleted.

Every declared row is re-asserted on every apply, deliberately: the seed is DATA, written in the
deferred phase past the DDL commit on Firebird, and a failure there would otherwise leave rows the
differ can never mention again (both configurations agree the value exists).

The seed's key is written **in the form its column accepts** — a raw scaffold column takes the guid,
a declared reference column takes the reference itself (which the row already declares as a cell, so
the cell is dropped from the statement to avoid naming the same column twice).

## 5. Enumerations — a closed list, and the order is a column

An enumeration is a reference object whose rows are *entirely* declared: values exist in the
configuration and nowhere else. It therefore declares its own attributes as columns exactly as a
catalog does — the reference, and `Order` — and seeds one row per value, with the position travelling
in `Order`.

`Order` is an ordinary number column, and that is the point: the list's default sort is
`ORDER BY Order DESC/ASC` like any other sort, with the reference as the tie-break. Two consequences
worth remembering:

- the sort is written into query TEXT, so the attribute name goes through the parser — and `Order` is
  also a keyword. In a position where only a column may stand, a keyword is a NAME (this holds in an
  `ORDER BY` item, after a `.`, and in an expression);
- the value is data, so changing the order in the configuration is an ordinary apply.

## 6. Quick choice — a bounded scan with a ceiling

Quick choice offers a small list to pick from without opening a selection form. It is a scan, and it
is bounded twice:

- **the ceiling is a property of the ANSWER, not of the widget**: past ~100 entries a "quick" choice
  is not quick, so the answer is NO LIST (the caller falls through to the selection form). One row
  past the ceiling is read deliberately — that is what tells "exactly a hundred" from "at least a
  hundred and one";
- **the row being judged is the row already in hand**: the comparator is filled from the current scan
  row. It used to run a query PER ROW to re-read the values the scan had just passed over.

An empty request means "everything", which is the commonest case, and nothing about a row can change
that answer — so nothing about the row is read. The identity column is asked for **by name**
(`GetDataReference()`), and the value it yields is the reference itself: it goes into the result as
it is, without being reduced to a guid and rebuilt.

## 7. Presentation

A reference renders through the metaobject's presentation rule — the searched attributes and the
description. It is display, never identity: see §2. An object whose row is missing presents as
"Not found <…>", which is the honest answer and must never be parsed back into a key.

## 8. Honest remainder

- A tabular section has no key of its own, so its rows are INSERTed rather than upserted; the owner
  reference is repeated per line and deliberately not unique.
- Orphan seed rows in bases that drifted before the seed was fixed are NOT swept: a base is a
  function of its configuration by construction, and repairing drift from outside would legalise it.
  For test bases the answer is to recreate them.
- Containerness of a folder row is decided in the fetch and not re-asked per view mode — see the
  remainder of [column-groups.md](column-groups.md).
