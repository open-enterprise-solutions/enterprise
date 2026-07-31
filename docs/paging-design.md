# Paged data fetching for ibValueListDataObject / ibValueModelTreeDataObject

**Status:** **The universal `Get*Fetch` contract (§8) is what lives
in code today.** Sections 1-7 describe an earlier templated-buffer
design (`ibTableViewBuffer` / `ibTreeViewBuffer` / `ibPagingLog`)
that **did not survive** the §8 convergence — those symbols are
**not** in the current tree. The actual paging implementation is:
`ibFetchAnchor / ibFetchRequest / ibFetchResponse` templates in
`backend/model.h`, per-model `GetFirstFetch / GetNextFetch /
GetPrevFetch` virtuals on `ibDataViewModel` + `ibValueModel`,
control-driven prefetch + lying scrollbar inside `ibDataViewCtrl`.
Keep §1-7 as design history; treat §8 as the canonical reference.
Backend Fetch contract shared by desktop wxDataView fork, headless
callers, and the upcoming web frontend.

**Goal:** unified, well-behaved batched fetch for OES dynamic lists and trees.
The GUI must not stall on scroll, must not double-fetch, must support lazy tree
expansion, and must remain coherent when filter / sort / view-mode change.

## Table of contents

1. [Current state](#1-current-state)
2. [Known issues](#2-known-issues)
3. [Target design](#3-target-design)
4. [Migration plan](#4-migration-plan)
5. [Open questions / pre-work](#5-open-questions--pre-work)
6. [Out of scope](#6-out-of-scope)
7. [Files touched (planned)](#7-files-touched-planned)
8. [Evolved architecture — universal `Get*Fetch` (2026-05-05)](#8-evolved-architecture--universal-getfetch-2026-05-05)

## 1. Current state

### Backend (where data lives)

* `ibValueModel` — script-exposed base (`ibValue` + `ibStandardCommandSource`),
  owns `m_filterRow`, `m_sortOrder`, dispatches `ExecuteAction`.
* `ibValueModelTableBase` / `ibValueModelTreeBase` — row/tree storage with
  `m_nodeValues` (vector) / `m_root` (tree).
* `ibValueListDataObject` — "Dynamic list" base. Concrete subclasses:
  * `ibValueListDataObjectEnumRef` — enum list (read-only).
  * `ibValueListDataObjectRef` — mutable catalog list.
  * `ibValueListRegisterObject` — register list.
* `ibValueModelTreeDataObject` — tree base; concrete: `ibValueModelTreeDataObjectFolderRef`.
* Each concrete class overrides:
  * `RefreshModel(topItem, countPerPage)` — full window reload.
  * `RefreshItemModel(topItem, currentItem, countPerPage, scroll)` — adjacent
    batch in scroll direction. **This is the only "paging" hook today.**

### Frontend (who triggers fetch)

* `ibValueModelTableBox::OnIdle` — initial fetch on `m_dataViewSizeChanged`
  via `m_tableModel->CallRefreshModel(top, count)`. Selection-restore chain
  (`createdValue` → `ownerControl->GetControlValue()` → `changedValue` →
  `FindRowValue`) runs after refresh.
* `ibValueModelTableBox::HandleOnScroll` — scroll-event handler, computes
  direction `±1`, calls `CallRefreshItemModel(top, focused, count, ±1)`,
  re-locates current line via `FindRowValue` if it became invalid.

### Stable identity

Each row carries a stable PK:
* `ibValueTableEnumRow::m_objGuid` (ibGuid)
* `ibValueTableListRow::m_objGuid` (ibGuid)
* `ibValueTableKeyRow::m_nodeKeys` + `GetUniquePairKey()` (composite, registers)
* `ibValueTreeListNode::m_objGuid` + `m_container` hint (ibGuid + bool)

Cursor pagination is feasible — we have stable keys for every row class.

### View modes — frontend concern, not the model's

The model does not know how the GUI renders rows. It returns data as
requested; the frontend decides whether to render it as a flat list, a
tree, or a folder-only hierarchy.

**`ibDataViewViewMode`** (`frontend/win/ctrls/dataview/dataview.h`) —
GUI rendering toggle. Stays in frontend, not part of `ibFetchRequest`.

**`ibValueModelTreeDataObjectFolderRef::{LIST_FOLDER, LIST_ITEM,
LIST_ITEM_FOLDER}`** — which form variant the model was constructed for:
list of items, list of folders, or both. Set in the ctor (`int
listMode`); this determines what rows the model can ever return.

When the user toggles GUI view mode, frontend translates that into
data-level requests on the model:
* expand a node → `Fetch(parentItem = node, direction = Reset, ...)`.
* scroll → `Fetch(anchor = lastVisible, direction = Forward, ...)`.
* switch to flat list → `Fetch(parentItem = {}, direction = Reset, ...)`.
* hierarchical (folders only) navigation → typically a model instance
  with `m_listMode = LIST_FOLDER`; switching may swap models.

The model never sees "tree" or "list" — only "give me N rows under
parent P starting at anchor A".

## 2. Known issues

| # | issue | symptom |
|---|-------|---------|
| 1 | Sync fetch on UI thread | Scroll stutters on remote DB. |
| 2 | No prefetch | Fetch fires only when viewport hits empty zone, user sees "loading". |
| 3 | No throttling | Held arrow-key fires N SQLs back-to-back. |
| 4 | "Next-row" fetch returns one row at a time | Not batched, scroll never feels smooth. |
| 5 | `top_item` as anchor is position-based | Concurrent insert/delete makes pages overlap or skip rows. |
| 6 | Tree shows everything at once | `m_container` is hinted but `GetChildren` is not lazy — full tree loaded up-front. |
| 7 | View mode awareness leaks into concrete `RefreshModel` | Backend has `LIST_*`, frontend has `ibDataView*ViewMode`; no single contract carries it. |
| 8 | `FindRowValue` may be linear-scan | After paging, target row may be outside buffer → re-locate fails → selection lost on filter/sort change. |
| 9 | Eviction not implemented | Buffer grows monotonically; long-lived list eats memory. |
| 10 | Refcount-pin for selection not enforced | If eviction lands, dangling `ibDataViewItem` pointer. |

## 3. Target design

```
┌──── frontend (ibValueModelTableBox) ─────────────────────────┐
│  OnIdle / OnSize / HandleOnScroll                            │
│         │                                                    │
│         └─→ model->NotifyViewportChanged(viewport)           │
└─────────────────────────────────────────────────────────────┘
                                │
┌──── backend base (ibValueModelTableBase / TreeBase) ─────────┐
│  ibViewBuffer (new)                                          │
│    - holds rows around viewport ± prefetch margin            │
│    - decides: no-op | prefetch | reset+initial               │
│    - submits fetch through ibSession::Submit (worker pool)   │
│    - on result: ItemsAdded via m_modelProvider, eviction     │
│         │                                                    │
│         └─→ Fetch(req)  (virtual)                            │
└─────────────────────────────────────────────────────────────┘
                                │
┌──── concrete (EnumRef / Ref / Register / FolderRef) ─────────┐
│  Builds SQL WHERE / ORDER from request, returns rows.        │
│  Carries view-mode interpretation and filter snapshot into   │
│  the SQL.                                                    │
└─────────────────────────────────────────────────────────────┘
```

### 3.1. Fetch contract

Templated by row-key type. Type safety stays at the concrete-class
level; the frontend talks to the model through a small set of
non-templated virtuals (§3.2) that internally route to the typed buffer.

```cpp
// Stable, opaque cursor for pagination.
template <class TKey>
struct ibFetchAnchor {
    bool                    empty       = true;   // true = no anchor, fetch top
    TKey                    key{};                // last row's stable key
    std::vector<ibValue>    sortValues;           // sort-col values for sort-by-non-PK
};

enum class ibFetchDirection : int8_t {
    Reset    = 0,   // discard buffer, fetch initial window at anchor
    Forward  = 1,   // append after anchor
    Backward = -1   // prepend before anchor
};

template <class TKey>
struct ibFetchRequest {
    ibFetchAnchor<TKey>     anchor;
    ibFetchDirection        direction = ibFetchDirection::Reset;
    int                     count     = 0;     // batch size
};

template <class TKey, class TRow>
struct ibFetchResponse {
    std::vector<TRow*>  rows;
    bool                hasMore = false;
};
```

Filter / sort snapshots are read directly from the model's own
`m_filterRow` / `m_sortOrder` members at Fetch time — request carries
only anchor + direction + batch.  Earlier drafts had `filter` / `sort` /
`parentItem` / `wantTotalCount` fields on the request and `totalCount` /
`stale` on the response; none were ever wired up and they were removed
during the post-paging audit.  Tree expansion is driven by the
universal `Get*Fetch(parent, …)` API at the model-virtual layer (§8.2)
rather than via a request field — keeps the typed Fetch lean, single-
responsibility.

`TKey` per concrete model:
* `ibGuid` — catalog / enum / folder rows.
* `ibUniqueKeyPair` — register rows.

`TRow` per concrete model:
* `ibValueTableRow` (or subclass) — table.
* `ibValueTreeNode`  (or subclass) — tree.

Concrete classes own a typed buffer and expose a typed `Fetch`:
```cpp
class ibValueListDataObjectRef : public ibValueListDataObject {
    ibTableViewBuffer<ibGuid, ibValueTableListRow> m_buffer;

    // Typed; called by the typed buffer, not a virtual on the base.
    ibFetchResponse<ibGuid, ibValueTableListRow>
        Fetch(const ibFetchRequest<ibGuid>& req);
    // ...
};
```

`Fetch` is **not** a virtual on `ibValueModel*` — templates and virtuals
don't combine. Base virtuals are reserved for non-templated frontend
hooks (§3.2). The buffer calls concrete `Fetch` via static dispatch
because it is templated by `TKey`/`TRow` and instantiated inside the
concrete class.

`Fetch` runs on a worker thread; no UI access permitted from inside.

### 3.2. Buffer manager

Two templated classes — table and tree have different storage shapes
(linear deque vs per-node children):

```cpp
template <class TKey, class TRow>  class ibTableViewBuffer;
template <class TKey, class TNode> class ibTreeViewBuffer;
```

Concrete models own a fully-instantiated buffer (see `ibValueListDataObjectRef`
example in §3.1). Buffer calls concrete `Fetch` directly — typed, not
virtual.

`ibTableViewBuffer<TKey, TRow>` state:
* `std::deque<TRow*> m_rows` — current window of loaded rows.
* `int m_viewportTopIdx`, `m_viewportSize` — last reported viewport.
* `bool m_hasMoreFwd`, `m_hasMoreBwd` — known boundaries.
* `int m_inFlightFetches` — to dedupe concurrent requests.
* `int m_bufferMargin` — typically `2 * viewportSize`, prefetch trigger at
  `viewportSize / 2` from edge.

`ibTreeViewBuffer<TKey, TNode>` state is per-parent-node — see §3.3.

Buffer's templated public API (table; tree analogous):
```cpp
void NotifyViewportChanged(const ibViewport& vp);
void ResetForFilterOrSort();
void OnRowAdded(TRow* newRow);
void OnRowRemoved(TRow* row);
void PinForSelection(const ibDataViewItem& item);
void UnpinForSelection(const ibDataViewItem& item);
```

The model exposes the same operations on its non-templated base via
plain virtuals so the frontend can call them without knowing `TKey`:
```cpp
class ibValueModel : public ibValue, public ibStandardCommandSource, ... {
    virtual bool IsPaged() const             { return false; }
    virtual void NotifyViewportChanged(const ibViewport&) {}
    virtual void ResetForFilterOrSort()      {}
    // ...
};
```
Concrete model overrides each virtual to forward into its typed
`m_buffer`.

`NotifyViewportChanged` decision tree:
* viewport fully inside buffer, away from edges → no-op.
* viewport approaching edge (within prefetch margin) → submit prefetch in
  that direction; do not block caller.
* viewport jumped outside buffer (THUMB drag, programmatic scroll) →
  Reset + initial fetch around new top.

Eviction:
* Run after each successful fetch.
* Keep `viewport ± bufferMargin` in `m_rows`.
* Skip eviction for rows where `wxRefCounter::GetRefCount() > 1` (held by
  selection / `ibValueModelReturnLine` / scripts) — they stay until released.

Throttling:
* Only one in-flight fetch per direction. New `NotifyViewportChanged` while
  a fetch is in flight updates "latest viewport" state; on fetch completion,
  buffer re-evaluates and may chain another fetch.

### 3.3. Lazy tree

Per-node state (extends `ibValueTreeListNode` or sibling field):
```cpp
enum class ibNodeLoadState : uint8_t {
    NotLoaded,   // mayHaveChildren but children not fetched
    Loading,     // fetch in flight
    Loaded,      // children populated (may be empty)
};
```

`IsContainer(item)` returns `node->m_container` (the hint flag) — unchanged.

`GetChildren(item, &out)`:
* If `state == Loaded` → fill `out` from `node->m_children`.
* If `state == NotLoaded` → submit `Fetch(req with parentItem=node)` in
  worker, set `state = Loading`, return empty (control shows expander but
  no children yet); on completion, populate + `ItemsAdded` notify.
* If `state == Loading` → return empty, control eventually receives
  `ItemsAdded` when the in-flight fetch returns.

Collapse → no eviction by default (children stay loaded for next expand).
Optional: under memory pressure, evict subtrees of long-collapsed branches.

### 3.4. listMode — model construction-time scope

`m_listMode` (`LIST_FOLDER / LIST_ITEM / LIST_ITEM_FOLDER`) is a
construction-time property of the concrete model. It shapes the SQL
`WHERE`:
* `LIST_FOLDER` — `is_folder = TRUE`.
* `LIST_ITEM` — `is_folder = FALSE`.
* `LIST_ITEM_FOLDER` — no row-type filter.

It is **not** a member of `ibFetchRequest` and does not change at
runtime. To present a different scope to the user, frontend constructs
a different model instance (e.g. when switching to hierarchical
navigation, swap to a model with `m_listMode = LIST_FOLDER`).

`parentItem` in the request is the only hierarchy-related field the
model interprets — empty means top-level rows; non-empty means children
of that node. The model has no opinion on whether the frontend will
render the result as a flat list or a tree.

### 3.5. Selection / current row

* Selection identity = `ibDataViewItem` (pointer to row). Stable across
  insert/delete in viewport (pointer doesn't move).
* On `Select(item)` → `PinForSelection(item)` — refcount++ so eviction
  skips the row.
* On filter / sort / view-mode change → snapshot the stable key of current
  row, `ResetForFilterOrSort`, and after initial fetch try `FindRowByKey`
  (DB-aware) to re-locate; on hit → re-select; on miss → clear selection.
* On scroll → selection is **not touched**. Existing
  `HandleOnScroll`'s late re-locate (`FindRowValue` after refresh) only
  fires if the row got evicted, which the pin above prevents — so this
  branch becomes dead code.

`FindRowByKey` is **new** and DB-aware:
```cpp
virtual ibDataViewItem FindRowByKey(const TKey& key) = 0;
```
Concrete impl runs `SELECT 1 ... WHERE pk = ?` and, if found, returns the
row pointer (loading it into the buffer if not already there). Existing
`FindRowValue(varValue, colName)` stays for free-form column-value search;
in paged mode it may need a similar DB-side rewrite.

## 4. Migration plan

Phased; no big-bang.

### Phase 1 — contract on paper (this document) ✅
* Land `paging-design.md`, agree on `Fetch`, `ibViewBuffer` shape.

### Phase 2 — add types + skeleton ✅ landed 2026-05-05
* `ibFetchAnchor<TKey>`, `ibFetchDirection`, `ibFetchRequest<TKey>`,
  `ibFetchResponse<TKey, TRow>`, `ibViewport` — `model.h`.
* `IsPaged() / NotifyViewportChanged / ResetForFilterOrSort` non-templated
  virtuals on `ibValueModel` (default no-op).
* `ibTableViewBuffer<TKey, TRow>` template — controller, holds no row
  storage, manages viewport state + dispatches Fetch via lambdas
  (Fetcher + AnchorOf), applies result by Append / Insert into
  `ibValueModelTableBase`'s public API. Forward + Backward fetch paths.
  Method bodies at file-end (after `ibValueModelTableBase` definition).
* `Fetch` is **not** a virtual on the base — types and virtuals don't
  combine. Concrete models expose a typed `Fetch(req)` member; buffer
  calls it through the Fetcher lambda.

### Phase 3 — backend dispatch (frontend untouched)
* `ibValueModel::CallRefreshModel` and `CallRefreshItemModel` now check
  `IsPaged()`:
  * paged → convert frontend's `(topItem, count, scroll)` into
    `ibViewport` and call `NotifyViewportChanged`.
  * non-paged → existing `RefreshModel` / `RefreshItemModel` path.
* Frontend (`tableBoxEvent.cpp`) keeps calling the same `CallRefresh*`
  methods. The switchover is invisible to it.
* Frontend rewiring (selection re-locate moved off scroll, etc.) is
  deferred to Phase 6 — only meaningful once paged models are in.

### Phase 4 — concrete `Fetch` implementations
One concrete class at a time, in order of pain:
1. `ibValueListDataObjectRef` — ✅ landed 2026-05-05. Full
   cursor-paginated: `Fetch(ibFetchRequest<ibGuid>)` via `ibListSqlBuilder`,
   `+1` probe row, forward + backward dispatch in buffer.
2. `ibValueListDataObjectEnumRef` — ✅ landed 2026-05-05. Single-batch
   only — CASE/WHEN parent-position order has no stable cursor.
   Custom ORDER BY inline; filter / bind via builder.
3. `ibValueListRegisterObject` — ✅ landed 2026-05-05. Single-batch.
   `TKey = ibUniqueKeyPair`; composite-key cursoring deferred to
   Phase 5+. Custom ORDER BY (LPAD for stable string sort) inline.
4. `ibValueModelTreeDataObjectFolderRef` — ⏸ deferred to Phase 5.
   Needs `ibTreeViewBuffer<TKey, TNode>` (different storage shape) +
   per-node `LoadState` for lazy expansion.  Routing through the
   table buffer is not meaningful (tree storage is per-parent,
   not linear).

Each landed concrete class:
* Implements `Fetch` (typed).
* Sets `IsPaged() = true`, overrides `NotifyViewportChanged` /
  `ResetForFilterOrSort` to forward into `m_buffer`.
* Old `RefreshModel` / `RefreshItemModel` left in place but
  unreachable when `IsPaged()` is on. Removal — Phase 7.
* `FindRowByKey` deferred (selection re-locate audit pending; see §5).

### Phase 4 diagnostics — `ibPagingLog` (retired)
The `ibPagingLog` sink was useful during the 2026-05-05..09 paging
arc and has since been stripped from the tree (no occurrences in
`src/engine/`). The lying-scrollbar / control-driven prefetch design
proved stable enough that the per-row diagnostic file (`paging.log`)
became more noise than signal. Prefer `wxLogDebug` at coarse entry
points (`PagedRefresh`, `PagedBootstrap`, `OnPagedFetch{Forward,Backward}Result`)
when revisiting paging behaviour.

### Phase 5 — eviction + refcount pinning
* `PinForSelection` / `UnpinForSelection` wired to control's `Select` /
  selection-clear.
* `ibValueModelReturnLine` already IncRef's the row — confirm path.
* Eviction enabled in `ibViewBuffer`.

### Phase 6 — frontend view-mode wiring
* Control toggle (Tree / Hierarchical / List) drives frontend's request
  shape: switch between `parentItem`-scoped fetches, top-level fetches,
  or model swap (when changing data scope, e.g. into folder-only mode).
* No request-level viewMode — model stays oblivious.
* View-mode toggle triggers `ResetForFilterOrSort` on the buffer (and a
  model swap when the new mode needs a different `m_listMode`).

### Phase 7 — cleanup
* Remove `RefreshModel` / `RefreshItemModel` from base if all concretes
  migrated.
* Remove `IsPaged()` if no holdouts (or keep as documentation hint).

## 5. Open questions / pre-work

* **`FindRowValue` audit.** Confirm whether current concrete impls do
  linear-scan over `m_nodeValues` or DB query. If linear, plan DB-aware
  variants for paged mode.
* **Composite-key cursor for registers.** `ibUniqueKeyPair` ordering must
  be total + match the `ORDER BY` in SQL; verify `< / > / ==` operators
  give the same order as DB sort.
* **wxDataViewCtrl fork capabilities.** What does the fork expect from
  the model on viewport change — pull (`GetValueByRow(row, col)`) or push
  (`ItemAdded`)? `NotifyViewportChanged` design above is push-style; if
  fork supports pull we may simplify by exposing total count and letting
  control request rows by index.
* **THUMB-drag UX.** Big jumps trigger Reset + initial fetch; meanwhile
  control shows nothing. Either accept a brief blank, show placeholder
  rows, or freeze old buffer until new arrives. Choose policy.
* **In-flight fetch cancellation.** If a Fetch is in flight and filter
  changes, we want to cancel the stale fetch (its result will be `stale`
  anyway, but it consumes a worker). Use `ibSession::RequestCancel`?

## 6. Out of scope

* Sub-millisecond first-paint — initial fetch will always be a DB
  round-trip. We optimise smoothness during scroll, not the very first
  display.
* Client-side filtering. All filtering goes into SQL WHERE; client-side
  filter on a paged buffer is incoherent (filter expects the whole set).
* Caching across model lifetimes. Buffer dies with the model; cross-model
  cache (e.g. session-level row cache) is a separate concern.
* Random-access by absolute row index. Cursor-based pagination doesn't
  give "scroll to row 5000 of 10000" cheaply. If a UI demand surfaces,
  reconsider with `OFFSET ?` as additional anchor variant.

## 7. Files touched (planned)

* `enterprise/src/engine/backend/model.h` / `.cpp` — `ibFetchRequest`,
  `ibFetchResponse`, `ibFetchAnchor`, `ibViewBuffer`, virtuals on
  `ibValueModel*Base`.
* `enterprise/src/engine/backend/metaCollection/partial/list/objectList.h`
  / `.cpp` — `Fetch`, `FindRowByKey`, removal of old Refresh* overrides
  (per concrete class, phased).
* `enterprise/src/engine/frontend/visualView/ctrl/tableBoxEvent.cpp` —
  `OnIdle` / `HandleOnScroll` rewired.
* `enterprise/src/engine/frontend/win/ctrls/tableView.{h,cpp}` and the
  forked `ibDataView*` — confirm contract for viewport reporting and
  pull-vs-push exchange.

No code changes in this draft. Phase 2 onwards begins implementation.

---

## 8. Evolved architecture — universal `Get*Fetch` (2026-05-05)

After Phases 2-4 landed and we shipped tape paging on Ref / Enum /
Register, the design converged on a stricter, broader contract that
covers DB-backed lists, RAM-backed tables, hierarchical trees, and the
forthcoming web frontend with the same primitives.

### 8.1 Principles

1. **Model is a stateless Fetch service.** Each `Get*Fetch` call is
   self-contained given its args. The model does not hold queue, scroll
   position, or per-consumer state. It does not even need to keep
   returned rows alive after the call — ownership transfers to caller.
2. **GUI manages the queue** (display window, prefetched rows, scroll
   position). Whenever GUI hits the edge of its queue it dispatches a
   new `Get*Fetch`. `m_hasMore == false` means "end of data in this
   direction"; GUI stops asking.
3. **Selection ≠ viewport.** User can scroll without changing
   selection; selected row may even be off-screen. Args carry both —
   `m_currentRow` (focus, preserved across fetches) and
   `m_viewportAnchor` (cursor for the SQL).
4. **`ibDataViewItem` is refcount-aware.** It holds `wxRefCounter*`
   internally; copy / dtor IncRef / DecRef. As long as any item holds
   a row, the row stays alive — solves selection-holds-evicted-row,
   frozen breadcrumb survival, and dangling pointers after batch
   replacement.
5. **Same contract for desktop, web, headless.** wfrontend.dll
   exposes `Get*Fetch` over HTTP (`/fetch?parent=…&anchor=…&count=…` →
   JSON); browser pages by batch the same way the wxDataView fork does.
   Without paging, the web client OOMs on large catalogs.
6. **Async via worker pool.** `ibSession::Submit([](){ return
   model->GetNextFetch(...); })` runs the Fetch on a worker; UI thread
   marshals back via `wxTheApp->CallAfter`. Per-scroll DB round-trip
   no longer freezes UI on remote DB.

### 8.2 API

```cpp
struct ibFetchArgs {
    ibDataViewItem m_parent;          // scope (invalid == root)
    ibDataViewItem m_currentRow;      // user selection (focus target)
    ibDataViewItem m_viewportAnchor;  // cursor for SQL
    int            m_count = 1;       // batch size (viewport on first)
};

struct ibFetchResponse {
    /* typed rows in concrete model;
       for the universal virtual it lands as ibDataViewItemArray */
    bool m_hasMore = false;
};

// On ibDataViewModel — universal entry, default fallback for legacy:
virtual unsigned int GetFirstFetch(parent, count, out) const;   // initial, no anchor
virtual unsigned int GetNextFetch(parent, anchor, count, out) const;
virtual unsigned int GetPrevFetch(parent, anchor, count, out) const;
```

Three methods (not one with a direction flag) — `wxTreeCtrl`
`GetFirstChild + cookie + GetNextChild` pattern is well known; clearer
semantics at call sites and easier to override piecemeal.

Default `GetFirstFetch` falls through to legacy `GetChildren`; default
`Get(Next|Prev)Fetch` returns 0. Models that don't support paging
silently keep their existing behaviour.

### 8.3 `ibDataViewItem` lifecycle

```cpp
class ibDataViewItem {
    wxRefCounter* m_id = nullptr;
public:
    ibDataViewItem(void* p);                // IncRef
    ibDataViewItem(const ibDataViewItem&);  // IncRef
    ibDataViewItem(ibDataViewItem&&);       // move
    ~ibDataViewItem();                      // DecRef
    /* assign / move-assign / IsOk / GetID / op==/!= */
};
```

Existing ctor signature `(void*)` preserved for ABI; cast to
`wxRefCounter*` internally — every OES row class (`ibValueTreeNode`,
`ibValueTableRow` and subclasses) inherits `wxRefCounter`.

**Resolved** (2026-05-05, verified 2026-06-06): ownership transport for
`Get*Fetch` results. Typed Fetch hands out `vector<TRow*>` with refcount=1;
`ibValueModel::AdoptRowsToItems` (model.h) adopts them — the typed
`ibDataViewItem(ibDataViewObject*)` ctor IncRefs to 2, then `r->DecRef()`
drops the initial allocation reference so `out` owns exactly one ref per
row. Every typed→universal bridge routes through it (objectListQuery, register
fetch). *(Superseded 2026-07 — §9.6: the typed bridge was retired; `RunComposerPage`
emits `ibComposerNode` directly and the template was removed.)* The `.get()` paths (auditLog `m_rowObjects`, predefinedEditor) borrow
model-owned rows — item IncRef/DecRef is balanced, model keeps the row alive.
No leak path remains. (Chose option 2's adopt-semantics, kept the existing
IncRef'ing copy ctor for the borrow paths.)

### 8.4 View modes (List / Tree / Hierarchy)

| mode | semantics | model fetch pattern |
|------|-----------|---------------------|
| **List** | flat list of all rows under invisible root | `GetFirstFetch(parent={}, count=N)` then incremental Next |
| **Tree** | nested expand/collapse, invisible root | `GetFirstFetch(parent={})` initial; `GetFirstFetch(parent=expandedNode)` on user expand |
| **Hierarchy** | breadcrumb of ancestors + children of current top | `GetAncestorChain(topItem)` for breadcrumb; `GetFirstFetch(parent=topItem)` for content |

The model exposes the same three methods; only the GUI-side
orchestration differs per mode. Sort by isFolder DESC at SQL level so
folders bubble to the top in any mode.

### 8.5 Sources beyond DB

`Get*Fetch` is not SQL-specific. Concrete implementations:

* **DB-backed** (Catalog / Document / Register / FolderRef): cursor
  query, `WHERE parent = ? AND uuid > ? LIMIT N`.
* **RAM-backed** (TabularSection / ibValueTable): slice the in-memory
  vector — `m_nodeValues[startIdx .. startIdx+count]`. Anchor → index
  via `find` or maintained map. Runtime mutations on the source
  reflect on the next fetch automatically (single source of truth).
* **HTTP-backed** (web client → wfrontend → backend): web client sends
  the same args as JSON, gets batch back. wfrontend just marshals.

### 8.6 GUI queue (frontend-only concept)

The model has no opinion on queueing. GUI keeps a deque per visible
scope: prefetched rows ahead of viewport. Scroll edge → pop one to
visible store, on low-water-mark dispatch `GetNextFetch` async to
refill. Scroll back → `GetPrevFetch` similarly. This eliminates
per-scroll-tick DB round-trips for the common case (user scrolling
within the prefetched window).

### 8.7 Tree / hierarchy navigation

`SetParentTopItem(item)` updates `m_topParentGuid` + triggers
`RefreshFromAction`. Drill-down (click folder) and drill-up (click
breadcrumb) flow through it. `GetAncestorChain(fromGuid)` returns
ancestor path for breadcrumb rendering — cached on `m_topParentGuid`
to avoid re-walking on every render tick. Header column click feeds
`OnSortColumnChanged` → updates `m_sortOrder` → reset.

### 8.8 Status by 2026-05-07

The 2026-05-05 handoff laid out the universal contract; the 2026-05-06
working tree extended it from "Get*Fetch on the model" into a
control-driven prefetch deque with a stateless fetch service on the
backend. Legacy `CallRefreshModel / RefreshModel / RefreshItemModel /
m_refreshModel / HandleOnScroll` are gone — refresh runs through a
single `RefreshFromAction()` entry that fires the
BeforeReset/AfterReset notifier; the control wipes its window and
re-fetches via `Get*Fetch`.

| feature | state |
|---|---|
| Tape paging on Ref via per-model `Fetch(req)` + dispatcher in `ibDataViewCtrl` | ✅ landed Phase 4 — original `ibTableViewBuffer` template did not survive the §8 convergence; functionality moved into the control |
| `ibValueListSqlBuilder` (filter/order/cursor) | ✅ landed |
| Single-batch paged Enum / Register | ✅ landed |
| `ibPagingLog` diagnostics → `paging.log` | ✅ landed |
| `OnSortColumnChanged` direct-integration in fork | ✅ landed |
| `RefreshFromAction` single refresh entry (BeforeReset/AfterReset notifier) | ✅ landed 2026-05-06 — replaces `CallRefreshModel / CallRefreshItemModel`; legacy `RefreshModel / RefreshItemModel` virtuals + `m_refreshModel` flag removed |
| FolderRef tree: load-all + folders-on-top sort + drill | ✅ landed |
| `GetAncestorChain` helper + cache | ✅ landed |
| Universal `Get*Fetch(parent, anchor, count, out)` virtuals on `ibDataViewModel` + `ibValueModel` | ✅ landed |
| Concrete `Get*Fetch` overrides on EnumRef / Ref / RegisterObject / FolderRef | ✅ landed 2026-05-06 — three overrides + private typed `Fetch(req)` per concrete class |
| `ibDataViewObject` base for rows/nodes | ✅ landed 2026-05-06 — virtual `IsContainer / GetParentItem / IsEqualTo` on the row, not on the model; `ibDataViewItem` delegates equality / parent / container queries directly to the held object |
| Refcount-aware `ibDataViewItem` (value-type, copy/move/op==/op!=) | ✅ landed |
| `BuildXxxHelper` rewired through `GetFirstFetch` | ✅ landed 2026-05-05 |
| FolderRef adapter `Get*Fetch(ibTreeFetchArgs)` → virtual `(ibDataViewItem)` | ✅ landed 2026-05-05 |
| Lazy tree expansion (`ibValueTreeListNode::LoadState{NotLoaded,Loading,Loaded}`) | ✅ landed 2026-05-06 — mutable LoadState on the node so const `GetChildren` can drive the first-expand fetch without const_cast'ing the node |
| Logical-equality selection survival across re-fetch | ✅ landed 2026-05-06 — `ibValueTableRow::IsEqualTo` compares `m_nodeValues`; `ibDataViewItem::operator==` short-circuits on pointer match then dispatches to row's virtual |
| Async fetch | ✅ landed 2026-05-06, **rebuilt 2026-07-31**. 2026-05-06: `ibDataViewModel::SubmitFetchAsync(work)` virtual forwarding to `ibSession::Submit` — which on the desktop resolves to the caller's own session, i.e. the wx main thread, so the read never actually left it. 2026-07-31: `DispatchPagedFetch` submits to `ibSession::Current()->Reader()` — a per-window sleeping session whose worker is the registry's headless pool — and the read genuinely runs off the UI thread. Result marshalled by `wxTheApp->CallAfter` into `OnPagedFetchForwardResult` / `OnPagedFetchBackwardResult`; a `shared_ptr<bool>` alive token replaced `wxWeakRef` (which un-links by writing into the tracked object, a race once a copy sits on a worker), and the refcounted anchor / parent items ride a shared payload so they are always released on the UI thread (`wxRefCounter` is not atomic). `SubmitFetchAsync` survives, now used only by the debounced `SchedulePagedRefresh`. Still synchronous: `PagedBootstrap`. See [table-model.md](table-model.md) § "Off the UI thread" |
| In-flight fetch cancellation (generation tokens) | ✅ landed 2026-05-06 — `m_pagedFetchGen` bumped on every reset (`PagedRefresh` / `Cleared` / `AssociateModel`); fetch lambda captures the token at submit time and discards the result if the live token has moved on |
| Per-direction in-flight fetch counters | ✅ landed 2026-05-06 — `m_pagedFetchingFwd / m_pagedFetchingBwd` independent so a scroll burst that crosses both edges can dispatch to each side without serialising (was a single counter pre-2026-05-06) |
| Soft-eviction via `wxDataViewTreeNode::SetHidden(bool)` | ✅ landed 2026-05-06 — hidden nodes stay in `m_children` but are skipped by walkers and excluded from `subTreeCount`; backward scroll re-shows fetched rows from the hidden head instead of going to DB. Replaces the un-hide-everything anti-pattern flagged in 2026-05-05's «Scroll polish» |
| 3-state lying scrollbar (THUMB drag fix) | ✅ landed 2026-05-06 — `IsPagedScrollbarMode / DerivePagedThumb / UpdatePagedScrollbar / AdjustScrollbars / SetScrollPos / SetScrollbar` overrides force the thumb to {Top, Middle, Bottom} from `m_pagedHasMoreFwd / m_pagedHasMoreBwd` regardless of `wxScrollHelper`'s real-virtual-size value. Thumb drag no longer chains many fetch ticks |
| Horizontal scroll regression from the 3-state scrollbar | ✅ fixed 2026-06-06 — `OnScrollEvent` had no orientation guard, so on a paged model it caught HORIZONTAL `EVT_SCROLLWIN` too: a horizontal thumb drag (`THUMBRELEASE`) hit the vertical-paging branch and returned without `event.Skip()` (swallowed → content never scrolled sideways), and horizontal line/page events mis-fired vertical `PagedFetch`. Symptom: "many columns → no usable bottom scrollbar". Fix: early `if (event.GetOrientation() != wxVERTICAL) { event.Skip(); return; }` at the top of `OnScrollEvent`. Plus `UpdateColumnSizes` now publishes the real `colswidth` virtual size in the can't-shrink-last-column branch instead of leaving a stale 0 |
| Non-paged `ibDataViewCtrl` never got a usable scrollbar (root of the above) | ✅ fixed 2026-06-06 — `wxScrollHelper::AdjustScrollbars` derives range as `virtualSize / pixelsPerLine`, and the **scroll rate was 0** for non-paged controls because `SetScrollRate` was only called from `RecalculateDisplay` (which early-returns when the model is null / seldom runs for a doc-form tablebox). With rate 0 the scrollbar never appears no matter the virtual size (diagnosed via `HasScrollbar`/`SetScrollbar` logging: `virtX=1647` vs `clientX=611` yet range=0). Fix: `CalcWindowSizes` now sets the x-rate (`SetScrollRate(FromDIP(10), curY)`, preserving the y-rate so the vertical scrollbar stays owned by RecalculateDisplay/paged path and doesn't pop up on empty tables) and defers `AdjustScrollbars` to `OnInternalIdle` via `m_calcScrollPending` (anti-flicker). Paged controls always worked because the paging machine keeps RecalculateDisplay running |
| ⚠ OPEN: vertical scrollbar flashes on the right while a list/document form opens | NOT FIXED 2026-06-06 — **not the dataview** (proven: `ibDataViewCtrl::HasScrollbar(wxVERTICAL)` is never true; its `SetScrollbar(wxVERTICAL,…)` range is always 0). The flash is the **form host `ibVisualHost` (`wxScrolledWindow`)**: `UpdateVirtualSize()` runs during the form build while the host client is a tiny placeholder (~16px) and sets virtual = content panel **+50px** (~70px) → virtual ≫ client → native scrollbars appear, then hide once the host grows to its real size. `Freeze()` does NOT suppress them (Win32 native scrollbars are non-client). Tried & reverted (didn't help): freezing the host itself during build; skipping `UpdateVirtualSize` while frozen + recomputing on a host `wxEVT_SIZE`. Real fix needs a form-host lifecycle change (defer scroll setup until the host has its final size / rework the `+50` margin / build hidden then show) — touches every form's scrolling, needs its own focused PR + testing. Constructor sets `SetScrollRate(5,5)` (`visualHost.h`); virtual size set in `ibVisualHost::UpdateVirtualSize` (`visualHost.cpp`) |
| Hierarchical drill chain pin | ✅ landed 2026-05-06 — `m_topParentChain` (refcount-pinned `ibDataViewItem` array) survives `Cleared() / DestroyTree`; replaces a single anchor that was wiped on re-fetch |
| wxDVC sort suppression for paged | ✅ landed 2026-05-06 — `ibDataViewMainWindow::GetSortOrder()` returns empty `SortOrder()` when the model `IsPagedModel()` so the fork's `InsertChild` appends in fetch order rather than scattering rows via binary-search insertion |
| Ownership-transport convention (`AdoptRowsToItems` / Append / Insert) | ✅ landed 2026-05-05 — `ibValueModel::AdoptRowsToItems` template; FolderRef bridges use it; tape-buffer Apply path already adopts via `Append`/`Insert`. **Retired 2026-07 (§9.6):** the typed-Fetch→universal bridge is gone (all families emit `ibComposerNode` from `RunComposerPage` directly), so the template had no callers and was removed |
| Selection re-locate by stable key | covered by existing `FindRowValue` + new `IsEqualTo` virtual on row — current-row check inside loaded buffer; no separate `FindRowByKey` API (request from 2026-05-05 plan superseded by the `IsEqualTo` path) |
| `ibPredefinedValueObject` shared_ptr/refcount mixing | ✅ closed 2026-05-06 — audit confirmed no callsite passes the object through `ibDataViewItem` (ctor takes only model/view rows), so the original mixing concern doesn't apply. Base stays `wxRefCounter` — `ibPredefinedValueObject` is not part of the data-view hierarchy |
| RAM-backed `Get*Fetch` for TabularSection / ibValueTable | ✅ landed — new intermediate base `ibValueModelRamTableBase` (`model.h:1100`) hosts a `GetFirstFetch` override (`model.h:1324`) slicing `m_nodeValues` by anchor+count. Inherited by `ibValueTabularSectionDataObjectBase`, `ibValueModelTable` (`system/value/valueTable.h`), and `ibValueRecordSetObject` for registers. `Features::RamFetch` flag advertised via `GetFeatures()` override |
| Composite-key cursor for registers | ✅ landed — `registerSqlBuilder.{h,cpp}` mirrors `ibListSqlBuilder` for registers: `EffectiveOrder = [user_sorts] ++ [identity_tail]` (recorder+line for HasRecorder, period?+dimensions otherwise), `BuildAnchorPredicate` emits the same composite cmp_op + case-when tiebreak as Ref. `ibValueListRegisterObject::Fetch(ibFetchRequest<ibUniqueKeyPair>)` (`objectListQuery.cpp:389`) is the cursor-paginated single SQL point; `GetFirst/Next/PrevFetch` (`objectListQuery.cpp:478/502/…`) build the anchor via `BuildRegisterAnchor` (`objectListQuery.cpp:454`) which captures `m_sortValues` directly from `row->GetTableValue(attr->GetMetaID())`. `ibUniqueKeyPair::operator<>` aren't used here — cursor binds `m_sortValues`, not the key itself; equality goes through `m_keyValues == other.m_keyValues` for selection survival and dedup |
| FB binary BINARY(20) bind for parent ref | ✅ landed 2026-05-07 — column is `BINARY(sizeof(ibReference)) = 16` bytes — a pure `ibGuidImpl`, the target type living in the sibling `_RTRef` clsid column (was 20 with an inline metaID until `f4d92cc7`, 2026-07-29) — not CHAR(16). FolderRef `GetNextFetch / GetPrevFetch` build a `ibReference` matching how save-path stores it (m_id=catalog metaID always, m_guid=zero for top-level / real for drill) and bind via `SetParamBlob`; SQL uses `WHERE refDataField = ?`. C++ byte-matcher gone. Bonus: `firebirdParameter.cpp::SetParamBlob` ctor got an `SQL_VARYING` branch (was a silent no-op for varying-typed params — write u16 length prefix + data into sqldata) |
| Iherarchical scrollbar after drill stuck at top | ✅ landed 2026-05-07 — `ibDataViewCtrl::SetTopParent` (drill UP / drill INTO) now calls `RecalculateDisplay()` after `Cleared()` so `m_tableAreaWin->SetVirtualSize` reflects the post-drill row count. Without this the virtual size kept the pre-drill value, wxScrollHelper computed a one-viewport range, and the thumb stayed pinned at top |
| Hierarchical drill prefetch tree level | ✅ landed 2026-05-07 — `OnPagedFetchForwardResult / OnPagedFetchBackwardResult` were inserting fetched rows under `m_root` while `PagedBootstrap` had built the breadcrumb chain `m_root → crumb_0 → ... → [data]` and put the bootstrap rows under the deepest crumb.  New rows therefore rendered as siblings of `crumb_0` instead of continuing the data window under the deepest crumb.  The trim trigger `over = m_root->GetChildNodes().size() - target` was always negative in drill mode (m_root has only the topmost crumb as a child), so trim never fired and `m_pagedHasMoreBwd` (flipped inside the trim block) stayed `false` forever, leaving `DerivePagedThumb()` locked to Top.  Fix introduces `GetPagedInsertParent()` (walks `m_root` → `children[0]` × chain depth) and routes both result handlers through it, correcting selection / currentRow / row-height-cache offsets via `crumbCount = m_topParentChain.GetCount()` so flat indices translate cleanly between data window and full tree.  Flat mode is the `crumbCount=0` degenerate, identical behaviour |
| Drill-mode thumb position cascade | ✅ landed 2026-05-07 — five secondary issues uncovered after the prefetch-tree-level fix while still seeing `[thumb] → Top` forever in drill mode.  (a) `GetFirstVisibleRow` was returning a data-area row index because `m_tableAreaWin->SetVirtualSize` subtracts the frozen offset; shifted by `GetDataViewWindowOffset(m_tableAreaWin).y` to land in full-tree coords (mirrors `DrawTableContent`'s `gridOffset` adjustment).  (b) `GetRowByItem(GetTopItem())` returned -1 in drill mode because fetched data rows have no model-side parent — `ItemToRowJob`'s parent-chain walker fails.  Replaced both `OnScrollEvent`'s `topAdj` and `DerivePagedThumb`'s `topRow` with `GetFirstVisibleRow()` directly.  (c) `topRow > 0` for dataBehind was true even at scroll-top in drill (topRow = crumbCount); now `topRow > crumbCount`.  (d) Lying scrollbar still appeared in tiny folders where data fit viewport because `renderedRows > viewport` (15 > 14 with one crumb); subtract crumbCount before comparing.  (e) Thumb stayed Bottom for 2-3 scroll-up notches: row-index math `topRow + viewport <= bufferSize` flickered around the overshoot region of m_tableAreaWin's virtual layout.  Replaced with direct scroll-position comparison: `sy >= maxScrollY` → Bottom, `sy <= 0` → Top, in between Middle, where `sy = CalcUnscrolledPosition(0,0,…).y` and `maxScrollY = m_tableAreaWin->GetVirtualSize().y - GetClientSize().y`.  Single scroll-up from Bottom now lands on Middle |
| Tree+selection cursor (path A) | ✅ landed 2026-05-07 — `PagedBootstrap` precomputes `treeCrumbs` via `BuildAncestorBreadcrumb(savedFocus)` once; in tree mode + restoreFromSelection cursor = `treeCrumbs[last]` (topmost ancestor folder).  Composite-cursor predicate becomes `<= (isFolder=1, …)` — includes ALL folders and non-folders ordered before chain head, so chain[last] is guaranteed in the first batch even when top-level folder count > viewport.  Walker reuses the same `treeCrumbs` array (no second BuildAncestorBreadcrumb).  Closes the residual edge case from the early Tree+selection fix |
| TabularSection RAM-backed paging end-to-end | ✅ landed 2026-05-07 (late) — eight cascaded fixes: (1) `ItemToRowJob` walker switched to pointer-identity `GetID() == GetID()` so StartEditing computes labelRect correctly for default-valued rows; (2) `PagedBootstrap` SetHasChildren unconditional (folder marker survives refresh in List mode); (3) `DoItemChanged` / `ItemAdded` / `ItemDeleted` gated on `Features::RamFetch` — RAM falls through to non-paged narrow path, no flicker; (4) `BuildVisibleView()` filter+sort helper + Get*Fetch slices view, `RefreshTabularSection` collapsed to `NotifyReset()`; (5) `AddValue` / `CopyValue` insert at `row+1` (after active); (6) `Select(item)` from public surface restores focus on add (new row) and on delete (row above); (7) number-line column resolved via inline `BuildVisibleView` lookup in `GetValueByRow`; (8) `m_pagedRestoreFocusRow` captured at refresh, used as primary focus-restore for RAM (replaces value-eq scan that false-positived on default rows).  Pointer-identity audit also fixed `FindChildByItem`, `FindNode` parentChain walker, narrow `ItemAdded.Index`, `ItemDeleted` itemNode loop |
| Web HTTP fetch endpoint | ⏸ planned (separate phase, deferred to web table renderer phase) — `wfrontend.cpp` not yet touched.  Building `/fetch` ahead of a web consumer would freeze the JSON schema blind |
| Cold-open empty-table — dangling-if reset stuck | ✅ landed 2026-05-09 — strip pass earlier removed the diagnostic-log bodies of two nested `if`s in `PagedBootstrap` end, leaving them brace-less.  C parsing rule made the `m_pagedNeedsBootstrap = false;` reset the body of the inner `if` → fired only when both `prevHas*` differed.  On cold-open both stayed equal → flag stuck, every `OnInternalIdle` re-armed bootstrap → infinite wipe-fetch-wipe loop.  Reset now unconditional, both `if`s carry braces.  See `session-2026-05-09.md` §1 |
| Sort loses focus when row resorts beyond first batch | ✅ landed 2026-05-09 — under `skipCapture` (sort path), `PagedRefresh` now captures `m_pagedRestoreFocus` from `m_currentRow` (anchor + selection still wiped — cursor invalidated by new ordering).  In `PagedBootstrap` the non-selection cursor falls back from anchor to focus when anchor is empty, so the post-sort fetch positions the batch around the previously-selected row.  `currentFocus` capture moved out of the `if (!skipCapture)` gate (was a no-op for sort otherwise).  See `session-2026-05-09.md` §2 |
| Add to empty folder didn't activate new row | ✅ landed 2026-05-09 — `SetPagedRestoreSelection(item)` now also stamps `m_pagedRestoreFocus` and forces `m_pagedRestoreFocusWasSelected = true` (programmatic Select implies "really select this row" — wasSelected was previously captured before Select stamped, landing false on a list with no prior selection).  Defence-in-depth: `savedFocus` in PagedBootstrap falls back to `m_pagedRestoreSelection` when `m_pagedRestoreFocus` is empty.  See `session-2026-05-09.md` §3 |
| `tableBox` AssociateModel reset wiped just-applied current line | ✅ landed 2026-05-09 — fresh control attaching to existing model (form rebuild after `createdValue` notification) used to fall through `m_tableCurrentLine.Reset()` and lose the line `ApplyCurrentLine` had just set on the previous control.  Now distinguishes "fresh attach to existing model" (carry `m_tableCurrentLine` forward + re-route through `Select(item)`) from "real model swap" (reset).  See `session-2026-05-09.md` §4 |
| Hierarchical drill flicker | ✅ landed 2026-05-09 — `SetTopParent` (drill UP / INTO) used to wrap `Cleared() + PagedBootstrap` in `ScopedPagedFreeze` expecting the sync sequence inside the freeze, but `Cleared()` for paged routes through async `SchedulePagedRefresh` → wipe + bootstrap landed AFTER `~Thaw`, user saw the old folder briefly.  Direct `PagedRefresh()` call instead of `Cleared()` keeps the sequence sync inside the freeze.  Bonus: `m_pagedFrozenForBootstrap = true` set before `PagedBootstrap` so its inner `m_table.Freeze()` is suppressed (would otherwise outlive the outer scope and block the `~Thaw` from painting new content until the next idle).  See `session-2026-05-09.md` §5 |
| Hierarchical breadcrumb — clicking ancestor crumb duplicated it | ✅ landed 2026-05-09 — `SetTopParent` only handled `item == GetTopParentItem()` (chain[0]) as drill-UP.  Clicking deeper crumbs fell through to drill-INTO and `Insert(item, 0)` duplicated them.  Linear scan of `m_topParentChain` now finds the clicked item's index `i` and pops `chain[0..i]` so the parent of the clicked crumb becomes the new top.  Drill-INTO branch handles only items not in the chain.  See `session-2026-05-09.md` §6 |
| Filter + Add row that misses the filter → empty table | ✅ landed 2026-05-09 — cursor-mode bootstrap with `cursor = newRow` and a SQL filter excluding the new row used to return 0 rows (cursor predicate excluded everything in the filtered scope), leaving the table fully blank.  Empty-fetch fallback in `PagedBootstrap`: when the cursor-mode fetch returns `n == 0` and the cursor was non-empty, retry with empty cursor → user sees the filtered top batch instead of nothing.  See `session-2026-05-09.md` §7 |
| `m_eventOnAddRow` fired twice on Add | ✅ landed 2026-05-09 — single user GUI Add fired the script `OnAddRow` event from both `tableBoxEvent.cpp::OnItemStartAdding` (notifier→`ItemAppended` path for RAM-paged TabularSection) and `tableBox.cpp::OnUpdated` `fromCreated` branch (DB-paged catalog post-save).  Removed the OnUpdated firing — `OnItemStartAdding` is now the single source of truth.  `fromCreated` flag dropped.  See `session-2026-05-09.md` §8 |
| Cell-mode subtle row hint | ✅ landed 2026-05-09 — in `ibDataViewSelectCell` mode the focused cell got a strong native selection rect, but the row had no cue.  Added a 15% blend of system-highlight over the row background as a faint tint behind the strong cell rect.  See `session-2026-05-09.md` §9 |
| Sort + filter types in modelView.h | ✅ landed 2026-05-09 — `ibSortOrder` / `ibSortData` / `ibSortModel` / `ibFilterRow` / `ibComparisonType` migrated from `model.h` to `modelView.h` so `datavgen.cpp`'s header-arrow path (`SyncColumnArrowsFromModel`) doesn't need to pull the value-subsystem header.  `modelView.h` now includes `<algorithm>` + `<vector>` + `backend/system/value/valueType.h`; no cycle (value subsystem doesn't include `modelView.h`).  See `session-2026-05-09.md` §10 |

### 8.9 Open audits

* **Web HTTP `/fetch` endpoint.** `wfrontend.cpp` not yet wired to
  the universal `Get*Fetch` path. Plan: `/fetch?parent=…&anchor=…&
  count=…` → JSON, mirroring desktop's `DispatchPagedFetch`. See
  §8.8 "Web HTTP fetch endpoint" row.

### 8.10 Closed audits

* ~~**`ibDataViewObject` migration coverage.**~~ Closed 2026-05-07
  — every `ibDataViewItem(void*)` callsite confirmed safe. The
  Mode::RawId ctor (`modelView.h:112`) is used only by virtual-list
  encodings (`wxUIntToPtr(row+1)` in `datavcmn.cpp:97 / 113 / 206 /
  301` and `datavgen.cpp:2777 / 6373 / 7440`) and by the
  `ibDataViewTreeStoreNode m_root` sentinel (`datavcmn.cpp:2519`).
  All real model rows (`ibValueTableRow* / ibValueTreeNode* /
  ibPredefinedValueObject*`) flow through the typed
  `ibDataViewObject*` ctor via overload resolution — no untyped raw
  pointer leaks across the boundary.
* ~~**RAM-backed `Get*Fetch`.**~~ Closed — see §8.8 row "RAM-backed
  `Get*Fetch` for TabularSection / ibValueTable".
* ~~**Async fetch lifetime under control teardown.**~~ Closed — every
  `SubmitFetchAsync` site in `datavgen.cpp` (`PagedRefreshScheduled`,
  `PagedFetchForward`, `PagedFetchBackward`) captures
  `wxWeakRef<ibDataViewCtrl> weakSelf(this)`. The worker-side lambda
  re-checks `weakSelf.get()` before dispatching `CallAfter`; the
  UI-side lambda inside `CallAfter` re-checks again and verifies
  `m_pagedFetchGen` matches before applying the result. Either side
  cleanly no-ops if the control was destroyed between submit and
  dispatch.
* ~~**Composite-key cursor for registers.**~~ Closed — see §8.8 row
  "Composite-key cursor for registers". `ibUniqueKeyPair` keeps
  `m_objGuid = wxNewUniqueGuid` per ctor for unification with the
  base `ibUniqueKey` shape; equality in `enUniqueKey` mode short-
  circuits to `m_metaObject == other.m_metaObject &&
  m_keyValues == other.m_keyValues` (see `uniqueKey.cpp:44`) so
  composite-key compare lands by dimension values, not by random
  guid.  The cursor SQL uses `m_sortValues` directly, so
  `operator< / >` matching SQL ORDER BY is unnecessary.
* ~~**`m_topParentChain` invalidation.**~~ Closed 2026-05-07 —
  carried over from `session-2026-05-06.md` "Open audits". Recovery
  path (drill-up → empty fetch → another drill-up → normal fetch)
  works without an explicit hook. `PagedBootstrap` n==0 branch
  cleans `hasFwd / hasBwd` so no phantom scrollbar appears on the
  empty step. Per-entry validation would cost a round-trip per
  chain entry per refresh — not worth it for the rare case where
  another user deletes a folder mid-drill.
* ~~**Generation token vs reentrant `Submit`.**~~ Closed 2026-05-07 —
  carried over from `session-2026-05-06.md` "Open audits". All
  `m_pagedFetchGen` reads/writes happen on the UI thread (capture
  in `DispatchPagedFetch`, bump in `PagedRefresh / Cleared /
  AssociateModel`, check inside the `CallAfter` lambda). `CallAfter`
  posts to the next UI idle event so the check is sequential w.r.t.
  any intervening bump. Worker thread never touches gen. The race
  «submit lands before bump completes» is impossible on a single
  thread; multi-session load test from `next-session-plan.md`
  «verify under load» covers it observationally.

## 9. Grouping, selection restore, and the L5-2 identity/navigation layer (2026-07)

This section records the arc that made **grouping** and **cross-view-mode
selection restore** work on DB (cursor) *and* RAM (storage) lists, and named the
layer it exercised.

### 9.1 DB list grouping composition

DB grouping had **never** produced output. Three defects, all in
`ibValueModelCursor::RunComposerPage` (`modelDb.cpp`) / its display path, now
fixed:

* **Group headers were dropped by the level filter.** A `TOTALS BY` result is a
  tree; `BuildDimensionTree` (`queryProvider.cpp` `FoldDimLevel`) folds from the
  Root (`m_level == 0`) so the first grouping level's headers sit at
  **`m_level == 1`** (`AddChild(node->m_level + 1)`), and `FlattenPreOrder` never
  visits the root. The fetch kept `if (r.m_level != 0) continue`, dropping every
  header → 0 rows → the list "disappeared". A flat / DETAIL fetch (no `TotalBy`
  → `hasTotals == false`) emits every row at level 0. Fix:
  `if (r.m_level != (groupLevel ? 1 : 0)) continue`.

* **Grouping-replaces-tree.** For a hierarchical source (catalog with a parent
  column) a user grouping now **replaces** the inherent folder tree rather than
  nesting inside it: `hierarchy = GetHierarchyColumn() != nullptr && !flatView &&
  !grouping`. With a grouping configured, hierarchy is OFF and `groupLevel` drives
  the same flat-scan + `TotalBy` a non-hierarchical document takes. The folder
  tree shows *only* when no grouping is configured. (This superseded an earlier
  "hierarchy outer, groups inner" plan.)

* **Two display traps.** (a) `ibValueModelTreeDataObjectFolderRef::GetAttrByRow`
  read `node->GetTableValue(GetDataIsFolder())` — `m_nodeValues.at()`, which
  **throws** `std::out_of_range`; a GROUP node carries only its dimension cell, no
  IsFolder → the throw aborted every group row's paint (blank + glitchy headers).
  Fixed with the safe `node->GetValue(id, out)` (catches the miss). (b) A grouped
  DETAIL row that happened to be a folder kept `isContainer =
  folderCol.GetBoolean()` → drilling it re-entered `RunComposerPage` with an empty
  group path (depth 0) → `groupLevel` fired again → the whole grouping tree nested
  under the folder ("infinite" re-grouping). Fix: under grouping a DETAIL row is
  always a LEAF (`isContainer = grouping ? false : …`).

`flatView` gates `dims` exactly as RAM does (`modelRam.cpp`): a flat List view
passes the ignore-parent sentinel → grouping OFF; Tree/Hierarchical → ON. Note the
view-mode enum order (`dataview.h`): `ibDataViewTree = 0`,
`ibDataViewHierarchical = 1`, `ibDataViewList = 2` — only List(2) yields the
flat-scan sentinel.

### 9.1.1 Group-level paging — client window (2026-07-16)

A `TOTALS BY` read folds the **whole** level eagerly (`ExecuteTotals` ignores the
page envelope — the fold in `queryProvider.cpp` builds the full tree), so a grouped
level cannot keyset-page in SQL the way a detail level does. It was left as a
follow-up: an anchored group fetch returned 0 (`if (groupLevel && anchor.IsOk())
return 0`) rather than re-emitting the level. That hid a **data-loss** bug — the
detail-level `+1` probe-trim (`rows.pop_back()`) still ran on the grouped rows and
dropped the **last group** of any level with more groups than a page, and with the
anchored fetch returning 0 nothing ever re-fetched it. On any grouped list/report
with more than `defaultCountPerPage` groups the last group silently vanished.

Fixed by windowing the grouped level on the **client**, the SAME rule the RAM half
pages by (`ibComputePageWindow`, factored out of the former `RamWindowPositions` in
`modelRam.cpp` into `model.h` so both halves share it — no duplication).
`RunComposerPage` (`modelDb.cpp`) now collects every group node of the level,
locates the browsed anchor group by its own last group-path value, and takes the
`count` groups after/before it per direction (the anchor stays in a Reset page so the
viewport does not drift). The probe-trim/reverse is gated to the detail path only
(`if (!groupLevel)`). Reference-counting: each group node is built with the ctor ref,
the window `out.Add`s the on-page ones (IncRef), then one pass DecRefs every node —
on-page → 1 (owned by `out`), off-page → 0 (freed).

Boundary (honest): the fold is still eager — the whole level lands in RAM every
fetch, the window only bounds what reaches the control. A true keyset group push-down
in SQL (`LIMIT` on `GROUP BY` cursored by the dimension) is a larger, separate arc,
not this. What is closed here is correctness (no lost group) and working group
navigation, not server-side group streaming.

### 9.2 Selection restore across a view-mode switch — `BuildAncestorBreadcrumb`

Switching view mode with a selection **inside a sub-level** (a sub-folder, or a
row inside a group) lost the selection: the top-level fetch returns folder/group
*headers*, not the row, so `PagedBootstrap`'s restore-scan found no match.
`ibValueModel::BuildAncestorBreadcrumb` was a **stub** — the model built no
ancestor chain, so the Hierarchical/Tree restore drilled to the roots, not the
level the row lived in.

Implemented per source, two shapes:

* **DB cursor** (`ibValueModelCursor::BuildAncestorBreadcrumb`, `modelDb.cpp`):
  * *Folder* (no grouping): read the row's parent reference (the queryable's
    hierarchy column) and walk UP — one point lookup (`ResolveAnchorByKey` by the
    parent ref = the folder's own PK) per level, each yielding the folder's values
    and its own parent ref.
  * *Grouped*: the ancestors are GROUP levels, not folders — one crumb per grouping
    dimension, keyed by the row's own value for that dim, carrying the group PATH
    `root→this`.
* **RAM storage** (`ibValueModelStorage::BuildAncestorBreadcrumb`,
  `modelRam.cpp`): group branch only (a plain RAM list is flat), dim values via
  `m_storage.SplitField` / `ResolveField(StorageIndexOf(row), head, tail)`.

Contract: `out[0]` = the immediate (innermost) parent — the scope
`GetEffectiveFetchParent` returns; `out.back()` = the root. Each crumb is a
CONTAINER node whose key / `m_groupPath` `IsEqualTo`-matches the fetched
folder/group node, so the drill scopes into it and the restore-scan lands.

### 9.3 The L5-2 identity/navigation layer (named)

The above crystallised a layer that was implicit: **identity + navigation over a
source**, distinct from data production.

* **L3 / queryable** — data production (columns → keyset predicate → SQL/RAM scan).
* **L5-1** — the fetch (`RunComposerPage` + composer + driver): settings → a page of
  rows.
* **L5-2** — identity + navigation: `GetItemKey` (row identity), `ResolveAnchorByKey`
  (point lookup by key), `BuildAncestorBreadcrumb` (ancestor chain), `SetItemParent`
  (display parent). All source-agnostic virtuals on `ibValueModel`, dispatched to
  cursor/storage. This is what lets the frontend page, drill, restore and identify
  rows through **one** model interface without learning whether a cursor or storage
  backs it — the same unification L5-1 gave to the fetch, now for navigation.

**Not yet a designed contract — a scatter of virtuals.** Consolidation debt:
`ibValueModelCursor::GetItemKey` still returns empty (stub); the frontend restore
still has **two** paths (RAM pointer-identity vs DB value-compare — the pointer
shortcut breaks if a RAM node is re-created on reload). Direction: define the L5-2
contract, make every source implement it fully (key everything, retire the RAM
pointer-identity shortcut), shrink the frontend restore to
`key → ResolveAnchorByKey → BuildAncestorBreadcrumb → drill → match by key`, so the
restore SSOT becomes the KEY rather than the branchy `PagedBootstrap`.

### 9.4 Minor UI fixes landed alongside

* **Default view mode** `ibDataViewHierarchical` (was `ibDataViewTree`) —
  `tableBox.h` `m_propertyViewMode` default.
* **Tabular-section line number right-aligns again.** The value is a `"number"`
  variant and the renderer right-aligns numbers in `SetValue`, but the paint loop
  forced the EXPANDER column (column 0 in a non-List view) to
  `SetAlignment(wxALIGN_CENTER_VERTICAL)`, dropping the horizontal flag. Fixed to
  preserve the horizontal component: `SetAlignment((align & (wxALIGN_RIGHT |
  wxALIGN_CENTER_HORIZONTAL)) | wxALIGN_CENTER_VERTICAL)` (`datavgen.cpp` cell
  paint).

### 9.5 Dot-path column sort / grouping through the composer (2026-07)

A form-table column whose Source is a **dot path** (`Table.RefAttr.Field` — a
value reached by walking a reference) is a frontend-only projection: the model's
column collection holds only real attributes, and the cell is resolved per row by
`ibValueModelTableBox::ResolveCellValue` (`tableBox.cpp`), which walks the row's
reference. Sorting and grouping by such a column had never reached the composer;
this arc wired it end to end. All landed BUILD GREEN Debug|x86 2026-07-02.

**Header-click sort commits straight to the composer.** The click formerly sent
`model->OnSortColumnChanged(leaf-metaID)`, which `GetColumnNameByID` could not find
in a tabular section's column collection, so nothing sorted and the arrow never
round-tripped. Now that a composer backs every list, the frontend writes it
directly: `ibValueModelTableBox::OnColumnClick` (`tableBoxEvent.cpp`,
`wxEVT_DATAVIEW_COLUMN_HEADER_CLICK`) does `composer.ClearSorts();
composer.Sort(field, asc); m_tableModel->RefetchAll()`, sets the arrow itself
(`ResetAllSortColumns()` + `dataViewColumn->SetSortOrder(asc)`) and takes
`SetPagedSkipRestoreCapture()`. The backend model is blind to it — the same shape
the filter dialog already used. `field = col->GetSourceFieldName()` (`tableBox.h`)
= the column's `m_propertySource` string (`ibVariantDataSource::Write` → the public
`GetValueAsString()`, **not** a metaID round-trip) with the tablebox prefix stripped
(a string strip of `GetOwner()->GetSourcePath().size()` segments, because `Write`
yields the full form-rooted path while the composer's queryable is table-rooted and
wants a row-relative name). The same name is universal across sort / filter / group.

**Deleted machinery** (the old front↔back sort relay, obsolete now the composer is
the single store): `ibValueModel::SortBy` (both overloads) + `ibDataViewModel::SortBy`
+ the provider forwarders, `GetSortArrows` + `ibSortArrow`, `SyncColumnArrowsFromModel`,
`GetColumnSortField` (tablebox), `GetSortField` / `m_sortField` (dataview column).
Added: `ibPropertySource::GetValueAsString()` (thin, over `ibVariantDataSource::Write`).

**Dot-walk sort duplicated rows — keyset is incompatible with a JOIN sort.**
Sorting a DB list by a dot-walk field repeated the window. `BuildAnchorPredicate`
(`dbTableProvider.cpp`) deliberately excludes dot-walk sorts from the keyset
predicate (`if (!s.m_path.empty()) continue` — the joined column is not on the main
scan), but the rendered ORDER BY *includes* it, so the keyset advances by PK while
the order is by the joined value → the next page overlaps rows already shown. The
dot-walk value is not even marshalled into the anchor row (it is resolved per row on
the frontend, absent from the SELECT), so a full keyset fix is large (marshal the
join value + predicate over the alias). Low-risk fix instead: `RunComposerPage`
(`modelDb.cpp`) detects a dot-walk sort (`GetColumnIDByName(field) == NOT_FOUND`)
and fetches the whole ordered result in ONE unbounded snapshot (`page.m_count = 0`,
`dir = Reset`), returning 0 on any anchored continuation. Non-dot-walk paging is
untouched. Trade: a dot-walk sort loads all rows at once (user-chosen, acceptable).

**Dot-walk grouping showed empty headers.** Deeper, two layers. (1) Totals lowering
projects a dot-walk dimension under a **synthetic** model id `kSyntheticColumnBase =
0x50000000` (`queryLowering.cpp`) — outside real metaIDs so a self-referencing walk
(a `DataVersion` leaf sharing a metaID with the row's own attribute) cannot clash
with the main table — and `FoldDimLevel` stamps the group value under that synthetic
id. So `GetColumnIDByName(dim)` (and the leaf id) never find it. (2) The header's
display path is the dot-path column, which resolves `Ref.Field` through the *row's*
reference — a group header has no row reference → blank. Fix (Max's framing: "the
group value comes FROM the composer — just render it, don't re-resolve the dot per
row"): in `RunComposerPage`, when `GetColumnIDByName` misses, walk the path through
the queryable (`ResolveColumnByName` + `ResolveReferenceTarget`) to the leaf id (=
the dot-path column's model id) and flag `dotWalkDim`; when building the group node,
scan `r.m_values` for the synthetic keys `>= 0x50000000` and pick the `dwOrdinal`-th
(the synthetic ids ascend in lowering order — dim0, dim1, … then measures — so the Nth
dot-walk dimension takes the Nth synthetic; `dwOrdinal` = the count of dot-walk dimensions
before this level), re-key it under the leaf id + push it onto `groupPath`. The header
then renders the composer's value directly, and drilling filters `Ref.Field = value`.
Regular grouping is untouched (`dotWalkDim == false`). Multi-level dot-walk grouping works
(each subgroup reads its own synthetic key by ordinal); the one residual edge is MIXING
scalar-synthetic and expanded-LEFT-join dot-walk dimensions in one grouping — the expanded
ones take no synthetic id, skewing the ordinal — but pure single-source scalar dot-walk
dimensions, the common case, map 1:1. Note: `ibListFetchDriver::OnRow`
(`listFetchDriver.h`) keys a driver row by SCHEMA columns only, so a value stamped
under a non-schema column is discarded — the synthetic id survives because it *is* in
the schema (an earlier attempt to stamp under a display column via `FoldDimLevel` was
dropped here and reverted).

**Subgroups inherit ancestor dimensions.** A subgroup should show the fields grouped
above it (group 2 shows group 1; group 3 shows 1 and 2). DB: `FoldDimLevel`
(`queryProvider.cpp`) seeds the child node with `child->m_values = node->m_values`
(the parent's cells) before stamping its own dimension — both branches; the cascade
carries every ancestor down. RAM parity (`modelRam.cpp` `RunComposerPage`
group-level): the node stamped only its own dimension; fixed with a loop over the
ancestors — `for (k < depth) vals[dims[k].first] = parentPath[k]` — seeding ancestor
dimension cells under their own head-column ids before the node's own. (RAM detail
rows are live storage nodes and already carry every attribute — only the synthetic
subgroup headers needed it.) Regular multi-level grouping is unaffected.

**Sort arrow re-syncs after a dialog commit.** With `SyncColumnArrowsFromModel`
deleted, a sort committed through the "List settings…" dialog (not a header click)
updated the header arrow only after a full column refresh — `OnColumnClick` sets the
arrow only on a click, while the dialog commits the composer + `NotifyReset` without
reaching the columns. Fix without resurrecting `GetSortArrows`: a virtual
`ibDataViewColumnBase::SyncSortArrowFromModel()` (`dataview.h`, base no-op), overridden
by `ibDataViewColumnObject` (`tableBoxColumn.cpp`) to read the composer via
`GetControl()->GetSourceFieldName()` and `SetSortOrder` on a match; called from the
column's `OnUpdated` (form build) and from `datavgen` `PagedBootstrap`
(`ResetAllSortColumns()` + per column) after a refresh. ⚠️ The whole override is under
`#ifndef OES_USE_WEB` — `ibDataViewColumnObject` is desktop-only, so leaving it visible
breaks the `wfrontend.dll` compile (C2653).

### 9.6 Generic row — unification confirmed complete, vestige retired (2026-07)

The "generic row" goal (one row class replaces the four per-family list row classes; the
driver emits final model rows; the conversion loops + refcount dance retire) was mostly
**already landed** — a re-audit found no per-family rows left. `ibValueTableEnumRow`,
`ibValueTreeListNode`, and `ibValueTableKeyRow` are gone (the former `objectList.h/.cpp`
was itself removed, the surviving list bodies folded into `commonObject.*`);
**`ibComposerNode`** (`model.h`) is the single row for every family —
enum / catalog / FolderRef tree / register — carrying the values map (`m_nodeValues`), the
identity (`m_rowKey` PK values, `m_groupPath` dimension path, or live-node pointer), and the
container flag (`m_container`). `RunComposerPage` (DB + RAM) wraps the driver's
`ibListFetchDriver::Row` into an `ibComposerNode` per row — one unified path, not four.

Remaining cleanup done here: the vestigial `ibValueModel::AdoptRowsToItems` template
(the old typed-Fetch→universal ownership-transport bridge) had **zero callers** and was
removed. What was *not* done, deliberately: pushing node construction down into the
driver's `OnRow` so the driver emits `ibComposerNode` directly. `ibListFetchDriver` is a
generic `ibCompositionDriver` in `composition/`; `ibComposerNode` is a model concept
(`m_rowKey` / `m_groupPath` / container semantics the driver has no business knowing).
Coupling them would regress a deliberate layer split for no functional gain — the
per-row wrap in `RunComposerPage` stays. Row identity still uses `m_rowKey` / `m_groupPath`
/ pointer rather than `ibValue::GetHashKey()`; unifying that belongs to the L5-2 identity
consolidation (§9.3), a separate arc.

### 9.7 `ibReadPageRequest` cleanup + hierarchy scope as a KEY, not a bare guid (2026-07)

**Dead fields removed.** `m_anchorGuid` (`wxString`) was WRITE-only — the selector set it
(`objectSelectorQuery.cpp`) but nothing read it; the keyset cursor is `m_anchorSortValues`,
which already carries the row PK as a reference VALUE (its own tail, encoded by
`ReferenceKeyBlob`). `m_parentRefField` (the "legacy physical name" seam) was READ-only —
never written anywhere, so its ternary always fell through to `ReferenceFieldOf(m_parentCol)`.
Both removed. Also removed: the dead `ibListFetchDriver::ibTreeScope` struct + the driver's
2-arg tree ctor — nothing ever constructed a scope; the live path fills the request directly
in `RunComposerPage`.

**Rename.** The hierarchy block (`m_parentFilter` / `m_parentCol` / `m_parentGuid`) is set
ONLY on a tree drill (`modelDb.cpp` under `if (hierarchy …)`) and read only by the
parent-ref predicate + the cache guard — a hierarchy-only concern — so `m_parent*` →
`m_hierarchy*`. (`ibTreeScope`'s own `m_parent*` would have stayed as "the browsed parent
node", but the struct is gone.)

**The scope is a KEY value now, not a bare guid.** `m_hierarchyGuid : ibGuid` →
`m_hierarchyKey : ibValue` (the browsed parent's own PK reference; empty = roots).
`BuildParentRefPredicate` used to take a bare `ibGuid` and rebuild the parent `_RRRef` blob
from `queryable->GetQueryTableId()` — baking in **two assumptions**: (a) the hierarchy is
keyed by a guid, and (b) the parent is the same table (self-hierarchy), since the metaID came
from the queryable, not the parent. Both are crutches: a hierarchical queryable could be keyed
by anything (a string code), and the parent reference already self-describes its metaID. Fix:
pass the parent KEY value whole and encode it the SAME way the keyset anchor does — a reference
→ `ReferenceKeyBlob` (self-describing metaID, no same-table assumption), a non-reference key →
`ibConst`. Byte-identical for a self-hierarchy (the only source shape today: `ReferenceKeyBlob`
reads the ref's own metaID, which == `GetQueryTableId()` there), so no behavior change — just
the two assumptions gone. One residual: the empty-ROOT sentinel still derives the empty-ref type
from `GetQueryTableId()`; correct for self-hierarchy, and a fully non-reference hierarchy would
additionally need the root case + `GetHierarchyColumn` generalized — future work, no such source
exists yet.

**Audit — is the anti-pattern elsewhere?** No. The "decompose a reference to a bare guid, rebuild
the blob with a same-table assumption" shape was unique to `BuildParentRefPredicate`.
`WhereKey(ibGuid)` / `WhereKeyIn` look similar but filter the **uuid identity column** (which IS
a guid, not a reference — no metaID half to reconstruct), so a bare guid is the natural key there.
`ReferenceKeyBlob` is the correct canonical encoder. `GetQueryTableGuid()` returns the queryable's
own table-identity guid (the dynamic list's `GetGuid()`), not a decomposed row reference. All
legitimate.

### 9.8 Viewport after a re-order — the offset, not the anchor (2026-07-31)

Sorting drops the scroll ANCHOR on purpose: it is a keyset cursor (row PK + sort values), and
in the new ordering it names nothing. `PagedRefresh` therefore clears it (`skipCapture`) and
`PagedBootstrap` re-fetches from the top of the new order. That is correct — and it is also
where the viewport used to go missing, in two different ways.

**Nothing focused → the pixel lied.** With no anchor AND no focus there was nothing to
position on, so bootstrap simply did not scroll. But `wxScroll`'s scroll-Y is a PIXEL and it
survives `DestroyTree`, while the row that pixel used to name does not: the user landed in the
middle of a list they had just re-ordered, or past its end. The buffer is rebuilt from the
FIRST row of the ordering, so the viewport belongs at the top of it — `ScrollTo(crumbCount)`,
a no-op for the cold open that reaches the same branch.

**Something focused → the row was pinned to the top.** The bootstrap cursor falls back to the
focus, so the focused row came back as `items[0]` and sat at the very top of the viewport —
the row survived the sort, everything the user was reading around it slid up. What was missing
is not the row but the DISTANCE: `m_pagedRestoreFocusOffset` = focus row − visible top,
captured only on the `skipCapture` path (the only one with no anchor to answer with) and only
when the focus was actually on screen.

It is consumed ONCE, in `OnPagedFetchBackwardResult`, because that is the first moment it can
be honoured — after the bootstrap there are no rows ABOVE the focus to scroll into. The
backward fill prepends a batch, then the final `ScrollTo` targets `m_currentRow − offset`
instead of "the old top + n". Driven off `m_currentRow` (already shifted by the prepend) rather
than the saved index, so a focus-scan that MISSED drops the offset instead of scrolling to a
row nobody found. Dropped explicitly on every path that cannot use it — no backward rows,
selection-cursor mode (which centres the row itself, a stronger intent), an anchor that
survived — so an ordinary backward scroll can never inherit a sort's answer.

### 9.9 What the user sees while a portion is out (2026-07-31)

The read left the UI thread (§ table-model.md "Off the UI thread"), so for the first time the
control has a STATE between "asked" and "answered". Everything below is that state made honest;
each rule exists because its absence was visible on screen.

**One frame, not two.** A Reset pages FROM the row the user was on, so on its own it would put
that row at the top of the viewport and a later backward portion would slide it down — the list
visibly jumped up and then settled. The read now pulls the rows ABOVE the cursor in the same pass
(`ibPagedFetch::m_backfill`) and glues them in front of the page. The control receives one portion,
scrolls, and the row is already where the eye expects it.

How much to pull depends on which cursor mode the Reset is in, and both cases are the same
mechanism:

| Mode | Backfill | Where the row lands |
|---|---|---|
| Anchor (sort, filter, plain refresh) | the saved screen offset (§9.8) | at the same distance from the top it had |
| Selection (a record was just written) | a WHOLE viewport | mid-viewport; the scroll clamps if the ordering ends at the row |

A viewport rather than half of one, because a new record usually sorts LAST: the page below the
cursor is then a single row, and half a screen of backfill leaves the other half blank. Reading a
viewport costs one query either way and fills the window in both cases — the scroll decides where
the row sits, not the size of the read. Where the backfill delivered rows, the separate backward
portion is NOT dispatched: prepending a second time is the slide this fold removes. The symptom it
cured was reported as "the zebra flickers and the height lags on save" — two frames, the second one
taller than the first.

Two flags read off the same split: `m_pagedHasMoreFwd` counts the FORWARD part only
(`n - m_backfilled`, or a short page looks full), and `m_pagedHasMoreBwd` goes down when the
backfill returned fewer rows than asked — it hit the top, and leaving the flag up costs an empty
backward portion, which is one more frame for nothing.

**The offset itself** is `m_pagedRestoreFocusOffset` — focus row minus visible top, captured only
where the scroll ANCHOR is deliberately dropped (a sort: its cursor key means nothing in the new
ordering) and only while the focus was on screen. §9.8 is why it exists; this section is where it
is now spent.

**The busy arc.** `IsFetchInFlight() || m_pagedNeedsBootstrap` — waiting starts when the rows go,
not when the read leaves, because a whole idle pass separates the two and that gap was the blank
white rectangle. The arc appears only once the read has been out `kBusyShowDelayMs` (200 ms), and
that rule has NO exception: an empty table used to raise it immediately, on the reasoning that a
blank rectangle says nothing — but a RAM-backed table answers in microseconds, so what that
produced was an arc flashing on every instant read and then held for its minimum show. Reads slow
enough to be worth naming cross 200 ms regardless. Once up the arc stays `kBusyMinShowMs` (250 ms)
from the moment it appeared, and the DELIVERY takes it off — not the next idle or tick, which
lagged a frame behind the rows.

⚠ **A frozen window paints nothing — so the arc has a window of its own.** `ibDataViewBusyWindow`
is a small badge, a child of the CONTROL rather than of the rows area, shown while waiting and
hidden by the delivery. Painting it at the tail of the rows' `OnPaint` (as it first was) meant
freeze OR spinner, never both: `Freeze()` silences a window and everything painted inside it. A
sibling window is frozen by nobody.

That is what lets the rows-area freeze stay. It covers the REBUILD — wipe → build → focus → scroll,
armed on arrival and dropped by `EndBootstrapFreeze()` at the end of `OnPagedFetchResetComplete`
(with a watchdog copy in `OnInternalIdle` for the delivery that never comes) — so the half-built
tree is neither shown nor painted: one composite frame, and no pass per step of the rebuild.

What the freeze must NOT cover is the wait before the portion and the settle after it. Before: the
old rows are still the best answer available and stay legible. After: top-ups and the backward pull
move the viewport in the open, deliberately — a freeze held over the settle hides a focus landing
on the wrong row or a selection jumping behind a clean picture, which is precisely how those
defects went unnoticed while it did.

**Alternate-row stripes.** Painted from `(row + m_stripePhase) % 2`, not the raw index: the buffer
is a sliding window (a backward portion prepends, a trim drops from the front, a Reset rebuilds),
and every shift changed a row's parity — the whole list repainted in the other shade and read as
flicker. The phase is corrected where the buffer shifts and set on a rebuild so the row the user
was looking at keeps its shade.

**Restore precedence.** Explicit prefer > programmatic stamp (`SetPagedRestoreSelection`) >
pre-refresh current row. The middle case is load-bearing now that `SchedulePagedRefresh` posts
onto the event loop: a post-Save Select lands BEFORE the refresh, so recomputing focus and the
"was selected" flag from the ambient state threw the intent away — the second created row was
positioned to but not highlighted, and then re-selected the previous one. A stamp arriving after
the read was dispatched bumps the generation and re-arms, since the answer in flight was computed
for the older question.

**No phantom current row.** wx makes a focused control usable from the keyboard by stamping row 0
as current when it has none; while a restore is still coming that painted a highlight at the top
which then jumped away. `OnSetFocus` now stands aside while `m_pagedRestoreFocus` is pending.
