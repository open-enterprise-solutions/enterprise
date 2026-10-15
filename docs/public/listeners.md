# The listeners: TLS from backend.conf

**What.** A server of this platform listens through one door, `backend/server/listener.h`: TLS when
`backend.conf` names a certificate, plain HTTP on this machine's own addresses when it names none, and a
refusal on any other address. The assistant access (MCP) inside the designer and in the application server,
and `wenterprise-server`, listen through it; the application server's client listener does not yet.

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
refusal. A file named and not there, or not readable, or a key that is not the certificate's, is a refusal
that names the file - never a listener in the clear: a server that listens in the clear because its
certificate was mislaid is the failure this door exists to prevent. The files are read by the engine, by
their names in whatever script, and handed to the library as memory.

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
- **`wenterprise-server`** binds `127.0.0.1` unless `--host` says otherwise, so the designer's web
  debugging, which starts it without one and opens the browser on the address the manifest reports, is
  unchanged; `--host=0.0.0.0` without a certificate is a refusal on its console. Over TLS it does not
  bounce a `Host: localhost` request to `127.0.0.1` as it does in the clear: the certificate names what
  the browser asked for, so one named for the assistant moves the web debugging to `https://` too, and
  has to name `localhost` or `127.0.0.1`, whichever the browser is sent to.

## Where it stops

- The application server's client listener (`ws://`) is still plain; `oess://` is the next step.
- A base whose assistant access was set to an address off this machine's own, with no certificate named,
  does not start it any more: the designer's settings page says why, the application server's journal
  does. Name a certificate, or set the address back.
- The listener's timeouts are what they were (a request may take six minutes, a kept connection ten):
  a caller that opens connections and says nothing holds them that long, authorised or not.
- The certificate is the process's, not a base's: one file for every server the process runs.
- Nothing here checks the certificate's name against the address: that is the client's side, as with any
  TLS server.
