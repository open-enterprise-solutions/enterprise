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
- **The assistant access (MCP) listens on the person's own machine, and each person has their own.**
  Their client opens the port and carries the messages to the base; the server opens none, and
  `McpPort` in `server.conf` is no longer read. Point an assistant that used it at the client's port.
  The settings moved from the base to that machine, one set for the designer on port 7374 and one
  for the application on 7375. Saved ones are not carried over: turn it on again and hand the new key
  to the assistant.
- **The application has the assistant too, as a role says.** Two new rights: *Assistant* gives the
  menu and the schema alone, no record of it; *Data assistant* adds what works with the data, such as
  running a report, with what that person may see and change. Both are off until a role gives them, except
  in a base with no users, which is open access. In the designer neither is asked.
- **The application has the designer's Options.** **Tools → Options…** holds the keys, the editor, the
  fonts and the assistant access, plus a *Debugging* page. Each mode keeps its options apart, so the
  application starts with its own defaults. The assistant's window is at the bottom of **Administration**.
- **An assistant in the application can work in your window.** It can open a report it wrote, as an
  external report in a tab; open a list or a document and put the cursor on a row; add or correct a
  value. All of it under your roles, with the *Data assistant* right. What it writes is really
  written. Nothing is added to the configuration.
- **`Execute` and `Evaluate` now do what they say.** They ran as a watch, so what their code wrote was
  silently not written; code that failed said nothing from `Execute` and came back from `Evaluate` as
  the string `<error: …>`. They write now, and a failure raises an error. A module that relied on any
  of this should be checked.
- **An assistant's connection comes back by itself.** After a dropped connection its next call opens a
  new one. The call that was under way is not repeated, and the assistant is told to check its result.

## Script language

- **A form module adds an attribute to its open form and binds a control to it.**
  `ThisForm.AddAttribute(name, type)` takes a `TypeDescription` and answers the new attribute's value,
  so a table gets its columns on the next line; from then on it is `ThisForm.<name>`. It lives as long
  as the form and is not stored with it. `ThisForm.RemoveAttribute(name)` removes one the module
  added, and raises for one the form was designed with. `Control.Source = "Prices"` binds a control by
  the dotted path it reads, `"Goods.Product.Supplier"` across references, the same way `form_bind`
  does; a path that names nothing there raises. A form attribute's name is now matched without regard
  to case, as a module meets it.
- **`Tstr(text)` without a language reads the user's language.** It read the first language
  written in the text, so every user saw the same one. A language the text does not carry still
  falls back to the first written; a named language is read as before.
- **XML: `XMLReader`, `XMLWriter`, `XMLNamespaceContext`, `XMLNodeType`.** A script reads a document
  node by node and writes one, to a file or to a string, in UTF-8, UTF-16, windows-1251 or cp866.
  The parser is expat, built into the engine; an external entity is refused, not read. A file the
  writer could not write is raised at `Close`. The reader says what `xml:lang` and `xml:space` say
  (`Language`, `Space`, the enumeration `XMLSpace`) and keeps whitespace under
  `xml:space="preserve"`; it hands the declaration and the document type over as nodes, passes
  comments and instructions by, or reads CDATA as text, when told to. The writer puts attributes on
  lines of their own (`IndentAttributes`), indents by `IndentString`, and writes a document type
  (`WriteDocumentType`).
- **`XMLString(value)` and `XMLValue(xml)`: a value as XML and back.** The value is packed the way
  `JSONString` packs it, but the XML keeps each field's kind, so a date or a number comes back
  exactly, as the type the text names. The type is said once, by XML Schema's `xsi:type`, and a
  number, a string, a boolean or a date is the element's text; a date outside the years 1 to 9999 is
  refused. A reference inside a structure or an array does not come back yet. `Guid` and `BinaryData` now pack themselves too; a Guid takes its 16 bytes and is
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
- **Hashing: `DataHashing` and `HashFunction`.** `New DataHashing(HashFunction.SHA256)` takes strings
  (as UTF-8), `BinaryData` and files and gives `HashSum`, a `BinaryData`; MD5, SHA1, SHA256, SHA384
  and SHA512. A second argument, the key, makes it an HMAC. The sum can be read midway.
- **Bytes from text and back, and as hex.** `New BinaryData(text, encoding [, byteOrderMark])` holds the
  bytes a text spells, `data.GetString(encoding)` reads them back. `HexString(data)` and `HexValue(text)`
  write them as hex and read them, `Base64String(data)` and `Base64Value(text)` as base64. `String(data)`
  and `New BinaryData(text)` stay base64, and now skip line breaks. Text that is not base64 or hex is
  refused, not read as no bytes.
- **An encoding is a member of `TextEncoding`.** `TextReader` and `TextWriter` took a string or another
  enumeration's member without a word; now it is refused, as `HTTPRequest` already refused a string.

## The browser client

- **The start**: the thin client's splash, with arrows and the stage of the start under it, is up from the first paint;
  a login asked takes its place, and it stays until the first window has its rows. The window's title has the OES logo
  beside the configuration's name.
- **The start page**: the forms the configuration places on it, live, in its columns, with sashes to drag between them.
- **A form comes whole**: one just opened is shown once it has its rows and its fields can be seen, the one before staying up
  meanwhile, and it replaces that one in a single frame, its tab with it. The splash stays until the first window has its
  rows, however long they take. The tabs are no longer drawn anew by every answer.
- **A table's columns fold into their row only in a narrow window**; anywhere else those that do not fit scroll sideways.
- **A text too long loses its middle**, in a table's cell and its header, as the thin client's cell: the first letters and the
  last stay.
- **Denser on a mouse**: a field, a row, a tool, the window's title and a table's header take about two thirds of the height
  they took. A touch screen keeps its targets.
- **A table's three view modes**: List, Hierarchy with the folders it stands in frozen over the rows, and Tree with
  expanders. View mode on the bar asks which.
- **A table's selection mode**: the row, or the cell with its row tinted.
- **A table takes the thin client's keys and clicks**: the arrows, Page Up and Down, Home and End, Tab, F2, Space and Enter, and
  in a tree the arrows sideways and + - *. A second click on the cursor's cell opens its editor, and the table takes the focus
  as its form opens.
- **A table draws its rows as the thin client does**: the row's picture and the expander in the first column, a group's caption
  across the whole line, a flag as a tick, the header and the footer in as many lines as the table says. A column is dragged by
  its header.
- **A cell's editor is a field's**, in look and in buttons, and a column that takes no typing still opens for its buttons.
- **A hidden column in a group drawn as one is not drawn**, and such a group keeps its columns' footers.
- **A dialog moves by its title**, as a window on a desktop.
- **Escape closes what stands on top**: a cell's editor, a menu, a dialog, then the active tab, asking first about what is left
  to save.
- **Help → About** is drawn: the build, the base, the application, the user, the locale, the plugins.

## Fixes

- **`ThisForm.<attribute> = value` writes into the attribute.** It overwrote the attribute's own entry,
  and the attribute kept its old value.

- **A module nobody has compiled yet is compiled once.** Sessions entering a base together, with the
  bytecode cache empty, each compiled the same modules. The first now holds a lock in the database,
  the others wait and read what it saved; if it fails, the next one compiles.

- **Sessions logging in together no longer bring the web server down.** Each visitor rewrote the MCP
  server's settings as if it owned the process.

- **The process has a memory limit.** `MemoryLimit` in `backend.conf`, in megabytes; 0 is none but the
  operating system's. At 80% no new client or window is let in, at 90% nothing is done but closing a
  window and logging out, and the person is told the server is full. On Windows the system refuses
  past the limit. On Linux only the refusals stand: set `MemoryMax` on the service.

- **A Firebird connection caches 4 MB of pages, not 32.** Most of the memory of a busy server was this
  cache, committed at connect and hardly touched.

- **A password can no longer be guessed at full speed.** After a failed login, the next one for that
  name from that address waits 1 second, then 2, 4, up to a minute; one address may try 30 times, then
  once a second. The person is told how long to wait, and the audit event is `login_throttled`. Behind
  a proxy that hides the client's address only the wait per name applies.

- **The syntax helper finds the platform's types and enumerations.** A search for `Array`, `Structure`,
  `AccumulationRecordType` or `QueryResult` came back empty. The server's registry is now asked: the
  designer's pane lists them in Index and Search and shows what one offers, `syntax_search` answers with
  `types`, and `syntax_get` takes such a name. One that has an article still shows the article.

- **A value read from JSON no longer gains a `type` field.** The type's name the JSON view writes
  beside a value's type came back as a field, one more on each round.

- **A selection reads a row's standard attributes.** `Catalogs.X.Select()` and the selections of the
  charts published only `Ref` and the attributes a person added, so reading `Code`, `Description`,
  `DeletionMark` or `IsFolder` from a row was "field not found".

- **`config_check` names a register that the save would refuse**: a register with nothing in it, or one
  written by a recorder that no document posts into, sequences included. The save refuses the whole
  configuration over one of them, so every other change made in a session waited behind it, and the
  first sign of it was that refusal. The check asks the question the save asks and reports it in the
  same words, before anything is saved. A sequence with no dimensions has one border and is saved;
  it is not reported.

- **A list filtered by «contains» finds the rows that contain the value.** On a list read from the
  base the condition reached the database as `LIKE 'value'`, an exact match, so "Comment contains
  Оплат" showed nothing and only "%Оплат%" worked. The value is now wrapped as `%value%`, as the
  same condition already was in memory; a `%` or `_` typed into the value is still a wildcard. The
  query language's own `LIKE` is unchanged and takes its pattern as written.
- **A floating-point column read from PostgreSQL does not depend on the machine's locale.** The server
  writes such a number with a dot, and it was parsed by the locale's rules, where the separator may be a comma.
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
- **MCP: a common picture takes its image through `picture_set`** — `data` (base64 PNG) or `svg`, with a
  `name`. A `Picture` object could be declared with `metadata_create` and never given anything to show:
  `picture_set` refused it as having no picture of its own, and `metadata_set` on its `Picture` asked for
  a structure. The image is kept in the configuration, the picture is listed under `picture_list` →
  `configuration`, and `metadata_set` on that property now names `picture_set`.
