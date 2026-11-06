# Web addresses

A web server publishes a base under a name — `https://server/trade/` — and carries its clients there:
the page and `/trade/client`. What else the base answers under that name is the base's own list of
**web addresses**. Each entry is a path under the publication (`es/desk` is answered at
`/trade/es/desk/…`), a **kind** — which code answers there — and, for a kind that answers for one, the
configuration's object it answers for.

## Why the list is the base's

A web server may publish one base on many machines, and a base may be published by many web servers.
Keeping the list in the base means one registration reaches every one of them: each web server asks the
base for its list when it publishes it and is told every change, and mounts or unmounts within seconds.
The web server keeps no copy of its own and decides nothing about an entry: it hands each request to the
base, as it came, and writes back what the base answers.

## Where it is kept

- **Kept entries** are registered by an administrator and live in the base (`sys_settings`, category
  *Web*) — they survive a restart, and every application server of the same base sees them.
- **A session's own entries** go with the session that registered them.

A path is segments of letters, digits and `. _ ~ -`, with no slash at either end; the first segment is
never `client`. Two entries never have the same path. A kind nobody registered is refused, and so is an
entry its kind does not take.

## Doing it

- **Administration → Web addresses…** in the designer (the *Data administration* right): the list, with
  **Add…** offering what the configuration declares for each kind, or another path typed by hand, and
  **Remove**.
- Over MCP, in the designer: `address_list`, `address_register`, `address_unregister`.
- On the wire: the client protocol's `addresses` and `serve` ([client-protocol.md](client-protocol.md#web-addresses)).

## The web server's side

A request to a mounted address is queued and answered by a worker of the web server's own. Three keys
of `web.conf` bound it:

| Key | Default | What it bounds |
|---|---|---|
| `ServiceWorkers` | 8 | the workers that carry requests to the bases |
| `ServiceCalls` | 64 | the requests waiting or in flight; one more is answered 503 with `Retry-After: 5` |
| `ServiceWait` | 60 | seconds a request may take; past it the caller is answered 504 |

A base that cannot be reached answers 502. A path no entry has is 404, as before.

## Where it stops

- The platform registers the mechanism and no kind of its own: what answers at an address is a kind a
  feature registers (an HTTP service, for one).
- A 504 tells the caller, not the base: the base finishes the request it was handed.
- The list is edited in the thin designer; the browser does not show it.
