# Live updates — the stream, and who is allowed to wait on it

The browser holds one `EventSource` per tab on `GET <prefix>/stream`. The server
answers with a chunked `text/event-stream` whose content provider blocks until
the session's sequence moves, then writes one `data: <json>` frame carrying the
active host tree. On a 25-second timeout it writes `: ping` instead, so a proxy
does not collapse the connection. A client that cannot open the stream falls
back to the 2-second poll, which is why a broken stream is invisible until
somebody measures it.

## The stream has to name its session in the URL

`EventSource` cannot send a header. Every other request the client makes carries
`X-OES-Session` with the tab's own id — the one in `sessionStorage`, so that two
browser tabs are two `ibWebSession`s — and the stream cannot.

It was opened with `withCredentials`, leaving the cookie to identify it. That is
wrong twice. The cookie names **one** session for the whole browser, so a second
tab's stream would have been listening to the first tab's session; and a browser
that had no cookie for the path got `401 no session`, fell silently back to
polling, and stayed there. Firefox reported exactly that, 2026-09-07.

The server has always read `?sid=` for requests that can only carry a URL —
`<img src>` for a tab icon uses it — so the stream uses it too:

```js
liveSource = new EventSource(API + '/stream?sid=' + encodeURIComponent(tabSid),
  { withCredentials: true });
```

## A waiter outlives what wakes it

`ibWebLiveSignal` (`web/webApplication.h`) is a `shared_ptr`-owned box holding
the sequence, its mutex and its condition variable. `ibWebApplication` owns one;
`MarkDirty` bumps it, `WaitForChange` waits on it, and `OnExit` **closes** it.

It is a separate object because of what the waiter is. An SSE subscriber parks
for 25 seconds inside `wfrontendLiveWait`, and in that window its session can be
swept for idleness or destroyed with the browser tab. The code took a raw
`ibWebApplication*` under the manager's mutex, dropped the mutex and waited on
that application's own condition variable — with a comment saying so, and
naming the fix it did not do. Closing a tab that had a live stream aborted the
process: `EXIT=134`, no message, no `.ips`. Twelve open-and-close rounds went
from *dead by the second* to twelve alive, and forty pages across two engines
survive the same cycle.

Two rules come out of it:

- **Wait on something you own.** `SessionManager::FindLiveSignal` hands back the
  `shared_ptr`, so the waiter's own reference keeps the box alive whatever
  happens to the application behind it.
- **Say when nothing more is coming.** `Close()` wakes every waiter and makes
  every later wait return at once, so teardown does not have to outlast a
  25-second timeout, and 25 seconds of an HTTP worker thread is not held by a
  session that has stopped existing.

The provider then ends the response when `wfrontendSessionExists` is false. Left
running it would spin: a closed signal returns immediately, the sequence has not
moved, so it would write a heartbeat and ask again, forever.

## The sequence rides on every tree

Every tree the server returns carries `"seq"` at its root — from `/action`,
`/fire`, `/change`, `/toggle`, `/command`, `/tab` (both verbs), `/open-meta`,
`/form`, `/active`, and every `data:` frame on the stream. It is the session's
live sequence: the counter `ibWebLiveSignal` holds, the one `MarkDirty` bumps
and the stream waits on. `{}` stays `{}` and an error object stays as it is;
only a tree has one.

**What it is for.** The browser keeps its DOM and reconciles each tree into it,
and the same state reaches it twice: once as the direct answer to what it
posted, and once more when the stream wakes on the bump that answer caused.
Comparing the two payloads as text was how the repeat used to be caught, and it
caught only a byte-identical one. The number does it properly: the client keeps
the last sequence it applied per host, and a tree whose `seq` is not above it —
for the same host, by its root `id` — is a repeat and is not applied. A tree for
another host is always applied, and so is one with no `seq` at all.

**When it is read.** `HostTreeJSON` (`wfrontend.cpp`) is the one place a tree is
produced, and it reads the sequence *after* the road's own `MarkDirty` and
*before* it builds the tree. That order is the whole rule: **the sequence a
payload carries is never newer than the state the payload shows.** Most roads
run on the session worker, which is single-threaded per session, so nothing
changes the tree between the read and the serialisation there. `/form` is the
exception — it runs on the HTTP thread under a session scope — and a worker
task landing between its read and its serialisation only sends that tree out
under a number *older* than its state, which is the direction the rule allows.

**Why `≤` is safe.** Two things can happen around the read. A bump that lands
after it — a timer tick queued behind the request — sends this tree out under
the older number, and the stream then wakes for the newer one and the browser
applies that too: a repeat, never a loss. The reverse, a tree carrying a number
above the state it shows, would let the browser discard the next real change as
already seen — and it is exactly what reading the sequence *after* serialising
would produce. So the read comes first, and the client drops on `≤` rather than
on `<`, because a tree under the same number is the echo and nothing else.

**Every road bumps for the state it returns.** `Dispatch` and `DispatchCommand`
end in `SettleAfterScript` — drain the tabs the script closed, then `MarkDirty`;
`OpenForm`, `ActivateTab` and `CloseTab` bump before they serialise. Two were
missing and were added: `OpenMetaObject`, when `Execute` only activates a tab
that was already open (opening a form bumps on its own, activating one did
not); and the `CloseTab` veto path — the browser had already taken the tab down
and has to apply the tree it gets back, so that tree has to arrive under a
number it has not seen. `/active` bumps nothing: it shows the current state
under the current number, which is what a poll is for.

**And the road that is not a request.** A form timer (`ibWebTimer`) posts its
handler to the worker from its own tick thread, and it used to post nothing
else: a handler that changed a label produced a tree under the unchanged
number, the stream never woke for it, and a browser dropping on `≤` would have
dropped the change when it did arrive. The timer's task now ends with the same
`SettleAfterScript` a dispatch ends with. It is the only such road: background
jobs run on the job's own session, not on a browser's, and the MCP server lives
in the designer.

**And the strip names the hosts.** Each entry of `tabs[]` in `/session` carries
`host` — the root `id` of the tab's host tree, the web-local control id of its
`ibVisualHostClient` — when the tab has one. The browser keeps one mounted DOM
per host, keyed by that id, and after each refresh of the strip disposes the
mounts whose id is no longer listed.

## What still hands out a bare pointer

`SessionManager::FindApp` returns `ibWebApplication*` with no pin, and
`wfrontendOpenMetaObject` runs a worker task through it after the lock is gone.
The window is far narrower than the stream's — it is the length of one menu
click, not half a minute — but it is the same shape. Filed as
[#105](https://github.com/open-enterprise-solutions/enterprise/issues/105).
