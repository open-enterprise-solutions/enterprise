# The thin client

**What.** The desktop client that runs nothing of the configuration: a window, its document/view skeleton and its
editors, talking to a base through the [client protocol](client-protocol.md). The runtime's is `enterprise-thin`; the
thin designer will stand on the same library.

**Why.** The configuration's code runs on the server only, so one platform serves many clients. The client keeps the
routine the desktop window always kept — menus, toolbars, what may be pressed, file dialogs, recent files, print
preview — so the server is not asked for it on every move.

## Parts

The desktop's `frontend` carries two things at once — the forms' logic and their drawing. Here they are split, with the
protocol between them; neither half is called a frontend.

| Part | Holds |
|---|---|
| `protocol` | the contract — `protocol.h`: every name and number of the wire, depending on nothing — and the library of the sides that talk it: the wire's node, the connections (a server's socket, a file base in process), the communicator, the protocol's journal |
| `frmserver` | the server side: sessions, forms, the configuration's documents, the client host. It includes the contract only |
| `core` | under both the engine and the client: ids and value kinds, string, number, date, guid, the node with its JSON and binary forms, the refusal (`ibCoreException`), the translation engine (`ibLocalization`); of wx, the base only |
| `frmclient` | the client's graphics library: the main window, the document/view skeleton, the controls, the editors. It links `core` and `protocol` |
| `fileserver` | a file base in the client's process — the engine loaded beside the window |
| `enterprise-thin` | the runtime's window and its document manager |

Chain: core → backend → frmserver ⇄ protocol ⇄ frmclient → enterprise-thin; `fileserver` = backend + frmserver in the
client's process. `protocol` does not link `core` — `protocol.h` stays dependency-free.

A web server will link `protocol` as `frmclient` does, and tell a browser the same names and numbers.

`frmclient/backend/` is a reduced copy of what the engine's `backend` keeps above `core`: the same names and classes, only
what the client's windows use. Its `backend_core.h`, `backend.h`, `backend_exception.h` just include `core`.

| Copied | For |
|---|---|
| `formatString` | the formats of numbers and dates |
| `typeDescription`, `backend_type`, `system/value/valueType` | a type, the type factories, a value fitted to its type |
| `compositionDescription`, `system/value/composition/valueComposerField` | the settings schema; a composition field as a value |
| `metaCollection/partial/reference/reference.h` | a facade: `ConvertToMetaIds` answered from the server's `References` |

The wire meets the engine's node at one seam, `serialize/dataProtocol.h` (`ibReadProtocolNode`, `ibWriteProtocolNode`),
through the engine's own JSON — what is a field, a property, a child is the engine's rule. Next into `core`: the settings
vocabulary and the type description, then the sheet description, then the composition description with a stored value.

## Who does what

| On the client | On the server |
|---|---|
| File and Edit, the toolbars, what may be pressed (a tab's `Commands`), file dialogs, files opened last, print preview, the tabs | forms, the start page, external data processors and reports: their code, their data |
| the file dialogs, the upload of a file the person picked | texts and spreadsheets opened from a file, external data processors and reports, and what a form shows — drawn by the client from the frame's `View` |
| cut, copy and paste in the field under the focus | save, undo, redo and close of its documents |

## The document/view skeleton

The desktop's document/view, without its metadata part. Four classes take other names — `ibFrontendDocument`,
`ibFrontendView`, `ibFrontendDocManager`, `ibFrontendDocTemplate` — because a file base puts the server's own in the
same process, and wx keeps one class table.

- **A server's tab is a bridge document.** One invisible template makes one for each tab the frame has. It holds the
  tab's id and nothing of the document: its title, and whether it may be saved, undone or closed now, are read from the
  tab; save, undo, redo and close are calls.
- **A file goes to the server, by two calls.** `upload` puts it into the session's temporary storage and `open` makes
  the document of it by its id — the server works with ids, not files. An external data processor or report runs its
  form; a text or a spreadsheet is the server's document of its own template. The client's document goes once it has
  done that — what was opened is the server's, in a tab of its own.
- **A view is the kind of what is shown.** The document is the bridge whatever the tab holds; the view decides how it is
  drawn, and brings its own menu and toolbar.

## Forms

- **A value is local or the server's.** A primitive — a flag, a number (exact, `ibNumber`), a date, a string, an
  enumeration member — is held as it is. A value of a server's type — a reference, a configuration's enumeration — is
  the node the server wrote (`{t, v}`), carried and given back untouched. The client has no runtime, no database and no
  configuration: it is never the designer, and no configuration is active in it.
- **The form's tree owns its controls**, as the desktop's: `ibValueFrame` is a value with the property helper; a parent
  holds its children and their order. The host finds a control by its node id and owns none.
- **A column dragged moves in the tree at once**, and goes to the server as `Move`.
- **The form editor** opens over the tab's document window; Apply, Save and Reset answer its request (`FormEditor`).
  The form's root carries its own properties (`Title`, `Orient`, …); the tree's captions are the server's.
- **The desktop's windows, drawn here, decided there.** The list settings window (`ListSettings`, a table's Filter
  command) and the calculator and calendar (`SimpleChoice`) are the desktop's, copied. The client draws them over what
  the request carries; the server reads, checks and puts on what comes back.

## Spreadsheets

A sheet — a file's or a report's — is the server's document. The client's view draws it with the desktop's grid editor:
the sheet comes whole, in its node form, and an edit goes back whole (`Change {Sheet}`); what is saved is the server's.

## Printing

Print, Preview and Page setup are the document/view's own steps. A server's document is printed from a PDF the server
makes: a browser has no other way to print, and every client then prints the same pages.

## A file base

The same protocol, in process. Client and engine share one process: one debugger steps from the window through the
protocol into the server's form, a crash leaves one dump with both sides, and the journals — the window's, the
protocol's, the engine's — lie side by side. The protocol is never shortcut there — what holds over a file base holds
over a server.

## What it guarantees

- **No code of the configuration runs on the client.**
- **The routine is not asked for.** A menu is opened, a button enabled, a file chosen without a call.
- **The client keeps no second copy of the server's data.** A table fetches what it shows; a sheet is the one on show,
  and what is saved is the server's.

## Where it stops

- A document saved on the server is saved there; the person's file it came from is written back only by a download.
- A web client has no document/view of its own: it draws the same frame its own way.
- Not yet in the list settings window: choosing a reference value in a filter (next: the server's choice form in a
  window over the settings window); the presentation of a server-typed value already in a saved setting; the "any
  reference" families in the type picker.
