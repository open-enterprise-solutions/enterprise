# The reference register — one reference object per identity, per session

> **Scope:** the live table that makes a reference an identity instead of a copy — what it holds,
> why its lifetime needs no policy, where the table lives, why it is per session, and the two
> journal lines that measure it. Landed 2026-08-22.
>
> **Companions:** [reference-objects.md](reference-objects.md) (what a reference *is*, and the
> object it points at), [reference-key-metaid.md](reference-key-metaid.md) (the key's byte layout),
> [session-ownership.md](session-ownership.md) (whose session this is),
> [access-policy-rls.md](access-policy-rls.md) (the rights that make it per session),
> [technology-journal.md](technology-journal.md) (the journal it writes to).

---

## 0. Where it lives

| file | what is in it |
|---|---|
| `src/engine/backend/metaCollection/partial/reference/reference.h` | `ibReferenceRegistry` — `Find` / `Remember` / `Forget`; `m_registryTable` on the reference |
| `src/engine/backend/metaCollection/partial/reference/reference.cpp` | the table (`ibReferenceTable`), the key (`ibRefKey` + `ibRefKeyHash`), the three creation doors, the constructor / destructor calls, the cross-session read refusal |
| `src/engine/backend/metaCollection/partial/reference/referenceQuery.cpp` | `ReadData` — the row read and the `read` journal line |
| `src/engine/backend/session/session.h` | `ibSession::Local<T>()` — per-session state keyed by the asking type |
| `src/engine/backend/session/session.cpp` | `FindLocal` / `SetLocal` over `m_locals` |
| `src/engine/backend/clsid.h` | `IsReference` / `metaID_from_clsid` — the metaID read straight off a reference clsid |

---

## 1. What it is

**One reference object per `(metaID, guid)` per session, for as long as somebody holds it.**

A reference is an identity — this catalogue, this row — so two objects naming the same object are
not two things; they are one thing counted twice. Before the register they really were two: every
cell of every list built its own, and each one went to the database for its own copy of the same
row. The same nomenclature printed on forty lines was read forty times.

So the creation door asks first. If the object is already alive in this session, that one is
returned and nothing is built — the row it has already read serves every later holder for nothing.

`ibValueReferenceDataObject::Create` has three overloads, differing only in how the caller happens
to name the type (a metaobject in hand, a metaID, or the clsid stored in a row). All three ask the
register before they build.

## 2. Lifetime needs no policy — which is what makes it a register and not a cache

A reference is reference-counted already. When the last holder lets go, its destructor runs and the
object strikes itself out of the table. There is no eviction rule, no size limit, no age, nothing to
tune:

- the table holds exactly what is alive, and never one entry more;
- a base with a billion rows does not mean a billion entries — only what a form, a report or a
  script is holding at that moment, which is the population that existed anyway;
- the cost per live object is a key and a pointer.

`ReadData` therefore keeps **no cache of rows**. There used to be one, keyed by (metaobject, guid),
and it existed to stop the same row being read once per reference object holding that identity. The
register removed the twins it was compensating for: there is now one object per identity per
session, its own `m_initializedRef` says whether the read already happened, and a second answer that
goes stale after a write is worth nothing beside it.

**On a live reference the load mode is best-effort.** `ibReferenceLoad` says how much of the row the
caller wants now (`OnDemand` / `Unlatched` / `Latched`). Ask for `Unlatched` and get a reference that
is already latched, and `PrepareRef` returns at once — somebody else settled it, and there is one of
it. "Unlatched" was never a property of a request, only of an object.

## 3. Registration is in the constructor, not in the creation doors

`ibReferenceRegistry::Remember(this)` is called from the `ibValueReferenceDataObject` constructor,
and `Forget(this)` from its destructor — nowhere else. Every reference is born through that
constructor, the value-ctor path included, so one line covers every way of making one. Registered
per door instead, one door forgotten is a twin nobody ever finds again.

Two consequences that are easy to get wrong:

- **Both read the members, not the accessors.** In the constructor a virtual call answers for the
  class being built, so a subclass overriding `GetMetaObject` / `GetGuid` would be filed under the
  base's answer and looked up under its own. In the destructor the derived part is already gone and
  a virtual call is undefined behaviour. `ibReferenceRegistry` is a friend for this reason.
- **`Forget` erases from the table the reference registered in**, held on the reference as
  `std::shared_ptr<void> m_registryTable`, not from whatever session is current at that moment: a
  value can travel and be released elsewhere. Holding the table (rather than the session) also keeps
  it alive to be struck from. The erase is conditional on the entry still pointing at this object,
  so a reference born before there was a session — registered nowhere — cannot remove a living one.

**An EMPTY reference is never registered.** An all-zero guid is what empty *is*: it has no row,
nothing to share, and two empty ones are interchangeable. So the table's population is "objects
being looked at", not "references alive". A reference built with no session at all (bring-up, a
tool, a test) registers nowhere and behaves exactly as references did before the register existed.

## 4. Hashed, not scanned

The key is the raw identity — the metaID plus the 16-byte guid (`ibGuidImpl`, a pinned POD; see the
`static_assert` in `guid.h`) — with no rendered string anywhere, so a lookup allocates nothing:

```cpp
struct ibRefKey { ibMetaID m_metaId; ibGuidImpl m_guid; };

struct ibReferenceTable {
    std::unordered_map<ibRefKey, ibValueReferenceDataObject*, ibRefKeyHash> m_live;
};
```

An earlier attempt walked an array on every creation. A thousand live references and a thousand more
being built is a million comparisons — slower than the database read it was meant to save. Looked up
by key, the cost does not depend on how many are alive.

**The identifier is the primitive.** `Find(const ibMetaID&, const ibGuidImpl&)` is the real function
and the metaobject overload forwards to it. Which way round that is matters: a caller holding a
metaID used to have to search the metadata for the metaobject before it could ask a question the
table answers off the identifier alone — and on a hit the metaobject is never needed, because the
live reference is already holding it.

The stored-row door goes one step further. A reference clsid is constructive — its body IS the
metaID (`metaID_from_clsid` in `clsid.h`) — and the `_RRRef` blob IS the raw key, so both halves of
the table key come straight off the row:

```cpp
if (::IsReference(refClsid))
    if (ibValueReferenceDataObject* const live = ibReferenceRegistry::Find(
            static_cast<ibMetaID>(metaID_from_clsid(refClsid)), reference->m_guid))
        return ReadAsAsked(live, load);
```

A hit costs one hash probe: no `ibMetaData` search, no type-ctor lookup. Only a miss pays for
resolving the metaobject, and only a miss needs one. This door runs once per reference cell of every
list and every report.

## 5. Per session — and that is rights, not caution

Row-level access decides what a given user may read, and a row he may not read comes back as **"not
found"**, deliberately indistinguishable from a deleted one.

Sharing reference OBJECTS between sessions would be harmless — a reference is a type and a guid.
Sharing what they have READ is not, and the read row is the whole point of the register. One
borrowed pointer would let the session that may not see the row read `false` into the object, after
which the session that MAY see it shows "not found" for something plainly in front of it. That
breaks the reference for the user who is entitled to it, which is the worse of the two outcomes by
far.

So `PrepareRef` refuses to read when it is asked from a session other than the one the reference
belongs to, and says so — an error line in the technology journal, under the source `reference`:

```
refused: read attempted from a session other than the one it belongs to <metaID:guid>
```

The refusal costs the borrower nothing it is owed. **The road for a reference between sessions is
serialisation:** it travels as type + guid (`DoSerialize` / `DoDeserialize` carry the guid, the type
is in the header) and is rebuilt on the far side as that session's own object, read under that
session's rights. Only a raw pointer handed across in process lands on the refusal, and that is a
defect at the handing-over.

## 6. The table lives in the session's by-type store

`ibSession::Local<T>()` returns per-session state keyed by the asking type, made on first use and
destroyed with the session:

```cpp
template <class T>
std::shared_ptr<T> Local(bool createIfMissing = true);   // key: std::type_index(typeid(T))
```

The reference table is its first user; `TableOfCurrentSession()` in `reference.cpp` is the whole of
the wiring, and returns null when there is no session.

**The session does not know what it is holding, deliberately.** A named member per subsystem would
make `session.h` the list of everything that happens to want per-session state, and the next
subsystem would add a second member and a dependency on its header. The type is the key, so a new
one costs nothing in the session and cannot collide with another. The map (`m_locals`) holds the
only owning pointer, so everything parked there is released when the session goes.

**Not locked, and does not need to be.** A session is leased to one worker at a time (see
`workerPoolHeadless.h`), so there is no concurrent reader by construction rather than by discipline.
A lock here would sit on the busiest path in the engine and cost more than the read it saves.

## 7. How it is measured

Two lines under the journal source `reference` (see [technology-journal.md](technology-journal.md)):

| line | written by | means |
|---|---|---|
| `read %s <%i>` (guid, metaID) | `ReadData`, after a row comes back | one row was actually fetched |
| `hit %s <%i>` (guid, metaID) | `ibReferenceRegistry::Find`, on a hit | one ask was answered by a live object somebody already had |

Reads are rows fetched; hits are asks answered without fetching. Forty reads under one print means
nothing was being shared on that path and the register is buying nothing there — which is a fact a
run answers, rather than an argument about how the mechanism ought to behave.

A read that FAILED is a third thing and goes to the logger instead (`ibLogger::Error`, source
`reference`, with the physical table name). "There is no such row" is an ordinary answer and stays
quiet; "the row could not be read" is a fault and is said out loud.
