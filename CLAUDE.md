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
| Build (cross-platform) | CMake — planned, `CMakeLists.txt` does not exist yet |
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
│   ├── ARCHITECTURE.md
│   └── BUILD.md
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

### 3. ibProcUnit Bytecode Interpreter

Scripts are compiled to bytecode by `ibCompileCode` and executed by `ibProcUnit`. This is a simple stack machine. Each compilation unit produces an `ibByteCode` containing an instruction array and a function table. `ibProcUnit::Execute()` dispatches on `ibByteUnit::m_numOper`.

The compiler is a single-pass recursive descent parser with a deferred-call-resolution second pass for forward function references.

### 4. Two-DLL Architecture

`backend.dll` has zero UI code. `frontend.dll` owns all wxWidgets code. Communication is through abstract C++ interfaces exported by `backend.dll`. This allows the backend to run headless (daemon, codeRunner, service mode).

### 5. Throw-By-Pointer Exception Pattern

Exceptions in the backend use the throw-by-pointer idiom inherited from wxWidgets:

```cpp
// throw
throw(new ibBackendInterruptException);

// catch
catch (const ibBackendException* err) {
    // handle or rethrow
    throw(err);
}
```

This is intentional and matches `ibBackendException::ProcessError` which calls `throw(err)`. Do not change exceptions to throw-by-value unless you update all catch sites.

### 5. Metadata CLSID System

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

### SQL Injection in appDataQuery.cpp

`src/engine/backend/appDataQuery.cpp` contains session management queries that format table names and datetime values directly into query strings using `wxString::Format` / `RunQueryWithResults(wxT("... '%s'"), value)`. Table names come from internal constants (`session_table`, `user_table`) so the injection surface is limited, but it is not safe against malicious database content. Future work: migrate all queries to `ibPreparedStatement`.

### MD5 Password Hashing

User passwords are stored and compared as MD5 hashes (`ibApplicationData::ComputeMd5()`). MD5 is cryptographically broken. This should be migrated to bcrypt or Argon2 before any production deployment.

### NDEBUG Defined in Debug|Win32

`src/engine/backend/backend.vcxproj` defines `NDEBUG` in the `Debug|Win32` configuration, disabling all `wxASSERT` checks in 32-bit debug builds. The `Debug|x64` configuration is not affected. Track this as a build configuration bug.

### Empty Catch Blocks

Some catch sites in the backend discard exceptions silently:

```cpp
catch (const ibBackendException*) {
    // nothing — error swallowed
}
```

These are located primarily in the compiler and metadata loading paths. They mask real errors. When adding new code, always at minimum log or rethrow.

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

### CMake (future — not yet available)

```bash
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --parallel
```

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

The 8 business object types and their C++ classes:

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

---

## Compiler Quick Reference

- **Opcodes:** 66, defined in `src/engine/backend/compiler/codeDef.h` as `OPER_*` enumerators
- **Keywords:** 44, defined as `KEY_*` enumerators in the same file
- **Built-in functions:** 88, registered in `ibSystemManager` (`src/engine/backend/system/systemManager.cpp`)
- **Syntax modes:** VBS-style (`If…Then…EndIf`) and CES-style abbreviations; both compile to the same bytecode
- **Debugger port:** 1650 (`defaultDebuggerPort` in `src/engine/backend/debugger/debugDefs.h`)

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
