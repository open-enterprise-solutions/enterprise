# The browser client

**What.** The client a browser runs: the page the [web server](../ARCHITECTURE.md#the-web-server-webserverexe-srcenginewebserver)
serves at `/<publication>/`, talking to the base through the [client protocol](client-protocol.md) over the socket
beside it, `/<publication>/client`. Like [the thin client](thin-client.md), it runs nothing of the configuration.

**Why.** A person with a browser alone — another machine, a phone — works in the same base with the same forms. The
thin client is its reference: what the thin client does with a frame, the page does the same.

**No build.** The browser loads the modules as they are: no Node, no package manager, no bundler — one toolchain less
to keep beside the platform's own. Types are JSDoc, checked by the editor (`jsconfig.json`, `checkJs`). What the page
is built on lies in `vendor/<library>/<version>/` with its licence, MIT only: Web Awesome (the standard controls),
Tabulator (the grid). The controls are plain custom elements, `<oes-…>`; no framework keeps a second copy of the
server's tree.

**Where.** `src/engine/webserver/www/`. The web server serves it from `Root` (a folder: edit and reload), a release
from its pack `web/www.wpk`.

## Parts

| Folder | Holds |
|---|---|
| `src/protocol/` | the socket (OES-RPC packets — `packet.js` reads and writes them — deflated answers), the session — login, one call at a time, the frame and its patches, coming back after a drop — and the pictures |
| `src/window/` | the window: login, the section panel, the tabs, the messages, the questions, All functions |
| `src/controls/` | a form's controls, one file each, each its own element; the sizers; the label column |
| `src/tablebox/` | the table: the grid fed with the server's pages |
| `src/gridbox/` | the spreadsheet, drawn as a table |
| `styles/` | the palette, the window, the form — and the form folded for a narrow screen |

## Who does what

The form lives on the server. The page draws the frame's tree, applies its patches and sends what the person did; the
caret, the typed echo, the scroll, the keys and the measuring of the label column are the browser's. A control updated
by a patch is updated in place, so the focus and the caret stay where they were.

## What it guarantees

- **The thin client's rules.** The login first goes in with no name — a base with no users lets it in. A refused call
  is said in a box with the server's words; a session that ended closes the window. A dropped socket is opened again and
  the session taken back with `login {Token}` while the server keeps it; the call the drop left unanswered is sent again
  under its own id.
- **A question is never left standing.** The server's code waits where it asked; a question the page does not draw is
  answered as cancelled, and the person is told so.
- **A choice form opens as a tab**, as any form. Asked from a modal dialog — a report's settings — it opens over that
  dialog: the dialog blocks the form, so a tab beneath it could not be seen.
- **A form's command bar stays on top**; the controls under it fill the tab while they fit, what grows sharing what is
  left, and scroll under the bar when they do not — the thin client's layout. A table or a sheet takes the height the
  form gives it and scrolls its rows inside itself. A command bar keeps one line; what does not fit is under its ».
- **A sheet's cells are taken as in a spreadsheet program**: a click, a drag, Shift and Ctrl with a click, the arrows,
  Ctrl+A. Ctrl+C copies them as cells — a figure as a number — and the figures taken are summed in the sheet's corner:
  average, count, sum. A row folded away is neither summed nor copied.
- **The table** keeps its place: a list read again is read around its top visible row, paged both ways, a stale page
  dropped. A cell keeps the server's text; its editor has the column's buttons (… ▷ ×).
- **A table holds a window of rows, not the list** — the thin client's: a page is as many rows as the window shows,
  read when fewer than five are left past its edge, and the side left behind is let go, so a list scrolled to its end
  holds about two windows. Its scrollbar says only whether there are rows before and after (at the top, in the middle,
  at the bottom); the thumb let go at the very top reads the list from its first row.
- **A narrow screen** (under 40 rem): the sizers fold into a column, fields take the full width, touch targets are
  44 px and more, nothing is reached by hover alone, a table scrolls sideways inside itself — never the page.
- **A number's … opens a calculator, a date's a calendar**, dropped under the field, as the thin client's: the
  calculator counts in exact decimals (0.1 + 0.2 is 0.3), keys typed work as keys pressed.
- **Printing prints the active tab's content**, without the window around it; a sheet a script prints is drawn for the
  printer alone. A sheet's frozen rows repeat on every page; a row's print break ends its page.
- **Busy and offline are shown**: a call unanswered for 800 ms shows a spinner, a dropped connection a strip.

## Where it stops

- The questions drawn are the message box, the choice, the menu, the calculator and the calendar, the error and the
  print; the others are answered as cancelled.
- A printed sheet is answered as printed: the browser does not say whether its print dialog was cancelled.
- A number goes back from the calculator exact to 15 digits.
- No files: nothing is uploaded or saved from the page.
- A view that is no form — a text, a spreadsheet opened from a file — is not drawn.
- The page's own captions are English; the server's texts come translated.
