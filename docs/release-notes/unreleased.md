# Unreleased

Since [v2.0.0-preview.1](v2.0.0-preview.1.md).

## Breaking changes

- **Kinds, functions, rights and tools take plain English names, one word for each family.** A module, a query or
  an assistant's call that names an old one no longer compiles or is refused: rename it.
  - The configuration is the *application*: `CommonMetadata` → `ApplicationMetadata`, its module
    `ConfigurationModule` → `ApplicationModule`, `ConfigModuleManager` → `ApplicationModuleManager`, the right
    `UpdateDatabaseConfiguration` → `UpdateDatabase`. Over MCP `config_apply`, `config_check`, `config_save`,
    `config_rollback`, `config_export_files`, `config_import_files` → `app_apply`, `app_check`, `app_save`,
    `app_rollback`, `app_export_files`, `app_import_files`.
  - The *Common* branch is *Shared*: `CommonModule`, `CommonForm`, `CommonTemplate`, `CommonCommand`,
    `CommonAttribute`, `CommonAttributeColumn` → `SharedModule`, `SharedForm`, `SharedTemplate`, `SharedCommand`,
    `SharedAttribute`, `SharedAttributeColumn`; `GetCommonForm()`, `ShowCommonForm()`, `GetCommonTemplate()` →
    `GetSharedForm()`, `ShowSharedForm()`, `GetSharedTemplate()`.
  - A data processor is a *data tool*: `DataProcessor` → `DataTool`, `DataProcessors.<Name>` → `DataTools.<Name>`,
    `ExternalDataProcessor` → `ExternalDataTool`, `ExternalDataProcessors` → `ExternalDataTools`. Over MCP
    `external_processor_open` and `external_processor_save` → `external_data_tool_open` and `external_data_tool_save`.
  - A functional option is a *feature option*: `FunctionalOption` → `FeatureOption`, `FunctionalOptions.<Name>` →
    `FeatureOptions.<Name>`.
  - Exclusive mode is *single-user mode*: `ExclusiveMode()` → `SingleUserMode()`, `SetExclusive(on)` →
    `SetSingleUserMode(on)`, the right `ExclusiveMode` → `SingleUserMode`.
  - A value table's parts: `TableValueRow` → `TableRow`, `TableValueColumn` → `TableColumns`,
    `TableValueColumnInfo` → `TableColumn`.
  - The Visual-Basic-style syntax is stored as `ves`, not `vbs` (`ProgramSyntax.ves`).
- **A base, an application file or an application folder written before this build is not read.** The stored names
  changed with the ones above, and nothing reads the old ones: make the base again and load the application into it.
- **Each syntax compares and joins conditions in its own spelling.** CES compares with `==` / `!=` and joins with
  `&&` / `||` / `!`; VES with `=` / `<>` and `And` / `Or` / `Not`. A module that mixes them no longer compiles, and
  the message names the spelling to use: *'=' is not a comparison here - comparison is written '=='*.
- **A tabular section's line number is `LineNumber`**, as a register record's is, and both are shown as *No.* (a
  tabular section's was *N*, a register's *Line number*). It was `NumberLine`: rename it in modules and queries (`row.LineNumber`,
  `ORDER BY G.LineNumber`). An application saved before opens with the line number broken, and the next update refuses
  every line a predefined item declares: export its files with the engine it was saved by, rename `NumberLine` to
  `LineNumber` in them, and import them into a new base.

- **In a date pattern `MM` is the month and `mm` the minutes**, as everywhere else they are written;
  the seconds are `ss`. It was the other way round. A stored format such as `DF=dd.mm.yyyy` now
  prints the minutes where the month was, and nothing reports it: change `mm` to `MM` (and `MM` to
  `mm`, `SS` to `ss`) in the formats of attributes, form items and report fields. The built-in
  presets are already written the new way.
- **`SerializeValue` and `DeserializeValue` are now `JSONString` and `JSONValue`**, a pair with
  `XMLString` and `XMLValue`. A module that calls the old names no longer compiles: rename the calls.
- **A guid takes 16 bytes, and the packet is OES-RPC version 2.** Every guid a node holds — a
  reference's, a metadata object's, a `Guid` value — is its 16 bytes, not its text; XML and JSON
  still show the text. An application, a form or a message written by this build is not read by an
  earlier one (`the packet is OES-RPC version 2, this end reads 1`): update the thin clients with
  the server.
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
  written. Nothing is added to the application.
- **An assistant opens external data tools and reports.** In the designer, one it makes or one you
  open opens in a tab you see. Its tree, modules and forms are changed with the same tools as the
  application, and its Save hands the file back to the assistant, as Save hands it to you. In the
  application it runs one under your roles, with the *Data assistant* right. The file stays where the
  assistant keeps it: the base never reads or writes that disk.
- **Performance measurement.** *Debug → Performance measurement* times every client call and every line of script
  the attached application runs, in every session, until the item is unchecked. A tab then opens with the calls by
  kind — how often, how long, how much of it was not script, the bytes both ways — each with the lines it ran, then
  the lines no call ran, slowest first: the module, the procedure, the line, how often it ran, its own time and its
  share, and its time with what it called. Under each line are the queries it ran — reads, writes and deletes — with how much of their time went to
  the database and how much stayed in memory, and under a query the statements the database got, with their plan
  (Firebird) and the values of the slowest run, so that run can be repeated. Opening a database connection and
  waiting for a free one are rows of their own. What the platform did for no line of yours, such as the register writes of posting, is a row of its
  own. A column's header sorts by it; a double click opens the line. Checks above the tree choose what is drawn —
  calls, lines, queries, SQL, plans, values: a level unchecked hands its rows to the one above, the same statement
  met twice becoming one row, so SQL alone lists every statement of the measurement, the heaviest first. An
  assistant starts and stops the same
  measurement (`profile_start`, `profile_stop`), reads the last one again narrowed to a call, a module, a
  procedure or a query (`profile_read`), and opens it in your window only when it means to show it to you;
  `code_run` with `profile` answers with the measurement of the code it ran.
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
- **The value table an accumulation register's `Balance` returns names a resource's column by the resource**:
  `Quantity`, not `QuantityBalance`. A module that reads `QuantityBalance` from it raises "Table column not
  found": read `Quantity`. The query virtual table keeps `QuantityBalance` (#409).

## Application

- **HTTP services: an application answers an HTTP request.** A new kind, `HTTPService`,
  with `URLTemplate`s (`/Transfer/{id}/reset`, `/*`) and their `HTTPServiceMethod`s (Get, Post, …
  or Any), shown in the tree's new Integration folder, right after Shared. Registered among the base's web addresses, a
  request to `<publication>/es/<RootURL>/…` on the web server is answered by a function of the
  service's own module, given an `HTTPServiceRequest` and returning an `HTTPServiceResponse`. Each
  request is a session of its own, logged in by its `Authorization: Basic`, checked before the session
  is made. See [http-services.md](../public/http-services.md).
- **The example base is an English demo of trading, payroll and accounting** (`examples/fb_test_buh`, the demo
  server's base of that name). Goods are bought, moved and sold by lots (FIFO); payroll runs on a calculation
  register; two companies keep their books in two ledgers at once - the continental one, where a posting is a pair,
  and the Anglo-Saxon general ledger, where an entry is lines - over a chart of accounts each. The start page shows a
  dashboard (a data tool's form), the sales being worked through and two reports.

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
- **A table's `Columns` answer `Find(name)` and `Count()`.** `Find` gives the column of that name, in any case,
  or Undefined when the table has none, so whether a column is there no longer has to be tried by reading it.
- **`Name(…)` calls the procedure or function of that name even where a variable has it.** Names are read
  whatever their case, so `chapterNames = ChapterNames();` called the variable and failed with "value is not a
  callable function". The module's procedure or function, declared above or below, now wins, as a system one
  already did; a function value in a variable is called only when nothing of its name is declared.
- **`SetStatus` in a background job says where the job has got to.** With no window it reached nothing; it is
  now the job's activity, which Active Users and `code_status` show.
- **The module check names a method the manager, object or reference does not have.** `Catalogs.Products.CreateElement()`
  compiled and failed only when it ran, with "Aggregate object field not found". The designer's *Syntax control*, the
  check on save, `module_write` and `app_check` now say "… has no method 'CreateElement'" at its line. A value that
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
- **The registration journal is a query source.** `SELECT … FROM Journal.Events(&BeginOfPeriod, &EndOfPeriod)`
  reads its events — `Date`, `Level` (`info`, `warn`, `error`, `audit`), `Session`, `User`, `Host`, `Source`,
  `Event`, `Message`, `Object` (a reference of the application), `Details` (the value written with the event) —
  and joins them to the application's tables like any other: `JOIN Catalog.Goods AS G ON E.Object = G.Ref`. Both
  moments are inclusive and either may be left out. It is read by the right that opens the journal window, and
  `SELECT ALLOWED` reads a refusal as no rows.
- **`Data.From(sourceName, arguments…)`** gives LINQ any source a query can name, with its arguments:
  `from e in Data.From("Journal.Events", Begin, End) select e`, or a register's
  `"AccumulationRegister.Stock.Turnovers"`. `Data.From(valueTable)` is as before.
- **A source in `Data` holds the tables that live in it.** A register's virtual tables are read through the dot —
  `Data.AccumulationRegisters.Sales.Balance` whole, `Data.AccumulationRegisters.Sales.Turnovers(Begin, End)` with the
  arguments a query gives them — and so are a document's tabular sections, `Data.Documents.Sale.Goods`. The LINQ
  constructor shows them under their table.
- **`Data.Platform`** holds the sources the platform declares in every application — `Data.Platform.Journal.Events`,
  the whole registration journal — so the LINQ constructor's tree shows the journal. The query constructor shows it
  under the same heading, *Platform*, after the application's kinds; the query still writes `Journal.Events`.
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

## Forms

- **A field, its kind chosen.** The new *Field* control is bound like a text field, and its `Kind` says how it shows
  the value: `Input` is the text field, `RadioButtons` a button for each choice, the one the value holds selected. The
  choices are the field's: `True` and `False` for a boolean, the members of an enumeration, or its own `ChoiceList`, one
  value of its type a line. A click writes that choice into the field and raises `OnChange`; a field bound read-only
  keeps its selection. The same in the thin client and the browser. The radio button is unchanged and binds nothing.
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
  beside the application's name.
- **The start page**: the forms the application places on it, live, in its columns, with sashes to drag between them.
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

- **A base can answer more than its clients on a web server.** Its *web addresses* are a list kept in the base:
  each a path under the publication and the code that answers there. Every web server that publishes the base
  mounts them within seconds and hands each request to the base; **Administration → Web addresses...** in the
  designer edits the list, and over MCP `address_list`, `address_register` and `address_unregister`. Two
  kinds answer there: an application's HTTP service, and a person's assistant from the browser. `web.conf`
  bounds the requests: `ServiceWorkers`, `ServiceCalls` (503 past it), `ServiceWait` (504 past it).
- **An assistant works with the browser's session.** **Tools → Options…** in the browser is the thin client's
  assistant access page: *Let an assistant reach this session* starts it by itself at login, **Start now** and
  **Stop now** act at once. Running, the session answers MCP at an address beside the page,
  `<publication>/mcp/<…>`, published on the web server for as long as it runs, with a key of its own; the page
  shows the configuration block and the Claude Code command to copy. The browser keeps the setting, the address
  and the key for that person, so a connected assistant stays connected. The assistant window is in the
  browser too. Requests are POSTs: no event stream.

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
  A code or a description made shorter than the base holds it, or a code turned between a string and
  a number, refuses the apply.
  Over MCP `metadata_get` shows each under the owner's property as a type, with the id `metadata_set_type` takes it
  by.
- **A length or a precision a field cannot hold is refused, not cut.** `metadata_set_type` placed a length of 70000 as
  4464 and 300 digits as 44; it now refuses past the designer's bound (1024 characters, 32 digits) and a scale above
  the precision, given in words or as a whole description. A type description read from a node refuses a length or a
  number of digits it would cut (past 65535 characters or 255 digits).
- **A string can have no limit.** Its length 0 — *Unlimited length* in the properties, *Unlimited* in the type dialog —
  stores text of any size, never indexed. A query compares, orders and groups it only cast to a length:
  `CAST(Comment AS String(100))`, cut by the database. Made unlimited, a field keeps its values; given a length back,
  each keeps its first characters, and the apply says how many it cuts.
- **A refusal of a name in ORDER BY, TOTALS BY or INDEX BY says where the name is**, as everywhere else in a query.
  `INDEX BY` over a field the statement does not output is refused; before, it built no index and said nothing.
  `VALUE(...)` naming a metaobject or a member the application does not have is refused by the check too, not only
  when the query runs.
  Over MCP, `query_check` and `report_query` no longer call every refusal a missing name: a rule of the language
  (a field not grouped, a string of no limit compared bare) is reported as the refusal it is.
- **A constant can be picked in the query constructor again.** It offers its `Value` field, `SELECT *` reads it, and
  `SELECT Value FROM Constant.Rate` parses - a bare `Value` was taken for `VALUE(...)`.
- **A query finds by name what the source lists, and nothing else.** One list answers `SELECT *`, the query
  constructor and a field named in the text, for every source: a table, a reading of a register, a tabular section,
  a temporary table, a nested query. What follows from it:
  - a document's `PointInTime` reads under an alias — selected beside a joined table or out of a nested query it came
    back empty;
  - a register's `Balance` and `BalanceAndTurnovers` give in `SELECT *` the fields the constructor offers;
  - a register dimension whose name starts with `Period` (`PeriodKind`) stays in every reading; it was taken for the
    period at a granularity and left out of the ones that fold it;
  - a field of an accounting register kept per side is named by its sides, `QuantityDr` and `QuantityCr`, as the
    list shows it; the bare `Quantity` is refused;
  - a field of a temporary table or a nested query is found whatever the case of its name, as a table's is.
- **A string's allowed length and a number's sign are set beside the length.** *Allowed length* (Variable or Fixed —
  a fixed string is padded with spaces) and *Non-negative* in the properties and the type dialog. A non-negative number
  takes no value below zero: one written is 0, and the apply makes the stored ones 0 and says how many. A number is kept
  within its digits (`Number(5, 2)` holds 999.99), and the scale never passes the precision. Over MCP
  `metadata_set_type` takes `allowedLength`, `nonNegative` and `dateFraction`; what a call does not give, the field keeps.
- **A report's composer is a template of the *Data composer* kind.** A template added to any object asks
  what it holds, a spreadsheet document or a data composer, the way a new form asks its kind; a shared
  template is a spreadsheet. The kind is chosen once; the template's row in the properties reads it and
  opens its editor. The report's *Composers* group is gone: its composers are among its
  templates, and `GetTemplate(name)` returns a composer's composition, a new one each time. Over MCP
  `metadata_create` with the kind `Composer` still makes one.
- **A predefined item declares what it is, not only what it is called.** A predefined characteristic
  of a chart of characteristic types declares its value `Type`; a predefined account declares its
  `AccountType`, whether it is `OffBalance`, the chart's accounting flags and its analytics — the rows
  of its `AccountDimensionKinds` section, each naming a predefined characteristic. They are kept in
  the application with the item and written by the apply, as its parent is: set only as data, they
  did not travel with the application. The analytics are unfolded into the account's kind columns
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
  where the base already holds one (remove the lines held there first). A line the application
  stops declaring stays, an ordinary line.
- **An assistant arranges the start page.** Over MCP `home_page` reads it - the template, each column's forms and
  every form it can show - and sets a column or the template, as the designer's start-page dialog does.

## Forms

- **A form field says how its value is chosen, over what its attribute says.** A text field and a
  table column have `QuickChoice` (Auto — as the type says, Use, Don't use) and `ChoiceFoldersAndItems`
  (Auto — as the attribute's Select says, Items, Folders, Folders and items), and in the `Choice`
  category the attribute's own two: `TypeLink` and `ChoiceParameters`. A form's attribute has none:
  one reached through the dot cannot be edited, so the control is the one place a link on a form is
  set. The form is their source — they name its attributes and the fields of what each holds. The
  field's rows narrow the list over the bound field's: a row for the same parameter replaces that
  one, the others are added. A value written into the field is narrowed by its link by type. A value
  typed or chosen into a field or a cell empties the fields whose links name it, unless the row says
  Keep — the cell of a column in the row being edited; a module writing the attribute empties
  nothing. MCP:
  `form_set {control, property: "ChoiceParameters", value: {"Owner": "Counterparty"}}`, in the shape
  `metadata_set_choice` takes.

## Fixes

- **A catalog folder is written on Firebird.** Every folder was refused: *Incorrect values within SQLDA structure -
  empty pointer to data*. A folder's deletion mark was left empty, and the driver sent an empty value without
  saying it may be empty.
- **`||` works in CES.** Its second bar was read as the start of a continued string: *Constant string expected*.
- **A manager's `GetForm` opens the object's own form.** `Reports.Sales.GetForm()` opened an empty tab with no
  title: every manager made its `GetForm` through one generic road, without the object behind it, so the default
  form was never asked and a form laid out by the platform had nothing to lay out. Each now goes through its own
  door — the object form of a catalog, a document, a report, a data tool; the list of a register or an enumeration;
  an information register's record.
- **Two temporary tables join in one statement.** `SELECT … FROM A LEFT JOIN B ON …`, both made by `INTO` earlier in
  the package, was refused: *'' could not be copied into a base in memory*. The second table is read as a second
  reading of its own (the tables number their columns alike), and its rows came back under the first one's numbers,
  so the copy found none of its columns - and a plain condition on it was dropped as naming a column it does not have.
- **An aggregate over a table's rows names its column.** A query answers with a table, and `(from l in Goods select
  l.Amount).Sum()` added the rows themselves and answered `TableRow`. Rows are now aggregated by the column the call
  names - `q.Sum("Amount")`, as `Min`, `Max` and `Average` - or by a selector, `Sum(r => r.Amount)`; with neither, and
  over a column that is not added up (an item, a text), the call is refused and says why.
- **`skip` and `take` after `orderby` cut the order.** `from s in Stock orderby s.Quantity descending take 3` kept
  the first three rows read and then ordered them, so the answer was three rows from the middle. Modules are compiled
  again on first run (the bytecode format moved to 39).
- **The compiler says which word it expected.** Where a name was missing it said *Constant boolean expected*.
- **The compiler names what was meant.** A word of the language used as a name - `in = 5`, a parameter `from` - is
  refused as such; it was *Identifier expected*, or *Unexpected program code termination* two lines on. A loop
  written `for (i = 0; i < n; i++)` is told that a for counts with `To`. A call of a function that is not there names
  the global functions spelled similar to it: *StrFind - a global function with a similar name: Find*.
- **A committing run that caught a refused write names that write.** Its commit was refused with *rolled back at an
  inner level* and nothing more; the refusal it caught is now part of the sentence.
- **A register's Period says what it wants.** A record a document writes keeps its moment in every register, with
  periodicity or without, and the refusal of an empty one now says so: usually the document's Date.
- **A member a value does not have is refused naming the value's type**, and the members it has spelled similar to the
  one asked for: *'Amout' is not a member of Structure - a member with a similar name: Amount*. It was *Aggregate
  object field not found 'Amout'*, which said neither.
- **A lock refusal says what is locked, who holds it and since when**, and what frees it: *Goods receipt is open for
  editing by Application Workspace on OFFICE-7 since 19.11.2026 18:40:12. It is free when it is closed there or that
  session ends.* It named the object by an internal identifier, and a holder without a user as *another session*.
- **`code_run` says how a run answers** when its statements end in `Return`: the compiler refuses a `Return` outside a
  procedure, and the run's answer is what it assigns to `Result`.
- **Declaring one more line on a predefined item that already has lines in the base is applied.** The update stopped
  on *Field 'Row_RRRef' not found in the resultset*: the check that a declared line lands only where nothing stands
  counted the base's lines per item without reading which item they were.
- **What `Correspondence` chooses is said where it is chosen.** On, the continental books: a posting is a pair. Off,
  the Anglo-Saxon general ledger: an entry is lines, one account and one side each, balancing per recorder. Both are
  double entry; the description called the second one a special case.
- **An object is no longer reported as reordered after the database is updated.** `database_diff` kept listing it
  while the update had nothing left to do. The order compared is the author's: children of one kind among
  themselves, the built-in fields left out.
- **A template put into a document that names no language reads in your language.** `Put` and `Join` laid a
  template's captions down in the language written first for every user unless a script had set the document's
  `LanguageCode`; a document without one now takes the session's (#680). And an area taken from a template carries
  the template's `LanguageCode`, as the help always said: it was left behind, and setting it on the template changed
  nothing.
- **A tab shows `*` as soon as an assistant changes what it holds**, and drops it when the assistant saves. The
  mark came only with the next thing you did in the designer, so a saved external report could still show `*`.
- **A sequence with no dimensions moves its border.** Posting a document registered in it was refused: "an upsert
  into '…_Borders' names no key columns". Its one border is one row, replaced as the documents are posted.
- **Deleting a register no longer leaves its tables behind in the list of sources.** An accounting register with
  correspondence, or an accumulation register with balances, deleted and applied, kept `.DrCrTurnovers` (or
  `.Balance` and `.BalanceAndTurnovers`) in the query constructor's list; the next time the list was read, the
  designer closed.
- **One palette for every window.** Dialogs and panels are the theme's light surface, controls its powder; windows
  that set colours of their own (user lists, authorization, the designer's panels and lists, the code editor's
  margin) take the theme's instead, and no longer stand out.
- **The property inspector, the date and time pickers and the calendar are drawn by the theme**, as the other
  controls are: their colours, borders, expanders and check boxes, and a drop-down placed where it fits on the
  screen.
- **Ctrl+V, Ctrl+C, Ctrl+X and Ctrl+A in a field go to the field.** In the Watch window's name field the paste went
  to the module open beside it.
- **A spreadsheet with a row or column area keeps its scroll bars.** The cells were laid over them. The lines
  between the outline, the area and the labels are drawn, and a report with frozen rows keeps adding rows as it is
  scrolled down.
- **A tree's expander sits in its slot**, not on the edge of the cell.
- **In the type dialog a click on a type's name selects it; the box checks it.** A type could not be looked at — what
  it holds, its length — without being checked. The dialog is never narrower than the length, precision or date row,
  and a disabled drop-down no longer opens.
- **A field's caret stays in the field once the messages have opened.** The messages pane, opening as the first
  message came, gave the focus to its tabs while its editor was taking it; the editor went on believing it held the
  focus and moved the caret of whatever field was being typed in onto its own place — under the field's title.
- **A field reached by Tab has its text selected, and so has the value a rename dialog offers**, as in any Windows
  field: typing replaces it, an arrow keeps it. The caret stood at the start.
- **An event handler typed in the thin designer's inspector is kept.** The procedure was written into the module,
  but the event — a form command's Action, a control's event — went to the server with its old, empty handler ahead
  of the new one, and the server kept the empty one.
- **An accumulation register's `Balance` takes the dimensions and the resources** as its third and fourth
  arguments, comma lists: `Balance(, Filter, "", "Quantity")` is one line with the whole balance. A name that
  is not the register's refuses, naming it. It took two arguments and refused a third (#409).
- **A grouped query sorts by a field of a key of several reference types.** `GROUP BY T.Doc ORDER BY T.Doc.Date`, where
  `Doc` may hold one of several document types, came back in whatever order the server liked: a sort that walked on
  from a group key was not grouped by, and the statement dropped it without a word. The walk is now a key of the
  statement as well — the groups stay the same, since the key fixes everything it walks into (#410).
- **A thin designer in an application server gets single-user mode.** The server's own login counted as another
  session, so an apply was refused with "other sessions are connected" when nobody else was. A job
  running at that moment still refuses, and now says so: it ends by itself, and the request can be made again.
- **An apply that changes the structure asks for single-user mode before it starts**, not at its
  first table change tens of seconds in. With another session connected the apply is refused at once, and nobody
  can come in while it runs. An apply of modules and forms only still asks nothing (#404). Deleting
  an object the base holds counts as a change of structure from the start too: before the application was
  written, the deleted object's table was still counted.
- **A document's number and an attribute are not narrowed, as a catalog's code is not.** A number made shorter -
  bound to the numerator of a shorter number (#661) - and an attribute of a catalog or a document, its own or a
  tabular section's, given a shorter string or fewer whole or fractional digits (`Number(10,0)` to `Number(10,2)`)
  reached the base as an in-place type change, which Firebird refuses, and the apply failed on a physical column
  name. They are refused before the apply now, naming the object, the section and the field (#414).
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
- **On Firebird, a statement that fails with no transaction open says what Firebird said.** A duplicate in a
  unique index, for example, was refused with an empty description and a code of 0. And when one of several
  statements run together failed before the last one, the transaction they ran in was left open.
- **A column whose choice parameters take a value from a header field is emptied in every row when that field
  changes.** A contract column filtered by the header's counterparty kept its contracts when the counterparty
  changed, in a form and from a module alike; a row marked *Keep* still keeps its value.
- **Text typed into a field finds no more than its choice form shows.** The choice parameters — the attribute's
  and the field's own — narrowed the choice form but not the search by typed text or the quick choice, so a
  contract of another counterparty could be typed in.
- **The designer's choice parameters dialog shows the parameter picked in a row, and the form wizard opens with a
  kind checked.** A drop-down list showed its first item without choosing it, and a group of radio buttons came up
  with none on.
- **MCP: `form_set` of `ChoiceParameters` refuses a row the designer's dialog would not offer**, and names the
  parameters it would.

- **`ThisForm.<attribute> = value` writes into the attribute.** It overwrote the attribute's own entry,
  and the attribute kept its old value.
- **A scheduled job switched off stays off.** Its `Active = False` was written to the base and read back
  as on, so the job came back on its schedule at the next start.
- **A breakpoint in an external data tool's or report's module stops the run in the designer.** It was
  refused as a line the running application does not have.
- **Breakpoints moved by an edit are all kept when the module is saved.** Of two breakpoints moved by
  lines inserted above them, the base kept one.
- **Save in a module of an external data tool or report writes its file.** It saved nothing to the
  file. The module is checked first, as an application's module is.

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
  `app_apply` waited behind it; its refusal now names `app_stop`.

- **MCP: `app_close` closes the designer itself**, as when its window is closed: it asks whether to save a changed
  application and each unsaved tab, and the person answers; `force: true` asks nothing. Behind it, a session closed
  without force — a script's `EndJob(False)` as well — now closes the thin client and the browser as the person's
  [X] does, questions and all; it used to take the client off the server without asking. Such a close — and one
  from the taskbar — now waits while a dialog of the client's own is up: with the LINQ constructor open on a module,
  the designer closed the module's tab under it and fell.

- **MCP: a search of the pattern corpus is a fifth faster.** Its entries are cut into topics once, when the corpus
  is built, rather than on every search - `pattern_read` with a query, `mcp_search`, and the first call of each tool
  family, whose reminder searches the corpus.

- **MCP: the reminder on a family's first call points only where the word was found.** A word met only in the
  application's notes sent the caller to `syntax_search`, which knew nothing of it.

- **A value read from JSON no longer gains a `type` field.** The type's name the JSON view writes
  beside a value's type came back as a field, one more on each round.

- **A selection reads a row's standard attributes.** `Catalogs.X.Select()` and the selections of the
  charts published only `Ref` and the attributes a person added, so reading `Code`, `Description`,
  `DeletionMark` or `IsFolder` from a row was "field not found".

- **`app_check` names a register that the save would refuse**: a register with nothing in it, or one
  written by a recorder that no document posts into, sequences included. The save refuses the whole
  application over one of them, so every other change made in a session waited behind it, and the
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
  application's types, and the "..." opens the type picker and the declared-value window, as the desktop's did.
  A condition's value chosen there is the reference chosen, a predefined element too; the desktop's made every one
  of them the empty reference. A parameter's expression is checked again, as the desktop checked it: `1 + (` or a
  name nobody declares is said when the editor or the window is closed, where it went into the cell without a word.
- **A value declared in the designer reads as it was chosen when the composition is opened again**, where a
  parameter set to `CatalogRef.Goods.EmptyRef` or to a predefined element read as a number: its text was written
  under the name the value's type is written under, and the type was read in its place.
- **The thin designer does what the desktop did in these places**:
  - A row dragged onto another in the application tree is copied there.
  - Clear database closes the open tabs first. A modified tab asks to be saved, and a tab that stays leaves the
    base as it was.
  - A data tool or a report replaced from its file keeps its place in the tree.
  - Tools → Options takes effect in the editors already open and in the output window.
  - A picture's System list offers the class icons too.
  - A control's Source window names fields by their synonyms.
  - A web client started from the designer runs in the base's language.
  - On macOS, Start without debugging starts the thin client. The designer looked for a plain file and missed the
    client's bundle.
  - A composer has no "Composition data" row in the inspector.
- **MCP: a shared picture takes its image through `picture_set`** — `data` (base64 PNG) or `svg`, with a
  `name`. A `Picture` object could be declared with `metadata_create` and never given anything to show:
  `picture_set` refused it as having no picture of its own, and `metadata_set` on its `Picture` asked for
  a structure. The image is kept in the application, the picture is listed under `picture_list` →
  `application`, and `metadata_set` on that property now names `picture_set`.
- **A fold that opens on a module's first line has its marker**, in the designer's code editor and in the code
  runner's. The editors wrote the fold levels from the second line on.
- **`ExternalDataTools.Create` and `ExternalReports.Create` no longer hand back a freed object.** A data
  tool with no form, or a report with neither a form nor a composer, unloaded at the end of its own start,
  and the call returned what was left of it; it now returns nothing.
- **MCP: `form_set` refuses a name for a button's `Command`** and names `form_bind`, which binds a command by its
  name. Sent `{name: …}`, it answered with a binding nobody asked for: the button kept the command it had and its
  item type became a number no type carries. A command binding now reads only the shape it is written in, `hops`
  and `commandType`, and replaces what it held.
- **A data tool or a report read from its file into an application keeps its forms' bindings.** Its objects
  were given new ids, but its forms still held the file's: a button named the command of the object the file was
  saved from, a field its attribute. Read into the application it came from, the copy's form ran the original's
  command. The file is now read on its own and pasted in, as a copy made in the designer is pasted. And a pasted
  button names the pasted object's command — in the designer's copy and paste too, where it kept the original's.
- **On macOS a text field shows every letter typed whole.** The left edge of each new letter stayed unpainted, in
  the launcher's fields and in every other text field of the desktop programs.
- **On macOS a window and a dialog of the desktop programs have their title bar and frame.** The launcher's window
  and its connection dialog showed neither, and the dialog merged with the window behind it — it could only be
  left by saving the connection. The title bar's buttons now work there too.
- **On macOS the window's buttons do what a Mac's do.** The red one on the program's window puts it away and
  the program stays in the Dock — its icon brings the window back, Cmd+Q quits; the yellow one sends the window
  to the Dock; the green one takes it full screen and back, with Option it fits the window to the screen. On
  every system the three buttons are drawn smooth.
- **On macOS closing a dialog no longer crashes the program** when the focus is in an empty field with a hint.
- **On macOS menu icons stand level with their items' text.** On a Retina screen each icon sat half a line above
  its label, and the first one was cut by the menu's edge.
- **MCP: `app_apply` says what it applied.** Its `changes` counted the structure work, so an apply of data
  tools, reports, modules or forms, which live in no table, answered `changes: 0` with an empty ledger
  while it applied them. `changes` and `objects` are now the objects the apply carried, in `database_diff`'s
  words; the structure work stays in `ledger`, `structureChanges` long.
- **MCP: `code_run` says it needs the debugger.** It reaches an application only over the debugger's connection,
  and an application started with `app_run {debug: false}` was refused as "no application is attached - app_run
  starts one", so a second application was started beside the first. The refusal now says that the running one has
  no debugger and names `app_run` with `restart: true`; `app_run`'s `debug` says which verbs need it. And `app_run`
  no longer starts a second application beside one it started without the debugger: it asks for `restart: true`,
  as it did beside a debug session.
- **MCP: an object created and deleted before the application is written leaves no trace in `database_diff`.**
  Its parents read "changed" and the change read as structural until the application was written; the same
  count was in `app_apply`'s `changes`.
- **A missing property named like a global function is reported as a missing property.** `Doc.Date` or `P.Type`
  on a value that has no such property said "'Date' is a global function - it is not a member of this value",
  which read as `.Date` being taken for a call of `Date()`. That sentence is now kept for a method call such as
  `x.ValueIsFilled()`; a read or a write says the field is not found.
- **A line in Messages opens its module again.** Double-clicked, an error from a module opens that module at its
  line, an external data tool's or report's too, a new one not yet saved included. The thin designer did not; the
  desktop's did not for a file never saved.
- **A watch expression that fails says why.** Its row read `<error>` and nothing more: it now reads
  `<error: …>` with the reason, and a breakpoint's condition that fails is reported with it.
