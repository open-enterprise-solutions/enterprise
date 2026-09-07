# Open issues

What is known to be wrong or unfinished, so the next iteration inherits a list
rather than a surprise. Dated when first recorded.

**Fixed since:** *a second session cannot open a form* (recorded 2026-09-05,
fixed 2026-09-07) — the open-document registry was process-wide and the second
session found the first one's document. See
[session-scoping.md](session-scoping.md). *The web server dies during session
teardown* (recorded 2026-09-05, fixed 2026-09-07) — see
[tab-ownership.md](tab-ownership.md). And *closing a tab with a live stream
aborts the server* (2026-09-07, same day) — see
[live-updates.md](live-updates.md).

## Blocking the next iteration

Nothing. The teardown crash that stood here is fixed —
[tab-ownership.md](tab-ownership.md) has the finding and what it cost.

## The session a page load throws away

**Every navigation mints a session nobody uses.** *2026-09-07.* `GET /` creates a
session and hands its id back as a cookie, but the client keys off its own
`sessionStorage` id and never adopts that one: login, forms and every XHR go to
the client's id, and the cookie session sits there unauthenticated until the idle
sweep takes it. Found while reading why an `F5` reported `tabCount=0` for a
session that plainly had two tabs — it was reading the orphan, correctly. Costs a
`sys_session` row and a sweep entry per page load, and makes the reload road
(read the cookie session, re-mint it if empty) reason about a session that is
never the one holding the forms. The shape that fixes it is for `GET /` to mint
nothing and let the first XHR create the session under the id the client already
has.

## Firefox

**The SSE stream was answered `401 no session`.** *2026-09-07, fixed the same
day.* `EventSource` cannot send a header, so the stream leaned on the cookie
while every other request used the tab's own `sessionStorage` id. It carries
`?sid=` now, which the server has always read for requests that can only send a
URL — see [live-updates.md](live-updates.md). The 401 was invisible in use: the
client falls back to polling, so the page kept working a second or two behind.

**The SSE stream would not connect.** *2026-09-07, fixed the same day.*
`svr.set_keep_alive_max_count(1)` was applied on every platform, though the
reason written beside it is a Windows one — a cpp-httplib keep-alive stall on
the browser's poll. One request per connection is cheap at a handful of
requests and not at 450, which is what a cold UI5 page load is: Firefox has six
connections to a host and had to churn all 450 through them, and `EventSource`
could not get one. It fell back to polling, so nothing broke visibly. The
workaround is `#if defined(_WIN32)` now, and the read timeout is 30s off Windows
because on a kept-alive connection that value is the idle window between a
browser's requests, not a stall.

## A pointer without a pin

**`SessionManager::FindApp` hands out a raw `ibWebApplication*`.** *2026-09-07.*
The manager's map owns `shared_ptr<ibWebSession>`, and three of its methods
(`Login`, `TabIconPNG`, `ModalReply`) take a copy under the lock for exactly the
reason each says: the session can be destroyed the moment the lock is released.
`FindApp` does not, and `wfrontendOpenMetaObject` then runs a worker task
through the pointer after the lock is gone. Found while fixing the SSE waiter,
which was the same defect with a 25-second window instead of a menu click's —
see [live-updates.md](live-updates.md). A pin is not the whole answer here
either: `OnExit` resets the application's `unique_ptr`, so holding the session
keeps the session, not the application. The shape that fixes it is the one the
signal uses — hand back something the caller owns.

## The guard that says nothing

**`ibCrashGuard` is silent on a segfault in the web server.** *2026-09-05, still
true 2026-09-07.* The teardown crash produced no dump, no terminate log, no
signal message, and neither of the two lines `main` prints on its failure roads —
the process left by a road nobody instrumented. It cost two days, and it was
found in the end by running the server under `lldb` rather than by anything the
program said. macOS wrote a `.ips` report for some of the sightings and not for
others, so even that is not a floor. Worth closing before the next control is
written: the next fault of this shape starts from a log tail again.

The stream crash the same day made the case sharper. It aborted — `SIGABRT`, not
a segfault — and still printed nothing: no `libc++abi` line on the merged
stderr, no `.ips`. What identified it was wrapping the process in a shell that
echoed `$?`, and `134` was the whole diagnosis. Under `lldb` it would not
reproduce at all, the debugger's own timing being enough to close the race, so
the road the earlier bug was caught on was shut for this one.

## Metadata over MCP

**A form cannot be created through MCP.** *2026-09-05.* `metadata_create
{kind: Form, …}` opens the modal "Goods form wizard" even when `properties` is
passed — its own description promises the opposite ("a form, which a click answers
in a dialog, is created outright") — and then times out. Worse, `window_dismiss
{close: true}` answers `{"closed": ["Goods form wizard"]}` and does not close it,
so every metadata verb refuses with "waiting on a dialog" from that point on and
the only documented escape does nothing. Reproduced from a freshly started
designer. A form therefore has to be created by hand before an assistant can put
controls on it.

## The demo base

**No designed forms.** *2026-09-05.* `/forms` answers `[]`, and the list forms
that do open are entirely `tablebox`, which is still a stub on the web. So
Iteration 1's three controls have nothing in this base to render on, and the only
place they can be seen is the harness. A designed object form on a catalog would
fix that.

## Carried into later iterations

**The shell is not tokenized.** *2026-09-05.* Iteration 1 tokenized only the
surfaces a form is drawn on (`#main`, `#tab-body`, `.form-host`), because dark
theme was unreadable without it. Still hardcoded, and each will fight the dark
theme the same way: the body's default foreground and outer canvas; the title bar,
app badge, menu and the temporary switcher; the sidebar, its navigation and its
status line; the tab strip with its active/inactive tabs, scroll and close
controls; the status bar and output toggle; the output panel, header, body,
messages and resize affordance; the network banner; the boot, auth and session
overlays; dialogs and spinners.

**The dev switcher shows in `ui=legacy`.** *2026-09-05.* It is marked DEV ONLY,
but the program asks that legacy render exactly as it does today, and a control in
the title bar is not exactly that. Hide it under `ui5`, or accept it and say so.

**`ERR_EMPTY_RESPONSE` on asset modules under load.** *2026-09-05.* Seen twice
during harness loads, on three i18n/CLDR modules. Probably the same death as the
teardown crash, caught mid-flight; treat the two as one symptom until proven
otherwise.

**`-Wmismatched-tags` on `ibCompositionDescription`.** *2026-09-05.* Declared as a
class in `mcp/mcpTool.h`, defined as a struct in `compositionDescription.h`. Valid
C++, and a linker error waiting under the MSVC ABI. Pre-existing, unrelated to
this program, noticed while building it.
