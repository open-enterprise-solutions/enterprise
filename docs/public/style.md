# Style

What the platform's look is painted with: a fixed list of items — colours, fonts, borders — and a style giving each
item a value for the light look and one for the dark.

## The items

`core/style/styleItem.h`. One list, the platform's, for the server and both clients to paint with.

| Kind | Items |
|---|---|
| Colour | form background and text, button background and text, field background and text, selection and its text, alternate background, tooltip background and text, special text, negative number, border, table header and footer background and text, report header, report line, activity, accent, navigation, secondary navigation, important; the desktop's chrome — desktop background (behind the windows), panel background (a docked pane, an inactive caption), bar background (a menu and a tool bar), control background (a scroll bar, a tab strip, a pressed face), disabled text, caption text, accent pressed, accent text (on the accent), shelf (a held tab's ground) |
| Colour list | report grouping — one colour per level, the first outermost, at least one; caption — an active window's caption, its top and its bottom |
| Font | text, small, regular, large, very large |
| Border | control, button — a border's kind and width (`core/graphics/border.h`); its colour is the Border item, so a theme recolours every border at once |

An item has a number and a name. **The numbers are kept forever**: a saved form and the wire hold them. A new item is
appended to its kind's run.

## A style

`core/style/style.h`. A style is two sets of values over the items, `Light` and `Dark`.

- **The platform's style is the default** (`ibStyle::Platform`): a value for every item in both looks. Its light look
  is what the screen shows — the forms' ink on a ground a step below white, the Luna theme's chrome, field, border,
  accent and button colours, and the browser's the same; the items it had no colour for, and the dark look, are new.
- **A form's colour left empty is the platform's**: the form text in the look in force, and the ground the kind's own
  — the form its background, a field its well, a table the content, a button its face, a label and a group the surface
  under them. The server keeps no colour for it.
- **A colour the author chose is chosen in the light look**, and the dark look carries it there (`ibStyle::Get` of a
  colour): its OKLab lightness moved from the light look's form ground and ink onto the dark look's — past the ink, the
  rest of the way to black onto the rest of it to white — its hue kept, its chroma cut to what sRGB holds. White
  becomes about the dark ground, a yellow a dark olive, a navy ink a pale blue. A form's colours and a table row's, on
  both clients; a sheet's paper is the same in every look.
- **The desktop theme paints from it.** Luna asks the platform's style for every colour it paints with, in the look
  in force (wx's appearance — the theme chosen in the client, or the machine's): one table of colours, two looks of
  one theme, not a dark theme beside a light one.
- **An application's style lies over it**, giving only what it changes: an item it leaves out is the platform's
  (`ibStyle::Get`). So a style saved before an item existed reads whole.
- **An item takes a value of its own kind**, and only one its text reads back as — an opaque colour, a font its
  text keeps whole, a border no wider than 64, a list of at least one colour; `Set` refuses the rest, so what is set
  is what is read back.
- **As a node**, each set is a child keyed by item name, a value its text (`r,g,b`, a font's text, a border's
  `kind,width`, an array of texts for a list). What a reader does not know — an item a later build added, a value of
  another kind, a text that names nothing — is left out, and the rest is read. A colour's text is read as `ibColour`
  reads it: a text of numbers it cannot part is black.

## Where it is going

The client's theme says which set is in force — chosen in the client and kept on its machine (the server keeps none):
the desktop takes it when it starts, the browser at once; a report deeper than its grouping list takes the last level; a control's colour may name an item instead of holding a
colour, and a
client resolves it through the values in force; an application holds its own styles, and the studio edits one with
a live preview. Those are built on this list.
