# Database modes — file vs server

> **Scope:** `ibDatabaseMode` — how OES connects to its database, what differs between a
> file base and a server base, and where each mode puts its artefacts.
> Companions: [ARCHITECTURE.md](ARCHITECTURE.md) (`ibRunMode` — the *process* modes,
> a different axis), [connection-pool.md](connection-pool.md),
> [firebird-mesh-driver.md](firebird-mesh-driver.md).
> This is foundation code.

---

## 1. Two independent axes — do not confuse them

| Axis | Enum | Answers |
|---|---|---|
| **Run mode** | `ibRunMode` | *which process am I* — launcher / designer / enterprise / wes / service |
| **Database mode** | `ibDatabaseMode` | *how is the data reached* — a file, or a DB server |

Both live in `backend/appData.h`, and any combination is possible: the Designer can open a
file base, `wenterprise-server.exe` can serve a file base (ill-advised — §5), and
`codeRunner.exe` can hit a server base.

```cpp
enum ibDatabaseMode {
    eFILE,
    eSERVER,

    eNONE = 1000
};
```

`eNONE` is the un-connected state — `ibApplicationData` constructs with
`m_dbMode(ibDatabaseMode::eNONE)` and only the successful open assigns a real mode. The
`1000` gap keeps it out of the way of `eFILE`/`eSERVER` in serialised values.

The user-facing labels are exactly two words (`appData.h`):

```cpp
case eNONE:   return wxEmptyString;
case eFILE:   return _("File");
case eSERVER: return _("Server");
```

---

## 2. File mode — embedded Firebird over a directory

Selected when a **directory** is given. The connection is embedded Firebird opening
`sys.fdb` inside it (`appData.cpp`):

```cpp
std::shared_ptr<ibDatabaseLayerFirebird> db(new ibDatabaseLayerFirebird());
wxString pathSep = wxFileName::GetPathSeparator();
if (db->Open(strDirDatabase + pathSep + sys_db)) {
    s_instance = new ibApplicationData(runMode);
    s_instance->m_strFile = strDirDatabase;      // ← the DIRECTORY is the identity
    s_instance->ReadEngineConfig();
    s_instance->m_dbMode = ibDatabaseMode::eFILE;
    …
}
```

Key facts:

- **The base *is* a directory containing `sys.fdb`.** `m_strFile` holds that directory, not
  a file path. Everything else in the mode follows from that.
- The driver is **always Firebird** in this mode — the embedded engine, no server process.
- `ReadEngineConfig()` runs from the same directory, so per-base engine settings travel
  with the base.

## 3. Server mode

Selected when a server/database pair is given (`m_strServer` / `m_strDatabase`). The
concrete driver comes from the connection settings — Firebird, PostgreSQL, MySQL or ODBC
([../CLAUDE.md](../CLAUDE.md) §1). There is no embedded engine in the process.

---

## 4. What the mode actually changes

### 4.1 Where the log lives (`ResolveLogDir`)

```cpp
if (m_dbMode == ibDatabaseMode::eFILE) {
    // Lives next to sys.fdb — admins see logs alongside the base.
    return m_strFile + sep + wxT("oeslog");
}
if (m_dbMode == ibDatabaseMode::eSERVER) {
    // Per-user persistent location. %TEMP% would be wiped by Windows disk cleanup;
    // %LOCALAPPDATA% survives reboots and "temp-file cleanup".
    wxString tag = m_strDatabase;
    if (tag.IsEmpty()) tag = m_strServer;
    if (tag.IsEmpty()) tag = wxT("default");
    tag.Replace(wxT("\\"), wxT("_"));   // path separators in db names would break Mkdir
    …
}
```

- **File base** → `<base-dir>/oeslog`. The log sits *with* the data, which is what an admin
  expects when the whole base is a folder they can copy.
- **Server base** → a per-user folder under `%LOCALAPPDATA%`, tagged by database (falling
  back to server, then `"default"`). Two reasons stated in the source: `%TEMP%` gets wiped
  by Windows disk cleanup, and **until the compute server arrives this is the only place a
  client's journal lives** — the log is client-side, so it must survive a reboot.
- The tag is sanitised because a database name can contain path separators.

See [audit-log.md](audit-log.md) for what goes into the journal.

### 4.2 The connection pool

Both modes go through the same pool — it is the single owner of every connection in the
process:

```cpp
s_instance->m_connectionPool->Init(db, 32, PickConnectionMinIdle(runMode));
```

- **Size 32** — slack over ~20 concurrent user sessions.
- **`minIdle` is picked per *run* mode**, not per database mode: a server host pre-warms
  connections, a GUI process does not. Beyond `minIdle`, clones grow lazily and shrink on
  an idle timeout.
- In file mode the master `db` is the embedded connection; the pool holds it as `m_source`
  for `Clone()` **and** as the first idle entry, so the earliest `Checkout` hands it out
  directly.

Details and invariants: [connection-pool.md](connection-pool.md).

### 4.3 Schema creation

The open path also carries first-run creation:

```cpp
if (runMode == ibRunMode::eDESIGNER_MODE && !ibApplicationData::TableAlreadyCreated()) { … }
```

Creating a base is a **Designer** act — the two axes meeting in one condition.

---

## 5. Honest remainder — the file-mode ceiling

File mode is embedded Firebird. Sharing a file base across users means sharing the
**directory** — a network share — and that is the classic failure mode of a file-based
RDBMS: concurrent writes over SMB. [firebird-mesh-driver.md](firebird-mesh-driver.md) puts
the boundary at roughly **8 users / 5 GB**, past which the choice is "keep wrestling with
file-mode" or move to a server.

Read the modes as a deployment ladder, not as equals:

| | File | Server |
|---|---|---|
| Setup | copy a folder | install and administer a DBMS |
| Drivers | Firebird only (embedded) | Firebird / PostgreSQL / MySQL / ODBC |
| Log | with the base | per-user `%LOCALAPPDATA%` |
| Concurrency | small teams; degrades over a share | the real answer |

**Oracle and MSSQL drivers do not exist** ([ROADMAP.md § 5](ROADMAP.md)).
