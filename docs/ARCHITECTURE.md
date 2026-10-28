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
12. [Localization (UI gettext)](#localization-ui-gettext)

---

## System Overview

```
  TOOLS                        THIN CLIENTS                              SERVERS
  launcher · codeRunner        designer-thin · enterprise-thin           appserver
            │                            │                                       │
            │                            ▼                                       ▼
            │                  ┌───────────────────┐  client protocol  ┌───────────────────┐
            │                  │  frmclient.dll    │  WS / POST,       │  frmserver.dll    │
            │                  │  protocol.dll     │◄─────────────────►│  fileserver.dll — │
            │                  └───────────────────┘  OES-RPC          │  a file base, in  │
            │                                                          │  the client       │
            │                                                          └─────────┬─────────┘
            ▼                                                                    ▼
  ┌────────────────────────────────────────────────────────────────────────────────────┐
  │  backend.dll — ibApplicationHost → its bases · ibMetaDataConfiguration ·           │
  │  ibCompileCode / ibProcUnit · sessions · jobs · MCP · ibDatabaseLayer (+4 drivers) │
  │  · ibDebuggerServer                                                                │
  └──────────────────────────────────────────┬─────────────────────────────────────────┘
                                             ▼
        Database tier — Firebird, PostgreSQL (production) · SQLite (tests, logging) · ODBC

  appcluster  ── appserver --worker processes, one or more per base; holds the clients' port and carries their sockets on
  webserver   ── browsers to application servers' bases (relayed) and to file bases (fileserver, in-process)
  core.dll    ── under every library above, the client's as well as the engine's;
                 appcluster and appcluster-tray link it alone
```

Two roads lead into one engine, and two tools sit on it directly (`launcher` and `codeRunner` link `backend` and wx,
with no form). A **thin client** draws only: its forms live in `frmserver.dll` beside the engine, and the two
talk the [client protocol](public/client-protocol.md) — across the network to an application server, or inside the
client's own process to a file base (`fileserver.dll`, loaded for it). The **servers** hold bases for clients who come
over that protocol. Under all of them sits `core.dll` — see [Core Layer](#core-layer-coredll).

---

## Layer Descriptions

### Application Layer (executables)

Each executable provides a `wxApp` (a console one, `wxAppConsole`) subclass. What it links decides what it is: the
tools and the servers link `backend` and pick the run mode and the kind of their own session; the thin ones link
`frmclient` + `protocol` and hold no base — unless they open a file base, when `fileserver`, loaded for it, brings the
engine into their process; the cluster and its tray link `core` alone:

| Executable | Run Mode (`ibRunMode`) | Own session (`ibSessionKind`) | Purpose |
|---|---|---|---|
| `launcher.exe` | `eLAUNCHER_MODE` | — | Connection chooser; creates/selects database |
| `designer-thin.exe` | its host's | `ThinDesigner` (on the server) | The designer as a thin client: an application server's base by its address (`oes://host:port/base`, `oess://` over TLS), or a file base opened in its own process |
| `enterprise-thin.exe` | its host's | `ThinClient` (on the server) | The runtime as a thin client, by the same two roads; holds no base of its own and runs no code |
| `appserver.exe` | `eSERVER_MODE` | `Service` | The application server: serves one base of its server folder for the cluster, headless; protocol clients are `ThinClient` / `ThinDesigner` |
| `appcluster.exe` | — | — | The application cluster: a console program — or a service of the system — on `core.dll` alone; `appserver --worker=<base> --route=<n>` for each base of its folder, as many as its `Processes`, started again when one falls; holds the clients' port and carries each socket on to the server its session lives in |
| `appcluster-tray.exe` | — | — | The cluster's tray, on `core.dll` alone: shows the cluster's servers and steers them over its control socket; starts the cluster — its service, or the program — when none runs |
| `webserver.exe` | `eFILE_MODE` (the file bases it opens) | — | The web gateway: browsers over HTTP(S) and WebSocket to file bases it opens and to application servers' bases |
| `codeRunner.exe` | `eSANDBOX_MODE` | — | Executes a single script module |

> The run mode says only how the process holds the base — serving it (`eSERVER_MODE`) or as a file base for the one who started it (`eFILE_MODE`). What a session works in is its kind: either mode hosts designers, runtimes and jobs alike, and `DesignerMode()` / `EnterpriseMode()` are answered by the current session's kind. The base loads and runs its configuration as it opens, before any session is let in; every session takes a reference to it, and a designer's session works in an editable configuration of its own.

### Core Layer (`core.dll`)

`src/engine/core/` — what the engine and its clients mean by the same words. Under `backend` and `frmclient`; of wx the
base only, and privately Asio, llhttp, LodePNG, NanoSVG and Mbed TLS; reads no base, runs no script, draws no window. No metadata, no
factories, no checks: a schema stores ids and structure.

| Unit | Holds |
|---|---|
| `coreApi.h` | what every header of the core stands on, with no include of its own: `IB_EXPORT` / `IB_IMPORT` (every module's API macro is spelled with them), `CORE_API` / `CORE_EXPORTS`, `IB_FORCEINLINE` / `IB_NOINLINE`, the platform — `__IBMSW__` / `__IBOSX__` / `__IBGTK__` — and the string's character and literal, `ibChar` (`char16_t` on every OS) / `ibT("…")` (`u"…"`) |
| `core.h` | `coreApi.h`, and id typedefs (`ibRoleID`, `ibMetaID`, `ibSourceId`, `ibFormID`, `ibActionID`, `ibPictureID`, `ibVersionID`), `ibValueTypes`, `ibDateFractions`, `ibCtorObjectType`, the primitives' class ids (`g_valueUndefinedCLSID`, `g_valueBooleanCLSID` … `g_valueNullCLSID`) and `GetIDByVT`, `COMPONENT_TYPE_*`, the packed value node's field names `kValueFieldClsid` "t" / `kValueFieldData` "v" |
| `variant`, `variant_ptr.h` | `ibVariant` — THE value of the system: a flag, a number, a date, a text, or a counted reference to an object derived from it; its lifecycle, node serialization, the compare family (`CompareValueEQ` …). No base class, no factory; copying a value (`CloneValue`) and looking one up by text (`FindValue`) are the runtime's, not the variant's. A literal, a property's value, a cell, a sheet parameter are each one; the engine's `ibValue` (+ runtime, factory) and the thin client's (+ a small factory) derive from it. `ibVariantPtr<T>` — a counted, typed holder |
| `clsid.h`, `fstring`, `fnumber`, `fdatetime`, `fboolean`, `guid`, `stringUtils.h` | `ibClassID`, `ibString` (+ pool, `ibEmptyString`), `ibNumber`, `ibDateTime` (a wall-clock reading, no wx; free-form text read by `fdatetimeParse.cpp` — [date-model.md](public/date-model.md)), `ibBoolean`, `ibGuid`. The core's text is `ibString` throughout; `wxString` meets it only in `fstring.h`, the conversion both ways |
| `compiler/` | the script language's words and its text: `codeDef.h` (opcodes, keywords), `ibTranslateCode` (the lexer — `s_listKeyWord`; its constant is an `ibVariant`), `ibParseCode` (what a module text declares, read without compiling it — the procedures window, autocomplete, the module outline), `ibFoldCode` (where a module's text folds — both code editors'), `ibLinqBlock` (LINQ's block syntax as parts). The compiler and the interpreter are the backend's |
| `formatString` | `ibFormatString` — the `Format` string as a value: `Parse` / `Render`, `Apply(const ibVariant&)`, `FromTypeDesc` (the format a type description gives, over either side's description) |
| `memory/` | `ibMemoryBuffer` (bytes: shared until written, the string's shape, no wx), `ibMemoryPool` (the per-thread block pool of strings and buffers), `ibWriterMemory` / `ibReaderMemory` (chunked byte streams over a buffer), lz block compression, `ibZlibDeflate` / `ibZlibInflate` (zlib's form — a stored blob's). A read, seek or advance past the end of a block, or a zero-terminated string with no terminator inside it, is refused (`ibCoreException`) in every build |
| `serialize/` | `ibDataNode` / `ibDataValue`, binary provider, `ibJsonProvider`, JSON text. A type is named only by the injected resolver (`SetTypeResolver`). The JSON reader refuses nesting deeper than 256 objects and arrays (`kMaxNesting`, the root counted) |
| `exception` | `ibCoreException : std::exception` — the base of every refusal (see [Exception taxonomy](#exception-taxonomy)) |
| `description/` | the schemas both sides read and write the same way: `ibSourceDescription` (a binding path as hops), `ibCommandDescription` (a command path as hops), `ibCalcScheduleDescription`, `ibChoiceLinkDescription`, `ibCompositionDescription` (what a list and a report are: filter, sort, grouping, structure, parameters, variants — every value in it packed as a node, restored by the side's `backend_composition.h` against its configuration, which also holds the check `ibValidateSettings`), `ibHomePageDescription`, `ibJobScheduleDescription` (when a job is due, as fields and masks — what they mean, and whether they are well formed, is `ibJobScheduleRules`, `job/`), `ibPictureDescription` (an engine picture's id, a configuration's Guid or the image bytes — whether the engine has that id is asked by whoever reads it), `ibSpreadsheetDescription`, `ibStandardCommandDescription` (the action a tool runs — a system id or a handler's name), `ibTypeDescription` / `ibMetaDescription` (a type as clsids + qualifiers; a binding as metaIDs — the ids alone, no names: an id that names nothing is dropped by whoever reads it). A schema holds ids and nodes, never a value |
| `graphics/` | a picture, a font, a colour, a point, a size, a rect — as DATA, for both sides and for a page: `ibPicture` (a PNG's bytes; its size, scaled and read from a master's Base-64 by LodePNG, drawn from SVG and said as SVG by NanoSVG), `ibFont` (face, style, weight, fractional size, family, underlined, struck through), `ibColour` (RGBA or none), `ibPoint` / `ibSize` / `ibRect` (wx's, byte for byte). The conversion to wx is each window's (the thin client: `ibProtocolImage`; codeRunner: `ToBitmap` / `ToIcon` in its `win/picture.h`); the backend and frmserver have no wx image. The backend and frmserver hold fonts, colours, points and sizes only as these (property cells, form defaults, sheet cells); `typeConv` (`Font`/`Colour`/`Point`/`Size` ⇄ wx) lives in `frmclient/win/typeconv.h` |
| `tabular/` | `tabularModelView` — the table's model view, ONE for the server's tables and the thin client's: `ibDataViewModel` (values, hierarchy, the paged fetch, the notifier, `SubmitFetchAsync` — a thread of the model's own, bare: no session, no base), `ibDataViewItem` / `ibDataViewObject`, `ibDataViewItemAttr` (a row's look — colours as `ibColour`; the font it lays over a view's is the side's, `ibEffectiveFont` beside each data view), `ibFetchDirection`, `s_constIgnoreParent`. The server's models bind the fetch thread to the asker's base in `ibDataViewModelProvider` (`backend/tabularModel.h`) |
| `query/` | the query language's words: `ibQueryKeyword` + the keyword and scalar-call tables (`queryKeywords`), `ibQueryDimUnfold` (`queryUnfold.h`), `ibBalanceRole` (`queryBalanceRole.h`), the period units (`queryPeriodUnits.h`). `ibQueryAggregatesFor` / `ibQueryAggregateCallsFor` — the aggregates a type admits: the lowering refuses by it, every window offers by it. `ibQueryConstructorField` — a field a source or a text offers (name, caption, type, picture, available). `ibQueryAstExpr` declared. The lexer stays in the backend and matches against them |
| `job/` | `ibJobScheduleRules` — what a schedule means at a moment: allowed, the next one, its sentence, well formed |
| `spreadsheet/`, `composition/` | `ibSpreadsheetObject` — the sheet as a model, ONE class for the engine and the thin client, each side adding its listener and its values; `ibSheetFormat` — a foreign table file read into it and written out of it; `ibCompositionTheme` — the palette a report is painted in |
| `net/` | the engine's HTTP: `ibHttpServer` — HTTP/1.1 and WebSocket over plain TCP or TLS, asynchronous (Asio; llhttp parses; Mbed TLS), a waiting socket a buffer, not a thread; it refuses oversized and slow requests and foreign origins by itself, and plain HTTP off this machine unless the owner allows it (`m_clearBeyondMachine` — the debugger's alone; `ibIsLoopback` says what is this machine). `ibHttpClient` / `ibWebSocketClient` / `ibHttpPost` — the connections out (the web server's, the thin client's, the debugger's, a script's HTTP), over TLS when handed a client's trust (`ibTlsClientContext`, made once for all of a caller's sockets); `ibWebSocketRelay` — a socket carried on to a server (the web server's, the cluster's); `ibIoThreads` — the I/O threads of a process that listens on nothing |
| `server/` | `ibServerConfig` — the server folder (`server.conf`, `server.key`, `cluster.token`), read by the application server and the cluster; `clusterControl.h` — the words of the cluster's control socket (states, fields, verbs) |
| `crypto/`, `tls/` | `ibFieldCipher` — AES-256-GCM over Mbed TLS (the server's secrets, the protected fields); the session's bearer token (CTR-DRBG bytes, SHA-256 kept); the Mbed TLS configuration (threading on: two handshakes at once) |
| `fiber/` | `ibFiber` — a stackful fiber pinned to its thread: a script that waits parks on one (`workerPoolHeadless`); `ibFiberLocals` — the values a parked fiber must not leak into the next session on the thread |
| `syntaxHelper/` | the syntax helper's corpus — `ibHelpService` (the process's, reloaded atomically), `ibHelpCorpus`, its loader and resolver |
| `startProcess`, `launchTicket` | `ibStartProcess` — a program started from any thread, not waited for; a child's passwords on its stdin, never on its command line; `ibStopProcess` / `ibKillProcess` / `ibProcessExists`, `ibSetEnv`, `ibHostName` |
| `dynamicLibrary` | `ibDynamicLibrary` — a library loaded at run time by path, asked for its functions; `CanonicalizeName` / `Extension` — the file a library's name is on this system. The database clients, the protocol's fileserver and the plugin manager load through it |
| `pictureRegistry`, `picturePredefined`, `fileKind`, `typeName`, `utils/base64.h` | the pictures a process registered, by number, as data; the file kinds the platform writes, in one table; a class's name as the source writes it (`ibTypeInfoName`); base64 straight into and out of `ibString` |
| `logger/`, `mcp/`, `settings/` | the journal's question and row (`ibLogFilter`, `ibLogRow`; the reader is the server's), `ibMcpSettings`, the settings shelf's words (`ibSettingsCategory`, `ibComposerSettingsEntry`; the storage is the server's) |
| `roleCompositionMode.h`, `selectorDataType.h`, `propertyChoiceMode.h` | one vocabulary each: how a role combines, what shape a type is picked in, how many of a property's choices are held |
| `localization`, `localization/` | `ibLocalization` — template (`ibLocalizationEntryArray` or `en = '…'; ru = '…';`) + a language in, that language's text out; missing language → the first written. The owner passes the language. `ibDictionary` — the engine's own gettext: `_()`, `ibPLURAL`, `ibGETTEXT_IN_CONTEXT`, `ibGetTranslation`, the language the thread's base's ([translations.md](public/translations.md)) |
| `diagnostics/` | the technology journal (`ibTechJournal`, `ibJournal*`) — ONE per process, the engine's, the protocol's and the client's lines in one file; `ibCrashGuard` (idempotent `Install`), leak tracker, `debugTrace.h`; the header-only app bases `oesApp.h` / `oesConsole.h` ([Entry-point helpers](#entry-point-helpers)) |
| `build` | `GetBuildId` / `GetBuildStamp`, the program version (`OES_VERSION_MAJOR` / `_MINOR` / `_RELEASE` — every version resource reads them too) |
| `version.rc2`, `program.manifest` | what every program's and library's resources share: the version block (company, product, copyright, the version from `build.h`) and the manifest merged into every executable's (supported OS, long paths) |
| `programFolder` | `ibProgramFolder` — where the files a program shares are looked for (`backend.conf`, `lang/`, `help/`, `plugins/`, `_fb/`, `web/`, `server/`, the other programs): the executable's folder, or on macOS the folder that holds the `.app`. `ibDiagnosticFolder` — where `journal/`, `crashdumps/` and `oes-debug.log` go: the program folder, except for a program running from a macOS bundle, which writes under `~/Library/Logs/OES` |
| `core.cpp` | `DllMain`: `DLL_THREAD_DETACH` drains the `ibString` pool for every process that holds one, engine or not |

- Links: backend → core (PUBLIC); protocol → core; frmclient → core + protocol; enterprise-thin, designer-thin → frmclient +
  protocol + core; webserver → protocol + core; appcluster, appcluster-tray → core alone.
- Not in core: `typeconv` (fonts/colours/GDI, dates ⇄ `wxDateTime`; in the GUI sides only, not in backend since 2026-10-13), frmclient's `dataProtocol` seam, the value's storage (each side's own),
  the data view controls (frmclient's).
- Every executable and library carries a version resource: its `<name>.rc` beside the project names the file and its
  kind and includes `core/version.rc2`; a program with a window adds its icon. Every executable merges
  `core/program.manifest`; wx adds the common-controls one by itself.

### Backend Layer (`backend.dll`)

The backend is the core engine. It is self-contained — no GUI dependencies. Key objects:

- **`ibApplicationHost`** (`src/engine/backend/appHost.h`) — the PROCESS: the bases it holds, and what belongs to the process rather than to any base — the plugins, the platform locale and the syntax-helper corpus, the one worker pool, the limits read from `backend.conf`. See [Processes and bases](#processes-and-bases).
- **`ibApplicationInstance`** (`src/engine/backend/appData.h`) — ONE BASE: its connection pool, lock manager, session registry, job manager, MCP server, settings storage, logger and metadata. A process holds one (a thin client's file base, the web host, the tests) or several (the application server). Reached through the `appData` macro, which answers the base of the session the thread works for — there is no global current base. Per-session state (user info, ProcUnits, frame) lives on `ibSession`; sys_user on `ibUserInfo`; the sys_session snapshot on `ibSessionSnapshot` produced by `ibSessionRegistry`.
- **`ibMetaDataConfiguration`** (`src/engine/backend/metadataConfiguration.h`) — loads, saves, and manages the metadata tree (all business objects). Accessed via `activeMetaData` — the calling session's own configuration. Stores compile cache (compiled bytecode) per module descriptor; runtime instances live in sessions.
- **`ibSession` / `ibSessionRegistry`** (`src/engine/backend/session/`) — per-session state and the base's session manager (one registry per base). The session is reachable via `ibSession::Current()`: the calling thread's own binding, else the base's fallback — the process's own session in that base (never a job's or a web tab's). `ibSessionScope` and `ibSessionThreadBinding` are the RAII helpers that bind a session to the calling thread. See [Sessions and Runtime Ownership](#sessions-and-runtime-ownership).
- **`ibDebuggerServer`** (`src/engine/backend/debugger/debugServer.h`) — TCP server that accepts designer connections and relays debugger events.

### Thin client layer (`protocol.dll`, `frmserver.dll`, `frmclient.dll`, `fileserver.dll`)

The desktop front (`frontend.dll`, the thick `designer.exe` / `enterprise.exe`, the old web `wfrontend.dll`) was removed
on 2026-10-27; it is kept on the branch `oes/thick_client`. It carried two functions at once — the forms' logic and
their drawing. The thin client splits them, with the
[client protocol](public/client-protocol.md) between: **`frmserver`** holds the forms on the server and answers clients;
**`frmclient`** draws them (main window, document/view, controls, editors) for **`enterprise-thin.exe`** and
**`designer-thin.exe`**, on `frmclient/uikit` — the interface kit; **`protocol`** is
the contract both keep (`protocol.h`, no dependencies — `frmserver` includes it only) and the talking side's library
(`frmclient` and the web server link it). **`fileserver`** puts `backend` + `frmserver` into the client's process for a
file base. `frmclient` does not link `backend`; it links `core` (node and JSON, string, number, date, guid, exception,
localization — the same classes the engine uses). The schemas (type, settings, sheet, composition), the table's model view
and the words both sides speak (a query's fields and aggregates, a schedule's rules, the journal's question, the settings
shelf) are core's, one copy for both sides; `frmclient/backend/` keeps reduced copies of the rest — same names and classes, only
what the client's windows use (the type factories, a value fitted to its type, a composition's values restored) —
meeting the wire at `frmclient/backend/serialize/dataProtocol.h`; its `backend_core.h` / `backend.h` /
`backend_exception.h` just include core. Chain: core → backend → frmserver ⇄ protocol ⇄ frmclient → enterprise-thin,
designer-thin. The thin designer keeps beside it what only a designer's windows need (`designer-thin/backend/`: its
debugger client among them). See [thin-client.md](public/thin-client.md).

Form objects:
- **`ibValueForm`** — an open form, on each side of the wire: the server's (`frmserver/visualView/ctrl/form.h`) holds the control tree and answers user events; the client's (`frmclient/visualView/ctrl/form.h`) draws it.
- **`ibVisualHost` / `ibVisualHostClient`** — who holds a form and takes its controls through their life (`frmserver/visualView/visualHost.h`, no window: the frame it draws), and the client's view of a tab drawn from that frame (`frmclient/visualView/visualHostClient.h`).
- **Doc-view frames** — backend-facing interface `ibBackendDocFrame` (`backend/backend_mainFrame.h`); the server's implementation is `ibClientFrame` (`frmserver/client/clientFrame.h`: the frame of one client, held on the server — a script's `OpenForm`, `Message`, `Question` land there). The client's window is `ibFrontendMainFrame` (`frmclient/mainFrame/mainFrame.h`); codeRunner has `ibFrameCodeRunner`. The frame is owned by the `ibSession` that created it (no process-level singleton on the backend side); backend code reaches it through `ibSession::Current()->GetFrame()` or `ibSession::CurrentFrame()`.
- **`ibCodeEditor`** (`frmclient/win/editor/codeEditor/`) — the Scintilla-based script editor of the thin designer. Highlighter, fold parser (core's `ibFoldCode`), auto-indent on Enter, format / increase / decrease indent, comment add/remove, Ctrl-Space autocomplete, GotoLine + ProceduresAndFunctions dialogs all live here. Debugger integration is a designer-only concern, kept out of the base via virtual hooks (`IsDebuggerEnterLoop`, `OnEditDebugPoint`, `OnPatchModule`, `OnEvaluateToolTip`, `RefreshBreakpointMarkers`, …); `ibCodeEditorDesigner` (`designer-thin/win/editor/codeEditor/codeEditorDesigner.h`) overrides them. `codeRunner` carries its own copy of the old desktop editor (`codeRunner/win/editor/codeEditor/`, with its `artProvider/`, dialogs and `win/picture.*`), with no document behind it.

---

## Application Bootstrap and Ownership

### Processes and bases

A process holds **one base or several**. `ibApplicationHost` (`backend/appHost.h`) is the process: it owns its bases (`std::unique_ptr<ibApplicationInstance>`, in the order they were opened) and whatever is the process's rather than a base's. `ibApplicationInstance` (`backend/appData.h`) is one base and owns that base's subsystems. A thin client's file base (`fileserver`), the web hosts and the tests hold one base; the application server (`appserver.exe`) holds every base of its server folder — or, started by the cluster (`appcluster.exe`), one: a base per process, so a fault in one does not take the others. Design and decisions: `docs/private/multi-base-process.md`.

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
               system tables + migrations (the configuration's too), locale, logger, platform jobs,
               then the configuration — loaded and run before any session is let in — the session registry
               and the job manager.
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

`m_logger` is created by `Open` (`CreateLogger`) after the tables exist. `m_activeMetaData` — the application's `ibMetaDataConfiguration` — is made, loaded and run by `Open` (`CreateActiveMetaData(applicationInstance, flags)`, the flags from the request: the debug server), before any session is let in; a configuration that does not come up refuses the base. Launcher and codeRunner have none. It is held for the sessions to come: a session takes its own reference when it is let in and lets it go when it leaves. A designer's session (`Designer`, `ThinDesigner`) takes none: it makes the configuration it edits, an `ibMetaDataConfigurationStorage`, and holds it alone (`ibSession::AcquireMetaData`). Its apply replaces the active one with the configuration it published (`ReplaceActiveMetaData`); sessions already working finish on the old one, which closes with its last holder. The job manager starts with the base, at the end of `Open` — jobs need no user. Every metadata — a configuration, an external report or data processor, a configuration file — is made by `ibMetaData::MakeShared` and held by `shared_ptr` by whoever works in it. The plugins and the syntax-helper corpus (constructed in `InitLocale()` once the locale is settled — see `docs/private/syntax-helper-design.md`) are the process's.

### Closing a base (`ibApplicationHost::Close` → `ibApplicationInstance::Close`)

Two steps. The base is first **closed while it is still listed** — its sessions, removing their rows as the registry stops, ask `ibApplicationHost::HasRegistry` whether their registry still stands, and the answer must be yes — and only then taken out of the set and freed, an empty shell:

```
ibApplicationInstance::Close() {
    m_jobManager->Stop();               // nothing of ours still runs against what goes next
    m_mcpServer->Stop();
    m_sessionRegistry->Stop();          // drains this base's work off the shared pool, DELETEs sys_session
    m_activeMetaData->OnDestroy();
    release(m_activeMetaData);          // the base's reference — its last holder closes it, while the pool stands
    m_connectionPool->Shutdown();
    // then every field is released in reverse declaration order, each emptied under the
    // process's lock first, so a question asked after it is gone reads "none"
}
```

`ibApplicationHost::CloseAll` closes every base newest first, then the process: its worker pool is joined, then the plugins unload. `Close()` is idempotent — the destructor runs it again over the shell. If a new subsystem joins, add its hook to `Close()` and append the field in declaration order so it dies before its dependencies.

### Token pattern for subsystem construction

Every owned subsystem's ctor takes `ib::AppDataCtorToken` (`backend/appDataCtorToken.h`). Its ctor is private and only `ibApplicationInstance`, `ibApplicationHost` and `ibSession` are friends, so external code that tries `new ibSessionRegistry(…)` gets a compile error. The token also says **whose**: a base mints `AppDataCtorToken{ this }` and the subsystem reads its owner off it (`GetApplicationInstance()`); the host mints a null one for the process's share (the plugins, the corpus); a designer's session mints one with its base for the configuration it edits.

Why a token instead of a friend declaration on each subsystem header? Friends scattered across seven headers mean refactoring the owner ripples into every owned class. One token type keeps the "who can build subsystems" decision in one place; subsystem headers declare their ctors public.

Unit tests build subsystems directly (no full appData) — the test CMake target defines `OES_TESTING`, which opens the token's default ctor inside test TUs only. Production builds (sln / CMake non-test) leave it undefined and the gate stays closed.

### The application server (`appserver.exe`, `src/engine/appserver/`)

A headless process that serves **one base** of its **server folder** for the application cluster, which starts it (`--worker=<base>`, and `--route=<n>` — which of the base's servers it is); nothing else starts one. The folder is `--dir`, by default `server` in the program folder — beside the executable, or beside the `.app` inside a macOS bundle:

```
<server folder>/
  server.conf      Host, Port — the cluster's; one group per base: Kind=firebird|postgresql, Id, Path or
                   Server/Port/Database/User/Password, IbUser/IbPassword, McpPort, Processes (its servers side
                   by side, 1 when absent) — the Id and McpPort are assigned by the cluster at its first start
                   and written back
  server.key       this installation's key, made at the first start
  <Id>/            one folder per base: its journal (oeslog) and infobase.conf; a Firebird base's sys.fdb too
```

- **Settings in three layers**, key by key: built-in values → `backend.conf` (the process: `Locale`, `Workers`, `Bases`, the default `Connections`) → the base's `infobase.conf` (`Connections` to ITS DBMS). `Workers` and `Bases` are a maximum, 0 = no limit (the pool grows as sessions bring work); `Connections` 0 = the default; a value that cannot be used is said in the journal (`ibApplicationHost::ReadCount`).
- **Secrets** are sealed in `server.conf` with AES-256-GCM (`ibFieldCipher`, key in `server.key`); plain text is refused. `appserver --set-password=<base>/<Password|IbPassword>` reads one from the keyboard and seals it.
- **Opening:** its base through `CreateFile/ServerAppDataEnv` in `eSERVER_MODE`, then a session of kind `Service` logged into it. A base that does not open or refuses the login is said, and the server ends: the cluster shows that line as the cause.
- **Who may come in** (`ibServiceExclusivePolicy`, asked at open by `CanOpen` and at every session by `CanAdd`): application servers (`eSERVER_MODE`) share a base with application servers, file bases with file bases — a file-base process (a thin client's file base, the web host) is refused while a server serves the base, and a server while a file base uses it. The row's run mode decides, whatever its kind: a server's thin clients are the server's. What several servers on one base share already lives in it: `sys_session`, `sys_lock`, a job's claim and its clock in `sys_job`.
- **Journal:** every line about a base carries its name — `(trade1) source …`. The console shows the server's own lines and every warning and error; the running commentary stays in the file.
- **Its port** (`clientListener`): `WS /<base>/client` and `POST /<base>/client` over the core's HTTP (`core/net`, Asio) — a waiting socket costs a buffer, not a thread. On `127.0.0.1` in the clear, the first free one of 7500–7599 (`ibPort::Workers`, `core/net/ports.h`), said on stdout (`serving <port>`): the clients come in by the cluster's port, TLS there, and the cluster carries them on. `ClientConnections` (backend.conf, 1000) at once; a socket that has not logged in goes 15 s after it opened.
- **Its base's assistant access** (MCP), when the base's settings switch it on: on `server.conf`'s `McpPort`, given by the cluster from 7600–7699 (`ibPort::BaseAssistant`), so the bases of one cluster never meet on one port; on and its key are the base's settings. Of a base's several servers, the first (`--route=1`) holds it.
- **Its sessions' tokens end in its route** — `<hex>.<route>` (`ibSessionToken::WithRoute`), the login's Token and the debugger's Channel key alike: only the hex is the bearer and only it is hashed; the route is how the cluster carries a client that comes back to this server.
- **Stop:** `stop` on stdin, stdin closing (the cluster gone), Ctrl+C, SIGTERM or the console closing raises a flag; the main thread ends the server's session, then closes the base. Started again after a fall it is given `--again`: its first error takes no dump — the first start took one, and a dump at every start would fill the disk while a DBMS stays down.
- **Not yet:** creating a new base (only the designer creates the system tables). It is not a service of the system itself — the cluster is, and runs it.

### The application cluster (`appcluster.exe`, `src/engine/appcluster/`)

A console program over the same server folder that links `core.dll` alone (of wx, the base). A base's fault stays in the base's process:

- reads `server.conf` (`ibServerConfig`, `core/server/`), writes each new base's `Id` and `McpPort`, and starts `appserver --worker=<base> --route=<n>` for each base, as many as its `Processes` (1 when absent) — children whose pipes it holds (`ibWorkerPipe`). A base's first server starts alone; its others once it serves. A tray's start or restart of a base's server reads its `Processes` again: servers are added, or the last ones stopped;
- holds the config's port — `Port`, 7373 (`ibPort::Clients`) unless another is written; taken, it is refused, never moved off — and carries each client's WebSocket `/<base>/client` on to a server of its base (`ibClusterRoute` chooses, `ibWebSocketRelay` carries, `core/net/`): by the first message, the server its token's route names — a login by Token, a debugger's Listen by its Channel — while that one serves, else the serving one carrying the fewest sockets; clients do not change. TLS ends there: the port goes through the listener's door with `backend.conf`'s certificate, the servers listen on `127.0.0.1` in the clear;
- a server that falls is started again (after 1 s, or later and later after short lives); its sessions are lost with it, the other bases and its base's other servers do not notice;
- **a service of the system**, as Apache is: `appcluster --install` (an administrator's) installs it to start with Windows, `--service` is how the service manager runs it, `--uninstall` removes it; under systemd a unit runs `appcluster --service` and SIGTERM stops it. Ctrl+C, SIGTERM and the manager's stop all stop it the same way: every server first;
- **the control socket** `/cluster` on the same port (`core/server/clusterControl.h`): from this machine alone (`ibIsLoopback`) and with the token written into the server folder at each start (`cluster.token`, removed as the cluster goes). The cluster sends the list of its servers as the socket opens and on every change; a verb comes back — start, stop, restart one server, named by its base and route, or stop the cluster.

**The tray** (`appcluster-tray.exe`, `src/engine/appcluster-tray/`) is a program of its own and no part of the cluster: it shows the servers of each base ("trade #2" when there are several) — state, process, falls, and why it fell (the last error it said) — with Start / Stop / Restart and Stop the cluster, over the control socket; a fallen server's two read "Start now" and "Do not start again". Not running, the cluster is started from it: its service if one is installed, the program beside the tray otherwise; a cluster that ends with an error is said in a message box. A tray closed leaves the cluster serving.

- **Not yet:** the designer's own process, the settings in the tray, the client's address past the relay (servers see `127.0.0.1`), the control socket when `Host` names an address off this machine (the tray reaches `127.0.0.1` only).

### The web server (`webserver.exe`, `src/engine/webserver/`)

A transport between browsers and bases: it holds no forms and runs no code of its own. Links `protocol` and `core`, not `backend`.

- **Publications** — `web.conf` in the web folder (`--dir`, by default `web` beside the program), or one named on the command line (`--file` / `--server`). One group per publication: `Kind=firebird` + `Directory` — a file base opened in this process through `fileserver` and held as a server holds its bases (a connection per browser); `Kind=server` + `Address=oes://host:port/base` — a base of an application server, relayed (`oess://` over TLS, trusting the system's store or `Trusted=`'s PEM file, made once per publication).
- **Routes:** `WS /<publication>/client` — a connection, each binary message one OES-RPC packet of the [client protocol](public/client-protocol.md), the base's notifications pushed; `POST /<publication>/client` — one request; the site's files from `Root` (`www`) at `/`, with ETag/304.
- **The site** is [the browser client](public/browser-client.md) (`webserver/www/`): native ES modules with no build, the form drawn from the frame as the thin client draws it; a release serves it from its pack `web/www.wpk`.
- **Transport:** the core's (`core/net`) — Asio (standalone) + llhttp, TLS by Mbed TLS. HTTP serves loopback only; on any other interface plain HTTP only sends the browser on to HTTPS. Answers of 1 KB and more are deflated on `?compress=deflate`; a page of another site (`Origin` not in `Origins`) is refused.
- **Stop:** Ctrl+C, Ctrl+Break or the console closing — the listeners stop, then every base it opened closes.
- **Not yet:** web services, running as a service.

### Entry-point helpers

Two header-only helpers in `core/diagnostics/`, beside the guard they wire, cover the boilerplate a binary needs at startup. Header-only = the core itself never compiles them (it has wx base only); both helpers only call `CORE_API ibCrashGuard::*` plus wx primitives the binary already links. The programs that do not include them — `appcluster`, `appcluster-tray` — derive from `wxApp` / `wxAppConsole` and call `ibCrashGuard::Install` in their own `OnInit`.

| Binary type | Helper | What it wires |
|---|---|---|
| GUI (`codeRunner` / `launcher` / `enterprise-thin` / `designer-thin`) | `ibWxApp` base class (`oesApp.h`) | `wxApp::OnInit` → `ibCrashGuard::Install` + `DoOnInit()`; `OnRun` → try/catch around `DoOnRun()`; 3 exception overrides (main-loop / unhandled / fatal) — the refusal it says and goes on is `ibCoreException`, nothing above the core. Subclass overrides only `GetExeName()` + optional `DoOnRun()`. |
| Console (`appserver` / `webserver`) | `ibOesConsoleBoot` RAII (`oesConsole.h`) | Single object that holds a `wxInitializer` and calls `ibCrashGuard::Install`. `IsOk()` checks the wx init result. |

### Crash plumbing layers

```
  core/diagnostics/crashGuard.{h,cpp}     ─── CORE_API. Headless:
                                              SEH filter / signal handlers /
                                              minidump writer / std::set_terminate /
                                              <exe>_terminate.log + <exe>_startup.log /
                                              MessageBoxW (Win) | fprintf+stderr (POSIX)
                                              No wx UI, no main loop required.

  core/diagnostics/oesApp.h               ─── Header-only. wxApp pipeline:
                                              wxDebugReportPreviewStd, wxMessageBox,
                                              wxLogError, wxTheApp->CallAfter. Delegates
                                              logging to crashGuard.

  core/diagnostics/oesConsole.h           ─── Header-only. RAII boot helper for
                                              console binaries. Wraps wxInitializer +
                                              crashGuard::Install.
```

### Exception taxonomy

```
ibCoreException : std::exception          ─── core/exception.h. The base of every
  │                                            refusal; thrown by the core itself
  │                                            (read past a block's end, wrong value
  │                                            kind, malformed JSON). frmclient's
  │                                            ibBackendException is an alias of it.
  │
  ibBackendException                      ─── engine base; per-thread error chain
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

A handler that only tells a person what went wrong catches `ibCoreException`. The script runtime's `Try` meets a core
refusal as the engine's own (re-raised as `ibBackendCoreException`, then the same `Try` / `ProcessError` path);
`ibWxApp`'s main-loop handler shows it and continues.

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
- **Inside business logic**: never swallow — log via the per-thread chain, rethrow, or surface via `ibCrashGuard::LogStartupError` / `ibWxApp::OnExceptionInMainLoop`.

---

## Module Descriptions

### `src/engine/backend/compiler/`

| File | Class | Role |
|---|---|---|
| `core/compiler/translateCode.h/cpp` | `ibTranslateCode` | Lexer: tokenises source text into `ibLexem` stream — the core's, so the thin side reads a text by the same rules |
| `compileCode.h/cpp` | `ibCompileCode` | Parser and code generator: consumes lexemes, emits `ibByteCode` |
| `byteCode.h` | `ibByteCode`, `ibByteUnit` | Bytecode container: array of `ibByteUnit` instructions |
| `byteCodeAOT.cpp` | `ibByteCode::SerializeAOT/DeserializeAOT` | Binary persistence for the AOT cache (`sys_bytecode_cache.bc_blob`); host-endian linear format with magic `'PBC1'` + format version; the history of every version bump |
| `byteCodeFormat.h` | `kAOTFormatVersion` | The format version (36): written into every blob and into the cache key. Bumped when an opcode is added, removed or renumbered, or changes what it does |
| `cache/byteCodeCache.h/cpp` | `ibByteCodeCache` | The AOT cache's rows: `Load` / `Save` / `Invalidate` by descriptor, under `CacheKey` |
| `engineFingerprint.cmake` | — | Build-time script (run by CMake and by `backend.vcxproj`): a SHA-256 of every file under `compiler/`, `system/` and `../core/compiler/` (the translator), written to the generated `engineFingerprintBuild.h` as `kEngineFingerprint` — only when it changes |
| `procUnit.h/cpp` | `ibProcUnit` | Interpreter: executes `ibByteCode` against a variable stack |
| `procContext.h/cpp` | `ibRunContext` | Execution context: local variable frame, call stack |
| `value.h/cpp` | `ibValue` | The runtime value: `ibVariant` (core) + members, iterators, script dispatch, the class factory; `CloneValue` packs and creates through the factory — tag enum `ibValueTypes` in `core/core.h`: `TYPE_EMPTY` (=0, the "undefined" value), `TYPE_BOOLEAN`, `TYPE_NUMBER` (`ibNumber`), `TYPE_DATE`, `TYPE_STRING`, `TYPE_NULL`, `TYPE_REFFER` / `TYPE_CONST_REFFER`, object kinds `TYPE_VALUE` / `TYPE_ENUM` / `TYPE_OLE` / `TYPE_FUNCTION` / `TYPE_ITERATOR` |
| `core/compiler/codeDef.h` | enums | Opcode (`OPER_*`) and keyword (`KEY_*`) definitions |
| `compileState.h` | `ibCompileState` | The code style a session's modules compile in (CES / VES) — the session's, `thread_local` for a thread with none |

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
| `debugServer.h/cpp` | `ibDebuggerServer` | The debuggee's port — a WebSocket at `/debugger` on the core's HTTP, one designer at a time; owned by the running configuration |
| `debugClient.h/cpp`, `debugClientAsync.cpp`, `debugClientQuery.cpp` | `ibDebuggerClient` | The designer's end (a thin designer's session on the server): the connection, the events told every bridge, the breakpoints kept in the base |
| `debugClientBridge.h/cpp` | `ibDebuggerClientBridge` | Bridge between client socket and UI |
| `debugDefs.h`, `debugEvent.h` | enums | `CommandId`, `EventId`, `ConnectionType`, the events |

### `src/engine/backend/system/`

| File | Class | Role |
|---|---|---|
| `systemManager.h/cpp` | `ibSystemManager` | Built-in function dispatcher; registers 92 functions + 6 procedures = 98 built-ins as of 2026-08-08 (count drifts as features land; grep `AppendFunc\|AppendProc` for the live total) |
| `systemEnum.h` | enums | System-level enumeration constants |

### `src/engine/frmserver/visualView/`

| File/Dir | Content |
|---|---|
| `ctrl/form.h/cpp` | `ibValueForm`, form control collection, action wiring |
| `ctrl/frame.h/cpp` | `ibControlFrame` base class for all controls |
| `ctrl/widgets.h` | Declarations for all control types |
| `visualHost.h/cpp` | `ibVisualHost` — who holds the form; no window, the frame it draws |
| `ctrl/` | Individual control implementations (see Form System) |

The client's side is `frmclient/visualView/` (`visualHostClient.h/cpp` — the tab's view drawn from that frame).

---

## Bytecode Engine

### Compiler Pipeline

```
Source text (ibString)
        │
        ▼
 ibTranslateCode::Load() → Translate()
   Lexer: fills m_listLexem (vector<ibLexem>)
   Each ibLexem: { type, keyword/delimiter number, string data, ibVariant, line, position }
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

Opcodes are defined as plain integer constants in `src/engine/core/compiler/codeDef.h`. The opcodes fall into these groups:

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

63 keywords defined in `KEY_*` enumerators (`core/compiler/codeDef.h`, `KEY_IF=0` … before the `LastKeyWord` sentinel), in lock-step with `s_listKeyWord[]` in `core/compiler/translateCode.cpp` — a `static_assert` keeps the two the same length. Groups:
- Control / structure — `KEY_IF`, `KEY_FOR`, `KEY_FOREACH`, `KEY_WHILE`, `KEY_PROCEDURE`, `KEY_FUNCTION`, `KEY_TRY`, `KEY_EXCEPT`, `KEY_ENDTRY`, `KEY_RAISE`, `KEY_RETURN`, `KEY_NEW`, …
- Access modifiers (leading) — `KEY_PUBLIC` (was `Export`), `KEY_PRIVATE`, `KEY_PROTECTED`
- Preprocessor — `KEY_DEFINE`, `KEY_UNDEF`, `KEY_IFDEF`, `KEY_IFNDEF`, `KEY_ELSEDEF`, `KEY_ENDIFDEF`, `KEY_REGION`, `KEY_ENDREGION`
- LINQ — `KEY_FROM`, `KEY_WHERE`, `KEY_SELECT`, `KEY_ORDERBY`, `KEY_ASCENDING`, `KEY_DESCENDING`, `KEY_TAKE`, `KEY_SKIP`, `KEY_DISTINCT`, `KEY_JOIN`, `KEY_ON`, `KEY_EQUALS`, `KEY_GROUP`, `KEY_BY`, `KEY_INTO` (`KEY_IN` is reused from `Foreach`)
- Access policy — `KEY_RESTRICT` (`restrict <id> in <src> join … where …` — the row-level-security filter; see `docs/access-policy-rls.md`)

Keywords are English-only — there are no Cyrillic / Russian-language synonyms. Two parallel syntax modes share these keywords and compile to the same bytecode:
- **VES** — Visual-Basic-style: `If c Then … EndIf`, `Foreach x In coll Do … EndDo`, `Procedure F() … EndProcedure`. Keyword-fenced blocks.
- **CES** — C-style: `if (c) { … }`, `Foreach (x In coll) { … }`, `Procedure F() { … }`. Brace-delimited. Default for new configurations since 2026-05-10.

Mode is the session's (`ibCompileCode::SetCodeStyle()` writes its `ibCompileState`, taken from its base's configuration); serialised configurations preserve their stored Syntax flag (wire token still reads `vbs` for back-compat).

---

## Metadata System

### ibValueMetaObject

`ibValueMetaObject` (defined in `src/engine/backend/metaCollection/metaObject.h`) is the abstract base for all configuration objects. It extends `ibValueDynamicMembers` (which extends `ibValue`, so it can be assigned to script variables) and `ibPropertyObjectHelper<ibValueMetaObject>` (so properties appear in the designer's object inspector), plus `ibAccessObject` / `ibInterfaceObject`.

Key attributes:
- `ibMetaID m_metaId` — numeric identifier within the configuration
- `ibGuid m_metaGuid` — globally-unique GUID
- `ibString GetName()` / `GetSynonym()` — developer name and user-visible synonym
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

Metadata serialization runs through a uniform, format-agnostic tree (`src/engine/core/serialize/dataBuilder.h`):

- **`ibDataNode`** — one self-similar node of the structure tree (clsid + metaId + field bag + property bag + child nodes). A metaobject contributes its data into a node; a composite value can itself *be* a child node (`ibDataKind::Child`).
- **`ibFormatProvider`** — abstract `Write(node, writer)` / `Read(reader, node)`. Concrete providers:
  - `ibBinaryProvider` — the packet: OES-RPC 1.0, the node's own bytes (`OES1`, a version, a length, the root node; every container says how long it is and how many it holds, text UTF-16LE). The same bytes are the node in memory, in storage (the configuration, form blobs) and on the client protocol's WebSocket; `Read` checks a packet once, where it comes in. Read + write.
  - `ibJsonProvider` (`serialize/jsonProvider.{h,cpp}`) — JSON export for Git VCS / AI generation / human reading. `Read` is a full parser that reads messages, never a configuration — MCP and the client protocol's HTTP `POST` parse every JSON-RPC request through it (`rpc/rpcMessage.cpp`, before any login), so it refuses nesting past 256 containers; the view is lossy by design (Fields + Properties flatten into one key set, Date → ISO string, synthetic `TypeDesc`), so Write→Read is not a round trip — `ibBinaryProvider` is.
- **`ibDataBuilder`** owns the root node and drives `Save(provider, writer)` / `Load(provider, reader)`.

A node is held as its packet's bytes and read where it lies; `ibReaderMemory` / `ibWriterMemory` carry a packet where a stream is asked for. Each metadata class registers a `ibClassID` CLSID (e.g. `MD_CAT` for Catalog) used as the per-node type discriminator.

File-level export/import goes through `LoadConfigFromFile()` / `SaveConfigToFile()` on `ibMetaDataConfigurationBase`. (The former separate `metadataConfigurationXML.cpp` / `metadataConfigurationJSON.cpp` and the `SaveConfigToXML/JSON` API have been replaced by the provider model; XML support is currently retired.)

### Accounting Objects

Three metadata types support double-entry bookkeeping. Built and applied to a live Firebird base
(2026-08-13) — the write path, the account rules, the five virtual tables and the trigger-maintained
totals bundle; see [accounting-register-arc.md](private/accounting-register-arc.md) for what is still absent.

⚠ The word **Subconto** is gone from the engine (2026-08-12): it was a calque carried over from
older accounting software rather than a name in its own right, and the concept is
an **account dimension** whose KIND is a characteristic. Only the opaque CLSID body key
`MD_SKTB` kept its spelling, being a key rather than a name.

| Type | CLSID | Purpose |
|---|---|---|
| ChartOfCharacteristicTypes | `MD_CHRC` | The account dimensions' KINDS — each element stores an `ibTypeDescription` narrowing what values that kind admits |
| ChartOfAccounts | `MD_CHOA` | Chart of accounts with AccountType (Active/Passive/AP), OffBalance, the accounting kinds it declares (two branches of its own: `MD_ACKD` — a boolean field of every account; `MD_ADKD` — a tick-box column of the account's kinds table), the maximum dimension count, and the predefined `AccountDimensionKinds` tabular section |
| AccountingRegister | `MD_AREG` | Double-entry register. One-sided: RecordType (Debit/Credit) + Account + N *(kind, value)* AccountDimension slots; correspondence: AccountDr / AccountCr + N slots per side, and a dimension or resource with `Balance` cleared kept per side as hidden `<Field>Dr` / `<Field>Cr` attributes. Totals per side at two grains (per account and breakdown, per account). Balance / Turnovers / DrCrTurnovers / BalanceAndTurnovers / RecordsWithAccountDimensions |

Bindings: AccountingRegister → ChartOfAccounts (via `ibPropertyChartOfAccounts`), ChartOfAccounts → ChartOfCharacteristicTypes (via `ibPropertyChartOfCharacteristicTypes`). Each binding has its own property class. Since 2026-09-10 (committed 2026-09-11) every chart binding is edited by ONE designer editor, `ibPGChartBindingProperty` (`frmclient/propertyManager/property/advprop/advpropChartBinding.cpp`), which replaced a copy per chart: the candidates and whether one chart or a set may be ticked are the property's own answer (`GetValueList` → `ibPropertyChoiceMode`), and only an accepted dialog changes the value.

### Calculation Objects

Two metadata types and one subordinate support calculation — payroll accruals and deductions whose
types displace one another over days, take a base from other types' results, and go stale when those
change. Built 2026-09-10, committed 2026-09-11; verified by running a payroll configuration on a live
Firebird base. See [payroll-arc.md § 11](private/payroll-arc.md) for the design as built.

| Type | CLSID | Purpose |
|---|---|---|
| ChartOfCalculationTypes | `MD_CHCL` | The calculation TYPES — a hierarchical chart with `UseActionPeriod`, `BaseDependence` (`None` / `ByActionPeriod` / `ByRegistrationPeriod`), `BaseCharts`, and three predefined relation sections of one class, `ibValueMetaObjectCalculationTypeRelationTable` (`MD_DSTB`): `Displacing`, `Base`, `Leading`. A row is an edge from the type that owns it to the type it names |
| CalculationRegister | `MD_CREG` | Movements subordinate to a recorder, keyed by (recorder, line), no record manager. Standard attributes `CalculationType`, `RegistrationPeriod` (its one period — no `Period` column), `Storno`; `ActionPeriod` / `ActionPeriodStart` / `ActionPeriodEnd` with `UseActionPeriod`; `BasePeriodStart` / `BasePeriodEnd` with `UseBasePeriod`. The actual action periods are a reading of the records (source `CalculationRegister.<Register>.ActualActionPeriod`); with a `Schedule` bound (an information register, its value resource, its date dimension, a dimension link per other dimension), `CalculationRegister.<Register>.ScheduleData` reads the schedule summed over each record's periods; with `UseRecalculation` the record-set write keeps the recalculation marks — one table of the register's own columns, read as `CalculationRegister.<Register>.Recalculation`; `GetBase` is a function of the records (a record's line and the manager), and the same reading is the source `CalculationRegister.<Register>.Base<BaseRegister>` — a table per register the chart takes a base from |
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
- **Kind** (`ibSessionKind`) — Launcher / Designer / Enterprise / Service (a server's own login) / WebServer (the old web server's technical row) / WebClient (per-tab) / BackgroundJob / ScheduledJob / SystemJob / ThinClient / ThinDesigner (a client of the protocol, in the application or in the configuration). The kind, not the run mode, says what a session is: a job or a thin client lives in whatever process hosts it, and its run mode is its host's.
- **State machine** — `ibSessionState` (Created / Added / Rejected / Stopping / Gone), `ibAuthState` (Anonymous / Authenticated / AuthFailed)
- **User info** — `ibUserInfo` (formerly `ibApplicationDataUserInfo`) — OES-user (from `sys_user` table), distinct from the DB-level admin user used to open the database connection. Plus `m_sessionRawPassword` — plain-text cached only for Designer "Start debugging" so spawned children can re-authenticate without prompting. `ibUserInfo` itself owns sys_user CRUD as static factories — `appData` no longer mediates.
- **Working date** — `m_workDate` per-session (replaces the legacy static `ibValueSystemFunction::ms_workDate` so two web sessions don't step on each other).
- **Translate state** — `ibTranslateState`: explicit override, the user's language, the configuration's own, and the resolved code (`override || user || configuration`). Selects which metadata synonym / form-label translation is shown. Per-session, so concurrent web tabs each render their own user's language and two bases in one process each keep their own. Distinct from the engine's UI gettext (`ibDictionary`, the base's locale per thread). Read through `ibBackendLocalization::GetUserLanguage()`.
- **Compile state** — `ibCompileState`: the code style (CES / VES) the session's modules compile in, taken from its base's configuration. Like the interpreter's `ibProcUnitState`, a thread with no session (codeRunner, tests) has its own `thread_local` one.
- **Root module manager** — `m_root : ibValuePtr<ibValueModuleManagerRuntimeConfiguration>`. Created via `EnsureRoot()` in `ibSessionRegistry::NotifyAuthenticated`, right after `AcquireMetaData` and before the `OnAuthenticated` listeners. Stays nullptr for sessions that never run scripts — **the Designer never creates a root** (`EnsureRoot` is gated on `DesignerMode()`; it uses the lightweight `ibValueModuleManagerDesigner` in the compile cache instead), and likewise WebServer technical / Launcher. Objects/records/modules reach the right manager through the `ibSession::GetEditModuleManager(metaData)` seam (Designer → compile-cache designer manager; runtime → `m_root`). See `module-manager-split.md`.
- **Frame** — `virtual ibBackendDocFrame* GetFrame() const { return nullptr; }` on base `ibSession`. Frame storage lives on derived sessions that have one (`ibClientSession::SetFrame(ibClientFrame*)`, `frmserver/client/clientSession.h` — a protocol client's). Base has no `m_frame` field — null means "no frame on this session" (codeRunner, a server's technical session). The frame belongs to the session that created it, not to a process-wide singleton.
- **Per-session debug** — optional `ibDebugSession` (CV/mutex + per-session watch expressions + run context) so concurrent web sessions can each enter their own debug loop without blocking.
- **Exclusive (monopoly) mode** — `m_exclusive`. At most one session in the registry holds it; while held, every other Connect parks until release.
- **Server back-link** — `m_server : weak_ptr<ibSession>` from a server-spawned client to the session that hosts it (e.g., a WebClient → its WebServer). Used by shutdown logic, cluster topology, and admin UI.

`ibSession::Current()` is the canonical "session this code is currently working on". One rule in every host:

- **The thread's binding** — a thread that bound a session (`ibSessionScope`, `ibSessionThreadBinding`, a job's run) works for it.
- **Else the base's fallback** — the process's own session there (`IsProcessSessionKind`: the designer's or the client's window, the application server's login). A job or a web tab never becomes it.

`ibSessionScope` (legacy) and `ibSessionThreadBinding` (preferred for app entry points) are the RAII helpers that bind a session to the calling thread. The interpreter no longer reads global `thread_local` state directly: `ibProcUnitState` lives under `ibSession` (`session.h`), and the only `thread_local` slot in `session.cpp` is a fallback for sessionless callers (codeRunner sandbox / system bootstrap). The worker pool (`workerPool.h` + `workerPoolHeadless.cpp`) leases a session into a thread via `tl_currentLease` and runs the request on it.

Runtime ProcUnits live on per-session descriptors. The session's root `ibValueModuleManagerRuntimeConfiguration` (`ibSession::m_root`) owns common modules, forms, and per-instance object runtimes; each child is its own descriptor (`ibRuntimeModuleDataObject`) carrying its own `shared_ptr<ibProcUnit>`. Concurrent web sessions therefore each work on their own descriptor instances — no shared ProcUnit, no cross-session execution mutex. See `runtime-facade.md` for the descriptor composition / parent chain details.

### ibSessionRegistry

`ibSessionRegistry` (`src/engine/backend/session/sessionRegistry.h`) is the base's session manager — one per base, owned by its `ibApplicationInstance`:

- **Single-consumer queue + priority** — one registry thread processes `Add / Attach / Detach / Remove / SetActivity` requests (Urgent → Normal → Low → Background). All DB mutation for `sys_session` happens on this thread.
- **Liveness via heartbeat on `lastActive`** — each process's `JobHeartbeatOwn` UPDATEs `lastActive` on its own `sys_session` rows every ~1s. Any row trailing `now` by more than `kStaleCutoffSec` (10s, in `sessionRegistry.cpp`) is treated as a zombie and DELETEd by another process's sweep. The earlier row-lock-as-source-of-truth design (a long-running `SELECT ... WITH LOCK` over own-session rows, probed via `TryProbeRowLock`) was **rolled back** — it self-deadlocked (see `session-registry.md §4`); only retirement comments remain in code. Record write protection is a separate concern — optimistic `DataVersion` check + the `sys_lock` table, see [record-locks.md](private/record-locks.md).
- **Connect/Disconnect API** — `ibApplicationInstance::CreateSession` calls `registry.Connect(req)` which returns an `ibSessionTicket` (RAII, dtor submits Remove@Urgent).
- **Single mutator of session state.** `ibSession`'s state-machine mutators (`Transition`, `TransitionAuth`, `SetIdentity`, `SetInserted`, `WaitForState`, `WaitForAuth`) and auth-flow setters (`SetUserInfo`, `SetSessionRawPassword`) are private under `friend ibSessionRegistry`. The public façade `ibSessionRegistry::InstallUser(s, info, pwd)` is the only entry point for auth bring-up — `appData` and login dialogs route through it, never poke session internals directly.
- **Cluster snapshot** — `ibSessionRegistry::GetClusterSnapshot()` returns `ibSessionSnapshot` (formerly `ibApplicationDataSessionArray` on `appData`), refreshed every ~3s by `JobRefreshSnapshot`. Snapshot now lives in `backend/session/sessionSnapshot.{h,cpp}`.

The registry supports multiple concurrent sessions (a server's clients, a file base's connections) through the same mechanism. See `project_session_registry_refactor` memory entry for the current implementation status.

### Runtime ownership

**Compile state — shared, immutable.** `ibCompileCode` produces an `ibByteCode` that lives on the configuration's compile descriptor (`ibCompileModule` on `ibValueMetaObjectModuleBase`). One bytecode per module is shared across all sessions; rebuilt only on Designer edit or metadata reload. AOT cache (`sys_bytecode_cache` via `ibByteCodeCache`) lets `Compile()` skip the parse+emit on cache hits — the cached blob is `ibByteCode::SerializeAOT/DeserializeAOT` (magic `'PBC1'`; see the [compiler module table](#srcenginebackendcompiler)). A row is found only under the key it was written with — an MD5 of the format version (`kAOTFormatVersion`), the engine hash taken at build time (`kEngineFingerprint`: the compiler, the interpreter and the built-in registry) and the configuration's digest — so a saved configuration, a compiler change or a new built-in is a miss and a recompile, never a stale hit. A hit skips the parser, **not** the preparation: the descriptor still runs `PrepareModuleData()` to build the module's name surface, because a name's address is the number of rungs the resolver walked and a parent with no live compile context is counted differently; the surface it builds is then verified name-for-name against the loaded `m_listVar`, and a row that disagrees is invalidated and recompiled from source. Bytecode compiled under an eval (a watch or the debugger's sandbox) is never written to the cache — its host frame is an extra rung. See [compiler-pipeline.md § 4a](private/compiler-pipeline.md).

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

**Worker pool dispatch.** Script execution runs on a worker thread leased via `ibWorkerPool` (`backend/session/workerPool.h` + headless impl). Each request leases a session into `tl_currentLease` for the call's duration; `ibSession::Current()` resolves through this slot. Every host but the launcher runs it (`appHost.cpp`) — a thin client's file base, the web host and the application server alike — each request leased onto a worker of the process's one pool; a script that waits parks a fiber (`core/fiber`) on its thread, which goes on serving other sessions until `Wake` resumes it. The script interpreter never touches global `thread_local` state directly — `ibProcUnitState` lives under `ibSession`, the one `thread_local` fallback in `session.cpp` exists only for sessionless callers (codeRunner sandbox / system bootstrap).

**Same model for every host** — the only difference is session count. The `ibSessionRegistry + ibSession + ibSessionScope + per-session runtime tree + worker pool` stack is identical across all run modes.

**Bytecode self-contained.** `ibByteCode` holds its own moduleName, rootContext, parent-bytecode ref, and dependency manifest. There is no `byteCode->m_compileModule` back-pointer; runtime lifetime is decoupled from `ibCompileCode` lifetime, so metadata reload can drop compile state while running sessions hold their bytecode through their shared_ptr.

### Designer — compile only

A designer session (`Designer`, `ThinDesigner`) has no runtime — `AttachRuntime` returns early for a designer kind. Designer reads `ibCompileCode` for autocomplete, function search, jump-to-definition, and cascading recompile. Scripts are not executed. Autocomplete surfaces bound names by reading the compile module's bind tables and walking the **compile-module** parent chain — from the backend since 2026-09-08 (`ibValueAtCaret` / `ibNamesAtCaret`, `backend/compiler/scriptComplete.h`); the editor's own precompiler is deleted and it keeps only its lexer. See [Name binding § Designer](private/name-binding.md#designer--surfacing-the-same-binds). Debug sessions attach to the process that runs the configuration over its debug port — see [Debugger Architecture](#debugger-architecture).

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
    ibT("SELECT * FROM %s WHERE guid = ?"), table_name);
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

The concrete `ibValueForm` (`frmserver/visualView/ctrl/form.h`) multiply-inherits both (plus `ibRuntimeModuleDataObject` for its per-instance ProcUnit):

```
ibValueForm  (concrete — frmserver/visualView/ctrl/form.h)
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

### Visual controls — 18 registered + 5 system-kind children

Implemented in `src/engine/frmserver/visualView/ctrl/` (drawn by `frmclient/visualView/ctrl/`), verified 2026-10-27
by grepping `CONTROL_TYPE_REGISTER`. The five system-kind children are registered via `S_CONTROL_TYPE_REGISTER`:
ClientForm, SizerItem, NotebookPage, TableboxColumn, TableboxColumnGroup (added with column groups, 2026-08-17).
Choice, ComboBox, ListBox and ToolBar (with Tool / ToolSeparator) were the desktop front's only and went with it.

| Control | File |
|---|---|
| Button | `button.cpp` |
| CheckBox | `checkbox.cpp` |
| Form (root) | `form.cpp` |
| Frame | `frame.cpp` |
| Gauge | `gauge.cpp` |
| GridBox | `gridBox.cpp/.h` |
| HtmlBox | `htmlBox.cpp/.h` |
| Notebook | `notebook.cpp/.h` |
| RadioButton | `radiobutton.cpp` |
| Slider | `slider.cpp` |
| StaticText | `statictext.cpp` |
| StaticLine | `staticline.cpp` |
| TableBox | `tableBox.cpp/.h` |
| TextBox | `textBox.cpp/.h` |
| TextCtrl | `textctrl.cpp` |
| BoxSizer | `boxsizer.cpp` |
| GridSizer | `gridsizer.cpp` |
| StaticBoxSizer | `staticboxsizer.cpp` |
| WrapSizer | `wrapsizer.cpp` |
| ChartBox | `chartBox.cpp/.h` |

### Form Rendering

`ibVisualHost` (frmserver) holds the form and writes it as a frame — no window. The client draws that frame:
`ibVisualHostClient` (frmclient) builds the wx controls from it, and what the person does goes back over the
[client protocol](public/client-protocol.md) to the form's script event handlers.

---

## Debugger Architecture

### Client-Server Model

```
the debuggee — whatever runs the configuration       the designer
(appserver, webserver,                               designer-thin.exe — the client in its session
 a thin client's file base)                          on the server, reached over the client protocol
      │                                                     │
ibDebuggerServer ◄──── WebSocket /debugger ────────► ibDebuggerClient
(backend/debugger/, on core/net)                     (backend/debugger/)

Ports: 7700–7709, the first free one  (ibPort::Debugger, core/net/ports.h)
```

The thin designer's debugger is the server's: the engine's `ibDebuggerClient` lives in the designer's session where
the base is, and `designer-thin/backend/debugger/` is its facade — a command goes as `debug {Command}` on a second
connection of the same client, bound to the first (`ibProtocol::ibDebug`, protocol 6), and what the engine says comes
back as news on it. The breakpoints at the editor's lines stay with the editor; the store they are kept in is the
server's.

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

The port's I/O threads only move bytes: each command is one binary WebSocket message (`SendCommand` / `RecvCommand`), handed in order to the debug thread — a `std::thread` of `ibDebuggerServer::ibDebuggerServerConnection`, the one the session registry knows as the debugger's. A packet that cannot be read — shorter than what it claims to hold, or a string with no terminator — ends that connection, with a line in the technology journal; the debuggee goes on running. The designer's end does the same with a packet it cannot read.

---

## Key Data Flows

### User Login (file base)

```
enterprise-thin.exe / designer-thin.exe --file=… (a file base: fileserver, in the client's process)
  └─ ibApplicationInstance::CreateAppDataEnv(ibFileInstanceRequest{ server, port, user, pwd, db, locale })
       └─ ibDatabaseLayer::Open(server, port, db, ibUser, ibPwd)   # DB-level admin connection
            └─ ibClientInstance::Login (frmserver) — on the client's login over the protocol
                 └─ appData->CreateSession<ibClientSession>(ThinClient | ThinDesigner, …)
                 # registry runs Connect(req) under the session factory:
                 #   - Submit(Add, Normal), waits state Added
                 #   - Add handler INSERTs the sys_session row
                 └─ session->Open(user, password), on the session's own worker   # auth orchestration
                      └─ ticket.Attach(user, pwd) — Submit(Attach, Normal)
                           └─ ibApplicationInstance::AuthenticateUser
                                  (PBKDF2 preferred, MD5 silent-upgrade path)
                                └─ InstallUser writes session->m_userInfo
                                     └─ NotifyAuthenticated phases (registry-driven; the configuration
                                        ran as the base opened):
                                          1. OnFirstConnect — on the first auth
                                          2. session->AcquireMetaData() — its own reference
                                             (a designer's: a configuration of its own, loaded and run)
                                             session->EnsureRoot() — CreateRoot(GetMetaData())
                                          3. OnAuthenticated — session->CompileRoot()
                                                             + mm->AttachRuntime(s)
                                                                — main ProcUnit + Execute top-level
                                                                — StartMainModule: BeforeStart / OnStart
                                                                — the client draws what the protocol answers
```

### Form Open

> The full map of this layer — who builds a form, what its identity is, how it reaches a
> window and how it dies — is [form-engine.md](private/form-engine.md). The chain below is the
> one-screen version.

```
Script, or a client's command over the protocol:
OpenForm("Catalog.Products.ListForm")
  └─ ibValueMetaObjectFormBase::CreateAndBuildForm(request, metaForm, owner, srcObj)
       └─ ibSession::CurrentFrame()->CreateNewForm(request, metaFormObject, ownerControl, srcObject)
              # session-owned frame: ibClientFrame (frmserver)
            └─ ibValueForm constructed (per-instance compileModule + ProcUnit)
                 └─ LoadFormData / BuildForm — control tree built from metadata
                      └─ frame creates the doc-view child (ibClientChildFrame)
                           └─ ibVisualHost created (no window: the frame the client draws)
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

### Session Teardown (a protocol client)

```
ibClientInstance::OnExit (frmserver) — the client logs out, or its connection is gone
  └─ session->WakeDebugLoop(); session->Cancel()   # a parked or waiting script unwinds first
       └─ on the session's worker: frame->CloseDocuments() (form dtors)
            └─ ExitMainModule (BeforeExit / OnExit events, under m_runtimeMutex)
                 └─ session->ClearRoot() → mm->DetachRuntime(s) — release descriptor ProcUnits
                                                     bound to this session
                      └─ m_frame.reset — the frame releases the session's holder
                           └─ ticket.reset → Submit(Remove, Urgent)
                                └─ registry DELETE sys_session row
```

---

## Localization (UI gettext)

Two parallel translation surfaces exist; do not confuse them:

- **Configuration language** — the session's translate state (`ibTranslateState::m_resolvedLanguageCode`), selects metadata synonyms / form-label translations stored *inside* the configuration. See `ibBackendLocalization::GetUserLanguage` / `SetUserLanguage`.
- **Engine UI language** — gettext catalogs under `lang/`, laid out as the programs read them: the template `lang/open_es.pot`, and per language `lang/<code>/open_es.po` with its compiled `open_es.mo` beside it. Strings wrapped in `_("...")` macros across `src/engine/**` end up in the `.mo` and are looked up by the engine's own `ibDictionary` (`core/localization/dictionary.h`), one per language, chosen per thread by the base it works for ([translations.md](public/translations.md)). A program finds `lang/` beside it, or walking up from its folder in a build tree; the packages carry `lang/`.

**Workflow** (when adding new `_()` strings):

```
# Regenerate the template from current sources (uses Poedit's gettext tools).
xgettext --from-code=UTF-8 --keyword=_ --keyword=ibTRANSLATE --keyword=RuntimeError \
         --keyword=ibPLURAL:1,2 --keyword=ibGETTEXT_IN_CONTEXT:1c,2 --keyword=ibTRANSLATE_IN_CONTEXT:1c,2 --language=C++ --no-wrap \
         --output=lang/open_es.pot --files-from=<list-of-cpp-files>

# Merge new entries into each language file (preserves existing translations).
msgmerge --no-wrap --update --backup=none lang/ru/open_es.po lang/open_es.pot
msgmerge --no-wrap --update --backup=none lang/uk/open_es.po lang/open_es.pot

# Translate empty msgstr entries (manually or in Poedit).

# Compile to .mo, in place: the programs read lang/<code>/open_es.mo.
msgfmt --check-format --output-file=lang/ru/open_es.mo lang/ru/open_es.po
msgfmt --check-format --output-file=lang/uk/open_es.mo lang/uk/open_es.po
```

wx's own words (stock labels, its dialogs) are its catalog `wxstd`: `lang/<code>/wxstd.po` is wx's
`locale/<code>.po`, compiled the same way to `lang/<code>/wxstd.mo`; `wxLocale::Init` loads it by itself.

The Poedit GUI (`File → Open` on the .po) does Step 1 + the editing UI in one shot. CLI route is faster for batch updates from CI.

One English word with two meanings takes a context for the special one: `ibGETTEXT_IN_CONTEXT("document attribute", "Number")` is translated apart from `_("Number")`.
