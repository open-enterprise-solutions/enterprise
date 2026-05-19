# macOS — Tables / TableBox / DataView known issues

**Status:** open. User-reported as "tables don't work on Mac" — investigation
2026-05-19 found three structural causes that ship as platform divergence in
the vendored `ibDataView` / `ibGrid` controls. None has a single-commit fix;
the real resolution is a multi-day refactor of the virtual model on macOS.
Documenting here so the symptoms aren't mistaken for new regressions.

## Symptoms

The user sees one or more of:

1. Tabular form widgets (`ibValueModelTableBox`) appear empty or stale even
   when the underlying data set has rows. Adding / removing rows in script
   doesn't update the rendered table.
2. Clicks on visible rows miss — row highlights don't appear, double-click
   doesn't fire the edit handler.
3. The table renders wider than its sizer slot, painting over neighbouring
   controls.

## Root causes

### 1. `ibDataViewVirtualListModel` is stubbed on macOS

`/Volumes/T9/Web/oes-enterprise/src/engine/frontend/win/ctrls/dataview/dataview.h:173-176`:

```cpp
#ifdef __WXOSX__
// better than nothing
typedef ibDataViewIndexListModel ibDataViewVirtualListModel;
#else
class FRONTEND_API ibDataViewVirtualListModel : public ibDataViewListModel { … };
#endif
```

The body of the virtual model (every notification: `RowAppended`,
`RowInserted`, `RowDeleted`, `RowsDeleted`, `RowChanged`, `RowValueChanged`,
`Reset`, `GetRow`, `GetItem`, `Compare`) lives behind `#ifndef __WXOSX__` in
`datavcmn.cpp:223-328`. On macOS, every caller that expects a
`ibDataViewVirtualListModel` gets an `ibDataViewIndexListModel` instead,
which stores actual hash items rather than tracking `m_size`. Notification
semantics differ — rows don't propagate to the wxDataViewCtrl.

`tableView.cpp:25` declares `wxDataViewFilterModel : public ibDataViewVirtualListModel`
— resolves to the index-list base on macOS, breaking every filter-overlay
table.

### 2. Paged dataview gates state on `__WXMSW__`

`/Volumes/T9/Web/oes-enterprise/src/engine/frontend/win/ctrls/dataview/datavgen.paged.private.h:18, 48`
guard the paged-private struct with `#ifdef __WXMSW__`. The paging refetch
work that landed in commits `ca474008`, `28bdf1e8`, `5994a9a9`, `db87ecab`,
`df4e7463`, `ff8bd7cd` ships only the Windows branch. macOS falls back to
the pre-paging path which combined with cause #1 means the
`tableBox.cpp:228-238` bootstrap-restore channel for paged models has no
working notifier.

### 3. `MacSetClipChildren` permanently disabled on wx 3.3.2

`/Volumes/T9/Web/oes-enterprise/src/engine/frontend/win/ctrls/dataview/datavgen.cpp:4007`:

```cpp
#if defined(__WXOSX__) && !wxCHECK_VERSION(3, 3, 0)
    MacSetClipChildren(true);
#endif
```

Submodule is wxWidgets 3.3.2 so the guard is always false. Upstream removed
`MacSetClipChildren` as a hint, but the generic dataview on macOS still
relies on the child being clipped to its parent. A `tableBox` inside a form
inside notebook/splitter renders, but mouse hit-test and visible-rect /
`EnsureVisible` calculations fall off — clicks miss rows.

## Recommended next actions (multi-day work)

1. Compile out the macOS short-circuit in `dataview.h:173-176` and rebuild
   the full `ibDataViewVirtualListModel` body on macOS, removing every
   `#ifndef __WXOSX__` guard in `datavcmn.cpp:223-328`. Verify the
   `wxDataViewCtrl::Notifier` API works correctly with the virtual model
   on macOS (wxWidgets 3.3.2 generic dataview supports it; the original
   guard predates the generic backend on macOS).

2. Drop the `__WXMSW__` gates around the paged-private struct
   (`datavgen.paged.private.h:18, 48`). The paged code is platform-agnostic
   in design — the gating was conservative early-development scaffolding.

3. Re-add macOS clip-children behaviour either via `SetCanFocus(false)` on
   the wrapper sizer or by overriding `wxControl::DoSetSize` to call
   `Refresh()` on the parent. The exact patch needs a quick test pass with
   a representative form (Catalog with tabular section, Document with form,
   form with TableBox + GridBox on the same page).

## Workaround until fix lands

Use the Windows binary on VM 106 for any work that exercises `tableBox`
or `gridBox` widgets — those have full coverage. The macOS binary is fine
for everything else: code editing, metadata navigation, syntax helper,
debugger, plugin development, BSL/CES compilation.

## References

- Investigation 2026-05-19 (this document)
- `docs/web/open-issues.md:227, 270` — pre-existing acknowledgement that
  `tablebox/gridbox` is "the unfinished model-driven control"
- Commits exercising the paged path (Windows-only at landing time):
  `ca474008`, `28bdf1e8`, `5994a9a9`, `db87ecab`, `df4e7463`, `ff8bd7cd`
