# Table model — one model, two source kinds, one fetch

> **Scope:** the architecture every OES table/list/tree runs on — the view-facing
> `ibDataViewModel` contract, the script-facing `ibValueModel` that bridges to it, and the
> single `RunComposerPage` fetch primitive with its DB and RAM realisations. This is a MAP of
> code that **already exists**, not a plan.
>
> **Companions:** [paging-design.md](paging-design.md) (the fetch/paging mechanics + the arc
> that built them — read it for cursor, prefetch deque and the lying scrollbar),
> [dynamic-list.md](dynamic-list.md) (the DB-backed list consumer + the source-command layer),
> [data-composer.md](data-composer.md) (L5 — the composer a fetch renders),
> [property-system.md](property-system.md) (a table's columns ARE property objects),
> [source-object.md](source-object.md) (the dot-walk a table starts — §3a here, §3 there),
> [connection-pool.md](connection-pool.md) (where the DB fetch's connection comes from),
> [job-manager.md](job-manager.md) (the rented run a page fetch is).
>
> **Status:** landed, but **still moving.** This is one of the most-churned subsystems in the
> engine — a long-running, constantly-evolving mechanism. Treat any specific below as a snapshot;
> the shape (§0) is the durable part. Every standard list/tree/register/value-table is an
> `ibValueModel` fetching through `RunComposerPage`; the legacy per-family models and the typed
> per-model `Fetch` are DELETED. Web HTTP fetch endpoint is the one deferred piece (§9, §10).
>
> **2026-07-31: every read leaves the thread that asked** — the first page included. One door on
> the model decides where it runs, one dispatcher in the control asks for it, one lock guards it.
> See § "Off the UI thread" here and [paging-design.md](paging-design.md) §9.9 for what the user
> sees while a portion is out.

---

## 0. Lineage — fork → provider → fetch → web

This mechanism has a history, and it explains its shape.

- **Born as a fork of `wxDataViewCtrl`** (wxWidgets' data-view). It always lived on the
  **frontend**; the backend side was an ad-hoc, crooked implementation bolted onto it.
- **It used to be a component part.** The backend model *inherited* the wx model class **and**
  the runtime value at once — two ownership/identity chains (parallel references) fighting over
  the same object. That coupling was a persistent source of bugs.
- **The provider was the fix.** `ibValueModel` stopped *being* a wx model and instead **owns a
  bridge** to it (`ibDataViewModelProviderImpl`, §2). The widget/view became a **bridge, not a
  base** — the backend model no longer inherits the toolkit, and the parallel-reference mess is
  gone. This decoupling is the load-bearing move the rest of the design rests on.
- **Then came the fetch** (`Get*Fetch`) — the key step — **and then pagination** (keyset cursor,
  prefetch deque, lying scrollbar; [paging-design.md](paging-design.md)).

**⭐ Fetch is the road to the web.** The reason the fetch matters most is not the desktop scroll —
it is that the backend became a **stateless fetch service**, and that same `Get*Fetch` primitive
is the exact contract the **web frontend** consumes over HTTP (batch by batch, no whole-table
load). One door for desktop, headless, and web. Paging was not a scroll optimisation — it was the
platform's **direct path to the web**, which is where OES is heading. The HTTP endpoint that
completes that path is the one piece still un-wired (§10).

---

## 1. The thesis — the composer is the principle, fetch is its bridge

The centre of gravity is the **L5 composer** (the store of source + filter + sort + group; see
[data-composer.md](data-composer.md)). A model is a thin **facade over its composer** — it holds
no filter/sort store of its own (§5) — and the data-view side (§2) is only how the front
*consumes* what the composer already holds: the model receives the composer's rows, wraps them,
and works with them as a data-view. `RunComposerPage` is the **bridge** the composer builds to
turn itself into a paged batch of rows — nothing more. Fetch does not decide anything; it renders
the composer.

That is why there is no longer a typed `Fetch(ibFetchRequest<TKey>)` per concrete model (catalog
list, enum list, register list, folder tree — see [paging-design.md](paging-design.md) §1–7 for
that history). All of it was **retired** into one composer-rendering primitive:

```cpp
// ibValueModel (model.h) — PURE VIRTUAL, realised per SOURCE KIND, not per table type.
// The body renders the model's composer to a page; the composer is the source of truth.
virtual unsigned int RunComposerPage(const ibDataViewItem& parent, const ibDataViewItem& anchor,
    int count, ibFetchDirection dir, ibDataViewItemArray& out) const = 0;
```

Two realisations, and only two:

| Realisation | Source | What it does | Rows it hands out |
|---|---|---|---|
| `ibValueModelCursor::RunComposerPage` (`modelDb.cpp`) | a queryable (DB) | render the composer → SQL → **keyset** page → walk driver rows | **COPY** nodes (`ibComposerNode`) |
| `ibValueModelStorage::RunComposerPage` (`modelRam.cpp`) | `ibRamValueStorage` (live nodes) | `ibDataRamComposer::ComputeOrder` filters + sorts the live rows **in place**, windowed by anchor | the **LIVE** storage node (the node IS the row) |

Every table type in the product — dynamic list, table-of-values, tabular section, register
recordset — is one of these two by inheritance (§7). Filter / sort / group are **not** per-model:
they live in the L5 composer (§5). This is the "one bus" applied to tabular data — add a table
kind by choosing a source realisation, not by writing a fetch.

---

## 2. Two layers, one object — the model bridge

A table is two collaborating bases fused into one runtime object:

| Layer | Class | Lives | Is |
|---|---|---|---|
| **View contract** | `ibDataViewModel` | `backend/modelView.h` | wx-neutral: what a data-view control needs (values, hierarchy, notifier, paged fetch) |
| **Script + composition** | `ibValueModel` | `backend/model.h` | `ibValue` + tabular command/data object: the model as a script value, owner of the L5 composer |

`ibValueModel` does **not** inherit `ibDataViewModel` — it *owns a bridge* to it, a nested
`ibDataViewModelProviderImpl m_modelProvider` (model.h) that forwards every view virtual
(`GetValue` / `GetParent` / `GetFirstFetch` / `GetFeatures` / …) to the owning value-model:

```cpp
virtual unsigned int GetFirstFetch(parent, anchor, count, out) const override {
    return m_ownerModel->GetFirstFetch(parent, anchor, count, out);   // → RunComposerPage
}
virtual bool IsPagedModel() const override { return true; }   // every ibValueModel is paged
```

So the control talks to `GetDataViewModel()` (the provider); the script talks to the
`ibValueModel`; both reach one truth. `IsTableValue()` is declared once on `ibValueModel` and
inherited by every subclass, so the class factory reports them all as tables by CLSID.

---

## 3. The row — `ibDataViewItem` / `ibDataViewObject`

Rows are **refcount-aware** and carry their own queries, so the model shrinks to "produce items,
accept writes, notify".

- **`ibDataViewObject : wxRefCounter`** (`modelView.h`) — the base every row/node subclass
  (`ibComposerNode`, `ibValueTreeNode`, `ibValueTableRow`) derives. Virtuals live **on the row**,
  not the model: `IsContainer()`, `GetParentItem()`, `IsEqualTo()` (logical row equality across
  re-fetch behind a fresh pointer), `IsAttached()` (row survives its model's Clear via refcount →
  a script path checks this before dereferencing a detached row).
- **`ibDataViewItem`** — a single-pointer handle over an `ibDataViewObject*` with three modes
  (`Empty` / `Refcounted` / `RawId`). The Refcounted mode IncRef/DecRefs on copy/dtor, so a copy
  **pins the row alive** across model resets — this is what makes selection survive an evicted
  row, breadcrumbs survive a rebuild, and paged replacement not dangle. `RawId` is the escape
  hatch for non-refcounted holders (virtual-list `wxUIntToPtr(row+1)` tags, legacy tree-store
  nodes) — copy/dtor skip refcount ops there.

`operator==` short-circuits on pointer match, then dispatches to the row's `IsEqualTo` — "same
business row" is the row's decision, not the model's.

---

## 3a. The dot-walk — the table STARTS it, the source finishes it

`ibValueModel` is an `ibTabularDataObject` (`backend/tabularDataObject.h`), and it is that
interface's **only derivative** in the product — the tests aside, every table in OES is a model.
`ibTabularDataObject` is deliberately a light interface and not the model: the walk in
`srcDataObject.cpp` needs one thing from a table, that it can answer for its own column, and
naming `ibValueModel` there would leak the whole model layer into a walk that stays
metadata-free.

A dotted column (`Goods.Item.Name`) is resolved by ONE call:

```cpp
bool ibTabularDataObject::GetValueByPath(const ibDataViewItem& item,
    const std::vector<ibSourceHop>& path, size_t from, ibValue& out) const
{
    if (from >= path.size())
        return false;
    ibValue current;
    if (!GetValueBySourceHop(item, path[from], current))   // MY row, MY column — nobody else can read this cell
        return false;
    return ibSourceDataObject::ResolvePath(current, path, from + 1, out);   // the rest is not the table's business
}
```

The split is the point. The **first** hop belongs to the table because only the model knows how
to read a cell out of an `ibDataViewItem`; every **deeper** hop goes through the shared blind
loop every dotted path in the engine uses. The table never asks what the cell IS — whatever it
yielded self-describes the rest ([source-object.md § 3.1](source-object.md)).

The caller is `ibValueModelTableBox::ResolveCellValue`
(`frontend/visualView/ctrl/tableBox.cpp`), once per visible dotted cell, with `from` = the
tablebox's own bound prefix length so the tail is row-relative. A column reaching only one hop
past the prefix is a plain cell the model already serves (`IsPathColumn` requires ≥ 2
row-relative hops); a column whose path diverges INSIDE the prefix is rooted at a form source
above the table (the header object) and is resolved once through the form, not per row.

### Three seams, three depths — override the shallowest that suffices

| Seam | Question it answers | Override when |
|---|---|---|
| `GetColumnTypeById(id)` | what my column **accepts** | never, normally — `ibValueModel` answers it once off `GetColumnCollection()` |
| `GetValueBySourceHop(hop, out)` | the row-less **structure** step | your columns step into something that is not a reference |
| `GetValueBySourceHop(item, hop, out)` | the **per-row value** | a cell is computed rather than stored |

**The shallowest one is already answered for everybody.** `ibValueModel::GetColumnTypeById`
asks its own column collection — `GetColumnCollection()->GetColumnByID(id)->GetColumnType()` —
and every model already keeps that collection in step with its truth:

| Model | Its column info's `GetColumnType()` reads |
|---|---|
| tabular section (`ibValueTabularSectionColumnInfo`) | the attribute's `GetTypeDesc()` |
| register recordset (`ibValueRecordSetRegisterColumnInfo`) | the attribute's `GetTypeDesc()` |
| dynamic list (`ibDynamicListColumns::ColInfo`) | the wrapped queryable column's `GetTypeDesc()` |
| value table (`ibValueModelTableColumnInfo`) | the `Type` property the user edits |

So a model normally overrides **nothing** to be walked by the dot. This too was briefly said
three times — three overrides re-scanning the same three collections by hand, which is the loop
`GetColumnByID` already runs.

The middle seam has a shared body (it builds a reference twin from the column's type; the one
metadata touch in the whole walk — [source-object.md § 3.3](source-object.md)), and it is
`virtual` so that a reference stays a *particular* case rather than the law
([source-object.md § 3.4](source-object.md) names the model this is held open for).

**A model may legitimately answer `false`.** At design time there is no row; if a model cannot
produce something of the column's type, refusing is the honest answer — the walk stops at that
hop and the designer simply offers no dot past that column. Fabricating a value so the dot
"works" would bind a path that resolves to nothing at runtime.

> ⚠ **C++ trap.** Declaring only ONE of the two `GetValueBySourceHop` overloads in a derived
> model **hides** the other for callers holding the derived type — name lookup stops at the
> first class that declares the name. `ibValueModel` declares the row-ful one, so today's
> callers reach the row-less one through an `ibTabularDataObject*` and nothing breaks; the
> moment a caller holds `ibValueModel*`, the fix is `using ibTabularDataObject::GetValueBySourceHop;`
> next to the override, not a second body.

An intermediate that is itself a table ends the walk — the shared loop steps through
`ibSourceDataObject`, which a table is not. See [source-object.md § 3.6](source-object.md) for
what to do about it (present fields as a source, not as rows).

---

## 4. The fetch — `Get*Fetch` → `RunComposerPage`

The control drives three methods (the `wxTreeCtrl` `GetFirstChild`+cookie+`GetNextChild`
pattern); the base derives the direction and routes all three into `RunComposerPage`:

```cpp
// ibValueModel default — a model with a source + composer needs NO override:
GetFirstFetch → RunComposerPage(…, Reset)
GetNextFetch  → anchor ? RunComposerPage(…, Forward) : RunComposerPage(…, Reset)
GetPrevFetch  → anchor ? RunComposerPage(…, Backward) : 0
```

- **`parent`** = the scope. Invalid item = the invisible root (top-level rows). The sentinel
  `s_constIgnoreParent` (modelView.h) = a FLAT scan of a *hierarchical* source (one `ORDER BY`
  over the whole table instead of recursing per folder) — the front's List view passes it.
- **`anchor`** = the keyset cursor: the last (Forward) / first (Backward) loaded row. Not a
  positional offset — a concurrent insert/delete can't make pages overlap or skip. On a sort
  by a non-PK column the anchor carries the sort-column values too.
- **`count`** = batch size; a return < `count` means that direction is exhausted.

The DB half positions the page with a keyset predicate (`WHERE … > ?`, `+1` probe row for
`hasMore`); the RAM half windows the in-place-sorted live vector by `ibComputePageWindow`. The
hierarchy scope (flat vs parent-scoped) rides on `ibReadPageRequest` (`m_flatScan` /
`m_hierarchyCol` / `m_hierarchyKey`), filled by the model.

**`RunComposerPage` is `const`.** It only READS the source and RETURNS nodes — no `const_cast`
on the model. `ResolveAnchorByKey(rowKey)` is the point-lookup that re-reads one row's sort
tuple so a page can be positioned AT a row by its key (selection restore after a child-form
save — the old per-list `FindRowValue` sort-value pre-read, moved into L5).

The backend model itself is stateless — the prefetch deque and scroll position live in the
control (§6).

### Off the UI thread — the model answers, the control has one door (landed 2026-07-31)

**One point of departure, direction inside the request.** `ibDataViewCtrl::DispatchPagedFetch`
takes an `ibFetchDirection` — `Reset` (the first page, and every reload after a sort or a filter),
`Forward`, `Backward` — builds ONE parcel (`ibPagedFetch`) and hands the work to
**`model->SubmitFetchAsync`**. The control's own `PagedFetchDir` is gone; the first page stopped
being a separate machine. The direction matters again only at the far end, where three result
handlers say what to do with the portion: fill from scratch, append at the tail, prepend at the
head.

The model answers WHERE the read runs, and that is the whole split — it lives where the knowledge
is, and it is the only place in the engine that knows a background job exists:

| | Answers | Because |
|---|---|---|
| `ibDataViewModel::SubmitFetchAsync` (base, `modelView.cpp`) | **a thread of the model's own** — one, owned, joined in the dtor and before the next read | "the thread that asked" is not always a UI thread with time to spare: the same forms render on the web server, where it is a session's worker and a blocking read holds up that session's whole answer |
| `ibValueModel::SubmitFetchAsync` (`model.cpp`) | **a rented run** | that is a RUNTIME table — it reads a DATABASE |
| `ibValueModelStorage::SubmitFetchAsync` (`model.h`) | **back to the base's thread** | RAM: there is no database on the other end, so a session and a pooled connection would be minted for nothing — and its rows are the LIVE storage nodes the view is holding, pinned by a non-atomic refcount |

A model also outlives its own read by construction: `ibValueModel` keeps the run handle and
cancels + waits it out in its destructor (the base joins its thread there). The control's alive
token answers for the CONTROL; nothing else answered for the model.

Pinned by `tests/test_modelFetchDoor.cpp`: the work leaves the calling thread; `GuardFetch`
serialises even when the model runs its units in parallel (the base serialises by joining, so the
lock can only be tested through a model that does not); the destructor waits for a read in flight.

⚠ On a web host the base's answer is the one to revisit — one thread per model is right for a
window and wrong for a server with a hundred tabs. It is a single virtual, which is the point of
having the door.

**One lock, in the door.** `ibDataViewModel::GuardFetch` wraps every unit of work handed over,
whoever runs it. A sort click is a Reset: the composer recomputes, a portion is re-fetched, the
view rebuilds its tree — and while the previous portion is still coming back, the next one waits
in the door. Nothing else takes it: not the mutators, not the composer, not the paint.

**A scroll burst is answered in full.** A request that arrives while one is out cannot be
dispatched — it would be anchored on the same row and fetch the same portion twice — so it is
remembered (`m_pagedFetchAgain*`) and re-asked by the delivery, with the anchor by then advanced.
Scroll three times and three portions arrive in turn; the exhaustion flag the result sets is what
ends the chain.

The rented run is `ibJobManager::StartBackground(..., ibJobTenancy::Tenant)`: no identity, no
runtime, no row in `sys_session`, and the caller's access policy borrowed rather than rebuilt. It
creates a session for one reason only — a session owns exactly one connection and the caller's is
busy being the caller's — and takes that connection on the CALLING thread, so a saturated pool is
a refusal in front of somebody who can act on it. **Where the caller lives does not enter into
it**: a list is a list in the Designer, in the thick client and in a browser tab, and the run is
used up, brings the data back and ends in all three. Anything that cannot be rented (no free
connection, no job manager at all — tests, headless) falls back to reading inline, and the
control is never told which happened. See [job-manager.md](job-manager.md) § tenancy.

The control therefore has ONE code point per read: raise the in-flight counters, hand over the
work, post the delivery back. The busy veil, the generation token and the counters are identical
for every kind of table it can hold — there is no second path to keep in step.

⚠ `SchedulePagedRefresh` deliberately does NOT use that door: it is a re-entry on the same
thread (capture restore state, arm the bootstrap — all control state), so it posts through
`wxTheApp->CallAfter`. Sending it through the fetch door would run a tree walk on a worker.

An earlier shape kept ONE sleeping reader session per window between portions
(`ibSession::Reader()`, removed): a session held open is one that can sit in Active Users
holding a connection with nobody able to tell working from stuck, and a run that ends by
construction cannot.

The worker touches nothing but the model — `GetNextFetch` / `GetPrevFetch` into a shared payload
— then `wxTheApp->CallAfter` marshals the delivery back. Through `wxTheApp`, not the control:
posting is safe from any thread, but *reading the control to ask it to post* is not. On the UI
thread the delivery decrements the in-flight counter and is **discarded when the generation token
moved** (a sort / filter / refresh wiped the buffer since submit) — the answer is to a question
nobody is asking any more.

Two details are load-bearing, and neither is stylistic:

- **An alive token, not `wxWeakRef`.** `m_aliveToken` is a `shared_ptr<bool>` the control owns
  and clears in its destructor. The worker only ever *copies* it, and it is read exclusively on
  the UI thread, where the destructor also runs — so "is the form still there" is answered with
  no cross-thread write at all. `wxWeakRef` un-links itself by WRITING into the tracked object
  when that object dies, which races a copy sitting on a worker.
- **A refcounted row is never released on a worker.** `ibDataViewItem` pins its node through
  `wxRefCounter`, whose `IncRef` / `DecRef` are a plain `++` / `--` — **not atomic**. So the
  anchor and the fetch parent live in a `shared_ptr<FetchRequest>` payload that both lambdas
  hold, instead of being captured into the worker lambda directly: the worker drops its copy as
  soon as it has posted, by which time the UI lambda already holds one, so the payload — and the
  two items in it — is always destroyed on the UI thread.

There is no per-fetch cancel handle, and none is wanted: a rented run ends by construction, and
the alive token makes a delivery already posted to the UI queue a no-op when it lands on a
control that is gone.

`ibValueModel::SubmitFetchAsync` still exists (`model.cpp`) and still routes through
`ibSession::Submit` — i.e. the caller's OWN session, which on the desktop *is* the wx main
thread. Its only caller now is the debounced `SchedulePagedRefresh`; it is not the fetch path.

**There is no UI dispatcher.** The former `SetUiDispatcher` / `DispatchToUi` hop on `model.h` was
REMOVED, not moved. It was a process-wide static, which is the wrong shape twice over: the
backend had to know what a host's UI thread is, and a web server — one process, MANY sessions —
has no single answer to give it. "Run this where the view lives" is already answered per session
and polymorphically by `ibSession::Submit` → `ibWorkerPool`: the GUI pool drains onto the wx main
thread, the headless pool onto that session's FIFO worker, and a host with no pool runs inline
because nothing is watching. A background read hands its result back by submitting to the session
that ASKED — captured when the read starts, never `ibSession::Current()`, which in the background
is the reading session, not the form's.

### The busy overlay

The control, not the model, knows a read is out: `IsFetchInFlight()` reads the two fetch
counters it incremented at dispatch. A second copy of that fact on the model could only ever
disagree. `UpdateBusyIndicator()` samples it on every idle pass (`OnIdleEvent`), so an idle list
costs nothing.

| Constant | Value | Why |
|---|---|---|
| `kBusyShowDelayMs` | 300 ms | under it the list simply updates and no spinner appears — the common case on a local base, and a spinner that flashes for two frames reads as a glitch |
| `kBusyTickMs` | 60 ms | animation cadence — continuous to the eye, without repainting a list 20×/s for decoration |
| `kBusySteps` | 24 | one turn of the arc ≈ 1.4 s at that cadence |

**The tick is also the watchdog.** `OnBusyTimer` re-checks `IsFetchInFlight()` before every
frame, which is what ends the spinner when a read dies without delivering (worker gone, pool
stopped, session torn down). A spinner that only stops on success can spin forever. What it
cannot catch is a read wedged in a socket — cancellation is cooperative and a blocking read never
sees the flag — that case belongs to the driver's timeout.

`DrawBusyOverlay(dc, area)` paints at the **tail of the table area's `OnPaint`**, after the rows:
a translucent white veil (alpha 140) plus a stroked accent arc drawn through `wxGraphicsContext`
(`wxSYS_COLOUR_HIGHLIGHT`, a 3/4 sweep rotated by the phase — a path, not a bitmap, so it scales
with DPI for free). No graphics backend → skip; never fail a paint.

Deliberately **not** a child window: one on top flickers on scroll under wxMSW and takes part in
focus, and this must do neither. There is no mouse capture — the list stays fully usable while
the overlay shows, and the previous rows stay legible underneath, which is what tells the user
WHICH list is loading and lets them keep reading the old answer.

---

## 4a. Dynamic data read — live cursor vs whole-list RAM snapshot

A DB list (`ibValueModelCursor`) is a **live keyset cursor** by default: each fetch reads one page from
the DB (§4). A `virtual bool IsDynamicRead()` (default `true`) gates a **fallback** — when a subclass
returns `false`, the model materialises the WHOLE result set once into an in-memory `ibRamValueStorage`
and serves every fetch / scroll / group from RAM. The DB half borrows the RAM half's paging; the two
kinds (§1) meet.

- **One primitive, two callers.** `ibValueModel::RunStoragePage(storage, composer, …)` — the RAM sibling
  of `RunComposerPage` — pages any in-memory storage. The native RAM model passes its own
  `m_storage`/`m_composer`; a Cursor with dynamic-read OFF passes its `m_snapshot`/`m_snapshotComposer`.
  One windowing rule, no duplication.
- **`EnsureSnapshot()`** renders the persistent filter + sort into ONE unbounded flat SELECT (grouping and
  the page limit dropped), walks every row into a copy node, and adopts it into `m_snapshot`. Grouping is
  mirrored onto the snapshot composer and rebuilt IN RAM (`RunStoragePage`); filter / sort are already
  baked into the materialised order.
- **Invalidation is free.** The snapshot re-materialises only when the view generation moves — a refresh /
  filter / sort change bumps it, a scroll does not. Scrolling reuses the snapshot; a settings change reloads
  it. (The silent `AddValue` bumps the counter per row, so the generation is captured AFTER the fill.)
- **A RESET re-reads it, generation or not (2026-08-03).** Free invalidation was too free: the generation is
  bumped by `RefetchAll` alone — sort, filter, settings — and a SAVE bumps nothing. So the path that matters
  most, an object form writing a row and the list re-pulling (`UpdateForm` → `SchedulePagedRefresh` → a Reset
  fetch), found the snapshot still valid and kept serving the pre-save rows: with dynamic read OFF a created
  element never appeared and an edited one showed its old cells until the user happened to sort. A Reset IS
  the re-read, so `RunComposerPage` drops `m_snapshotValid` on it; Forward / Backward keep reusing the
  materialisation, because a scroll is not a re-read and RAM paging is the whole point of this mode.
- **The toggle** is the dynamic list's `DynamicRead` designer property (default = live). OFF is the safety
  net for when cursor paging misbehaves, or a small / stable list where liveness doesn't matter — at the
  cost of loading the whole result into memory and a static (refresh-to-update) view. A self-hierarchy
  source flattens under it (the parent-ref tree is a live-cursor feature).

---

## 5. Filter / sort / group — L5, by field-path

A model carries `m_listSettings` (`ibValueListSettings`, `backend/composition/listFilter.h`) —
the script-visible container of **Filter** / **Order** / **Group**, a thin facade over the L5
composer. `RunComposerPage` reads the composer **directly every fetch**; the legacy per-model
`m_filterRow` / `m_sortOrder` are abolished. The settings object is a transactional dialog
buffer: `ibLoadSettingsFromComposer` on open (composer → buffer), `ibCommitSettingsToComposer`
on OK (buffer → composer, clear then re-apply); Cancel is a no-op.

Fields are **paths**: a dot-walk (`"Ref.Owner"`) resolves to an auto-JOIN on the door. Grouping
is any query-result field, not the table's parent column: a non-empty Group turns a flat list
into a drillable tree via `TOTALS BY <dim>` — **including aggregate-free pure grouping**. Drill
re-fetches scoped by the already-drilled dimension values. (A single plain scalar Elements grouping level over
a single source pages its groups SERVER-side — a keyset `GROUP BY dim ORDER BY dim LIMIT` (§10); reports /
dot-walk / multi-level / multi-source still load a whole level at once. Detail rows page normally.)

Header-click sort is committed entirely on the **front** (`ibValueModelTableBox::OnColumnClick`
pokes this model's composer + `RefetchAll`) — the model bridges no `{col, asc}` pair.

---

## 6. Notifier + refresh — pure push, single entry

`ibDataViewModelNotifier` (`modelView.h`) is **PURE PUSH**: the model tells the front WHAT
CHANGED (`ItemInserted` / `ItemDeleted` / `ItemChanged` / `ValueChanged` / `BeforeReset` /
`AfterReset` / `Cleared` / `Resort`) and nothing else. Everything the notifier used to *pull*
(selection, drill parent, page size, view mode) is gone — the control owns selection + page
size and reads them itself; commands receive the front's selection as an argument.

All refresh runs through one path:

```cpp
void RefetchAll() {   // ibValueModel
    BumpViewGeneration();               // RAM visible-view rebuilds on next fetch
    m_modelProvider->BeforeReset();     // control wipes its loaded window
    m_modelProvider->AfterReset();      // control dispatches a fresh GetFirstFetch
}
```

The old `CallRefreshModel` / `RefreshModel` / `RefreshItemModel` / `HandleOnScroll` are gone
(see [paging-design.md](paging-design.md) §8.8).

---

## 7. The consumers — all converge on one model

| Consumer | Source realisation | Also is | File |
|---|---|---|---|
| **Table-of-values** (`ibValueModelTable`) | `ibValueModelStorage` (RAM) | `ibSourceDataObject` (form source) + `ibPropertyObject` (columns serialise with the attribute) | `system/value/valueTable.h` |
| **Dynamic list** (`ibValueDynamicList`) | `ibValueModelCursor` (DB) | `ibSourceDataObject` + `ibPropertyObject`; metadata-blind, source-command layer | `system/value/valueDynamicList.{h,cpp}` |
| **Tabular section** (`ibValueTabularSectionDataObjectBase`) | RAM (`ibValueModelRamTableBase`) | a document/catalog's line collection | `metaCollection/…` |
| **Register recordset** (`ibValueRecordSetObject`) | RAM (`ibValueModelRamTableBase`) | a register's movement set | `metaCollection/…` |

The value-table and the dynamic list are **exact analogues**: both fuse model + form-source +
property-object, differing only in RAM-vs-DB. A value-table's **columns** are the clearest
demonstration of the property-system reuse — each `ibValueModelTableColumnInfo` is at once an
`ibPropertyObject` (Name/Caption/Type edit in the inspector), an `ibBackendSourceColumn` (the
bound tablebox reads its header/type through the same explorer seam a metadata field uses), and
serialises through the unified property node — single source of truth in the property variant,
nothing to mirror. GROUP folds the flat table into a tree "with one easy move" via `GetFeatures`.

The legacy per-family models (`ibValueListDataObjectEnumRef` / `…Ref` / `…RefDocument` /
`ibValueListRegisterObject` / `ibValueModelTreeDataObjectFolderRef`) are **DELETED**.

---

## 8. View modes + the frontend control

The model never sees "tree" or "list" — only "N rows under parent P from anchor A". The view
mode is a **front** concern, translated into fetch shape:

| Mode | Fetch pattern |
|---|---|
| **List** | `GetFirstFetch(parent = s_constIgnoreParent)` — flat scan, then incremental Next |
| **Tree** | `GetFirstFetch(parent = {})`; on expand `GetFirstFetch(parent = node)` |
| **Hierarchy** | `BuildAncestorBreadcrumb(top)` for the crumb + `GetFirstFetch(parent = top)` for content |

`ibDataViewCtrl` (the forked wxDataView, `frontend/win/ctrls/tableView.{h,cpp}` + `datavgen.cpp`)
owns the prefetch deque, the 3-state "lying" scrollbar (thumb forced to {Top,Middle,Bottom} from
`hasMoreFwd`/`hasMoreBwd`, since cursor paging has no absolute row count), soft-eviction of
off-window rows, and in-flight generation tokens. `ibValueModelTableBox`
(`frontend/visualView/ctrl/tableBox*.cpp`) is the form control that hosts it and runs the
source-command band. Details: [paging-design.md](paging-design.md) §8.

---

## 9. Where it lives

| File | Holds |
|---|---|
| `backend/modelView.{h,cpp}` | `ibDataViewModel`, `ibDataViewItem`, `ibDataViewObject`, notifier, `ibFetchDirection`, `s_constIgnoreParent` |
| `backend/tabularDataObject.{h,cpp}` | `ibTabularDataObject` — the two hop gates, the shared row-less body, `GetValueByPath` (§3a) |
| `backend/srcDataObject.{h,cpp}` | `ibSourceDataObject::ResolvePath` (the shared deep loop) + `WalkColumns` (its design-time twin) |
| `backend/model.h` + `model.cpp` | `ibValueModel`, the provider bridge, `Get*Fetch` defaults, `RefetchAll`, `ResolveAnchorByKey`, `m_listSettings` |
| `backend/modelDb.cpp` | `ibValueModelCursor::RunComposerPage` — DB keyset fetch |
| `backend/modelRam.cpp` | `ibValueModelStorage::RunComposerPage` — RAM in-place fetch; `ibValueModelRamTableBase` |
| `backend/composition/ramComposer.{h,cpp}` | `ibDataRamComposer::ComputeOrder` (RAM filter/sort) |
| `backend/composition/listFilter.h` | `ibValueListSettings`, `ibLoadSettingsFromComposer` / `ibCommitSettingsToComposer` |
| `system/value/valueTable.h` | `ibValueModelTable` (table-of-values) |
| `system/value/valueDynamicList.{h,cpp}` | `ibValueDynamicList` |
| `frontend/win/ctrls/tableView.{h,cpp}`, `datavgen.cpp` | `ibDataViewCtrl` — the forked control |
| `frontend/win/ctrls/dataview/datavgen.paged.cpp` | `PagedBootstrap` (decide → dispatch) + `PagedBootstrapApply`, `DispatchPagedFetch` (both → a rented run), the fwd/bwd result handlers, the busy indicator |
| `backend/job/jobManager.{h,cpp}` | `ibJobTenancy`, `StartBackground` — the rented run a page read is |
| `frontend/session/workerPoolGUI.{h,cpp}` | `ibWorkerPoolGUI` — inline on the wx main thread, `CallAfter` otherwise |
| `frontend/visualView/ctrl/tableBox*.cpp` | `ibValueModelTableBox` — the form control |

---

## 10. Honest remainder

- ~~**`PagedBootstrap` is still synchronous.**~~ **Landed 2026-07-31.** Every read the control
  makes now leaves the UI thread, the first page included. `PagedBootstrap` was split by WHO MAY
  TOUCH WHAT rather than by convenience — decide (UI) → read (worker) → apply (UI) — carried in
  one parcel, `ibPagedFetch` (`datavgen.paged.private.h`, shared by all three directions); the read is a free function
  so it cannot reach the control at all, and it gathers EVERYTHING the rebuild will need,
  including the ancestor breadcrumb and each tree crumb's children, which the auto-expand walk
  used to fetch per level in the middle of building the tree. The wipe moved to the END: the old
  rows stay legible under the busy veil until there is something to put in their place, and the
  wipe + rebuild + focus + scroll happen in one frozen transaction on arrival. Dispatch raises
  BOTH fetch counters — a bootstrap replaces the whole buffer, so neither end may be extended
  while the replacement is out (a backward portion arriving after the wipe would prepend rows
  from an ordering that no longer exists). An answer overtaken by a later refresh is dropped by
  the generation stamp; a read that throws leaves the list exactly as it was, with the reason in
  the journal. No job manager (tests, a headless embedder) → the model reads it on its own thread instead.
- **Web HTTP `/fetch` endpoint — not wired.** The universal `Get*Fetch` was designed to serve
  the web frontend over HTTP (`/fetch?parent=…&anchor=…&count=…` → JSON), but `wfrontend.cpp` is
  not yet on it (building the JSON schema ahead of a consumer would freeze it blind). Without
  paging the web client OOMs on large catalogs. See [paging-design.md](paging-design.md) §8.9.
- **Group-level paging — partial (server-side for the common case).** A single plain scalar **Elements**
  grouping level over a **single source** now pages its groups server-side — a keyset `GROUP BY dim ORDER BY
  dim [dim </> anchor] LIMIT count` (`ibDbTableProvider::ExecuteGroupLevelPage`, reached from the composer's
  single-scalar-dim TOTALS drill via `SelectAggregatePage`) — so the nomenclature-hierarchy tree no longer
  loads every group. Reports (measures), dot-walk / multi-level / multi-source groupings still load the whole
  level at once and RAM-fold. (A reference-spread ROLLUP totals now pushes down — single-source AND a co-located
  **JOIN** — the reference groups as ONE composite `ROLLUP((spread))` element, reassembled on read; a **single-
  source dot-walk** GROUP key pushes down too via a reference-join chain. Still fold: a co-located **UNION**
  reference (the branch projects one field), and dot-walk **aggregate inputs**.)
- **The `ibVisualHost` scrollbar flash on form open is NOT the data-view.** `ibDataViewCtrl`'s
  own vertical scrollbar range is always 0; the flash is the form host (`wxScrolledWindow`)
  setting a virtual size larger than its placeholder client during build. Tracked in
  [paging-design.md](paging-design.md) §8.8 — needs a form-host lifecycle change.
- **Absolute-row random access is intentionally absent.** Cursor pagination gives smooth scroll,
  not cheap "row 5000 of 10000". If a UI demand surfaces, an `OFFSET ?` anchor variant is the
  path (see [paging-design.md](paging-design.md) §6).
