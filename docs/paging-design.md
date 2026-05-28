# Paged data fetching for ibValueListDataObject / ibValueModelTreeDataObject

**Status:** Phase 4 + tape paging shipped, evolved into universal
`Get*Fetch` architecture (§8). Backend Fetch contract is shared by
desktop wxDataView fork, headless callers, and the upcoming web
frontend. Sections 1-7 describe the original templated buffer design
(landed); §8 documents the consolidated direction we converged on
during the 2026-05-05 session.

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

* `ibValueModel` — script-exposed base (`ibValue` + `ibActionDataObject`),
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
class ibValueModel : public ibValue, public ibActionDataObject, ... {
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
  `ibFetchResponse<TKey, TRow>`, `ibViewport` — `tableInfo.h`.
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

### Phase 4 diagnostics — `ibPagingLog` ✅ landed 2026-05-05
`BACKEND_API void ibPagingLog(const char* fmt, ...)` — single sink,
emits to `wxLogDebug` (Debug-only) and appends a timestamped line to
`paging.log` in the working directory (any build).  Wired at:
* Buffer entry points: `NotifyViewportChanged`, `ResetForFilterOrSort`,
  `DispatchReset` / `Forward` / `Backward`.
* All three Fetch impls: entry (table + count + anchor + direction),
  full SQL after build (Ref only), post-exec (rows + hasMore).

Open the file alongside the running enterprise / wes process to trace
buffer / fetch behaviour without attaching a debugger.

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

* `enterprise/src/engine/backend/tableInfo.h` / `.cpp` — `ibFetchRequest`,
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

**Open**: ownership transport convention for `Get*Fetch` results.
Currently `vector<TRow*>` raw pointers; caller IncRefs via item ctor
but no explicit DecRef on the initial allocation refcount → leak. Two
clean fixes:

1. Switch result transport to `vector<wxRefPtr<TRow>>` — auto-Adopt
   on push, auto-DecRef on destroy. No manual cleanup.
2. Add `AdoptTag` ctor on `ibDataViewItem` that takes ownership of
   the initial refcount without an extra IncRef; caller uses it for
   transfer. Existing copy ctor stays IncRef'ing.

Pick one before turning paged Fetch on for real workloads.

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
| `ibTableViewBuffer<TKey,TRow>` tape paging (Ref) | ✅ landed Phase 4 |
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
| Async fetch via `ibSession::Submit` | ✅ landed 2026-05-06 — `ibDataViewModel::SubmitFetchAsync(work)` virtual + `ibValueModel` override forwarding to `ibSession::Submit`; default impl runs inline. Result UI marshalling done by control via `wxTheApp->CallAfter`-style hops (`OnPagedFetchForwardResult` / `OnPagedFetchBackwardResult`) |
| In-flight fetch cancellation (generation tokens) | ✅ landed 2026-05-06 — `m_pagedFetchGen` bumped on every reset (`PagedRefresh` / `Cleared` / `AssociateModel`); fetch lambda captures the token at submit time and discards the result if the live token has moved on |
| Per-direction in-flight fetch counters | ✅ landed 2026-05-06 — `m_pagedFetchingFwd / m_pagedFetchingBwd` independent so a scroll burst that crosses both edges can dispatch to each side without serialising (was a single counter pre-2026-05-06) |
| Soft-eviction via `wxDataViewTreeNode::SetHidden(bool)` | ✅ landed 2026-05-06 — hidden nodes stay in `m_children` but are skipped by walkers and excluded from `subTreeCount`; backward scroll re-shows fetched rows from the hidden head instead of going to DB. Replaces the un-hide-everything anti-pattern flagged in 2026-05-05's «Scroll polish» |
| 3-state lying scrollbar (THUMB drag fix) | ✅ landed 2026-05-06 — `IsPagedScrollbarMode / DerivePagedThumb / UpdatePagedScrollbar / AdjustScrollbars / SetScrollPos / SetScrollbar` overrides force the thumb to {Top, Middle, Bottom} from `m_pagedHasMoreFwd / m_pagedHasMoreBwd` regardless of `wxScrollHelper`'s real-virtual-size value. Thumb drag no longer chains many fetch ticks |
| Hierarchical drill chain pin | ✅ landed 2026-05-06 — `m_topParentChain` (refcount-pinned `ibDataViewItem` array) survives `Cleared() / DestroyTree`; replaces a single anchor that was wiped on re-fetch |
| wxDVC sort suppression for paged | ✅ landed 2026-05-06 — `ibDataViewMainWindow::GetSortOrder()` returns empty `SortOrder()` when the model `IsPagedModel()` so the fork's `InsertChild` appends in fetch order rather than scattering rows via binary-search insertion |
| Ownership-transport convention (`AdoptRowsToItems` / Append / Insert) | ✅ landed 2026-05-05 — `ibValueModel::AdoptRowsToItems` template; FolderRef bridges use it; tape-buffer Apply path already adopts via `Append`/`Insert` |
| Selection re-locate by stable key | covered by existing `FindRowValue` + new `IsEqualTo` virtual on row — current-row check inside loaded buffer; no separate `FindRowByKey` API (request from 2026-05-05 plan superseded by the `IsEqualTo` path) |
| `ibPredefinedValueObject` shared_ptr/refcount mixing | ✅ closed 2026-05-06 — audit confirmed no callsite passes the object through `ibDataViewItem` (ctor takes only model/view rows), so the original mixing concern doesn't apply. Base stays `wxRefCounter` — `ibPredefinedValueObject` is not part of the data-view hierarchy |
| RAM-backed `Get*Fetch` for TabularSection / ibValueTable | ✅ landed — new intermediate base `ibValueModelRamTableBase` (`tableInfo.h:1257`) hosts `GetFirstFetch` slicing `m_nodeValues` by anchor+count (`tableInfo.h:1441`). Inherited by `ibValueTabularSectionDataObjectBase` (`tabularSection.h:8`), `ibValueModelTable` (`system/value/valueTable.h:12`), and `ibValueRecordSetObject` for registers. `Features::RamFetch` flag advertised via `GetFeatures()` override |
| Composite-key cursor for registers | ✅ landed — `registerSqlBuilder.{h,cpp}` mirrors `ibListSqlBuilder` for registers: `EffectiveOrder = [user_sorts] ++ [identity_tail]` (recorder+line for HasRecorder, period?+dimensions otherwise), `BuildAnchorPredicate` emits the same composite cmp_op + case-when tiebreak as Ref. `ibValueListRegisterObject::Fetch(ibFetchRequest<ibUniqueKeyPair>)` (`objectListQuery.cpp:333`) is the cursor-paginated single SQL point; `GetFirst/Next/PrevFetch` (`objectListQuery.cpp:449/477/497`) build the anchor via `BuildRegisterAnchor` which captures `m_sortValues` directly from `row->GetTableValue(attr->GetMetaID())`. `ibUniqueKeyPair::operator<>` aren't used here — cursor binds `m_sortValues`, not the key itself; equality goes through `m_keyValues == other.m_keyValues` for selection survival and dedup |
| FB binary BINARY(20) bind for parent ref | ✅ landed 2026-05-07 — column is `BINARY(sizeof(ibReference)) = 20` bytes (`[ibGuidImpl 16][ibMetaID 4]`), not CHAR(16). FolderRef `GetNextFetch / GetPrevFetch` build a `ibReference` matching how save-path stores it (m_id=catalog metaID always, m_guid=zero for top-level / real for drill) and bind via `SetParamBlob`; SQL uses `WHERE refDataField = ?`. C++ byte-matcher gone. Bonus: `firebirdParameter.cpp::SetParamBlob` ctor got an `SQL_VARYING` branch (was a silent no-op for varying-typed params — write u16 length prefix + data into sqldata) |
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
| Sort + filter types in tableView.h | ✅ landed 2026-05-09 — `ibSortOrder` / `ibSortData` / `ibSortModel` / `ibFilterRow` / `ibComparisonType` migrated from `tableInfo.h` to `tableView.h` so `datavgen.cpp`'s header-arrow path (`SyncColumnArrowsFromModel`) doesn't need to pull the value-subsystem header.  `tableView.h` now includes `<algorithm>` + `<vector>` + `backend/system/value/valueType.h`; no cycle (value subsystem doesn't include `tableView.h`).  See `session-2026-05-09.md` §10 |

### 8.9 Open audits

* **Web HTTP `/fetch` endpoint.** `wfrontend.cpp` not yet wired to
  the universal `Get*Fetch` path. Plan: `/fetch?parent=…&anchor=…&
  count=…` → JSON, mirroring desktop's `DispatchPagedFetch`. See
  §8.8 "Web HTTP fetch endpoint" row.

### 8.10 Closed audits

* ~~**`ibDataViewObject` migration coverage.**~~ Closed 2026-05-07
  — every `ibDataViewItem(void*)` callsite confirmed safe. The
  Mode::RawId ctor (`tableView.h:112`) is used only by virtual-list
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
