# The client protocol

**What.** How a client works with a base: the thin runtime, the thin designer, a web server, a webview client, an
assistant. The server holds the session, its forms, its documents and all the code; the client draws what it is
answered and sends what the person did.

**Why.** One platform, many clients. The code runs on the server only, so no client keeps a model of its own in step
with the platform — what differs between clients is how they draw. The desktop one is [the thin client](thin-client.md).

**Where it is written.** Every name and number below is written once, in `src/engine/protocol/protocol.h` — a header
that depends on nothing, included by the server (`frmserver`), the thin client and the web server alike. A number there
is never renumbered.

## Transports

| Transport | Where | For |
|---|---|---|
| WebSocket `ws://host:port/<base>/client`, one JSON-RPC 2.0 message per call | an application server | thin clients, web servers |
| HTTP `POST /<base>/client`, one JSON-RPC request per call | an application server | whatever cannot hold a socket |
| In process: `ibClientHost::Call(method, params, result, refusal, error)` | the client's own process | a file base; the assistant's MCP tool (`client_call`) |

The address is `oes://server[:port]/<base>`; the port is 7373 unless the server's `server.conf` says otherwise. A
file base needs no server: the client opens it itself and makes the same calls in process. A thin designer's
WebSocket, and a thin runtime's when `backend.conf` `Resume` is 0, ends its session when it closes. A thin runtime
with the window on (left out, 120 seconds; 0 is off; never more than 30 minutes) keeps the session that long, and
comes back with `login` `{Token}`.

A WebSocket opened with `?compress=deflate` gets every answer of 1 KB and more as a binary message: the JSON, raw
deflate (RFC 1951 — a browser's `DecompressionStream("deflate-raw")`). A text message is the JSON as it is.

## Methods

| Method | Parameters | Answer |
|---|---|---|
| `login` | `User`, `Password`, `Mode` (1 runtime — the default, 2 designer), `Protocol` (the newest version the client speaks, 1 if absent), `Features` (what this client accepts; `resume` opts into the slot below), `Read` (the schemas the client reads right after its start, each `{Schema}`) | `Client`, `Protocol` (the version both speak), `Features` (what the server offers; `resume` when a dropped thin runtime is kept), `Token` (that runtime only, once), `Read` (each schema of the login's `Read` the server built, as `schema` answers it, with its `Schema`), and the frame |
| `login` | `Token` and no `User`, only while that client is detached | the same `Client`, `Protocol`, `Features`, and no frame and no new `Token`. The next `frame` `{Since}` is the patch |
| `users` | — (before the login, no `Client`) | `Users` — the names the login window offers |
| `logout` | `Client` | — |
| `frame` | `Client` | the frame |
| `schema` | `Client`, `Schema`, `Args` (what of it, when the schema takes it) | what the schema shows |
| `schema` | `Client`, `Schema`, `Command`, `Args` | the frame — the schema did it |
| `execute` | `Client`, `Command` (an object's metadata id), `Type` (100 default, 150 create, 151 list, 152 select) | the frame — the object opened as the navigation opens it |
| `event` | `Client`, `Control`, `Event`, `Args`, `Form` — or `Tab` for a tab's view that is no form | the frame |
| `respond` | `Client`, `Id`, `Response` | the frame |
| `fetch` | `Client`, `Control`, `Request`, `Form` — or `Tab` for a tab's view that is no form | a table's rows, a spreadsheet's sheet |
| `activate`, `close` | `Client`, `Tab` (the tab's id) | the frame |
| `upload` | `Client`, `Name`, `Data` — then `Client`, `File`, `Data` for each next part | `File` |
| `download` | `Client`, `File`, `Part` | `Name`, `Part`, `Parts`, `Data` |
| `open` | `Client`, `File` or `Extension` | the frame — the file opened as a document; without a file, a new document of the template that opens `Extension` (File → New) |
| `command` | `Client`, `Command` | the frame — the command done on the active tab's document |
| `presentation` | `Client`, `Value`, `Format` | `Text` — the value as a person reads it, formatted by the server (`Format`: the codes `Format()` takes) |
| `picture` | `Client`, `Pictures` — the ids | `Pictures` — each `Id`, `Picture` (PNG, base64; empty when there is none of that id) |

`login`'s `Read` saves the start's own reads: the designer names the debugger's store (`Schema` 15), which was a
call of its own at every start. A schema the server does not know, the person has no right to, or that could not be
built is left out of the answer, and so is everything while the start waits on a question; an older server answers no
`Read` at all. Either way the client reads what is missing with `schema`, as before — no protocol version is needed.

A method name is never changed; a new method takes a new name. Every number below is the protocol's: never
renumbered, a new one takes the next. A field a side does not know is skipped; an absent field is its default — so
the protocol grows by new fields, methods and numbers, never by a new format.

## The frame

Every answer that is a frame is numbered: `Frame`. A call may name the frame the client holds — `Since`, the number
it was last answered with — and is then answered with `Patch`, what turns that frame into the one drawn now, instead
of the whole. A call without `Since`, or naming another number, gets the frame whole. The events — `Messages`,
`Clear` — come beside either: they are what happened, not what the frame is.

A patch is a node of the frame's shape carrying only what changed. A client applies it to its copy:

- `NodeRemoved` names the entries gone, taken out first; then an entry it carries is set — merged into the copy's
  own when both are nodes;
- children with ids (`NodeId`: a form's controls, the tabs): one the copy has is merged into it, one it has not is
  inserted whole; `NodeRemovedIds` names those gone, taken out first; `NodeOrder` gives the order of all of them
  whenever it changed or something was inserted;
- children without ids come whole when anything among them changed: `NodeChildrenWhole`.

From protocol 2 a patch's `View` is taken from the view the client was last sent for the tab active now — back on a
tab it gets what changed there, not the whole form. A client keeps each tab's view, puts the one of the tab the patch
makes active in place (keeping the view it holds when it has none for that tab), applies the patch's `View` to it,
and begins the store again with a frame sent whole.

A tab's `Kind` says what its view shows when it is no form (2 a text, 3 a spreadsheet, 4 the registration journal).
A text's `View` is a textbox's node, its `State` the `Text` and `ReadOnly`; help is such a tab, read-only. The
journal's rows are fetched by the tab, newest first: `fetch {Tab, Request}` with the filter `From`, `To` (ms since the
epoch, 0 — no bound), `Level`, `User`, `Session`, `Source`, `Event`, `Ref`, `Search` (a part of the message), `Offset`,
`Count`; each row `Time`, `Level`, `Session`, `User`, `Host`, `Source`, `Event`, `Message`, `Ref`, `RefMeta`. A row's
object is opened by event `Open {Ref, RefMeta}`. A spreadsheet document's
`View` is a gridbox's node — its `State` with `GridLines` set — and its sheet is fetched by the tab (`fetch {Tab}`);
with no `Version` the sheet is read once. A gridbox's `fetch` gives the whole sheet, `Sheet`, in the form a template is
stored in — `cells` (`row`, `col` from 0, `value` as shown), `rows` and `cols` (`areas`, `breaks`, `sizes`, `groups`),
`freeze` — the `Version` it was read at, and `ReadOnly` when the document takes no edits; a cell or a field absent is
the default. A sheet edited on the client goes back whole, in the same form: event `Change {Sheet}`. A report's gridbox
says in `ComposeState` what its sheet is to the report: 0 composed (or nothing composed yet), 1 composing — the sheet
on show stays until the new one is delivered, 2 outdated — the setting changed since the sheet was built, and it is
composed again when the person says so (Compose). When a composed sheet's `Version` is new, that frame's `State` may
also carry `Sheet` (and `ReadOnly`) — the same as `fetch` would give — so the client need not fetch; a server that
does not put `Sheet` in the state is answered with `fetch`, as before.

In the designer a report's composer is a tab of `Kind` 14. Its `View` holds `State` (`ReadOnly`), `Composition` —
the composer's description in its node form — and `Fields`, `References`, `Error` — what its query offers, as the
ComposerSettings question has them. The client edits the description itself and sends it back whole: event
`Change {Composition}`; the configuration is modified, and the answer's `View` carries the fields of the query the
composition now has. The fields of another text are fetched by the tab: `fetch {Tab, Request: {Kind: 21, Text}}` —
`Fields`, `References`, `Error`; the fields of a reference's targets — `fetch {Tab, Request: {Kind: 22, Targets}}` —
`Expanded` and `References`, as a ListSettings window asked again has them.
A form designer tab answers the same two requests for an attribute of type DataComposition: its inspector row
`ibPropertyDataComposition` carries the composition as its `Value`, the client opens the settings window over it,
asks the form's tab for the fields (`Kind` 21, 22) and sends the edited composition back through the attribute's `Set`.
An attribute of type DynamicList is edited the same way: its `ibPropertyDynamicList` row opens the list settings
window (filter, sort, grouping, conditional appearance, its query) over the list's composition. The window asks the
form's tab `Kind` 23 (Source) `{Id, Text}`: the fields of the list's main table (`Id`, its table id; omitted: none) and
those its query text adds (`Text`, optional), answered as `Kind` 21 answers (`Fields`, `References`, `Error`); a
reference opened is `Kind` 22. Its `ibPropertyDynamicSource` row carries the sources the configuration registers as
choices (`Id` the table id, `Title` namespace.name); picking one is the row's `Set`, after which the list is rebuilt over
that table. A list with no source keeps no settings in the form, as in the desktop designer.
A calculation register's `ibPropertyCalcSchedule` row carries, besides the registers it may name as its choices, the
lists its sub-rows choose from for the register bound now as an optional node `Schedule`: children with `Kind` 1 (a
resource that may be the value), 2 (a dimension that may be the date) and 3 (a dimension to link, its own children the
fields of the calculation register that may answer for it), each with `Id` and `Title`. The edited description goes
back through the row's `Set`, and the rows of the answer carry the lists for the register and date now bound. A client
that reads no `Schedule` shows the row read-only; a server that writes none leaves it so.
A field's `ibPropertyChoiceLink` (link by type) and `ibPropertyChoiceParameters` rows carry what their windows ask of
the configuration, as optional nodes. `Targets`, on both: the field's reference types, each `Id` (its metaobject),
`Title`, `Type` (the class its reference is registered under, as a link keeps it) and `Picture`; on the parameters row
each has as children the fields a parameter may fill (`Id`, `Title`, `Picture`). `Sources`, on the parameters row only:
`Title` the object or tabular section the field stands in, and as children the fields beside it that may supply a
value (`Id`, `Title`, `Picture`), each with as children (`Id`) the parameters it could fill, the likeliest first, and
`Default` the one that is obvious (absent: a real choice). The edited description goes back through the row's `Set`. A
client that reads none opens the link's window without the question of which type it decides and does not open the
parameters' window. A description's `path` is read whether it arrives as a field (as written) or as a property (an
object read from JSON).

A picture is named by its id everywhere — in the view, a tab's `Icon`, a schema, a request, a list of choices: a
backend picture's number, as a decimal (a class's own: its class id), or a configuration picture's guid. No answer
carries the picture itself:

- the platform's own pictures are compiled into the client — it never asks for them;
- any other it asks for with `picture`, the ids of a whole list in one call, and keeps for the session — an id there
  is none of too, so it is not asked again. Before it draws an answer, the client asks for every picture the answer
  names at once (its `Picture`, `Icon` and `…Picture` fields): the designer's start is one `picture` call, not one for
  the templates and another for the tree;
- only a picture with no id (one held as a file's bytes) comes as itself, a PNG in base64.

A field's `State` carries `Text`, as shown, and `Value`, the value with its type: `{"t": <type id>, "type": <name>,
"v": <text>}`, a reference `{"t", "m", "g"}`. An `event` that changes a field (`Change`) gives either: `Value` is
taken as it is, `Text` is parsed by the field's type.

A table's `fetch` gives rows, each with `Container`, `Group` (a group's caption), `Picture`, and its cells as children
by column id: `Text`, `Checked` for a flag, `Number`, `Value` when the cell takes an edit; its look, when the server's
model set one — `TextColour`, `BackgroundColour` (a colour's text, `r,g,b`, as a form's colours), `Bold`, `Italic`,
`Strikethrough`, `Underlined`, `Face`, `Size`, `Align` (`Left`, `Center`, `Right`). In the designer (`login` `Mode` 2)
a table reads no rows — its `fetch` is refused, and a designer's client does not ask.

From protocol 4 a fetch from the start (`Direction` 0) may name `Before`, the rows wanted ahead of the row it starts
at; the answer gives those rows first and says in `Before` how many came. A server before protocol 4 gives none: the
page comes alone.

From protocol 5 a table's `State` may carry `Rows` with a new `Version` — the first page, the same shape as `fetch`
(rows as children, `End`, `Before`, `Pictures`) — so the client need not fetch to draw the tab; a server that does
not put `Rows` in the state is answered with `fetch`, as before.

A later `Version` (a cell's `Change`, a row added, a refresh) may carry `Rows` too, read around the server's current row:
`Anchor` that row, `Count` the rows from it, `Before` how many came ahead of it (fewer than `Count`: the top was
reached). A page read from the top says `Before` 0 and no `Anchor`. The client answers its own read anew from it when
the read names the same `Anchor` and asks no more, or — when the page is the whole level, the top reached and `End` —
around any row of it, cut out as the server would read it. Any other read is a `fetch`, as before. The fields are those
of a `fetch` answer, so a client that does not look at them takes the page only for a read that matches it.
A designer's module tab (its `View` `State`: `Syntax`, `Module`, `Modified`, `ReadOnly`) carries the module's `Text`
in the frame that opens the tab, once; the client draws it without a `fetch`. A server that puts no `Text` there is
asked with `fetch`, as before. A form's tab reads its module's text as it did.

A table's column dragged in its header is `Move {Column, Holder, Position}`: `Holder` the group it now stands in, 0 the
table. The client moves the column in its own copy of the form at once; the server's form follows.

A notebook's tabs stand where `OrientPage` says; its `State` names the page selected, `ActivePage` (a page's control
id). A page picked is event `Page {Page}`, a page's tab dragged `Move {Position}`. A page shown in the notebook carries
its `Title` and `Picture`; a hidden one is out of it. A slider's ends are `MinValue` and `MaxValue`, a gauge's full
mark `Range`; an HTML box's `State` has the `Page` it shows, a radio button's `Caption` and `Selected`. A chart box is
drawn empty.

| Field | Holds |
|---|---|
| `Title`, `Status` | the client's window |
| `Schemas` | the schemas its application offers |
| `Templates` | the documents it opens from a file and makes anew — the client offers these and no others: `Title`, `Mask`, `Extension` (`open` without a file names it), `Picture` (by its id), `OnlyOpen` (it makes no new one) |
| `Menu` | the application's menus, as a tree — beside the client's own File and Edit |
| `Tabs`, `ActiveTab` | the tabs, each by its id (`NodeId`, given when it opens, never another tab's): `Title` (a modified document's ends with `*`), `Icon`, `Locked` (the start page — first, never closed), `Commands` (the commands its document takes now), `Formats` (what its document saves as: `Title`, `Mask`), `File` (its document's file, once it has one), `Parent` (the tab whose document owns this one's — raised under it, closed with it); the active one by its id |
| `Messages`, `Clear` | messages to the person since the last answer — events, never in a patch |
| `Exit` | the client is to go: Exit done, every tab closed — an event, never in a patch |
| `Properties` | the inspector's rows (as `Schema` 10 shows them) of each object a door acted on since the last answer, each by its `NodeId` and `Tab` (the tab whose tree holds it; absent: the configuration's) — an event, never in a patch |
| `Request` | a question to the person, while one is pending |
| `View` | the active tab drawn: a form — its `Key` and its controls as the designer saved them, each with a `State` of what it shows now; or the start page |

A designer's form editor tab is opened by the frame that opens it: its view's `State` carries `Opening` once —
what the tab's `fetch` with `Request {Kind: Text}` answers (the form, the classes, the module's text). The client
makes the editor from it without a `fetch`; a frame without `Opening` (an older server, a tab opened before) is read
with `fetch`, as before.

## Events held until the next call

From protocol 3. A **state event** says where the person is: `Focus` (1), `Input` (4), `Move` (11), `Row` (12),
`Resize` (15). Every other event is something done and goes at once.

- A control's `State` may carry `Subscribed`: the state events the server is subscribed to on it, by number —
  something there runs on them. These go at once.
- Any other state event is held by the client: the latest per control and event (`Move` and `Resize` per column), in
  the order last done.
- A table subscribes to `Input` on a cell only as it does to `Row` — when a selection handler is hung on it. Without
  one, a cell's `Input` is held and rides with the `Change` that commits the edit.
- A tab's text handed back — a module's `Change {Text}`, as the focus leaves the editor or before Save, a syntax
  check, a close — is held too, the latest per tab: what reads the text on the server is a call, and carries it.
- A form designer's form handed back (`Change {Form}`) is held the same way, apart from the text of the same tab.
- Every call but `login` and `logout` carries what is held as `Pending`, a list of `{Control, Event, Args, Form | Tab}`,
  and the client clears it. The server applies `Pending` in order before the call itself.

```
event {Client, Control: 12, Event: 3,
       Pending: [{Control: 7, Event: 12, Args: {Row: 841}}, {Control: 9, Event: 1}]}
```

So a walk down a list with the arrow keys is one record, and the code a press runs reads the row the cursor stands
on. A client that holds nothing stays correct, only chattier; a server below 3 never gets `Pending`.

## Schemas

A schema is a dialog of the desktop held as data: the client shows it however it likes.

| Schema | In | Shows | Commands | Right |
|---|---|---|---|---|
| 1 All functions | the runtime | the objects the person may open, grouped by kind, each by its `Item` | 1 Open `{Item, Type}` | the mode of all functions |
| 2 Active users | both | `Sessions` and `Locks` | the designer's: 5 Kick `{Session}`, 6 Reload `{Session}`, 7 Release `{Lock}` | active users |
| 3 Sections | the runtime | the section panel: `Sections` with their `Blocks` of items | 1 Open `{Item, Type}` | — |
| 4 About | both | `Build`, `Picture`, `Database`, `Application`, `User`, `Locale`, `Plugins` (`Name`, `Version`) | — | — |
| 5 Registration journal | both | nothing | 1 Open — opens its tab (`Kind` 4) | active users |
| 6 Debugging | the runtime | `Debugging` — the session's process listens for the designer; `Allowed` — it may be switched now (not on an application server, not while a start waits for the designer) | 2 Start, 3 Stop | — |
| 7 Assistant access | the designer | the person's MCP server: `Enabled`, `Address`, `Port`, `Token`, `Running`, `Endpoint`, `Allowed` | 2 Start `{Enabled, Address, Port, Token}`, 3 Stop, 4 Save `{…}` | — |
| 8 Users | the designer | `Users` (`Guid`, `Name`, `FullName`), and what a record is chosen from: `Roles` (`Guid`, `Name`, `Title`, `Mode`), `Languages` (`Guid`, `Title`, `Code`); with `Args {Guid}` — that one's `User` (`Guid`, `Name`, `FullName`, `Password` — whether one is stored, `Language`, `Roles`) | 4 Save `{Guid, Name, FullName, Language, Roles[, Password]}` — the password hashed by the server, absent: kept; 8 Remove `{Guid}` | data administration |

A schema the client's application does not have, or the person has no right to, is refused.

What a schema shows is read once for what the person does: a dialog opened by a schema's menu item is drawn from that
item's answer, and the client asks again only after one of the schema's commands changed what it shows (assistant
access: Options is one exchange; Start now is the command and one read of the token and endpoint it settled on).
The inspector's rows of an object a door acted on come as the event `Properties` (above), and the client takes them in
for that object instead of reading them with `schema`: after the inspector's Save (`Schema` 10, `Command` 4 — an edit
is one exchange), after the configuration tree's Create (`Schema` 9, `Command` 2 — the new object) and Remove
(`Command` 3, which takes `Select`: the id the tree selects after it), and when a form editor tab opens over a form of
the configuration. When the work waits on a question (a form's type), they ride the answer to the `respond` that lets
it finish. A server that sends none is read with `schema`, as before.

A schema read changes nothing and moves no frame number, so the client keeps the inspector's rows it received or read
while the number stays: clicking an object again, or walking back to it, before anything else was done is not a call.

## Questions to the person

| Kind | Asks | `respond` with |
|---|---|---|
| 1 Message | `Text`, `Caption`, `Style` | `Button`: 2 Yes, 8 No, 4 OK, 16 Cancel |
| 2 Choice | `Caption` and items: `Id`, `Caption`, `Picture`, `Selected` | `Id` — one of those offered |
| 4 Edit | `Command` (7–11): done to the field under the client's focus | nothing |
| 5 File | `Caption` and items: `Title`, `Mask` — a file of the person's chosen and uploaded | `File` — its id; none — cancelled |
| 6 Generation | `Picture` and items: `Id`, `Caption`, `Picture` — the objects this one generates | `Id` — the one to base a new one on; none — cancelled |
| 7 ViewMode | `Picture`, `Mode` (0 tree, 1 hierarchical, 2 list) — the one in force | `Mode` — the one chosen; none — cancelled |
| 9 Menu | items: `Id`, `Caption`, `Checked`, `Enabled` (false — greyed) and their own items — a submenu; a popup menu where the mouse is | `Id` — the item picked; none — dismissed |
| 8 SavedSettings | `Mode` (0 save, 1 restore), `Key`, `Default` and items: `Id`, `Caption` — the person's saved settings | `Act` (1 save, 2 restore, 3 default, 4 rename, 5 remove), `Id`, `Name`; none — closed |
| 10 FormEditor | the active tab's form, as the frame has it: `Picture`, `HasSetting`, `Result` (the act before's: 0 done, 1 no storage, 2 no address, 3 refused), `Classes` (`Name`, `Picture`) | `Act` (1 apply — with `Controls`: each `Control`, its `Position`, the properties a person may arrange; 2 save; 3 reset); none — closed |
| 11 ListSettings | a table's settings: `Settings` (the setting in force, in the node form the settings schema writes), `Fields` (`Name`, `Presentation`, `Id`, `Type`, `Available`, `Picture` — the column's, by its id, where the source describes itself; none — the attribute picture), `References` (`Type` — a class id as text, `Targets` — the metaobjects it points at), `Error` | `Settings` — the setting edited; none — cancelled |
| 12 SimpleChoice | `Value` — a field's number or date, with its type | `Value` — the one chosen; none — dismissed |
| 13 ComposerSettings | a report's settings: `Settings` (the setting in force), `Composition` (what the report arranges, in its node form), `Fields` and `References` as ListSettings has them — the fields its query offers, no `Id`; `Error` | `Settings` — the setting edited; none — cancelled |
| 14 Error | a script's failure: `Picture`, `Text` — the error with its call stack | `Act`: 1 close the window, 2 and send it to the designer debugging the base, 3 and close the client — each does the one before it too; none — 1 |
| 15 Print | a script's sheet: `Sheet`, as a gridbox's `fetch` gives it | `Done` — printed; false — the print dialog cancelled, or the printer failed |
| 16 Schedule | a form's schedule: `Schedule`, in its stored form | `Schedule` — the one edited, and the form is modified; none — cancelled |

A SavedSettings window stays open through its acts: each act is a `respond`, done by the server, which asks again with
the shelf as it now stands and `Done` — how the act went — until the window answers with no `Act`. A FormEditor window
does the same: each act is done and asked again with `Result`.

A ListSettings window asks on the way too: `Act` 1 Expand with `Targets` — a reference field opened — is asked again
with `Expanded`, the fields of those targets (one per name, its type the union of theirs; shaped as `Fields`,
`Presentation` the synonym), and their `References`. The setting answered is read against the configuration and
checked: one refused is asked again as it was given, with `Error`; one taken becomes the table's user setting, and its
rows are read again. `Act` 2 Choose — a condition's value of a type of the server's (`Type`, `Value` held, `Choice` —
the client's number for it): the server opens its quick choice, or its choice form — a tab like any other. The value
chosen comes back in the window's question, asked again or said anew, as `Chosen` (`Choice`, `Value` with its
`Presentation`).

A ComposerSettings window acts the same way. The setting taken becomes the report's user setting; the report is not
composed again — its gridbox says `ComposeState` 2 until the person composes it.

A SimpleChoice is a number's calculator or a date's calendar, dropped under the field pressed. The server asks it from
the field's Select, after the field's choice event — the event runs on the server, the client only draws.

The server's code waits for the answer where it asked. A call made while a question is pending runs inside that
wait; which calls a person can make meanwhile is the client's to say.

## What the server says unasked

A client's frame may change without a call of its: a report composed in the background is delivered, an idle handler
runs, work nobody called asks the person something. The server then sends the client's connection a JSON-RPC
notification — no id, no answer:

| Method | Params | The client |
|---|---|---|
| `changed` | `Client` | asks for its frame (`frame`, with `Since`) |

A connection that stays open (a WebSocket, a file base in the client's process) is told; an HTTP request is not — it
asks.

What a call's own work changes is not told: its answer carries it. A question the work asks while the call is
answered — a confirmation answered No — comes in that answer, with no `changed` and no empty frame after it; a change
made after the answer's frame was drawn (a report composed in the background) is told as before.

## Files

A file the client hands over is kept in the session's temporary storage, in parts of at most 1 MB, and named by an
id. `open` opens it by the template its name matches, as the desktop opened a file from disk; a document saved goes
back to the same id, and `download` takes it. The files are the session's: another session never sees them, and they
go when the session does.

## Menu and commands

The menu is a tree. An item is a separator; a command (`Command`, `Title`, `Shortcut`, `StockPicture`, `Enabled`) —
done with `command`; a schema (`Schema`, `Title`, `Enabled`) — asked for with `schema`; or a submenu.

**The routine is the client's, the document is the server's.** File, Edit and the toolbars are the client's own — the
desktop's doc/view on the thin client: it enables them by a tab's `Commands` and asks nothing for that, keeps the
files opened last, shows the file dialogs and the print preview. What is done to a document goes to the server: Undo,
Redo, Save with `command`, a tab closed with `close`, a file opened with `upload` and `open`. Cut, Copy, Paste, Delete
and Select all are done by the field under the client's focus. Closing the client's window is Exit.

| Command | Default key | Done |
|---|---|---|
| 1 Undo | Ctrl+Z | the active document |
| 2 Redo | Ctrl+Y | the active document |
| 3 Save | Ctrl+S | the active document |
| 4 Close | — | the active tab |
| 5 Open | — | a File request, the file opened by its template |
| 6 Exit | — | every tab closed, then the `Exit` event |
| 7 Cut, 8 Copy, 9 Paste, 10 Delete, 11 Select all | Ctrl+X, C, V, —, A | an Edit request |
| 12 Save as | — | the active document saved as a file of `Name`, written as the name says — the tab's `File`, taken down with `download` |

`StockPicture` names one of the client's own pictures, drawn its platform's way: 1 New, 2 Open, 3 Save, 4 Save as,
5 Close, 6 Quit, 7 Undo, 8 Redo, 9 Cut, 10 Copy, 11 Paste, 12 Delete, 13 Find, 14 Print.

**A key from the server is a default.** The keys a person presses are theirs, kept by the client in their own
directory and put over the defaults; the server keeps none. A key is written `Ctrl`/`Alt`/`Shift` + key; the client
maps it to its platform (Ctrl to Cmd on a Mac). `Enabled` and
a tab's `Commands` answer the question a call is refused by: the active document for a command — that tab's document
for its `Commands` — the person's right for a schema.

## Sessions

A client's session is a thin client in the runtime and a designer in the designer, whatever process hosts it.
Active users shows both columns: the application (Runtime, Designer, Application server, …) and the kind (Thin client,
Client, Server, Job). A thin client whose socket has dropped stays in the list for the resume window, and its
activity says `detached until HH:MM:SS` — the moment the window ends — so an administrator can end it.

## A dropped connection

`backend.conf` key `Resume`, seconds. Absent is 120. 0 is off: the drop ends the session, its locks and its seat at
once. A value above 30 minutes is 30 minutes. The window is a thin runtime that logged in on a WebSocket. A designer,
an HTTP client, and `Resume` 0 get no `Token` and no `resume` feature.

While the window is armed, a call that names `Client` is 401. An HTTP client is the login that arrived with no
connection; a null connection on a client that had a socket is the drop, and it does not admit another socket.
`login` `{Token}` binds the new socket and returns the
same `Client` and no frame. The next `frame` `{Since}` is the patch. A token presented while the client is still
attached is 401 and does not move the session. An unknown, malformed or expired token is 401. While attached, a
WebSocket call whose connection is not the one that logged in is 401, including one that repeats the last id.

`Token` is 32 bytes from a CSPRNG, returned once, on the password `login`. The server stores SHA-256 of it and
compares that digest in constant time. The answer of `login` `{Token}` does not carry a new token.

### The slot

Only a client that named `resume` in `login`'s `Features`, and only when the server offers the window.

- **Stored:** the JSON-RPC id, SHA-256 of the method and the params, and the response text (a result or a refusal).
  One slot on that client. Not written to the base. Not replaced by `login` `{Token}`.
- **Keyed by** the client and that id.
- **Cleared** when a newer id is answered (the slot then holds that call), and when the session ends. A detached
  401 is not stored.
- **Same id, same method and params:** the stored text is returned and the call is not run.
- **Same id, different method or params:** 400, not run, and the stored text stays.
- **An id lower than the last one answered:** 400, not run.
- **A notification** (no id) is never deduplicated.
- A client has one outstanding call. Ids are unique and increasing across reconnects; the counter is not put back
  to 0. The lost call is sent again under its original id, after `login` `{Token}` under a newer id.

## Refusals

A refused call is a JSON-RPC error: `code` says why — a client branches on it — and `message` says it to a person.
The code is in the body, never an HTTP status: a WebSocket's messages and a call in process have none. The numbers
are HTTP's by meaning; where two reasons share one, the method tells them apart.

| Code | Why |
|---|---|
| 400 | a parameter missing or malformed |
| 401 | `login`: the user, the password, or a token that is unknown, expired, or presented while the client is still attached. Any other call: no such client, a session that is gone, a detached client, or a connection that is not the one logged in — log in anew |
| 403 | no right to it; `login`: the application refused to start |
| 404 | no such schema, command, item, tab, file, part or request |
| 409 | not now: the active document does not take the command |
| 500 | the server could not do it |

An HTTP status answers only what comes before the protocol: no base at the address (404), a notification (204).

A message that cannot be read as a request — not JSON, JSON nesting more than 256 objects and arrays (the
outermost counted), or no `method` in it — is answered with JSON-RPC's own parse error, `-32700`, and an `id` of
`null`, over a WebSocket and over HTTP alike. It is refused before anything else is looked at, the client and its
session included.

## What it guarantees

- **Code never runs on the client.**
- **One call of a client at a time.** It is answered once its work settles: done, or a question to the person pending.
- **A refusal changes nothing.** An unknown schema, command, item or file, a command the active document does not take
  now, a part over 1 MB — refused before any work is done, with the reason.
- **What a menu offers is what a call lets through** — both ask the same question, a tab's `Commands` too.

## Where it stops

- A call is answered once. A client that did not name `resume` in `login`'s `Features`, and a session with no
  window, runs a repeated call again. A dropped socket with the window off, or a designer's, ends its session.
- HTTP `POST` is not bound to a connection. The `Client` id remains its bearer. The window and the token are not
  offered there.
- A file travels in base64 inside JSON.
- The platform's own captions are in the server's language, not the session's.
