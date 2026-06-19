# 01. Project Structure

## Repository organization

All projects live in a **single GitHub organization** with access control via **Teams**.

<!-- TODO: insert link to organization -->

### Organization structure

```
Organization (GitHub Org)
│
├── Teams (access management):
│   ├── oes-core          — access to core platform repositories
│   ├── oes-plugins       — access to plugin and extension repositories
│   ├── devops            — access to all repositories
│   └── management        — read-only across all
│
├── OES platform repositories:
│   ├── enterprise        — main C++ platform repository (this one)
│   ├── oes-plugins       — plugin and extension repository
│   └── oes-docs          — user-facing documentation
│
├── Shared:
│   ├── engineering-playbook — this directory (development standards)
│   └── shared-cmake         — shared CMake modules and toolchain files
```

### Teams and access rights

| Team | Own repos | Other repos | Shared repos | Role |
|------|-----------|-------------|--------------|------|
| oes-core | Write | — | Read | Platform core developers |
| oes-plugins | — | Write | Read | Plugin developers |
| devops | Admin | Admin | Admin | Infrastructure, CI/CD |
| management | Read | Read | Read | Management, overview |

**Advantages of Teams:**
- A person only sees their team's repositories
- Permissions are configured at the team level, not per repo
- Easy to add/remove people
- Audit log at the organization level
- Separate secrets and deploy keys per team

### Principle: one product = one repository

Each independent component or extension of the platform lives in a separate repository. This simplifies builds, access management, and CI/CD.

**Repository naming:** `kebab-case`, descriptive, with a project prefix where needed.

```
oes-enterprise         — not "OESCore" or "oes_enterprise"
oes-plugin-reporting   — not "ReportingPlugin"
engineering-playbook   — not "DevGuidelines"
```

---

## Standard project file layout

### Monolithic C++ repository (current structure)

OES Enterprise is a monolithic desktop product. Everything lives in one repository.

```
enterprise/
├── src/                          — Source code
│   └── engine/                   — Platform engine
│       ├── backend/              — Platform core (business logic, compiler, DB)
│       │   ├── appData.cpp       — ibApplicationData: authentication, session (AuthenticateUser())
│       │   ├── appDataQuery.cpp  — Session and user queries
│       │   ├── metadataConfiguration.cpp — Metadata configuration management
│       │   ├── compiler/         — Built-in script engine
│       │   │   ├── compileCode.cpp  — ibCompileCode, ibTranslateCode
│       │   │   ├── procUnit.cpp     — ibProcUnit: bytecode interpreter
│       │   │   └── value.h          — ibValue, ibValueTypes (ibNumber lives in fnumber.h)
│       │   ├── databaseLayer/    — DBMS abstraction
│       │   │   ├── databaseLayer.h        — ibDatabaseLayer (base class)
│       │   │   ├── firebird/              — ibDatabaseLayerFirebird
│       │   │   ├── postgres/              — ibDatabaseLayerPostgres
│       │   │   ├── sqlite/                — ibDatabaseLayerSQLite
│       │   │   ├── mysql/                 — ibDatabaseLayerMySQL
│       │   │   └── odbc/                  — ibDatabaseLayerODBC
│       │   ├── metaCollection/   — Object metadata
│       │   │   └── partial/
│       │   │       └── commonObjectQuery.cpp — CRUD queries, CreateAndUpdateTableDB()
│       │   └── debugger/
│       │       └── debugServer.cpp  — ibDebuggerServer
│       ├── frontend/             — UI components (wxWidgets)
│       │   └── visualView/
│       │       └── ctrl/         — ibValueForm, ibValueFrame, ibValueTextCtrl,
│       │                           ibValueButton, ibValueModelTableBox
│       ├── designer/             — Low-code form and report designer
│       │   └── mainApp.cpp       — Designer entry point
│       └── enterprise/           — Runtime environment
│           └── mainApp.cpp       — Application entry point
├── tests/                        — Google Test tests
│   ├── unit/                     — Unit tests (mirror src/ structure)
│   │   ├── backend/
│   │   ├── designer/
│   │   └── frontend/
│   └── integration/              — Integration tests
├── docs/                         — Documentation
│   ├── engineering-playbook/     — Development standards (this directory)
│   ├── architecture/             — Architectural descriptions
│   │   ├── overview.md
│   │   └── adr/                  — Architecture Decision Records
│   └── api/                      — Public API documentation
├── cmake/                        — CMake modules and helper scripts
│   ├── FindFirebird.cmake
│   ├── FindwxWidgets.cmake
│   └── CompilerOptions.cmake
├── scripts/                      — Automation scripts (build, deploy)
│   ├── build.ps1                 — PowerShell build for Windows
│   └── package.ps1               — Distribution packaging
├── third_party/                  — Vendored dependencies (if not via vcpkg/conan)
├── resources/                    — Application resources
│   ├── icons/
│   ├── strings/                  — Localization strings
│   └── templates/                — Templates
├── .github/
│   ├── workflows/                — GitHub Actions CI/CD
│   │   ├── build.yml
│   │   └── test.yml
│   └── PULL_REQUEST_TEMPLATE.md
├── CMakeLists.txt                — Root CMake (macOS/Linux; Windows uses MSBuild)
├── enterprise.sln                — Visual Studio Solution (primary build system, Windows)
├── .gitignore
├── CLAUDE.md
├── .claude.json
└── README.md
```

### Organization of header and source files

In C++ each logical module consists of a `.h` / `.cpp` pair. Within a subsystem:

```
src/engine/backend/databaseLayer/
├── databaseLayer.h          — ibDatabaseLayer (base class), ibPreparedStatement, ibDatabaseResultSet
├── databaseLayer.cpp        — implementation
├── firebird/
│   ├── databaseLayerFirebird.h   — ibDatabaseLayerFirebird
│   └── databaseLayerFirebird.cpp
└── postgres/
    ├── databaseLayerPostgres.h   — ibDatabaseLayerPostgres
    └── databaseLayerPostgres.cpp
```

**Rule:** the header contains the **interface** (declarations), the `.cpp` the **implementation**. Definitions in headers are only allowed for templates and `inline` functions.

---

## Mandatory files

Every repository **must** contain:

### 1. README.md

Project description, how to build, how to run. Details in [04-documentation.md](./04-documentation.md).

```markdown
# OES Enterprise

Cross-platform enterprise low-code/no-code platform for building business applications.

## Tech Stack
- **Language:** C++17
- **GUI:** wxWidgets 3.3.2
- **Build:** MSBuild (VS 2017+), transitioning to CMake
- **Databases:** Firebird (primary), PostgreSQL, SQLite, MySQL, ODBC
- **Tests:** Google Test
- **License:** LGPL 2.1

## Prerequisites

- Visual Studio 2017+ (with Desktop C++ workload)
- wxWidgets 3.3.2 (built libraries)
- Firebird 4.x (for development with a local DB)
- CMake 3.20+ (for the new build system)

## Building

### MSBuild (current path)
```
Open enterprise.sln in Visual Studio
Choose Debug or Release configuration
Build → Build Solution (Ctrl+Shift+B)
```

### CMake (transitional target)
```bash
cmake -B build -DCMAKE_BUILD_TYPE=Debug
cmake --build build
```

## Running Tests
```bash
cd build
ctest --output-on-failure
```

## Project Structure
Brief overview of key directories.
```

### 2. CLAUDE.md

Context file for AI agents (Claude Code, Claude Desktop). This is the single most important file for working effectively with AI — it replaces hours of explanations at the start of every new session.

**CLAUDE.md structure:**

```markdown
# CLAUDE.md — OES Enterprise

## Project Overview
Open Enterprise Solutions (OES) — a C++ cross-platform low-code/no-code platform
for building business applications. Lets end users design forms, reports,
and business logic without writing code.

## Tech Stack
- Language: C++17
- GUI: wxWidgets 3.3.2
- Build: MSBuild (VS 2017+), transitioning to CMake 3.20+
- Databases: Firebird 4.x (primary), PostgreSQL, SQLite, MySQL, ODBC
- Tests: Google Test (being introduced)
- CI/CD: GitHub Actions (being introduced)
- Platform: Windows primary, cross-platform goal
- License: LGPL 2.1

## Project Structure
- src/engine/backend/           — platform core, compiler, DB abstraction
  - compiler/compileCode.cpp    — ibCompileCode, ibTranslateCode
  - compiler/procUnit.cpp       — ibProcUnit (bytecode interpreter)
  - compiler/value.h            — ibValue, ibValueTypes
  - compiler/fnumber.h          — ibNumber (self-contained exact-decimal, no ttmath)
  - appData.cpp                 — ibApplicationData (authentication, AuthenticateUser())
  - appDataQuery.cpp            — session and user queries
  - databaseLayer/              — ibDatabaseLayer + Firebird/Postgres/SQLite/MySQL/ODBC drivers
  - metaCollection/partial/commonObjectQuery.cpp — CRUD, CreateAndUpdateTableDB()
  - metadataConfiguration.cpp   — configuration management
  - debugger/debugServer.cpp    — ibDebuggerServer
- src/engine/frontend/          — wxWidgets UI components
  - visualView/ctrl/            — ibValueForm, ibValueFrame, ibValueTextCtrl,
                                  ibValueButton, ibValueModelTableBox
- src/engine/designer/          — visual form and report designer
  - mainApp.cpp                 — designer entry point
- src/engine/enterprise/        — runtime environment
  - mainApp.cpp                 — application entry point
- tests/                        — Google Test (unit + integration)
- docs/                         — documentation, ADR, playbook

## Key Patterns
- RAII for all resources: ibTransactionGuard (transactions), file descriptors, GDI objects
- std::unique_ptr / std::shared_ptr — no raw owning pointers
- ibPreparedStatement with SetParamString/SetParamInt — parameterized queries
- Observer pattern via wxWidgets events
- ibApplicationData — application-level singleton (authentication, session)
- ibBackendCoreException / ibBackendInterruptException — exception hierarchy

## Build Commands
```bash
# MSBuild (Windows — primary build system)
msbuild enterprise.sln /p:Configuration=Debug /p:Platform=x64

# CMake (macOS/Linux — built separately)
cmake -B build -DCMAKE_BUILD_TYPE=Debug -DwxWidgets_ROOT_DIR=/usr/local
cmake --build build --config Debug

# Run tests
ctest --test-dir build --output-on-failure
```

## Database
- Primary DBMS: Firebird 4.x
- Base class: ibDatabaseLayer; implementations ibDatabaseLayerFirebird, ibDatabaseLayerPostgres, etc.
- Parameterized queries are mandatory: ibPreparedStatement + SetParamString/SetParamInt — SQL concatenation is forbidden
- Transactions through ibTransactionGuard (RAII wrapper, commonObject.h)
- Schema is updated via CreateAndUpdateTableDB() in commonObjectQuery.cpp

## Business Rules
- Documents cannot be physically deleted, only marked as deleted
- All DB operations are logged
- Plugins are loaded from the plugins/ directory next to the exe

## Current State
Migration from MSBuild to CMake. Adoption of Google Test. Setting up GitHub Actions CI.
Known technical debt: [link to issues]

## Conventions
- Commits: type: description (in English)
- Branches: feature/*, fix/*, hotfix/*
- Classes: PascalCase, methods: camelCase, constants: UPPER_SNAKE_CASE
- Files: lowercase camelCase for OES classes (databaseLayer.h, compileCode.cpp), lowercase for utilities
- Namespace: oes::core, oes::designer, oes::runtime

## Do NOT
- Do not use raw owning pointers (new without immediate assignment to a smart ptr)
- Do not use exceptions in destructors
- Do not mix Cyrillic into code identifiers
- Do not hardcode file paths or database connection strings
- Do not commit .env files or configs with real credentials
- Do not disable compiler warnings without a comment explaining why
```

### 3. .claude.json

Configuration for Claude Code — MCP (Model Context Protocol) connections and settings.

```json
{
  "mcpServers": {
    "filesystem": {
      "command": "npx",
      "args": ["-y", "@modelcontextprotocol/server-filesystem", "/path/to/enterprise"]
    },
    "github": {
      "command": "npx",
      "args": ["-y", "@modelcontextprotocol/server-github"],
      "env": {
        "GITHUB_TOKEN": "ghp_xxx"
      }
    }
  }
}
```

**Important:** `.claude.json` may contain tokens for local work. Add it to `.gitignore` if it has real credentials. A template without credentials can be committed as `.claude.json.example`.

### 4. .gitignore

Standard `.gitignore` for C++ projects on Visual Studio + CMake:

```gitignore
# Visual Studio
*.user
*.suo
*.sdf
*.opensdf
*.VC.db
*.VC.opendb
.vs/
ipch/

# Build output (MSBuild)
Debug/
Release/
x64/
Win32/
*.obj
*.pdb
*.ilk
*.exp
*.lib
*.dll
*.exe

# Build output (CMake)
build/
cmake-build-*/
CMakeCache.txt
CMakeFiles/
cmake_install.cmake
CTestTestfile.cmake
_deps/

# Dependency managers
vcpkg_installed/
conan_cache/

# IDE
.vscode/
.idea/
*.swp
*.swo

# OS
.DS_Store
Thumbs.db

# Logs
*.log
logs/

# Test results
Testing/
test-results/

# Keys and certificates
*.pem
*.key
*.crt

# Local configuration with credentials
.claude.json
*.env
local.config

# Compiled resources
*.res
```

---

## File and directory naming

**Classes and headers:** OES uses an `ib` prefix (IntelBase) without PascalCase separation. The file name matches the class name:
```
databaseLayer.h / databaseLayer.cpp         — ibDatabaseLayer (base class)
databaseLayerFirebird.h / .cpp              — ibDatabaseLayerFirebird
compileCode.h / compileCode.cpp             — ibCompileCode, ibTranslateCode
value.h                                     — ibValue, ibValueTypes enum (ibNumber in fnumber.h)
```

**Utilities and helpers:** `snake_case` or `camelCase` (be consistent within a module).
```
string_utils.h
date_helpers.cpp
```

**Directories:** `lowercase` or `kebab-case`.
```
src/core/database/
src/gui/controls/
docs/engineering-playbook/
```

**Bad:**
```
Src/Core/Database/     — uppercase letters in directories
databasemanager.h      — does not reflect the class name
DB_Manager.h           — mixed styles
```

---

## Creating a new module — checklist

1. Create a directory in `src/engine/backend/` (backend) or `src/engine/frontend/` (UI) depending on purpose
2. Define the interface through a pure virtual class (e.g. `INotification.h`) — if the module is extensible
3. Implement the `Notification.h` / `Notification.cpp` class
4. Add the new files to `CMakeLists.txt` (macOS/Linux) or the `enterprise.sln` project (Windows/MSBuild)
5. Add unit tests in `tests/unit/backend/notifications/` (or `frontend/`)
6. Update `CLAUDE.md` — add a description of the new module
7. Create `config.ini.example` (or update the existing one) if the module introduces new configuration parameters
8. First commit: `feat: add notifications module skeleton`
9. When public interface changes — update `docs/architecture/`

---

## Namespace convention

OES classes use the `ib` (IntelBase) prefix and are not wrapped in a C++ namespace. This is a historical project convention:

```cpp
// Real platform classes — ib prefix, no namespace
class ibDatabaseLayer { ... };        // src/engine/backend/databaseLayer/
class ibApplicationData { ... };      // src/engine/backend/appData.cpp
class ibCompileCode { ... };          // src/engine/backend/compiler/
class ibValueForm { ... };            // src/engine/frontend/visualView/ctrl/
class ibTransactionGuard { ... };     // src/engine/backend/ (RAII for transactions)
class ibPreparedStatement { ... };    // src/engine/backend/databaseLayer/
```

For new test and helper modules, the `oes` namespace is acceptable:
```cpp
namespace oes::tests {
    // test factories, helpers
}
```

Third-party libraries (wxWidgets, Google Test) are used as-is.
