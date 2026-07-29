# File & folder naming plan

> **Status: PARTIALLY APPLIED** (re-verified 2026-07-29). **Wave 1 is done** — all seven empty
> `advprop*.h` headers are deleted (no zero-byte headers remain in the tree). The `tableInfo*` /
> `tableView*` renames of §6.2/§7 are also done, under different target names: those files are now
> `backend/model.{h,cpp}` / `modelDb.cpp` / `modelRam.cpp` / `modelView.{h,cpp}`, which also
> dissolves the `backend/tableView.h ↔ frontend/win/ctrls/tableView.h` collision §5.3 warns about.
> Waves 2-4 are outstanding. ⚠️ The occurrence counts below were measured 2026-07-15 and have
> drifted — the `backend/` root is now 99 files / 63 modules, not 94 / 59.
> Scope: **file and folder names only** — naming that follows what the code *does*, rather
> than how it happened to grow. Class/member naming and code grouping are a separate plan.
> Every cost figure below was measured with grep on 2026-07-15.

---

## 1. Principle

**A name should answer "what lives here", and be unique enough that a reader — or an
`#include` — cannot pick the wrong one.** Three failure modes, in order of severity:

1. **A name that lies** (`fileSystem/` is not a file system) — costs comprehension every
   time someone reads it.
2. **A name that collides** (two `mainFrameDesigner.h`) — costs correctness; the wrong file
   can be included.
3. **A name that repeats its container** (`backend/backend_core.h`) — costs nothing but
   noise, and there are 10 of them.

---

## 2. What is NOT renamed, and why

Decide these once so they stop coming up:

| Thing | Why it stays |
|---|---|
| **CLSID keys** (`"VL_ARR"`, `"EN_ITMO"`, `"PC_CLOSE"`) | the string **is** the clsid body; changing it changes the id and breaks stored data ([script-value-types.md § 1.1](script-value-types.md)) |
| **Script type names** (`"Array"`, `"IndexingMode"`, even `externalManagerDataProcessor`) | user-facing API **and** registry keys — a rename is a breaking change for every configuration |
| `3rdparty/wxWidgets` | submodule |
| `ctrls/charts/` (wxCharts) | vendored upstream ([wx-fork.md § 3](wx-fork.md)) |
| `databaseLayer/firebird/engine/` (incl. the duplicate `iberror.h`) | vendored Firebird |
| `resource.h`, `mainApp.h` per executable | one per app by design — the duplication is correct |
| `__BORLANDC__` / `wxprec.h` relics | dead code, **but** currently the only marker of what came from wxDatabaseLayer ([database-layer.md § 1.1](database-layer.md)). Record the lineage before removing |

---

## 3. Wave 1 — free wins (no include churn)

| Action | Cost | Risk |
|---|---|---|
| **Delete 7 empty headers** — `advpropBoolean.h`, `advpropColour.h`, `advpropDate.h`, `advpropEnum.h`, `advpropForm.h`, `advpropModule.h`, `advpropSpreadsheet.h` (0 bytes, still `#include`d by their own `.cpp`) | 7 files + 7 include lines | none |

Do this first: it is pure subtraction, and it shrinks `advprop/` by a quarter of its
entries.

---

## 4. Wave 2 — folders whose names lie

### 4.1 `backend/fileSystem/` → `backend/io/`

It is **not** a file-system abstraction — it is the byte stream (`ibWriter` / `ibReader` /
chunks / compression) that metadata, copy/paste and the AOT cache ride on
([serialization-io.md](serialization-io.md)). Script-level file work is the `File` value
type and the file built-ins, which live elsewhere.

- **Cost:** 32 files include `fileSystem/fs.h`; + `.vcxproj`, `.filters`, `CMakeLists.txt`.
- **Risk:** low — mechanical, and the compiler catches every miss.
- **Note:** `io/` is a 2-letter name; if that reads too generic, `stream/` is the
  alternative. Prefer `io/` — it is what the two classes are.

### 4.2 `backend/databaseLayer/sqllite/` → `sqlite/`

A typo that became a directory name, an include path and a project filter.

- **Cost:** only **3** code files reference it, plus `backend.vcxproj`,
  `backend.vcxproj.filters`, `CMakeLists.txt`.
- **Risk:** low. Cheapest correctness-of-naming fix in the tree.

---

## 5. Wave 3 — collisions (correctness, not taste)

### 5.1 ⚠ `mainFrameDesigner.h` ×2 — the dangerous one

```
designer/mainFrameDesigner.h            133 lines — class wxAuiDocDesignerMDIFrame : wxAuiDocMDIFrame
designer/mainFrame/mainFrameDesigner.h  186 lines — class ibFrontendMainFrameDesigner : ibFrontendMainFrame
```

**Two different classes, two different layers, one filename, one module.** The root one is
the doc/view MDI host; the nested one is the application window
([main-frame.md](main-frame.md)). Which one an `#include "mainFrameDesigner.h"` resolves to
depends on include-path order.

The same split repeats for the companions:

```
designer/mainFrameDesignerEvent.cpp   ↔  designer/mainFrame/mainFrameDesignerEvent.cpp
designer/mainFrameDesignerMenu.cpp    ↔  designer/mainFrame/mainFrameDesignerMenu.cpp
```

**Proposal:** the root trio is the *doc/view frame*, not the designer window — rename to
match what it is:

| From | To |
|---|---|
| `designer/mainFrameDesigner.h` | `designer/docFrameDesigner.h` |
| `designer/mainFrameDesignerCmd.cpp` | `designer/docFrameDesignerCmd.cpp` |
| `designer/mainFrameDesignerEvent.cpp` | `designer/docFrameDesignerEvent.cpp` |
| `designer/mainFrameDesignerMenu.cpp` | `designer/docFrameDesignerMenu.cpp` |

leaving `designer/mainFrame/mainFrameDesigner.*` as the only "main frame".

- **Risk:** medium — must confirm which file each existing include meant. Do it alone, in
  its own commit.

### 5.2 `uikit` vs `visualView` — `frame.h` / `control.h` / `window.h`

```
frontend/uikit/frame.h            ↔  frontend/visualView/ctrl/frame.h
frontend/uikit/ctrl/control.h     ↔  frontend/visualView/ctrl/control.h
frontend/uikit/window.h           ↔  frontend/visualView/ctrl/window.h
```

Two parallel hierarchies using the same three words. These are *legitimately* different
things — uikit is the custom-drawn engine ([uikit.md](uikit.md)), visualView is the form
control tree — and both are always included with a path prefix.

**Proposal: leave them.** The collision is namespaced by directory and the names are right
in their own context. Renaming would make both worse. Listed here so the question is
settled, not reopened.

### 5.3 `tableView.h` / `picturePredefined.h` ×2

```
backend/tableView.h  ↔  frontend/win/ctrls/tableView.h        (model vs widget)
backend/picturePredefined.h  ↔  frontend/artProvider/private/picturePredefined.h
```

Backend/frontend split makes the intent clear at the include site. **Leave**, unless the
backend one is renamed as part of Wave 4 anyway (`tableView` → the model it actually is).

---

## 6. Wave 4 — the `backend/` root is a drawer (59 modules, 94 files)

This is the largest naming problem and the one worth arguing about. `backend/` has 19
subfolders **and** 59 loose modules at its root, mixing types, utilities, factories,
metadata and domain code.

### 6.1 Drop the `backend_` prefix inside `backend/`

Ten modules repeat their own container:

```
backend_core  backend_exception  backend_form  backend_localization  backend_mainFrame
backend_metatree  backend_picture  backend_spreadsheet  backend_type  backend  (itself)
```

Every one is `#include "backend/backend_core.h"` — the word twice in one path.

- **Cost:** `backend_core.h` alone is included by **45** files; the rest are 5–20 each.
- **Recommendation:** fold this into 6.2 rather than doing it as a pure prefix strip — a
  rename is worth doing once, to a final location.

### 6.2 Proposed grouping of the root

Names below describe **what the module is**, and each group is a folder that already has an
obvious meaning:

| New folder | Takes | Rationale |
|---|---|---|
| `backend/core/` | `backend`, `backend_core`, `backend_exception`, `backend_localization`, `backend_type`, `clsid`, `valueInfo` | the prelude every TU pulls |
| `backend/types/` | `fnumber`, `fstring`, `guid`, `uniqueKey`, `typeconv`, `value_cast`, `value_ptr`, `stringUtils`, `fontcontainer`, `rowValues` | primitives and helpers, no domain |
| `backend/describe/` | `typeDescription`, `sourceDescription`, `spreadsheetDescription`, `pictureDescription`, `picturePredefined`, `actionInfo` | the description pattern ([descriptions.md](descriptions.md)) — today scattered across the root |
| `backend/factory/` | `ctorRegistry`, `metaCtor`, `objCtor`, `objCtorDefs`, `characteristicCtor`, `constantCtor`, `metadataFactory`, `metadataDataProcessorFactory`, `metadataReportFactory` | the registries ([factories.md](factories.md)) |
| `backend/app/` | `appData`, `appDataCtorToken`, `appDataQuery`, `appEnv`, `userInfo`, `roleHelper` | process/application state |
| `backend/source/` | `srcObject`, `srcDataObject`, `tabularDataObject`, `tableInfo`, `tableInfoDb`, `tableInfoRam`, `tableView` | what a form binds to ([source-object.md](source-object.md)) |
| `backend/metadata/` | `metaData`, `metadataConfiguration`, `metadataConfigurationQuery`, `metadataDataProcessor`, `metadataReport`, `metadataSerialization`, `restructureInfo`, `moduleInfo`, `interfaceHelper` | the metadata layer proper |
| stays at root | `backend.h` (the public umbrella), `backend_form.h`, `backend_mainFrame.h`, `backend_metatree.h`, `backend_picture.h`, `backend_spreadsheet.h` | **the frontend-facing contracts** — see below |

**The one idea worth keeping from `backend_*`:** those six files are *contracts the
frontend implements* ([main-frame.md § 1](main-frame.md),
[metadata-tree.md § 1](metadata-tree.md), [report-engine.md § 3](report-engine.md)). That
is a real category, and the prefix — accidentally — marks it. Better: move them to
`backend/contract/` and drop the prefix:

```
backend/contract/form.h        (was backend_form.h)
backend/contract/mainFrame.h   (was backend_mainFrame.h)
backend/contract/metaTree.h    (was backend_metatree.h)     ← also fixes "metatree" casing
backend/contract/picture.h     (was backend_picture.h)
backend/contract/spreadsheet.h (was backend_spreadsheet.h)
```

Then `#include "backend/contract/mainFrame.h"` *says* "I am using the backend's contract".

### 6.3 Spelling fixes to fold in

| From | To | Note |
|---|---|---|
| `backend_metatree.h` | `contract/metaTree.h` | `metatree` → `metaTree`, matching `metaData` |
| `srcObject` / `srcDataObject` | `sourceObject` / `sourceDataObject` | the code says `Source` everywhere; only the filename abbreviates |

- **Cost:** `srcDataObject.h` = 19 includes; `tableInfo.h` = 21; `typeDescription.h` = 9.

---

## 7. Audit — filename vs. what the file declares

Machine-checked on 2026-07-15: every header's name compared, word-set against word-set,
with the classes it declares (abbreviations like `src`/`app`/`db` treated as matches, so
only real divergence is listed). Ordered by how much the name misleads.

### 7.1 Names that say nothing — the "Info / Defs / Types / Helper" bag

The worst category, because the name is a **placeholder**: it tells a reader there is
*stuff* inside.

| File | Actually declares |
|---|---|
| `backend/valueInfo.h` | `ibReference`, `ibValueDataObject` |
| `backend/moduleInfo.h` | `ibRuntimeRoot`, `ibRuntimeModuleDataObject` |
| `backend/standardCommand.h` | `ibStandardCommandDescription`, `ibCommandItem`, `ibStandardCommandSet` … (6) |
| `backend/tableInfo.h` | `ibValueModel`, `ibDataViewModelProvider`, `ibVariantDataValue` … (**20**) |
| `backend/roleHelper.h` | `ibRole`, `ibRoleUserInfo`, `ibAccessObject` |
| `backend/interfaceHelper.h` | `ibInterfaceObject` (+ the section enums) |
| `backend/debugger/debugDefs.h` | `ibDebugData`, `ibStackData`, `ibWatchWindowData` … (**12**) |
| `backend/lock/lockTypes.h` | `ibLockItem`, `ibLockOptions` |

Suggested direction (needs a read each, so these are proposals, not decisions):
`valueInfo.h` → `reference.h`; `moduleInfo.h` → `runtimeModule.h`;
`roleHelper.h` → `role.h`; `interfaceHelper.h` → `interfaceObject.h`;
`lockTypes.h` → `lockItem.h`; `standardCommand.h` → `action.h`.

### 7.2 Names that describe the wrong thing

| File | Declares | Note |
|---|---|---|
| `backend/fileSystem/fs.h` | `ibWriter`, `ibReader`, `ibWriterMemory`, `ibReaderMemory` | folder **and** file both lie (§4.1) |
| `backend/system/value/valueMap.h` | `ibValueContainer`, `ibValueStructure`, `ibValueReturnContainer` | there is no "Map" — the script types are `Container` / `Structure` / `KeyValue` |
| `backend/propertyManager/property/variant/variantType.h` | `ibVariantDataAttribute` | "Type" vs. an attribute variant |
| `backend/tableView.h` | `ibDataViewObject`, `ibDataViewItem` … (9) | it is the **data-view** model, not a "table view" |
| `backend/query/querySelector.h` | `ibSelector` | the `query` prefix is the folder, repeated |
| `backend/logger/loggerEntry.h` | `ibLogEntry` | `logger`≠`log` |
| `backend/compiler/procContext.h` | `ibRunContext`, `ibRunContextSmall` | proc vs run |
| `backend/compiler/procUnitValues.h` | `ibValueIterator`, `ibValueFunction` | the two script values that live in the interpreter |
| `backend/compiler/enumUnit.h` | `ibValueEnumeration*` | "Unit" is from the `procUnit` era |
| `backend/system/systemManager.h` | `ibValueSystemFunction` | the built-ins ([system-functions.md](system-functions.md)) |
| `backend/databaseLayer/firebird/firebirdHlc.h` | `ibHlcClock` | fine, listed for completeness |

### 7.3 The `f` prefix — an artefact

| File | Declares |
|---|---|
| `backend/fnumber.h` | `ibNumber` |
| `backend/fstring.h` | `ibString`, `ibStringAllocator` |

`f` meant nothing to a reader; the classes dropped it long ago. → `number.h`, `string.h`
(inside `backend/types/`, §6.2, so no collision with the standard header).

### 7.4 Bags — one header, many classes

Not a naming issue per se, but it is *why* the names are vague — a file with 49 classes
cannot have an accurate name.

| File | Classes |
|---|---|
| `backend/metaCollection/partial/commonObject.h` | **49** |
| `backend/tableInfo.h` | 20 |
| `backend/objCtor.h` | 14 |
| `backend/debugger/debugDefs.h` | 12 |
| `backend/tableView.h` | 9 |

**Splitting these is a restructuring act, not a rename** — out of scope here, but
`commonObject.h` at 49 classes is the single biggest structural item in the tree and the
reason `commonObject` means nothing.

### 7.5 Verified clean

The driver folders (`firebird*`, `mysql*`, `postgres*`, `odbc*`, `sqlite*`) look mismatched
to a naive check but are **correct**: `firebirdDatabaseLayer.h` → `ibDatabaseLayerFirebird`
is the same words in a different order. No action.

---

## 8. Order, cost, verification

| Wave | What | Files touched | Risk |
|---|---|---|---|
| 1 | delete 7 empty headers | ~14 | none |
| 2 | `sqllite` → `sqlite` | ~6 | low |
| 2 | `fileSystem/` → `io/` | ~35 | low |
| 3 | `designer/mainFrameDesigner*` → `docFrameDesigner*` | ~10 | **medium** |
| 4 | `backend/` root regrouping | **~200+** | **high** (churn, not danger) |

**Rules for executing this:**

- **One wave per commit**, never mixed with behaviour changes — a rename commit must be
  reviewable as "only paths moved".
- Use `git mv` so history follows the file.
- Update the three build inputs every time: `*.vcxproj`, `*.vcxproj.filters`,
  `CMakeLists.txt`. A file renamed in the tree but not in the project builds locally and
  breaks CI.
- **Verification is a full rebuild**, both toolchains if possible. Nothing here is
  behavioural, so a green build is a sufficient check.
- Wave 4 will conflict with any in-flight branch that touches `backend/`. Land it when the
  tree is quiet, or skip it — it is the only wave that is *taste* rather than *defect*.

**If only one thing is done:** Wave 1 + `sqllite` + `fileSystem/` → `io/`. That is ~55
files, no danger, and removes the two names that actively mislead.
