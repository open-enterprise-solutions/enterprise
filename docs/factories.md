# Factories — how classes get registered and created

> **Scope:** the registries that turn a name / clsid / type into an object. Three of them,
> layered: the **value** factory (runtime objects), the **metadata** factory above it, and
> the **queryable** factory (query sources).
> Companions: [../CLAUDE.md](../CLAUDE.md) §6 (CLSID kind-typing), §7 (`ibMetaImage`),
> [script-value-types.md](script-value-types.md) (what gets registered),
> [query-language-arc.md](query-language-arc.md).
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
| `name` | **linear scan** | compile-time `CreateObject("Name")` — low frequency |

The name scan is linear *on purpose*: OES name comparison is **case-insensitive**, so it
cannot ride the case-sensitive clsid hash anyway, and it is not a hot path.

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
