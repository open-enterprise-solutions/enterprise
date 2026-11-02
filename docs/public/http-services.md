# HTTP services

**What.** A configuration answers an HTTP request with its own code. An `HTTPService` declares an
address; its `URLTemplate`s declare the paths under it; each template's `HTTPServiceMethod`s say
which request methods it answers and which function of the service module answers them.

**Why.** Other systems call an application as well as being called by it: a messenger sends every
incoming message to a webhook, a workstation or a page that is not a platform client reads and
writes over REST, another base pushes its changes. The outbound half is the
[HTTP client](http-client.md); this is the inbound half.

## The shape

| Kind | Properties | Children |
|---|---|---|
| `HTTPService` (under Common) | `RootURL` — one segment, unique in the configuration; `ServiceModule` — a manager module of its own | `URLTemplate` |
| `URLTemplate` | `Template` — `/Transfer/{id}/reset`, `/*` | `HTTPServiceMethod` |
| `HTTPServiceMethod` | `HTTPMethod` — Get, Post, Put, Patch, Delete, Head, Options, or Any; `Handler` — a function of the service module | — |

A request to

    <web server>/<publication>/hs/<RootURL>/<path under the root>

is answered by

```
Function TransferReset(Request) {
    var response = New HTTPServiceResponse(200);
    response.Headers.Insert("Content-Type", "application/json");
    response.SetBodyFromString("{\"id\": \"" + Request.URLParameters.id + "\"}");
    return response;
}
```

The service module is prepared with one such function per method, named by its `Handler` (a new
method is answered by `<template name><method name>` until it is named otherwise). It is a manager
module: compiled with the session's modules, so a handler calls common modules as any code does.

| Value | Made with | Holds / does |
|---|---|---|
| `HTTPServiceRequest` | handed to the handler, never created | `HTTPMethod`, `BaseURL` (`/trade/hs/desk`), `RelativeURL` (`/Transfer/12/reset`), `Headers`, `QueryOptions`, `URLParameters`; the body as a string or as `BinaryData` |
| `HTTPServiceResponse` | `(statusCode [, headers])` | `StatusCode`, `Headers`; the body from a string or `BinaryData`, and back |

The help corpus (`syntaxHelper/*/http.json`) has an article for every member.

## What it guarantees

- **A template is segments, each one of three:** a literal (`Transfer`), a parameter (`{id}`, one
  whole segment, handed over in `URLParameters`), or `*` — the rest of the path, nothing included,
  and only as the last segment. Anything else is refused as a template.
- **The most particular template answers.** Read from the left, a literal segment comes before a
  parameter and a parameter before `*`; between two of the same shape the one declared first. An
  address is matched as written: `/Transfer` is not `/transfer`. A slash at the end says nothing.
- **Then the method:** the template's method of the request's name, else its `Any`. A template
  with neither is 405, with `Allow` listing what it has; a path no template matches is 404.
- **Each request is a session of its own**, of the kind *HTTP service* in Active Users while it
  works, ended once it is answered. It logs in by `Authorization: Basic` (the base's own names and
  passwords, the same roles and row-level access as a person's); a base with no users lets it in
  as it lets in anybody. Nobody let in is 401 with `WWW-Authenticate: Basic`, whatever the address,
  so what a base serves is told only to whoever may call it. The session module runs as for any
  session.
- **A handler's failure is the base's to read.** A handler that raises, or returns anything but an
  `HTTPServiceResponse`, is answered 500, and its words go to the base's registration journal — the
  caller is somebody else's system. A reason a caller needs is a response the handler builds.
- **Headers are a `Structure`**, as the client's: a request's names are lower case, and a Structure
  finds them whatever case is asked; a name sent twice is joined with `, `. A response's headers
  that say how a message is carried (`Content-Length`, `Transfer-Encoding`, `Connection`, `Host`,
  `Expect`, `Upgrade`, `Proxy-Authorization`) are the server's: a response that sets one is refused
  by name. The words after a status code are the server's too.
- **The transport is the web server's**, HTTP or HTTPS as its `web.conf` says ([listeners](listeners.md)).
  A file base's publication answers in the web server's process; an application server's is
  carried there over the server's port, as its clients' calls are.

## Where it stops

- One session per request: nothing is kept between requests.
- A body is held in memory, within the web server's 4 MB; nothing is streamed either way.
- The application server's own port serves its clients and stays on its machine: HTTP services are
  published by the web server.

## Underneath

The web server routes `/<publication>/hs/` — every method — to the base as one protocol call,
`http` ([client-protocol.md](client-protocol.md)): the request as it came, and the response as the
service made it. The base answers it with `ibHttpServiceCall` (`backend/httpService/httpServiceCall.h`);
the kinds are `metaCollection/metaHttpServiceObject.h`, the values `system/value/valueHttp.h`.
