# Factories — how classes get registered and created

> **Scope:** the registries that turn a name / clsid / type into an object. Three of them,
> layered: the **value** factory (runtime objects), the **metadata** factory above it, and
> the **queryable** factory (query sources).
> Companions: [../CLAUDE.md](../CLAUDE.md) §6 (CLSID kind-typing), §7 (`ibMetaImage`),
> [script-value-types.md](script-value-types.md) (what gets registered),
> [reference-objects.md](reference-objects.md) (what each registered ROLE is for — reference vs
> object vs manager vs record set), [query-language-arc.md](query-language-arc.md).
> This is foundation code.

---

## 1. The shape — one registry template, several users

```
ibCtorRegistry<T>                       "our class for registering classes"
    ├── ibCtorRegistry<ibCtorValueType…>      → the ibValue factory   (valueFactory.cpp)
    └── ibCtorRegistry<ibCtorMetaValueType>   → the METADATA factory  (ibMetaImage::m_factoryCtors)

ibQueryableFactory                      → query SOURCES — a separate, non-owning registry
```

The metadata factory **sits above** the base runtime-object factory: registering a
metaobject type is the same mechanism as registering `Array`, one level up. The queryable
factory is deliberately *not* the same thing — see §4.

---

## 2. `ibCtorRegistry<T>` — the registry template

`backend/ctorRegistry.h`. The header states its own rule:

> the single owner of registered type-ctors and their lookup indices ("our class for
> registering classes"): clsid / type_info / name all resolve **through** this object,
> never by touching a raw container.

**Single source of truth is the ctor object itself.** `T` (: `ibCtorAbstractType`) carries
`GetClassType()` / `GetTypeInfo()` / `GetClassName()`; the maps are pointer indices *into*
that one object.

Three lookups, deliberately unequal:

| Key | Cost | Used for |
|---|---|---|
| `clsid` | O(1) hash | **hot** — `CreateObject`, `IsRegisterCtor`, VT |
| `type_info` | O(1) hash | **hot** — a live object identifying itself (`GetClassType`) |
| `name` | O(1) hash, on the **folded** name | **hot** — every `New <Name>` a script runs |

The name index used to be a linear scan, justified here as "compile-time, low frequency". That was
wrong and measurably so: `OPER_NEW` resolves the class name **at runtime**, on every object a script
creates, and with ~180 registered types (each comparison allocating twice through
`stringUtils::CompareString`) a single `New Structure` paid ~360 heap allocations. It has its own
index now — keyed on the UPPER-CASED name, because OES name comparison is case-insensitive and so
cannot ride the case-sensitive clsid hash.

**The name index is a CACHE of a computed value, not a store.** A dynamic metatype's ctor derives its
name from its METAOBJECT (`"CatalogRef." + GetName()`), so a rename changes what every one of them
answers while the identities (clsid) stay put. Rather than re-file entries per rename,
`ibValueMetaObject::SetName` (and the object inspector's rename hook) marks the metadata registry's
cache stale and the next lookup by name rebuilds it in one pass — no list of kinds to keep in step,
and nothing for the next metatype to forget. Unregister erases the name entry **by value** (find the
entry pointing AT this clsid), never by recomputed key: after a rename the recomputed key is exactly
the one that no longer matches.

### 2.1 Ownership and the two invariants

```cpp
std::unordered_map<ibClassID, std::shared_ptr<T>>       m_byClsid;   // primary store + index
std::unordered_map<std::type_index, std::shared_ptr<T>> m_byType;    // self-id index
```

One ctor lives in **both** indices as the *same* `shared_ptr` — it survives while either
holds it, and destroying the registry frees every ctor. No manual delete, no leak on a
wholesale drop.

Two subtleties worth preserving:

```cpp
void Register(T* ctor) {
    std::shared_ptr<T> owned(ctor);
    m_byClsid.emplace(ctor->GetClassType(), owned);
    const std::type_info& typeInfo = ctor->GetTypeInfo();
    if (typeInfo != typeid(void))       // meta/control ctors carry NO concrete C++ type
        m_byType.emplace(std::type_index(typeInfo), owned);
}
```

- **`typeid(void)` means "no self-id index".** Metadata and control ctors are not backed by
  one concrete C++ class, so they are clsid-addressable only. That is why the type index is
  conditional, not a bug.
- **`Unregister` reads both keys *before* erasing** — the registry owns the ctor, so
  erasing the last index entry frees `*ctor`; touching it afterwards is a use-after-free.
  (`typeInfo` is a reference to a static `std::type_info`, so it stays valid after.)

Because `Register` / `Unregister` are the **only** mutators and both read their keys off
the same ctor in the same call, the two indices **cannot drift**.

Lookups return a **raw, non-owning** pointer: callers observe, the registry owns.

---

## 3. The two `ibCtorRegistry` users

### 3.1 The value factory — runtime objects

`backend/compiler/valueFactory.cpp`, populated by the `*_TYPE_REGISTER` macro family
(`typeCtor.h`) — `VALUE_TYPE_REGISTER` / `SYSTEM_TYPE_REGISTER` / `ENUM_TYPE_REGISTER` /
… — each expanding to a static registration object. This is the factory behind
`New Array()` and behind every script type in
[script-value-types.md](script-value-types.md).

The macro you register with determines the **kind** ([../CLAUDE.md](../CLAUDE.md) §6) and
whether `CreateObject()` can build anything at all
([script-value-types.md § 1](script-value-types.md)).

### 3.2 The metadata factory — above it

```cpp
ibCtorRegistry<ibCtorMetaValueType> m_factoryCtors;   // metaData.h — a member of ibMetaImage
```

Owned by **`ibMetaImage`**, the runtime image of an open configuration
([../CLAUDE.md](../CLAUDE.md) §7). That placement is the whole design:

- **Metadata ctors live and die with the open configuration.** Opening a config creates the
  image and its factory; closing drops the image, and every metadata ctor with it. The
  value factory, by contrast, is process-wide and static.
- `LoadGuard` (the RAII load transaction) therefore rolls back type registration for free —
  a failed load leaves no half-registered types.

Its entries are per-metaobject-type ctors, which is how a `Catalog` definition can produce
reference / object / manager values ([../CLAUDE.md](../CLAUDE.md) §6, dynamic metaobject
values: `reference_to_clsid(metaID)` / `object_to_clsid` / …).

**Not yet adopted:** `ibMetaDataDataProcessor` / `ibMetaDataReport` still do a raw
`std::find_if` over their own `m_factoryCtors` with an `activeMetaData` fallback. The
header names them as the next adopters — same three lookups, same shape.

---

## 3a. The full cycle — who registers what, and when

The two registries above are the WHERE. This is the WHEN, end to end, because that is the part
that cannot be read off any single file.

### The metatype registers itself — once, at process start

`METADATA_TYPE_REGISTER(ibValueMetaObjectCatalog, "Catalog", g_metaCatalogCLSID)` (`catalogMetadata.cpp`,
bottom of file) creates a static `ibCtorMetaType<T>` in the VALUE factory. Static initialisation:
this happens before any configuration exists, and it is what makes `Catalog` a name the compiler
knows.

`ibValue::RegisterCtor` then raises the **registration event** on it:

```cpp
virtual void CallEvent(ibCtorObjectTypeEvent event) {      // metaCtor.h
    if (event == ibCtorObjectTypeEvent_Register) {
        T::OnRegisterObject(GetClassName(), this);         // the metatype's own hook
        RegisterAnyReference();                            // …and its family, if it has references
    }
    …
}
```

That event is the right place for anything a METATYPE owns — as opposed to anything one catalog
owns. `CatalogRef` (the family barrier, §5b) is registered exactly here, which is why it is
declarable in a configuration that has no catalogs yet.

⚠️ **The event fires BEFORE the ctor is inserted into the registry** (`valueFactory.cpp`:
`CallEvent` then `registry->Register`). Nested registration from inside the event is therefore safe
— it completes before the outer insert.

### What a metatype HAS — `s_features`

`RegisterAnyReference` does not consult a list of reference-bearing metatypes. Each metaobject class
declares its own set (`ibValueMetaObject::s_features`, `metaObject.h`), and everything deriving it
inherits:

| class | set |
|---|---|
| `ibValueMetaObject` | `None` — form, template, role |
| `ibValueMetaObjectRecordDataRef` | `Reference \| Manager` — an **enumeration stops here** |
| `ibValueMetaObjectRecordDataMutableRef` | `+ Object \| Selection` — catalog, document, charts |
| `ibValueMetaObjectRegisterData` | `Manager \| RecordSet \| Selection` — **no reference at all** |
| `ibValueMetaObjectRecordDataExt` | `Object \| Manager` — data processor, report |

Read with `if constexpr`, so it costs nothing at run time and a metatype that grows a capability
says so in one place. The bits line up with the `register*()` calls below — that is the invariant
worth keeping: a set that claims something nobody registers is a lie the compiler cannot catch.

### The metaOBJECT registers its types — per configuration, on run

When a configuration runs, every metaobject gets `OnBeforeRunMetaObject`, and that is where the
per-object ctors enter the METADATA factory (`objCtor.h` macros):

```cpp
registerReference();   // ibCtorMetaValueTypeReference → "CatalogRef.Goods"
registerManager();     // ibCtorMetaValueTypeManager   → "CatalogManager.Goods"
registerObject();      // … and Selection / RecordSet / TabularSection / RecordKey by kind
```

`registerReference()` also tells the FAMILY that this reference exists (`AddMember`), and
`unregisterReference()` takes it back — which is how `CatalogRef` answers "is this one of mine"
without a metadata lookup on the hot path.

`OnAfterCloseMetaObject` unregisters. And because the whole `m_factoryCtors` lives on `ibMetaImage`,
closing the configuration drops every metadata ctor at once — the per-object unregister matters only
while the configuration stays open (deleting an object in the designer).

### Looking a type up — the two registries together

```cpp
ibMetaData::IsRegisterCtor(clsid)   // MY registry OR ibValue's   ← "does this type exist at all"
ibMetaData::CreateObjectRef(clsid)  // MY ctor      OR ibValue's   ← same fallback
ibMetaData::GetTypeCtor(clsid)      // MY registry ONLY            ← "is this MINE"
```

⚠️ **The fallback is inside the factory, not around it** (`metadataFactory.cpp`). Asking
`IsRegisterCtor` when you meant "is this mine" always answers yes, and any redirect built on it is
dead code that looks alive. Use `GetTypeCtor` for ownership; `IsRegisterCtor` for existence.

### Creating a value from stored data

```
ibMetaData::Deserialize(node)          // the door: read the type, try MY registry
        ↓ not mine
ibValue::FromNode(node)                // the mechanism: the value-level registry
        ↓ nobody has it
ibBackendCoreException                 // never a quiet empty
```

The configuration types (a reference, an enum member) exist only in the first; everything built-in
in the second. See [serialization-io.md §4a](serialization-io.md).

---

## 4. `ibQueryableFactory` — query sources, and why it is different

`backend/query/queryableFactory.h`. Owned by `ibApplicationData`, reached through the
`query_sources` macro, token-gated construction (`ib::AppDataCtorToken`) — the same
ownership pattern as `ibLockManager`. Present even with no metadata open, because external
sources still resolve.

**It is NON-OWNING, and that is the point:**

> A DESCRIPTOR is **owned by the metaobject** (a member, like `m_queryable`), registered
> with the factory **by pointer** when the object runs and unregistered by pointer when it
> closes — no name / clsid round-trip. The factory just maps `(namespace, name) ->
> descriptor*` and asks it to CREATE the queryable.

So the factory holds no types and constructs nothing itself; it is an index of live
descriptors that each know how to build their own source.

### 4.1 What is queryable at all

The query language covers **only the relational metaclasses**: records with a data-reference
(catalogs, documents, charts of characteristic types / accounts, enumerations), registers,
and constants.

**Reports and data processors register no descriptor** — so a query against them simply
fails to resolve. That is by design, not an omission.

### 4.2 Descriptors

The standard one is a template that *contains* the metaobject's queryable and replaced its
former plain `m_queryable` field:

```cpp
ibMetaCommandDescriptor<TQueryable, TMeta>
```

A separate table (a register's balance / turnover / slice) or an external source uses its
**own** descriptor subclass, whose `CreateQueryable` builds a fresh configured queryable
from the call params — registered per concrete register / per external source.

### 4.3 Copy is deleted — deliberately

```cpp
// A source descriptor is `this`-BOUND (it holds a back-pointer to its metaobject) — it must
// NEVER be copied, else a copy would keep the ORIGINAL's identifiers. So copy is DELETED: a
// metaobject holding one is thereby non-copy-constructible, which forces the correct
// paste-via-factory path (a FRESH object, freshly bound with its own new metaID / guid) —
// copying identifiers can never go wrong silently.
```

This is a good pattern to recognise: **a deleted copy ctor used to make a wrong path
impossible to write.** Because the metaobject holds a descriptor, the metaobject itself
becomes non-copy-constructible, which forces paste to go through the factory and mint fresh
identity — rather than silently duplicating a metaID. See [copy-paste.md](copy-paste.md).

---

## 5. How an object is actually constructed — the two-phase `Init` idiom

This is the part to internalise: **the factory always calls the default constructor.**

```cpp
template <class T>
class ibCtorValueType : public ibCtorValueTypeBase {
    virtual ibValue* CreateObject() const { return new T(); }   // ← ALWAYS T()
};
```

There is no other option. The factory holds `ibValue**` + a count — it cannot know a
type's constructor signature, and C++ cannot dispatch on runtime arguments. So construction
is split in two:

| Phase | What | Who |
|---|---|---|
| 1. **Construct** | `new T()` — default ctor, no arguments | the ctor object |
| 2. **Initialise** | `Init(paParams, lSizeArray)` — the type reads its own arguments | the type |

```cpp
virtual bool Init() {
    if (m_pRef != nullptr && m_typeClass == ibValueTypes::TYPE_REFFER)
        return m_pRef->Init();                          // forward through a reference
    return true;                                        // default: nothing to do
}

virtual bool Init(ibValue** paParams, const long lSizeArray) {
    if (m_pRef != nullptr && m_typeClass == ibValueTypes::TYPE_REFFER)
        return m_pRef->Init(paParams, lSizeArray);
    return true;
}
```

Three things this encodes:

- **A type parses its own arguments.** `Init` receives the raw `ibValue**` and decides —
  arity, types, defaults, failure. That is why `ibValueEnumerationBase::Init` reads
  `paParams[0]->GetInteger()` ([enumerations.md § 2](enumerations.md)) and why a queryable
  descriptor builds "from the call-scoped params" ([§4.2](#42-descriptors)).
- **The base forwards through a reference.** Calling `Init` on a `TYPE_REFFER` initialises
  the object *under* it, so callers do not care whether they hold the value or a reference
  to it.
- **The default is `return true`** — "nothing to initialise" is success, not an error. A
  type that needs no arguments implements nothing.

So `New Array(10)` in a script becomes: `OPER_NEW` → resolve clsid → `CreateObject()`
(`new ibValueArray()`) → `Init(params, 1)` (the array reads its size). The factory
handles identity; the type handles meaning.

The entry points are the `CreateObjectRef` family (`compiler/value.h`), keyed the same
three ways as the registry (§2):

```cpp
static ibValue* CreateObjectRef(ibValue** paParams = nullptr, const long lSizeArray = 0);              // by T
static ibValue* CreateObjectRef(const ibClassID& clsid,       ibValue** paParams, const long lSize);   // by clsid
static ibValue* CreateObjectRef(const std::type_info& typeInfo, ibValue** paParams, const long lSize); // by type
static ibValue* CreateObjectRef(const wxString& className,     ibValue** paParams, const long lSize);  // by name
```

**Consequence for anyone adding a type:** give it a working default constructor and put the
real work in `Init`. A type whose only meaningful constructor takes arguments cannot be
created by the factory — so it cannot be `New`-ed, cannot be deserialised, and cannot be
pasted.

### 5.1 What else the ctor forwards to the type

`ibCtorValueType<T>` is a thin forwarder to **static** members of `T`:

```cpp
virtual wxIcon GetClassIcon() const  { return T::GetIconGroup(); }
virtual bool   IsTableValue() const  { return T::IsTableValue(); }   // compile-time trait forward
virtual void   CallEvent(ibCtorObjectTypeEvent event) {
    if      (event == ibCtorObjectTypeEvent_Register)   T::OnRegisterObject(GetClassName(), this);
    else if (event == ibCtorObjectTypeEvent_UnRegister) T::OnUnRegisterObject(GetClassName());
}
```

- `IsTableValue()` carries a comment worth keeping: it *"forwards the table trait to the
  concrete type — `T::IsTableValue()` resolves (via name lookup) to `ibValueModel`'s gate
  for models, to `ibValue`'s default otherwise. Pure compile-time: a non-model `T` never
  needs `ibValueModel` visible in its TU."* A trait query with no include cost.
- `CallEvent` gives a type static **register / unregister hooks** — the place a type sets up
  or tears down anything global when it enters or leaves the registry.

---

## 5a. A type is the GATE — `AllowValue` (landed 2026-08-05, replaces `CastValue`)

`ibCtorAbstractType` carries one virtual, and it takes a **class id** — not a value:

```cpp
virtual bool AllowValue(const ibClassID& clsid) const { return clsid == GetClassType(); }
```

- **`true`** — let it through, unconditionally: nothing else runs, no conversion, no type
  description, no metadata.
- **`false`** — not allowed as it stands; the caller falls back to the full conversion path,
  which converts or refuses.

**The default is the plain comparison** — the answer for almost every type, and the cheapest one:
a class-id comparison instead of building a type description per assignment.

**Why it lives on the registrar.** A type is the only thing that knows what it accepts, and the list
of types is *open* — a metaobject registers one per configuration, a plugin can register more. A
central switch would need editing for every new type and would be silently incomplete for the ones
it never heard of. Here a new type brings its rule with it.

**Caller:** the interpreter's `OPER_SET_TYPE`, i.e. a declared type applied to a slot
([script-language.md §4a](script-language.md)) — and it is the WHOLE of that opcode. The gate says
yes and the value is left exactly as it is; the gate says no and the interpreter raises a type
mismatch. There is no third branch: no primitive tag, no `AdjustValue`, no conversion attempted
behind the author's back.

⚠️ **A declaration no longer converts** (changed 2026-08-05). `Number x = "5"` is a type error
rather than a silent parse, and a declaration does not apply the type's qualifiers (scale, length)
either. A declaration states what a value IS; asking for a conversion is a different verb and reads
differently in the source.

⚠️ **Absence passes every gate.** An unset variable carries the `Undefined` class — not id 0 — so
`ib_clsid_is_absent()` answers for both spellings. Without it `Number x;` with no initialiser would
be a type error, which is why the check lives in one place rather than in each override.

### 5b. Barrier types — the families

The overrides are the **barriers**: registered types that create nothing and exist to be declared.

| | Where | Gate |
|---|---|---|
| `Any` | `compiler/typeAny.cpp` | everything |
| `AnyRef`, `AnyObject`, `AnyManager`, `AnyControl`, `AnyValue`, `AnyEnum` | `compiler/typeAny.cpp` | `clsid_kind(clsid) == kind` — one byte |
| `CatalogRef`, `DocumentRef`, `EnumerationRef`, … | `metaCtor.h` | membership in a recorded set |

**The metatype families arrive with their metatype.** `ibCtorMetaType<T>::CallEvent(Register)`
registers `<Name>Ref` when `T::s_hasReference` is true — a `static constexpr bool` that
`ibValueMetaObjectRecordDataRef` flips on and every reference-bearing metatype inherits. Nobody
keeps a list of which metatypes those are; a new one gets its family the day it derives the base.

**Membership is recorded, not derived.** A reference's class id is `(kind Reference, body = metaID)`
— the metaID says *which* metaobject, not which *kind* of one, so the kind cannot separate a
catalog's reference from a document's. So `registerReference()` tells the family
(`AddMember`) and `unregisterReference()` takes it back. The gate stays a set lookup: no metadata
on the hot path, and the distinction the declaration promised is actually enforced.

⚠️ **Lists have no barrier** — the platform has one list type, the dynamic list, and a family of
one is not a family.

**What this replaced.** The families used to be an *encoding*: a class id with an empty body, plus
`IsAnyOfKind` and a branch in the interpreter. That shape could not express a family narrower than a
kind (so "any catalog reference" was impossible), and every site that met one needed a special case.
All of it — `make_clsid_any`, `kIbClsidAny`, `ibClassKind_AnyKind`, `IsAnyOfKind`, the interpreter
branch, the parser's special name resolution — is gone.

---
## 6. Related ctor files

`characteristicCtor.{h,cpp}` · `constantCtor.{h,cpp}` · `compiler/enumFactory.{h,cpp}` ·
`compiler/typeCtor.h` (the macro family) · `appDataCtorToken.h` (owner-only construction
token).

---

## 7. Honest remainder

- **Three registries, three ownership models** — process-static (value), image-scoped
  (metadata), appData-owned-but-non-owning (queryable). Correct, but there is no single
  document-level statement of it in the code; this section is it.
- The `find_if` holdouts in §3.2 are the known drift: two factories doing by hand what the
  template does.
- `ibQueryableFactory`'s `(namespace, name)` key is a `std::map` — a linear-ish lookup where
  the ctor registry deliberately went to hash indices. Fine at current source counts; worth
  knowing if source counts grow.
