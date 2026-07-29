# The wx control fork — what is forked, what is vendored, what is ours

> **Scope:** `frontend/win/` (~367 files) — the forked and vendored widget layer under the
> OES controls. Companions: [uikit.md](uikit.md) (the custom-drawn engine + Luna theme),
> [docview-fork.md](docview-fork.md), [paging-design.md](paging-design.md),
> [spreadsheet-editor.md](spreadsheet-editor.md), [designer-editors.md](designer-editors.md).
> This is foundation code.

---

## 1. Layout

| Dir | Files | What |
|---|---|---|
| `ctrls/charts/` | **246** | **vendored** wxCharts (§3) |
| `ctrls/dataview/` | 11 | **forked** wxDataViewCtrl + our paging (§2.1) |
| `ctrls/grid/` | 9 | **forked** wxGrid → `ibGrid` (§2.2) |
| `ctrls/` (loose) | ~18 | **ours** — `checktree`, `controlButton`, `controlTextEditor`, `controlCheckboxEditor`, `controlStaticText`, `tableView`, `dynamicBorder`, `floatingNotebook`, `toolBar` |
| `dlgs/` | 35 | dialogs |
| `editor/` | 34 | `codeEditor` / `gridEditor` / `textEditor` |
| `theme/` | 6 | theming |

The 246-file charts library dominates the file count and is the least OES-specific thing
here — the number is not a measure of complexity.

---

## 2. Forks — upstream code we carry and changed

### 2.1 `dataview/` — wxDataViewCtrl

Files: `datavgen.cpp` (the generic implementation), `datavcmn.cpp`, `dataview.h`,
`dvrenderers.h`, `headerctrlg.{h,cpp}`, `headerctrlcmn.cpp` — plus **two files that are
purely ours**:

```
datavgen.paged.cpp
datavgen.paged.private.h
datavgen.scrollbar.cpp
```

The private header says why it exists:

> Private helpers shared between `datavgen.cpp` and `datavgen.paged.cpp` — the
> **paged-bootstrap split (2026-05-08)** moved several methods out of the main translation
> unit but left their dependencies (RAII freeze guard + buffer-slack constant) in the
> original file. This header re-exposes them at namespace scope so both `.cpp` files
> compile.
>
> Header is intended for these two `.cpp` files only. **Do not include from public headers
> or other translation units.**

So the fork's shape is: **upstream's control, with paging grafted in** — the platform's list
is virtual/paged over a database ([paging-design.md](paging-design.md)), which upstream's
model never anticipated. `datavgen.scrollbar.cpp` is the matching scrollbar behaviour.

This is the file where the "more items exist, no scrollbar, yet it scrolls" and
"the flag does not get through" comments live — the paging seam is the delicate part.

### 2.2 `grid/` — wxGrid → `ibGrid`

`gridext.h` keeps upstream's header verbatim:

```
// Name:        wx/generic/grid.h
// Purpose:     ibGrid and related classes
// Author:      Michael Bedward (based on code by Julian Smart, Robin Dunn)
// Modified by: Santiago Palacios
// Licence:     wxWindows licence
```

Note the honesty: the **Purpose line was updated** to say `ibGrid`, the authorship and
licence were **kept**. Files: `gridext` / `gridextctrl` / `gridexteditors` / `gridextsel` /
`gridextprivate.h`.

`ibGrid` is what `ibGridEditor` derives from
([spreadsheet-editor.md § 1](spreadsheet-editor.md)) — so the spreadsheet document is drawn
by a forked wxGrid.

---

## 3. Vendored — wxCharts

`ctrls/charts/` — 246 files, `wxareachart`, `wxbarchart`, `wxlinechart`, `wxpiechart`, … each
with `*ctrl`, `*options`, `*datasetoptions` companions.

```
Copyright (c) 2017-2019 Xavier Leclercq
Permission is hereby granted, free of charge, … including without limitation the rights to
use, copy, modify, merge, publish, distribute, sublicense, and/or sell copies …
```

An MIT-style grant — **commercially clean**, unlike the compression codecs
([serialization-io.md § 4](serialization-io.md)). It was taken from a third-party developer
deliberately for that reason, and vendored as-is.

### 3.1 Why it is here — the intent

This is **groundwork, not a stray dependency**. The plan it was brought in for:

> **draw a chart → render it to a picture → push the picture into the grid.**

That is: charts as *content of a spreadsheet document*, so a report can carry a bar chart, a
pie, or a **Gantt** diagram the same way it carries text and numbers. The pieces the plan
relies on already exist independently:

- wxCharts renders to a wx drawing context;
- a picture can be pure bytes with no metadata (`eFromFile` —
  [pictures.md § 2](pictures.md));
- a spreadsheet cell is data in a description, and the document is a value
  ([descriptions.md § 4](descriptions.md), [report-engine.md](report-engine.md)).

So the chart never needs to be a live widget inside the document — it becomes an image, and
the document stays a plain value that serialises, diffs and prints like any other.

The frontend control is `chartBox` (`frontend/visualView/ctrl/chartBox.cpp`), which includes
`frontend/win/ctrls/charts/wxcharts.h` directly.

**State today:** `chartBox` is wired to the library but populated with **demo data** —
`wxChartsCategoricalData` / `wxChartsDoubleDataset("Dataset 1", …)` / `wxChartSliceData(300,
…, "Red")`, with a commented-out `wxMath2DPlotCtrl` alternative. The render-to-picture →
into-grid path is the part not yet built. Read the 246 files as a **stocked shelf**: the
renderer is present and licensed; the wiring to real data and to the spreadsheet is the
remaining work.

---

## 4. Ours

The loose files in `ctrls/` are OES controls with no upstream:

- `checktree` — tree with checkboxes
- `controlButton` / `controlStaticText` / `controlTextEditor` / `controlCheckboxEditor` —
  in-cell / in-form control primitives
- `tableView`, `dynamicBorder`, `floatingNotebook`, `toolBar`

`editor/` holds the three editors ([designer-editors.md](designer-editors.md),
[spreadsheet-editor.md](spreadsheet-editor.md)):

| Editor | Base | Lines |
|---|---|---|
| `ibCodeEditor` | `wxStyledTextCtrl` | ~6 600 |
| `ibGridEditor` | `ibGrid` (§2.2) | ~3 800 |
| `ibTextEditor` | `wxStyledTextCtrl` | ~380 |

`ibTextEditor` is the small one — a plain Scintilla text editor with
`ibTextCommandProcessor` (undo/redo) and `ibTextEditorPrintout : wxPrintout`. No language
awareness, no debugger: where `ibCodeEditor` folds by construct and evaluates in the live
runtime ([designer-editors.md § 2](designer-editors.md)), this one edits text. The pattern
repeats across all three: **an editor = a Scintilla/grid base + a `wxCommandProcessor` +
a printout.**

---

## 5. Reading the lineage from the tree

Each inherited body has its own marker — useful when deciding whether a file is ours:

| Layer | Marker | Grep |
|---|---|---|
| Form editor | attribution header | `wxFormBuilder` |
| Database layer | Borland-era relic | `__BORLANDC__` |
| wxGrid fork | upstream header | `Michael Bedward` |
| wxCharts | copyright | `Xavier Leclercq` |
| dataview fork | ours, dated | `paged-bootstrap split` |

Licensing across this folder is **clean**: wxWindows licence (grid, dataview) and MIT-style
(charts). The problem cases are elsewhere — the compression codecs
([serialization-io.md § 4](serialization-io.md)).

---

## 6. Honest remainder

- **`ctrls/charts/` is 246 of the folder's 367 files** — 67% of `frontend/win` is a vendored
  chart library that is never touched. Any "the wx layer is huge" instinct should be checked
  against that.
- The dataview fork is the highest-risk inherited code: upstream's generic control plus a
  grafted paging model, with a private header explicitly scoped to two translation units.
  Upstream merges into it are effectively impossible — treat it as owned.
- `wxWidgets` itself is a **submodule** (`src/3rdparty/wxWidgets`, 3.3.2) — these forks are
  *copies* in-tree, not modifications of the submodule.
