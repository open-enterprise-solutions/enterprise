# OES Enterprise — AI Context File

This file gives an AI assistant (Claude Code or similar) the context needed to work effectively in this repository without reading every source file.

---

## What This Project Is

**Open Enterprise Solutions (OES)** is a C++17 cross-platform low-code enterprise application platform. It lets developers define business applications through metadata (object types, forms, modules) and a built-in scripting language, without writing low-level code.

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
| Other databases | PostgreSQL, MySQL, ODBC (CMake `OES_USE_*` opt-in); SQLite is always embedded |
| License | LGPL 2.1 |

---

## Repository Layout

```
enterprise/
├── enterprise.sln           # MSBuild solution (13 projects + a Solution Items folder)
├── Common.props             # Shared output paths and macros
├── ConfigurationDefs.props  # Per-configuration preprocessor defines
├── CLAUDE.md                # This file
├── docs/
│   ├── ai-context.md         # READ FIRST if you are an AI generating metadata / scripts
│   ├── ARCHITECTURE.md
│   ├── BUILD.md
│   ├── ui-palette.md         # Interior-design palette — source of truth for UI colours
│   ├── uikit.md              # Custom-drawn UI engine (wxUniversal fork + Luna theme)
│   ├── data-composer.md      # L5 — declarative composition over the query language
│   └── configuration-compare.md  # Compare/Merge feature — walker, model, Apply paths
└── src/
    ├── 3rdparty/wxWidgets/  # Submodule (wxWidgets 3.3.2)
    └── engine/
        ├── backend/         # backend.dll  — engine, compiler, DB, metadata, debugger
        ├── frontend/        # frontend.dll (desktop UI) + wfrontend.dll (web) projects
        ├── enterprise/      # enterprise.exe (thick client)
        ├── wenterprise-server/ # web server (wes process)
        ├── designer/        # designer.exe  (IDE)
        ├── launcher/        # launcher.exe  (connection chooser)
        ├── daemon/          # daemon.exe    (background service)
        ├── codeRunner/      # codeRunner.exe
        └── simplePlugin/    # simplePlugin.dll (example)
```

---

## Key Architectural Decisions

### 1. ibDatabaseLayer Abstraction

All database access goes through the abstract `ibDatabaseLayer` interface (`src/engine/backend/databaseLayer/databaseLayer.h`). The five concrete drivers (Firebird, PostgreSQL, SQLite, MySQL, ODBC) implement this interface. Code everywhere uses `db_query->RunQuery(...)` or `db_query->PrepareStatement(...)`. Never access driver classes directly.

### 2. ibValue Universal Type

`ibValue` (`src/engine/backend/compiler/value.h`) is the single value type used for all script variables. The tag is the `ibValueTypes` enum (`backend_core.h`, `: unsigned char`): primitives `TYPE_EMPTY` (=0, the "undefined" value), `TYPE_BOOLEAN`, `TYPE_NUMBER`, `TYPE_DATE`, `TYPE_STRING`, `TYPE_NULL`; references `TYPE_REFFER` (owned, ref-counted) / `TYPE_CONST_REFFER` (non-owned, read-only); object kinds `TYPE_VALUE`, `TYPE_ENUM`, `TYPE_OLE`, `TYPE_FUNCTION` (lambda), `TYPE_ITERATOR`. Arithmetic and comparison operations have type-dispatched variants (the `TYPE_DELTA*` opcode offset scheme in `codeDef.h`).

`ibValueMetaObject` extends `ibValue`, meaning metadata objects (Catalog definitions, Document definitions, etc.) can be stored in and returned from script variables.

### 2a. ibNumber — exact-decimal lazy-grow

`ibNumber` (`src/engine/backend/fnumber.h`) is the numeric storage type used by `ibValue::m_fData`. It is **not** a typedef for ttmath::Big — that dependency was removed; the class is self-contained.

- `sizeof(ibNumber) == 8` always. Single tagged `uint64_t`: bit 0 = tag, bits [16:1] = exp10, bits [63:17] = 47-bit signed mantissa. Most values stay inline (immediate tier).
- Heap tier: `BigImpl { std::vector<uint32_t> limbs; bool negative; int32_t exp; }` — exact decimal, magnitude grows by demand. Supports 200+ fractional digits (1С-style precision).
- Self-contained: no ttmath dependency. Schoolbook Add/Sub/Mul + base-2 long-division Div live in `fnumber.cpp`. MSVC x86/x64 use `_addcarry_u32`/`_subborrow_u32` intrinsics; portable fallback elsewhere.
- **Immediate fast paths.** `+` / `-` / `*` / `/` and `Compare` short-circuit two immediate-INTEGER operands (`exp10 == 0`) through a single `int64` op — no `BigImpl`, no `10^30` inflate, no long division — via the private `TryImmInts` gate. They fire only when the result fits immediate (and, for `/`, the division is exact), so the result is **bit-identical** to the `BigImpl` path; exactness is preserved. Common integer arithmetic / comparison is a few ns; non-exact decimal division still pays the full exact long-division cost (inherent, not a regression).
- Buffer / wire: `wxMemoryBuffer GetBuffer()` plus `bool GetBuffer(ibWriterMemory&)` / `bool SetBuffer(const ibReaderMemory&)` — chunk-encapsulated I/O with internal `kIbNumberChunk` ID. Compact-zero encoding: zero produces 0-byte buffer, no allocation.
- 128-bit raw: `To128Bytes(uint8_t[16])` / `From128Bytes` for Firebird SQL_INT128 columns.
- Tests: `enterprise/tests/test_number.cpp` (gtest). Micro-benchmarks (DISABLED by default) live in `enterprise/tests/bench_runtime.cpp` — `RuntimeBench` (interpreter) / `NumberBench` (ibNumber) / `ParserBench` (compile), each printed next to a native-C++ baseline. Build Release and run:
  `oes_tests --gtest_also_run_disabled_tests --gtest_filter=*Bench*`.

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

Every class in the metadata and value system is identified by an `ibClassID` (`unsigned wxLongLong_t`, 64-bit). It is **KIND-TYPED**: the high byte is a class KIND (`ibClassKind` enum in `clsid.h`), the low 56 bits are the body. This lets stateless helpers — `IsReference(clsid)` / `IsObject` / `IsManager` / `IsEnum` / `IsControl` / `IsMetadata` / … — read a class's kind straight from the id, with **no `ibMetaData` lookup** (used heavily by L3 to classify columns by bit instead of pulling the metadata).

- **Static types**: body = `ib_clsid_hash(name)` (FNV-1a 64) masked to 56 bits; kind = the **registrar** (the `*_TYPE_REGISTER` macro family — `metadata_to_clsid` / `value_to_clsid` / `control_to_clsid` / `system_to_clsid` / `enum_to_clsid` / `context_to_clsid` / `primitive_to_clsid` / `picture_to_clsid`). The kind comes from WHERE a type is registered, **never** from the string prefix (a `"VL_TVROW"` registered via `SYSTEM_TYPE_REGISTER` is System-kinded). The legacy `"XX_YYY"` string is kept only as an opaque unique KEY for the body; PascalCase names (`"Catalog"`, `"Number"`, `"Button"`) feed the 2-arg macro form (clsid = `<kind>_to_clsid(hash(name))`).
- **Dynamic metaobject values**: body = the **metaID itself** (constructive, no hash → `(kind, metaID)` unique BY CONSTRUCTION); kind = the metatype (`reference_to_clsid(metaID)` / `object_to_clsid` / `manager_to_clsid` / `list_to_clsid` / … / `externalObject_to_clsid`, mirroring `ibCtorObjectMetaType`). The old `"R_42"` name-hash grammar is gone.
- `string_to_clsid()` is **removed** — every callsite uses a per-kind generator. `make_clsid(name, kind)` is the common entry; `make_clsid(name, ibClassKind_None)` is the escape for synthetic, unregistered ids (config-compare umbrellas, tool ids).

CLSIDs appear in serialised configuration files and the DB; the kind-typing changed every value, so the AOT cache version is bumped (`kAOTFormatVersion` = 16) and persisted CLSID blobs regenerate. Uniqueness: dynamic is constructive (impossible to collide); static is hash-bodied but collision is only possible WITHIN a kind among the tens of names there (negligible) and is caught by the registry's duplicate-clsid check. Tests: `tests/test_clsid.cpp`.

### 7. Metadata open/close — `ibMetaImage`

`ibMetaData` (`src/engine/backend/metaData.h`) holds an open configuration as a single runtime image: `std::shared_ptr<ibMetaImage> m_image`. **Its presence is the open state** — `IsConfigOpen()` is just `m_image != nullptr`. A `LoadGuard` RAII transaction creates the image at the start of a run and drops it (rolling the load back) if the run does not complete. The image owns the type-ctor registry (`ibCtorRegistry<ibCtorMetaValueType> m_factoryCtors`, which owns ctors via `shared_ptr`), the module storage, and the compile-value cache built by `CreateDesignerCache()` (designer kinds only; `nullptr` otherwise). Subclass accessors (`GetModuleStorage`, `GetCompileCache`, …) all guard `m_image`.

---

## Important Patterns

### Accessing the Database

```cpp
// Global macro — std::shared_ptr<ibDatabaseLayer>
db_query->RunQuery(wxT("INSERT INTO %s ..."), tableName);

ibPreparedStatement* stmt = db_query->PrepareStatement(wxT("SELECT * FROM %s WHERE id = ?"), table);
stmt->SetParamInt(1, id);
ibDatabaseResultSet* rs = stmt->RunQueryWithResults();
```

### Accessing Application State

```cpp
appData->GetAppMode();           // ibRunMode enum (static GetAppMode())
db_query;                        // std::shared_ptr<ibDatabaseLayer> (macro = GetDatabaseLayer())
appData->GetUserInfo();          // const ibUserInfo& — current logged-in user
activeMetaData->GetCommonMetaObject();  // root of the metadata tree (ibValueMetaObjectConfiguration*)
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
    metaFormObject,     // const ibValueMetaObjectFormBase* (creator), or nullptr
    ownerControl,       // ibBackendControlFrame* or nullptr
    sourceObject,       // ibSourceDataObject* or nullptr
    formGuid            // const ibUniqueKey& — wxNullUniqueKey to auto-generate
);
```

---

## Known Issues

### Password Hashing — PBKDF2-HMAC-SHA256

New passwords are hashed via `ibPasswordHash::Hash` (`src/engine/backend/utils/passwordHash.{hpp,cpp}`) using PBKDF2-HMAC-SHA256 at 600k iterations (OWASP 2023) with a 16-byte system-RNG salt, stored in PHC-style format `$pbkdf2-sha256$<iter>$<saltB64>$<hashB64>`. `Verify` additionally accepts legacy 32-hex MD5 hashes from pre-migration databases; callers use `NeedsRehash` + `Hash` to upgrade silently on successful login (see `ibApplicationData::AuthenticateUser`). MD5 stays in-tree for metadata integrity (`ibMD5::ComputeMd5`) — **never** reuse it for passwords.

Argon2id (OWASP #1, memory-hard) would be the stronger option but requires vendoring an external library; revisit when the threat model calls for it.

### Empty Catch Blocks

About 30 `catch (...) {}` sites exist in `backend.dll` — they live in
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

Output lands in `bin\<Platform>\<Configuration>\` — `Platform` is `Win32` (x86)
or `Win64` (x64), e.g. `bin\Win64\Release\`, `bin\Win32\Debug\`.

### CMake (macOS / Linux)

```bash
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --parallel 3   # cap parallelism on 16 GB RAM
```

DB driver options (all default OFF; SQLite is always embedded, no flag):
`OES_USE_FIREBIRD`, `OES_USE_POSTGRESQL`, `OES_USE_MYSQL`, `OES_USE_ODBC`.
See `docs/BUILD.md` for per-platform requirements.

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

## Configuration Serialization

Metadata is serialized through the format-agnostic **`ibDataNode`** tree
(`src/engine/backend/serialize/dataBuilder.h`). A metaobject contributes its
state into an `ibDataNode` tree; a pluggable **`ibFormatProvider`** then writes
that tree to bytes (and reads it back).

- **Entry points:** `ibMetaDataConfigurationBase::LoadConfigFromFile(fileName)` /
  `SaveConfigToFile(fileName)` (`metadataConfiguration.h`).
- **`ibBinaryProvider`** (`dataBuilder.{h,cpp}`) — the internal, owned binary
  format. This is the round-trip path (Write + Read).
- **`ibJsonProvider`** (`serialize/jsonProvider.{h,cpp}`) — JSON, **write-only**:
  `Write` emits JSON for diff/inspection; `Read` is a no-op (`TODO: JSON -> tree`).
  Do not rely on JSON import.
- The old XML/JSON config layer is **gone**: `metadataConfigurationXML.cpp` /
  `metadataConfigurationJSON.cpp` and the `SaveConfigToXML` / `LoadConfigFromXML`
  / `SaveConfigToJSON` / `LoadConfigFromJSON` methods no longer exist.

### What is serialized

All 11 business object types with: attributes (full type qualifiers), tabular
sections, forms (control tree + module code), object/manager modules, predefined
values, default form assignments, MetaDescription bindings (Owner, Generation,
RegisterRecord, ChartOfCharacteristicTypes, ChartOfAccounts), QuickChoice,
WriteMode, Periodicity, RegisterType, dimensions, resources, enum values. CLSID
stored as the 64-bit `ibClassID` hash.

### ibValue Serialization

`src/engine/backend/compiler/valueSerialization.cpp` — `DoSerialize`/`DoDeserialize` for primitive types (Boolean, Number, String, Date). Used for client-server data exchange.

`src/engine/backend/metadataSerialization.cpp` — `ibMetaData::Serialize`/`Deserialize` wraps values in OES Serialize format: `S: OES Serialize;;;C:<classType>;;;L:<length>;;;D:<data>;;;E: OES Serialize;;;`

---

## Compiler Quick Reference

- **Opcodes:** defined in `src/engine/backend/compiler/codeDef.h` as `OPER_*` enumerators. Call-family: `OPER_CALL` (stack-frame named call), `OPER_CALL_CLOSURE` (named call with heap-promoted frame — callee has an inner lambda capturing locals), `OPER_CALL_METHOD` (per-class method dispatched by name string), `OPER_CALL_LINQ` (universal pipeline op dispatched by `ibLinqMethod` enum id, no string lookup), `OPER_CALL_LAMBDA` (dynamic call — target read from a slot at runtime, must wrap an `ibValueFunction`). Lambda body fences `OPER_LFUNC` (the active materialiser) / `OPER_ENDLFUNC`. (`OPER_FUNC_PTR` is retired — `OPER_LFUNC` materialises the lambda value.)
- **Keywords:** 60, defined as `KEY_*` enumerators (`KEY_IF`=0 … `KEY_INTO`) in the same file — includes access modifiers (`Public`/`Private`/`Protected`), preprocessor (`#Define`/`#Ifdef`/…) and the LINQ block (`From`/`Where`/`Select`/`Join`/`Group`/…). The matching token strings are `s_listKeyWord[]` in `translateCode.cpp`, in lock-step index order with the enum.
- **Built-in functions:** ~93, registered in `ibSystemManager` (`src/engine/backend/system/systemManager.cpp`); count drifts as features land — grep `AppendFunc\|AppendProc` for the live total
- **Syntax modes:** VES (`If…Then…EndIf`, Visual-Basic-style with 1С/BSL mix) and CES (`if (…) { … }`, C-flavoured); both compile to the same bytecode. Mode is process-global on `ibCompileCode::SetCodeStyle()` / `GetCodeStyle()`. **CES is the default** for new configurations (2026-05-10); existing serialised configs preserve their stored Syntax. Wire token in metadata enum still reads `vbs` for back-compat — user-visible label is `ves`.
- **Anonymous functions:** `Function(args) ... EndFunction` and `Procedure(args) ... EndProcedure` (or CES `Function(args) { … }`) work as expressions — assignable to slots, callable through variables. Backed by `ibValueFunction` (inline class in `procUnit.cpp` near `ibValueIterator`, CLSID `VL_FUNC`). Lambda's compile-context return kind is `RETURN_LAMBDA_FUNCTION` / `RETURN_LAMBDA_PROCEDURE` (`compileCode.h`). Eval-in-lambda resolves outer frames via splice in `CompileExpression` (lambda-shim's `m_pppArrayList[1..]` → eval's `[2..]`). No closure capture yet — outer-function locals fail with "undefined identifier" at compile. See `docs/lambda.md`.
- **Debugger port:** 1650 (`defaultDebuggerPort` in `src/engine/backend/debugger/debugDefs.h`)

### Bytecode resolver (kind-driven, AOT-ready)

`ibByteCode` carries everything needed for cross-bc resolve, with no runtime dependency on the compile-context.

- `m_listVar`: `std::vector<ibByteCodeVarInfo>` — `ibVarKind` enum (`byteCode.h`): `Local / Export / External / Context / ContextProp / Protected`.
- `m_listFunc`: `std::vector<ibByteFunction>` — `ibFnKind` enum: `Local / Export / ContextMethod / Lambda / Protected` (the `Lambda` kind landed with the anonymous-function feature; cross-bc invisible, name-keyed lookups filter via `IsLambda()`).
- `m_listLocals` (per-function): same vector shape as `m_listVar`.
- Cross-table refs: `ContextProp / ContextMethod` carry `m_parentRef` indexing back into `m_listVar`.
- `ibByteBinder` reads `m_listVar` directly and binds slots whose kind ∈ `{External, Context}` (`IsBindRequired()`).
- Eval / watch expressions use `ibCompileEval` (in `procUnit.cpp`); `ibCompileCode::IsExpressionOnly()` and `GetEvalHostFunction()` are virtual hooks the eval class overrides.
- Descriptors expose `ExportNamesToHelper(helper, alias)` on `ibRuntimeModuleDataObject` to populate a value's helper from the bc's export entries.

See `docs/eval-scope-refactor.md` for the full architecture.

### Runtime infrastructure (landed)

- **Worker pool** — `ibWorkerPool` (`src/engine/backend/session/workerPool.h`) + headless implementation (`workerPoolHeadless.{h,cpp}`). Each session has a queue + an atomic "leased" flag (`workerPoolHeadless.h`); sessionless callers fall back to a `thread_local ibProcUnitState ts_fallbackPUState` in `session.cpp` (`ibSession::GetPUState`).
- **AOT bytecode cache** — `byteCodeAOT.cpp` serialises a compiled `ibByteCode` to a memory stream; deserialisation reverses the compile step without re-running the parser. Intended target: `sys_bytecode_cache.blob` keyed by descriptor + source hash + metadata version.
- **Per-session runtime image** — each `ibSession` owns `m_root : ibValuePtr<ibValueModuleManagerRuntimeConfiguration>` (built in `CreateRoot`; `GetManagerModule()`) and `m_lambdaRuntime : std::unique_ptr<ibProcUnit>` (wired to `m_root`'s procUnit on first `GetLambdaRuntime()`). Designer / codeRunner edit-time managers come from `GetEditModuleManager(metaData)` / `EditModuleManagerFor(metaData)`, kept separate from the per-session runtime root.

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
Generates Google Test tests. Naming: `TEST(ClassName, Method_Condition_Expected)`. Uses MockDatabaseLayer for unit tests, real SQLite for integration tests. Handles throw-by-value / catch-by-const-ref exceptions (see decision #5).

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
