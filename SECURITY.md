# Security

## Reporting a vulnerability

Report it privately, through GitHub: the repository's **Security** tab → **Report a vulnerability**. Please do
not open a public issue, a pull request or a discussion for it.

Include what is needed to see it happen:

- the program and the build — a release, a nightly (its date), or a commit of `develop`;
- the OS, and the database (Firebird file base, Firebird or PostgreSQL server);
- the steps, a request, a module or an application that shows it, and what an attacker gains.

The report is answered in the advisory it opens. A fix lands on `develop`, ships in the next nightly and the
next release, and the advisory is published with it, naming the reporter unless asked not to.

## What is supported

The latest release and the nightly built from `develop`. An older release is not patched: the fix comes with
the next one.

## What the platform does to protect a base

| Where | What |
|---|---|
| Passwords of the base's users | PBKDF2-HMAC-SHA256, 600 000 iterations, a 16-byte random salt per password; a stored hash below the policy is re-hashed at the next successful login |
| The network | Every server listens through one door: TLS when `backend.conf` names a certificate; with none, plain HTTP on this machine's own addresses only, and a refusal on any other ([listeners.md](docs/public/listeners.md)). TLS is Mbed TLS, linked into the core and nowhere else |
| Server configuration | The database passwords in a server folder's configuration are sealed (AES-256-GCM) |
| Rights | Rights per object and operation, folded across a user's roles; a restricting role takes away whatever the others grant; row-level restrictions are code in the role and fail closed ([access-rights.md](docs/public/access-rights.md)) |
| Database access | A value goes to the database as a parameter of a prepared statement, never joined into the SQL text — a rule every change is reviewed against ([development.md](docs/development.md)) |
| Assistant access (MCP) | Off until a person turns it on; on that person's own machine, behind a token; every call is journalled; until the assistant has introduced itself in the person's window, it may only read what it needs to do so |

The third-party components that read untrusted input — Mbed TLS (TLS), llhttp (HTTP), expat (XML), LodePNG
and lunasvg (pictures) — are submodules taken as their authors publish them, with no fork, so a fix of theirs
reaches the platform as a submodule update ([NOTICE.md](NOTICE.md), [docs/BUILD.md](docs/BUILD.md)).
