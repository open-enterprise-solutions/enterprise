# The client protocol

**What.** How a client works with a base: the thin runtime, the thin designer, a web server, a webview client, an
assistant. The server holds the session, its forms, its documents and all the code; the client draws what it is
answered and sends what the person did.

**Why.** One platform, many clients. The code runs on the server only, so no client keeps a model of its own in step
with the platform — what differs between clients is how they draw.

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
| `event` | `Client`, `Control`, `Event`, `Args`, `Form` | the frame |
| `respond` | `Client`, `Id`, `Response` | the frame |
| `fetch` | `Client`, `Control`, `Request`, `Form` | a table's rows, a spreadsheet's cells |
| `activate`, `close` | `Client`, `Tab` (the tab's id) | the frame |
| `upload` | `Client`, `Name`, `Data` — then `Client`, `File`, `Data` for each next part | `File` |
| `download` | `Client`, `File`, `Part` | `Name`, `Part`, `Parts`, `Data` |
| `open` | `Client`, `File` | the frame — the file opened as a document |
| `command` | `Client`, `Command` | the frame — the command done on the active tab's document |
| `presentation` | `Client`, `Value`, `Format` | `Text` — the value as a person reads it, formatted by the server (`Format`: the codes `Format()` takes) |

A method name is never changed; a new method takes a new name. Every number below is the protocol's: never
renumbered, a new one takes the next. A field a side does not know is skipped; an absent field is its default — so
the protocol grows by new fields, methods and numbers, never by a new format.

## The frame

Every answer that is a frame is numbered: `Frame`. A call may name the frame the client holds — `Since`, the number
it was last answered with — and is then answered with `Patch`, what turns that frame into the one drawn now, instead
of the whole. A call without `Since`, or naming another number, gets the frame whole. The events — `Messages` and
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

A field's `State` carries `Text`, as shown, and `Value`, the value with its type: `{"t": <type id>, "type": <name>,
"v": <text>}`, a reference `{"t", "m", "g"}`. An `event` that changes a field (`Change`) gives either: `Value` is
taken as it is, `Text` is parsed by the field's type.

| Field | Holds |
|---|---|
| `Title`, `Status` | the client's window |
| `Schemas` | the schemas its application offers |
| `Templates` | the documents it opens from a file: `Title`, `Mask` |
| `Menu` | its menu, as a tree |
| `Tabs`, `ActiveTab` | the tabs, each by its id (`NodeId`, given when it opens, never another tab's): `Title`, `Icon`, `Locked` (the start page — first, never closed); the active one by its id |
| `Messages`, `Clear` | messages to the person since the last answer — events, never in a patch |
| `Request` | a question to the person, while one is pending |
| `View` | the active tab drawn: a form — its `Key` and its controls as the designer saved them, each with a `State` of what it shows now; or the start page |

## Schemas

A schema is a dialog of the desktop held as data: the client shows it however it likes.

| Schema | In | Shows | Commands | Right |
|---|---|---|---|---|
| 1 All functions | the runtime | the objects the person may open, grouped by kind, each by its `Item` | 1 Open `{Item, Type}` | the mode of all functions |
| 2 Active users | both | `Sessions` and `Locks` | — | active users |

A schema the client's application does not have, or the person has no right to, is refused.

## Questions to the person

| Kind | Asks | `respond` with |
|---|---|---|
| 1 Message | `Text`, `Caption`, `Style` | `Button`: 2 Yes, 8 No, 4 OK, 16 Cancel |
| 2 Choice | `Caption` and items: `Id`, `Caption`, `Picture`, `Selected` | `Id` — one of those offered |
| 3 Help | `Title`, `Text` | nothing |

The server's code waits for the answer where it asked. While a question is pending, only `respond` is accepted.

## Files

A file the client hands over is kept in the session's temporary storage, in parts of at most 1 MB, and named by an
id. `open` opens it by the template its name matches, as the desktop opened a file from disk; a document saved goes
back to the same id, and `download` takes it. The files are the session's: another session never sees them, and they
go when the session does.

## Menu and commands

The menu is a tree. An item is a separator; a command (`Command`, `Title`, `Shortcut`, `Enabled`) — done with
`command`; a schema (`Schema`, `Title`, `Enabled`) — asked for with `schema`; or a submenu.

| Command | Key |
|---|---|
| 1 Undo | Ctrl+Z |
| 2 Redo | Ctrl+Y |
| 3 Save | Ctrl+S |
| 4 Close | — |

A key is written `Ctrl`/`Alt`/`Shift` + key; the client maps it to its platform (Ctrl to Cmd on a Mac). `Enabled`
answers the question a call of the item is refused by: the active document for a command, the person's right for a
schema.

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
| 423 | a question to the person is pending — respond to it first |
| 500 | the server could not do it |

An HTTP status answers only what comes before the protocol: no base at the address (404), a notification (204).

## What it guarantees

- **Code never runs on the client.**
- **One call of a client at a time.** It is answered once its work settles: done, or a question to the person pending.
- **A refusal changes nothing.** An unknown schema, command, item or file, a command the active document does not take
  now, a part over 1 MB — refused before any work is done, with the reason.
- **What a menu offers is what a call lets through** — both ask the same question.

## Where it stops

- A call is answered once; a call repeated after a dropped connection is done again — the dropped socket ends its
  clients anyway.
- A file travels in base64 inside JSON.
- The platform's own captions are in the server's language, not the session's.
