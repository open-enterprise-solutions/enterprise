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
> [connection-pool.md](connection-pool.md) (where the DB fetch's connection comes from).
>
> **Status:** landed, but **still moving.** This is one of the most-churned subsystems in the
> engine — a long-running, constantly-evolving mechanism. Treat any specific below as a snapshot;
> the shape (§0) is the durable part. Every standard list/tree/register/value-table is an
> `ibValueModel` fetching through `RunComposerPage`; the legacy per-family models and the typed
> per-model `Fetch` are DELETED. Web HTTP fetch endpoint is the one deferred piece (§9, §10).

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
// ibValueModel (tableInfo.h) — PURE VIRTUAL, realised per SOURCE KIND, not per table type.
// The body renders the model's composer to a page; the composer is the source of truth.
virtual unsigned int RunComposerPage(const ibDataViewItem& parent, const ibDataViewItem& anchor,
    int count, ibFetchDirection dir, ibDataViewItemArray& out) const = 0;
```

Two realisations, and only two:

| Realisation | Source | What it does | Rows it hands out |
|---|---|---|---|
| `ibValueModelCursor::RunComposerPage` (`tableInfoDb.cpp`) | a queryable (DB) | render the composer → SQL → **keyset** page → walk driver rows | **COPY** nodes (`ibComposerNode`) |
| `ibValueModelStorage::RunComposerPage` (`tableInfoRam.cpp`) | `ibRamValueStorage` (live nodes) | `ibDataRamComposer::ComputeOrder` filters + sorts the live rows **in place**, windowed by anchor | the **LIVE** storage node (the node IS the row) |

Every table type in the product — dynamic list, table-of-values, tabular section, register
recordset — is one of these two by inheritance (§7). Filter / sort / group are **not** per-model:
they live in the L5 composer (§5). This is the "one bus" applied to tabular data — add a table
kind by choosing a source realisation, not by writing a fetch.

---

## 2. Two layers, one object — the model bridge

A table is two collaborating bases fused into one runtime object:

| Layer | Class | Lives | Is |
|---|---|---|---|
| **View contract** | `ibDataViewModel` | `backend/tableView.h` | wx-neutral: what a data-view control needs (values, hierarchy, notifier, paged fetch) |
| **Script + composition** | `ibValueModel` | `backend/tableInfo.h` | `ibValue` + tabular command/data object: the model as a script value, owner of the L5 composer |

`ibValueModel` does **not** inherit `ibDataViewModel` — it *owns a bridge* to it, a nested
`ibDataViewModelProviderImpl m_modelProvider` (tableInfo.h) that forwards every view virtual
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

- **`ibDataViewObject : wxRefCounter`** (`tableView.h`) — the base every row/node subclass
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
  `s_constIgnoreParent` (tableView.h) = a FLAT scan of a *hierarchical* source (one `ORDER BY`
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

**Async.** `SubmitFetchAsync(work)` routes through `ibSession::Submit` (the worker pool) so a
DB round-trip never blocks the UI thread; the control marshals the result back via `CallAfter`
and discards it if a generation token moved on (filter/sort changed mid-flight). The backend
model itself is stateless — the prefetch deque and scroll position live in the control (§6).

---

## 5. Filter / sort / group — L5, by field-path

A model carries `m_listSettings` (`ibValueListSettings`, `backend/composition/listFilter.h`) —
the script-visible container of **Filter** / **Order** / **Group**, a thin facade over the L5
composer. `RunComposerPage` reads the composer **directly every fetch**; the legacy per-model
`m_filterRow` / `m_sortOrder` are abolished. Settings are applied on change (not cleared and
rebuilt per fetch) through `ibApplyDynamicSettings(composer, settings)`.

Fields are **paths**: a dot-walk (`"Ref.Owner"`) resolves to an auto-JOIN on the door. Grouping
is any query-result field, not the table's parent column: a non-empty Group turns a flat list
into a drillable tree via `TOTALS BY <dim>` — **including aggregate-free pure grouping**. Drill
re-fetches scoped by the already-drilled dimension values. (Group-level rows currently load a
whole level at once — group paging is a follow-up; detail rows page normally.)

Header-click sort is committed entirely on the **front** (`ibValueModelTableBox::OnColumnClick`
pokes this model's composer + `RefetchAll`) — the model bridges no `{col, asc}` pair.

---

## 6. Notifier + refresh — pure push, single entry

`ibDataViewModelNotifier` (`tableView.h`) is **PURE PUSH**: the model tells the front WHAT
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
| `backend/tableView.{h,cpp}` | `ibDataViewModel`, `ibDataViewItem`, `ibDataViewObject`, notifier, `ibFetchDirection`, `s_constIgnoreParent` |
| `backend/tableInfo.h` + `tableInfo.cpp` | `ibValueModel`, the provider bridge, `Get*Fetch` defaults, `RefetchAll`, `ResolveAnchorByKey`, `m_listSettings` |
| `backend/tableInfoDb.cpp` | `ibValueModelCursor::RunComposerPage` — DB keyset fetch |
| `backend/tableInfoRam.cpp` | `ibValueModelStorage::RunComposerPage` — RAM in-place fetch; `ibValueModelRamTableBase` |
| `backend/composition/ramComposer.{h,cpp}` | `ibDataRamComposer::ComputeOrder` (RAM filter/sort) |
| `backend/composition/listFilter.h` | `ibValueListSettings`, `ibApplyDynamicSettings` |
| `system/value/valueTable.h` | `ibValueModelTable` (table-of-values) |
| `system/value/valueDynamicList.{h,cpp}` | `ibValueDynamicList` |
| `frontend/win/ctrls/tableView.{h,cpp}`, `datavgen.cpp` | `ibDataViewCtrl` — the forked control |
| `frontend/visualView/ctrl/tableBox*.cpp` | `ibValueModelTableBox` — the form control |

---

## 10. Honest remainder

- **Web HTTP `/fetch` endpoint — not wired.** The universal `Get*Fetch` was designed to serve
  the web frontend over HTTP (`/fetch?parent=…&anchor=…&count=…` → JSON), but `wfrontend.cpp` is
  not yet on it (building the JSON schema ahead of a consumer would freeze it blind). Without
  paging the web client OOMs on large catalogs. See [paging-design.md](paging-design.md) §8.9.
- **Group-level paging is a follow-up.** A grouped model loads a whole group level at once;
  detail rows page normally. Fine for typical dimension cardinality, not for a level with
  thousands of groups.
- **The `ibVisualHost` scrollbar flash on form open is NOT the data-view.** `ibDataViewCtrl`'s
  own vertical scrollbar range is always 0; the flash is the form host (`wxScrolledWindow`)
  setting a virtual size larger than its placeholder client during build. Tracked in
  [paging-design.md](paging-design.md) §8.8 — needs a form-host lifecycle change.
- **Absolute-row random access is intentionally absent.** Cursor pagination gives smooth scroll,
  not cheap "row 5000 of 10000". If a UI demand surfaces, an `OFFSET ?` anchor variant is the
  path (see [paging-design.md](paging-design.md) §6).
