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

## What still hands out a bare pointer

`SessionManager::FindApp` returns `ibWebApplication*` with no pin, and
`wfrontendOpenMetaObject` runs a worker task through it after the lock is gone.
The window is far narrower than the stream's — it is the length of one menu
click, not half a minute — but it is the same shape. Recorded in
[open-issues.md](open-issues.md).
