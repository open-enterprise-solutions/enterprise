# Spreadsheet editor — the grid behind templates and reports

> **Scope:** `frontend/win/editor/gridEditor/` (~3800 lines) — the editor that renders and
> edits an `ibBackendSpreadsheetObject`: templates in the Designer, report output at
> runtime. Companions: [report-engine.md](report-engine.md) (the document itself),
> [property-system.md § 8.4](property-system.md) (the cell-selection inspector).
> This document describes code that **already exists**; it is a map, not a plan.

---

## 1. Where it sits

```
ibBackendSpreadsheetObject           backend.dll — the document (GUI-free)
        │  notifier
        ▼
ibGenericSpreadsheetNotifier         frontend — implements ibBackendSpreadsheetNotifier
        │  forwards to
        ▼
ibGridEditor : ibGrid                frontend — the live grid widget
```

`ibGridEditor` derives from `ibGrid` — the forked wx grid, not `wxGrid` directly (see
[uikit.md](uikit.md) for the fork rationale).

The **same** editor serves two jobs, which is why it exists once and not twice:

- **Design time** — editing a template (`ibValueMetaObjectSpreadsheet` /
  `ibValueMetaObjectCommonSpreadsheet`, [report-engine.md § 4](report-engine.md)).
- **Runtime** — displaying a generated report document.

The difference is `EnableEditing` and whether the property inspector is enabled — not a
different class.

---

## 2. The notifier — the one bridge

`ibGenericSpreadsheetNotifier` (nested in `ibGridEditor`) is the concrete
`ibBackendSpreadsheetNotifier` from [report-engine.md § 3](report-engine.md). Every method
is a straight forward into the grid:

```cpp
class ibGenericSpreadsheetNotifier : public ibBackendSpreadsheetNotifier {
    ibGenericSpreadsheetNotifier(ibGridEditor* view) : m_view(view) {}

    virtual void ClearSpreadsheet()          { m_view->ClearGrid(); }
    virtual void EnableEditing(bool edit)    { m_view->EnableEditing(edit); }
    virtual void SetRowSize(int row, int h)  { m_view->SetRowSize(row, h); }
    virtual void SetRowFreeze(int row)       { m_view->FreezeTo(row, 0); }
    virtual void SetColFreeze(int col)       { m_view->FreezeTo(0, col); }

    virtual void SetCellFont(int row, int col, const wxFont& font) {
        GetOrCreateCell(row, col)->SetCellFont(row, col, font, false);
    }
    …
};
```

Two details that repeat across every cell method:

- **`GetOrCreateCell(row, col)`** — cells are materialised on demand. A document is sparse;
  the grid only grows a cell object when something is actually set on it.
- **the trailing `false`** — "do not write back". The notifier is applying what the
  document already decided; re-notifying would loop.

This is what keeps `backend.dll` free of wx GUI code while still driving a real grid: the
document calls `SetCellFont`, the notifier turns it into a widget call, and a headless run
simply registers no notifier ([report-engine.md § 3](report-engine.md)).

---

## 3. The parts

| Class / file | Lines | Role |
|---|---|---|
| `ibGridEditor : ibGrid` | 631 (.h) / 1201 (.cpp) | the editor widget |
| `ibGenericSpreadsheetNotifier` | in .h | document → grid bridge (§2) |
| `ibGridEditorStringTable : ibGridStringTable` | in .h | the cell **data table** + fill types |
| `ibGridEditorCellTextEditor : ibGridCellEditor` | 337 (`gridCellEditor.cpp`) | in-place cell text editing |
| `ibPropertyGridEditorSpreadsheet : ibPropertyObject` | 279 (`gridProperty.cpp`) | selection properties → inspector |
| `ibGridEditorPrintout` | 503 + 91 (`gridPrintout.*`) | printing / print preview |
| `gridEditorDoc.cpp` | 488 | doc/view wiring |
| `gridEditorClipbrd.cpp` | 316 | cut / copy / paste of cell ranges |

---

## 4. Fill type — a cell is not just text

`ibGridEditorStringTable` tracks a per-cell `ibSpreadsheetFillType` alongside the string:

```cpp
struct ibGridEditorStringTableFillType {
    int m_row, m_col;
    ibSpreadsheetFillType m_fillType;
};
```

The fill type is what makes a template a template: a cell is literal **text**, a
**parameter**, or a **template string** with parameters embedded in it. `IsEmptyCell`
branches on it — an "empty" cell means different things for a literal and for an unbound
parameter.

At generation time this is the type `ComputeStringValueFromParameters` dispatches on
([report-engine.md § 3](report-engine.md)).

---

## 5. Selection properties

Selecting cells shows their properties in the object inspector. The mechanism is covered
in [property-system.md § 8.4](property-system.md) — the short version: `ibGridEditor` owns
an `ibPropertyGridEditorSpreadsheet` that **is** an `ibPropertyObject`, binds to
`wxEVT_GRID_SELECT_CELL` / `wxEVT_GRID_RANGE_SELECTED`, and generates its property values
live from the grid for the current `m_selection` (a `wxVector<ibGridBlockCoords>`). Editing
a property writes back across every selected block.

The selection's `Name` property is its address — `R1C1` for a single cell, `R1C1:R2C3` for
a block — which is also how named areas are addressed by
`GetAreaByName` ([report-engine.md § 3](report-engine.md)).

---

## 6. Honest remainder

- **Cell borders are not applied.** All four border methods on the notifier have **empty
  bodies**:

  ```cpp
  virtual void SetCellBorderLeft  (int row, int col, const ibSpreadsheetBorderDescription& desc) {}
  virtual void SetCellBorderRight (int row, int col, const ibSpreadsheetBorderDescription& desc) {}
  virtual void SetCellBorderTop   (int row, int col, const ibSpreadsheetBorderDescription& desc) {}
  virtual void SetCellBorderBottom(int row, int col, const ibSpreadsheetBorderDescription& desc) {}
  ```

  The document stores borders (`ibSpreadsheetBorderDescription` round-trips through
  `ibSpreadsheetDescription`) and the printout path can consume them, but **the on-screen
  grid does not draw them**. Anything relying on borders being visible while editing is
  relying on something that is not implemented here.
- The editor is the wx **Grid** fork; the runtime form control is **Gridbox**. Both consume
  the same `ibBackendSpreadsheetObject`, and the web front is intended to consume the same
  binary through a JS spreadsheet — see
  [report-engine.md](report-engine.md) and [ROADMAP.md § 4.2](ROADMAP.md).
