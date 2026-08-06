# Common attributes — one declaration, many objects

> **Status:** built 2026-08-06, not yet exercised by a test. The metatype, the composition
> mechanism, the editor and the propagation are in place; data separation (the second half
> this was designed to make possible) is NOT.
> Companions: [session-parameters.md](session-parameters.md), [access-policy-rls.md](access-policy-rls.md),
> [metadata-lifecycle.md](metadata-lifecycle.md), [designer-editors.md](designer-editors.md).

---

## 1. What it is

A **common attribute** is declared once, under Common, and then exists as a real attribute
inside every object checked into its composition. Not a virtual column, not something
resolved at read time: by the time a table is built it is an ordinary attribute with its
own metaID, its own physical column (`fld<metaId>`) and its place in restructuring.

```
Common
 └─ Common attributes
     └─ Organisation          ← the DECLARATION (type, qualifiers, editor)
Catalogs
 └─ Contractors
     └─ Attributes
         └─ Organisation      ← the COPY: a real child, not editable here
Documents
 └─ Invoice
     └─ Attributes
         └─ Organisation      ← another copy, its own metaID and column
```

## 2. Why an attribute and not a new kind of thing

Same reason a session parameter is one: an attribute already **is** "a declared name with a
type, its qualifiers and an editor". Deriving it means the type description, the property
page, serialisation and `AdjustValue` arrive finished.

What differs is ownership. A catalog's attribute is a column of that catalog. This one
belongs to the configuration and appears in a chosen subset of objects — and the subset is
the interesting part, because it is what the person editing decides.

## 3. Two classes, two ids

| Class | clsid | What it is |
|---|---|---|
| `ibValueMetaObjectCommonAttribute` | `MD_CATT` | the DECLARATION, under Common |
| `ibValueMetaObjectCommonAttributeColumn` | `MD_CATC` | the COPY inside one object |

Both live in `metaCollection/metaCommonAttributeObject.{h,cpp}`; the context menus are split
out to `…ObjectMenu.cpp` as every other metatype does.

**What the copy delegates, and what it stores** — the split is not a preference:

- **type → delegated.** `GetTypeDesc()` returns the declaration's, so a type change is
  correct the instant it is made; there was never a second answer to update.
- **name → stored.** `ibValueMetaObject::GetName` is **not virtual** — the designer tree and
  the property grid read the stored value directly — so an override would be seen by almost
  nobody. The copy therefore holds its name, written when it is created and rewritten by the
  declaration's `OnRenameMetaObject`. This is the one propagation the design pays for.
- **identity → its own.** Its metaID is issued inside its owner, which is what gives it a
  column of its own and takes it through restructuring with that owner.

## 4. Composition — the membership mechanism

`backend/compositionHelper.{h,cpp}` — `ibCompositionObject`, mixed into every metaobject
beside `ibAccessObject` (roles) and `ibInterfaceObject` (sections):

```cpp
void SetComposition(const ibMetaID& id, const bool& set = true);
bool IsInComposition(const ibMetaID& id) const;
virtual bool IsCompositionAllowed() const { return false; }
```

It is a **copy of the interface mechanism in shape and deliberately separate in identity.**
`ibInterfaceObject` belongs to sections — it is their older name and carries their
vocabulary (`ibInterfaceCommandSection`, the panel areas). Putting a second kind of
membership into the same set would leave one container holding two meanings with nothing
able to tell them apart. So: its own set, its own chunk on disk (`0x200021`, beside the
interface block, never the same one), its own `LoadComposition` / `SaveComposition` called
from the common header in `metaObjectSerialize.cpp` next to interface and roles.

**Two halves, one operation.** Checking a box records membership on the object AND creates
the copy inside it; unchecking undoes both. `SetCompositionObject` is the only door, which is
what keeps the flag and the column from disagreeing.

**Who may be checked in** is asked, never listed: `IsCompositionAllowed()`, answered `true`
on the line a catalog and a document share (`ibValueMetaObjectRecordDataMutableRef`) — which
covers charts of accounts and characteristic types too. Registers and the unstored kinds say
no: for a register it is a decision about data separation that has not been taken, and a
processor or report has no table to put a column in.

## 5. The copy cannot be edited where it sits

Two layers, because a guarantee that holds only in the UI is not a guarantee:

| Layer | What it does |
|---|---|
| menu | `PrepareContextMenu` returns **true** — the standard New / Edit / Remove block is not appended; only "Go to common attribute" |
| metaobject | `OnBeforeCloseMetaObject` refuses unless the removal comes from the declaration (`AllowRemoval`) or the owner is already marked deleted — a catalog takes its columns with it |

The second matters because delete is reachable from the toolbar and the keyboard, not only
from the menu; all of them go through `RemoveMetaObject` → `OnBeforeCloseMetaObject`.

## 6. What a change costs

| Change | What happens |
|---|---|
| **rename** the declaration | `OnRenameMetaObject` walks its copies and renames them |
| **retype** it | nothing to propagate — copies ask for the type. But every carrier is told to re-read (`OnReloadMetaObject`), so open forms, completion and tree rows stop showing the old answer |
| **uncheck** an object | copy removed, membership cleared |
| **delete** the declaration | every copy removed, every carrier's membership cleared |
| **delete a carrying object** | its copy goes with it as an ordinary child; nothing to notify |
| **copy/paste an object** | both halves travel: the composition set rides the common header, the copy rides as a child |

## 7. The editor

`designer/win/editor/commonAttributeEditor/` + `docManager/templates/docViewCommonAttribute.{h,cpp}`.
Double-clicking a declaration opens its composition: a checkable tree of the objects that may
carry it.

Its tree is built by **walking the metadata and grouping by metatype**, with the group's
caption and icon taken from the type registry — not from a written-out list of branches. A
metatype that starts allowing common attributes appears on its own. This is the shape the
role and section editors were converted to as well ([designer-editors.md](designer-editors.md)).

## 8. Where it lives

| File | What |
|---|---|
| `backend/compositionHelper.{h,cpp}` | the membership mechanism |
| `backend/metaCollection/metaCommonAttributeObject.{h,cpp}` | declaration + copy |
| `backend/metaCollection/metaCommonAttributeObjectMenu.cpp` | both context menus |
| `backend/metaCollection/metaCommonAttributeObject_res.cpp` | the icon |
| `designer/win/editor/commonAttributeEditor/` | the composition editor |
| `designer/docManager/templates/docViewCommonAttribute.{h,cpp}` | its document/view pair |

## 9. Not built

- **Numbering within a value.** Codes and document numbers are unique across the whole base;
  with several organisations in one base each usually numbers from one. That would be one
  more component in the sequence key, and it does not exist yet.

  (Not to be confused with `interval` in `GenerateNextIdentifier`: that is the numbering
  PERIOD's granularity, written as a number — which digits of a date the counter resets on.
  A different question entirely, and nothing to do with common attributes.)

  Note what is NOT missing here. A separate "data separator" type — a condition forced into
  every query, impossible to bypass — would be a second currency for something this platform
  already has: a `Restrict` policy over this column against a session parameter arrives at
  the source as a decorator, so it cannot be written around either
  ([access-policy-rls.md](access-policy-rls.md)). Common attribute + session parameter + RLS
  is the whole of data separation; a separator metatype would only duplicate it.
- **Orphan cleanup.** Pasting a carrying object into a *different* configuration leaves a copy
  whose declaration id resolves to nothing; it answers an empty type and contributes no
  column, but nobody removes it.
- **Tests.** `tests/test_commonAttribute.cpp` covers the mechanism (creation, no duplicate
  copy, check-out, delegated type, presence in the object's attribute list, liveness tied
  to the declaration, and the composition set kept apart from the section one) — written,
  registered in `tests/CMakeLists.txt`, **not yet run locally**; CI is the first pass.
- **The lists.** Every defect in this arc was a metatype missing from a list kept by hand.
  The general answer — one table of metatypes × the same questions — is the next step in
  [ROADMAP.md](ROADMAP.md).
