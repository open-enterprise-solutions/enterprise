# Unreleased

Since [v2.0.0-preview.1](v2.0.0-preview.1.md).

## Breaking changes

- **In a date pattern `MM` is the month and `mm` the minutes**, as everywhere else they are written;
  the seconds are `ss`. It was the other way round. A stored format such as `DF=dd.mm.yyyy` now
  prints the minutes where the month was, and nothing reports it: change `mm` to `MM` (and `MM` to
  `mm`, `SS` to `ss`) in the formats of attributes, form items and report fields. The built-in
  presets are already written the new way.
- **`SerializeValue` and `DeserializeValue` are now `JSONString` and `JSONValue`**, a pair with
  `XMLString` and `XMLValue`. A module that calls the old names no longer compiles: rename the calls.
- **A guid takes 16 bytes, and the packet is OES-RPC version 2.** Every guid a node holds — a
  reference's, a metadata object's, a `Guid` value — is its 16 bytes, not its text; XML and JSON
  still show the text. A configuration, a form or a message written by this build is not read by an
  earlier one (`the packet is OES-RPC version 2, this end reads 1`): update the thin clients with
  the server. A base written before is read as it is.

## Script language

- **XML: `XMLReader`, `XMLWriter`, `XMLNamespaceContext`, `XMLNodeType`.** A script reads a document
  node by node and writes one, to a file or to a string, in UTF-8, UTF-16, windows-1251 or cp866.
  The parser is expat, built into the engine; an external entity is refused, not read. A file the
  writer could not write is raised at `Close`.
- **`XMLString(value)` and `XMLValue(xml)`: a value as XML and back.** The value is packed the way
  `JSONString` packs it, but the XML keeps each field's kind, so a date or a number comes back
  exactly, as the type the text names. A reference inside a structure or an array does not come
  back yet. `Guid` and `BinaryData` now pack themselves too; a Guid takes its 16 bytes and is
  written as its text.
- **`Format` spells a date out and pads a number.** A date pattern takes `d`/`dd`, `ddd`/`dddd`
  (the weekday), `M`/`MM`, `MMM`/`MMMM` (the month's name, after a day in the genitive in ru and
  uk), `h`/`hh` and `tt` (a 12-hour clock) and text in single quotes. The names come from the base's
  language; `L=ru` says them in the language it names. New codes: `DLF` (a preset — `D`, `DD`, `T`,
  `DT`, `DDT` — used when there is no `DF`), `L` and `NLZ` (the whole part padded with zeros to
  `ND − NFD` digits).
- **`BegOfHour`, `EndOfHour` and `GetMillisecond`.** The first two are the hour's period, as
  `BegOfDay`/`EndOfDay` are the day's. `GetMillisecond(d)` is the date's own count, milliseconds from
  the empty date, where a date taken as a number is seconds: two of them apart are the time between,
  to the millisecond.
- **An enumeration manager answers `EmptyRef()`**, as a catalog's, a document's and a chart's do.

## Fixes

- **A selection reads a row's standard attributes.** `Catalogs.X.Select()` and the selections of the
  charts published only `Ref` and the attributes a person added, so reading `Code`, `Description`,
  `DeletionMark` or `IsFolder` from a row was "field not found".

- **A list filtered by «contains» finds the rows that contain the value.** On a list read from the
  base the condition reached the database as `LIKE 'value'`, an exact match, so "Comment contains
  Оплат" showed nothing and only "%Оплат%" worked. The value is now wrapped as `%value%`, as the
  same condition already was in memory; a `%` or `_` typed into the value is still a wildcard. The
  query language's own `LIKE` is unchanged and takes its pattern as written.
- **A server whose base and `backend.conf` name no language speaks US English**, not the language of
  the machine it runs on, so the same server answers the same words everywhere. A language named
  and not installed on the machine falls back to US English too.
- **A table read anew with no row to read around is placed by the server** — a client's first page, a
  list read again from its top: from the top when the table stands on no row, else around the row it
  stands on, with as many rows ahead of that row as the page, the page saying whether it is the top (`Begin`).
  The browser client took such a page for the top: a table standing on its last row came back to a
  resumed session as that one row, with no way to the rows above it. The thin client puts the row it
  was read around mid-view and reads the rows above the ones that came when there are more.
- **The browser client's table reads nothing while it has no height** (a notebook page behind another),
  where every page fell short of the window and the whole list was read; and when the rows held fall
  short of the window with nothing after them, it reads back, not on.
- **A folder opened in the hierarchical view shows the element just added to it.** A list read anew
  inside a folder (an element added, the list refreshed) showed the folder empty, and the next element
  went to the top: the server read the top for that frame and lost the folder the client stood in. In a
  folder the client now reads its own level, and the server keeps the folder.
- **A list in the List view is flat.** A folder in it could be opened, and showed the whole list again,
  the folder among it. A folder opens only where the tree is read.
- **A custom list (one with a query of its own) shows its tree level by level.** Its query is computed in
  memory, and that road read the whole list for every folder, so a folder opened into itself without end.
  Every page was the whole list too, so the rows came twice, the ones above a page turned round. It now
  reads one level and one page from where the last ended, as a list read from the base does.
- **Switching the view mode reads the list at once**, where the rows came in the new mode only after the
  next click.
- **A read the server could not do is said in the thin client**, where the list or the sheet showed
  nothing: a folder opened, a list scrolled or read anew. The engine's exception is refused with a code
  of its own, 520, its words in `message` (500 stays "the server could not do it"); the thin client raises
  it as its own, the list keeping the rows it had. A folder the server no longer holds is said too.
- **An Excel workbook with a part that does not parse is refused, the part named**, where it was read
  with that part left out: shared strings that did not parse left every string cell of the book empty,
  and nobody was told. The workbook's XML is read by the engine's own parser, no longer by wx's.
- **The thin designer's composer names its types and chooses its values.** Every Type cell read `<any>`, and the
  "..." of a parameter's type, of its value and of a condition's value opened nothing. The cells name the
  configuration's types, and the "..." opens the type picker and the declared-value window, as the desktop's did.
  A condition's value chosen there is the reference chosen, a predefined element too; the desktop's made every one
  of them the empty reference. A parameter's expression is checked again, as the desktop checked it: `1 + (` or a
  name nobody declares is said when the editor or the window is closed, where it went into the cell without a word.
- **A value declared in the designer reads as it was chosen when the composition is opened again**, where a
  parameter set to `CatalogRef.Goods.EmptyRef` or to a predefined element read as a number: its text was written
  under the name the value's type is written under, and the type was read in its place. A composition saved before
  reads such a value as nothing until it is chosen again; what it refers to is unchanged.
- **The thin designer does what the desktop did in these places**:
  - A row dragged onto another in the configuration tree is copied there.
  - Clear database closes the open tabs first. A modified tab asks to be saved, and a tab that stays leaves the
    base as it was.
  - A data processor or a report replaced from its file keeps its place in the tree.
  - Tools → Options takes effect in the editors already open and in the output window.
  - A picture's System list offers the class icons too.
  - A control's Source window names fields by their synonyms.
  - A web client started from the designer runs in the base's language.
  - On macOS, Start without debugging starts the thin client. The designer looked for a plain file and missed the
    client's bundle.
  - A composer has no "Composition data" row in the inspector.
