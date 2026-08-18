# Column groups — a row is N bands, and a column is a rectangle in it

> **Scope:** how a table shows more columns than fit side by side — column **groups**
> (stack / row / merged-in-cell), the layout every painter reads, who owns the columns,
> and the laws that govern column widths and the resize drag. Landed 2026-08-17 and
> working in the designer and the runtime.
>
> **Companions:** [wx-fork.md](wx-fork.md) §2.1 (the forked dataview this lives in),
> [table-model.md](table-model.md) (the model the columns show),
> [source-object.md](source-object.md) (what a column binds to),
> [form-engine.md](form-engine.md) (the form that builds them).

---

## 0. The case this exists for

An accounting register journal declares four account-dimension slots per side, each with a
KIND half — twelve columns in a row, titles clipped to `Account di…`. A bookkeeper reads the
dimensions of a posting **downwards**, under their account, not sideways. So the table had to
learn to spend HEIGHT instead of width.

---

## 1. The primitive: bands

A data row is drawn as **N bands** (sub-rows). A column is a **rectangle in an (x, band)
grid** — not a full-height stripe. `ibDataViewColumnLayout` (`datavlayout.{h,cpp}`) derives
that grid from the group tree and is the single source of it: the header, the cells, the
FOOTER, frozen panes, hit-testing and the drag hint all read the same placements, so a group
title stands exactly over the columns it heads, and a stacked column's cell sits exactly under
its own title. A flat table is the degenerate case (one band), not a separate code path.

The layout is a **cache derived from the tree**, never stored state: anything that changes a
column or a group drops it, and the next reader rebuilds it.

## 2. The tree: the root group IS the column store

`ibDataViewCtrl` says one thing about columns: `GetRootColumnGroup()`. Adding, counting,
finding, moving and removing all live on `ibDataViewColumnGroup` (`dataview.h`), and a column
enters a table only by being hung on a group — the root one, or a group under it.

**Membership is ownership.** Whatever hangs on the root at any depth is what the control frees
when it dies; taking a member out hands it back to whoever detached it — which is exactly what
the visual host does (it deletes the object right after `Cleanup`). One fact instead of two
lists, so "freed twice" and "never freed" stop being expressible.

Three kinds of group:

| Kind | What it does |
|---|---|
| `ibColumnGroupVertical` | columns stack — one under another inside one width (the default for a new group) |
| `ibColumnGroupHorizontal` | columns side by side (what a table does without a group) |
| `ibColumnGroupInCell` | the cells MERGE — one header cell over all of them, no divider between |

A group's own title is hidden by default: the plain use of a group is to stack or merge, and
that needs no title. Turned on, it takes a band of the header above its columns.

On the form a group is an ordinary control (`ibValueModelTableBoxColumnGroup`, CLSID
`CT_TBCG`) whose runtime object is `ibDataViewColumnGroupObject` — the same shape a column and
its renderer have, so nesting needs no second mechanism and serialization comes for free.

## 3. Automatic grouping, said by the SOURCE

A source column carries one named field — the **family** it belongs with
(`ibSourceExplorer::GetSourceGroup`). Empty means the column stands on its own, which is the
ordinary flat row.

The family name is the slot's name **without its digits, plus `Group`**:
`AccountDimensionDr1…Dr4` → `AccountDimensionDrGroup`, their kind halves →
`AccountDimensionDrKindGroup`, the credit side → its own two. The sides and the halves
separate themselves, because their slot names differ — no list of literals anywhere, and
nothing reads a role back out of a name (which columns ARE slots comes from the register's one
description of them, `ForEachOwnAttribute`).

Both form builders — the auto-built form (`ibValueForm::BuildForm`) and the designer's
refill-from-source — ask the table the same question:
`ibValueModelTableBox::GetColumnGroupHolder(family)`, which finds the group BY NAME and makes
it on first ask. Finding by name rather than remembering a run means the source may hand its
columns out in any order.

**The source does not name the ORIENTATION.** It says which columns are one thing; how that is
shown is the form's (`Grouping`, vertical by default). Otherwise one fact would have two
owners: the user switches a group to a row, and the next regeneration overwrites the choice.

Reading order of a posting is the register's own knowledge and lives in `FillSourceExplorer`:
debit account → its dimensions → credit account → its dimensions. The list used for the
SCHEMA (every kind, then every value) is a different question and is left alone.

## 4. The law on column widths

Two cases, and no third:

- **the columns fit** (Σ asked-for ≤ room) → they are **stretched in proportion** to what they
  ask for, filling the width to the right edge (the rounding remainder goes to the last one so
  no sliver of background is left);
- **they do not fit** → nothing is squeezed. Every column keeps the width it asks for and the
  **scrollbar** carries the overflow.

There is deliberately **no automatic squeeze**. Squeezing produced unreadable rows
(`P.. R.. Li.. A..`), and the per-column floors invented to stop it then fought the manual
drag. The ratio is always taken from the ASKED-FOR width, never from the current one, so
repeated passes are idempotent. A stack counts ONCE — its columns share one x range and their
edges have to agree (`CollectWidthRanges`).

**While a drag is in progress this law stands aside entirely.** The drag keeps the same
invariant by construction (§5), so re-deriving the widths here corrects nothing and only
disagrees — and two accounts of one geometry always end the same way. Measured on the
13-column journal: the fit handed every range 3 px, *including the ones left of the drag*, the
next pass took them back, and the two took turns per mouse-move — the dragged column's left
edge alternating 504/516 and its border up to 47 px from the pointer.

The widths are applied QUIETLY and everything that follows from them is announced ONCE
(`WXColumnWidthsApplied`): dropping the geometry cache and repainting header, footer and rows.
Announced per column, that ran thirteen rebuilds and repaints per mouse-move over
half-applied widths — visible as leftover dividers in the header and a crawling drag. And it is
announced **only if a width really changed**: the fit runs far more often than the widths move,
and a change nobody made still cost a full rebuild plus three repaints. Measured: the layout was
rebuilt 2–6 times per pixel of mouse travel and the rows area painted twice per pass, the second
time a frame behind the header — which on screen read as "the row catches up with the column".

## 5. The resize drag

**Four rules, in order, and that is the whole law** (`WXApplyColumnWidth`):

1. the dragged range is **exactly as wide as the pointer says** — never "as much as the others
   can afford", which is how a border stops following the cursor;
2. **nothing to its LEFT moves**, so its left edge, and the row before it, stands still. Letting
   the left-hand columns donate slides the whole row under the cursor;
3. the ranges to its **RIGHT take what the table has left**, in proportion to the widths the drag
   STARTED from, each floored at a **sliver** (24 px) — enough to see the column is there and to
   grab its border back. Anyone whose share falls under its floor is pinned there and the rest is
   shared out again, which is exact and needs no fixed number of rounds;
4. when even the floors do not fit, the row is **wider than the table** — and that is precisely
   what the scrollbar is for.

Widen far enough and the right-hand side ends up as equal slivers: that is the maximum working
space a drag can free.

**It is a function of where the pointer is, not of how it got there.** Every move is computed
from the picture the drag began with (`WXBeginColumnDrag`), so nothing accumulates: drag back to
the start and the widths are exactly what they were, and no sequence of moves can drift.

The same law expressed as changes against the PREVIOUS state — the version this replaced —
needed a spare-room budget, four rounds of proportional collection, a clamp for exhausted donors
and a total-restoring correction, and still flip-flopped with the automatic fit. Do not rebuild
it. A pixel per move is not a rounding detail: the drag fires on every movement.

**The one border that cannot be moved** is the right edge of the LAST range: it IS the table's
right edge, so with no range beyond it there is nobody to hand freed room to and the row must
still fill the table. Narrowing it is not a width the law can grant (widening it is — the row
grows and the scrollbar says so). "Nothing to its right" means **no range there at all**, never
"they are all at their floors" — reading the empty donor pool as that condition snapped the
dragged column out to the table's edge and held it there: ask 251 answered with 446, ten ranges
floored, the border 195 px from the pointer.

**The scrollbar is asked for, not drawn** (`RequestScrollbarSync`): a width change sets a flag
and idle serves it, at most once per frame. Doing it inline cost an `AdjustScrollbars` window
cascade per pixel (a sticky drag); deferring it to the mouse release was worse — crossing the
table's edge mid-drag left the rows laid out for the old width, so the bar appeared only on
release. It also makes the hosts agree: the bar used to appear during a drag in the thick client
and only on release in the Designer, because the client's form happened to give the table a size
cascade that re-derived the bar as a side effect and the Designer's dialog did not.

## 6. Moving a column between groups

The drop is read as a POINT, not an x — in a stack several columns share one x range and only
the band tells them apart.

- **the middle of a column** → into that column's group, beside it;
- **the edge of a group** (~10 px) → OUT of it, next to the group itself, one level up. This is
  how a column gets between two groups, and the only way it can leave one: every point of a
  grouped header belongs to somebody, so without an edge zone there is nowhere to drop a column
  meaning "not inside anything";
- **a group title**, away from its edges → into it, at the end.

The hint drawn while dragging is shaped from the SAME answer the drop uses
(`FindColumnDrop`): a horizontal bar between bands inside a stack ("it will go under this
one"), a vertical bar between columns otherwise. No phantom frame follows the pointer — it is
restored from pixels captured when the drag began, so after the columns move it shows a stale
header inside a floating box.

The tree IS the order, so the header's own order array is not permuted as well (`DoMoveCol` is
a no-op there): two answers to "which column is at position N" is precisely how the drawn order
and the stored order used to drift apart.

## 7. What the header and footer draw

Both build their cells from the layout, and the difference is which placements they read:

- the **header** reads header placements — group titles included, and a column under a stack
  gets one band so the titles below it are not painted over;
- the **footer** reads BODY placements: a footer cell is the foot of the column above it, in
  that column's own band, so a stack of three shows three totals rather than one wide strip. Its
  height is `max(the form's property, the row's band count)` for the same reason.

Frozen panes take their edge from the layout too — the right edge of the last frozen column.
Adding up the first N widths counted a stack once per column in it (they share one x range),
which pushed the edge far past where those columns are drawn.

**And so does the hit test — flat or grouped, with no fallback.** `GetColStart` / `GetColEnd` /
`FindColumnAtPoint` answer from the same placements the cells are drawn from; only "no y known"
(a programmatic call with no point) defers to the base class. A flat header is a header of ONE
band, not a separate arithmetic. While it kept the base class's width-summing for the flat case,
the two agreed until anything touched one and not the other (a hidden column, the header's own
order array, a stretch just applied) — and then the resize border was grabbed to the LEFT of
where it was painted.

A divider belongs to the cell on its **left**, from either side of it: a point lands in the cell
whose range contains it, so a pixel to the right of a divider is already inside the NEXT cell,
and asking that cell about its own right edge said "no divider here" — half the grab zone was
dead, catchable only from the left.

**In the rows area the same rule, and one function for it**: `WXColumnAtRowPoint(x, y)` — the
inverse of `GetColumnCellRect`, reading the same grid (the row, then its band, then the column).
The hit test and the click handler both ask it. The click handler used to add up widths left to
right and take the first column whose range held the x, which in a stack is always the topmost
member — so no click could put the cell cursor on any other member of a group, and because
summing widths counts a stack once per member, columns to the RIGHT of a stack answered as the
wrong column too. The expander's hit zone and the rectangle handed to an activatable renderer
come from the same placements: a row-tall rectangle put a checkbox's hot zone over the cell
below it.

## 7a. The expander column — one question, two askers

The tree twisty is drawn in ONE column, and after the layout moved to groups two places decided which
one, by different rules:

- `ibValueModelTableBox::UpdateExpanderColumn` — the first **shown** column, asked of the column tree
  in the order it draws;
- `GetExpanderColumnOrFirstOne` (the render fallback, used until the owner has assigned one) —
  `GetColumn(0)`, with no question about visibility.

While every column was visible the two agreed by accident. With a hidden leading column the expander
was pinned to a column the render loop skips (`if (col->IsHidden()) continue;`), so `col == expander`
never came true and the twisty was drawn **nowhere**: a folder looked exactly like an item, with no
way to see that it opens at all. Both now ask the same question — the first column actually shown —
and the fallback re-picks if the stored one turns out to be hidden.

Worth keeping in view while reading this file: the container flag itself does NOT come from the model
on demand. The fetch stamps it on the row node (`ibComposerNode`), `ibDataViewItem::IsContainer()`
reads it off the node, and `MakeChildNode` turns it into `SetHasChildren` — one marker, read by BOTH
the folder icon and the expander.

## 8. Honest remainder

- The runtime builds its columns from the model's column collection, which carries no family
  name — so automatic grouping applies to the form builders, not to that path.
- The register's resources (amount, quantity per side, currency amount) are not named as
  families yet; the mechanism is ready, the naming belongs where those columns are declared.
- Group serialization round-trip has not been exercised deliberately (a group is an ordinary
  control with `ReadData`/`WriteData`, so it rides the form's own path).
- Containerness is decided in `modelDb` as `isFolderRow || itemHierarchy || hasChildren` — the VIEW
  MODE is not part of it, although the rule is per mode (a flat `List` renders no containers at all;
  `Hierarchical` / `Tree` do). Nothing visible depends on it today, because the render draws no
  expander in a list — but that means the rule lives in two places instead of one.
