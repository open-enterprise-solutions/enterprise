# The application cluster

**What.** `appcluster` serves the bases of a **server folder**: for each base it starts one or more
application servers (`appserver --worker=<base> --route=<n>`), holds the one port every client comes in
by, and carries each client to a server of its base. It runs as a console program or as a service of
the system. `appcluster-tray` shows its servers and steers them.

**Why.** The engine is C++: a fault in its memory ends its process, not one call. A base per process
keeps a fault in the base it happened in — its clients reconnect, the other bases do not notice. The
same walls are how SAP's work processes and a browser's tabs are built. Clients see one address
whatever runs behind it.

## The parts

| Part | Does |
|---|---|
| `appcluster` | Reads `server.conf`, starts each base's servers, holds the clients' port (7373 unless `Port` says another), ends TLS there, starts a fallen server again. Links `core` alone: it opens no base itself |
| `appserver` | Serves ONE base for the cluster that started it: the base's connections, sessions, locks, jobs and journal. Listens on `127.0.0.1` in the clear, on the first free port of 7500–7599. Refuses to start without `--worker` |
| `appcluster-tray` | Shows each base's servers (state, process, falls, the last error) and starts, stops or restarts them over the cluster's control socket; starts the cluster when none runs. Closing it leaves the cluster serving |

## The server folder

```
<server folder>/          --dir; by default "server" in the program folder
  server.conf             Host, Port; one group per base
  server.key              this installation's key, made at the first start
  cluster.token           the control socket's key, written at each start, removed as the cluster goes
  <Id>/                   one folder per base: its journal and infobase.conf; a Firebird base's files too
```

A base's group: `Kind` (`firebird` or `postgresql`), `Path` or `Server` / `Port` / `Database` / `User` /
`Password`, `IbUser` / `IbPassword` (the base's own user the server logs in as), `Processes` (its servers
side by side, 1 when absent), and `Id` — assigned by the cluster at its first start and written back.
Firebird and PostgreSQL bases stand side by side in one folder.

## What it guarantees

- **A fault stays in its base.** A server that falls is started again — after a second, later and later
  after short lives. Its sessions are lost with it; its base's other servers and every other base go on.
  A server started again after a fall takes no second crash dump, so a DBMS that stays down does not fill
  the disk.
- **A client comes back to its server.** A session token ends in its route (`<hex>.<route>`), and the
  cluster carries a client that logs in by token, or a debugger's channel, to the server that route
  names while it serves. A new client goes to the serving one carrying the fewest sockets.
- **Several servers on one base share what they must through the base:** sessions (`sys_session`),
  record locks (`sys_lock`), and a job's claim and clock (`sys_job`) — a scheduled job runs once across
  them.
- **One kind of holder at a time.** Servers share a base with servers; a file base opened by a client
  shares it with other file-base processes. A server is refused a base a file-base process holds, and
  the reverse.
- **TLS at the door.** With a certificate in `backend.conf` the port speaks TLS (`oess://`); without one
  it stays on this machine. The servers behind it are reached through the cluster alone
  ([listeners.md](listeners.md)).
- **No password in the clear.** Passwords in `server.conf` are sealed with the installation's key
  (AES-256-GCM); plain text is refused. `appserver --set-password=<base>/<Password|IbPassword>` seals one
  from the keyboard.
- **A memory wall, said in words.** `MemoryLimit` in `backend.conf` (megabytes): at 80% no new client or
  window is let in, at 90% every call but closing and logging out is refused before it starts. On
  Windows the limit itself is a Job Object; on Linux it is the service's `MemoryMax`.
- **The control socket is this machine's.** `/cluster` on the same port answers loopback only, and only
  with the token from the server folder.
- **A service like any other.** `appcluster --install` (an administrator's) makes it a Windows service,
  `--uninstall` removes it; under systemd a unit runs `appcluster --service`. Every stop — Ctrl+C,
  SIGTERM, the service manager's — stops every server first.

## Where it stops

- A server opens existing bases; the designer creates a base and its tables.
- The cluster starts its servers on its own machine: one cluster, one machine.
- A server behind the cluster sees every client as `127.0.0.1`: the client's own address is not carried
  past the relay.
- The tray reaches a cluster on `127.0.0.1` only, and its settings are not edited from it.
- The designer has no process of its own in the cluster: a thin designer works in the base's server.
