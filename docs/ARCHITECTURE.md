# OES Architecture

## Table of Contents

1. [System Overview](#system-overview)
2. [Layer Descriptions](#layer-descriptions)
3. [Application Bootstrap and Ownership](#application-bootstrap-and-ownership)
4. [Module Descriptions](#module-descriptions)
5. [Bytecode Engine](#bytecode-engine)
6. [Metadata System](#metadata-system)
7. [Sessions and Runtime Ownership](#sessions-and-runtime-ownership)
8. [Database Abstraction](#database-abstraction)
9. [Form System](#form-system)
10. [Debugger Architecture](#debugger-architecture)
11. [Key Data Flows](#key-data-flows)

---

## System Overview

```
┌──────────────────────────────────────────────────────────────┐
│                      Executables                             │
│  designer.exe   enterprise.exe   launcher.exe  appserver.exe │
│  codeRunner.exe                                              │
└────────────┬─────────────────┬────────────────┬─────────────┘
             │                 │                │
             ▼                 ▼                ▼
┌────────────────────┐  ┌────────────────────────────────────┐
│   frontend.dll     │  │           backend.dll              │
│                    │  │                                    │
│  ibValueForm       │◄─►  ibApplicationHost → its bases     │
│  ibVisualHost      │  │  ibMetaDataConfiguration           │
│  18 Controls       │  │  ibCompileCode / ibProcUnit        │
│  ibMainFrame       │  │  ibDatabaseLayer (+ 4 drivers)     │
│  Property editor   │  │  ibDebuggerServer                  │
└────────────────────┘  └────────────────┬───────────────────┘
                                         │
                        ┌────────────────▼────────────────────┐
                        │         Database tier                │
                        │  Firebird  PostgreSQL   (production) │
                        │  SQLite    (tests + logging)         │
                        │  ODBC      (base for MSSQL)          │
                        └─────────────────────────────────────┘
```

Communication between `frontend.dll` and `backend.dll` goes through abstract C++ interfaces exported from `backend.dll`. The frontend never accesses database drivers or the compiler directly.

---

## Layer Descriptions

### Application Layer (executables)

Each executable links against both DLLs and provides a `wxApp` subclass that selects the run mode and the kind of its own session:

| Executable | Run Mode (`ibRunMode`) | Own session (`ibSessionKind`) | Purpose |
|---|---|---|---|
| `launcher.exe` | `eLAUNCHER_MODE` | — | Connection chooser; creates/selects database |
| `designer.exe` | `eFILE_MODE` | `Designer` | Full IDE — metadata editor, form designer, debugger client |
| `enterprise.exe` | `eFILE_MODE` | `Enterprise` | Desktop thick-client runtime (GUI, single user session per process) |
| `wenterprise-server.exe` | `eFILE_MODE` | `WebServer` | Web runtime host — HTTP server, N per-cookie `WebClient` sessions, browser client; given its DBMS on the command line |
| `appserver.exe` | `eSERVER_MODE` | `Service` | The application server: holds the bases of its server folder, headless; protocol clients are `ThinClient` / `ThinDesigner` |
| `codeRunner.exe` | `eSANDBOX_MODE` | — | Executes a single script module |

> The run mode says only how the process holds the base — serving it (`eSERVER_MODE`) or as a file base for the one who started it (`eFILE_MODE`). What a session works in is its kind: either mode hosts designers, runtimes and jobs alike, and `DesignerMode()` / `EnterpriseMode()` are answered by the current session's kind. The base's FIRST SESSION picks its configuration by its kind — editable for a designer kind, runtime otherwise; a server's first session is the server itself.

### Backend Layer (`backend.dll`)

The backend is the core engine. It is self-contained — no GUI dependencies. Key objects:

- **`ibApplicationHost`** (`src/engine/backend/appHost.h`) — the PROCESS: the bases it holds, and what belongs to the process rather than to any base — the plugins, the platform locale and the syntax-helper corpus, the one worker pool, the limits read from `backend.conf`. See [Processes and bases](#processes-and-bases).
- **`ibApplicationInstance`** (`src/engine/backend/appData.h`) — ONE BASE: its connection pool, lock manager, session registry, job manager, MCP server, settings storage, logger and metadata. A process holds one (every desktop host, the web host, the tests) or several (the application server). Reached through the `appData` macro, which answers the base of the session the thread works for — there is no global current base. Per-session state (user info, ProcUnits, frame) lives on `ibSession`; sys_user on `ibUserInfo`; the sys_session snapshot on `ibSessionSnapshot` produced by `ibSessionRegistry`.
- **`ibMetaDataConfiguration`** (`src/engine/backend/metadataConfiguration.h`) — loads, saves, and manages the metadata tree (all business objects). Accessed via `activeMetaData` — the calling session's own configuration. Stores compile cache (compiled bytecode) per module descriptor; runtime instances live in sessions.
- **`ibSession` / `ibSessionRegistry`** (`src/engine/backend/session/`) — per-session state and the base's session manager (one registry per base). The session is reachable via `ibSession::Current()`: the calling thread's own binding, else the base's fallback — the process's own session in that base (never a job's or a web tab's). `ibSessionScope` and `ibSessionThreadBinding` are the RAII helpers that bind a session to the calling thread. See [Sessions and Runtime Ownership](#sessions-and-runtime-ownership).
- **`ibDebuggerServer`** (`src/engine/backend/debugger/debugServer.h`) — TCP server that accepts designer connections and relays debugger events.

### Frontend Layer (`frontend.dll`, `wfrontend.dll`)

Two sibling DLLs share the same form/view/control code paths through `OES_USE_WEB` ifdefs and `ibFrontendWindow` typedef (`wxWindow` for desktop, `ibWebWindow` for web):

- **`frontend.dll`** — wxWidgets GUI. Used by `enterprise.exe`, `designer.exe`, `launcher.exe`, `codeRunner.exe`. (`appserver.exe` links `backend` alone.)
- **`wfrontend.dll`** — web UI (HTML serialisation of form control trees via `ToJSON()`, cpp-httplib transport). Used by `wenterprise-server.exe`.

Shared frontend objects:
- **`ibValueForm`** — the runtime representation of an open form; holds the control tree and responds to user events. Same class on both builds.
- **`ibVisualHost` / `ibVisualHostClient`** — render and input routing surface. Desktop = wxWindow; web = `ibWebWindow` tree serialised to JSON.
- **Doc-view frames** — backend-facing interface `ibBackendDocFrame` (`backend/backend_mainFrame.h`); concrete implementations are `ibFrontendMainFrame` (desktop, `frontend/mainFrame/mainFrame.h`: `public ibBackendDocFrame, public wxAuiMDIParentFrame, public ibDocParentFrameAnyBase`) and `ibWebFrame` (web). Child frames are `ibAuiDocChildFrame` / `ibDialogDocChildFrame` on desktop (`frontend/mainFrame/mainFrameChild.h`) and `ibWebDocChildFrame` on web. The frame is owned by the `ibSession` that created it (no process-level singleton on the backend side); the legacy `mainFrame` macro still exists in `frontend/mainFrame/mainFrame.h` as a frontend-local accessor for the GUI singleton, but new backend code reaches the frame through `ibSession::Current()->GetFrame()` or `ibSession::CurrentFrame()`.
- **`ibCodeEditor`** (`frontend/win/editor/codeEditor/`) — the Scintilla-based script editor. Lives in `frontend.dll` so any GUI host can use it: `designer.exe` (its module/form editors), `codeRunner.exe` (sessionless scratch runner). Highlighter, fold parser, auto-indent on Enter, format / increase / decrease indent, comment add/remove, Ctrl-Space autocomplete, GotoLine + ProceduresAndFunctions dialogs all live here. Debugger integration is a designer-only concern, kept out of the base via six virtual hooks (`IsDebuggerEnterLoop`, `OnEditDebugPoint`, `OnPatchModule`, `OnEvaluateAutocomplete`, `OnEvaluateToolTip`, `RefreshBreakpointMarkers`); designer's `ibCodeEditorDesigner` (`designer/win/editor/codeEditor/codeEditorDesigner.{h,cpp}`) overrides them with `debugClient->…` calls. Document-less / sessionless mode: passing `nullptr` for the `ibMetaDocument*` skips metadata-driven autocomplete and breakpoint markers but keeps everything else functional — codeRunner uses this to embed the same editor without any DB / metadata / debug infrastructure.

---

## Application Bootstrap and Ownership

### Processes and bases

A process holds **one base or several**. `ibApplicationHost` (`backend/appHost.h`) is the process: it owns its bases (`std::unique_ptr<ibApplicationInstance>`, in the order they were opened) and whatever is the process's rather than a base's. `ibApplicationInstance` (`backend/appData.h`) is one base and owns that base's subsystems. Every desktop host, the web host and the tests hold one base; the application server (`appserver.exe`) holds every base of its server folder. Design and decisions: `docs/private/multi-base-process.md`.

There is **no global current base**. A thread reaches a base through a chain of owners: its session → the session's registry → the base → the process. `ibApplicationInstance::Get()` (the `appData` macro) answers the base of the session the thread works for, else the base the thread is bound to (`ibApplicationInstanceScope`: the thread opening or closing a base, a model's read thread, the web server's HTTP threads). A thread with neither is refused with an exception; `Get(false)` answers null instead, for destructors and teardown. A base's own service threads (the registry, the job tick) do not bind — they hold their base and ask it. Every `Get*()` returns `nullptr` before the first base opens and after the last one closes.

| Owner | Holds |
|---|---|
| `ibApplicationHost` — the process | its bases; the plugins (loaded once); the platform locale and the syntax-helper corpus; ONE worker pool for every base; the limits of `backend.conf` (`Workers`, `Bases`, the default `Connections`) |
| `ibApplicationInstance` — a base | connection pool (sized by the base's own `infobase.conf`), lock manager, L4 queryable factory, session registry, job manager, MCP server, settings storage, logger, active metadata |
| `ibSession` | its connection holder, its runtime (ProcUnits), its frame or visual host |

A connection layer knows its pool (`ibDatabaseLayer::GetPool`), so a transaction is pinned in the base it runs in, never in "the current" one.

### Opening a base

```
  exe-specific main()
    • argv parsing, runMode pick
    • ibCrashGuard::Install (headless) OR ibWxApp::OnInit (GUI)
    • ibApplicationInstance::CreateAppDataEnv(ibFileInstanceRequest)  ← the type is the run mode; name, locale,
                                                      the folder, a PostgreSQL server when named (else Firebird)
      or CreateAppDataEnv(ibServerInstanceRequest)  ← the server folder + the base's name: the DBMS is read
                                                      from server.conf by the opening itself
          │
          ├─ ibApplicationHost::Ensure(runMode)   the process comes up with its first base;
          │                                       one beyond backend.conf `Bases` is refused
          ├─ the driver opens the database        Firebird file / PostgreSQL server
          ├─ new ibApplicationInstance            its subsystems, in init order (below)
          └─ ibApplicationInstance::Open          ONE road for every kind of base:
               listed in the process, the opening thread working for it,
               pool (infobase.conf `Connections`), CanOpen (held by another process?),
               system tables + migrations, locale, logger, platform jobs.
               Refused or thrown → closed again, alone; the other bases stay.
```

### Construction order (`ibApplicationInstance::ibApplicationInstance`)

The init list is **the** ordering contract:

| # | Field | Why this slot |
|---|---|---|
| 1 | `m_connectionPool` | Everything else needs DB access. `Open` inits it once the database is open. |
| 2 | `m_lockManager` | Its holder names the pool from the token — it never asks "the current" one. |
| 3 | `m_queryableFactory` | L4 query-engine source factory; its descriptors follow the metadata's open/close. |
| 4 | `m_sessionRegistry` | After the pool, so the first session can already check connections out. Its work runs on the process's worker pool. |
| 5 | `m_jobManager` | The base's scheduled jobs; each run opens a session of its own. |
| 6 | `m_mcpServer` | The designer's MCP listener — started by a designer session. |
| 7 | `m_settingsStorage` | sys_settings — what people saved on their forms and lists. |

`m_logger` is created by `Open` (`CreateLogger`) after the tables exist. `m_activeMetaData` is populated by `CreateActiveMetaData(applicationInstance, kind, flags)` at the base's first session, by its kind — `ibMetaDataConfigurationStorage` for a designer kind, `ibMetaDataConfiguration` otherwise; launcher and codeRunner have none. It is held for the sessions to come: a session takes its own reference when it is let in and lets it go when it leaves. A designer's apply replaces it with the configuration it published (`ReplaceActiveMetaData`); sessions already working finish on the old one, which closes with its last holder. Every metadata — a configuration, an external report or data processor, a configuration file — is made by `ibMetaData::MakeShared` and held by `shared_ptr` by whoever works in it. The plugins and the syntax-helper corpus (constructed in `InitLocale()` once the locale is settled — see `docs/syntax-helper-design.md`) are the process's.

### Closing a base (`ibApplicationHost::Close` → `ibApplicationInstance::Close`)

Two steps. The base is first **closed while it is still listed** — its sessions, removing their rows as the registry stops, ask `ibApplicationHost::HasRegistry` whether their registry still stands, and the answer must be yes — and only then taken out of the set and freed, an empty shell:

```
ibApplicationInstance::Close() {
    m_jobManager->Stop();               // nothing of ours still runs against what goes next
    m_mcpServer->Stop();
    m_sessionRegistry->Stop();          // drains this base's work off the shared pool, DELETEs sys_session
    m_activeMetaData->OnDestroy();
    m_connectionPool->Shutdown();
    // then every field is released in reverse declaration order, each emptied under the
    // process's lock first, so a question asked after it is gone reads "none"
}
```

`ibApplicationHost::CloseAll` closes every base newest first, then the process: its worker pool is joined, then the plugins unload. `Close()` is idempotent — the destructor runs it again over the shell. If a new subsystem joins, add its hook to `Close()` and append the field in declaration order so it dies before its dependencies.

### Token pattern for subsystem construction

Every owned subsystem's ctor takes `ib::AppDataCtorToken` (`backend/appDataCtorToken.h`). Its ctor is private and only `ibApplicationInstance` and `ibApplicationHost` are friends, so external code that tries `new ibSessionRegistry(…)` gets a compile error. The token also says **whose**: a base mints `AppDataCtorToken{ this }` and the subsystem reads its owner off it (`GetApplicationInstance()`); the host mints a null one for the process's share (the plugins, the corpus).

Why a token instead of a friend declaration on each subsystem header? Friends scattered across seven headers mean refactoring the owner ripples into every owned class. One token type keeps the "who can build subsystems" decision in one place; subsystem headers declare their ctors public.

### The application server (`appserver.exe`, `src/engine/appserver/`)

A headless process that serves the bases of its **server folder** (`--dir`, by default `server` beside the executable):

```
<server folder>/
  server.conf      one group per base: Kind=firebird|postgresql, Id, Path or Server/Port/Database/User/Password,
                   IbUser/IbPassword — the Id is assigned at the first start and written back
  server.key       this installation's key, made at the first start
  <Id>/            one folder per base: its journal (oeslog) and infobase.conf; a Firebird base's sys.fdb too
```

- **Settings in three layers**, key by key: built-in values → `backend.conf` (the process: `Locale`, `Workers`, `Bases`, the default `Connections`) → the base's `infobase.conf` (`Connections` to ITS DBMS). `Workers` and `Bases` are a maximum, 0 = no limit (the pool grows as sessions bring work); `Connections` 0 = the default; a value that cannot be used is said in the journal (`ibApplicationHost::ReadCount`).
- **Secrets** are sealed in `server.conf` with AES-256-GCM (`ibFieldCipher`, key in `server.key`); plain text is refused. `appserver --set-password=<base>/<Password|IbPassword>` reads one from the keyboard and seals it.
- **Opening:** every base through `CreateFile/ServerAppDataEnv` in `eSERVER_MODE`, then a session of kind `Service` logged into it. A base that does not open or refuses the login is said and closed alone; the rest are served. Firebird and PostgreSQL bases share one process.
- **Who may come in** (`ibServiceExclusivePolicy`, asked at open by `CanOpen` and at every session by `CanAdd`): application servers (`eSERVER_MODE`) share a base with application servers, file bases with file bases — a file-base process (designer, enterprise, the web host) is refused while a server serves the base, and a server while a file base uses it. The row's run mode decides, whatever its kind: a server's thin clients are the server's. What several servers on one base share already lives in it: `sys_session`, `sys_lock`, a job's claim and its clock in `sys_job`.
- **Journal:** every line about a base carries its name — `(trade1) source …`. The console shows the server's own lines and every warning and error; the running commentary stays in the file.
- **Stop:** Ctrl+C, SIGTERM or the console closing raises a flag; the main thread ends the server's sessions, then closes every base newest first.
- **Not yet:** a port and a protocol for clients (the host's protocol is the next stage), choosing which bases to start (`--base`, for a coordinator), running as a Windows service, creating a new base (only the designer creates the system tables).

Unit tests build subsystems directly (no full appData) — the test CMake target defines `OES_TESTING`, which opens the token's default ctor inside test TUs only. Production builds (sln / CMake non-test) leave it undefined and the gate stays closed.

### Entry-point helpers

Two header-only helpers in `frontend/diagnostics/` cover the boilerplate every binary needs at startup. Header-only = no link-time `frontend.dll` dependency; both helpers only call `BACKEND_API ibCrashGuard::*` plus wx primitives the binary already links.

| Binary type | Helper | What it wires |
|---|---|---|
| GUI (`enterprise` / `designer` / `codeRunner` / `launcher`) | `ibWxApp` base class (`oesApp.h`) | `wxApp::OnInit` → `ibCrashGuard::Install` + `DoOnInit()`; `OnRun` → try/catch around `DoOnRun()`; 3 exception overrides (main-loop / unhandled / fatal). Subclass overrides only `GetExeName()` + optional `DoOnRun()`. |
| Console (`wenterprise-server` / `appserver`) | `ibOesConsoleBoot` RAII (`oesConsole.h`) | Single object that holds a `wxInitializer`, runs `wxSocketBase::Initialize`, calls `ibCrashGuard::Install`. `IsOk()` checks the wx init result. |

### Crash plumbing layers

```
  backend/diagnostics/crashGuard.{h,cpp}  ─── BACKEND_API. Headless:
                                              SEH filter / signal handlers /
                                              minidump writer / std::set_terminate /
                                              <exe>_terminate.log + <exe>_startup.log /
                                              MessageBoxW (Win) | fprintf+stderr (POSIX)
                                              No wx UI, no main loop required.

  frontend/diagnostics/oesApp.h           ─── Header-only. wxApp pipeline:
                                              wxDebugReportPreviewStd, wxMessageBox,
                                              wxLogError, wxTheApp->CallAfter. Delegates
                                              logging to crashGuard.

  frontend/diagnostics/oesConsole.h       ─── Header-only. RAII boot helper for
                                              console binaries. Wraps wxInitializer +
                                              wxSocket init + crashGuard::Install.

  wfrontend layer (web)                   ─── cpp-httplib set_exception_handler in
                                              wes' main.cpp emits JSON 500 with
                                              Kind / native_code / sqlstate. crashGuard
                                              handles process-level faults underneath.
```

### Exception taxonomy

```
  ibBackendException                      ─── base; per-thread error chain
    │                                          (PushLastError / DrainLastErrors).
    │
    ├── ibBackendDatabaseException        ─── DB-tier failure. Enum Kind:
    │     │                                    ConnectionLost / Syntax / Constraint /
    │     │                                    Deadlock / Timeout / Unknown.
    │     │                                    IsRetryable() derived from Kind.
    │     │
    │     └── ibDatabaseLayerException    ─── Concrete driver throw. Adds
    │                                          GetDriverErrorCode() + GetSqlState().
    │                                          Created via static Throw(...).
    │
    ├── ibBackendQueryException           ─── THIS TIER could not do its job.
    │     │                                    Kind: TranslationFailure /
    │     │                                    UnsupportedNode / PoolExhausted.
    │     │                                    (query/queryException.h)
    │     │
    │     ├── ibBackendQuerySourceException ── THE QUERY does not hold up.
    │     │                                    Carries line + column, so the
    │     │                                    constructor can point at the text.
    │     │
    │     └── ibBackendQueryLinqException  ─── THE PIPELINE does not hold up.
    │                                          A LINQ door fails at a STAGE, not
    │                                          at a caret — one type in place of
    │                                          the "LINQ: " prefix spelled by hand.
    │
    ├── ibBackendSessionException         ─── The SESSION refuses. Kind:
    │                                          ExclusiveHeld / OthersActive /
    │                                          NoSession / NoConnection.
    │                                          (session/sessionException.h)
    │
    ├── ibBackendLockException            ─── Optimistic-write conflict.
    │                                          Kind: VersionChanged / …
    │
    ├── ibBackendAccessException          ─── A right was refused; `subject`
    │                                          names WHAT, when affordable.
    │
    ├── ibBackendCoreException            ─── Everything the engine states in
    │                                          its own voice (static Error()).
    │
    └── ibBackendInterruptException       ─── The user stopped the program.
```

⭐ **Each subsystem owns its exception TYPE; `Kind` says what went wrong inside it.** One type
plus a string would make every handler match on message TEXT. The division is by WHO refused,
which is what a caller can act on — see [exceptions.md](private/exceptions.md).

⚠ `NoConnection` moved from the query type to the session one on 2026-08-14: *"there is no
connection to work on"* is the session refusing, not the query tier failing at its job, and holding
the kind in both places would have been two names for one refusal with nothing to choose between
them.

Per-driver `ClassifyDatabaseError(int nativeCode)` on each `ibDatabaseErrorReporter` subclass maps the driver's native code (Firebird `isc_*` gds, PG SQLSTATE int, ODBC SQLSTATE, SQLite result code) to a Kind. The mapping is regression-tested in `tests/test_dbTaxonomy.cpp` — if a future driver bump flips `SQLITE_CONSTRAINT (19)` away from `Kind::Constraint`, the test fails before code that branches on `IsRetryable()` silently misroutes.

`catch` discipline:
- **Inside RAII destructors and rollback / cleanup paths**: `catch (...) {}` is intentional (a destructor that throws is worse than a swallowed error during cleanup).
- **Inside business logic**: never swallow — log via the per-thread chain, rethrow, or surface via `ibCrashReporter::ReportStartupError` / `ibWxApp::OnExceptionInMainLoop`.

---

## Module Descriptions

### `src/engine/backend/compiler/`

| File | Class | Role |
|---|---|---|
| `translateCode.h/cpp` | `ibTranslateCode` | Lexer: tokenises source text into `ibLexem` stream |
| `compileCode.h/cpp` | `ibCompileCode` | Parser and code generator: consumes lexemes, emits `ibByteCode` |
| `byteCode.h` | `ibByteCode`, `ibByteUnit` | Bytecode container: array of `ibByteUnit` instructions |
| `byteCodeAOT.cpp` | `ibByteCode::SerializeAOT/DeserializeAOT` | Binary persistence for the AOT cache (`sys_bytecode_cache.bc_blob`); host-endian linear format with magic `'PBC1'` + format version |
| `procUnit.h/cpp` | `ibProcUnit` | Interpreter: executes `ibByteCode` against a variable stack |
| `procContext.h/cpp` | `ibRunContext` | Execution context: local variable frame, call stack |
| `value.h/cpp` | `ibValue` | Universal value type — tag enum `ibValueTypes` in `backend_core.h`: `TYPE_EMPTY` (=0, the "undefined" value), `TYPE_BOOLEAN`, `TYPE_NUMBER` (`ibNumber`), `TYPE_DATE`, `TYPE_STRING`, `TYPE_NULL`, `TYPE_REFFER` / `TYPE_CONST_REFFER`, object kinds `TYPE_VALUE` / `TYPE_ENUM` / `TYPE_OLE` / `TYPE_FUNCTION` / `TYPE_ITERATOR` |
| `codeDef.h` | enums | Opcode (`OPER_*`) and keyword (`KEY_*`) definitions |

### `src/engine/backend/databaseLayer/`

| File | Class | Role |
|---|---|---|
| `databaseLayer.h/cpp` | `ibDatabaseLayer` | Abstract base: Open/Close/RunQuery/PrepareStatement/Transactions |
| `databaseResultSet.h/cpp` | `ibDatabaseResultSet` | Abstract result set cursor |
| `preparedStatement.h` | `ibPreparedStatement` | Abstract prepared statement |
| `firebird/` | `ibDatabaseLayerFirebird` | Firebird 3/4/5 driver (INT128 via `SQL_INT128`; RLS confirmed live on FB5) |
| `postgres/` | `ibDatabaseLayerPostgres` | PostgreSQL driver |
| `sqllite/` | `ibDatabaseLayerSQLite` | SQLite 3 driver (note: directory named `sqllite`) |
| `odbc/` | `ibDatabaseLayerODBC` | ODBC generic driver |

> Concrete driver classes use the `ib<Base><Vendor>` suffix pattern: `ibDatabaseLayer<Vendor>`, `ibDatabaseResultSet<Vendor>`, `ibPreparedStatement<Vendor>` (e.g. `ibDatabaseResultSetFirebird`, `ibPreparedStatementPostgres`).

### `src/engine/backend/query/`

The source-agnostic query floor — L3 and the contracts every tier speaks — plus the schema
projection the restructuring runs on. **The floor plan is
[query-engine-layers.md](private/query-engine-layers.md); it is the one to read first** and this table only
says which file is where.

| File | Holds |
|---|---|
| `queryProvider.h/cpp` | `ibBackendQueryProvider` — the whole L3↔L2-1 layer: turns the door's calls into L2-1 IR |
| `queryable.h` | `ibBackendQueryable` — the source-navigation contract L3 reads a metaobject through. A pure mixin, no data, second base so `ibValue` stays at offset 0 |
| `queryColumn.h` | `ibBackendQueryColumn` — the column counterpart. Deliberately a LIGHT header (an attribute derives from it), so nothing GUI-shaped may enter |
| `queryAST.h`, `queryParser.*`, `queryLexer.*`, `queryRender.*`, `queryRewrite.*` | the L4-1 text query — one AST shared with LINQ, its reader, its writer, and the rules that move a term (`WHERE` → `HAVING`) |
| `queryLowering.*` | L4 → the single L3 door: name resolution, aggregate rules, `DescribeOutput`, package execution |
| `queryHierarchy.{h,cpp}` | `ibQueryHierarchyScope` — the parent walk and the "whom does this row report under" map, asked of the COLUMN through the provider (one read, not one per node). Left the accounting register in 2026-08-13 |
| `queryUnfold.h` | `ibQueryDimUnfold` — `Elements` / `Hierarchy` / `HierarchyOnly`, the three words a dimension can be asked for |
| `queryException.{h,cpp}` | `ibBackendQueryException` (this tier failed) and `ibBackendQuerySourceException` (the query itself does not hold up — carries line + column) |
| `schemaSnapshot.{h,cpp}` | `ibSchemaSnapshot` + `DiffSnapshots` — projects a configuration into tables / columns / indexes and turns a PAIR of them into DDL. Knows nothing about metadata, and never asks the live database anything ([schema-authority.md](private/schema-authority.md)) |
| `schemaBuilder.*`, `structureBuilder.*`, `structureBatch.*` | applying that difference: rendering and running the DDL, the deferred second phase, and its compensation |
| `derivedStateBuilder.cpp` | L3-4 — regenerating what a changed declaration made stale |
| `tempTableManager.*`, `tempTableQueryable.h` | temp tables as ordinary sources (`ibTempTableManager` decides how long one lives) |
| `dbTableProvider.cpp` | the DB-backed provider: read, aggregate and the two NON-terminal endings (`BuildAggregateRelation` / `BuildReadRelation`) that let a reading hand back a relation instead of rows |

### `src/engine/backend/metaCollection/`

Metadata object hierarchy. Every business object type extends `ibValueMetaObject` (defined in `metaObject.h`), which itself extends `ibValue` so metadata objects can be passed as script values.

**One translation unit per ASPECT — `<metatype>Metadata<Aspect>.cpp`.** A metatype's files sit
together in `metaCollection/partial/`, named after the metatype (the file name follows the metatype,
not the C++ class: `chartOfAccounts*`, not `ibValueMetaObjectChartOfAccounts*`). The METAOBJECT's
aspects carry the `Metadata` infix; the runtime value, its manager and its commands do not.

| File | Holds |
|---|---|
| `chartOfAccounts.h` | the class declarations |
| `chartOfAccountsMetadata.cpp` | the metaobject — ctor, lifecycle events, `ReadData` / `WriteData` |
| `chartOfAccountsMetadataSchema.cpp` | **the database schema** — `ContributeTables` plus the restructuring guards those tables carry |
| `chartOfAccountsMetadataProperty.cpp` | the property hooks (`OnPropertyChanged`, …) |
| `chartOfAccountsMetadataMenu.cpp` | the designer context menu |
| `chartOfAccountsMetadata_res.cpp` | the embedded icons |
| `chartOfAccountsObject.cpp`, `…Manager.cpp`, `…Manager_impl.cpp`, `…Action.cpp`, `…Enum.{h,cpp}` | the runtime value, its manager, its commands, the enum value types the family registers — none of these is the metaobject, hence no infix |

Aspects in use: `Schema`, `Property`, `Menu`, `_res`, plus three one-of-a-kind readings that earn a
file each — `accumulationRegisterMetadataTotals.cpp`, `informationRegisterMetadataSlice.cpp`,
`constantMetadataQuery.cpp`. A base FAMILY is named after its base header instead of a metatype:
`commonObject.cpp`, `commonObjectSchema.cpp`, `commonObjectProperty.cpp`, `commonObjectAction.cpp`,
`commonObjectMetaQuery.cpp`, `commonObjectEnum.{h,cpp}`.

**The rule for new code:** everything a metaobject contributes to the database SCHEMA —
`ContributeTables` and the `m_beforeChange` / `m_afterChange` rules attached to the tables it
declares — goes in `<metatype>MetadataSchema.cpp`, so "what does this become in the database" is one
file per metatype rather than a section of a large one. Seven exist: `commonObjectSchema.cpp` (the
record / enum / register / hierarchy families), `accumulationRegisterMetadataSchema.cpp` (the derived
totals bundle), `accountingRegisterMetadataSchema.cpp` (the two-sided totals bundle and its views),
`chartOfAccountsMetadataSchema.cpp` (the analytics-ceiling rule),
`constantMetadataSchema.cpp` (one column in the shared external `sys_const`),
`calculationRegisterMetadataSchema.cpp` (the lookup indexes `_DIX` / `_RIX`, and the recalculation's marks
where `UseRecalculation` is on). Declaring the table and
declaring the rule that guards it are the same statement, so they cannot drift apart —
[query-engine-layers.md](private/query-engine-layers.md) § L3.

### `src/engine/backend/debugger/`

| File | Class | Role |
|---|---|---|
| `debugServer.h/cpp` | `ibDebuggerServer` | TCP server accepting designer connections |
| `debugClient.h/cpp` | `ibDebuggerClient` | Client-side connection (runs in designer) |
| `debugClientBridge.h/cpp` | `ibDebuggerClientBridge` | Bridge between client socket and UI |
| `debugDefs.h` | enums | `CommandId`, `EventId`, `ConnectionType` |

### `src/engine/backend/system/`

| File | Class | Role |
|---|---|---|
| `systemManager.h/cpp` | `ibSystemManager` | Built-in function dispatcher; registers 92 functions + 6 procedures = 98 built-ins as of 2026-08-08 (count drifts as features land; grep `AppendFunc\|AppendProc` for the live total) |
| `systemEnum.h` | enums | System-level enumeration constants |

### `src/engine/frontend/visualView/`

| File/Dir | Content |
|---|---|
| `ctrl/form.h/cpp` | `ibValueForm`, form control collection, action wiring |
| `ctrl/control.h/cpp` | `ibControlFrame` base class for all controls |
| `ctrl/widgets.h` | Declarations for all control types |
| `visualHost.h/cpp` | `ibVisualHost` — wxWindow rendering surface |
| `visualHostClient.h/cpp` | Client-side form binding |
| `ctrl/` | Individual control implementations (see Form System) |

---

## Bytecode Engine

### Compiler Pipeline

```
Source text (wxString)
        │
        ▼
 ibTranslateCode::Load() → PrepareLexem()
   Lexer: fills m_listLexem (vector<ibLexem>)
   Each ibLexem: { type, keyword/delimiter number, string data, ibValue, line, position }
        │
        ▼
 ibCompileCode::Compile()       (ibCompileCode : public ibTranslateCode)
   Recursive-descent parser
   Emits ibByteUnit records into ibByteCode
   Resolves forward function references in a second pass
        │
        ▼
 ibByteCode
   m_listCode  : vector<ibByteUnit>          — instruction stream
   m_listConst : vector<ibValue>             — constant pool (primitives)
   m_listVar   : vector<ibByteCodeVarInfo>   — unified symbol table,
                 kind-tagged {Local, External, Context, ContextProp}
   m_listFunc  : vector<ibByteFunction>      — unified function table,
                 kind-tagged {Local, Export, ContextMethod, Lambda}
   (names live on m_strRealName; storage is vector — stable order for AOT)
        │
        ▼
 ibProcUnit::Execute()
   Stack machine with ibRunContext frames
   Dispatches on ibByteUnit::m_numOper
```

### Opcode Categories

Opcodes are defined as plain integer constants in `src/engine/backend/compiler/codeDef.h`. The opcodes fall into these groups:

| Category | Opcodes |
|---|---|
| Arithmetic | `OPER_ADD`, `OPER_SUB`, `OPER_MULT`, `OPER_DIV`, `OPER_MOD`, `OPER_INVERT` |
| Comparison | `OPER_GT`, `OPER_EQ`, `OPER_LS`, `OPER_GE`, `OPER_LE`, `OPER_NE` |
| Logical | `OPER_NOT`, `OPER_AND`, `OPER_OR` |
| Control flow | `OPER_GOTO`, `OPER_IF`, `OPER_FOR`, `OPER_FOREACH`, `OPER_IN`, `OPER_NEXT`, `OPER_NEXT_ITER` |
| Variables | `OPER_LET`, `OPER_CONST`, `OPER_CONSTN`, `OPER_SET`, `OPER_SETREF`, `OPER_SETCONST` |
| Functions | `OPER_FUNC`, `OPER_ENDFUNC`, `OPER_CALL`, `OPER_CALL_CLOSURE` (heap-frame variant when the callee has an inner lambda capturing locals), `OPER_CALL_METHOD`, `OPER_RET` |
| Lambdas | `OPER_LFUNC` (anonymous body entry — materialises an `ibValueFunction` value at its dest slot in one step), `OPER_ENDLFUNC` (body close — distinct from `OPER_FUNC`/`OPER_ENDFUNC` so a containing named-function's module-init skip doesn't terminate on a nested lambda's terminator), `OPER_CALL_LAMBDA` (dynamic call — target read from a slot at runtime, must wrap an `ibValueFunction`). See `docs/lambda.md`. `OPER_FUNC_PTR` was an earlier separate materialise opcode — retired; doc references kept for git-blame readability only. |
| LINQ | `OPER_CALL_LINQ` — universal pipeline method on an iterable receiver (Where / Select / OrderBy / GroupBy / Join / Skip / Take / Aggregate / ...). Compile-side detects LINQ method names at chain-method emit time and chooses this opcode over `OPER_CALL_METHOD`; runtime reads the `ibValue::ibLinqMethod` enum id directly from `m_param3.m_numIndex` (no const-string lookup, no `FindMethod` walk) and dispatches through the virtual `ibValue::DispatchLinqMethod`. See `docs/linq.md`. |
| Arrays | `OPER_GET_ARRAY`, `OPER_SET_ARRAY`, `OPER_CHECK_ARRAY`, `OPER_SET_ARRAY_SIZE`, `OPER_ENTER_A`, `OPER_GET_A`, `OPER_SET_A` |
| Objects | `OPER_NEW`, `OPER_SET_TYPE` |
| Exceptions | `OPER_TRY`, `OPER_ENDTRY`, `OPER_RAISE`, `OPER_RAISE_T` |
| Optimised const variants | `OPER_ADDCONS`, `OPER_SUBCONS`, `OPER_MULTCONS`, `OPER_DIVCONS`, `OPER_MODCONS`, `OPER_GTCONS`, etc. |

Each opcode has type-specialised variants selected by adding `TYPE_DELTA1` (number), `TYPE_DELTA2` (string), `TYPE_DELTA3` (date), or `TYPE_DELTA4` (boolean) to the base opcode. The `TYPE_DELTAn` macros must stay **parenthesised** — a missing outer paren silently breaks `%`/`/` uses (add/subtract are unaffected), which once left the `shortLet` peephole dead; see [compiler-pipeline.md](private/compiler-pipeline.md) §3.1.

### Execution Model

`ibProcUnit` maintains a linked list of parent `ibProcUnit` objects representing the module scope chain. Each call creates an `ibRunContext` holding the local variable array. Exceptions are thrown by value and caught by const reference (standard C++): `throw ibBackendInterruptException();` (often through static `Error()` helpers like `ibBackendCoreException::Error(_("..."))`) caught as `catch (const ibBackendException& err)`. `ibBackendException` has a virtual destructor so catching by base preserves dynamic type for `dynamic_cast`. Rethrow uses bare `throw;`. The previous throw-by-pointer / `catch(const ibBackendException*)` pattern is fully removed.

### Keyword Inventory

62 keywords defined in `KEY_*` enumerators (`codeDef.h`, `KEY_IF=0` … `KEY_RESTRICT`, before the `LastKeyWord` sentinel), in lock-step with `s_listKeyWord[]` in `translateCode.cpp`. Groups:
- Control / structure — `KEY_IF`, `KEY_FOR`, `KEY_FOREACH`, `KEY_WHILE`, `KEY_PROCEDURE`, `KEY_FUNCTION`, `KEY_TRY`, `KEY_EXCEPT`, `KEY_ENDTRY`, `KEY_RAISE`, `KEY_RETURN`, `KEY_NEW`, …
- Access modifiers (leading) — `KEY_PUBLIC` (was `Export`), `KEY_PRIVATE`, `KEY_PROTECTED`
- Preprocessor — `KEY_DEFINE`, `KEY_UNDEF`, `KEY_IFDEF`, `KEY_IFNDEF`, `KEY_ELSEDEF`, `KEY_ENDIFDEF`, `KEY_REGION`, `KEY_ENDREGION`
- LINQ — `KEY_FROM`, `KEY_WHERE`, `KEY_SELECT`, `KEY_ORDERBY`, `KEY_ASCENDING`, `KEY_DESCENDING`, `KEY_TAKE`, `KEY_SKIP`, `KEY_DISTINCT`, `KEY_JOIN`, `KEY_ON`, `KEY_EQUALS`, `KEY_GROUP`, `KEY_BY`, `KEY_INTO` (`KEY_IN` is reused from `Foreach`)
- Access policy — `KEY_RESTRICT` (`restrict <id> in <src> join … where …` — the row-level-security filter; see `docs/access-policy-rls.md`)

Keywords are English-only — there are no Cyrillic / Russian-language synonyms. Two parallel syntax modes share these keywords and compile to the same bytecode:
- **VES** — Visual-Basic-style: `If c Then … EndIf`, `Foreach x In coll Do … EndDo`, `Procedure F() … EndProcedure`. Keyword-fenced blocks.
- **CES** — C-style: `if (c) { … }`, `Foreach (x In coll) { … }`, `Procedure F() { … }`. Brace-delimited. Default for new configurations since 2026-05-10.

Mode is process-global on `ibCompileCode::SetCodeStyle()`; serialised configurations preserve their stored Syntax flag (wire token still reads `vbs` for back-compat).

---

## Metadata System

### ibValueMetaObject

`ibValueMetaObject` (defined in `src/engine/backend/metaCollection/metaObject.h`) is the abstract base for all configuration objects. It extends `ibValueDynamicMembers` (which extends `ibValue`, so it can be assigned to script variables) and `ibPropertyObjectHelper<ibValueMetaObject>` (so properties appear in the designer's object inspector), plus `ibAccessObject` / `ibInterfaceObject`.

Key attributes:
- `ibMetaID m_metaId` — numeric identifier within the configuration
- `ibGuid m_metaGuid` — globally-unique GUID
- `wxString GetName()` / `GetSynonym()` — developer name and user-visible synonym
- `ibMetaData* m_metaData` — back-pointer to the owning metadata container

### Thirteen Business Object Types

| Type | Class | File |
|---|---|---|
| Catalog | `ibValueMetaObjectCatalog` | `metaCollection/partial/catalog.h` |
| Document | `ibValueMetaObjectDocument` | `metaCollection/partial/document.h` |
| Enumeration | `ibValueMetaObjectEnumeration` | `metaCollection/partial/enumeration.h` |
| Constant | `ibValueMetaObjectConstant` | `metaCollection/partial/constant.h` |
| InformationRegister | `ibValueMetaObjectInformationRegister` | `metaCollection/partial/informationRegister.h` |
| AccumulationRegister | `ibValueMetaObjectAccumulationRegister` | `metaCollection/partial/accumulationRegister.h` |
| DataProcessor | `ibValueMetaObjectDataProcessor` | `metaCollection/partial/dataProcessor.h` |
| Report | `ibValueMetaObjectReport` | `metaCollection/partial/dataReport.h` |
| ChartOfCharacteristicTypes | `ibValueMetaObjectChartOfCharacteristicTypes` | `metaCollection/partial/chartOfCharacteristicTypes.h` |
| ChartOfAccounts | `ibValueMetaObjectChartOfAccounts` | `metaCollection/partial/chartOfAccounts.h` |
| AccountingRegister | `ibValueMetaObjectAccountingRegister` | `metaCollection/partial/accountingRegister.h` |
| ChartOfCalculationTypes | `ibValueMetaObjectChartOfCalculationTypes` | `metaCollection/partial/chartOfCalculationTypes.h` |
| CalculationRegister | `ibValueMetaObjectCalculationRegister` | `metaCollection/partial/calculationRegister.h` |

The last two landed 2026-09-10 (committed 2026-09-11). A **recalculation** is not an object: since
2026-09-14 it is the register's property `UseRecalculation` (the nested class
`ibValueMetaObjectCalculationRegister::ibValueMetaObjectRecalculation` only loads a configuration saved
with the object it used to be, and lets it go). See *Calculation Objects* below.

### Inheritance Chain (simplified)

```
ibValue
  └─ ibValueMetaObject
       ├─ ibValueMetaObjectAttribute         (scalar field descriptor)
       │    └─ ibValueMetaObjectConstant
       ├─ ibValueMetaObjectRecordData        (record-based objects)
       │    ├─ ibValueMetaObjectRecordDataMutableRef
       │    │    └─ ibValueMetaObjectRecordDataRecorderRef   (records movements → sits in time)
       │    │         └─ ibValueMetaObjectDocument
       │    ├─ ibValueMetaObjectRecordDataHierarchyMutableRef
       │    │    ├─ ibValueMetaObjectCatalog
       │    │    ├─ ibValueMetaObjectChartOfCharacteristicTypes
       │    │    ├─ ibValueMetaObjectChartOfAccounts
       │    │    └─ ibValueMetaObjectChartOfCalculationTypes
       │    └─ ibValueMetaObjectRecordDataExt
       │         ├─ ibValueMetaObjectDataProcessor
       │         └─ ibValueMetaObjectReport
       ├─ ibValueMetaObjectRegisterData
       │    ├─ ibValueMetaObjectInformationRegister
       │    ├─ ibValueMetaObjectAccumulationRegister
       │    ├─ ibValueMetaObjectAccountingRegister
       │    └─ ibValueMetaObjectCalculationRegister
       └─ ibValueMetaObjectRecordDataEnumRef
            └─ ibValueMetaObjectEnumeration
```

#### The recorder layer (2026-08-23)

`ibValueMetaObjectRecordDataRecorderRef` sits between the mutable-ref base and the document, beside the
hierarchy layer rather than above it: `MutableRef` splits into **what RECORDS** (this) and **what is
ARRANGED IN A TREE** (`…HierarchyMutableRef`), and a metatype takes the one it is.

It exists because the metaobject side had lost a distinction the VALUE side has had since 2026-05-25
(`ibValueRecordDataObjectRecorderRef` — "ref with movements": posting, the register cascade, un-posting
on a deletion mark). Without its twin, everything a recorder owns had to sit on the shared base — and
from there a **catalogue inherited it too**.

Four things live on it, because they are one fact:

* the **number** and the **date** — what a recorded fact is looked up by and placed in time (both indexed);
* the **record description** — which registers it writes into, plus registering / clearing this metatype's
  reference in each register's `Recorder` attribute (that is what makes `Recorder = <this document>`
  expressible at all);
* the **moment** — the date plus the record standing at it. Not stored: its column is constructed from
  the date and the reference and lives in the recorder's own queryable (`ibRecorderQueryable`, which
  vends it beside the attributes exactly as `ibTabularQueryable` vends a section's `Ref`).

A **document** is therefore a recorder plus one fact of its own: whether it is posted. And "a catalogue
has no point in time" stopped being a rule anybody enforces — a catalogue is simply not a recorder, so
there is nothing to build a moment from and nothing offered in its field tree.

### Open/Close — the runtime image (`ibMetaImage`)

A metadata's **open state is the presence of its image**, not a separate boolean. `ibMetaData` (`src/engine/backend/metaData.h`) holds `std::shared_ptr<ibMetaImage> m_image`: nullptr = closed, live = open. `IsConfigOpen()` returns `m_image != nullptr`.

- **`ibMetaImage`** aggregates everything a run fills and a close discards: the type-ctor factory (`ibCtorRegistry<ibCtorMetaValueType>`), the common-module skeleton (`ibModuleStorage`), and the designer-only compile-value cache (`ibCompileValueCache`). The registry **owns its ctors via `shared_ptr`**, so dropping the image frees them — no manual cleanup. The image is non-copyable / non-movable; lifetime is managed only through the shared_ptr.
- **`LoadGuard`** (nested in `ibMetaData`) is the RAII transaction. Its ctor creates the image (asserting the metadata was closed); the dtor drops it — *unless* `Commit()` ran. A raised `ibBackendException` (or any early return) unwinds through the dtor, the image is dropped, and the state is exactly the closed state it started from ("the load never happened").
- `m_factoryCtorCountChanges` (the monotonic compiler-cache invalidation counter) deliberately lives on `ibMetaData`, *outside* the image, so dropping the image never resets it.
- Each metadata kind builds its own designer infrastructure via `CreateDesignerCache()` (cache + module-manager for designer kinds; `nullptr` for runtime / external DP / report). The compile cache is `nullptr` on runtime configurations — callers gate with `if (auto* cc = metaData->GetCompileCache())`, not `appData->DesignerMode()`.

`RunDatabase()` / `CloseDatabase()` (overridden per subclass) drive the run/close cascade under `LoadGuard`. `LoadDatabase()` / `SaveDatabase()` are the buffer-level entry points on `ibMetaDataConfigurationBase`; they reach `LoadConfigFromBuffer` / `SaveConfigToBuffer`.

### Serialization — `ibDataNode` + format providers

Metadata serialization runs through a uniform, format-agnostic tree (`src/engine/backend/serialize/dataBuilder.h`):

- **`ibDataNode`** — one self-similar node of the structure tree (clsid + metaId + field bag + property bag + child nodes). A metaobject contributes its data into a node; a composite value can itself *be* a child node (`ibDataKind::Child`).
- **`ibFormatProvider`** — abstract `Write(node, writer)` / `Read(reader, node)`. Concrete providers:
  - `ibBinaryProvider` — the internal owned binary format used for DB persistence and form blobs (the `eHeaderBlock` / `eDataBlock` / `eChildBlock` chunk layout). Read + write.
  - `ibJsonProvider` (`serialize/jsonProvider.{h,cpp}`) — JSON export for Git VCS / AI generation / human reading. `Read` is a full parser but is wired to nothing on purpose: the view is lossy by design (Fields + Properties flatten into one key set, Date → ISO string, synthetic `TypeDesc`), so Write→Read is not a round trip — `ibBinaryProvider` is.
- **`ibDataBuilder`** owns the root node and drives `Save(provider, writer)` / `Load(provider, reader)`.

`ibReaderMemory` / `ibWriterMemory` remain the underlying chunked byte streams; `ibBinaryProvider` emits the node-tree layout into them. Each metadata class registers a `ibClassID` CLSID (e.g. `MD_CAT` for Catalog) used as the per-node type discriminator.

File-level export/import goes through `LoadConfigFromFile()` / `SaveConfigToFile()` on `ibMetaDataConfigurationBase`. (The former separate `metadataConfigurationXML.cpp` / `metadataConfigurationJSON.cpp` and the `SaveConfigToXML/JSON` API have been replaced by the provider model; XML support is currently retired.)

### Accounting Objects

Three metadata types support double-entry bookkeeping. Built and applied to a live Firebird base
(2026-08-13) — the write path, the account rules, the five virtual tables and the trigger-maintained
totals bundle; see [accounting-register-arc.md](private/accounting-register-arc.md) for what is still absent.

⚠ The word **Subconto** is gone from the engine (2026-08-12): it was a calque carried over from
older accounting software rather than a name in its own right, and the concept is
an **account dimension** («Аналитика») whose KIND is a characteristic. Only the opaque CLSID body key
`MD_SKTB` kept its spelling, being a key rather than a name.

| Type | CLSID | Purpose |
|---|---|---|
| ChartOfCharacteristicTypes | `MD_CHRC` | The account dimensions' KINDS — each element stores an `ibTypeDescription` narrowing what values that kind admits |
| ChartOfAccounts | `MD_CHOA` | Chart of accounts with AccountType (Active/Passive/AP), OffBalance, the accounting kinds it declares (two branches of its own: `MD_ACKD` — a boolean field of every account; `MD_ADKD` — a tick-box column of the account's kinds table), the maximum dimension count, and the predefined `AccountDimensionKinds` tabular section |
| AccountingRegister | `MD_AREG` | Double-entry register. One-sided: RecordType (Debit/Credit) + Account + N *(kind, value)* AccountDimension slots; correspondence: AccountDr / AccountCr + N slots per side, and a dimension or resource with `Balance` cleared kept per side as hidden `<Field>Dr` / `<Field>Cr` attributes. Totals per side at two grains (per account and breakdown, per account). Balance / Turnovers / DrCrTurnovers / BalanceAndTurnovers / RecordsWithAccountDimensions |

Bindings: AccountingRegister → ChartOfAccounts (via `ibPropertyChartOfAccounts`), ChartOfAccounts → ChartOfCharacteristicTypes (via `ibPropertyChartOfCharacteristicTypes`). Each binding has its own property class. Since 2026-09-10 (committed 2026-09-11) every chart binding is edited by ONE designer editor, `ibPGChartBindingProperty` (`frontend/propertyManager/property/advprop/advpropChartBinding.cpp`), which replaced a copy per chart: the candidates and whether one chart or a set may be ticked are the property's own answer (`GetValueList` → `ibPropertyChoiceMode`), and only an accepted dialog changes the value.

### Calculation Objects

Two metadata types and one subordinate support calculation — payroll accruals and deductions whose
types displace one another over days, take a base from other types' results, and go stale when those
change. Built 2026-09-10, committed 2026-09-11; verified by running a payroll configuration on a live
Firebird base. See [payroll-arc.md § 11](private/payroll-arc.md) for the design as built.

| Type | CLSID | Purpose |
|---|---|---|
| ChartOfCalculationTypes | `MD_CHCL` | The calculation TYPES — a hierarchical chart with `UseActionPeriod`, `BaseDependence` (`None` / `ByActionPeriod` / `ByRegistrationPeriod`), `BaseCharts`, and three predefined relation sections of one class, `ibValueMetaObjectCalculationTypeRelationTable` (`MD_DSTB`): `Displacing`, `Base`, `Leading`. A row is an edge from the type that owns it to the type it names |
| CalculationRegister | `MD_CREG` | Movements subordinate to a recorder, keyed by (recorder, line), no record manager. Standard attributes `CalculationType`, `RegistrationPeriod` (its one period — no `Period` column), `Storno`; `ActionPeriod` / `ActionPeriodStart` / `ActionPeriodEnd` with `UseActionPeriod`; `BasePeriodStart` / `BasePeriodEnd` with `UseBasePeriod`. The actual action periods are a reading of the records (source `CalculationRegister.<Register>.ActualActionPeriod`); with a `Schedule` bound (an information register, its value resource, its date dimension, a dimension link per other dimension), `CalculationRegister.<Register>.ScheduleData` reads the schedule summed over each record's periods; with `UseRecalculation` the record-set write keeps the recalculation marks — one table of the register's own columns, read as `CalculationRegister.<Register>.Recalculation`; `GetBase` is a function of the records (a record's line and the manager) |
| Recalculation | `MD_RCLC` | Retired 2026-09-14 — a calculation register keeps its marks itself (`UseRecalculation`). The class stays registered so a configuration saved with the object still loads; the register lets the object go on load, with a line in the journal, and the table it had is declared by nobody |

The calculation logic that is not a metaobject — days, intervals, the relations, displacement, what
a change leads — lives in `backend/calculation/calculation.{h,cpp}`; the metaobjects read and write,
that file decides.

Bindings: CalculationRegister → ChartOfCalculationTypes (via `ibPropertyChartOfCalculationTypes`, exactly one — the save refuses an empty binding), ChartOfCalculationTypes → the other charts its `Base` and `Leading` sections may name (`BaseCharts`, the same property class in set mode, `ibPropertyChoiceMode::Mult`). Both are edited by the shared chart-binding editor above.

---

## Sessions and Runtime Ownership

OES distinguishes between **metadata** (compile-time, process-wide, shared) and **runtime state** (per-session, bound to one user context).

### ibSession

`ibSession` (`src/engine/backend/session/session.h`) is the unit of runtime state:

- **Identity** (`ibSessionIdentity`) — guid, userName, userGuid, computer, address (host:port for web), appMode, started timestamp, pid
- **Kind** (`ibSessionKind`) — Launcher / Designer / Enterprise / Service / WebServer (wes process technical row) / WebClient (per-tab). 1:1 with `ibRunMode` for the unambiguous cases; the web run mode splits into the two distinct session kinds inside one process.
- **State machine** — `ibSessionState` (Created / Added / Rejected / Stopping / Gone), `ibAuthState` (Anonymous / Authenticated / AuthFailed)
- **User info** — `ibUserInfo` (formerly `ibApplicationDataUserInfo`) — OES-user (from `sys_user` table), distinct from the DB-level admin user used to open the database connection. Plus `m_sessionRawPassword` — plain-text cached only for Designer "Start debugging" so spawned children can re-authenticate without prompting. `ibUserInfo` itself owns sys_user CRUD as static factories — `appData` no longer mediates.
- **Working date** — `m_workDate` per-session (replaces the legacy static `ibValueSystemFunction::ms_workDate` so two web sessions don't step on each other).
- **Translate state** — `ibTranslateState`: explicit override, the user's language, the configuration's own, and the resolved code (`override || user || configuration`). Selects which metadata synonym / form-label translation is shown. Per-session, so concurrent web tabs each render their own user's language and two bases in one process each keep their own. Distinct from the platform's wxLocale (UI gettext, process-wide). Read through `ibBackendLocalization::GetUserLanguage()`.
- **Compile state** — `ibCompileState`: the code style (CES / VES) the session's modules compile in, taken from its base's configuration. Like the interpreter's `ibProcUnitState`, a thread with no session (codeRunner, tests) has its own `thread_local` one.
- **Root module manager** — `m_root : ibValuePtr<ibValueModuleManagerRuntimeConfiguration>`. Created via `EnsureRoot()` in `ibSessionRegistry::NotifyAuthenticated`'s middle phase (between `OnFirstConnect` and `OnAuthenticated` listener phases). Stays nullptr for sessions that never run scripts — **the Designer never creates a root** (`EnsureRoot` is gated on `DesignerMode()`; it uses the lightweight `ibValueModuleManagerDesigner` in the compile cache instead), and likewise WebServer technical / Launcher. Objects/records/modules reach the right manager through the `ibSession::GetEditModuleManager(metaData)` seam (Designer → compile-cache designer manager; runtime → `m_root`). See `module-manager-split.md`.
- **Frame** — `virtual ibBackendDocFrame* GetFrame() const { return nullptr; }` on base `ibSession`. Frame storage lives on derived sessions that have a GUI surface (e.g. `ibWebClientSession::SetFrame(ibWebFrame*)`; `ibGUISession` desktop variants). Base has no `m_frame` field — null means "no frame on this session" (codeRunner / wenterprise-server technical session). The frame belongs to the session that created it, not to a process-wide singleton.
- **Per-session debug** — optional `ibDebugSession` (CV/mutex + per-session watch expressions + run context) so concurrent web sessions can each enter their own debug loop without blocking.
- **Exclusive (monopoly) mode** — `m_exclusive`. At most one session in the registry holds it; while held, every other Connect parks until release.
- **Server back-link** — `m_server : weak_ptr<ibSession>` from a server-spawned client to the session that hosts it (e.g., wes's WebClient → wes's WebServer). Used by shutdown logic, cluster topology, and admin UI.

`ibSession::Current()` is the canonical "session this code is currently working on". One rule in every host:

- **The thread's binding** — a thread that bound a session (`ibSessionScope`, `ibSessionThreadBinding`, a job's run) works for it.
- **Else the base's fallback** — the process's own session there (`IsProcessSessionKind`: the designer's or the client's window, the application server's login, wenterprise-server's `WebServer` row). A job or a web tab never becomes it.

`ibSessionScope` (legacy) and `ibSessionThreadBinding` (preferred for app entry points) are the RAII helpers that bind a session to the calling thread. The interpreter no longer reads global `thread_local` state directly: `ibProcUnitState` lives under `ibSession` (`session.h`), and the only `thread_local` slot in `session.cpp` is a fallback for sessionless callers (codeRunner sandbox / system bootstrap). The worker pool (`workerPool.h` + `workerPoolHeadless.cpp`) leases a session into a thread via `tl_currentLease` and runs the request on it.

Runtime ProcUnits live on per-session descriptors. The session's root `ibValueModuleManagerRuntimeConfiguration` (`ibSession::m_root`) owns common modules, forms, and per-instance object runtimes; each child is its own descriptor (`ibRuntimeModuleDataObject`) carrying its own `shared_ptr<ibProcUnit>`. Concurrent web sessions therefore each work on their own descriptor instances — no shared ProcUnit, no cross-session execution mutex. See `runtime-facade.md` for the descriptor composition / parent chain details.

### ibSessionRegistry

`ibSessionRegistry` (`src/engine/backend/session/sessionRegistry.h`) is the process-wide session manager:

- **Single-consumer queue + priority** — one registry thread processes `Add / Attach / Detach / Remove / SetActivity` requests (Urgent → Normal → Low → Background). All DB mutation for `sys_session` happens on this thread.
- **Liveness via heartbeat on `lastActive`** — each process's `JobHeartbeatOwn` UPDATEs `lastActive` on its own `sys_session` rows every ~1s. Any row trailing `now` by more than `kStaleCutoffSec` (10s, in `sessionRegistry.cpp`) is treated as a zombie and DELETEd by another process's sweep. The earlier row-lock-as-source-of-truth design (a long-running `SELECT ... WITH LOCK` over own-session rows, probed via `TryProbeRowLock`) was **rolled back** — it self-deadlocked (see `session-registry.md §4`); only retirement comments remain in code. Record write protection is a separate concern — optimistic `DataVersion` check + the `sys_lock` table, see [record-locks.md](private/record-locks.md).
- **Connect/Disconnect API** — desktop `ibApplicationInstance::CreateSession` and web `ibWebSession::Login` both call `registry.Connect(req)` which returns an `ibSessionTicket` (RAII, dtor submits Remove@Urgent).
- **Single mutator of session state.** `ibSession`'s state-machine mutators (`Transition`, `TransitionAuth`, `SetIdentity`, `SetInserted`, `WaitForState`, `WaitForAuth`) and auth-flow setters (`SetUserInfo`, `EnableDebug`, `SetSessionRawPassword`) are private under `friend ibSessionRegistry`. Public façades `ibSessionRegistry::InstallUser(s, info, pwd)` and `EnableDebugForSession(s)` are the only entry points for auth bring-up — `appData` and login dialogs route through them, never poke session internals directly.
- **Cluster snapshot** — `ibSessionRegistry::GetClusterSnapshot()` returns `ibSessionSnapshot` (formerly `ibApplicationDataSessionArray` on `appData`), refreshed every ~3s by `JobRefreshSnapshot`. Snapshot now lives in `backend/session/sessionSnapshot.{h,cpp}`.

The registry supports multiple concurrent sessions (N on web, 1 on desktop) through the same mechanism. See `project_session_registry_refactor` memory entry for the current implementation status.

### Runtime ownership

**Compile state — shared, immutable.** `ibCompileCode` produces an `ibByteCode` that lives on the configuration's compile descriptor (`ibCompileModule` on `ibValueMetaObjectModuleBase`). One bytecode per module is shared across all sessions; rebuilt only on Designer edit or metadata reload. AOT cache (`sys_bytecode_cache` via `ibByteCodeCache`) lets `Compile()` skip the parse+emit on cache hits — the cached blob is `ibByteCode::SerializeAOT/DeserializeAOT` (magic `'PBC1'`; see the [compiler module table](#srcenginebackendcompiler)). A hit skips the parser, **not** the preparation: the descriptor still runs `PrepareModuleData()` to build the module's name surface, because a name's address is the number of rungs the resolver walked and a parent with no live compile context is counted differently; the surface it builds is then verified name-for-name against the loaded `m_listVar`, and a row that disagrees is invalidated and recompiled from source. Bytecode compiled under an eval (a watch or the debugger's sandbox) is never written to the cache — its host frame is an extra rung. See [compiler-pipeline.md § 4a](private/compiler-pipeline.md).

**Runtime state — per-session, owned via descriptors.** Each session owns its own runtime tree:

```
ibSession::m_root  →  ibValueModuleManagerRuntimeConfiguration  (per-session root)
                       │
                       ├── ibValueModuleUnit             (per common module)
                       │     └── m_procUnit : shared_ptr<ibProcUnit>
                       ├── ibValueModuleUnit             (per common module)
                       │     └── m_procUnit : shared_ptr<ibProcUnit>
                       └── ...
```

- The root manager `m_compileModule` references the shared compile state; its `m_procUnit` is the session's main module ProcUnit.
- Each common module wraps a child `ibRuntimeModuleDataObject` (`backend/moduleInfo.h`) carrying its own `shared_ptr<ibProcUnit>` and `m_binder` (per-execute context-var binder produced by `bc.CreateBinder()`). Context handles (`ThisObject`/`ThisForm`), scope containers, export handles (`Controls`/`DataSource`/…) and a module's own injected locals (a constant's `Value`) are registered once via `Bind{Context,Scope,Export,Local}Variable` and seed the binder — they replaced the hand-rolled `PrepareNames`/`AppendProp` name surface. See [Name binding](private/name-binding.md).
- Forms, per-instance catalog/document runtimes, external data processors / reports hang off as children of the root via the same descriptor mixin (`m_parent` raw-pointer chain; container enforces parent-outlives-child).
- Concurrent sessions therefore run on **physically separate** ProcUnit instances. The shared resource is the immutable `ibByteCode` (read-only); per-session frame stacks, locals, and binders are isolated.

**`m_runtimeMutex` guards bring-up vs teardown, not execution.** `ibValueModuleManager::AttachRuntime(session)` (called from `ibSession::CompileRoot` at login, on every run mode; a configuration that does not start throws there and the login is refused) builds the runtime tree under the lock. `DetachRuntime(session)` drops it under the same lock. Per-session script execution does NOT take this lock — different sessions execute in parallel on their own descriptors.

**Worker pool dispatch.** Script execution runs on a worker thread leased via `ibWorkerPool` (`backend/session/workerPool.h` + headless impl). Each request leases a session into `tl_currentLease` for the call's duration; `ibSession::Current()` resolves through this slot. Desktop has N=1 session on the wx main thread; web has N per-cookie sessions, each pinned to its own worker. The script interpreter never touches global `thread_local` state directly — `ibProcUnitState` lives under `ibSession`, the one `thread_local` fallback in `session.cpp` exists only for sessionless callers (codeRunner sandbox / system bootstrap).

**Same model for desktop and web** — the only difference is session count and entry threading. The `ibSessionRegistry + ibSession + ibSessionScope + per-session runtime tree + worker pool` stack is identical across all run modes.

**Bytecode self-contained.** `ibByteCode` holds its own moduleName, rootContext, parent-bytecode ref, and dependency manifest. There is no `byteCode->m_compileModule` back-pointer; runtime lifetime is decoupled from `ibCompileCode` lifetime, so metadata reload can drop compile state while running sessions hold their bytecode through their shared_ptr.

### Designer — compile only

A designer session (`Designer`, `ThinDesigner`) has no runtime — `AttachRuntime` returns early for a designer kind. Designer reads `ibCompileCode` for autocomplete, function search, jump-to-definition, and cascading recompile. Scripts are not executed. Autocomplete surfaces bound names by reading the compile module's bind tables and walking the **compile-module** parent chain — from the backend since 2026-09-08 (`ibValueAtCaret` / `ibNamesAtCaret`, `backend/compiler/scriptComplete.h`); the editor's own precompiler is deleted and it keeps only its lexer. See [Name binding § Designer](private/name-binding.md#designer--surfacing-the-same-binds). Debug sessions attach to a separate runtime process (enterprise.exe / wenterprise-server.exe) via the TCP debug protocol.

---

## Database Abstraction

### Class Hierarchy

```
ibDatabaseLayer  (abstract — databaseLayer.h)
  ├─ ibDatabaseLayerFirebird   (databaseLayer/firebird/)
  ├─ ibDatabaseLayerPostgres   (databaseLayer/postgres/)
  ├─ ibDatabaseLayerSQLite     (databaseLayer/sqllite/)
  └─ ibDatabaseLayerODBC       (databaseLayer/odbc/)

ibDatabaseResultSet  (abstract)
  ├─ ibDatabaseResultSetFirebird
  ├─ ibDatabaseResultSetPostgres
  ├─ ibDatabaseResultSetSQLite
  └─ ibDatabaseResultSetODBC

ibPreparedStatement  (abstract)
  ├─ ibPreparedStatementFirebird
  ├─ ibPreparedStatementPostgres
  ├─ ibPreparedStatementSQLite
  └─ ibPreparedStatementODBC
```

### Access Pattern

All database access goes through the `db_query` macro, which is `ibApplicationInstance::GetDatabaseLayer()` — it resolves through the connection pool of the thread's base and returns a `std::shared_ptr<ibDatabaseLayer>` (the active checked-out layer). Example:

```cpp
ibDatabaseResultSet* rs = db_query->RunQueryWithResults(
    wxT("SELECT * FROM %s WHERE guid = ?"), table_name);
```

Every driver folder except `odbc/` contains an `engine/` subdirectory with the vendored native client library; ODBC links the system driver manager (`odbc32` / `odbccp32`).

### Where the physical schema comes from

> ⭐⭐ **THE DIFF BETWEEN TWO CONFIGURATIONS CARRIES ALL THE INFORMATION.** The apply is a function of
> exactly two arguments — the baseline (the active configuration) and the target (the edited one) —
> and it brings the database to a schema matching the target exactly. The live catalogue is never
> consulted, and **physical introspection is banned**: insuring against the physics turns a drift
> from a defect into a normal state and deletes the only signal that the mechanism is broken. The
> guarantee covers a base *we* created and have maintained through restructurings; a base changed
> from outside is not a warranty case.

Stated as policy 2026-08-14. The rule, its scope, the three physical reads that remain legitimate,
and the four things it already forces (rollback-able apply, publication last, compensated second
phase, schema settings must serialise) are in
**[schema-authority.md](private/schema-authority.md)**. The differ itself is
`query/schemaSnapshot.{h,cpp}`.

---

## Form System

### ibValueForm Hierarchy

`backend_form.h` declares two sibling backend interfaces, both rooted under `ibBackendValue`:

- `ibBackendControlFrame` — the abstract control interface (any control, including a form root, presents this).
- `ibBackendValueForm` — the abstract form interface.

The concrete `ibValueForm` (`frontend/visualView/ctrl/form.h`) multiply-inherits both (plus `ibRuntimeModuleDataObject` for its per-instance ProcUnit):

```
ibValueForm  (concrete — frontend/visualView/ctrl/form.h)
  : public ibValueFrame
  , public ibBackendValueForm        (abstract — backend_form.h)
  , public ibRuntimeModuleDataObject (per-instance compile module + ProcUnit)
```

`ibValueForm` owns the complete control tree for one open form. The static entry point `ibBackendValueForm::CreateNewForm()` instantiates the form; the higher-level orchestrator is `ibValueMetaObjectFormBase::CreateAndBuildForm()` (`metaCollection/metaFormObject.h`), which resolves the form descriptor from metadata and builds the control tree (see [Form Open](#form-open)). The verb a caller actually uses is `ibValueMetaObjectFormBase::GetObjectForm()` — virtual, answered by the form's own kind (a common form stands alone, an object form asks the object that owns it). Full map: [form-engine.md](private/form-engine.md).

### Attribute binding

A form no longer holds a single hard-wired data object. It owns a registry of typed
**attributes** (`ibFormAttributeValue` wrapping `ibValueFormAttribute`); controls bind by a
metaId **PATH** whose head selects an attribute (the gate), resolved through one
`Get/SetValueByAttributePath` pair. The source object passed on open lands in the MAIN
attribute. See `docs/form-attribute-binding.md` (Blocker A of the ERP roadmap).

### Visual controls — 19 registered + 7 system-kind children

Implemented in `src/engine/frontend/visualView/ctrl/`. Two corrections to the table below,
verified 2026-07-29 by grepping `CONTROL_TYPE_REGISTER`:

- **Choice, ComboBox and ListBox carry NO registration.** The classes exist (~55 lines each,
  `Create()` only) but no CLSID is registered, so they cannot be placed from the palette or
  instantiated from metadata — on either desktop or web. Treat the rows below as "class exists,
  control does not".
- **StaticBoxSizer and WrapSizer are missing from the table** and *are* registered
  (`staticboxsizer.cpp`, `CT_SSZER`; `wrapsizer.cpp`), as are the seven system-kind children
  registered via `S_CONTROL_TYPE_REGISTER`: SizerItem, NotebookPage, TableboxColumn,
  TableboxColumnGroup (added with column groups, 2026-08-17), Tool, ToolSeparator, ClientForm.

| Control | File |
|---|---|
| Button | `button.cpp` |
| CheckBox | `checkbox.cpp` |
| Choice | `choice.cpp` |
| ComboBox | `combobox.cpp` |
| Form (root) | `form.cpp` |
| Frame | `frame.cpp` |
| Gauge | `gauge.cpp` |
| GridBox | `gridBox.cpp/.h` |
| HtmlBox | `htmlBox.cpp/.h` |
| ListBox | `listbox.cpp` |
| Notebook | `notebook.cpp/.h` |
| RadioButton | `radiobutton.cpp` |
| Slider | `slider.cpp` |
| StaticText | `statictext.cpp` |
| StaticLine | `staticline.cpp` |
| TableBox | `tableBox.cpp/.h` |
| TextBox | `textBox.cpp/.h` |
| TextCtrl | `textctrl.cpp` |
| ToolBar | `toolBar.cpp/.h` |
| BoxSizer | `boxsizer.cpp` |
| GridSizer | `gridsizer.cpp` |
| ChartBox | `chartBox.cpp/.h` |

### Form Rendering

`ibVisualHost` is the wxWindow that owns the visual surface. It receives layout instructions from `ibValueForm` and positions wxWidgets controls. `ibVisualHostClient` handles the reverse channel: user input events travel from wxWidgets up through the host into the form's script event handlers.

---

## Debugger Architecture

### Client-Server Model

```
enterprise.exe                     designer.exe
      │                                  │
ibDebuggerServer ◄──── TCP ──────► ibDebuggerClient
(src/engine/backend/debugger/)     (src/engine/frontend/ or designer/)

Default port: 1650  (defined as defaultDebuggerPort in debugDefs.h)
```

### Connection Types (`ConnectionType` enum)

| Value | Purpose |
|---|---|
| `ConnectionType_Scanner` | Designer scanning for debuggable processes |
| `ConnectionType_Waiter` | Enterprise waiting for a connection |
| `ConnectionType_Debugger` | Active debug session |

### Command Flow

The designer sends `CommandId` packets; enterprise responds with `EventId` packets:

```
Designer                           Enterprise
   │  CommandId_VerifyConnection      │
   │ ────────────────────────────────►│
   │  CommandId_SetConnectionType     │
   │ ◄────────────────────────────────│
   │  CommandId_StartSession          │
   │ ────────────────────────────────►│
   │  EventId_SessionStart            │
   │ ◄────────────────────────────────│
   │  CommandId_ToggleBreakpoint      │
   │ ────────────────────────────────►│
   │  CommandId_Continue              │
   │ ────────────────────────────────►│
   │  EventId_EnterLoop               │
   │ ◄────────────────────────────────│
   │  CommandId_GetArrayBreakpoint    │
   │  CommandId_SetLocalVariables     │
   │  CommandId_SetStack              │
   │ ◄────────────────────────────────│
```

The server runs each connection as a `wxThread` (`ibDebuggerServer::ibDebuggerServerConnection`). Raw binary packets are sent via `SendCommand` / `RecvCommand`.

---

## Key Data Flows

### User Login (desktop)

```
launcher.exe (or direct enterprise.exe with CLI creds)
  └─ ibApplicationInstance::CreateAppDataEnv(ibFileInstanceRequest{ server, port, user, pwd, db, locale })
       └─ ibDatabaseLayer::Open(server, port, db, ibUser, ibPwd)   # DB-level admin connection
            └─ appData->CreateSession<ibEnterpriseSession>()        # phased session lifecycle
                 # registry runs Connect(req) under the session factory:
                 #   - Submit(Add, Normal), waits state Added
                 #   - Add handler INSERTs sys_session row under row-lock
                 #   - OnCreateSession() fires on the main thread
                 #     (ibGUISession overrides — builds the wx frame here)
                 └─ session->Open(user, password)                   # auth orchestration
                      └─ ticket.Attach(user, pwd) — Submit(Attach, Normal)
                           └─ ibApplicationInstance::AuthenticateUser
                                  (PBKDF2 preferred, MD5 silent-upgrade path)
                                └─ InstallUser writes session->m_userInfo
                                     └─ NotifyAuthenticated phases (registry-driven):
                                          1. OnFirstConnect — metadataCreate (one-shot)
                                          2. session->AcquireMetaData() — its own reference
                                             session->EnsureRoot() — CreateRoot(GetMetaData())
                                          3. OnAuthenticated — RunDatabase (one-shot)
                                                             + session->CompileRoot()
                                                             + mm->AttachRuntime(s)
                                                                — main ProcUnit + Execute top-level
                                                                — StartMainModule: BeforeStart / OnStart
                                                                — wx main loop handles UI
```

### User Login (web — wenterprise-server)

```
HTTP: POST /w/<dbalias>/login  (body: user+pwd, cookie: tabSid UUID)
  └─ wfrontendCreateSessionWithId(tabSid)      # if unknown cookie, create new session
       └─ Sessions().Login(tabSid, user, pwd)
            └─ registry.Connect(req)           # same path as desktop
                 └─ ticket.Attach(user, pwd)   # AuthenticateUser on worker-side
                      └─ NotifyAuthenticated phases (OnFirstConnect / EnsureRoot / OnAuthenticated)
                           └─ session->EnsureRoot + CompileRoot + mm->AttachRuntime(s)
                                └─ ibSessionScope(session) on HTTP handler thread
                                     └─ ibWebApplication::OnInit
                                          └─ StartMainModule (BeforeStart / OnStart, under m_runtimeMutex)
                                               └─ StartWorker — per-session worker thread
                                                    └─ future HTTP calls POST to worker via RunOnWorker
```

### Form Open

> The full map of this layer — who builds a form, what its identity is, how it reaches a
> window and how it dies — is [form-engine.md](private/form-engine.md). The chain below is the
> one-screen version.

```
Script (desktop) / HTTP POST /open?metaID=N (web):
OpenForm("Catalog.Products.ListForm")
  └─ ibValueMetaObjectFormBase::CreateAndBuildForm(request, metaForm, owner, srcObj)
       └─ ibSession::CurrentFrame()->CreateNewForm(request, metaFormObject, ownerControl, srcObject)
              # session-owned frame; desktop = ibFrontendMainFrame, web = ibWebFrame
            └─ ibValueForm constructed (per-instance compileModule + ProcUnit)
                 └─ LoadFormData / BuildForm — control tree built from metadata
                      └─ frame creates the doc-view child (ibAuiDocChildFrame / ibWebDocChildFrame)
                           └─ ibVisualHost created (wxWindow on desktop, ibWebWindow on web)
                                └─ controls instantiated and laid out
                                     └─ OnOpen() script handler via ibProcUnit::CallAsProc()
```

### Document Save

```
Script: Write()   (or user presses Save)
  └─ ibValueRecordDataObject::Write()
       └─ ibDatabaseLayer::BeginTransaction()
            └─ generate SQL INSERT/UPDATE for main table
                 └─ iterate tabular sections → INSERT/UPDATE child rows
                      └─ post RegisterRecords() for registers if Document
                           └─ ibDatabaseLayer::Commit()
                                └─ OnWrite() script handler called
```

### Session Teardown (web — /logout or pagehide beacon)

```
HTTP: POST /w/<dbalias>/logout?sid=<tabSid>  (sendBeacon from browser pagehide)
  └─ Sessions().Destroy(sid)
       └─ shared_ptr<ibWebSession> dropped from sessions map
            └─ ~ibWebSession → OnExit
                 └─ RunOnWorker DeleteAllViews of open tabs (form dtors)
                      └─ StopWorker (joins per-session worker thread)
                           └─ ExitMainModule (BeforeExit / OnExit events, under m_runtimeMutex)
                                └─ mm->DetachRuntime(s) — release descriptor ProcUnits
                                                                  bound to this session
                                     └─ delete frame
                                          └─ ticket.reset → Submit(Remove, Urgent)
                                               └─ registry DELETE sys_session row, release row-lock
```

---

## Localization (UI gettext)

Two parallel translation surfaces exist; do not confuse them:

- **Configuration language** — the session's translate state (`ibTranslateState::m_resolvedLanguageCode`), selects metadata synonyms / form-label translations stored *inside* the configuration. See `ibBackendLocalization::GetUserLanguage` / `SetUserLanguage`.
- **Process UI language** — gettext catalogs under `locale/` (`ru.po` / `uk.po` + compiled `*.mo`). Strings wrapped in `_("...")` macros across `src/engine/**` end up in the `.mo` and are looked up by wxLocale at runtime.

**Workflow** (when adding new `_()` strings):

```
# Regenerate the template from current sources (uses Poedit's gettext tools).
xgettext --from-code=UTF-8 --keyword=_ --keyword=wxTRANSLATE --keyword=RuntimeError \
         --keyword=wxPLURAL:1,2 --keyword=wxGETTEXT_IN_CONTEXT:1c,2 --language=C++ --no-wrap \
         --output=locale/open_es.pot --files-from=<list-of-cpp-files>

# Merge new entries into each language file (preserves existing translations).
msgmerge --no-wrap --update --backup=none locale/ru.po locale/open_es.pot
msgmerge --no-wrap --update --backup=none locale/uk.po locale/open_es.pot

# Translate empty msgstr entries (manually or in Poedit).

# Compile to .mo for the runtime.
msgfmt --check-format --output-file=locale/ru.mo locale/ru.po
msgfmt --check-format --output-file=locale/uk.mo locale/uk.po
```

The Poedit GUI (`File → Open` on the .po) does Step 1 + the editing UI in one shot. CLI route is faster for batch updates from CI.

One English word with two meanings takes a context for the special one: `wxGETTEXT_IN_CONTEXT("document attribute", "Number")` is translated apart from `_("Number")`.
