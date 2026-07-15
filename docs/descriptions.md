# Descriptions — the storage-shape pattern

> **Scope:** the `ibXxxDescription` family — the structs that describe *how data is
> stored*, and their `ibXxxDescriptionMemory` serialisers.
> `typeDescription.*`, `sourceDescription.*`, `spreadsheetDescription.*`,
> `pictureDescription.*`, `actionInfo.h`.
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
| `ibActionDescription` | `actionInfo.h` | a command |

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
- `ibActionDescription` lives in `actionInfo.h`, not in a `*Description.h` of its own —
  the one family member that is not where the pattern predicts.
- Descriptions are `struct`s with public fields by design. That is a deliberate exception
  to encapsulation: they are data, and the pattern relies on them staying that way.
