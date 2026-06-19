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
│  designer.exe   enterprise.exe   launcher.exe   daemon.exe   │
│  codeRunner.exe                                              │
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
| `enterprise.exe` | `eRUNTIME_MODE` | Desktop thick-client runtime (GUI, single user session per process) |
| `wenterprise-server.exe` | `eWEB_RUNTIME_MODE` | Web runtime host — HTTP server, N per-cookie user sessions, browser client |
| `daemon.exe` | `eSERVICE_MODE` | Headless background service |
| `codeRunner.exe` | `eSERVICE_MODE` | Executes a single script module |

> Both thick-client and web hosts are "runtime", differing only in UI transport — hence `eRUNTIME_MODE` / `eWEB_RUNTIME_MODE` (renamed from the former `eENTERPRISE_MODE` / `eWEB_ENTERPRISE_MODE`).

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
- **Doc-view frames** — backend-facing interface `ibBackendDocFrame` (`backend/backend_mainFrame.h`); concrete implementations are `ibFrontendMainFrame` (desktop, `frontend/mainFrame/mainFrame.h`: `public ibBackendDocFrame, public wxAuiMDIParentFrame, public ibDocParentFrameAnyBase`) and `ibWebFrame` (web). Child frames are `ibAuiDocChildFrame` / `ibDialogDocChildFrame` on desktop (`frontend/mainFrame/mainFrameChild.h`) and `ibWebDocChildFrame` on web. The frame is owned by the `ibSession` that created it (no process-level singleton on the backend side); the legacy `mainFrame` macro still exists in `frontend/mainFrame/mainFrame.h` as a frontend-local accessor for the GUI singleton, but new backend code reaches the frame through `ibSession::Current()->GetFrame()` or `ibSession::CurrentFrame()`.
- **`ibCodeEditor`** (`frontend/win/editor/codeEditor/`) — the Scintilla-based script editor. Lives in `frontend.dll` so any GUI host can use it: `designer.exe` (its module/form editors), `codeRunner.exe` (sessionless scratch runner). Highlighter, fold parser, auto-indent on Enter, format / increase / decrease indent, comment add/remove, Ctrl-Space autocomplete, GotoLine + ProceduresAndFunctions dialogs all live here. Debugger integration is a designer-only concern, kept out of the base via six virtual hooks (`IsDebuggerEnterLoop`, `OnEditDebugPoint`, `OnPatchModule`, `OnEvaluateAutocomplete`, `OnEvaluateToolTip`, `RefreshBreakpointMarkers`); designer's `ibCodeEditorDesigner` (`designer/win/editor/codeEditor/codeEditorDesigner.{h,cpp}`) overrides them with `debugClient->…` calls. Document-less / sessionless mode: passing `nullptr` for the `ibMetaDocument*` skips metadata-driven autocomplete and breakpoint markers but keeps everything else functional — codeRunner uses this to embed the same editor without any DB / metadata / debug infrastructure.

---

## Application Bootstrap and Ownership

OES runs on a single coordinator — `ibApplicationData` (`backend/appData.h`) — that owns every process-wide subsystem. No subsystem has a static `Instance()` of its own; every `Get*()` returns `nullptr` before bring-up and after teardown, callers null-check. The pattern is the same for every entry-point binary (enterprise / designer / launcher / codeRunner / daemon / wes); only the run-mode flag and the wxApp class change.

### The sandwich

```
  ┌─────────────────────────────────────────────────────────────────┐
  │ exe-specific main()                                             │
  │   • argv parsing, runMode pick                                  │
  │   • ibCrashGuard::Install (headless) OR ibWxApp::OnInit (GUI)   │
  │   • appDataCreateFile / appDataCreateServer                     │
  └────────────────────────────┬────────────────────────────────────┘
                               │
                               ▼
  ┌─────────────────────────────────────────────────────────────────┐
  │ ibApplicationData ctor — wires every subsystem in init order    │
  │                                                                 │
  │   m_connectionPool    ◄── primary DB layer, lazy clones         │
  │   m_pluginManager     ◄── scans plugins/ and loads .dll         │
  │   m_lockManager       ◄── sys_lock coordinator                  │
  │   m_queryableFactory  ◄── L4 query-engine source factory        │
  │   m_sessionRegistry   ◄── ibSession registry + worker pool      │
  │   m_logger            ◄── audit+trace sink (.olg); lazy after DB │
  │   m_helpService       ◄── syntax-helper corpus; lazy in locale  │
  │   m_activeMetaData    ◄── per-runMode fabric, populated later   │
  │                                                                 │
  │   (each owned subsystem ctor takes ib::AppDataCtorToken)        │
  └────────────────────────────┬────────────────────────────────────┘
                               │
                               ▼
  ┌─────────────────────────────────────────────────────────────────┐
  │ ibSession (per-cookie on web, single on desktop)                │
  │   • holds a connection holder out of the pool                   │
  │   • owns a per-session ibProcUnit (runtime)                     │
  │   • frame/document graph on GUI; visual host on web             │
  └─────────────────────────────────────────────────────────────────┘
```

### Construction order (`ibApplicationData::ibApplicationData`)

The init list is **the** ordering contract. Listed in the order they're constructed:

| # | Field | What it does | Why this slot |
|---|---|---|---|
| 1 | `m_connectionPool` | Pool of `ibDatabaseLayer` — master + lazy clones | Everything else needs DB access; this must exist first. |
| 2 | `m_pluginManager` | Loads `plugins/*.dll` | Plugins may need `db_query` (=pool). |
| 3 | `m_lockManager` | `sys_lock` table coordinator | Independent — could move; ordered for readability. |
| 4 | `m_queryableFactory` | L4 query-engine source factory (`ibQueryableFactory`) | Descriptor contents follow the metadata open/close lifecycle; the object itself lives with appData. |
| 5 | `m_sessionRegistry` | Session registry + worker pool (`PickWorkerCount(runMode)` workers) | After pool so first session can already check connections out. |

`m_logger` is created lazily in `CreateLogger()` after the DB opens (not in the init list).

`m_helpService` (syntax-helper corpus) is constructed lazily in `InitLocale()` once the platform locale is settled — corpus directory naming depends on the canonicalised locale code, so the service can't come up before locale resolution finishes. See `docs/syntax-helper-design.md` for the loader / `ZipSource` / pack-on-build pipeline.

`m_activeMetaData` is populated later by `CreateActiveMetaData(mode, flags)` — the fabric picks a concrete subclass (`ibMetaDataConfiguration` for runtime, `ibMetaDataConfigurationStorage` for designer) by `runMode`. Launcher/codeRunner have no metadata at all.

### Destruction order (`~ibApplicationData`)

The destructor does **business actions only** (the explicit hooks each subsystem needs before its dtor fires). It does NOT call `reset()` on any `unique_ptr` field — destruction is left to the compiler-generated sweep in **reverse declaration order**. Declaration order in `appData.h` is chosen so reverse sweep gives the safe sequence:

```
~ibApplicationData() {
    m_activeMetaData->OnDestroy();      // business hook
    m_sessionRegistry->Stop();          // drain workers, DELETE sys_session
    m_pluginManager->UnloadAll();       // each plugin sees Destroy()
    m_connectionPool->Shutdown();       // invalidate handouts
    // dtor sweep follows, in reverse declaration order:
    //   m_activeMetaData     (last declared → destroyed first)
    //   m_helpService
    //   m_sessionRegistry
    //   m_logger             (own SQLite, no external deps)
    //   m_queryableFactory
    //   m_lockManager
    //   m_pluginManager
    //   m_connectionPool     (first declared → destroyed last)
}
```

If a new subsystem joins, **add the business hook to the dtor body** and **append the field in declaration order so it dies before its dependencies**.

### Token pattern for subsystem construction

Every owned subsystem's ctor takes `ib::AppDataCtorToken` (`backend/appDataCtorToken.h`) as its first argument. The token's default ctor is private; only `ibApplicationData` is a friend, so only it can mint a token. External code that tries `new ibSessionRegistry(…)` gets a compile error — token is unreachable.

Why a token instead of `friend class ibApplicationData` on each subsystem header? Friend scattered across 7 headers means refactoring appData ripples into every owned class. A single token type centralises the "who can build subsystems" decision; subsystem headers stay clean and declare ctors public.

Unit tests build subsystems directly (no full appData) — the test CMake target defines `OES_TESTING`, which opens the token's default ctor inside test TUs only. Production builds (sln / CMake non-test) leave it undefined and the gate stays closed.

### Entry-point helpers

Two header-only helpers in `frontend/diagnostics/` cover the boilerplate every binary needs at startup. Header-only = no link-time `frontend.dll` dependency; both helpers only call `BACKEND_API ibCrashGuard::*` plus wx primitives the binary already links.

| Binary type | Helper | What it wires |
|---|---|---|
| GUI (`enterprise` / `designer` / `codeRunner` / `launcher`) | `ibWxApp` base class (`oesApp.h`) | `wxApp::OnInit` → `ibCrashGuard::Install` + `DoOnInit()`; `OnRun` → try/catch around `DoOnRun()`; 3 exception overrides (main-loop / unhandled / fatal). Subclass overrides only `GetExeName()` + optional `DoOnRun()`. |
| Console (`wenterprise-server` / `daemon`) | `ibOesConsoleBoot` RAII (`oesConsole.h`) | Single object that holds a `wxInitializer`, runs `wxSocketBase::Initialize`, calls `ibCrashGuard::Install`. `IsOk()` checks the wx init result. |

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
    └── (other backend categories)
```

Per-driver `ClassifyDatabaseError(int nativeCode)` on each `ibDatabaseErrorReporter` subclass maps the driver's native code (Firebird `isc_*` gds, PG SQLSTATE int, MySQL errno, ODBC SQLSTATE, SQLite result code) to a Kind. The mapping is regression-tested in `tests/test_dbTaxonomy.cpp` — if a future driver bump flips `SQLITE_CONSTRAINT (19)` away from `Kind::Constraint`, the test fails before code that branches on `IsRetryable()` silently misroutes.

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
| `byteCodeAOT.cpp` | `ibByteCode::SerializeAOT/DeserializeAOT` | Binary persistence for the AOT cache (sys_bytecode_cache.blob); host-endian linear format with magic `'PBC1'` + format version |
| `procUnit.h/cpp` | `ibProcUnit` | Interpreter: executes `ibByteCode` against a variable stack |
| `procContext.h/cpp` | `ibRunContext` | Execution context: local variable frame, call stack |
| `value.h/cpp` | `ibValue` | Universal value type — `TYPE_UNDEFINED`, `TYPE_NULL`, `TYPE_BOOLEAN`, `TYPE_NUMBER` (`ibNumber`), `TYPE_DATE`, `TYPE_STRING`, `TYPE_REFFER` |
| `codeDef.h` | enums | Opcode (`OPER_*`) and keyword (`KEY_*`) definitions |

### `src/engine/backend/databaseLayer/`

| File | Class | Role |
|---|---|---|
| `databaseLayer.h/cpp` | `ibDatabaseLayer` | Abstract base: Open/Close/RunQuery/PrepareStatement/Transactions |
| `databaseResultSet.h/cpp` | `ibDatabaseResultSet` | Abstract result set cursor |
| `preparedStatement.h` | `ibPreparedStatement` | Abstract prepared statement |
| `firebird/` | `ibDatabaseLayerFirebird` | Firebird 3/4 driver |
| `postgres/` | `ibDatabaseLayerPostgres` | PostgreSQL driver |
| `sqllite/` | `ibDatabaseLayerSQLite` | SQLite 3 driver (note: directory named `sqllite`) |
| `mysql/` | `ibDatabaseLayerMySQL` | MySQL driver |
| `odbc/` | `ibDatabaseLayerODBC` | ODBC generic driver |

> Concrete driver classes use the `ib<Base><Vendor>` suffix pattern: `ibDatabaseLayer<Vendor>`, `ibDatabaseResultSet<Vendor>`, `ibPreparedStatement<Vendor>` (e.g. `ibDatabaseResultSetFirebird`, `ibPreparedStatementPostgres`).

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
| `systemManager.h/cpp` | `ibSystemManager` | Built-in function dispatcher; registers ~93 built-in functions (count drifts as features land; grep `AppendFunc\|AppendProc` for the live total) |
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

Each opcode has type-specialised variants selected by adding `TYPE_DELTA1` (number), `TYPE_DELTA2` (string), `TYPE_DELTA3` (date), or `TYPE_DELTA4` (boolean) to the base opcode.

### Execution Model

`ibProcUnit` maintains a linked list of parent `ibProcUnit` objects representing the module scope chain. Each call creates an `ibRunContext` holding the local variable array. Exceptions are thrown by value and caught by const reference (standard C++): `throw ibBackendInterruptException();` (often through static `Error()` helpers like `ibBackendCoreException::Error(_("..."))`) caught as `catch (const ibBackendException& err)`. `ibBackendException` has a virtual destructor so catching by base preserves dynamic type for `dynamic_cast`. Rethrow uses bare `throw;`. The previous throw-by-pointer / `catch(const ibBackendException*)` pattern is fully removed.

### Keyword Inventory

60 keywords defined in `KEY_*` enumerators (`codeDef.h`, `KEY_IF=0` … `LastKeyWord`), in lock-step with `s_listKeyWord[]` in `translateCode.cpp`. Groups:
- Control / structure — `KEY_IF`, `KEY_FOR`, `KEY_FOREACH`, `KEY_WHILE`, `KEY_PROCEDURE`, `KEY_FUNCTION`, `KEY_TRY`, `KEY_EXCEPT`, `KEY_ENDTRY`, `KEY_RAISE`, `KEY_RETURN`, `KEY_NEW`, …
- Access modifiers (leading) — `KEY_PUBLIC` (was `Export`), `KEY_PRIVATE`, `KEY_PROTECTED`
- Preprocessor — `KEY_DEFINE`, `KEY_UNDEF`, `KEY_IFDEF`, `KEY_IFNDEF`, `KEY_ELSEDEF`, `KEY_ENDIFDEF`, `KEY_REGION`, `KEY_ENDREGION`
- LINQ — `KEY_FROM`, `KEY_WHERE`, `KEY_SELECT`, `KEY_ORDERBY`, `KEY_ASCENDING`, `KEY_DESCENDING`, `KEY_TAKE`, `KEY_SKIP`, `KEY_DISTINCT`, `KEY_JOIN`, `KEY_ON`, `KEY_EQUALS`, `KEY_GROUP`, `KEY_BY`, `KEY_INTO` (`KEY_IN` is reused from `Foreach`)

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
  - `ibJsonProvider` (`serialize/jsonProvider.{h,cpp}`) — JSON export for Git VCS / AI generation / human reading. **Write-only** (`Read` is a no-op).
- **`ibDataBuilder`** owns the root node and drives `Save(provider, writer)` / `Load(provider, reader)`.

`ibReaderMemory` / `ibWriterMemory` remain the underlying chunked byte streams; `ibBinaryProvider` emits the node-tree layout into them. Each metadata class registers a `ibClassID` CLSID (e.g. `MD_CAT` for Catalog) used as the per-node type discriminator.

File-level export/import goes through `LoadConfigFromFile()` / `SaveConfigToFile()` on `ibMetaDataConfigurationBase`. (The former separate `metadataConfigurationXML.cpp` / `metadataConfigurationJSON.cpp` and the `SaveConfigToXML/JSON` API have been replaced by the provider model; XML support is currently retired.)

### Accounting Objects (Work in Progress)

Three metadata types support double-entry bookkeeping. Core classes and designer integration are implemented; SQL DDL, runtime bindings, and Balance/Turnovers queries are under active development.

| Type | CLSID | Purpose |
|---|---|---|
| ChartOfCharacteristicTypes | `MD_CHRC` | Subconto type definitions — each element stores `ibTypeDescription` with allowed value types |
| ChartOfAccounts | `MD_CHOA` | Chart of accounts with AccountType (Active/Passive/AP), accounting signs, predefined SubcontoKinds tabular section |
| AccountingRegister | `MD_AREG` | Double-entry register with Account, RecordType (Debit/Credit), Subconto1-3, Balance/Turnovers/DrCrTurnovers |

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
- **Root module manager** — `m_root : ibValuePtr<ibValueModuleManagerRuntimeConfiguration>`. Created via `EnsureRoot()` in `ibSessionRegistry::NotifyAuthenticated`'s middle phase (between `OnFirstConnect` and `OnAuthenticated` listener phases). Stays nullptr for sessions that never run scripts — **the Designer never creates a root** (`EnsureRoot` is gated on `DesignerMode()`; it uses the lightweight `ibValueModuleManagerDesigner` in the compile cache instead), and likewise WebServer technical / Launcher. Objects/records/modules reach the right manager through the `ibSession::GetEditModuleManager(metaData)` seam (Designer → compile-cache designer manager; runtime → `m_root`). See `module-manager-split.md`.
- **Frame** — `virtual ibBackendDocFrame* GetFrame() const { return nullptr; }` on base `ibSession`. Frame storage lives on derived sessions that have a GUI surface (e.g. `ibWebClientSession::SetFrame(ibWebFrame*)`; `ibGUISession` desktop variants). Base has no `m_frame` field — null means "no frame on this session" (codeRunner / wenterprise-server technical session). The frame belongs to the session that created it, not to a process-wide singleton.
- **Per-session debug** — optional `ibDebugSession` (CV/mutex + per-session watch expressions + run context) so concurrent web sessions can each enter their own debug loop without blocking.
- **Exclusive (monopoly) mode** — `m_exclusive`. At most one session in the registry holds it; while held, every other Connect parks until release.
- **Server back-link** — `m_server : weak_ptr<ibSession>` from a server-spawned client to the session that hosts it (e.g., wes's WebClient → wes's WebServer). Used by shutdown logic, cluster topology, and admin UI.

`ibSession::Current()` is the canonical "session this code is currently working on". Dispatch depends on `AccessMode` (a process-wide setting fixed at startup before any session is created):

- **Single** (desktop, daemon, codeRunner) — one session per process for its lifetime. `Current()` returns the lone session regardless of thread.
- **Shared** (wenterprise-server) — per-thread lookup of bound sessions, with a process-wide fallback for threads that aren't bound (registry consumer, signal handlers).

`ibSessionScope` (legacy) and `ibSessionThreadBinding` (preferred for app entry points) are the RAII helpers that bind a session to the calling thread. The interpreter no longer reads global `thread_local` state directly: `ibProcUnitState` lives under `ibSession` (`session.h`), and the only `thread_local` slot in `session.cpp` is a fallback for sessionless callers (codeRunner sandbox / system bootstrap). The worker pool (`workerPool.h` + `workerPoolHeadless.cpp`) leases a session into a thread via `tl_currentLease` and runs the request on it.

Runtime ProcUnits live on per-session descriptors. The session's root `ibValueModuleManagerRuntimeConfiguration` (`ibSession::m_root`) owns common modules, forms, and per-instance object runtimes; each child is its own descriptor (`ibRuntimeModuleDataObject`) carrying its own `shared_ptr<ibProcUnit>`. Concurrent web sessions therefore each work on their own descriptor instances — no shared ProcUnit, no cross-session execution mutex. See `runtime-facade.md` for the descriptor composition / parent chain details.

### ibSessionRegistry

`ibSessionRegistry` (`src/engine/backend/session/sessionRegistry.h`) is the process-wide session manager:

- **Single-consumer queue + priority** — one registry thread processes `Add / Attach / Detach / Remove / SetActivity` requests (Urgent → Normal → Low → Background). All DB mutation for `sys_session` happens on this thread.
- **Liveness via heartbeat on `lastActive`** — each process's `JobHeartbeatOwn` UPDATEs `lastActive` on its own `sys_session` rows every ~1s. Any row trailing `now` by more than `kStaleCutoffSec` (10s, in `sessionRegistry.cpp`) is treated as a zombie and DELETEd by another process's sweep. The earlier row-lock-as-source-of-truth design (a long-running `SELECT ... WITH LOCK` over own-session rows, probed via `TryProbeRowLock`) was **rolled back** — it self-deadlocked (see `session-registry.md §4`); only retirement comments remain in code. Record write protection is a separate concern — optimistic `DataVersion` check + the `sys_lock` table, see [record-locks.md](record-locks.md).
- **Connect/Disconnect API** — desktop `ibApplicationData::Connect` and web `ibWebSession::Login` both call `registry.Connect(req)` which returns an `ibSessionTicket` (RAII, dtor submits Remove@Urgent).
- **Single mutator of session state.** `ibSession`'s state-machine mutators (`Transition`, `TransitionAuth`, `SetIdentity`, `SetInserted`, `WaitForState`, `WaitForAuth`) and auth-flow setters (`SetUserInfo`, `EnableDebug`, `SetSessionRawPassword`) are private under `friend ibSessionRegistry`. Public façades `ibSessionRegistry::InstallUser(s, info, pwd)` and `EnableDebugForSession(s)` are the only entry points for auth bring-up — `appData` and login dialogs route through them, never poke session internals directly.
- **Cluster snapshot** — `ibSessionRegistry::GetClusterSnapshot()` returns `ibSessionSnapshot` (formerly `ibApplicationDataSessionArray` on `appData`), refreshed every ~3s by `JobRefreshSnapshot`. Snapshot now lives in `backend/session/sessionSnapshot.{h,cpp}`.

The registry supports multiple concurrent sessions (N on web, 1 on desktop) through the same mechanism. See `project_session_registry_refactor` memory entry for the current implementation status.

### Runtime ownership

**Compile state — shared, immutable.** `ibCompileCode` produces an `ibByteCode` that lives on the configuration's compile descriptor (`ibCompileModule` on `ibValueMetaObjectModuleBase`). One bytecode per module is shared across all sessions; rebuilt only on Designer edit or metadata reload. AOT cache (`sys_bytecode_cache` via `ibByteCodeCache`) lets `Compile()` skip the parse+emit on cache hits — the cached blob is `ibByteCode::SerializeAOT/DeserializeAOT` (magic `'PBC1'`; see the [compiler module table](#srcenginebackendcompiler)).

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
- Each common module wraps a child `ibRuntimeModuleDataObject` (`backend/moduleInfo.h`) carrying its own `shared_ptr<ibProcUnit>` and `m_binder` (per-execute context-var binder produced by `bc.CreateBinder()`). Context handles (`ThisObject`/`ThisForm`), scope containers, export handles (`Controls`/`DataSource`/…) and a module's own injected locals (a constant's `Value`) are registered once via `Bind{Context,Scope,Export,Local}Variable` and seed the binder — they replaced the hand-rolled `PrepareNames`/`AppendProp` name surface. See [Name binding](name-binding.md).
- Forms, per-instance catalog/document runtimes, external data processors / reports hang off as children of the root via the same descriptor mixin (`m_parent` raw-pointer chain; container enforces parent-outlives-child).
- Concurrent sessions therefore run on **physically separate** ProcUnit instances. The shared resource is the immutable `ibByteCode` (read-only); per-session frame stacks, locals, and binders are isolated.

**`m_runtimeMutex` guards bring-up vs teardown, not execution.** `ibValueModuleManager::AttachRuntime(session)` (called from `ibApplicationData::Connect` for desktop / `ibWebSession::Login` for web) builds the runtime tree under the lock. `DetachRuntime(session)` drops it under the same lock. Per-session script execution does NOT take this lock — different sessions execute in parallel on their own descriptors.

**Worker pool dispatch.** Script execution runs on a worker thread leased via `ibWorkerPool` (`backend/session/workerPool.h` + headless impl). Each request leases a session into `tl_currentLease` for the call's duration; `ibSession::Current()` resolves through this slot. Desktop has N=1 session on the wx main thread; web has N per-cookie sessions, each pinned to its own worker. The script interpreter never touches global `thread_local` state directly — `ibProcUnitState` lives under `ibSession`, the one `thread_local` fallback in `session.cpp` exists only for sessionless callers (codeRunner sandbox / system bootstrap).

**Same model for desktop and web** — the only difference is session count and entry threading. The `ibSessionRegistry + ibSession + ibSessionScope + per-session runtime tree + worker pool` stack is identical across all run modes.

**Bytecode self-contained.** `ibByteCode` holds its own moduleName, rootContext, parent-bytecode ref, and dependency manifest. There is no `byteCode->m_compileModule` back-pointer; runtime lifetime is decoupled from `ibCompileCode` lifetime, so metadata reload can drop compile state while running sessions hold their bytecode through their shared_ptr.

### Designer — compile only

Designer (`eDESIGNER_MODE`) creates sessions without runtime — `AttachRuntime` returns early for Designer role. Designer reads `ibCompileCode` for autocomplete, function search, jump-to-definition, and cascading recompile. Scripts are not executed. Autocomplete surfaces bound names by reading the compile module's bind tables along the **descriptor** parent chain (the compile-module parent link is dead in the designer) — see [Name binding § Designer](name-binding.md#designer--surfacing-the-same-binds). Debug sessions attach to a separate runtime process (enterprise.exe / wenterprise-server.exe) via the TCP debug protocol.

---

## Database Abstraction

### Class Hierarchy

```
ibDatabaseLayer  (abstract — databaseLayer.h)
  ├─ ibDatabaseLayerFirebird   (databaseLayer/firebird/)
  ├─ ibDatabaseLayerPostgres   (databaseLayer/postgres/)
  ├─ ibDatabaseLayerSQLite     (databaseLayer/sqllite/)
  ├─ ibDatabaseLayerMySQL      (databaseLayer/mysql/)
  └─ ibDatabaseLayerODBC       (databaseLayer/odbc/)

ibDatabaseResultSet  (abstract)
  ├─ ibDatabaseResultSetFirebird
  ├─ ibDatabaseResultSetPostgres
  ├─ ibDatabaseResultSetSQLite
  ├─ ibDatabaseResultSetMySQL
  └─ ibDatabaseResultSetODBC

ibPreparedStatement  (abstract)
  ├─ ibPreparedStatementFirebird
  ├─ ibPreparedStatementPostgres
  ├─ ibPreparedStatementSQLite
  ├─ ibPreparedStatementMySQL
  └─ ibPreparedStatementODBC
```

### Access Pattern

All database access goes through the `db_query` macro, which is `ibApplicationData::GetDatabaseLayer()` — it resolves through the connection pool and returns a `std::shared_ptr<ibDatabaseLayer>` (the active checked-out layer). Example:

```cpp
ibDatabaseResultSet* rs = db_query->RunQueryWithResults(
    wxT("SELECT * FROM %s WHERE guid = ?"), table_name);
```

Each driver folder contains an `engine/` subdirectory with the vendored native client library.

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

`ibValueForm` owns the complete control tree for one open form. The static entry point `ibBackendValueForm::CreateNewForm()` instantiates the form; the higher-level orchestrator is `ibValueMetaObjectFormBase::CreateAndBuildForm()` (`metaCollection/metaFormObject.h`), which resolves the form descriptor from metadata and builds the control tree (see [Form Open](#form-open)).

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
