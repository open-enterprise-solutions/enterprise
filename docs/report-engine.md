# Report engine — runtime shape

> **Scope:** how a Report metaobject turns into a rendered spreadsheet at runtime.
> Companion docs: [data-composer.md](data-composer.md) (L5 — where a report's *data*
> comes from), [register-totals-strategy.md](register-totals-strategy.md).
> This document describes code that **already exists**; it is a map, not a plan.

---

## 1. The two halves

A report is two independent things that meet only at the end:

| Half | Owns | Lives in |
|---|---|---|
| **Data** | query / composition → rows | L3 door + L5 composer (`data-composer.md`) |
| **Presentation** | a spreadsheet document — cells, areas, formatting | `ibBackendSpreadsheetObject` |

Nothing in the spreadsheet half knows about queries; nothing in the data half knows
about cells. A script in the report's object module is what joins them.

---

## 2. Metaobject — `ibValueMetaObjectReport`

`backend/metaCollection/partial/dataReport.h`

- Extends `ibValueMetaObjectRecordDataExt` — a report is a **record-data object**, so it
  carries attributes and tabular sections like a Catalog/Document does. Those are the
  report's *parameters* surface, not stored table data.
- One form kind: `eFormReport` (`"FormReport"`), exposed via `GetFormType()`. The default
  form is chosen by the `DefaultFormObject` property.
- Two modules: `ObjectModule` (per-report logic) and `ManagerModule`.
- `GetCommandSection()` returns `ibInterfaceCommandSection_Report` — this is the single
  line that files every report under the **Report** section of the command interface
  (see [command-interface.md](command-interface.md)).

### External reports

`ibValueMetaObjectExternalReport` (`m_metaId = 10`) overrides `IsExternalCreate()` and is
loaded from a file rather than the configuration. Its value object,
`ibValueRecordDataObjectExternalReport`, mixes in `ibExternalOwnerHelper`, which owns the
transient external metadata container and drops it in its destructor — the RAII reason the
external variant is a separate class at all. Embedded reports use the plain
`ibValueRecordDataObjectReport`.

---

## 3. Spreadsheet document — `ibBackendSpreadsheetObject`

`backend/backend_spreadsheet.h`. A `wxRefCounter`, held through `wxObjectDataPtr`.

The document is **GUI-free** and lives in `backend.dll` (decision #4 in
[../CLAUDE.md](../CLAUDE.md)). All cell state lives in one `ibSpreadsheetDescription`
member (`spreadsheetDescription.h`); the object is a façade with behaviour over it.

### Notifier — how the UI ever sees it

`ibBackendSpreadsheetNotifier` is a pure-virtual sink. Every mutator on the document
(`SetCellValue`, `SetCellFont`, `SetRowFreeze`, `PutArea`, …) has a mirror on the notifier.
Views attach via the template `AddNotifier<T>(args…)` and detach with `RemoveNotifier`.

This is the seam that keeps the backend GUI-free: the frontend registers a notifier that
paints into a Gridbox, the web front registers a different one, and a headless run
(daemon / codeRunner) registers none at all and still produces a correct document.

`RowAreaAdded` / `ColAreaAdded` have empty default bodies **on purpose** — they were added
with the outline-group API and default to no-op so existing notifiers keep compiling.

### Areas — the template mechanism

A report is composed by copying rectangles out of a *template* and appending them to a
*result*:

```
ibSpreadsheetDescription GetArea(rowLeft, rowRight, colTop = -1, colBottom = -1);
ibSpreadsheetDescription GetAreaByName(strAreaLeftName, strAreaTopName = "");

void PutArea (const wxObjectDataPtr<ibBackendSpreadsheetObject>& doc, groupLevel = 0);
void JoinArea(const wxObjectDataPtr<ibBackendSpreadsheetObject>& doc, groupLevel = 0);
```

`PutArea` appends below; `JoinArea` appends to the right. `GetAreaByName` is the
named-area lookup a script uses (`GetAreaByName("Header")`), so a template edit does not
break the script as long as the name survives.

### Parameters

Cells are filled by name, not by coordinate:

```
void   SetParameter(const wxString& name, const ibValue& value = ibValue());
bool   GetParameter(const wxString& name, ibValue& out) const;
wxString ComputeStringValueFromParameters(const wxString& value, ibSpreadsheetFillType type) const;
```

A cell's fill type (`ibSpreadsheetFillType`) decides whether its text is a literal, a
parameter, or a template string with embedded parameters —
`ComputeStringValueFromParameters` is what resolves the last case.

### Outline grouping

`BeginRowGroup()` / `EndRowGroup()` (and the column pair) nest freely; `End*` pops the
stack and turns the range between the matching `Begin*` and the current row count into an
outline group at depth = stack size + 1. The stacks are plain `std::vector<int>` members.

### Drill-down ("details")

`SetCellDetailsParameter(row, col, s)` / `GetCellDetailsParameter` attach a decoding
payload to a cell, and `OpenCellDetailsParameter(row, col)` acts on it. This is the
platform's cell-level drill-down hook.

### Persistence and printing

`LoadFromFile` / `SaveToFile` round-trip a document. `m_docPrinterName`, the row/col
**brake** API (`AddRowBrake` / `SetColBrake` / `GetMaxRowBrake`) and freeze panes
(`SetRowFreeze` / `SetColFreeze`) exist for the print/geometry path.

---

## 4. Templates — `ibValueMetaObjectSpreadsheetBase`

`backend/metaCollection/metaSpreadsheetObject.h`

An abstract base with two concrete kinds, differing **only** in which property category
holds the data:

| Class | Category | Meaning |
|---|---|---|
| `ibValueMetaObjectSpreadsheet` | `Template` | template owned by one metaobject |
| `ibValueMetaObjectCommonSpreadsheet` | `CommonTemplate` | configuration-wide shared template |

Both store an `ibSpreadsheetDescription` behind an `ibPropertySpreadsheet` and expose it
through `Get/SetSpreadsheetDesc`. That is the entire difference — the duplication is a
candidate for the restructuring plan, not a design statement.

---

## 5. Honest remainder

- ~~**No platform-level report action.**~~ **Landed 2026-08-02.** `dataReportAction.cpp`
  now carries one standard command, **Compose** (`g_picGenerateCLSID`, `SetModify(false)`
  so it stays live on a view-only form). `CallAsAction` routes it to
  `ibValueRecordDataObjectReport::Composing()`, which calls the object module's
  `Composing(StandartProcessing)` handler — the report's twin of a document's
  `Post` → `Posting`.

  **Compose, not "generate".** In this tree *Generation* already means entering one object
  ON THE BASIS of another (`ibValueRecordDataObjectRef::Generate`, the document's Generate
  command). A report generates nothing; it COMPOSES a result — the word the rest of the
  stack already uses ([data-composer.md](data-composer.md), `ibQueryComposer`).

  The handler is a **declared default procedure** — `ibValueMetaObjectReport`'s ctor calls
  `SetDefaultProcedure("Composing", eProcedureHelper, { "StandartProcessing" })`, the same
  way a document declares `Posting` — so the designer offers it in the object module's
  handler list rather than the developer having to know the name.

  **The argument is `StandartProcessing`, not a cancel flag**, following the
  `Filling` / `SetNewCode` / `SetNewNumber` contract:

  | Flag after the handler | Who composes |
  |---|---|
  | left **TRUE** (including: no handler written) | the PLATFORM — `DoStandardCompose()` |
  | set **FALSE** | the SCRIPT already did; the platform stands down |

  `DoStandardCompose()` returns false today, and the reason is structural rather than
  unfinished plumbing: a standard composition needs a declared **composition schema** on
  the metaobject — what to read and how to lay it out — and the Report metaobject has no
  such property (it carries modules, forms, a default form). Until that exists there is
  nothing for the platform to compose *from*, and inventing a second private notion of a
  report's data here would be worse than saying so. **A report written today therefore sets
  `StandartProcessing = False` and composes in its handler** — data through the query /
  composer layer, presentation through the spreadsheet's areas (§1) — which is exactly how
  reports already worked before the command existed. When the schema lands, its execution
  goes into `DoStandardCompose` (virtual, so a report kind can own its own) and every
  existing report keeps working unchanged.
- **Export is two branches, one document.** Geometry (printout → PDF) and semantics
  (walk → Excel / Word) are separate consumers of the same `ibBackendSpreadsheetObject`;
  Excel is deliberately *not* produced through the printout path. Cell typing is the
  known risk in the semantic branch.
- `IsCellReadOnly(int row, int col, bool isReadOnly = true)` takes an `isReadOnly`
  argument it never uses — a getter carrying a setter's signature. Harmless, and a
  cleanup candidate.
