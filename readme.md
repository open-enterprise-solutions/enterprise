# Open Enterprise Solutions (OES)

OES is a source-available platform for building line-of-business applications — stock, accounting,
payroll, sales, whatever a business keeps books on — written in C++17. You describe the business
in **metadata** (catalogs, documents, registers, charts of accounts and of calculation types), write
its rules in a built-in scripting language, and read its data with a **query language and a report
composer**. The platform turns the description into database tables, forms and reports, and keeps
the database in step with the description as it changes.

It ships with everything that loop needs: a designer, a compiler and bytecode interpreter, a remote
debugger, a multi-database layer, a job manager, a web server — and a built-in **MCP server**, so an
AI assistant can read, build and check a configuration alongside the developer, through the same
doors the developer uses.

---

## What you build with it

| Kind | Metadata objects |
|---|---|
| Reference data | Catalog (flat or hierarchical, with predefined items) · Enumeration · Constant · Chart of characteristic types |
| Operations | Document — written, posted into registers, reposted, marked for deletion |
| Registers | **Information** (periodic, with slices) · **Accumulation** (balances and turnovers, trigger-maintained totals) · **Accounting** (chart of accounts, double entry, correspondence, analytics addressed by kind) · **Calculation** (payroll: displacement, base, recalculation) |
| Charts | Chart of accounts · Chart of calculation types (displacing, base and leading types) |
| Processing and reports | Data processor · Report with its own composers · external data processors and reports |
| Configuration-wide | Common modules · common forms, templates and commands · common attributes · session parameters · roles · sections (the navigation panel) · languages · pictures · scheduled jobs |

---

## Key features

- **Query language** — `SELECT` over the business objects and over the registers' virtual tables
  (`Balance`, `Turnovers`, `BalanceAndTurnovers`, `SliceLast` / `SliceFirst`, `DrCrTurnovers`,
  `RecordsWithAccountDimensions`, `ActualActionPeriod`, `ScheduleData`, `Recalculation`); nested queries, temporary
  tables and query packages, `TOTALS BY … HIERARCHY`, `CAST`, `VALUE`, parameters that keep their
  type. A query runs as SQL on the server wherever the engine can prove it may, and in memory
  otherwise — with the same answer.
- **Report composer** — groupings, totals, cross tables, drill-down from a cell to the records
  behind it, parameters, and the user's own saved settings and variants. Output
  to a spreadsheet document that prints, and exports to **Excel (`.xlsx`, read and write)** and
  **Word (`.docx`, write)**.
- **Scripting language** — two dialects over one compiler (**CES**, C-flavoured, the default; **VES**,
  keyword-fenced), procedures and functions, lambdas with closure capture, `try … except`,
  multi-line strings for query texts, **LINQ** (`from … where … join … group by … orderby … select`)
  compiled into the bytecode, 94 built-in functions and 7 procedures. Bytecode of 82 opcodes, kept
  in an ahead-of-time cache so a module compiles once per configuration version.
- **Designer** — metadata tree, form designer (drag a field onto a form and a bound control
  appears), code editor with IntelliSense, a query constructor that edits the query inside the
  literal the caret is in, a spreadsheet template editor, configuration compare, and a git panel.
  Every property carries its own help text.
- **Debugger** — remote, over TCP (port 1650): breakpoints with conditions, stepping, watches,
  evaluation in the stopped frame; the designer attaches to a desktop application or to the web
  server.
- **Access control** — rights per object and operation, folded across a user's roles. A role either
  *permits* (adds rights) or *restricts* (subtracts them, whatever the others grant), so a data
  separator is declared once instead of copied into every role. **Row-level security** is code in the
  role (`OnAccessRead` / `OnAccessWrite`) that narrows what a query may read or write — fail-closed.
- **Schema management** — the configuration is applied to the database by a diff; a rehearsal shows
  the exact DDL before anything moves; on Firebird a two-phase apply compensates its first commit if
  the second fails, so a refused apply leaves the database where it was.
- **Jobs** — scheduled and background jobs in sessions of their own, with a cross-process claim so a
  job runs once across a cluster; the platform's own housekeeping (totals folding and verification,
  Firebird sweep and backup) runs on the same manager.
- **AI access over MCP** — an MCP server inside the designer, 114 tools across the platform: read and
  edit metadata, forms and modules; apply the configuration with a rehearsal; run code on the
  application; ask the data questions and compose reports; drive the debugger; read and write the
  registration journal. Protected by a token, listening on loopback by default; every call is
  journalled.
- **Databases** — Firebird (embedded, shipped with the distribution) and PostgreSQL for production;
  ODBC; SQLite for tests and logging.
- **Localisation** — the interface in English, Russian and Ukrainian; every caption of a
  configuration can be written per language.
- **Web client — in progress.** `wenterprise-server` serves the same forms to a browser. Layout,
  commands, navigation, text fields, check boxes and toolbars work today; tables and reference
  pickers are the next controls to arrive, so a full document form does not assemble in the browser
  yet.

### Measured

On a copy of a 40 000-employee payroll base (Release, x86): a month's payroll reposted in about
**20 s**, the payroll sheet report built in about **4 s**.

---

## Screenshots

> Screenshots will be added here. Contributions welcome.

![Designer overview](https://github.com/user-attachments/assets/8513361c-bbc3-44bb-b07c-776202940d8b)

---

## Quick Start

### Windows

1. Install [Visual Studio 2019 or 2022](https://visualstudio.microsoft.com/) with the **Desktop development with C++** workload.
2. Clone the repository and initialise the wxWidgets submodule:
   ```cmd
   git clone https://github.com/open-enterprise-solutions/enterprise.git
   cd enterprise
   git submodule update --init --recursive
   ```
3. Open `enterprise.sln` in Visual Studio.
4. Select configuration `Debug|Win32` or `Release|x64`.
5. Build the solution (`Ctrl+Shift+B`). Binaries are placed in `bin\<Platform>\<Configuration>\`
   (`Win32` or `Win64`).
6. Run `designer.exe` to build a configuration, `enterprise.exe` to work in it — or `launcher.exe`
   to pick a base first.

### macOS

> The CMake build lives at `CMakeLists.txt` (repo root, CMake ≥ 3.20).

```bash
# Install dependencies
brew install cmake wxwidgets firebird-client postgresql

# Clone and initialise submodules
git clone https://github.com/open-enterprise-solutions/enterprise.git
cd enterprise
git submodule update --init --recursive

# Configure and build
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j$(sysctl -n hw.logicalcpu)
```

### Linux (Ubuntu / Debian)

> The CMake build lives at `CMakeLists.txt` (repo root, CMake ≥ 3.20).

```bash
# Install dependencies
sudo apt update
sudo apt install -y build-essential cmake libwxgtk3.2-dev \
    libfirebird-dev libpq-dev libsqlite3-dev

# Clone and initialise submodules
git clone https://github.com/open-enterprise-solutions/enterprise.git
cd enterprise
git submodule update --init --recursive

# Configure and build
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j$(nproc)
```

---

## Build Instructions

### Windows — MSBuild

```cmd
# From a Developer Command Prompt for VS
msbuild enterprise.sln /p:Configuration=Release /p:Platform=x64 /m
```

Output: `bin\Win64\Release\`

### All Platforms — CMake

The top-level `CMakeLists.txt` (CMake ≥ 3.20) builds every target. DB drivers opt in — `OES_USE_FIREBIRD`, `OES_USE_POSTGRESQL`, `OES_USE_ODBC` (all default OFF); SQLite is always embedded, no flag:

```bash
cmake -B build \
      -DCMAKE_BUILD_TYPE=Release \
      -DwxWidgets_ROOT_DIR=src/3rdparty/wxWidgets
cmake --build build --parallel
```

The unit tests (Google Test) build with the same tree: `-DBUILD_TESTING=ON`.

### wxWidgets Submodule

wxWidgets 3.3.2 lives at `src/3rdparty/wxWidgets` as a git submodule. After cloning:

```bash
git submodule update --init --recursive
```

---

## Project Structure

```
enterprise/
├── enterprise.sln            # MSBuild solution (10 C++ projects)
├── CMakeLists.txt            # the cross-platform build, tests included
├── Common.props              # Shared MSBuild properties (paths, platforms)
├── ConfigurationDefs.props   # Preprocessor definitions per configuration
├── LICENSE.md                # PolyForm Noncommercial 1.0.0 (source-available)
├── NOTICE.md                 # third-party licenses, the wx fork, the LGPL past
├── locale/                   # interface translations (ru, uk)
├── tests/                    # Google Test suites (built by CMake)
├── docs/                     # how we work, build, portability, architecture — start with docs/README.md
│   └── private/              # PRIVATE submodule (design docs) — resolves for members of the
│                             # organisation only and is empty for everyone else. The build never
│                             # needs it, so links to docs/private/… will not open for you.
└── src/
    ├── 3rdparty/
    │   └── wxWidgets/        # Git submodule — wxWidgets 3.3.2
    └── engine/
        ├── backend/          # Core engine DLL
        │   ├── compiler/     # Lexer, parser, bytecode, interpreter, LINQ
        │   ├── query/        # The query language: parse, lower, render to SQL, run in memory
        │   ├── composition/  # The report composer
        │   ├── calculation/  # The calculation register's engine (displacement, base, recalculation)
        │   ├── databaseLayer/# DB abstraction + drivers (Firebird, PostgreSQL, SQLite, ODBC)
        │   ├── metaCollection/  # Metadata: business objects, registers, charts, common objects
        │   ├── mcp/          # The MCP server and its tools
        │   ├── job/          # Scheduled and background jobs
        │   ├── session/      # Sessions, their registry and worker pools
        │   ├── lock/         # Record locks, cluster-wide
        │   ├── sheetFormat/  # Spreadsheet import and export (.xlsx, .docx)
        │   ├── debugger/     # TCP debug server/client
        │   ├── moduleManager/
        │   ├── propertyManager/
        │   └── system/       # Built-in functions and values
        ├── frontend/         # UI DLL (wxWidgets controls, form renderer)
        │   ├── visualView/   # Forms and their controls
        │   ├── web/          # The same controls for the browser (wfrontend)
        │   ├── docView/      # Document/view framework wrappers
        │   └── win/          # Editors, dialogs and custom widgets
        ├── enterprise/       # Enterprise runtime executable
        ├── designer/         # Designer/IDE executable
        ├── wenterprise-server/ # Web server (wes process)
        ├── launcher/         # Launcher (connection chooser)
        ├── daemon/           # Background service
        ├── codeRunner/       # Script runner
        └── simplePlugin/     # Example plugin
```

---

## Technology Stack

| Layer | Technology |
|---|---|
| Language | C++17 |
| GUI framework | wxWidgets 3.3.2 |
| Primary database | Firebird (embedded) |
| Production alternative | PostgreSQL |
| Other databases | ODBC; SQLite (tests + logging) |
| AI access | Model Context Protocol (Streamable HTTP) |
| Build (Windows) | MSBuild / Visual Studio 2019+ |
| Build (cross-platform) | CMake ≥ 3.20 — `CMakeLists.txt` at repo root (macOS / Linux) |
| Tests | Google Test, run in CI (GitHub Actions) |
| License | PolyForm Noncommercial 1.0.0 — source-available, not open source |

---

## Contributing

Contributions are welcome — see [CONTRIBUTING.md](CONTRIBUTING.md) for what a change should
look like and, more importantly, for what sending one means.

The short version: branch from `develop` and pull-request into it; match the code around you;
say **why** in the commit message, because the diff already says what. And because this
project is licensed commercially as well as noncommercially, opening a pull request includes a
grant that lets us sublicense your contribution — you keep your copyright, we get the right to
sell what contains it. That paragraph is the one worth reading in full before you send
anything.

Bug reports and feature requests are welcome as GitHub Issues.

---

## License

OES is **source-available, not open source**, under the
[PolyForm Noncommercial License 1.0.0](LICENSE.md). Copyright is held by Open Enterprise
Solutions.

**Free, and meant to be used:** clone it, build it, break it, change it, run it on your own
machine, write configurations for it, teach a course from it, write a thesis about it, publish
what you learned. No notification, no permission, no explanation owed to anyone.

**Also free: evaluation, for a business too.** A company or an implementer may install it and
run it to decide whether to build on it — trial configurations, test data, its own people.
Live work is not evaluation.

**Needs a license from us:** earning from it. Running it in a business, providing a service
with it, shipping it inside something you are paid for — and, said outright because it is what
these sources are most likely to be taken for, forking it to ship a rival platform or lifting a
piece of it (the query engine, the composition and reporting engine, the metadata layer) into a
product of your own.

**A commercial license is for a build we release** — we answer for it, and it carries the
license check. The platform as you build it yourself may be used for noncommercial and
evaluation purposes: to study it, or to decide whether to use it, for example.

**Under your own brand** — an implementer that takes the platform to its customers under its
own name, builds it itself and licenses it to them on its own terms — the platform is licensed
separately, case by case. It stays ours all the same: we develop it and release its patches,
and the implementer decides whether and when to take them. Its own features on top — what it
sells besides the brand — are its own, because it wrote them. We own the land; what you cook
on it is yours.

Two things the terms above do not cover, both in [NOTICE.md](NOTICE.md): releases up to
2026-08-22 went out under the **LGPL 2.1** and stay available under it — a license already
granted cannot be withdrawn — and the wxWidgets-derived widget sources remain under the
**wxWindows Library Licence**.
