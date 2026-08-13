# Source object — the self-describing data node

> **Scope:** `ibSourceObject` / `ibSourceDataObject` / `ibSourceExplorer` / `ibTabularDataObject`
> (`backend/srcObject.h`, `backend/srcDataObject.h`, `backend/tabularDataObject.h`) — what a
> form binds *to*, and how a dotted path walks it.
> Companions: [form-attribute-binding.md](form-attribute-binding.md) (the binding arc),
> [table-model.md § 3a](table-model.md) (the table side: which seam a model overrides),
> [descriptions.md § 3](descriptions.md) (`ibSourceDescription` / `ibSourceHop`),
> [form-engine.md § 2](form-engine.md) (who BUILDS the source a form gets, per form kind),
> [form-editor.md § 5](form-editor.md) (drag-to-create).
> This is foundation code. The walk (§ 3.1–3.6) describes the code as of **2026-08-13**.

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
static bool ResolvePath(const ibValue& start, const std::vector<ibSourceHop>& path,
                        size_t from, ibValue& out);                                      // the shared loop
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

### 3.1 `ResolvePath` — the blind loop

`ResolvePath` is an inline static in `srcDataObject.h`. It is the ONE deep walk in the
engine, and its whole body is this:

```cpp
ibValue current = start;
for (size_t i = from; i < path.size(); ++i) {
    ibSourceDataObject* source = nullptr;
    current.ConvertToValue<ibSourceDataObject>(source);
    if (source == nullptr)
        return false;                                    // a primitive mid-path: you cannot dot into it
    ibValue next;
    if (!source->GetValueBySourceHop(path[i], next))     // the live value, or the pinned type's empty twin
        return false;
    current = next;
}
out = current;
return true;
```

**The loop consults no metadata.** No id → name, no `FindProp`, no `ibMetaData` at all: each
value **self-describes the next id**. The loop asks the current value to be an
`ibSourceDataObject`; if it is, it answers for its own field, and whatever it hands back is
asked the same question. That is why one loop walks a Catalog reference, a dynamic list's
queryable column and a RAM value-table column without knowing which is which — and why a hop
id may be a config metaID in one step and a RAM-local column id in the next.

It is `static` because the start need not be `this`: `GetValueByPath` resolves its own first
hop and feeds the rest in; a table feeds the row's cell (§ 3.2). The gateway `bool` is "did
the walk resolve", and `out` is meaningful **only on `true`**.

The command side has the same loop with `ibBackendCommandSender` / `GetCommandByHop`
substituted — see [command-interface.md](command-interface.md). Two doors of one shape; they
never share a value.

### 3.2 The two hops of a table — with a row, and without

A table value is per-row, so `ibTabularDataObject` (`backend/tabularDataObject.h`) carries
the row alongside the hop. There are **two** gates, not one, and the difference is the whole
design:

| Gate | Question | Who answers |
|---|---|---|
| `GetValueBySourceHop(item, hop, out)` | "the value of this column **in this row**" | `ibValueModel` — a direct cell read, `GetValueByMetaID(item, hop.m_id, out)` |
| `GetValueBySourceHop(hop, out)` | "something **of this column's type**, there is no row" | virtual with a **shared body** in `tabularDataObject.cpp` |

The row-less one is the **structure step**. A form under construction has no rows, and the
walk does not want a datum anyway: it wants something of the column's type so it can go on
reading *that* source's own columns. `Goods.Item.Name` resolves in the designer because the
`Item` column can be stepped into by type; nothing is read from a table that has no rows yet.

The runtime path is the mirror image: `ibTabularDataObject::GetValueByPath(item, path, from,
out)` resolves `path[from]` off the ROW (only the model can read a cell out of an
`ibDataViewItem`) and hands `from + 1 …` to `ResolvePath`. See
[table-model.md § 3a](table-model.md) for the table side and its caller.

The row-less step is written **once**, and the history is why it is worth saying: it existed
as three identical copies — on the value table, on the dynamic list and on the tabular
section — each added separately, each after the same symptom, a dotted reference column
reading back as `<not selected>` though the field picker showed it. A fourth kind of table
now gets it for free.

The design-time twin of the loop is `WalkColumns` (§ 3.5) — same stepping, collecting the
leaf column and the dotted name instead of a value.

### 3.3 The one place metadata is touched

The shared row-less body is one line:

```cpp
bool ibTabularDataObject::GetValueBySourceHop(const ibSourceHop& hop, ibValue& out) const
{
    return ibValueReferenceDataObject::CoerceHopType(hop, out, GetColumnTypeById(hop.m_id), GetSourceMetaData());
}
```

`CoerceHopType(hop, out, filter, metaData)` (`metaCollection/partial/reference/reference.cpp`)
is the **twin materialiser**: if `out` is not already of the pinned type `hop.m_type`, it
builds an empty typed reference of that type and substitutes it. It refuses in four cases,
and each refusal is load-bearing:

| Condition | Why it refuses |
|---|---|
| `hop.m_type` is not a reference | nothing to pin — keep whatever the id read gave |
| `filter` is non-empty and does not contain the pin | the field was **retyped** in the designer; fabricating the old twin would keep a dead path resolving as a phantom |
| the live value is already of the pinned type | never fabricate over a real value |
| the pin decodes to no target (`ConvertToMetaIds`) | no metadata, or a pin that is not a resolvable reference |

The `filter` is the table's own answer to "what does this column accept"
(`GetColumnTypeById`); a metadata-fixed field (record / reference gate — its type cannot be
retyped at runtime) passes an empty filter and skips the check.

**This is the single point in the dot-walk where `ibMetaData` is involved, and the pointer
comes from the caller's own `GetSourceMetaData()`.** That is what keeps the source layer
metadata-free: `srcDataObject` never creates a value and never decodes a clsid; the
reference — already metadata-bound by nature — owns the creation, and each source hands it
the metadata it already has (a tabular section its owner metaobject's config, a dynamic list
the config captured with its source, a RAM value-table the ACTIVE config, since it has none
of its own). The clsid → target decode is `ConvertToMetaIds`, a **metadata** lookup through
the class factory, not a kind-byte shortcut off the clsid body — the shortcut misclassified
composite branches.

### 3.4 A reference is the default, not the law

The shared body builds a **reference** twin. That is one particular way a column can be
stepped into, and the `virtual` is precisely what keeps it from becoming the rule: a table
whose columns step differently overrides the row-less gate and materialises its own.

The case this is being held open for is concrete: a **filter / sort model bound to a form**,
carrying its own Field and Value rows. Its `Value` column has no fixed column type — what it
accepts is decided by the neighbouring `Field` cell. Such a model cannot answer through
`GetColumnTypeById` at all, and it must not be handed a reference twin by a mechanism that
decided on its behalf that every dot in the product is a reference.

Which seam to override, and how deep, is [table-model.md § 3a](table-model.md).

### 3.5 `WalkColumns` — the same walk over the explorer tree

`WalkColumns(path, from, leaf&, outText, outLeafIsTable, outContainerIsTable)`
(`srcDataObject.cpp`) is the design-time twin: it steps `path[from..]` through the source
EXPLORERS and collects the leaf descriptor plus the dotted display name. Two node shapes,
two steps:

- a node **with children** is a container (a tabular section). Its columns live under it in
  the SAME explorer, so `explorer = node` — but the walk also fetches the section's own value
  off the owner and keeps it as the `ibTabularDataObject` it converts to. From there the
  section itself answers for its columns: the step is a real one, through the object the
  previous hop yielded, instead of keeping the root and asking it about somebody else's rows.
- a **leaf reference** node hops into the target: whoever the walk is standing on answers —
  the table it stepped into (its column, row-lessly), else the node's owner — and the value
  that comes back vends the next explorer. Landing on a non-source ends the walk.

Values produced mid-walk are parked in a local vector so the explorers they vend outlive each
step; `leaf` points into the owning metaobject and is stable regardless.

`outLeafIsTable` / `outContainerIsTable` report facts a `leaf` pointer cannot carry (a
section has no column descriptor): whether the leaf is itself a table, and whether it sits
inside one — which is what decides the control class on a drop.

### 3.6 The known boundary — an intermediate table ends the walk

`ResolvePath` converts every intermediate value to `ibSourceDataObject`. A **table** is not
one (`ibTabularDataObject` is the parallel contract, § 1), so a hop that lands on a table
value stops the walk. `Section.Column` works because the table is the START of the walk and
the table's own gate resolves that first hop; `Ref.Section.Column` does not, because the
table turns up in the middle.

This is a boundary, not a bug to route around. A model that must be walkable **by the dot**
should present its fields **as a source** — fields by id, one value each — rather than as
rows. The moment it is rows, "which row" is a question the path does not carry an answer to.

Pinned by `tests/test_tabularHop.cpp` (the table starts the walk; a primitive cell ends it),
`tests/test_sourceHopChain.cpp` (row → reference → field, at one and two hops) and
`tests/test_sourceExplorer.cpp` (design-time `WalkColumns`).

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
