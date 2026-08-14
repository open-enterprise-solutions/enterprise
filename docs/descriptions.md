# Descriptions — the storage-shape pattern

> **Scope:** the `ibXxxDescription` family — the structs that describe *how data is
> stored*, and their `ibXxxDescriptionMemory` serialisers.
> `typeDescription.*`, `sourceDescription.*`, `spreadsheetDescription.*`,
> `pictureDescription.*`, `standardCommand.h`.
> Companions: [property-system.md](property-system.md) (descriptions live inside
> variants), [pictures.md](pictures.md), [report-engine.md](report-engine.md),
> [form-attribute-binding.md](form-attribute-binding.md).
> This is foundation code.

---

## 1. The pattern

A **Description** is a plain `struct` that says what a datum *is*: its shape, not its
behaviour. Every one of them comes as a **pair**:

| Half | Form | Job |
|---|---|---|
| `ibXxxDescription` | `struct` — public fields, value semantics, `operator==` | **the data** |
| `ibXxxDescriptionMemory` | `class BACKEND_API`, **static** `ReadNode` / `WriteNode` | **the serialisation** |

```cpp
struct ibExternalPictureDescription { … };                        // data

class BACKEND_API ibExternalPictureDescriptionMemory {            // serialiser
    // node form: a Child { Name, Buffer(image bytes), Width, Height }
    static bool ReadNode(const ibDataValue& value, ibExternalPictureDescription& pictureDesc);
    static bool WriteNode(ibDataValue& value, const ibExternalPictureDescription& pictureDesc);
};
```

**Why split them:** the description stays a clean value — copyable, comparable, cheap,
usable as a variant payload with no I/O dependency — while the format knowledge lives in a
separate, stateless class. Serialisation is a *view* on the data, not a member of it. A
description can be compared and copied by code that knows nothing about `ibDataValue`.

The family:

| Description | File | Describes |
|---|---|---|
| `ibTypeDescription` | `typeDescription.h` (548) | what values a slot may hold |
| `ibSourceDescription` | `sourceDescription.h` (83) | a binding path |
| `ibSpreadsheetDescription` | `spreadsheetDescription.h` (835) | a whole table document |
| `ibPictureDescription` | `pictureDescription.h` | where an image comes from |
| `ibStandardCommandDescription` | `standardCommand.h` | a command |
| `ibCommandDescription` | `commandDescription.h` (81) | a command path — id hops, the leaf is executed |

They are what the five property surfaces move around
([property-system.md § 1](property-system.md)): put a description in a variant and it
serialises, diffs, copies and scripts.

---

## 2. `ibTypeDescription` — the type of a slot

The most important one: it is what a metadata attribute, a form attribute, a table column
and a query column all use to say "what can live here".

```cpp
struct ibTypeDescription {
    std::vector<ibClassID> m_listTypeClass;      // ← a SET of types, not one
    struct ibTypeData {
        ibQualifierNumber m_number;
        ibQualifierDate   m_date;
        ibQualifierString m_string;
    };
};
```

**Two decisions worth reading carefully.**

### 2.1 A type is a *list*

`m_listTypeClass` is a vector: a slot may accept **several** types (a composite type — the
same field taking a Number *or* a String, or a reference to any of 15 document kinds). Every
"is this allowed" check is therefore a set membership test, not an equality test.

That is why `ibPropertyOwner` / `ibPropertySource` / register dimensions talk in
`GetTypeCount()` / `GetByIdx(i)` rather than a single type — and why a
`Recorder` composite works at all ([report-engine.md](report-engine.md),
[access-policy-rls.md](access-policy-rls.md)).

### 2.2 All three qualifiers exist at once — deliberately

`ibTypeData` holds a number **and** a date **and** a string qualifier simultaneously. It is
**not** a union, and that is a consequence of §2.1: if the slot accepts Number *or* String,
both qualifiers must be there to describe both branches.

```cpp
struct ibQualifierNumber {
    bool m_nonNegative;
    unsigned char m_precision;      // default 10
    unsigned char m_scale;          // default 0
};
struct ibQualifierDate {
    ibDateFractions m_dateTime;     // Date | DateTime | Time
};
struct ibQualifierString {
    unsigned short m_length;        // default 10
    ibAllowedLength m_allowedLength;// Variable | Fixed
};
```

These are exactly the qualifiers exposed to scripts as `QualifierNumber` / `QualifierDate` /
`QualifierString` ([script-value-types.md § 2.6](script-value-types.md)) and the ones the DB
layer turns into column types.

`ibTypeData`'s constructors select by argument type — the same trick as
`ibPictureDescription` ([pictures.md § 2](pictures.md)):

```cpp
ibTypeData()                                                     // empty defaults
ibTypeData(unsigned char precision, unsigned char scale, bool nonnegative = false)   // number
ibTypeData(ibDateFractions dateTime)                             // date
ibTypeData(unsigned short length, ibAllowedLength allowed = Variable)   // string
ibTypeData(const ibQualifierNumber&, const ibQualifierDate&, const ibQualifierString&)  // all
```

Each constructor fills its own qualifier and leaves the others at defaults — so
`ibTypeData(20, 2)` reads as "Number(20,2)" and the date/string parts are simply inert.

### 2.3 ⭐⭐ "Is the type filled in" is asked of the FACTORY (2026-08-14)

```cpp
class BACKEND_API ibBackendTypeFactory {
    virtual ibTypeDescription& GetTypeDesc() const = 0;              // backend_type.h:55
    bool IsEmptyTypeDesc() const { return !GetTypeDesc().IsOk(); }   // backend_type.h:74 — NOT virtual
};
```

An empty type description is **a column no value can ever enter**, and several rules refuse exactly
that state: a register that says it is written by a recorder but names no document
(`ibValueMetaObjectRegisterData::OnSaveMetaObject`, `metaCollection/partial/commonObject.cpp:1122`),
and an accounting register's analytics slots left untyped because the chart of accounts names no
characteristic chart (`accountingRegisterMetadataSchema.cpp:117`, a restructure WARNING — a
configuration is built up in steps, but silence about it is how one ships).

**The place is the point.** The predicate sits on `ibBackendTypeFactory` because that is where
`GetTypeDesc()` is declared, so it is the only place that can answer for *every* holder of a type
description — an attribute, a control's bound source, a filter's field. It spent one revision on
`ibValueMetaObjectAttributeBase` and was wrong twice over there:

- **the name was borrowed from the wrong family.** It was called `IsEmptyProperty`, which is
  `ibPropertyObject`'s question ([property-system.md](property-system.md)) — an attribute has no
  *property* to be empty, it has a TYPE DESCRIPTION, and reusing the neighbour's word makes two
  different questions read alike at every callsite;
- **the place would have produced drift.** On a subclass it answers for that subclass alone, and the
  next holder spells the same fact its own way — `GetClsidCount() == 0`, `GetClsidList().empty()`,
  `!GetTypeDesc().IsOk()`. Three spellings of one question, each compiling, is the shape
  `accountingRegisterMetadataSchema.cpp` was already sliding into by counting classes at the callsite.

**Deliberately non-virtual:** the answer is a function of the type description alone, and
`GetTypeDesc()` — which *is* virtual — already supplies whatever each holder keeps. A virtual here
would only offer subclasses a chance to disagree about what "empty" means, which is the drift it
exists to prevent (and, inline on a `BACKEND_API` class, it would demand an out-of-line definition
for every DLL that sees the header).

---

## 3. `ibSourceDescription` — a binding path

The smallest of the family (83 lines) and the one that carries the most weight in the form
layer.

```cpp
struct ibSourceHop;        // {id, expected type}
struct ibSourceDescription { … };
class BACKEND_API ibSourceDescriptionMemory { … };
```

A binding is a **path of hops**, each hop being an id plus the reference type pinned at that
step. That is what makes a dot-path (`Product.Supplier.Name`) resolvable without metadata at
runtime, what the drag-to-create payload carries
([form-editor.md § 5](form-editor.md)), and what serialises **as raw ids** rather than as
metadata guids ([form-attribute-binding.md](form-attribute-binding.md)).

---

## 4. `ibSpreadsheetDescription` — a document as data

The largest (835 lines), and the clearest demonstration of the pattern: **an entire table
document is a description**. `ibBackendSpreadsheetObject` is a façade with behaviour over
one of these ([report-engine.md § 3](report-engine.md)); the template property stores one
inside a variant ([property-system.md § 7.1](property-system.md)).

It nests further descriptions, each a plain struct:

| Struct | Describes |
|---|---|
| `ibSpreadsheetCellDescription` | one cell — value, fill type, font, colours, alignment, fit mode, read-only |
| `ibSpreadsheetBorderDescription` | one border edge |
| `ibSpreadsheetAreaDescription` | a named area |
| `ibSpreadsheetGroupDescription` | an outline group |
| `ibSpreadsheetRowSizeDescription` / `ibSpreadsheetColSizeDescription` | sizing |

with `ibSpreadsheetCellDescriptionMemory` and `ibSpreadsheetDescriptionMemory` as the
serialisers. Because the document *is* a value, `operator==` on it is what makes a template
diffable ([property-system.md § 6.1](property-system.md)) — the whole table compares by
value.

---

## 5. Recognising and extending the pattern

To add a description:

1. **`struct ibXxxDescription`** — public fields, sensible defaults, `IsEmptyXxx()`,
   `operator==`. No I/O, no wx GUI types, no back-pointers.
2. **`class BACKEND_API ibXxxDescriptionMemory`** — static `ReadNode(const ibDataValue&,
   Xxx&)` / `WriteNode(ibDataValue&, const Xxx&)`, and document the node shape in a comment
   above them, as the existing ones do.
3. If it must be editable: a `wxVariantData` subclass holding it + `Eq` delegating to the
   struct's `operator==`, then an `ibProperty` over that
   ([property-system.md § 3](property-system.md)).

Do that and the datum gets all five surfaces for free. Skip step 1's value semantics — hold
a pointer, a handle, or state — and diff/copy silently stop working.

---

## 6. Honest remainder

- The naming is not uniform: the serialiser suffix is `…Memory` (from the
  `ibReaderMemory` / `ibWriterMemory` era — [serialization-io.md](serialization-io.md)),
  which reads as "in-memory" rather than "serialiser". A rename candidate.
- `ibStandardCommandDescription` lives in `standardCommand.h`, not in a `*Description.h` of its own —
  the one family member that is not where the pattern predicts.
- Descriptions are `struct`s with public fields by design. That is a deliberate exception
  to encapsulation: they are data, and the pattern relies on them staying that way.
