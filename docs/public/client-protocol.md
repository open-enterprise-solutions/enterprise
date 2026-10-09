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
file base needs no server: the client opens it itself and makes the same calls in process. Clients logged in over a
WebSocket go when it closes.

A WebSocket opened with `?compress=deflate` gets every answer of 1 KB and more as a binary message: the JSON, raw
deflate (RFC 1951 — a browser's `DecompressionStream("deflate-raw")`). A text message is the JSON as it is.

## Methods

| Method | Parameters | Answer |
|---|---|---|
| `login` | `User`, `Password`, `Mode` (1 runtime — the default, 2 designer), `Protocol` (the newest version the client speaks, 1 if absent) | `Client`, `Protocol` (the version both speak), `Features` (what the server offers beyond it — none yet), and the frame |
| `logout` | `Client` | — |
| `frame` | `Client` | the frame |
| `schema` | `Client`, `Schema` | what the schema shows |
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

A method name is never changed; a new method takes a new name. Every number below is the protocol's: never
renumbered, a new one takes the next. A field a side does not know is skipped; an absent field is its default — so
the protocol grows by new fields, methods and numbers, never by a new format.

## The frame

Every answer that is a frame is numbered: `Frame`. A call may name the frame the client holds — `Since`, the number
it was last answered with — and is then answered with `Patch`, what turns that frame into the one drawn now, instead
of the whole. A call without `Since`, or naming another number, gets the frame whole. The events — `Messages`,
`Clear`, `Pictures` — come beside either: they are what happened, not what the frame is.

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

A tab's `Kind` says what its view shows when it is no form (2 a text, 3 a spreadsheet). A spreadsheet document's
`View` is a gridbox's node — its `State` with `GridLines` set — and its sheet is fetched by the tab (`fetch {Tab}`);
with no `Version` the sheet is read once. A gridbox's `fetch` gives the whole sheet, `Sheet`, in the form a template is
stored in — `cells` (`row`, `col` from 0, `value` as shown), `rows` and `cols` (`areas`, `breaks`, `sizes`, `groups`),
`freeze` — the `Version` it was read at, and `ReadOnly` when the document takes no edits; a cell or a field absent is
the default. A sheet edited on the client goes back whole, in the same form: event `Change {Sheet}`.

A picture in the view — a command's, a button's, a column's — is named by its id: a backend picture's number, as a
decimal, or a configuration picture's guid. The answer that first names it gives it in `Pictures`, once per client;
the client keeps it for the session. A picture with no id of its own (one held as a file's bytes) comes as itself, a
PNG in base64, as do a tab's `Icon` and the pictures of schemas and requests.

A field's `State` carries `Text`, as shown, and `Value`, the value with its type: `{"t": <type id>, "type": <name>,
"v": <text>}`, a reference `{"t", "m", "g"}`. An `event` that changes a field (`Change`) gives either: `Value` is
taken as it is, `Text` is parsed by the field's type.

A table's column dragged in its header is `Move {Column, Holder, Position}`: `Holder` the group it now stands in, 0 the
table. The client moves the column in its own copy of the form at once; the server's form follows.

| Field | Holds |
|---|---|
| `Title`, `Status` | the client's window |
| `Schemas` | the schemas its application offers |
| `Templates` | the documents it opens from a file and makes anew — the client offers these and no others: `Title`, `Mask`, `Extension` (`open` without a file names it), `Picture` (by its id), `OnlyOpen` (it makes no new one) |
| `Menu` | the application's menus, as a tree — beside the client's own File and Edit |
| `Tabs`, `ActiveTab` | the tabs, each by its id (`NodeId`, given when it opens, never another tab's): `Title` (a modified document's ends with `*`), `Icon`, `Locked` (the start page — first, never closed), `Commands` (the commands its document takes now), `Formats` (what its document saves as: `Title`, `Mask`), `File` (its document's file, once it has one), `Parent` (the tab whose document owns this one's — raised under it, closed with it); the active one by its id |
| `Messages`, `Clear` | messages to the person since the last answer — events, never in a patch |
| `Pictures` | the pictures the frame names that the client was not given yet: `Id`, `Picture` (PNG, base64) — an event, never in a patch |
| `Exit` | the client is to go: Exit done, every tab closed — an event, never in a patch |
| `Request` | a question to the person, while one is pending |
| `View` | the active tab drawn: a form — its `Key` and its controls as the designer saved them, each with a `State` of what it shows now; or the start page |

## Schemas

A schema is a dialog of the desktop held as data: the client shows it however it likes.

| Schema | In | Shows | Commands | Right |
|---|---|---|---|---|
| 1 All functions | the runtime | the objects the person may open, grouped by kind, each by its `Item` | 1 Open `{Item, Type}` | the mode of all functions |
| 2 Active users | both | `Sessions` and `Locks` | — | active users |
| 3 Sections | the runtime | the section panel: `Sections` with their `Blocks` of items | 1 Open `{Item, Type}` | — |
| 4 About | both | `Build`, `Picture`, `Database`, `Application`, `User`, `Locale`, `Plugins` (`Name`, `Version`) | — | — |

A schema the client's application does not have, or the person has no right to, is refused.

## Questions to the person

| Kind | Asks | `respond` with |
|---|---|---|
| 1 Message | `Text`, `Caption`, `Style` | `Button`: 2 Yes, 8 No, 4 OK, 16 Cancel |
| 2 Choice | `Caption` and items: `Id`, `Caption`, `Picture`, `Selected` | `Id` — one of those offered |
| 3 Help | `Title`, `Text` | nothing |
| 4 Edit | `Command` (7–11): done to the field under the client's focus | nothing |
| 5 File | `Caption` and items: `Title`, `Mask` — a file of the person's chosen and uploaded | `File` — its id; none — cancelled |
| 6 Generation | `Picture` and items: `Id`, `Caption`, `Picture` — the objects this one generates | `Id` — the one to base a new one on; none — cancelled |
| 7 ViewMode | `Picture`, `Mode` (0 tree, 1 hierarchical, 2 list) — the one in force | `Mode` — the one chosen; none — cancelled |
| 9 Menu | items: `Id`, `Caption`, `Checked`, `Enabled` (false — greyed) and their own items — a submenu; a popup menu where the mouse is | `Id` — the item picked; none — dismissed |
| 8 SavedSettings | `Mode` (0 save, 1 restore), `Key`, `Default` and items: `Id`, `Caption` — the person's saved settings | `Act` (1 save, 2 restore, 3 default, 4 rename, 5 remove), `Id`, `Name`; none — closed |
| 10 FormEditor | the active tab's form, as the frame has it: `Picture`, `HasSetting`, `Result` (the act before's: 0 done, 1 no storage, 2 no address, 3 refused), `Classes` (`Name`, `Picture`) | `Act` (1 apply — with `Controls`: each `Control`, its `Position`, the properties a person may arrange; 2 save; 3 reset); none — closed |
| 11 ListSettings | a table's settings: `Settings` (the setting in force, in the node form the settings schema writes), `Fields` (`Name`, `Presentation`, `Id`, `Type`, `Available`), `References` (`Type` — a class id as text, `Targets` — the metaobjects it points at), `Error` | `Settings` — the setting edited; none — cancelled |
| 12 SimpleChoice | `Value` — a field's number or date, with its type | `Value` — the one chosen; none — dismissed |

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

| Command | Key | Done |
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

A key is written `Ctrl`/`Alt`/`Shift` + key; the client maps it to its platform (Ctrl to Cmd on a Mac). `Enabled` and
a tab's `Commands` answer the question a call is refused by: the active document for a command — that tab's document
for its `Commands` — the person's right for a schema.

## Sessions

A client's session is a thin client in the runtime and a designer in the designer, whatever process hosts it.
Active users shows both columns: the application (Runtime, Designer, Application server, …) and the kind (Thin client,
Client, Server, Job).

## Refusals

A refused call is a JSON-RPC error: `code` says why — a client branches on it — and `message` says it to a person.
The code is in the body, never an HTTP status: a WebSocket's messages and a call in process have none. The numbers
are HTTP's by meaning; where two reasons share one, the method tells them apart.

| Code | Why |
|---|---|
| 400 | a parameter missing or malformed |
| 401 | `login`: the user or the password; any other call: no such client, or its session is gone — log in anew |
| 403 | no right to it; `login`: the application refused to start |
| 404 | no such schema, command, item, tab, file, part or request |
| 409 | not now: the active document does not take the command |
| 500 | the server could not do it |

An HTTP status answers only what comes before the protocol: no base at the address (404), a notification (204).

## What it guarantees

- **Code never runs on the client.**
- **One call of a client at a time.** It is answered once its work settles: done, or a question to the person pending.
- **A refusal changes nothing.** An unknown schema, command, item or file, a command the active document does not take
  now, a part over 1 MB — refused before any work is done, with the reason.
- **What a menu offers is what a call lets through** — both ask the same question, a tab's `Commands` too.

## Where it stops

- A call is answered once; a call repeated after a dropped connection is done again — the dropped socket ends its
  clients anyway.
- A file travels in base64 inside JSON.
- The platform's own captions are in the server's language, not the session's.
