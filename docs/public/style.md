# Style

What the platform's look is painted with: a fixed list of items — colours, fonts, borders — and a style giving each
item a value for the light look and one for the dark.

## The items

`core/style/styleItem.h`. One list, the platform's, for the server and both clients to paint with.

| Kind | Items |
|---|---|
| Colour | form background and text, button background and text, field background and text, selection and its text, alternate background, tooltip background and text, special text, negative number, border, table header and footer background and text, report header, report line, activity, accent, navigation, secondary navigation, important |
| Colour list | report grouping — one colour per level, the first outermost, at least one |
| Font | text, small, regular, large, very large |
| Border | control, button — a border's kind and width (`core/graphics/border.h`); its colour is the Border item, so a theme recolours every border at once |

An item has a number and a name. **The numbers are kept forever**: a saved form and the wire hold them. A new item is
appended to its kind's run.

## A style

`core/style/style.h`. A style is two sets of values over the items, `Light` and `Dark`.

- **The platform's style is the default** (`ibStyle::Platform`): a value for every item in both looks. Its light look
  keeps what the platform painted with — the forms' ink on cream, the Luna theme's chrome, field, border, accent and
  button colours; the items it had no colour for, and the dark look, are new.
- **A configuration's style lies over it**, giving only what it changes: an item it leaves out is the platform's
  (`ibStyle::Get`). So a style saved before an item existed reads whole.
- **An item takes a value of its own kind**, and only one its text reads back as — an opaque colour, a font its
  text keeps whole, a border no wider than 64, a list of at least one colour; `Set` refuses the rest, so what is set
  is what is read back.
- **As a node**, each set is a child keyed by item name, a value its text (`r,g,b`, a font's text, a border's
  `kind,width`, an array of texts for a list). What a reader does not know — an item a later build added, a value of
  another kind, a text that names nothing — is left out, and the rest is read. A colour's text is read as `ibColour`
  reads it: a text of numbers it cannot part is black.

## Where it is going

The person's theme says which set is in force; a report deeper than its grouping list takes the last
level; a control's colour may name an item instead of holding a colour, and a
client resolves it through the values in force; a configuration holds its own styles, and the designer edits one with
a live preview. Those are built on this list.
