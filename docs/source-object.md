# Source object — the self-describing data node

> **Scope:** `ibSourceObject` / `ibSourceDataObject` / `ibSourceExplorer`
> (`backend/srcObject.h`, `backend/srcDataObject.h`) — what a form binds *to*, and how a
> dotted path walks it.
> Companions: [form-attribute-binding.md](form-attribute-binding.md) (the binding arc),
> [descriptions.md § 3](descriptions.md) (`ibSourceDescription` / `ibSourceHop`),
> [form-editor.md § 5](form-editor.md) (drag-to-create).
> This is foundation code.

---

## 1. Two layers, deliberately thin

```cpp
class ibSourceObject {                                   // srcObject.h — the MINIMUM
    virtual const ibMetaData* GetSourceMetaData() const = 0;
    virtual const ibValueMetaObjectCompositeData* GetSourceMetaObject() const = 0;
    virtual ibClassID GetSourceClassType() const = 0;
    virtual wxString  GetSourceCaption() const = 0;
};

class BACKEND_API ibSourceDataObject : public ibSourceObject {   // srcDataObject.h — a concrete source
    virtual const ibValueMetaObjectGenericData* GetSourceMetaObject() const = 0;   // covariant narrowing
    virtual const ibSourceExplorer* GetSourceExplorer() const = 0;
    …
};
```

`ibSourceObject` answers "what am I" in four calls. `ibSourceDataObject` is **a concrete
source instance, mostly metadata-independent** — the self-describing node a binding hops
over.

`ibTabularObject` is the parallel contract for tabular sections — same four questions, a
different shape.

Two implementation notes that are load-bearing:

- **It was extracted out of `commonObject.h`** so a reference value can inherit it without a
  cycle (`commonObject.h` includes `reference.h` before this class).
- **`GetSourceMetaObject()` is covariant**: the base returns `CompositeData*`, this one
  narrows to `GenericData*`. That works because `genericData.h` is included **complete** and
  forward-declares `ibSourceDataObject` back — so no cast is needed anywhere, and no cycle
  forms.

---

## 2. `ibSourceExplorer` — the column template, metadata-FREE

Nested inside `ibSourceDataObject` — "a node is only ever produced by / bound to a source,
so the type lives inside the source's type".

> **METADATA-FREE:** a node holds plain values (name / synonym / id / type) + UI flags; a
> column is appended from the neutral `ibBackendQueryColumn` (a metaobject attribute **is**
> one, a queryable column **is** one) — **never a metaobject pointer**.

```cpp
struct ibSourceInfo {
    wxString          m_srcName;
    wxString          m_srcSynonym;
    ibMetaID          m_mid = wxNOT_FOUND;
    ibTypeDescription m_typeDesc;                 // ← what may live here (descriptions.md §2)
    int               m_flags = eSrcEnabled | eSrcVisible | eSrcSelect;
    ibSourceDataObject* m_owner = nullptr;        // source this node fetches its live value FROM
    const ibBackendSourceColumn* m_col = nullptr; // descriptor it was built FROM; null for a plain-value node
};
```

**This is the whole reason end-user dot-paths work.** Because a node is plain data, a source
that has no metadata at all (a queryable, a computed table, an external source) produces the
same nodes as a Catalog does — so the form, the picker and the walker never ask "are you
metadata?".

### 2.1 Flags — one int, on purpose

```cpp
enum ibSourceFlag {
    eSrcEnabled      = 1 << 0,
    eSrcVisible      = 1 << 1,
    eSrcTableSection = 1 << 2,
    eSrcSelect       = 1 << 3,
    eSrcDefault      = 1 << 4,   // this node's value is a DEFAULT — substituted at new-form init
    eSrcChoiceMode   = 1 << 5,   // a TABLE node is a value PICKER: the tablebox shows Select first
};
```

Packed into **one `int` instead of a bool per flag**: the node struct is copied per column,
and separate bools plus padding would cost ~8 bytes each. Room is left for more flags —
including a future "how to fetch the value" mode.

`eSrcDefault` is the interesting one: when the constructor generates a **new** form it reads
the explorer and substitutes that value at init. **Which values are defaults is decided per
object** — each source flags its own nodes.

### 2.2 Construction is by plain values only

```cpp
// The one value ctor — every node is built from plain values + flags, no metaobject.
ibSourceExplorer(const wxString& name, const wxString& synonym, const ibMetaID& id,
                 const ibTypeDescription& typeDesc, bool tableSection = false, bool select = true,
                 bool enabled = true, bool visible = true);

// Queryable-style node: the name doubles as the synonym.
ibSourceExplorer(const wxString& name, const ibMetaID& id, const ibTypeDescription& typeDesc);
```

The second ctor is the tell: a queryable column has no synonym, so name serves as both. The
type accommodates sources that carry less information — it does not demand metadata's
richness.

`m_sourceExplorer` is **empty (no allocation) until first used** — a source that is never
browsed costs nothing.

---

## 3. The walk — hop by hop

```
GetSourceExplorer()  →  node by id  →  GetValue  →  next source  →  (repeat)
```

A binding is a `std::vector<ibSourceHop>`, each hop `{id, expected type}`
([descriptions.md § 3](descriptions.md)). The walk is:

```cpp
virtual bool GetValueBySourceHop(const ibSourceHop& hop, ibValue& out) const { return false; }
virtual bool SetValueBySourceHop(const ibSourceHop& hop, const ibValue& value)  { return false; }

bool GetValueByPath(const std::vector<ibSourceHop>& path, ibValue& pvarMetaVal) const;   // NOT virtual
```

**`GetValueByPath` is non-virtual; `GetValueBySourceHop` is the per-source virtual.** The
path walk is one algorithm; each source only answers "give me the value at *this* hop, and
hand me the next source".

> ⚠ **The base returns `false`.** A source that does not override `GetValueBySourceHop`
> resolves **nothing** — a bound control on it reads as unset. That is the exact shape of the
> "`<not selected>`" class of bug: the mechanism is fine, a particular source simply never
> implemented the hop. When a binding mysteriously does not resolve, check whether *that*
> source overrides it before looking at the walker.

`m_owner` on the node is what makes the chain work: it is set by `GetSourceExplorer` and
names the source the node fetches its **live** value from — so a node is not just a
description, it knows who to ask.

---

## 4. What else a source carries

- **Locking** — `m_formLockHandle` / `TryAcquireFormLock(ibLockMode)`
  ([record-locks.md](record-locks.md)): a source is what a form holds a record lock on.
- **Identity** — `GetGuid()` via `ibUniqueKey`.
- **Caption** — `GetSourceCaption()` for the form title / picker header.

---

## 5. Honest remainder

- The two overlapping contracts (`ibSourceObject` and `ibTabularObject` both declaring
  `GetSourceMetaData` / `GetSourceMetaObject`) are near-duplicates that do not share a base —
  a restructuring candidate, but the split is intentional today.
- `GetValueBySourceHop` defaulting to `false` (§3) is a **silent** opt-out: a missing
  override is not a compile error and produces an empty read rather than a diagnostic.
  Cheapest possible improvement: a debug assert on the base.
- `ibSourceInfo::m_col` is a **raw, non-owning** pointer to the descriptor the node was built
  from. It is null for plain-value nodes; its lifetime is the source's, so a node must not
  outlive its source.
