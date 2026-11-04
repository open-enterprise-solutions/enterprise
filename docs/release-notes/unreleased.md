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
- **An assistant's sandbox and its runs are written in the registration journal**, source `assistant`:
  what ran and how it ended. They are no longer shown in your window's messages.
- **`Container` is now `Map`.** `New Container` no longer makes one: write `New Map`. A map the base
  stored reads back as before.
- **`Catalogs`, `Documents` and the other managers, `Metadata.Catalogs` and `Data.Catalogs` are
  collections.** `For Each` hands out the managers, the objects or the data sources themselves, not
  key-value pairs. `Count()` and `Get(name or position)` answer, and a name is found whatever its case.
- **A manager's create verbs are named by what they create.** A catalog, a chart of characteristic types
  and the parameterized jobs: `CreateItem` (was `CreateElement`). A chart of accounts: `CreateAccount`.
  A chart of calculation types: `CreateCalculationType`. A folder: `CreateFolder` (was `CreateGroup`). A
  module that calls the old names has to be changed.
- **A string becomes a number only when the whole of it is one.** `Number("234,8")` gave 234 and
  `Number("12x")` gave 12; both are now refused with "Cannot convert string to number". Spaces around the
  number are still allowed.

## Script language

- **What follows `new Class(…)` is read on.** `new Array(3).Count()`, `New Query(text).Execute().Select()`
  and `New Structure("A", 1).A` compile, as a statement too. They were refused with "Keyword or
  identifier expected".
- **`StrSplit(string, separators, includeBlank = true)`** cuts a string into an Array of its parts. Each
  character of `separators` is a separator; with `includeBlank` false the empty parts are left out.
- **`FillPropertyValues(receiver, source, list, exclude)`** fills the receiver's properties with the source's of the
  same names: a structure, a row, an object alike, and nothing is added to the receiver. With no list, every property
  the source has and the receiver can take; a list, names separated by commas, fills exactly those, and a name either
  side lacks is refused as an assignment would be. `exclude` leaves out what it names.
- **`ValueList`** — a list of values, each item with a presentation, a check and a picture. `Add`, `Insert`, `Get`,
  `Delete`, `FindByValue`, `UnloadValues`, `LoadValues`, `Copy`, `SortByValue`, `SortByPresentation`, `FillChecks`,
  `Move`, `Index`; `For Each` hands out the items. On a form it is a table that shows the value; the presentation,
  the check and the picture can be added. For a query's `IN`, pass `list.UnloadValues()`.
- **`Any` is a type that takes every value.** A value table's column given no type is `Any`, and so is the value
  of a list's item; the type picker offers it first, where it offered Undefined, which takes only itself.
- **`Name(…)` calls the procedure or function of that name even where a variable has it.** Names are read
  whatever their case, so `chapterNames = ChapterNames();` called the variable and failed with "value is not a
  callable function". The module's procedure or function, declared above or below, now wins, as a system one
  already did; a function value in a variable is called only when nothing of its name is declared.
- **`SetStatus` in a background job says where the job has got to.** With no window it reached nothing; it is
  now the job's activity, which Active Users and `code_status` show.
- **The module check names a method the manager, object or reference does not have.** `Catalogs.Products.CreateElement()`
  compiled and failed only when it ran, with "Aggregate object field not found". The designer's *Syntax control*, the
  check on save, `module_write` and `config_check` now say "… has no method 'CreateElement'" at its line. A value that
  came out of a call is not asked about, so the check runs no query.
- **A form module adds an attribute to its open form and binds a control to it.**
  `ThisForm.Attributes` lists the form's attributes as `ThisForm.Controls` lists its controls: by index,
  by name and in `Foreach`, each one with its `Name`, `Caption`, `Type` and `FillCheck`.
  `Attributes.Add(name, type)` takes a `TypeDescription` and answers the new attribute; its value is
  `ThisForm.<name>` from then on. It lives as long as the form and is not stored with it.
  `Attributes.Remove(attribute)` removes one the module added, and raises for one the form was
  designed with or one a control still shows. `ThisForm.Commands` lists the form's commands the same
  way. `Control.Source = "Prices"` binds a control by the dotted path it reads,
  `"Goods.Product.Supplier"` across references, the same way `form_bind` does; a path that names
  nothing there raises, saying what that level offers, and a table bound so gets the columns of what it
  shows. A form attribute's name is now matched without regard to case, as a module meets it.
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
- **`COMObject` compiles everywhere and is complete on Windows.** `New COMObject(progId)` compiles on
  macOS and Linux too, and is refused there when it runs ("COM objects are available on Windows only").
  On Windows it works on the application server's threads as well as in the client; arrays (an `Array` with
  rows of one length as one table of as many dimensions), `BinaryData` as bytes, CY and DECIMAL numbers (a
  fraction longer than a double keeps goes as DECIMAL), by-reference arguments — every one, as VBScript passes
  them, to an object that does not describe its members — and omitted ones cross over; `For Each` walks a COM
  collection; `obj.AddEventHandler(name, function or procedure name)` receives the object's events. A failed
  call names the object and the member. `obj <> Undefined` is True for a COM object; it was False.
- **`COMSafeArray` — an array as COM holds it.** `New COMSafeArray("VT_I4", 2, 3)`, or from an `Array` with an
  element type and sizes; `GetValue` / `SetValue` by the array's own indices, `GetLength`, `GetLowerBound`,
  `GetUpperBound`, `Resize`, `Unload()` to an `Array`. An array a COM object returns is always one — a
  spreadsheet's `Range.Value` keeps its bounds from 1 — and one goes to a method with its own element type.
- **A procedure a value does not carry out is refused.** A call such as `Obj.Lock()` went through without a word
  when the value had not done it; now it raises "The procedure '…' was not carried out by this value". The
  calls that had been silently lost now work: `Lock()` and `Unlock()` of catalog, document, chart and job
  objects, and an export procedure of an object's module called from outside it.
- **A function value keeps only the variables it refers to.** It kept every variable of the procedure it
  was written in, for as long as it lived; one stored in a variable of that same procedure kept them
  until the application closed. A COM application among them did not exit after `Quit`. Now the variables it
  does not name are released when the procedure ends.
- **A function value written inside another one passed to `Where`, `Select` or `SelectMany` sees that one's
  parameter.** `a.SelectMany(Function(x) { return b.Select(Function(y) { return x + y; }); }).ToArray()`
  closed the application, or answered with `Undefined` for `x`.
- **An assignment lets go of what the expression made.** `x = New COMObject(…)` or `x = Obj.Member` left a
  second hold on the value until the procedure ended, so `x = Undefined` released nothing: an Excel stayed
  running after `Quit`. Now it is released at once, and so is an object taken on the way — `Excel.Workbooks`
  in `Excel.Workbooks.Count`.
- **`First`, `Last`, `Min`, `Max`, `Single` and `ElementAt` in a loop answer for this pass.** With a function
  passed to `Where` or `Select` before them, a pass that found nothing answered with the previous pass's
  value, and `Max` of a group was compared with the group before.
- **An encoding is a member of `TextEncoding` or a code page by its name.** `TextReader`, `TextWriter`,
  `BinaryData` and the HTTP bodies take `"UTF-8"`, `"UTF-16"`, `"windows-1251"` and `"cp866"` in any case
  (also `cp1251`, `IBM866`), read by the table XML uses, the same on every machine: a 1251 file reads right
  where the system's own page is not 1251. Any other string and another enumeration's member are refused;
  `TextReader` and `TextWriter` took them without a word.
- **`Sequences.<Name>.Restore(boundary, key)`.** It posts again, in sequence order and each in its own
  transaction, every registered document after its key's border, up to the boundary (a date, a moment
  or a `Boundary`; all when left out) and for the dimension values the structure names (every key when
  left out); the border follows each one. The first failure stops the run, keeps the border before that
  document and raises its error with the document's name. A sequence's list has a *Restore* command.
- **A query over the turnovers, or the balance and turnovers, of an accumulation or accounting register by
  `Recorder` or `Record` runs in the database.** They were read into memory, and with them everything the query
  does above them: the joins, the grouping and the totals. They are now one statement where the register's totals
  are kept (not over ODBC). Each movement opens at the balance the one before it closed at, and a key that carried a
  balance in and did not move is one row at the beginning of the interval, as before. An accounting register whose
  chart keeps a subconto by turnovers only still reads the balance and turnovers in memory.
- **Totals over several levels add up in the database on every engine.** `TOTALS … BY Group, Product` read every
  row into memory on Firebird, which has no `ROLLUP`, and wherever the query was ordered by a figure. One `GROUP BY`
  over the levels' fields now returns the bottom groups, and the groups above and the grand total add up from them,
  in the order the rows put them in. A sum, a count, a minimum or a maximum; an average, a distinct count or a
  `HAVING` over several levels still folds in memory.
- **`PictureLib.<Name>` is a picture a module can name** (`Pic = PictureLib.Logo;`): the configuration's
  own pictures and the engine's, the set the picture property's picker offers. A configuration picture
  named like an engine one is the configuration's.

## Forms

- **Where a button's picture stands, a button as a tile, a button as a hyperlink.** A button's new `PictureLocation`
  puts its picture on the left (`Auto`, `Left`) or the right of its caption, or makes it a tile (`Tile`): the picture
  large over the caption and the new `Note` under them - a price, a count, a state - the button filling what its
  sizer gives it: a button for a finger, in both clients. Tiles in a grid sizer make a panel of them. Its new `View`
  draws it as a button or as a hyperlink - no face and no frame, its caption underlined - pressed the same way. What
  the button shows is still its `Representation`. The protocol is 8. A form saved with them and opened by an earlier
  designer shows a button with its picture on the left, and saved there it loses its location, its view and its note.
- **A report on the start page, a cell's detail and a report an assistant shows build themselves as they open.**
  Whether to compose at once is said by whoever opens the form, not by the grid: the grid's `ComposeOnOpen`
  property is gone, and a form that had it set now waits for Compose.
- **Background jobs: a key, a description, and `BackgroundJobs`.** `RunBackground(method, parameters, key,
  description)` refuses a second job of the same method under a key that is still running.
  `BackgroundJobs.GetBackgroundJobs(filter)` and `FindByUUID(id)` find every run the application started —
  a background job or a pass of a scheduled one — while it runs and among the last 256 after it ended. A `BackgroundJob`
  says `State` (`BackgroundJobState`: `Active`, `Completed`, `Failed`, `Cancelled`), `Begin`, `End`,
  `Key`, `Description`, `MethodName` and `UUID`. `Cancel()` did nothing when called as a statement; it
  cancels now.
  The assistant's `code_status` and `code_cancel` answer from the same list, and their help says so: a code
  run is answered while it runs and among the last 256 that ended, and as unknown after that.
- **A scheduled job's last pass.** A row of `ScheduledJobs.Predefined` has `LastJob`, and a parameterized
  job's manager `LastJob(job)`: the `BackgroundJob` of its last pass here, `Undefined` if none ran. A
  predefined job the base has no settings for reads as declared; it read as switched on, hourly.

## The browser client

- **A grid sizer's cells are one size, as in the thin client.** Every column takes an equal share of
  the width rather than the width of its widest content, and every row is as tall as the tallest. An
  item that does not stretch keeps its own width at the start of its cell, as there; given `Rows`,
  the grid keeps that many rows, and with no `Cols` counts its columns from them. A column is never
  narrower than what it holds: a form too narrow for them all widens that one and scrolls.
- **A dark theme.** Tools → Theme... in both applications: light, dark, or as the machine is set — the person's,
  kept in the base, so it goes with them from one machine to the next. The browser paints itself with it from the
  login on, and follows the machine as it changes when the system's is chosen; the thin client keeps the choice now
  and paints itself dark in a later build. A base with no users keeps one theme for everybody. A colour a form names
  for itself is kept, and a control's pair with it is the light theme's - so yellow keeps its dark ink in the dark; a
  cell's fill named with no ink is read with dark ink. A printout is light.
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
- **Output list works**: the columns are asked as in the thin client, each ticked or not, with OK and Cancel, and the list
  opens in a tab as a spreadsheet. A question that picks one of a list offers its items to choose, then OK.

## Metadata

- **The attributes every object of a kind has are a group of their own.** *Predefined attributes*, under *Context*,
  holds Ref, Code, Description, Number, Date, Parent, Owner, Period, Recorder and the rest; they stood under *Common*
  among the name, synonym and comment. An object with none does not show the group.
- **A catalog's and a chart's code and description, and a document's number, take their type and length.** The
  *Predefined attributes* group of the properties shows each as a type: a code or a number is a string or a number,
  a description a string, each with its length. A tabular section's line number takes its length the same way, a
  number alone; it was Number(6). They were String(8),
  String(150) and String(11) and could not be changed. A new code or number is generated in that type and length;
  a predefined item's code and description are values of that type — a number code takes a number in the designer
  and over MCP (`0042` is 42), a description is cut to its length — and are written as a type chosen later holds them.
  A code or a description made shorter than the applied configuration has it, or a code turned between a string and
  a number, refuses the apply.
  Over MCP `metadata_get` shows each under the owner's property as a type, with the id `metadata_set_type` takes it
  by. A configuration saved before keeps the old types.
- **A length or a precision a field cannot hold is refused, not cut.** `metadata_set_type` placed a length of 70000 as
  4464 and 300 digits as 44; it now refuses past the designer's bound (1024 characters, 32 digits) and a scale above
  the precision, given in words or as a whole description. A type description read from a node refuses a length or a
  number of digits it would cut (past 65535 characters or 255 digits); a configuration saved before reads as it was
  written.
- **A report's composer is a template of the *Data composer* kind.** A template added to any object asks
  what it holds, a spreadsheet document or a data composer, the way a new form asks its kind; a common
  template is a spreadsheet. The kind is chosen once; the template's row in the properties reads it and
  opens its editor. The report's *Composers* group is gone: its composers are among its
  templates, and `GetTemplate(name)` returns a composer's composition, a new one each time. A configuration
  or an external report written before opens with its composers as such templates. Over MCP
  `metadata_create` with the kind `Composer` still makes one.
- **A predefined item declares what it is, not only what it is called.** A predefined characteristic
  of a chart of characteristic types declares its value `Type`; a predefined account declares its
  `AccountType`, whether it is `OffBalance`, the chart's accounting flags and its analytics — the rows
  of its `AccountDimensionKinds` section, each naming a predefined characteristic. They are kept in
  the configuration with the item and written by the apply, as its parent is: set only as data, they
  did not travel with the configuration. The analytics are unfolded into the account's kind columns
  as saving an account does, and an account that declares none keeps the rows its data holds. Over
  MCP: `predefined_add` takes `values` and `lines`, `predefined_set` changes them on an item already
  declared (`lines: {Section: null}` stops declaring a section), `predefined_get` shows one item whole and
  what it may declare — each attribute and column with the types or values it `takes`. In the designer a
  type, a reference and an enumeration are picked through "...": a characteristic's `Type` among its
  chart's types of characteristics, a declared value in the designer's window for one.
- **The apply refuses a predefined item that does not say what it must**, before a statement, naming
  the item in the ledger: an item with no code or description (whichever the object is presented
  by), a characteristic with no `Type` or with one its chart's types of characteristics do not take (a
  type deleted since included), an account with more analytics than its chart takes or a line
  naming a kind its chart of characteristic types no longer declares, a calculation type whose
  `Displacing`, `Base` or `Leading` line names no type the section may name, a new declared line
  where the base already holds one (remove the lines held there first). A line the configuration
  stops declaring stays, an ordinary line.

## Fixes

- **Typing a number code finds the item wherever it stands.** A catalog whose code is a number was searched among
  its first hundred items only; `12` now finds 12, 120 to 129, 1200 to 1299 and so on, through the code's index.
- **Output list names a column by its header**, the field's synonym, not by the field's name.
- **A document list grouped by *Point in time* shows its rows.** It showed nothing, and the server said
  "Invalid field type".
- **An accumulation register's turnovers, and balance and turnovers, by `Recorder` or `Record` leave out the lines
  that are not active.** They counted them, as no other reading of the register does.
- **The lock sweep does not wait for a key somebody is taking.** A client starting while the designer swept the lock
  keys held the sweep behind its module compile, and the sweep gave up the rest of the keys with "update conflicts
  with concurrent update". A key held at that moment is left for the next pass, and the others are swept.

- **`ThisForm.<attribute> = value` writes into the attribute.** It overwrote the attribute's own entry,
  and the attribute kept its old value.
- **A scheduled job switched off stays off.** Its `Active = False` was written to the base and read back
  as on, so the job came back on its schedule at the next start.
- **A breakpoint in an external processing's or report's module stops the run in the designer.** It was
  refused as a line the running application does not have.
- **Breakpoints moved by an edit are all kept when the module is saved.** Of two breakpoints moved by
  lines inserted above them, the base kept one.
- **Save in a module of an external processing or report writes its file.** It saved nothing to the
  file. The module is checked first, as a configuration's module is.

- **A module nobody has compiled yet is compiled once.** Sessions entering a base together, with the
  bytecode cache empty, each compiled the same modules. The first now holds a lock in the database,
  the others wait and read what it saved; if it fails, the next one compiles.

- **Sessions logging in together no longer bring the web server down.** Each visitor rewrote the MCP
  server's settings as if it owned the process.

- **A reference's quick choice drops under the field again**, in the thin client and in the browser, where it
  came up as a window of its own. The same goes for the list offered for text typed into a field. A choice closed
  without one picked no longer picks the first item, such as «Select data type».

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

- **The syntax helper finds a function by its job.** "replace substring" found nothing and "split string" did
  not find `StrSplit`: a word said only inside a name, `StrReplace`, was not indexed, and an entry had to carry
  every word asked. The words a name is made of are indexed now, and when no entry has all the words, those with
  the most of them answer. `mcp_search` with a tool's name for a query answers that tool, as `name` does;
  `code_run` was not among the verbs found for it.

- **MCP: `app_stop` ends the application started from here**, with the debugger or without it, at a breakpoint
  or running. Only `app_run`'s restart could end it, so an application stopped at a breakpoint held the base and
  `config_apply` waited behind it; its refusal now names `app_stop`.

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

- **A register or sequence a document is set to write to in the designer's properties takes it as its
  recorder.** The list changed in place, so the document was never added, and the save refused the
  sequence: "no recorder".

- **A refused save says why in Messages.** The designer showed "Failed to save the configuration." and
  nothing else; each object the save refuses, such as an enumeration with no values or a register with no
  recorder, is now a line there.

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
- **A fold that opens on a module's first line has its marker**, in the designer's code editor and in the code
  runner's. The editors wrote the fold levels from the second line on.
- **`ExternalDataProcessors.Create` and `ExternalReports.Create` no longer hand back a freed object.** A data
  processor with no form, or a report with neither a form nor a composer, unloaded at the end of its own start,
  and the call returned what was left of it; it now returns nothing.
