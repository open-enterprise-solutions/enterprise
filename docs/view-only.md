# View-only mode — read-only forms

> **Scope:** how a form opens (or is switched) into *view-only* — every control shows its
> value but nothing can be edited, and data-modifying commands grey out. This describes code
> that **already exists**; it is a map, not a plan.

---

## 1. What view-only is (and is not)

View-only is **read-only-VALUE**, not availability. A control in view-only is live: it takes
focus, shows its value, the value can be selected / copied / opened, a table row can be
picked, filters / sort / view-mode work — you just cannot **change** the value. That is the
sharp line from `Enable(false)` (a greyed, dead widget), which stays reserved for action
**buttons** (a Save button that can't be pressed), not data controls.

It is also distinct from `IsEditable()` — that is the DESIGNER question ("can the form's
STRUCTURE be changed", true for a config loaded read-only from a DB/file). View-only is the
RUNTIME question ("can the user change the DATA").

---

## 2. The state — `ibValueForm::IsViewOnly()` (matryoshka)

`frontend/visualView/ctrl/form.{h,cpp}`

`IsViewOnly()` is a runtime state (not a persisted property), resolved as an **outer shell +
inner per-source** matryoshka:

- **Explicit** — `SetViewOnly(true)` / the runtime member `ThisForm.ReadOnly = True` force the
  whole form read-only.
- **Outer shell, two gates** — the FORM metaobject (the one that spawned the form; may be
  absent) then the MAIN source's metaobject. Either denying `AccessRight_Modify` makes the
  whole form view-only.
- **Inner per-source** — `IsWritableBinding(desc)` (the SINGLE gate every editable control
  asks, `formAttribute.cpp`) also checks the source held by *that binding's* attribute. A
  writable form can still carry a **read-only object**: a read-only role on that object denies
  its `AccessRight_Modify`, so ITS controls are read-only while the rest of the form edits.

The right comes from the **metaobject**, read polymorphically off `GetSourceMetaObject()` — a
dynamic list forwards it through its queryable, so a list over a read-only object greys only
its data-modifying commands too.

---

## 3. The right — `AccessRight_Modify` (generic, twin of `AccessRight_Show`)

`backend/metaCollection/genericData.h` + `commonObject.h` + `metaFormObject.h`

`AccessRight_Modify()` is the GENERIC "can change this" predicate, the write-side twin of the
existing `AccessRight_Show()` ("can view" → a common form maps to `Use`, a catalog to `Read`).
Each metaobject maps Modify to its own concrete right:

- base `ibValueMetaObjectGenericData` → `true` (a data processor / report has no modify concept
  → never view-only-gated);
- a record / register / constant → its `Write` role (a read-only role denies it);
- a form metaobject → its `Use` right.

**The constant reached this list late, and the way it failed is worth keeping (2026-07-29).** It was
an ATTRIBUTE, not a `GenericData`, so it could not answer `AccessRight_Modify` at all — and instead
of that showing up as a compile error, the object half was produced by casting the constant into an
unrelated class. The call landed in a foreign vtable and returned whatever was in that slot, so a
constant opened READ-ONLY for a user with full rights. Two properties of the failure are the lesson:
it looked like a rights problem while no right was ever consulted, and the form had no second gate
to fall back on (a constant has no form metaobject, so the first gate is always empty). The constant
is now a `GenericData` that maps Modify onto its own `Write` role, with the value living in a nested
column — see `query-language-arc.md §21.6`.

Virtual on the base so a form reads it without knowing the concrete type. The backend write
path already gates on the SAME `Write` role, so UI view-only and the write-time
`ibBackendAccessException` agree.

---

## 4. Controls — read-only, not disabled

The control reads writability at build time and renders classic read-only. `ibValueFrame`
carries `IsReadOnly()` (= its owner form's `IsViewOnly()`); a data control resolves per-binding
through `form->IsWritableBinding(sourceDesc)`.

- **Text box** (`ibControlTextEditor`) — `SetTextEditMode(false)` IS the read-only policy: the
  inner text goes inert AND the value-changing side buttons (**Select / Clear**) grey out;
  **Open** (read navigation) stays live. One flag, no separate read-only setter. Web mirror in
  `ibWebTextCtrl`.
- **Check box** (`ibControlCheckbox`) — `SetReadOnly(true)`: a click still toggles the native
  box, so the control's own `wxEVT_CHECKBOX` guard reverts it and swallows the event (no source
  write, no OnChange). Not `Enable(false)` — the box stays crisp. Web mirror in `ibWebCheckBox`
  (`FireToggle` ignores the client toggle).
- **Tablebox cell** — the inline editor is the text box, gated the same way
  (`SetTextEditMode(… && !IsReadOnly())`).
- **Radio button / slider** — not data-bound (value lives in a property), so view-only does
  not apply.

---

## 5. Commands — the `modifiesData` flag

A view-only form greys DATA-MODIFYING commands and keeps read-only ones live. `BuildCommands`
(`commandBar.cpp`) sets `ibCommandEntry::enabled = false` when the form is view-only AND the
command modifies data. The flag:

- AutoFill / system actions — `ibCommandItem::m_modifiesData` (default true), set at add time
  via `AddAction(…).SetModify(false)` for read-only actions (form chrome Close / Update / Help
  / Change; tablebox Select / Filter / ViewMode; **Generate** — creates a NEW object, doesn't
  modify THIS one);
- manual commands — a `ModifiesData` bool property on `ibValueCommandBarItem`;
- model row commands (tablebox Add / Delete / Copy / Edit) — carried through
  `AddAction(…).SetModify(c.m_modifiesData)`.

The **context menu** (`OnContextMenu`) honours the same flag, not just the toolbar. See
[command-interface.md](command-interface.md) for the action collection / fluent API.

---

## 6. Runtime + honest remainder

- **`ThisForm.ReadOnly`** — a read/write runtime member (`form.cpp` `eReadOnly`). Reading it
  reflects the mode (explicit flag ∨ rights). Writing it re-renders live via `RefreshForm()`,
  so setting it AFTER the form is open takes effect without reopening.
- Three sources of the mode: a read-only role on the object (auto), `SetViewOnly` from C++,
  `ThisForm.ReadOnly` from script.
- The per-source (nested read-only object inside a writable form's tablebox) case is covered
  for cell edit + row commands via `IsReadOnly()` / `IsViewOnly()`; a deeper per-column right
  inside a tabular section is a refinement, not wired.
