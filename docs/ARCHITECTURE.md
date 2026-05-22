# OES Architecture

## Table of Contents

1. [System Overview](#system-overview)
2. [Layer Descriptions](#layer-descriptions)
3. [Module Descriptions](#module-descriptions)
4. [Bytecode Engine](#bytecode-engine)
5. [Metadata System](#metadata-system)
6. [Sessions and Runtime Ownership](#sessions-and-runtime-ownership)
7. [Database Abstraction](#database-abstraction)
8. [Form System](#form-system)
9. [Debugger Architecture](#debugger-architecture)
10. [Key Data Flows](#key-data-flows)

---

## System Overview

```
┌──────────────────────────────────────────────────────────────┐
│                      Executables                             │
│  designer.exe   enterprise.exe   launcher.exe   daemon.exe   │
│  codeRunner.exe   classChecker.exe                           │
└────────────┬─────────────────┬────────────────┬─────────────┘
             │                 │                │
             ▼                 ▼                ▼
┌────────────────────┐  ┌────────────────────────────────────┐
│   frontend.dll     │  │           backend.dll              │
│                    │  │                                    │
│  ibValueForm       │◄─►  ibApplicationData (singleton)     │
│  ibVisualHost      │  │  ibMetaDataConfiguration           │
│  22 Controls       │  │  ibCompileCode / ibProcUnit        │
│  ibMainFrame       │  │  ibDatabaseLayer (+ 5 drivers)     │
│  Property editor   │  │  ibDebuggerServer                  │
└────────────────────┘  └────────────────┬───────────────────┘
                                         │
                        ┌────────────────▼────────────────────┐
                        │         Database tier                │
                        │  Firebird  PostgreSQL  SQLite        │
                        │  MySQL     ODBC                      │
                        └─────────────────────────────────────┘
```

Communication between `frontend.dll` and `backend.dll` goes through abstract C++ interfaces exported from `backend.dll`. The frontend never accesses database drivers or the compiler directly.

---

## Layer Descriptions

### Application Layer (executables)

Each executable links against both DLLs and provides a `wxApp` subclass that selects the run mode:

| Executable | Run Mode (`ibRunMode`) | Purpose |
|---|---|---|
| `launcher.exe` | `eLAUNCHER_MODE` | Connection chooser; creates/selects database |
| `designer.exe` | `eDESIGNER_MODE` | Full IDE — metadata editor, form designer, debugger client |
| `enterprise.exe` | `eENTERPRISE_MODE` | Desktop thick-client runtime (GUI, single user session per process) |
| `wenterprise-server.exe` | `eWEB_ENTERPRISE_MODE` | Web runtime host — HTTP server, N per-cookie user sessions, browser client |
| `daemon.exe` | `eSERVICE_MODE` | Headless background service |
| `codeRunner.exe` | `eSERVICE_MODE` | Executes a single script module |
| `classChecker.exe` | — | Validates metadata consistency |

> A rename `eENTERPRISE_MODE → eRUNTIME_MODE` is planned — the current name misleads, both thick-client and web hosts are "runtime", just with different UI transports. The constants stay as-is until the rename lands.

### Backend Layer (`backend.dll`)

The backend is the core engine. It is self-contained — no GUI dependencies. Key objects:

- **`ibApplicationData`** (`src/engine/backend/appData.h`) — master runtime object; holds the database connection pool, run mode, and application metadata. Accessed via the `appData` macro. No longer holds per-session state (user info, ProcUnits, frame moved to `ibSession`) and no longer mediates table-level queries — sys_user lives on `ibUserInfo` (`Read` / `Save` / `HasAny` / `ListAll` / `Serialize` / `Deserialize`), sys_session snapshot lives on `ibSessionSnapshot` produced by `ibSessionRegistry`. `appData` is now the *process*-level coordinator (run mode, DB pool, metadata factory), not a generic gateway.
- **`ibMetaDataConfiguration`** (`src/engine/backend/metadataConfiguration.h`) — loads, saves, and manages the metadata tree (all business objects). Accessed via `activeMetaData`. Stores compile cache (compiled bytecode) per module descriptor; runtime instances live in sessions.
- **`ibSession` / `ibSessionRegistry`** (`src/engine/backend/session/`) — per-session state and process-wide session manager. The session is reachable via `ibSession::Current()`, which dispatches by `AccessMode` — `Single` (one session per process: desktop, daemon, codeRunner) returns the lone session regardless of thread; `Shared` (wenterprise-server) does per-thread lookup with a process-wide fallback. `ibSessionScope` and `ibSessionThreadBinding` are the RAII helpers that bind a session to the calling thread. See [Sessions and Runtime Ownership](#sessions-and-runtime-ownership).
- **`ibDebuggerServer`** (`src/engine/backend/debugger/debugServer.h`) — TCP server that accepts designer connections and relays debugger events.

### Frontend Layer (`frontend.dll`, `wfrontend.dll`)

Two sibling DLLs share the same form/view/control code paths through `OES_USE_WEB` ifdefs and `ibFrontendWindow` typedef (`wxWindow` for desktop, `ibWebWindow` for web):

- **`frontend.dll`** — wxWidgets GUI. Used by `enterprise.exe`, `designer.exe`, `launcher.exe`, `daemon.exe`, `codeRunner.exe`.
- **`wfrontend.dll`** — web UI (HTML serialisation of form control trees via `ToJSON()`, cpp-httplib transport). Used by `wenterprise-server.exe`.

Shared frontend objects:
- **`ibValueForm`** — the runtime representation of an open form; holds the control tree and responds to user events. Same class on both builds.
- **`ibVisualHost` / `ibVisualHostClient`** — render and input routing surface. Desktop = wxWindow; web = `ibWebWindow` tree serialised to JSON.
- **Doc-view frames** — backend-facing interface `ibBackendDocFrame` (renamed from the historical `ibBackendDocMDIFrame`); concrete implementations are `ibFrontendDocMDIFrame` (desktop, wraps `wxAuiMDIParentFrame` + `wxDocParentFrameAnyBase`) and `ibWebFrame` (web). Children are `CAuiDocChildFrame` / `ibDialogDocChildFrame` on desktop and `ibWebDocChildFrame` on web. The frame is owned by the `ibSession` that created it (no process-level singleton on the backend side); legacy `mainFrame` macro still exists in `frontend/mainFrame/mainFrame.h` as a frontend-local accessor for the GUI singleton, but new backend code reaches the frame through `ibSession::Current()->GetFrame()` or `ibSession::CurrentFrame()`. Template-mixin rewrite to follow `wxDocParentFrameAny` is partial — `ibFrontendDocMDIFrame` keeps the "MDI" suffix until the frontend-side rename lands.
- **`ibCodeEditor`** (`frontend/win/editor/codeEditor/`) — the Scintilla-based script editor. Lives in `frontend.dll` so any GUI host can use it: `designer.exe` (its module/form editors), `codeRunner.exe` (sessionless scratch runner). Highlighter, fold parser, auto-indent on Enter, format / increase / decrease indent, comment add/remove, Ctrl-Space autocomplete, GotoLine + ProceduresAndFunctions dialogs all live here. Debugger integration is a designer-only concern, kept out of the base via six virtual hooks (`IsDebuggerEnterLoop`, `OnEditDebugPoint`, `OnPatchModule`, `OnEvaluateAutocomplete`, `OnEvaluateToolTip`, `RefreshBreakpointMarkers`); designer's `ibCodeEditorDesigner` (`designer/win/editor/codeEditor/codeEditorDesigner.{h,cpp}`) overrides them with `debugClient->…` calls. Document-less / sessionless mode: passing `nullptr` for the `ibMetaDocument*` skips metadata-driven autocomplete and breakpoint markers but keeps everything else functional — codeRunner uses this to embed the same editor without any DB / metadata / debug infrastructure.

---

## Module Descriptions

### `src/engine/backend/compiler/`

| File | Class | Role |
|---|---|---|
| `translateCode.h/cpp` | `ibTranslateCode` | Lexer: tokenises source text into `ibLexem` stream |
| `compileCode.h/cpp` | `ibCompileCode` | Parser and code generator: consumes lexemes, emits `ibByteCode` |
| `byteCode.h` | `ibByteCode`, `ibByteUnit` | Bytecode container: array of `ibByteUnit` instructions |
| `byteCodeAOT.cpp` | `ibByteCode::SerializeAOT/DeserializeAOT` | Binary persistence for the AOT cache (sys_bytecode_cache.blob); host-endian linear format with magic `'PBC1'` + format version |
| `procUnit.h/cpp` | `ibProcUnit` | Interpreter: executes `ibByteCode` against a variable stack |
| `procContext.h/cpp` | `ibRunContext` | Execution context: local variable frame, call stack |
| `value.h/cpp` | `ibValue` | Universal value type (Undefined, Boolean, Number, Date, String, Reference) |
| `codeDef.h` | enums | Opcode (`OPER_*`) and keyword (`KEY_*`) definitions |

### `src/engine/backend/databaseLayer/`

| File | Class | Role |
|---|---|---|
| `databaseLayer.h/cpp` | `ibDatabaseLayer` | Abstract base: Open/Close/RunQuery/PrepareStatement/Transactions |
| `databaseResultSet.h/cpp` | `ibDatabaseResultSet` | Abstract result set cursor |
| `preparedStatement.h` | `ibPreparedStatement` | Abstract prepared statement |
| `firebird/` | `ibFirebirdDatabaseLayer` | Firebird 3/4 driver |
| `postgres/` | `ibPostgresDatabaseLayer` | PostgreSQL driver |
| `sqllite/` | `ibSqliteDatabaseLayer` | SQLite 3 driver (note: directory named `sqllite`) |
| `mysql/` | `ibMysqlDatabaseLayer` | MySQL driver |
| `odbc/` | `ibOdbcDatabaseLayer` | ODBC generic driver |

### `src/engine/backend/metaCollection/`

Metadata object hierarchy. Every business object type extends `ibValueMetaObject` (defined in `metaObject.h`), which itself extends `ibValue` so metadata objects can be passed as script values.

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
| `systemManager.h/cpp` | `ibSystemManager` | Built-in function dispatcher; registers 88 built-in functions |
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
 ibTranslateCode::Translate()
   Lexer: produces vector<ibLexem>
   Each ibLexem: { type, keyword/delimiter number, string data, ibValue, line, position }
        │
        ▼
 ibCompileCode::Compile()
   Recursive-descent parser
   Emits ibByteUnit records into ibByteCode
   Resolves forward function references in a second pass
        │
        ▼
 ibByteCode
   vector<ibByteUnit>   — instruction stream
   map<wxString, ibByteFunction>  — function table (name → code line)
   vector<ibValue>      — constant pool
   vector<ibValue*>     — variable table
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
| Lambdas | `OPER_LFUNC`, `OPER_ENDLFUNC` (anonymous body fences — distinct from `OPER_FUNC`/`OPER_ENDFUNC` so a containing named-function's module-init skip doesn't terminate on a nested lambda's terminator), `OPER_FUNC_PTR` (materialises an `ibValueFunction` wrapper into a slot), `OPER_CALL_LAMBDA` (dynamic call — target read from a slot at runtime, must wrap an `ibValueFunction`). See `docs/lambda.md`. |
| LINQ | `OPER_CALL_LINQ` — universal pipeline method on an iterable receiver (Where / Select / OrderBy / GroupBy / Join / Skip / Take / Aggregate / ...). Compile-side detects LINQ method names at chain-method emit time and chooses this opcode over `OPER_CALL_METHOD`; runtime reads the `ibValue::ibLinqMethod` enum id directly from `m_param3.m_numIndex` (no const-string lookup, no `FindMethod` walk) and dispatches through the virtual `ibValue::DispatchLinqMethod`. See `docs/linq.md`. |
| Arrays | `OPER_GET_ARRAY`, `OPER_SET_ARRAY`, `OPER_CHECK_ARRAY`, `OPER_SET_ARRAY_SIZE`, `OPER_ENTER_A`, `OPER_GET_A`, `OPER_SET_A` |
| Objects | `OPER_NEW`, `OPER_SET_TYPE` |
| Exceptions | `OPER_TRY`, `OPER_ENDTRY`, `OPER_RAISE`, `OPER_RAISE_T` |
| Optimised const variants | `OPER_ADDCONS`, `OPER_SUBCONS`, `OPER_MULTCONS`, `OPER_DIVCONS`, `OPER_MODCONS`, `OPER_GTCONS`, etc. |

Each opcode has type-specialised variants selected by adding `TYPE_DELTA1` (number), `TYPE_DELTA2` (string), `TYPE_DELTA3` (date), or `TYPE_DELTA4` (boolean) to the base opcode.

### Execution Model

`ibProcUnit` maintains a linked list of parent `ibProcUnit` objects representing the module scope chain. Each call creates an `ibRunContext` holding the local variable array. Exceptions are thrown by value and caught by const reference (standard C++): `throw ibBackendInterruptException();` (often through static `Error()` helpers like `ibBackendCoreException::Error(_("..."))`) caught as `catch (const ibBackendException& err)`. `ibBackendException` has a virtual destructor so catching by base preserves dynamic type for `dynamic_cast`. Rethrow uses bare `throw;`. The previous throw-by-pointer / `catch(const ibBackendException*)` pattern is fully removed.

### Keyword Inventory

44 keywords defined in `KEY_*` enumerators (e.g., `KEY_IF`, `KEY_FOR`, `KEY_FOREACH`, `KEY_PROCEDURE`, `KEY_FUNCTION`, `KEY_TRY`, `KEY_EXCEPT`, `KEY_ENDTRY`, `KEY_RAISE`, `KEY_NEW`, etc.) plus preprocessor keywords (`KEY_DEFINE`, `KEY_UNDEF`, `KEY_IFDEF`, `KEY_IFNDEF`, `KEY_REGION`, `KEY_ENDREGION`). Keywords are English-only — there are no Cyrillic / Russian-language synonyms. Two parallel syntax modes share these keywords and compile to the same bytecode:
- **VES** — Visual-Basic-style: `If c Then … EndIf`, `For Each x In coll … EndDo`, `Procedure F() … EndProcedure`. Keyword-fenced blocks.
- **CES** — C-style: `if (c) { … }`, `for each (x in coll) { … }`, `Procedure F() { … }`. Brace-delimited. Default for new configurations since 2026-05-10.

Mode is process-global on `ibCompileCode::SetCodeStyle()`; serialised configurations preserve their stored Syntax flag (wire token still reads `vbs` for back-compat).

---

## Metadata System

### ibValueMetaObject

`ibValueMetaObject` (defined in `src/engine/backend/metaCollection/metaObject.h`) is the abstract base for all configuration objects. It extends both `ibValue` (so it can be assigned to script variables) and `ibPropertyObjectHelper` (so properties appear in the designer's object inspector).

Key attributes:
- `ibMetaID m_metaId` — numeric identifier within the configuration
- `ibGuid m_metaGuid` — globally-unique GUID
- `wxString GetName()` / `GetSynonym()` — developer name and user-visible synonym
- `ibMetaData* m_metaData` — back-pointer to the owning metadata container

### Eleven Business Object Types

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

### Inheritance Chain (simplified)

```
ibValue
  └─ ibValueMetaObject
       ├─ ibValueMetaObjectAttribute         (scalar field descriptor)
       │    └─ ibValueMetaObjectConstant
       ├─ ibValueMetaObjectRecordData        (record-based objects)
       │    ├─ ibValueMetaObjectRecordDataMutableRef
       │    │    └─ ibValueMetaObjectDocument
       │    ├─ ibValueMetaObjectRecordDataHierarchyMutableRef
       │    │    ├─ ibValueMetaObjectCatalog
       │    │    ├─ ibValueMetaObjectChartOfCharacteristicTypes
       │    │    └─ ibValueMetaObjectChartOfAccounts
       │    └─ ibValueMetaObjectRecordDataExt
       │         ├─ ibValueMetaObjectDataProcessor
       │         └─ ibValueMetaObjectReport
       ├─ ibValueMetaObjectRegisterData
       │    ├─ ibValueMetaObjectInformationRegister
       │    ├─ ibValueMetaObjectAccumulationRegister
       │    └─ ibValueMetaObjectAccountingRegister
       └─ ibValueMetaObjectRecordDataEnumRef
            └─ ibValueMetaObjectEnumeration
```

### Configuration Persistence

`ibMetaDataConfiguration::LoadDatabase()` / `SaveDatabase()` serialise the entire metadata tree to/from the system database using `ibReaderMemory` / `ibWriterMemory` binary streams. Each metadata class registers a `ibClassID` CLSID (e.g. `MD_CAT` for Catalog) used as a type discriminator during loading.

### Text Format (XML / JSON)

Configurations can be exported and imported in text formats for Git VCS, AI generation, and human editing:

- **XML** (OES-XML-2.0): `metadataConfigurationXML.cpp` — `SaveConfigToXML()` / `LoadConfigFromXML()`
- **JSON** (OES-JSON-1.0): `metadataConfigurationJSON.cpp` — `SaveConfigToJSON()` / `LoadConfigFromJSON()` using nlohmann/json

Both formats support full round-trip serialization of all 11 metadata types with attributes, type qualifiers, tabular sections, forms, modules, predefined values, and all bindings.

### Accounting Objects (Work in Progress)

Three metadata types support double-entry bookkeeping. Core classes and designer integration are implemented; SQL DDL, runtime bindings, and Balance/Turnovers queries are under active development.

| Type | CLSID | Purpose |
|---|---|---|
| ChartOfCharacteristicTypes (MD_CHRC) | Subconto type definitions — each element stores `ibTypeDescription` with allowed value types |
| ChartOfAccounts (MD_CHOA) | Chart of accounts with AccountType (Active/Passive/AP), accounting signs, predefined SubcontoKinds tabular section |
| AccountingRegister (MD_AREG) | Double-entry register with Account, RecordType (Debit/Credit), Subconto1-3, Balance/Turnovers/DrCrTurnovers |

Bindings: AccountingRegister → ChartOfAccounts (via `ibPropertyChartOfAccounts`), ChartOfAccounts → ChartOfCharacteristicTypes (via `ibPropertyChartOfCharacteristicTypes`). Each binding has its own property class and advprop UI handler with CLSID-filtered selection dialog.

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
- **Configuration language** — `m_languageCode` (explicit override) plus `m_resolvedLanguageCode` (cached `override || user-default`). Selects which metadata synonym / form-label translation is shown. Per-session so concurrent web tabs each render their own user's language. Distinct from the platform's wxLocale (UI gettext, process-wide). Routed through `ibBackendLocalization::GetActiveLanguage()` / `SetActiveLanguage()`.
- **Root module manager** — `m_root : ibValuePtr<ibValueModuleManagerConfiguration>`. Created via `EnsureRoot()` in `ibSessionRegistry::NotifyAuthenticated`'s middle phase (between `OnFirstConnect` and `OnAuthenticated` listener phases). Stays nullptr for sessions that never run scripts (Designer, WebServer technical, Launcher).
- **Frame** — `m_frame : ibBackendDocFrame*` (non-owning) for plain `ibSession`, or overridden virtual `GetFrame()` on `ibGUISession`. The frame belongs to the session, not to a process-wide singleton.
- **Per-session debug** — optional `ibDebugSession` (CV/mutex + per-session watch expressions + run context) so concurrent web sessions can each enter their own debug loop without blocking.
- **Exclusive (monopoly) mode** — `m_exclusive`. At most one session in the registry holds it; while held, every other Connect parks until release.
- **Server back-link** — `m_server : weak_ptr<ibSession>` from a server-spawned client to the session that hosts it (e.g., wes's WebClient → wes's WebServer). Used by shutdown logic, cluster topology, and admin UI.

`ibSession::Current()` is the canonical "session this code is currently working on". Dispatch depends on `AccessMode` (a process-wide setting fixed at startup before any session is created):

- **Single** (desktop, daemon, codeRunner, classChecker) — one session per process for its lifetime. `Current()` returns the lone session regardless of thread.
- **Shared** (wenterprise-server) — per-thread lookup of bound sessions, with a process-wide fallback for threads that aren't bound (registry consumer, signal handlers).

`ibSessionScope` (legacy) and `ibSessionThreadBinding` (preferred for app entry points) are the RAII helpers that bind a session to the calling thread. The interpreter no longer reads global `thread_local` state directly: `ibProcUnitState` lives under `ibSession` (`session.h`), and the only `thread_local` slot in `session.cpp` is a fallback for sessionless callers (codeRunner sandbox / system bootstrap). The worker pool (`workerPool.h` + `workerPoolHeadless.cpp`) leases a session into a thread via `tl_currentLease` and runs the request on it.

Runtime ProcUnits are not held in a per-session map on `ibSession` itself. Each module descriptor (`ibModuleDataObject`) keeps its own `m_procUnit`. Per-session ProcUnit ownership through a descriptor map is the target end-state of the runtime-facade plan (see [`runtime-facade.md`](runtime-facade.md), step 1) — not yet landed.

### ibSessionRegistry

`ibSessionRegistry` (`src/engine/backend/session/sessionRegistry.h`) is the process-wide session manager:

- **Single-consumer queue + priority** — one registry thread processes `Add / Attach / Detach / Remove / SetActivity` requests (Urgent → Normal → Low → Background). All DB mutation for `sys_session` happens on this thread.
- **Row-level pessimistic lock as source of truth** — the registry holds a long-running transaction with `SELECT ... WITH LOCK` over its own-session rows. Process crash → DB rolls back → another process's sweep acquires the lock via `TryProbeRowLock` → zombie row DELETEd. Replaces old heartbeat-UPDATE model.
- **Connect/Disconnect API** — desktop `ibApplicationData::Connect` and web `ibWebSession::Login` both call `registry.Connect(req)` which returns an `ibSessionTicket` (RAII, dtor submits Remove@Urgent).
- **Single mutator of session state.** `ibSession`'s state-machine mutators (`Transition`, `TransitionAuth`, `SetIdentity`, `SetInserted`, `WaitForState`, `WaitForAuth`) and auth-flow setters (`SetUserInfo`, `EnableDebug`, `SetSessionRawPassword`) are private under `friend ibSessionRegistry`. Public façades `ibSessionRegistry::InstallUser(s, info, pwd)` and `EnableDebugForSession(s)` are the only entry points for auth bring-up — `appData` and login dialogs route through them, never poke session internals directly.
- **Cluster snapshot** — `ibSessionRegistry::GetClusterSnapshot()` returns `ibSessionSnapshot` (formerly `ibApplicationDataSessionArray` on `appData`), refreshed every ~3s by `JobRefreshSnapshot`. Snapshot now lives in `backend/session/sessionSnapshot.{h,cpp}`.

The registry supports multiple concurrent sessions (N on web, 1 on desktop) through the same mechanism. See `project_session_registry_refactor` memory entry for the current implementation status.

### Runtime ownership — current state and direction

**Current:**
- Compile state (`ibCompileCode` with immutable `ibByteCode`) lives on the descriptor (`ibModuleDataObject`) and is shared across sessions.
- ProcUnits are kept on the descriptor itself (`ibModuleDataObject::m_procUnit`). The descriptor's runtime is rebuilt for each session by `ibValueModuleManager::AttachRuntime(session)` and torn down by `DetachRuntime(session)` — both serialised by `m_runtimeMutex`. There is no per-session ProcUnit map yet; the descriptor's ProcUnit is single-occupancy at any given moment, so concurrent web sessions on the same descriptor must coordinate through the runtime mutex (current scaling ceiling).
- The session's root `ibValueModuleManagerConfiguration` is owned by `ibSession::m_root` (intrusive-refcounted via `ibValuePtr`). `ibSession::CreateRoot(metaData)` allocates it; `ibSession::CompileRoot()` runs `CreateMainModule`; `appData`'s `OnAuthenticated` listener then drives `RunDatabase` (one-shot per process) + `CompileRoot` + `mm->AttachRuntime(session)`.
- `BeforeStart / OnStart / BeforeExit / OnExit` events dispatch through the session's root module manager.

**Direction (in progress, see [`runtime-facade.md`](runtime-facade.md)):**

`ibValueModuleManager` becomes the per-session runtime root. The descriptor's `m_procUnit` field disappears in favour of a per-descriptor `m_runtimes : map<ibSession*, shared_ptr<ibModuleDataObject>>`. Nested nodes (common module, catalog/document instance, form, external DP) inherit from `ibModuleDataObject + ibValue` and parent up via `weak_ptr<ibModuleDataObject> m_parent`. Eval is the only exception — outside the descriptor map, parent set from `ibValueModuleManager::Current()` at compile time.

Target structure for a runtime node:

```
ibModuleDataObject (per-session)
  ├── compileModule  — back-ref to compile state (descriptor-shared)
  ├── procUnit       — shared_ptr<ibProcUnit> (mutable runtime state)
  └── parent         — weak_ptr<ibModuleDataObject> (common→root, form→object|root, eval→Current())

ibValueModuleManager : ibModuleDataObject (root only — per-session singleton)
  ├── m_session     — which session owns it
  └── m_context     — runtime context (locals frame chain)
```

- Main module = session's runtime root; common modules, forms, per-instance catalog/document runtimes hang off as children via `parent` weak chain.
- Script dispatch walks the parent chain — each runtime reaches enclosing globals through parent's procUnit.
- Teardown cascade: session.Stop() iterates `m_touchedDescriptors` and calls `_DropRuntime(this)` on each; the descriptor drops its `shared_ptr<runtime>` for that session, the runtime dtor releases its procUnit, parent weak refs expire bottom-up.
- `ibByteCode` becomes self-contained (holds its own moduleName, rootContext, parent-bytecode ref); the `byteCode->m_compileModule` back-pointer is removed. This decouples runtime lifetime from `ibCompileCode` lifetime — metadata reload can drop compile state while running sessions hold their bytecode through their shared_ptr.

**Same model for desktop and web** — desktop has N=1 session (process-wide, `AccessMode::Single`), web has N per-cookie (`AccessMode::Shared`). The `ibSessionRegistry + ibSession + ibSessionScope + runtime tree` stack is identical; differences are only in session count and threading (desktop = wx main thread dispatches scripts; web = per-session worker thread via `RunOnWorker`).

### Designer — compile only

Designer (`eDESIGNER_MODE`) creates sessions without runtime — `AttachRuntime` returns early for Designer role. Designer reads `ibCompileCode` for autocomplete, function search, jump-to-definition, and cascading recompile. Scripts are not executed. Debug sessions attach to a separate runtime process (enterprise.exe / wenterprise-server.exe) via the TCP debug protocol.

---

## Database Abstraction

### Class Hierarchy

```
ibDatabaseLayer  (abstract — databaseLayer.h)
  ├─ ibFirebirdDatabaseLayer   (databaseLayer/firebird/)
  ├─ ibPostgresDatabaseLayer   (databaseLayer/postgres/)
  ├─ ibSqliteDatabaseLayer     (databaseLayer/sqllite/)
  ├─ ibMysqlDatabaseLayer      (databaseLayer/mysql/)
  └─ ibOdbcDatabaseLayer       (databaseLayer/odbc/)

ibDatabaseResultSet  (abstract)
  ├─ ibFirebirdResultSet
  ├─ ibPostgresResultSet
  ├─ ibSqliteResultSet
  ├─ ibMysqlPreparedStatementResultSet
  └─ ibOdbcResultSet

ibPreparedStatement  (abstract)
  ├─ ibFirebirdPreparedStatement
  ├─ ibPostgresPreparedStatement
  ├─ ibSqlitePreparedStatement
  ├─ ibMysqlPreparedStatement
  └─ ibOdbcPreparedStatement
```

### Access Pattern

All database access goes through the `db_query` macro, which calls `ibApplicationData::GetDatabaseLayer()` and returns the active `ibDatabaseLayer*`. Example:

```cpp
ibDatabaseResultSet* rs = db_query->RunQueryWithResults(
    wxT("SELECT * FROM %s WHERE guid = ?"), table_name);
```

Each driver folder contains an `engine/` subdirectory with the vendored native client library.

---

## Form System

### ibValueForm Hierarchy

```
ibBackendControlFrame  (abstract interface — backend_form.h)
  └─ ibBackendValueForm  (abstract — backend_form.h)
       └─ ibValueForm  (concrete — frontend/visualView/ctrl/form.h)
```

`ibValueForm` owns the complete control tree for one open form. It is created by `ibBackendValueForm::CreateNewForm()`, which resolves the `ibValueMetaObjectFormBase` descriptor from metadata and instantiates the correct form type.

### 22 Visual Controls

Implemented in `src/engine/frontend/visualView/ctrl/`:

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
  └─ ibApplicationData::CreateServerAppDataEnv(mode, server, port, ibUser, ibPwd, db, locale)
       └─ ibDatabaseLayer::Open(server, port, db, ibUser, ibPwd)   # DB-level admin connection
            └─ appData->CreateSession<ibEnterpriseSession>()        # phased session lifecycle
                 # registry runs Connect(req) under the session factory:
                 #   - Submit(Add, Normal), waits state Added
                 #   - Add handler INSERTs sys_session row under row-lock
                 #   - OnCreateSession() fires on the main thread
                 #     (ibGUISession overrides — builds the wx frame here)
                 └─ session->Open(user, password)                   # auth orchestration
                      └─ ticket.Attach(user, pwd) — Submit(Attach, Normal)
                           └─ ibApplicationData::AuthenticateUser
                                  (PBKDF2 preferred, MD5 silent-upgrade path)
                                └─ InstallUser writes session->m_userInfo
                                     └─ NotifyAuthenticated phases (registry-driven):
                                          1. OnFirstConnect — metadataCreate (one-shot)
                                          2. session->EnsureRoot() — CreateRoot(activeMetaData)
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

```
Script (desktop) / HTTP POST /open?metaID=N (web):
OpenForm("Catalog.Products.ListForm")
  └─ ibValueMetaObjectFormBase::CreateAndBuildForm(metaForm, owner, srcObj, uniqueKey)
       └─ ibSession::CurrentFrame()->CreateNewForm(metaFormObject, ownerControl, srcObject)
              # session-owned frame; desktop = ibFrontendDocMDIFrame, web = ibWebFrame
            └─ ibValueForm constructed (per-instance compileModule + ProcUnit)
                 └─ LoadFormData / BuildForm — control tree built from metadata
                      └─ frame creates the doc-view child (CAuiDocChildFrame / ibWebDocChildFrame)
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

- **Configuration language** — per-session `ibSession::m_resolvedLanguageCode`, selects metadata synonyms / form-label translations stored *inside* the configuration. See `ibBackendLocalization::GetActiveLanguage` / `SetActiveLanguage`.
- **Process UI language** — gettext catalogs under `locale/` (`ru.po` / `uk.po` + compiled `*.mo`). Strings wrapped in `_("...")` macros across `src/engine/**` end up in the `.mo` and are looked up by wxLocale at runtime.

**Workflow** (when adding new `_()` strings):

```
# Regenerate the template from current sources (uses Poedit's gettext tools).
xgettext --from-code=UTF-8 --keyword=_ --keyword=wxTRANSLATE \
         --keyword=wxPLURAL:1,2 --language=C++ --no-wrap \
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
