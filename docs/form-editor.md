# Form editor — the visual designer

> **Scope:** the Designer's form editor — its panels, the undo/redo command layer, and how
> an edit reaches the canvas. Companions:
> [form-attribute-binding.md](form-attribute-binding.md) (what a binding *is*),
> [property-system.md](property-system.md) (the inspector that drives most edits),
> [command-interface.md](command-interface.md) (the command-bar layer edited here).
> This document describes code that **already exists**; it is a map, not a plan.

`designer/win/editor/visualEditor/` — ~5400 lines across 8 files, the largest single
editor in the Designer.

---

## 1. Lineage — where this came from

**The form editor descends from wxFormBuilder.** That is where the *property* idea entered
the engine in the first place. It has been reworked far enough that perhaps ~5% of the
original remains, but the debt is worth stating plainly, because it explains the shape.

The attribution is still in the source, and stays there — five files, all of them **in the
editor**:

```
designer/win/editor/visualEditor/innerFrame.cpp        // Author : Maxim Kornienko, wxFormBuilder
designer/win/editor/visualEditor/titleFrame.cpp        // Author : Maxim Kornienko, wxFormBuilder community
designer/win/editor/visualEditor/visualEditor.cpp      // Author : Maxim Kornienko, wxFormBuilder
designer/win/editor/visualEditor/visualEditorCmdProc.cpp
designer/win/editor/visualEditor/visualEditorEvent.cpp
```

**There is not a single wxFormBuilder trace in `frontend/visualView/`.** That is the whole
migration, visible as a grep: the inherited code kept sliding until only the *editor* still
carries it. What was general moved out; what stayed is the editing tool.

### 1.1 What the rework actually was

Two things, and they are the reason this is not "a fork of a form designer":

1. **Metadata was separated from forms.** In the original, the form *was* the thing. Here,
   metadata and forms are two independent platform concepts that **ride the same bus** —
   the property object ([property-system.md § 5](property-system.md)). The metadata tree is
   `ibValueMetaObject`'s children; the form tree is `ibValueFrame`'s; both are the same
   skeleton. Nothing about a form is privileged.
2. **The form moved to the front, as a runtime.** `frontend/visualView/` is not the editor
   — it is the **base representation**: it holds the form's elements and renders them, and
   that is what the running application uses.

### 1.2 Base vs editor — the inheritance that encodes it

```cpp
class FRONTEND_API ibVisualHost      : public ibFrontendHostBase { … };  // frontend/visualView — runtime
class            ibVisualEditorHost  : public ibVisualHost       { … };  // designer/…/visualEditor — editing
```

The editor **inherits the runtime host** and adds what only a designer needs: the command
processor, undo/redo, selection highlight, drag-to-create. So the canvas is not a
simulation of the form — it *is* the form, plus editing.

Read that as the direction of travel: anything general belongs in `visualView`; if it only
makes sense while editing, it belongs here.

### 1.3 The visible inheritances

- **Command processor + undo/redo** (§3) — the clearest surviving wxFormBuilder concept.
- **`innerFrame` / `titleFrame`** — the canvas chrome.
- **Printing** (`printout/formPrintOut.h`) is *not* from wxFormBuilder — it was added later,
  sourced separately for this specific job.

---

## 2. Shape — a notebook of two pages

```cpp
#define wxNOTEBOOK_PAGE_DESIGNER    0
#define wxNOTEBOOK_PAGE_CODE_EDITOR 1

class ibVisualEditorNotebook : public ibFrontendVisualEditorNotebook, public wxAuiNotebook { … };
```

A form is **one document with two views**: the visual designer and its module's code. They
are pages of the same notebook, not separate documents — so switching does not reload.

The designer page itself is a nested class, `ibVisualEditorNotebook::ibVisualEditor`, which
owns three panels plus a menu:

| Nested class | Base | Role |
|---|---|---|
| `ibVisualEditorHost` | `ibVisualHost` | the **canvas** — renders the live control tree |
| `ibVisualEditorObjectTree` | `wxPanel` | the **control tree** — the form's composition |
| `ibVisualEditorAttributeTree` | `wxPanel` | the **attribute tree** — the form's data sources |
| `ibVisualEditorItemPopupMenu` | `wxMenu` | per-item context menu |

Each tree carries its own `wxTreeItemData` payload subclass
(`ibVisualEditorObjectTreeItemData` / `ibVisualEditorAttributeTreeItemData`).

The canvas is a **real control tree**, not a mock: `ibVisualEditorHost` derives from the
same `ibVisualHost` the runtime uses, so what the designer draws is what the runtime
builds. Selection highlight is painted over it by `ibDesignerWindow` (an `ibInnerFrame`)
through a dedicated `ibHighlightPaintHandler` event handler — a separate handler exists
because the highlight must paint over the frame's *content panel*, which the frame itself
does not own the paint cycle of.

---

## 3. Undo/redo — two command layers, do not confuse them

### 3.1 `ibVisualEditorCmd` — the editor's own command

```cpp
class ibVisualEditorCmd {
protected:
    virtual void DoExecute() = 0;   // apply
    virtual void DoRestore() = 0;   // undo
public:
    void Execute() { if (!m_executed) { DoExecute(); m_executed = true; } }
    void Restore() { if ( m_executed) { DoRestore(); m_executed = false; } }
protected:
    bool m_executed;
};
```

The `m_executed` flag makes both directions **idempotent** — a double `Execute()` or a
`Restore()` on a never-executed command is a no-op, not a corruption. Subclasses implement
only `DoExecute` / `DoRestore`.

The nine commands (`visualEditorCmdProc.cpp`, ~1500 lines) are the complete edit
vocabulary of the form editor:

| Command | Edits |
|---|---|
| `ibVisualEditorExpandObjectCmd` | tree expansion state |
| `ibVisualEditorInsertObjectCmd` | add a control |
| `ibVisualEditorRemoveObjectCmd` | delete a control |
| `ibVisualEditorCutObjectCmd` | cut a control |
| `ibVisualEditorShiftChildCmd` | reorder within a parent |
| `ibVisualEditorModifyPropertyCmd` | one property |
| `ibVisualEditorModifyEventCmd` | one event |
| `ibVisualEditorInsertAttributeCmd` | add a form attribute |
| `ibVisualEditorRemoveAttributeCmd` | delete a form attribute |

Anything the designer can change is one of these — which is why "it edited but did not
undo" always means a path bypassed the command layer.

### 3.2 `ibVisualDesignerCommandProcessor : wxCommandProcessor`

The wx-side stack that the menu's Undo/Redo talk to. `ibVisualEditor` also has a nested
`ibCommandProcessor`. Treat the `ibVisualEditorCmd` family as *what an edit is* and the
processor as *where the history lives*.

---

## 4. The one write path — `ModifyPropertyCmd::DoExecute`

This function is the whole "edit → repaint" policy, and it splits on **who owns the
edited property**:

```cpp
ibValueFrame* control = dynamic_cast<ibValueFrame*>(m_property->GetPropertyObject());

m_property->SetValue(m_newValue);
m_visualEditor->Modify(true);

if (control != nullptr) {
    if (g_controlFormCLSID == control->GetClassType())
        visualEditor->UpdateVisualHost();     // the FORM itself changed → rebuild the host
    else
        visualEditor->UpdateControl(control); // one control → repaint just it
}
else {
    // Non-control owner: a form ATTRIBUTE / its held value / a command-bar LAYER
    m_visualEditor->RefreshEditor();
    if (ibPropertyObject* owner = m_property->GetPropertyObject())
        objectInspector->SelectObject(owner, true);
}
```

Three things worth keeping:

- **Granularity is by owner, not by property.** A control edit repaints that control; a
  form-level edit rebuilds the host. Nothing walks "what changed" — the owner's type
  answers it.
- **A non-control owner rebuilds the whole editor.** An attribute, its held value, or a
  command-bar layer has no widget of its own to repaint, so `RefreshEditor()` fans out
  `NotifyEditorRefresh`, which re-reads the attribute tree too. `RefreshEditor` coalesces
  re-entrant calls itself, so the parallel `objinspect` child→parent bubble
  ([property-system.md § 5.2](property-system.md)) firing the same refresh is a no-op —
  **not** a double rebuild.
- **The re-select is deliberate, not incidental.** `RefreshEditor` rebuilds the object
  tree, which drops the tree selection and with it the inspector's focus. Without the
  explicit `SelectObject(owner, true)`, toggling e.g. a command bar's AutoFill bounces the
  inspector off the bar onto the form.

> **Edits go through `ModifyProperty`, not around it.** The command is what makes an edit
> undoable *and* what triggers the refresh; calling `SetValue` directly changes the value
> and leaves the canvas stale.

---

## 5. Drag-to-create — one payload, one decoder

Dragging a node from the **attribute tree** onto the form creates a control already bound
to that path. The payload is a source path as raw ids (`ibSourceDescription`), under the
`"oes_source_drag"` format:

```cpp
class ibSourceDragDropTarget : public wxDropTarget {
public:
    using Handler = std::function<void(wxCoord, wxCoord, const ibSourceDescription&)>;
    explicit ibSourceDragDropTarget(Handler handler);
    wxDragResult OnData(wxCoord x, wxCoord y, wxDragResult def) override;
private:
    Handler m_handler;
};
```

**Both** the canvas and the object tree install this same target, each supplying a handler
that maps the drop point to a parent and creates the bound control there. The payload is
decoded **once, in one place** — the two drop surfaces share a decoder rather than each
parsing the ids.

The control class is resolved *from the path* (the pinned reference type per hop, carried
by `ibSourceHop` — `backend/sourceDescription.h`), which is why the drop knows to make a
text box for a string and a reference picker for a reference. See
[form-attribute-binding.md](form-attribute-binding.md).

---

## 6. File map

| File | Lines | Holds |
|---|---|---|
| `visualEditor.h` | 1025 | every class declaration above |
| `visualEditorCmdProc.cpp` | 1512 | the nine commands + `Undo()` |
| `visualEditor.cpp` | 650 | editor lifetime, refresh |
| `visualEditorObjectTree.cpp` | 610 | control tree |
| `visualEditorAttributeTree.cpp` | 635 | attribute tree |
| `visualEditorNotebook.cpp` | 175 | the two-page notebook |
| `visualEditorPanel.cpp` | 151 | panel wiring |
| `visualEditorEvent.cpp` | 99 | event table |

Neighbours: `innerFrame.{h,cpp}` (462) — the frame the canvas draws in; `titleFrame`;
`printout/`.

---

## 7. Honest remainder

- **`visualEditor.h` at 1025 lines declares 10+ classes**, most of them nested inside
  `ibVisualEditorNotebook::ibVisualEditor`. The nesting expresses ownership honestly, but
  the single header is a restructuring-plan candidate (one header per panel).
- The canvas/tree/attribute-tree split is clean; the **notebook ↔ editor ↔ host** naming
  chain (`ibVisualEditorNotebook::ibVisualEditor::ibVisualEditorHost`) is three levels of
  the same word — a naming-plan candidate.
- `DoExecute`'s non-control branch re-selects `m_property->GetPropertyObject()`. For a
  *nested* owner (an attribute's held value) that is the nested object, not the inspected
  holder — worth a runtime pass on "change a form attribute's type" specifically, since a
  nested, unregistered owner reaching `GetClassName()` is the shape that asserts.
