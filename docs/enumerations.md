# Enumerations — the template system, and the property built for it

> **Scope:** how a plain C++ `enum` becomes a script value, an inspector dropdown, a
> serialisable property and a metadata type — all from one declaration.
> `backend/compiler/enumUnit.h` (346 lines of templates, 22 of `.cpp`) +
> `backend/propertyManager/property/propertyEnum.h`.
> Companions: [property-system.md](property-system.md),
> [script-value-types.md](script-value-types.md), [factories.md](factories.md).
> This is foundation code and it is worth reading closely — it is the cleanest
> template/property pairing in the engine.

---

## 1. Why it deserves attention

An engine enum (`ibRoundMode`, `ibIndexingMode`, `ibRepresentation`, …) has to be **five
things at once**:

1. a C++ `enum` the code switches on,
2. a **script value** (`TYPE_ENUM`) a module can hold and compare,
3. a **named list** with human labels, translated,
4. an **inspector dropdown**,
5. a **serialisable** property value.

The system below produces all five from one type — and the property was built specifically
to fit it, rather than the enum bending to the property.

---

## 2. The value side — `enumUnit.h`

Three template layers over one non-template base:

```cpp
class BACKEND_API ibValueEnumerationWrapper : public ibValueDynamicMembers {
    virtual ibValue* GetEnumVariantValue() const = 0;
    virtual wxString GetClassName() const = 0;
    virtual wxString GetString() const = 0;
protected:
    std::vector<wxString> m_listEnumStr;
};
```

The **non-template** base exists so the rest of the engine can hold *any* enumeration
through one pointer — templates below, a stable interface above.

```cpp
template <typename valT>
class ibValueEnumerationVariantBase : public ibValue {          // ONE VALUE
    ibValueEnumerationVariantBase() : ibValue(ibValueTypes::TYPE_ENUM, true) {}
    virtual valT GetEnumValue() const = 0;
    virtual void SetEnumValue(const valT& v) = 0;
};

template <typename valT>
class ibValueEnumerationBase : public ibValueEnumerationWrapper {   // THE COLLECTION
    virtual bool Init(ibValue** paParams, const long lSizeArray) {
        if (lSizeArray < 1) return false;
        SetEnumValue(static_cast<valT>(paParams[0]->GetInteger()));
        return true;
    }
};

template <typename valT>
class ibValueEnumeration : public ibValueEnumerationBase<valT> {    // default base for ALL enums
    std::map<valT, wxString> m_listEnumData, m_listEnumDesc;
protected:
    template <typename valType>
    class ibValueEnumerationVariant : public ibValueEnumerationVariantBase<valType> { … };
};
```

Read the split as **collection vs member**: `ibValueEnumeration<valT>` is the *type*
(`ComparisonKind` — the thing that lists its members), and its nested
`ibValueEnumerationVariant` is a *single value* (`ComparisonKind.Equal` — the thing a
variable holds, tagged `TYPE_ENUM`).

Two maps, not one:

| Map | Holds |
|---|---|
| `m_listEnumData` | value → **name** (the script identifier) |
| `m_listEnumDesc` | value → **description** (the human/translated label) |

That is why a dropdown can show "Greater than" while a module writes `Greater`.

`Init(paParams, …)` is the standard `ibValue::Init` idiom: build a value from an integer —
the bridge from stored data back to a typed enum value.

Registration is one line per enum, and it is the **kind** that makes it an enum, not the
name ([script-value-types.md § 1.1](script-value-types.md)):

```cpp
ENUM_TYPE_REGISTER(ibValueEnumComparisonKind, "ComparisonKind", enum_to_clsid("EN_CMPK"));
ENUM_TYPE_REGISTER(ibValueEnumSortDirection,  "SortDirection",  enum_to_clsid("EN_SDIR"));
ENUM_TYPE_REGISTER(ibValueEnumItemMode,       "ItemMode",       enum_to_clsid("EN_ITMO"));
ENUM_TYPE_REGISTER(ibValueEnumIndexingMode,   "IndexingMode",   enum_to_clsid("EN_INMO"));
```

→ `ibCtorEnumType<T>` in the ctor registry ([factories.md § 2](factories.md)).

`ibValueEnumFactory : ibValueDynamicMembers` (`enumFactory.h`) is the script-facing
namespace: its member surface **is** the set of registered enum classes, *rebuilt from the
global ctor registry on each `Build()`* — so a newly registered enum appears without
touching the factory.

---

## 3. The property built for it — `propertyEnum.h`

### 3.1 The base fixes the rendering, permanently

```cpp
class BACKEND_API ibPropertyEnumBase : public ibProperty {
    long GetValueAsInteger() const { return m_propValue; }     // stored as an integer
    void SetValue(const long& i)   { m_propValue = i; }

    virtual wxObject* GetPGProperty() const final {            // ← FINAL
        if (ms_propertyEnum != nullptr)
            return ms_propertyEnum(m_propLabel, m_propName, GetEnumList(), GetValueAsInteger());
        return nullptr;
    }
    static wxObject* (*ms_propertyEnum)(const wxString&, const wxString&, const wxPGChoices&, const int&);
protected:
    virtual wxPGChoices GetEnumList() const = 0;               // ← subclass supplies the list
};
```

- **`final`** — no enum property may render itself differently. Every enum in the product
  is one dropdown, by construction. The variation point is the *list*, not the widget.
- The frontend slot takes **`wxPGChoices`** ([property-system.md § 4](property-system.md)) —
  the only slot in the family whose signature carries a list.
- The stored value is an **integer**; `ReadNodeValue`/`WriteNodeValue` write a *typed
  Number*, not an opaque blob — so a JSON/diff view shows a readable number, and
  compare works through it ([property-system.md § 6.1](property-system.md)).

### 3.2 The template — the property owns a live enum instance

```cpp
template <typename valEnumProp>
class ibPropertyEnum : public ibPropertyEnumBase {
    using valueEnumType = typename valEnumProp::valEnumType;   // the C++ enum, pulled OUT of the value class

    ibValuePtr<valEnumProp> m_enumCreator =
        ibValuePtr<valEnumProp>(ibValue::CreateAndConvertObjectRef<valEnumProp>());   // ← a LIVE enum value
public:
    valueEnumType GetValueAsEnum() const { return static_cast<valueEnumType>(GetValueAsInteger()); }
    void SetValue(const valueEnumType& e) { ibPropertyEnumBase::SetValue(static_cast<int>(e)); }

    virtual bool SetDataValue(const ibValue& varPropVal) {
        SetValue(varPropVal.ConvertToEnumValue<valueEnumType>());
        return true;
    }

    virtual bool GetDataValue(ibValue& pvarPropVal) const {
        ibValue enumVariant = GetValueAsInteger();
        ibValue* ppParams[] = { &enumVariant, nullptr };
        if (m_enumCreator->Init(ppParams, 1)) {            // int → typed enum VALUE
            pvarPropVal = m_enumCreator->GetEnumVariantValue();
            return true;
        }
        pvarPropVal = m_enumCreator;                       // fall back to the collection itself
        return true;
    }
protected:
    virtual wxPGChoices GetEnumList() const {              // the dropdown, generated LIVE
        wxPGChoices list;
        for (unsigned int idx = 0; idx < m_enumCreator->GetEnumCount(); idx++)
            list.Add(m_enumCreator->GetEnumDesc(idx), m_enumCreator->GetEnumValue(idx));
        return list;
    }
};
```

This is the pairing worth understanding:

- **It is parameterised by the VALUE class, not by the C++ enum.** `valEnumProp` is
  `ibValueEnumComparisonKind`; the C++ enum is pulled out of it via
  `typename valEnumProp::valEnumType`. So the *value* type is the single source of truth and
  the property follows it — declare the enum once, on the value side.
- **The property holds a live instance of the enumeration** (`m_enumCreator`). It is not a
  static table: the dropdown (`GetEnumList`) and the script value (`GetDataValue`) are both
  **generated from the live object**, which is the same "generate live, do not cache" rule
  as everywhere else ([property-system.md § 3](property-system.md)).
- **The integer is the storage; the enum value is a view.** `SetDataValue` converts a script
  value in; `GetDataValue` asks `m_enumCreator->Init(int)` to mint a typed value out. The
  variant never holds a custom payload for an enum — which is why enums serialise, diff and
  copy with no extra code.

Declaring one is then a single member ([report-engine.md § 2](report-engine.md) style):

```cpp
ibPropertyEnum<ibValueEnumIndexingMode>* m_propertyIndexing =
    ibPropertyObject::CreateProperty<ibPropertyEnum<ibValueEnumIndexingMode>>(
        m_categoryData, wxT("Indexing"), _("Indexing"), ibIndexingMode::DontIndex);
```

and it is immediately editable, scriptable, serialisable, diffable and copyable — all five
surfaces of [property-system.md § 1](property-system.md).

---

## 4. RECIPE — how to add a runtime enumeration

**This is the canonical procedure.** Follow the reference implementation
(`metaCollection/attribute/metaAttributeObjectEnum.{h,cpp}` — `ItemMode` / `SelectMode` /
`IndexingMode`) rather than inventing a variant; match the surrounding names exactly.

Three steps, two files. A plain C++ enum becomes a live runtime object.

### Step 1 — the plain C++ enum (`*.h`)

```cpp
// Attribute indexing: a DB-level secondary index on the attribute for faster WHERE / JOIN /
// list filtering. WithAdditionalOrder appends the row reference to the index so list browsing
// (dynamic lists) is ordered too. DontIndex is the default — index only what searches / joins
// on it, not booleans / low-cardinality fields (an index slows writes and grows the DB).
enum ibIndexingMode {
    ibIndexingMode_DontIndex,
    ibIndexingMode_Index,
    ibIndexingMode_IndexWithAdditionalOrder
};
```

- Naming: **`ib<Name>` for the enum, `ib<Name>_<Member>` for members.**
- Nothing special — the code switches on this normally. Start numbering at `1` only if `0`
  must not be a valid value (`ibSelectMode` does; `ibItemMode` does not).
- Document *why* here, not in the value class. This comment is the one a developer reads.

### Step 2 — the value class (same `*.h`)

```cpp
#pragma region enumeration
#include "backend/compiler/enumUnit.h"

class ibValueEnumIndexingMode : public ibValueEnumeration<ibIndexingMode> {
public:
    ibValueEnumIndexingMode() : ibValueEnumeration() {}

    virtual void CreateEnumeration() {
        AddEnumeration(ibIndexingMode_DontIndex,                wxT("DontIndex"),                _("Don't index"));
        AddEnumeration(ibIndexingMode_Index,                    wxT("Index"),                    _("Index"));
        AddEnumeration(ibIndexingMode_IndexWithAdditionalOrder, wxT("IndexWithAdditionalOrder"), _("Index with additional ordering"));
    }
};
#pragma endregion
```

- Class name: **`ibValueEnum<Name>`**, deriving `ibValueEnumeration<the C++ enum>`.
- Default ctor delegating to the base. That is all the construction there is.
- **`CreateEnumeration()` is the one method you write.** The engine calls it; you list the
  members.
- `AddEnumeration(value, scriptName, humanLabel)` — three arguments, and the last two are
  **not** the same thing:

  | Argument | Form | Becomes |
  |---|---|---|
  | `value` | the C++ enumerator | the stored integer |
  | `scriptName` | `wxT("…")` — **never translated** | the identifier a module writes: `IndexingMode.Index` |
  | `humanLabel` | `_("…")` — **translated** | the inspector dropdown text, the UI |

  Getting these backwards is the classic mistake: a translated script name would make
  modules stop compiling in another locale.

### Step 3 — register it (`*.cpp`)

```cpp
#include "metaAttributeObjectEnum.h"

//***********************************************************************
//*                       Register in runtime                           *
//***********************************************************************

//add new enumeration
ENUM_TYPE_REGISTER(ibValueEnumIndexingMode, "IndexingMode", enum_to_clsid("EN_INMO"));
```

- The **script-facing name** is the macro's second argument (`"IndexingMode"`) — PascalCase.
- The clsid key is `EN_XXXX` (a short opaque token). It is a **key, not a label**:
  changing it changes the clsid and breaks stored data
  ([script-value-types.md § 1.1](script-value-types.md)).
- Keep it under the standard banner, with its siblings.

### Step 4 (optional) — expose it as a property

If a metaobject or control should carry it, one member is the whole job:

```cpp
ibPropertyEnum<ibValueEnumIndexingMode>* m_propertyIndexing =
    ibPropertyObject::CreateProperty<ibPropertyEnum<ibValueEnumIndexingMode>>(
        m_categoryData, wxT("Indexing"), _("Indexing"), ibIndexingMode_DontIndex);
```

Read it back with `(*m_propertyIndexing)->GetValueAsEnum()` — typed as `ibIndexingMode`, not
as an int.

### What you get, for free, from those three steps

- a script value: `IndexingMode.Index`, tagged `TYPE_ENUM`, comparable;
- `IndexingMode` visible in the enum namespace, because the factory surface is **rebuilt
  from the ctor registry** (§2) — no factory edit;
- an inspector dropdown with translated labels (§3.1), `final`-rendered like every other
  enum;
- serialization as a typed Number, so it diffs and copies
  ([property-system.md § 6](property-system.md));
- autocomplete / syntax-helper entries, from the same registration.

**No step 5.** There is no table to update, no switch to extend, no factory to touch. That
is the design: the enum declares itself and every surface follows.

---

## 5. Two different "enumerations" — do not confuse them

| | `ibValueEnumeration<valT>` (this doc) | `ibValueMetaObjectEnumeration` |
|---|---|---|
| What | a **platform** enum, written in C++ | a **user** metadata object |
| Declared by | a developer, `ENUM_TYPE_REGISTER` | a user, in the Designer |
| Lives in | the process, always | the open configuration |
| Examples | `ComparisonKind`, `IndexingMode` | whatever the application defines |

Both appear as enumerations to a script. Only the first is this template system; the second
is a metaobject ([../CLAUDE.md](../CLAUDE.md) § Metadata Object Types).

---

## 6. Honest remainder

- `enumUnit.h` is 346 lines of header against a 22-line `.cpp` — effectively header-only.
  Every enum instantiates the chain, which is a compile-time cost paid engine-wide.
- `m_listEnumStr` on the non-template wrapper overlaps `m_listEnumData` / `m_listEnumDesc`
  on `ibValueEnumeration<valT>`; which one is authoritative for a given call is worth
  checking before adding a sixth surface.
- `GetDataValue`'s fallback (`pvarPropVal = m_enumCreator`) hands back the **collection**
  when `Init` fails — a script would then see the enum *type* where it expected a *value*.
  Rare, but it is a silent shape change rather than an error.
