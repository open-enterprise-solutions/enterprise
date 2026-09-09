# Open issues

What is known to be wrong or unfinished, so the next iteration inherits a list
rather than a surprise. Dated when first recorded.

**Anything not fixed in the round that found it is filed** on
`open-enterprise-solutions/enterprise`, and this page is the index rather than
the record: the evidence, the stacks and the suggested shapes live on the issue.
Standing as of 2026-09-07: [#100](https://github.com/open-enterprise-solutions/enterprise/issues/100)
· [#101](https://github.com/open-enterprise-solutions/enterprise/issues/101)
· [#102](https://github.com/open-enterprise-solutions/enterprise/issues/102)
· [#103](https://github.com/open-enterprise-solutions/enterprise/issues/103)
· [#104](https://github.com/open-enterprise-solutions/enterprise/issues/104)
· [#105](https://github.com/open-enterprise-solutions/enterprise/issues/105)
· [#106](https://github.com/open-enterprise-solutions/enterprise/issues/106)
· [#107](https://github.com/open-enterprise-solutions/enterprise/issues/107)
· [#108](https://github.com/open-enterprise-solutions/enterprise/issues/108) *(built the same day)*,
plus what was added to [#86](https://github.com/open-enterprise-solutions/enterprise/issues/86)
(two embedded engines on one file — it deadlocks the first process, not just the
second) and [#96](https://github.com/open-enterprise-solutions/enterprise/issues/96)
(a web server that aborts mid-session prints nothing at all; the exit status is
the whole diagnosis).

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

**Every navigation mints a session nobody uses.** *2026-09-07, filed as
[#106](https://github.com/open-enterprise-solutions/enterprise/issues/106).* `GET /` creates a
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
requests and not at 450, which is what a cold page load was while the client ran
on a component library: Firefox has six connections to a host and had to churn
all 450 through them, and `EventSource` could not get one. It fell back to polling, so nothing broke visibly. The
workaround is `#if defined(_WIN32)` now, and the read timeout is 30s off Windows
because on a kept-alive connection that value is the idle window between a
browser's requests, not a stall.

## Building a form over MCP

Four things found on 2026-09-07 while laying out the Goods receipt form that this
program uses as its rich-layout reference. None is a web defect; all four are in
the way, and all four are filed — the detail and the evidence live there, this is
the index.

**A numeric control property cannot be set at all** ([#100](https://github.com/open-enterprise-solutions/enterprise/issues/100)). `form_set` declares `value`
as a string. Send `"1"` for `Proportion` and the platform refuses it by kind
("expected 2, got 4"); send `1` and the tool refuses it by schema. So
`Proportion`, `BorderSize` and `Wrap` are unreachable, and a table that should
share the remaining height cannot be told to.

**A caption with an apostrophe in it round-trips wrong** ([#101](https://github.com/open-enterprise-solutions/enterprise/issues/101)). `form_set` on `Title`
with *Lines are priced from the warehouse's current price list.* stored
`en = 'en = 'en = 'Lines are priced ... price list.';';';` -- the caption's own
serialised form, wrapped three times, and that is what the form then displays.
The apostrophe is the serialiser's quote character. Captions without one are
fine, which is why nothing had caught it.

**A `Button` cannot be made to do anything** ([#102](https://github.com/open-enterprise-solutions/enterprise/issues/102)). A button carries only a command,
and hides itself when none resolves. `form_bind` on `Command` answers "There is
no command called 'Post' here. Available: none", and there is no verb that
creates a form command -- `metadata_accepts` on a Form lists no child kinds. So
every button placed through MCP is an invisible one.

**`form_paste` of a container at form level does not wrap it in a `SizerItem`** ([#103](https://github.com/open-enterprise-solutions/enterprise/issues/103)).
The pasted control arrives with no layout flags, so a tablebox that filled the
width in the generated layout renders at its natural size in the stored one. The
way round is `form_add class=SizerItem` and pasting into that -- and a fresh
SizerItem defaults to `Shrink`, where the generated one is `Expand`.

## A pointer without a pin

**`SessionManager::FindApp` hands out a raw `ibWebApplication*`.** *2026-09-07,
filed as [#105](https://github.com/open-enterprise-solutions/enterprise/issues/105).*
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
true 2026-09-07. Recorded on
[#96](https://github.com/open-enterprise-solutions/enterprise/issues/96).* The teardown crash produced no dump, no terminate log, no
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

**The shell is not tokenized.** *2026-09-05, and now the only palette there is.*
The client's chrome — the title bar and its badge, the sidebar and its
navigation, the tab strip, the status bar, the output panel, the network banner,
the boot / auth / session overlays, dialogs and spinners — is written in literal
colours where it is used rather than read from a token. That was a defect while a
theme could change under it; with one light palette it is merely a place a second
one cannot reach. Whoever builds a dark theme starts here.

*(The dev switcher, filed here on 2026-09-05, is gone: its three axes existed to
steer the component library and left with it — ADR-015.)*

**`ERR_EMPTY_RESPONSE` on asset modules under load.** *2026-09-05.* Seen twice
during harness loads, on three i18n/CLDR modules. Probably the same death as the
teardown crash, caught mid-flight; treat the two as one symptom until proven
otherwise.

**`-Wmismatched-tags` on `ibCompositionDescription`.** *2026-09-05.* Declared as a
class in `mcp/mcpTool.h`, defined as a struct in `compositionDescription.h`. Valid
C++, and a linker error waiting under the MSVC ABI. Pre-existing, unrelated to
this program, noticed while building it.
