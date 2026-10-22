# The listeners: TLS from backend.conf

**What.** A server of this platform listens through one door, `core/server/listener.h`: TLS when
`backend.conf` names a certificate, plain HTTP on this machine's own addresses when it names none, and a
refusal on any other address. Through it listen the assistant access (MCP) inside the designer and in the
application server, the application server's port for its clients, and the application cluster's. The
server itself is the core's (`core/net/httpServer.h`), and so is its TLS: Mbed TLS is linked into the core
and nowhere else.

**Why.** Three servers listen on a network here, and all three spoke plain HTTP whatever the address:
the assistant's bearer token, a browser's session cookie and a thin client's password crossed the wire
in the clear. The transport is one thing for the process - the certificate on this machine - so it is
said once, where the process's other settings are, and every listener takes it from there.

## backend.conf

```
; PEM: the server's certificate, its chain after it; and the key of that certificate
Certificate = tls.pem
PrivateKey = tls.key
```

A comment stands on a line of its own, and a name stands bare: on the line of a value, `; ...` is read as
part of the value, and so are quotes around it. The two keys stand at the top of the file, before any
`[group]`: a key under a group, or misspelled, is a key not found, and not found is the clear. The file is
read as the process's other settings are: the working folder's `backend.conf` when there is one, else the
one beside the program - one file for every server the process runs. A Windows path keeps its backslashes.
A key under a password is not taken: there is no word for the password, and the refusal says so.

Relative names are resolved against the file's folder. Both or neither: one without the other is a
refusal. A file named and not there, or not readable, is a refusal that names the file; a certificate or a
key that is not PEM, or a key that is not the certificate's, is a refusal of the server's start that says
which of the two - never a listener in the clear: a server that listens in the clear because its
certificate was mislaid is the failure this door exists to prevent. The files are read by the engine, by
their names in whatever script, and handed to the server as text.

## The clear

Without a certificate a listener speaks plain HTTP on this machine's own addresses only - `127.0.0.0/8` by
number, `::1`, `localhost` - and is refused on any other, with `backend.conf` named in the reason: the
clear does not cross a network.

- **The assistant access** - in the designer, and the one the application server starts for a base - edits
  a configuration and runs code as the developer whose session it lives in, and a token sent in the clear
  across a network is a token given away. Widen the address, and the designer says that `backend.conf`
  must name a certificate first; the application server says it in its journal and serves the base
  without. With one named, the address it shows and the `claude mcp add` line it writes carry
  `https://`, and the client has to trust the certificate: a self-signed one is given to Claude Code's
  runtime through `NODE_EXTRA_CA_CERTS`, and the address shown is the one bound - with `0.0.0.0` that is
  no address a certificate names, so reach it by the name the certificate carries.
- **`wenterprise-server`** speaks no TLS, whatever `backend.conf` names: it binds this machine's own
  address only (`127.0.0.1` unless `--host` names another of them), and any other is a refusal on its
  console. The designer's web debugging, which starts it without `--host`, is unchanged. A browser over
  `https://`, or on another machine, is the web server's (`webserver`).

## The clients' port: oess://

- **Where.** `server.conf`'s `Host` and `Port` - the application server's, or the cluster's when it runs
  the folder's bases. `Host` left out is `127.0.0.1`: this machine alone. Another address - `0.0.0.0`, a
  network card's, empty (every interface) - wants a certificate in `backend.conf`; without one the server
  does not start, and its journal says why.
- **What it speaks.** A certificate named: TLS, and the address a person gives a client is
  `oess://server[:port]/<base>` - the server prints it for every base. None: `oes://`, on this machine.
- **The cluster** ends TLS at its port: the server it starts for each base listens on `127.0.0.1` in the
  clear, and is reached through the cluster alone. Its tray speaks TLS to it too, on `127.0.0.1`, without
  checking the certificate's name - it is let in by the folder's token.
- **The client** - `enterprise-thin`, `designer-thin`, the web server's publication of a server's base -
  checks the server's certificate: its chain against the system's store, or against the authorities of a
  PEM file it is given (`--trusted=<file>`; web.conf's `Trusted=`), and its name against the address's
  host. A self-signed certificate is trusted by naming itself. Refused, the client says which of the two.
- **The web server** makes its trust once per publication: every browser's socket to that server shares
  it.

## Where it stops

- The debugger's port (`1650`) is still plain off this machine.
- The launcher's list of bases has no field for a trusted file: a base on a server with a self-signed
  certificate is opened from the command line (`--trusted`), or its authority is put into the system's store.
- A base whose assistant access was set to an address off this machine's own, with no certificate named,
  does not start it any more: the designer's settings page says why, the application server's journal
  does. Name a certificate, or set the address back.
- The assistant's timeouts: a connection that sends no request within 30 seconds is let go, one kept
  alive between calls after ten minutes of silence; a call is not cut while it runs (a tool waits its turn
  on the designer's main thread, five minutes at most).
- The certificate is the process's, not a base's: one file for every server the process runs.
- Nothing here checks the certificate's name against the address: that is the client's side, as with any
  TLS server.
