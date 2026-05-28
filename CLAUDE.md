# OES Enterprise — AI Context File

This file gives an AI assistant (Claude Code or similar) the context needed to work effectively in this repository without reading every source file.

---

## What This Project Is

**Open Enterprise Solutions (OES)** is a C++17 cross-platform low-code enterprise application platform, conceptually similar to 1C:Enterprise. It lets developers define business applications through metadata (object types, forms, modules) and a built-in scripting language, without writing low-level code.

The runtime executes compiled bytecode, renders forms through wxWidgets, and stores all application data in a relational database (Firebird by default).

---

## Tech Stack

| Area | Technology |
|---|---|
| Language | C++17 |
| GUI | wxWidgets 3.3.2 (git submodule at `src/3rdparty/wxWidgets`) |
| Build (Windows) | MSBuild — `enterprise.sln` (Visual Studio 2019/2022) |
| Build (cross-platform) | CMake — `CMakeLists.txt` at repo root; macOS / Linux build supported, Windows uses MSBuild |
| Primary database | Firebird (embedded) |
| Optional databases | PostgreSQL, SQLite, MySQL, ODBC |
| License | LGPL 2.1 |

---

## Repository Layout

```
enterprise/
├── enterprise.sln           # 9-project MSBuild solution
├── Common.props             # Shared output paths and macros
├── ConfigurationDefs.props  # Per-configuration preprocessor defines
├── CLAUDE.md                # This file
├── docs/
│   ├── ai-context.md         # READ FIRST if you are an AI generating metadata / scripts
│   ├── ARCHITECTURE.md
│   ├── BUILD.md
│   ├── ui-palette.md         # Interior-design palette — source of truth for UI colours
│   └── configuration-compare.md  # Compare/Merge feature — walker, model, Apply paths
└── src/
    ├── 3rdparty/wxWidgets/  # Submodule
    └── engine/
        ├── backend/         # backend.dll  — engine, compiler, DB, metadata, debugger
        ├── frontend/        # frontend.dll — wxWidgets UI, form renderer
        ├── enterprise/      # enterprise.exe
        ├── designer/        # designer.exe  (IDE)
        ├── launcher/        # launcher.exe  (connection chooser)
        ├── daemon/          # daemon.exe    (background service)
        ├── codeRunner/      # codeRunner.exe
        ├── classChecker/    # classChecker.exe
        └── simplePlugin/    # simplePlugin.dll (example)
```

---

## Key Architectural Decisions

### 1. ibDatabaseLayer Abstraction

All database access goes through the abstract `ibDatabaseLayer` interface (`src/engine/backend/databaseLayer/databaseLayer.h`). The five concrete drivers (Firebird, PostgreSQL, SQLite, MySQL, ODBC) implement this interface. Code everywhere uses `db_query->RunQuery(...)` or `db_query->PrepareStatement(...)`. Never access driver classes directly.

### 2. ibValue Universal Type

`ibValue` (`src/engine/backend/compiler/value.h`) is the single value type used for all script variables. It is a tagged union covering: `TYPE_UNDEFINED`, `TYPE_BOOLEAN`, `TYPE_NUMBER`, `TYPE_DATE`, `TYPE_STRING`, `TYPE_REFFER` (reference/object). Arithmetic and comparison operations have type-dispatched variants (the `TYPE_DELTA*` opcode offset scheme in `codeDef.h`).

`ibValueMetaObject` extends `ibValue`, meaning metadata objects (Catalog definitions, Document definitions, etc.) can be stored in and returned from script variables.

### 2a. ibNumber — exact-decimal lazy-grow

`ibNumber` (`src/engine/backend/number.h`) is the numeric storage type used by `ibValue::m_fData`. It is **not** a typedef for ttmath::Big — that dependency was removed; the class is self-contained.

- `sizeof(ibNumber) == 8` always. Single tagged `uint64_t`: bit 0 = tag, bits [16:1] = exp10, bits [63:17] = 47-bit signed mantissa. Most values stay inline (immediate tier).
- Heap tier: `BigImpl { std::vector<uint32_t> limbs; bool negative; int32_t exp; }` — exact decimal, magnitude grows by demand. Supports 200+ fractional digits (1С-style precision).
- Self-contained: no ttmath dependency. Schoolbook Add/Sub/Mul + base-2 long-division Div live in `number.cpp`. MSVC x86/x64 use `_addcarry_u32`/`_subborrow_u32` intrinsics; portable fallback elsewhere.
- Buffer / wire: `wxMemoryBuffer GetBuffer()` plus `bool GetBuffer(ibWriterMemory&)` / `bool SetBuffer(const ibReaderMemory&)` — chunk-encapsulated I/O with internal `kIbNumberChunk` ID. Compact-zero encoding: zero produces 0-byte buffer, no allocation.
- 128-bit raw: `To128Bytes(uint8_t[16])` / `From128Bytes` for Firebird SQL_INT128 columns.
- Tests: `enterprise/tests/test_number.cpp` (gtest). 111/111 pass at landing.
- See `docs/next-session-plan.md` for the landing summary and known-risks list.

### 3. ibProcUnit Bytecode Interpreter

Scripts are compiled to bytecode by `ibCompileCode` and executed by `ibProcUnit`. This is a simple stack machine. Each compilation unit produces an `ibByteCode` containing an instruction array and a function table. `ibProcUnit::Execute()` dispatches on `ibByteUnit::m_numOper`.

The compiler is a single-pass recursive descent parser with a deferred-call-resolution second pass for forward function references.

### 4. Two-DLL Architecture

`backend.dll` has zero UI code. `frontend.dll` owns all wxWidgets code. Communication is through abstract C++ interfaces exported by `backend.dll`. This allows the backend to run headless (daemon, codeRunner, service mode).

### 5. Throw-By-Value Exception Pattern

Backend exceptions are thrown by value and caught by const reference, standard
C++ style. `ibBackendException` has a virtual destructor so catching by the
base class preserves the dynamic type for `dynamic_cast` and downstream
re-catches.

```cpp
// throw (usually through a static Error() helper that formats the message)
ibBackendCoreException::Error(_("..."), arg);         // internally: throw ibBackendCoreException(msg);
throw ibBackendInterruptException();

// catch (generic)
catch (const ibBackendException& err) {
    wxLogMessage(err.GetErrorDescription());
    // rethrow (same in-flight object, preserves m_errorHandled):
    throw;
}

// catch (derived first, base second — order matters)
catch (const ibBackendAccessException& err) { ... }
catch (const ibBackendException& err)       { ... }
```

`ibBackendException::ProcessError` is called from inside the procUnit catch
block; it formats/dispatches the error and then uses bare `throw;` to
propagate the same exception object.

### 6. Metadata CLSID System

Every class in the metadata and value system is identified by a 7-character string CLSID encoded into a `ibClassID` integer via `string_to_clsid()`. Examples: `"VL_NUMB"` (number value), `"MD_CAT"` (Catalog), `"MD_DOC"` (Document). These appear in serialised configuration files and the database.

---

## Important Patterns

### Accessing the Database

```cpp
// Global macro — returns ibDatabaseLayer*
db_query->RunQuery(wxT("INSERT INTO %s ..."), tableName);

ibPreparedStatement* stmt = db_query->PrepareStatement(wxT("SELECT * FROM %s WHERE id = ?"), table);
stmt->SetParamInt(1, id);
ibDatabaseResultSet* rs = stmt->RunQueryWithResults();
```

### Accessing Application State

```cpp
appData->GetRunMode();           // ibRunMode enum
appData->GetDatabaseLayer();     // ibDatabaseLayer*
appData->GetUserInfo();          // current logged-in user
activeMetaData->GetCommonMetaObject();  // root of the metadata tree
```

### Calling a Script Function from C++

```cpp
ibProcUnit unit;
unit.Execute(byteCode);
ibValue result;
unit.CallAsFunc(wxT("FunctionName"), result, arg1, arg2);
```

### Creating a Form

```cpp
ibBackendValueForm* form = ibBackendValueForm::CreateNewForm(
    metaFormObject,     // ibValueMetaObjectFormBase*
    ownerControl,       // ibBackendControlFrame* or nullptr
    sourceObject        // ibSourceDataObject* or nullptr
);
```

---

## Known Issues

### Password Hashing — PBKDF2-HMAC-SHA256

New passwords are hashed via `ibPasswordHash::Hash` (`src/engine/backend/utils/passwordHash.{hpp,cpp}`) using PBKDF2-HMAC-SHA256 at 600k iterations (OWASP 2023) with a 16-byte system-RNG salt, stored in PHC-style format `$pbkdf2-sha256$<iter>$<saltB64>$<hashB64>`. `Verify` additionally accepts legacy 32-hex MD5 hashes from pre-migration databases; callers use `NeedsRehash` + `Hash` to upgrade silently on successful login (see `ibApplicationData::AuthenticationAndSetUser`). MD5 stays in-tree for metadata integrity (`ibMD5::ComputeMd5`) — **never** reuse it for passwords.

Argon2id (OWASP #1, memory-hard) would be the stronger option but requires vendoring an external library; revisit when the threat model calls for it.

### Empty Catch Blocks

About 24 `catch (...) {}` sites exist in `backend.dll` — they live in
RAII destructors, Firebird driver rollback paths, and connection-pool
cleanup. The pattern is intentional: a destructor that throws is
worse than a swallowed error during cleanup, and a rollback that
fails on an already-failed transaction is a no-op. They are
**not** the "swallow real errors in production code paths" pattern.

When adding new code, follow the existing rule: `catch (...) {}`
only inside dtors / cleanup helpers, never in business logic. In
business code, log or rethrow.

---

## Build Commands

### Windows — MSBuild (preferred)

```cmd
# Release x64 from Developer Command Prompt
msbuild enterprise.sln /p:Configuration=Release /p:Platform=x64 /m

# Debug x64
msbuild enterprise.sln /p:Configuration=Debug /p:Platform=x64 /m
```

Output lands in `bin\Win64\Release\` or `bin\Win64\Debug\`.

### CMake (macOS / Linux)

```bash
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --parallel 3   # cap parallelism on 16 GB RAM
```

See `docs/BUILD.md` for per-platform requirements and the
`OES_USE_FIREBIRD / POSTGRESQL / MYSQL / ODBC / TBB` options.

---

## Branch Strategy

| Branch | Purpose |
|---|---|
| `master` | Release-tagged commits; stable |
| `develop` | Active development; target branch for all PRs |

All feature work happens on branches cut from `develop`. Pull requests target `develop`. Releases are merged from `develop` to `master` and tagged.

---

## Naming Conventions

| Element | Convention | Example |
|---|---|---|
| Public classes (backend) | `ib` prefix | `ibDatabaseLayer`, `ibValueMetaObject` |
| Public classes (frontend) | `ib` prefix | `ibValueForm`, `ibVisualHost` |
| Member variables | `m_` prefix | `m_pByteCode`, `m_strName` |
| Static members | `s_` prefix | `s_instance`, `s_listKeyWord` |
| Compile-time constants | `g_` prefix | `g_metaCatalogCLSID` |
| Macros (singletons) | lower camelCase | `appData`, `db_query`, `activeMetaData`, `debugServer` |
| File names | camelCase or lowerCamelCase | `compileCode.cpp`, `databaseLayer.h` |
| CLSID strings | `XX_YYY` pattern | `"MD_CAT"`, `"VL_NUMB"` |

---

## Metadata Object Types

The 11 business object types and their C++ classes:

| Type | Class | Header |
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

---

## Configuration Text Format

OES supports exporting and importing configuration metadata in text formats (XML and JSON), enabling Git VCS, AI-powered configuration generation, and human-readable editing.

### XML (OES-XML-2.0)

- **File:** `src/engine/backend/metadataConfigurationXML.cpp`
- **Export:** `ibMetaDataConfigurationBase::SaveConfigToXML(const wxString& fileName)`
- **Import:** `ibMetaDataConfigurationBase::LoadConfigFromXML(const wxString& fileName)`
- **Designer menu:** Configuration → Export/Import configuration to/from XML
- **Full round-trip:** Export → Import preserves all metadata

### JSON (OES-JSON-1.0)

- **File:** `src/engine/backend/metadataConfigurationJSON.cpp`
- **Library:** nlohmann/json v3.11.3 (`src/3rdparty/nlohmann/json.hpp`, MIT, header-only)
- **Export:** `ibMetaDataConfigurationBase::SaveConfigToJSON(const wxString& fileName)`
- **Import:** `ibMetaDataConfigurationBase::LoadConfigFromJSON(const wxString& fileName)`
- **Designer menu:** Configuration → Export/Import configuration to/from JSON

### What is serialized

All 11 business object types with: attributes (full type qualifiers), tabular sections, forms (base64 + module code), object/manager modules, predefined values, default form assignments, MetaDescription bindings (Owner, Generation, RegisterRecord, ChartOfCharacteristicTypes, ChartOfAccounts), QuickChoice, WriteMode, Periodicity, RegisterType, dimensions, resources, enum values. CLSID stored as u64 numeric values.

### ibValue Serialization

`src/engine/backend/compiler/valueSerialization.cpp` — `DoSerialize`/`DoDeserialize` for primitive types (Boolean, Number, String, Date). Used for client-server data exchange.

`src/engine/backend/metadataSerialization.cpp` — `ibMetaData::Serialize`/`Deserialize` wraps values in OES Serialize format: `S: OES Serialize;;;C:<classType>;;;L:<length>;;;D:<data>;;;E: OES Serialize;;;`

---

## Compiler Quick Reference

- **Opcodes:** defined in `src/engine/backend/compiler/codeDef.h` as `OPER_*` enumerators. Call-family: `OPER_CALL` (stack-frame named call), `OPER_CALL_CLOSURE` (named call with heap-promoted frame — callee has an inner lambda capturing locals), `OPER_CALL_METHOD` (per-class method dispatched by name string), `OPER_CALL_LINQ` (universal pipeline op dispatched by `ibLinqMethod` enum id, no string lookup), `OPER_CALL_LAMBDA` (dynamic call — target read from a slot at runtime, must wrap an `ibValueFunction`). Lambda body fences `OPER_LFUNC` / `OPER_ENDLFUNC`; materialise via `OPER_FUNC_PTR`
- **Keywords:** 43, defined as `KEY_*` enumerators in the same file
- **Built-in functions:** 91, registered in `ibSystemManager` (`src/engine/backend/system/systemManager.cpp`)
- **Syntax modes:** VES (`If…Then…EndIf`, Visual-Basic-style with 1С/BSL mix) and CES (`if (…) { … }`, C-flavoured); both compile to the same bytecode. Mode is process-global on `ibCompileCode::SetCodeStyle()` / `GetCodeStyle()`. **CES is the default** for new configurations (2026-05-10); existing serialised configs preserve their stored Syntax. Wire token in metadata enum still reads `vbs` for back-compat — user-visible label is `ves`.
- **Anonymous functions:** `Function(args) ... EndFunction` and `Procedure(args) ... EndProcedure` (or CES `Function(args) { … }`) work as expressions — assignable to slots, callable through variables. Backed by `ibValueFunction` (inline class in `procUnit.cpp` near `ibValueIterator`, CLSID `VL_FUNC`). Lambda's compile-context kind is `RETURN_LAMBDA`. Eval-in-lambda resolves outer frames via splice in `CompileExpression` (lambda-shim's `m_pppArrayList[1..]` → eval's `[2..]`). No closure capture yet — outer-function locals fail with "undefined identifier" at compile. See `docs/lambda.md`.
- **Debugger port:** 1650 (`defaultDebuggerPort` in `src/engine/backend/debugger/debugDefs.h`)

### Bytecode resolver (kind-driven, AOT-ready)

`ibByteCode` carries everything needed for cross-bc resolve, with no runtime dependency on the compile-context.

- `m_listVar`: `std::vector<ibByteCodeVarInfo>` — `ibVarKind` enum: `Local / Export / External / Context / ContextProp`.
- `m_listFunc`: `std::vector<ibByteFunction>` — `ibFnKind` enum: `Local / Export / ContextMethod / Lambda` (the `Lambda` kind landed with the anonymous-function feature; cross-bc invisible, name-keyed lookups filter via `IsLambda()`).
- `m_listLocals` (per-function): same vector shape as `m_listVar`.
- Cross-table refs: `ContextProp / ContextMethod` carry `m_parentRef` indexing back into `m_listVar`.
- `ibByteBinder` reads `m_listVar` directly and binds slots whose kind ∈ `{External, Context}` (`IsBindRequired()`).
- Eval / watch expressions use `ibCompileEval` (in `procUnit.cpp`); `ibCompileCode::IsExpressionOnly()` and `GetEvalHostFunction()` are virtual hooks the eval class overrides.
- Descriptors expose `ExportNamesToHelper(helper, alias)` on `ibRuntimeModuleDataObject` to populate a value's helper from the bc's export entries.

See `docs/eval-scope-refactor.md` for the full architecture and `docs/next-session-aot.md` for the persistence-layer plan that builds on this state.

### Runtime infrastructure (landed)

- **Worker pool** — `ibWorkerPool` (`src/engine/backend/session/workerPool.h`) + headless implementation (`workerPoolHeadless.{h,cpp}`). Per-thread session lease via `tl_currentLease`; sessionless callers get a `thread_local` fallback `ibProcUnitState` in `session.cpp`.
- **AOT bytecode cache** — `byteCodeAOT.cpp` serialises a compiled `ibByteCode` to a memory stream; deserialisation reverses the compile step without re-running the parser. Intended target: `sys_bytecode_cache.blob` keyed by descriptor + source hash + metadata version.
- **Per-session module manager** — each `ibSession` owns `m_moduleManager : ibValueModuleManager*` and `m_lambdaRuntime : ibProcUnit*`. The configuration-level `m_moduleManager` singleton is accessed only by Designer / codeRunner sandbox.

---

## What Not To Do

- Do not add `#include` for wxWidgets headers in `backend.dll` source files — the backend must remain GUI-free
- Do not use raw `RunQueryWithResults(wxT("...%s..."), userInput)` for user-supplied values — use `ibPreparedStatement`
- Do not commit changes to `enterprise.sln` project GUIDs or global section entries unless you are adding/removing a project
- Do not define `NDEBUG` in Debug configurations
- Do not catch `const ibBackendException*` and swallow it silently

---

## AI Agents

Specialized agents for OES development tasks. Defined in `~/.claude/agents/oes-*.md`.

### Security Scanner (`oes-security-scanner`)
Checks code for vulnerabilities: SQL injection (string concatenation in queries instead of `ibPreparedStatement`), buffer overflows (strcpy/sprintf), hardcoded credentials, unsafe file access, memory leaks (missing RAII). Flags MD5 password hashing as critical.

### Code Reviewer (`oes-code-reviewer`)
Reviews code against C++17 standards and OES architecture rules: RAII, const-correctness, explicit constructors, cross-platform compatibility (MSVC/Clang/GCC), naming conventions (`ib` prefix, `m_` members), no GUI headers in backend.

### Test Writer (`oes-test-writer`)
Generates Google Test tests. Naming: `TEST(ClassName, Method_Condition_Expected)`. Uses MockDatabaseLayer for unit tests, real SQLite for integration tests. Handles throw-by-pointer exceptions.

### CMake Writer (`oes-cmake-writer`)
Generates and fixes CMakeLists.txt. Target-based approach, C++17, compatible with MSVC 2019+/Clang/GCC 9+. Handles wxWidgets submodule, optional DB drivers (OES_USE_FIREBIRD, etc.).

### System Architect (`oes-system-architect`)
Evaluates architecture, identifies coupling issues, circular dependencies, two-DLL separation violations. Proposes incremental refactoring plans.

### Performance Engineer (`oes-performance-engineer`)
Optimizes DB queries, bytecode interpreter (ibProcUnit), form rendering, memory usage. Profile before optimizing.

### Debugger (`oes-debugger`)
Diagnoses build errors (MSVC vs Clang), runtime crashes, logic bugs. Knows common OES pitfalls: circular includes, ibGuid conversion, empty catch blocks.

### Database Expert (`oes-database-expert`)
Schema design, query optimization, cross-DB compatibility (Firebird/PostgreSQL/SQLite/MySQL). All queries through ibPreparedStatement.

### Deployment Wizard (`oes-deployment-wizard`)
CI/CD, cross-platform builds (MSBuild + CMake), release management. PRs → develop, releases → master with tag.

### Code Refactorer (`oes-code-refactorer`)
Fixes MSVC-specific code for Clang/GCC, breaks circular includes, modernizes to C++17 where appropriate. One refactoring per PR.
