# Authentication flow — desktop vs web

OES has two entry points into authentication:

1. **Desktop / headless** — the app gets an `ibSession*` from
   `appData->CreateSession()` (or typed `CreateSession<SessionT>()`),
   then calls `session->Open(user, password)` with credentials read
   from the command line or launcher arguments. Replaces the legacy
   monolithic `ibApplicationData::StartSession`.
2. **Web** — `ibWebSession::Login(user, password)` invoked per-session
   from the browser via `POST /w/<prefix>/login`.

Both paths funnel through the registry's `ProcessAttach`, which calls
`ibApplicationData::AuthenticateUser` (pure verifier — reads the user
row from `sys_user`, runs `ibPasswordHash::Verify` for PBKDF2 with
transparent MD5-legacy fallback and lazy upgrade via `NeedsRehash`),
then `InstallUser` writes the resolved identity onto the active
session.

## Open() fallback — OnShowAuthenticate

`session->Open(user, password)` is the desktop path. If silent CLI
authentication fails (bad/empty credentials), the session falls back
to its own prompt hook:

```cpp
// src/engine/backend/session/session.cpp — ibSession::Open
if (!Authenticate(user, password)) {
    if (OnShowAuthenticate(user, password))      // virtual override point
        return Authenticate(GetUserInfo().m_strUserName,
                            GetSessionRawPassword());
    return false;
}
```

`OnShowAuthenticate` is a virtual on `ibSession`. The previous
`backend_mainFrame->AuthenticationUser` indirection through the
frame interface is gone — auth UI is no longer a frame
responsibility. Concrete overrides:

- **Desktop** (`ibGUISession::OnShowAuthenticate`): shows the modal
  `ibDialogAuthentication` with prefilled login/password fields and
  blocks until the user submits or cancels. On submit it stores
  `m_userInfo` + `m_sessionRawPassword` on the session and returns
  true so the registry's `Open()` re-runs Authenticate with the
  dialog-filled creds.
- **Web** (`ibWebSession`): no override yet — base `ibSession::OnShowAuthenticate`
  returns false, so a failed CLI auth on the web path fails hard.
  Real web auth comes from the browser via `POST /login`, so this
  path is essentially never taken in practice.
- **Headless** (daemon / codeRunner): no override —
  base returns false. CLI credentials must be correct; failure
  exits cleanly.

## Why web doesn't hit Open() today

Web credentials come from the browser via `POST /login`, not from the
server process command line. The flow is:

```
browser → GET / → wfrontendCreateSession → session w/o auth
        → POST /login (user, password)
          → wfrontendLogin → ibWebSession::Login
            → registry.Connect(req) + ticket.Attach(user, pwd)
              → ProcessAttach → AuthenticateUser → InstallUser
                → 200 OK (success) or 401 (failure, client re-shows form)
```

`session->Open()` is the desktop / headless path used by
`enterprise.exe` / `designer.exe` / `daemon.exe` / `codeRunner.exe`
startup, not by `wenterprise-server`. The web server binds its DB
connection without an "IB user" per se — that connection is a
service account configured via `--ibuser=` / `--ibpwd=` flags when
launcher or designer spawn the server process; `ibWebSession::Login`
then attaches the real end-user identity on top via the same
`ProcessAttach` path the desktop session would use.

## TODO: interactive re-auth on web

If a future use-case needs CLI-driven auto-login on web (e.g. kiosk
mode pre-filling `--ibuser=...` / `--ibpwd=...` for a default user,
with the option to override interactively), we need a web override
of `ibSession::OnShowAuthenticate`. Proposal:

1. `ibWebSession::OnShowAuthenticate` flags the session as "needs auth"
   (add `bool m_needsAuth` on `ibWebSession`, or a setter on
   `ibWebApplication`). Returns `false` — current auth attempt still
   fails.
2. Every HTTP endpoint that needs an authenticated session checks the
   flag at entry. If set, returns `401` with body
   `{"needsAuth": true, "reason": "re-auth required"}`.
3. `webClient.cpp` sees `needsAuth` in any fetch response and re-shows
   the login form overlay (same path as the initial login screen).
4. `POST /login` clears the flag on successful
   authentication.

This keeps the desktop modal semantics without blocking the HTTP
handler thread — the server publishes a state, the client observes
and re-prompts asynchronously. It also composes with the existing SSE
live-update channel (the `needsAuth` signal can flow through `/stream`
as a special frame so the client can react even during idle polling).

Until that use-case materialises, web auth is a single-shot
POST /login with failure surfaced via HTTP status — no server-side
retry machinery.

## Per-session identity storage

User identity now lives on `ibSession`, not on a process-wide
singleton. `ibApplicationData::InstallUser` is the single writer; it
reads the active session via `ibSession::Current()` and stores:

- `m_userInfo` (`ibUserInfo`, renamed from `ibApplicationDataUserInfo`)
  — OES user row from `sys_user` (name, full name, guid, roles,
  language). Owns sys_user CRUD as static factories (`Read` / `Save` /
  `HasAny` / `ListAll` / `Serialize` / `Deserialize`); `appData` no
  longer mediates.
- `m_sessionRawPassword` — plain-text of the submitted password.
  Cleared by `ibSession::ClearSessionRawPassword` and on session
  destruction. Never persisted to disk.

  **Why plain-text**: Designer's "Start debugging" spawns a child
  runtime process (enterprise.exe or wenterprise-server.exe) and
  needs to pass the current user's creds to it so the debugger
  attaches without re-prompting. The stored hash is useless here —
  the child re-verifies against whatever it pulls from `sys_user`,
  so plain-text is what must travel. Risk is bounded: only in-memory,
  wiped on logout, not written to disk. When/if a token-based
  one-shot debug handshake replaces the creds-passthrough,
  `m_sessionRawPassword` can be dropped.

Session identity (the GUID published as `sys_session.session` and
threaded through the debug protocol handshake) is part of
`ibSessionIdentity::m_guid` — populated at Add time, immutable
afterwards.

Readers go through `appData->GetUserInfo()` (forwards to
`ibSession::Current()->GetUserInfo()`) or directly through the session
pointer where one is in scope. The 25+ legacy callsites picked up
session-aware behaviour automatically when the singleton field was
made a forwarder.

**Empty `userName` in `sys_session`** has three legitimate meanings,
not a single sentinel — see `reference_empty_username_meanings.md`
in the memory notes:

1. Open-access mode — `sys_user` table is empty, any cookie gets in.
2. Pending-auth — cookie minted via GET `/`, `/login` not completed
   yet; heartbeat row exists but user isn't known.
3. Web server technical session (Role::System) — process lives
   before any client connection; heartbeat row marks "server alive".

Admin listings should disambiguate via the `application` column
(ibRunMode enum) plus whether `sys_user` has any populated rows.

## Password storage

See `backend/utils/passwordHash.{hpp,cpp}` for the actual hash
primitive. New passwords are PBKDF2-HMAC-SHA256 at 600k iterations
(OWASP 2023 recommendation) with a 16-byte system-RNG salt, stored in
PHC format `$pbkdf2-sha256$<iter>$<saltB64>$<hashB64>`. `Verify` also
accepts legacy 32-hex MD5 strings for compatibility with pre-migration
databases; `NeedsRehash` returns true for those so the post-auth path
silently re-stores the hash in the modern format on the next
successful login (see `ibApplicationData::Login` →
`SaveUserData(outInfo)`).

Argon2id would be the stronger primitive (memory-hard, resists GPU
attacks) but requires vendoring a third-party library; revisit when
the threat model calls for it.
