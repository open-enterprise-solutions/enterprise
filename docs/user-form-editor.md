# The user-side form editor — "Change form"

A form in OES is not fixed at the shape its author gave it. Any user who has the form open can
re-arrange it for themselves — reorder fields, hide what they never use, change a caption, a
font, a colour, how much room a control takes — through a dialog the form itself offers. No
designer, no configuration change, no developer.

This is a runtime feature of the platform, not a designer one: it ships in `enterprise.exe` and
works on every form built from metadata.

---

## 1. How a user gets there

The form's own standard command set carries it (`ibValueForm::GetStandardCommands`,
`visualView/ctrl/formAction.cpp`):

```cpp
actionData.AddAction(wxT("Change"), _("Change form"), g_picChangeFormCLSID, false, enChange)
    .SetModify(false);
```

- It sits with the form's other chrome commands (Close / Update / Help), so it surfaces on the
  form's command bar the same way they do — see [command-interface.md](command-interface.md).
- `SetModify(false)` marks it as **not data-modifying**, which is what keeps it alive in a
  read-only form: a user may re-arrange a form they are not allowed to edit the data of
  ([view-only.md](view-only.md)).
- `enChange` → `ibValueForm::ChangeForm()` → a modal `ibDialogFormEditor`
  (`frontend/win/dlgs/formEditor.{h,cpp}`).
- In the DESIGNER the whole action path returns early (`CallAsAction` bails on
  `appData->DesignerMode()`) — the designer edits the form for real, through its own editor.

---

## 2. What the dialog is

Three panes over the LIVE form — the same `ibValueFrame` tree the widgets were built from:

| pane | what it does |
|---|---|
| control tree | every control of the open form, in its real hierarchy, icons and captions included |
| property grid | the properties of the selected control that a USER is allowed to touch (below) |
| toolbar / menu | Move Up, Move Down — plus drag & drop inside the tree, same effect |

Dragging is deliberately narrow: a control can only be reordered **among its own siblings**
(`OnEndDrag` refuses when the two items do not share a parent). A user re-arranges a form; they
do not re-parent it into something the author never laid out.

---

## 3. What a user may change

One whitelist, `ibValueFrame::GetAllowedUserProperty()` (`visualView/ctrl/frame.h`), decides the
entire surface — 13 properties:

```
title           minimum_size    maximum_size
font            fg              bg
align           stretch         proportion
orient          tooltip         visible
```

Two things follow from the list itself:

- It is **appearance and layout only** — caption, look, size hints, how the control shares space,
  whether it is shown at all. Nothing about DATA: a binding, a source, an event, a name are not
  in it and cannot be reached from here.
- `align` / `stretch` / `proportion` belong to the **sizer item**, not the control. The dialog
  knows that: when a property is missing on the control it looks at its `COMPONENT_TYPE_SIZERITEM`
  parent (`FillPropertyByFrameValue`). So "make this field stretch across the row" works from the
  same grid, with no separate concept for the user to learn.

Each property is rendered by the shared `ibPropertyRegistry` — the same editors and the same
help text the designer's inspector uses ([property-system.md](property-system.md)). One property
system, two audiences.

---

## 4. How edits are applied

Nothing is applied while the user clicks around. Each change becomes a COMMAND appended to a
list:

| command | what it holds |
|---|---|
| `ibModifyPropertyCmd` | control + property + new value |
| `ibShiftChildCmd` | control + how far to move it among its siblings |

- **OK / Apply** — run every queued command, then `m_owner->UpdateForm()`; the open form
  re-reads itself and the change is on screen. Apply also rebuilds the tree and clears the queue,
  so the dialog stays open for the next round.
- **Cancel** — the queue is dropped unexecuted. Nothing was touched, so there is nothing to undo.

That is why the dialog needs no undo stack of its own: an unapplied edit does not exist yet.

---

## 5. Boundaries as they stand today

- **The tweaks are not persisted.** They live on the OPEN form value; the next time the form is
  opened it is built from metadata again and comes back as the author left it. Saving a form's
  layout is `ibValueMetaObjectFormBase::SaveFormData`, and it is called only from the DESIGNER
  (`visualEditorPanel.cpp`, `visualEditorEvent.cpp`) — the user path deliberately does not write
  metadata.
- **Desktop only.** The dialog is inside `#ifndef OES_USE_WEB`; the web front end has no
  counterpart yet.
- **No per-user storage, therefore no per-user rights on it.** Whoever can open the form can
  re-arrange their own copy of it while it is open.

The obvious next step, and the reason the feature is worth more than it currently delivers: store
the applied commands per user + form key, replay them right after the control tree is built. The
pieces for that already exist — the form key (`ibFormVisualDocument::CreateFormUniqueKey`),
the property serialisation (`ibDataNode`), and the command list the dialog already builds.
