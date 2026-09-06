# Open issues

What is known to be wrong or unfinished, so the next iteration inherits a list
rather than a surprise. Dated when first recorded.

## Blocking the next iteration

**The web server dies during session teardown.** *2026-09-05.* Seen three times,
always the same tail and nothing after it:

```
[life] ~ibVisualHostClient …        (one pair per open tab)
[life] ~ibFormVisualDocument …
[app] ExitMainModule: done
[app] delete m_frame: begin
<exit 1>
```

Every sighting was a session that **owned open form tabs** — one, two, three —
being swept while a new `GET /` arrived. `ibCrashGuard` writes nothing for it: no
dump, no terminate log, no signal, and neither of the two messages `main` prints
on its failure roads, so the process leaves by a road nobody instrumented. Not
reproducible headlessly: 25 rounds at six-clients-at-once and 10 rounds of
tearing down a session with tabs all survive, because a headless click on "All
functions" does not open a form and the sessions end up with no tabs. A real
browser opens them. Two neighbouring faults in the same area are fixed and should
not be confused with this one: a double delete of the child frame at tab close,
and an unsynchronized build of `ibMemberTable`'s name index when sessions start
together.

**`ibCrashGuard` is silent on these exits.** *2026-09-05.* Filed separately
because it is what made the above expensive: with no dump and no message, every
hunt starts from a log tail. Worth closing before the next control is written.

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
