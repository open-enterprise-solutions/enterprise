# Functional options

**What.** A `FunctionalOption` says whether a part of the configuration is used in this base — several
warehouses, a foreign currency, serial numbers. While it is off, what belongs to it is not shown. It sits
under *Common → Functional options*.

**Why.** A configuration is written for many bases, and a base uses part of it. Hiding the unused part by
hand means every form, list and section asking the same switch, and the next form forgetting to. An
option is declared once; what belongs to it follows it everywhere the interface is built.

## What it is

An option **is a stored value, as a constant is** — both stand on one base: one value per base, a form to
change it, a `Read` and a `Write` right, and the module with `BeforeWrite(Cancel)` and `OnWrite(Cancel)`.
Its value is always Boolean.

| Part | Holds |
|---|---|
| The value | On or off, for every user of the base. Kept with the constants, and switched like one: from its own form, in *All functions* beside them or in a section it is put into |
| Members | The objects, attributes, tabular sections, dimensions, resources and commands that belong to it |
| `Initial value` | The value in a base where nobody has switched it yet. On by default |
| `Requires` | The option it works under: while that one is off, this one counts as off |
| The module | `BeforeWrite` refuses a switch the data does not allow; `OnWrite` carries its consequences |

**Membership is kept on the member**, the way an object keeps the sections it is in. Opening an option
shows the configuration as a tree to tick, like a section or a common attribute; a copied object takes
its options with it. What may belong is what the interface shows — an option itself never does.

**An element of a form** names its options itself: *User visibility → Functional options*, ANDed with
its `Visible`. A group, a page or a sizer that is not available takes what it holds with it; an element
bound to a field is also unavailable while its field is.

From code, as a constant is read:

```
If (FunctionalOptions.MultipleWarehouses.Get()) {
    …
}
```

## What it guarantees

- **Only what is shown changes.** An option switched off leaves its members in the metadata, in the
  schema and in every query. A query naming a hidden field runs, and the query constructor lists it; data
  written before is there when the option comes back on. Switching is therefore reversible.
- **One rule.** A member of no option is shown. A member of options is shown while **any** of them is on
  — a field two parts of the system use is needed while either is used. A field of a hidden object is
  hidden with it, and so is a field whose every type is a reference to hidden objects.
- **Everywhere the interface is built:** controls bound to a hidden field, columns of a table, a table on
  a hidden tabular section, sections of the command interface, *All functions*, command bars (commands of
  hidden objects, hidden common commands), toolbar buttons, the field pickers of list and report settings
  (a reference unfolds without its hidden fields), and *Change form* (a user cannot bring such an element
  back).
- **A setting is hidden, not cancelled.** A filter, sort, grouping or selected field already set on a
  hidden field stays in the settings; the settings window and the quick filters do not show it. What would
  show the field's values is left out when a report, a list, a table of values or a tabular section is
  composed — a grouping by it, a column of it — while a filter or a sort on it still applies. A value
  computed from a hidden field is hidden with it.
- **Not a right.** Nothing is refused: a form of a hidden object still opens from a reference to it. Who
  may see what is the roles' question.
- **The designer shows everything it edits.** A report it composes shows what the base uses, as the
  application would.
- **For every user of the base, read without asking the user's rights.** Switching needs `Write`.

## Where it stops

- A change is seen in forms built after it: at once in the process that switched it, and in another
  client from its next start. Open forms are not rebuilt.
- The value is one per base. An option that depends on an organisation or a date is written in the form's
  module, reading the settings register itself.
