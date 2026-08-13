# List settings — filter, sort, grouping

What a user changes about a list, and why it is built the way it is.

---

## 1. What they are

Three settings travel together on every list, report and future composition:

| Setting | What it says | Shape |
|---|---|---|
| **Filter** | which rows belong | a TREE of groups (And / Or / Not) over conditions |
| **Sort** | in which order | an ORDERED list of { field, direction } |
| **Grouping** | how rows fold | an ORDERED list of { field, kind } |

They are not three dialogs. They are three parts of one settings object
(`ibValueListSettings`, `backend/composition/listFilter.h`) that a model owns, a
form edits, and the composer applies.

---

## 2. Why the filter is a TREE and not a list

A flat list of conditions can only mean one thing: everything AND-ed together.
The moment a filter stops being trivial it needs the other shape —

```
Warehouse = Main AND (Status = Shipped OR Status = Delivered)
```

— and there is no flat form of that. So the filter is a tree: the root is a
group, a group holds conditions and other groups, and a group joins its children
with **And / Or / Not**.

The root group **is a row**, labelled `Filter (And)` — it says what it is and what
it joins its children with, and that operator is edited on it like any other
group's. It was invisible at first, on the reasoning that a row standing for "the
whole filter" says nothing. Two things proved otherwise:

* **The top-level operator had nowhere to live.** A user who wants `a OR b` at the
  top adds an "Or" group — and gets `a AND (b)`, because a group's operator joins
  *its own children*, and a group with one child has nothing to join. The tree
  read as if it said OR and behaved as AND, silently. With the root visible, the
  answer is one click on the row that was already there.
* **Every top-level row looked like the root.** The commands test "is this the
  root?" as "has no parent row", so with the root hidden that was true of *every*
  condition at the top level — and Delete, Move and Group were greyed out on
  exactly the rows a user has most of.

The root differs from the groups a user makes in exactly two ways, both asked
through one `IsRootGroup`: it names itself, and it has **no Use box**. Clearing
that box switched the entire filter off (`BuildFilterGroup` yields nothing for a
group not in use) while every condition below still showed as ticked — a rubber
band nobody could see. `SetRoot` also forces `Use = true`, so a setting saved
while the root happened to be off cannot come back as a filter that quietly
filters nothing.

A group's operator cell reads as the operator alone — `And`, `Or`, `Not`. It used
to read "And group", which invited the wrong reading ("group this with the next
line"); that a row *is* a group is already visible from its having children.

---

## 3. Both sides of a condition are VALUES

A condition is `{ Use, Left, Comparison, Right, DisplayMode, Presentation }`, and
**Left and Right are both `ibValue`**. That single decision is what makes

* `Amount > 100` — field against a number,
* `Price > Cost` — field against a FIELD,
* `True = True` — two constants,

one rule instead of three special cases. The left side is a **composition field**
(`ibValueCompositionField`): a dotted path, its readable presentation, and the
type of the leaf it points at. The right side takes **the type that field lends**
— which is what makes its editor the right one (a quick choice for a reference,
a two-item drop-down for a boolean, a calendar for a date) without the form
knowing anything about types.

Change the field on the left and the right side is cleared — but only when the
TYPE actually changes; re-picking the same field must not throw away what the
user already entered.

---

## 4. Every "kind" is a registered enumeration

`ComparisonKind`, `SortDirection`, `FilterGroupKind`, `FilterDisplayMode`,
`GroupKind` are runtime enumerations, not C++ enums hidden behind a combo box.

That is not decoration. A registered enumeration:

* is offered by the runtime's own quick choice, so no form spells the list twice
  (and no form has to keep its order in step with an enum by hand);
* is what a script writes (`ComparisonKind.Equal`);
* round-trips through a saved setting **by number**, while presenting itself by
  its description (the translated caption a user reads), so renaming a caption
  never breaks stored data;
* costs nothing to add a member to — the picker follows.

A grouping's kind was the last plain enum here, and while it was one it simply
could not be changed in the form: there was nothing to offer.

---

## 5. Where it goes — the same AST the language compiles to

`ibBuildFilterAst` turns the tree into `ibQueryAstExpr` — the very expression the
`Restrict` keyword compiles to, and the one the lowering renders into SQL. A
condition becomes `Compare` / `Like`, a group becomes left-folded `Logical`
nodes, a field becomes a `Column` carrying its path AS SEGMENTS (the lowering
dot-walks them into joins), and anything else becomes a `Param`, so a string is
never read as syntax and a date never in somebody's locale.

The point of this is not tidiness. It means **one language of conditions** serves
the list filter, the report composer, conditional appearance and the access
policy (RLS). A second condition language was never written, and there is
nowhere for the two to drift apart.

Sort and grouping travel the same way: `composer.Sort(path, ascending)` and
`composer.TotalBy(path, kind)`.

---

## 6. One store, one door

The filter has exactly one storage: **the root group**. The flat `Filter`
collection a script uses (`list.Settings.Filter.Add(…)`) and the quick
column-filter command are a DOOR onto that same root — not a second list.

They were two stores for one day, and everything that touched only one of them
silently did nothing: clearing the filter cleared the store nobody applied, and
the quick filter wrote to a list the commit did not read. The lesson is in
[[MEMORY]]-shaped form: a second currency for the same value is not a
convenience, it is a bug waiting for its first user.

**Persistence** goes through the node serialisation (`ibDataNode`): the tree packs
itself, children and all, so a saved setting, a copy into the dialog's buffer and
a future transfer to the web are the same mechanism.

⚠️ **The dialog's buffer is a PACKED COPY**, and that is the same mechanism again:
`ibLoadSettingsFromComposer` clears the buffer's root and refills it by packing the
live tree and reading it back. So a value type with no packed form does not
degrade — it takes the whole filter with it. That is what an enum member did until
2026-08-13: it could not pack, the refill was skipped, and the settings form opened
empty over a list that was visibly filtered
([serialization-io.md § 4a](serialization-io.md)). The neighbouring failure looks
identical from the outside and is a different defect: a restored REFERENCE that kept
the guid but still called itself new presented as an empty string, so the row was
there and the value cell was blank.

⚠️ **Reading goes through the metadata DOOR** (`ibMetaData::Deserialize`), never
straight to `ibValue::FromNode`. A filter holds configuration types — a reference
to a document, an enum member — and those exist only in the metadata's registry.
Asking the value factory for one raises "Unknown value type" on a filter that was
saved perfectly well.

---

## 7. How the form edits it

The settings form has one rule: **the cell asks the row for a VALUE, and the
value's TYPE decides how it is chosen.**

```
click "…"  ->  what is in this cell?
               composition field  -> the field picker (the source tree)
               enumeration        -> the runtime's quick choice
               reference          -> the metaobject's own selection form
               plain value        -> typed in
```

This is not a switch in the dialog — it is `ibTypeControlFactory::ChooseValue`,
the SAME route a text control on a form and a table column walk. Three copies of
that sequence existed; they are one now, and the cell only answers its questions
(`GetTypeDesc`, `GetDataType`).

**Whether there is a short list at all is ONE function too:**

```cpp
FRONTEND_API bool HasQuickChoice(const ibCtorAbstractType* typeCtor);   // frame.h, body in frame.cpp
```

A primitive edits itself, an **enumeration** offers its finite registered members, and a
metadata type answers through its metaobject — `ibValueMetaObjectRecordDataRef::HasQuickChoice`,
which is the property a user ticks on a catalog or a chart, and a flat `false` on a document.
Anything else has no short list to offer. Both callers ask this one function — the form
control (`ibValueFrame::HasQuickChoice`) and the filter cell — and the cell adds exactly one
case of its own before it: the composition FIELD on the left, which has a picker (the source
tree) but no ctor to ask.

It used to be a walk over the ctor KINDS written out at both callsites, and the two
copies disagreed about enumerations: the same account type dropped its member list in
a filter and refused to on a form. One walk over the kinds, in one place, is the whole
fix.

A new line is created **empty**. Taking whatever happens to be selected in the
field tree invents a condition the user did not ask for — and worse, fixes its
type before anything was chosen.

---

## 8. Refusing, not dropping

`ibValidateSettings` raises `ibBackendException` on a setting that cannot be
applied: a condition with no field, a value the field cannot hold, a sort or a
grouping line with no field.

ONE check, TWO faces — the runtime catches an exception; the form catches the
same one and shows it as a warning, staying open on the offending line. The check
runs BEFORE the composer is touched, so a half-written line never takes the
previously working settings down with it.

---

## 9. Where this is going

1. **Settings as PARTS OF THE MODEL.** A filter should identify itself as a table
   model, bound to its list and reading it, with a command interface of its own —
   then it can be dropped on an ordinary form, drawn by the tablebox, and the
   settings dialog becomes just a form that hosts it. See
   `project_settings_parts_as_models` in memory.
2. **The field tree through SOURCE HOPS.** It is currently walked by hand
   (`AppendSourceFields`), which has already produced three bugs: a lost readable
   path, technical names instead of synonyms, and infinite expansion through a
   self-referencing type. `ibSourceHop` / `ResolvePath` already do this walk, with
   presentation and types included.
3. **The tree stored IN the composer.** It reaches the composer as one expression
   and cannot be read back out, so the form takes it from the model's settings
   instead. Storing the tree itself would leave a single source of truth — and
   the web and the script would see exactly what the desktop sees.
4. **The same type everywhere else** — report composition settings, conditional
   appearance, and RLS already share the AST; they should share the editor too.

---

## 10. Traps this cost

* **Model column 0 is reserved** by the `ibDataViewCtrl` fork — it paints blank
  and does not edit. Settings columns start at 1.
* **`SetTextEditMode(false)` is the read-only POLICY** — it locks the Select and
  Clear buttons along with the text. Using it to stop typing into a field cell
  disables the picker on every tab at once.
* **A modal picker ends the cell's edit.** The row must be captured BEFORE the
  modal opens; by the time the choice returns, the base has cleared it.
* **`X().ConvertToValue(p)`** — a temporary `ibValue` owns what it wraps and dies
  at the end of the full expression, leaving `p` dangling. Hold the value.
* **Reading a filter without the metadata door** raises on configuration types.

---

## 11. Open defects

* ⚠ **The filter tree's root group does not expand by default** (open, 2026-08-13). The
  dialog opens with the root folded shut, which is exactly the state that reads as
  "nothing set" — the thing §2 says the visible root exists to prevent. **The cause is
  not found.** The expansion is posted, not called inline (`RefreshFilterTree` →
  `CallAfter(&ibDialogListSettings::ExpandFilterTree)`, and again from `wxEVT_SHOW`),
  because expanding a row the view has not FETCHED yet is a no-op; `ExpandFilterTree`
  itself walks depth-first, parent before child, for the same reason. That is the shape
  the code is in, and it is not enough. Anyone picking this up starts by establishing
  WHEN the view has actually fetched the root — not by adding another post.
