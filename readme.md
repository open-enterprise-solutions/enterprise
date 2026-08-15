# Open Enterprise Solutions (OES)

OES is an open-source, cross-platform low-code enterprise application platform written in C++17, conceptually similar to 1C:Enterprise. It provides a fully integrated environment — compiler, bytecode interpreter, visual form designer, multi-database abstraction layer, and remote debugger — that allows developers to build line-of-business applications using a built-in scripting language with two syntax modes (VES, the Visual-Basic-flavoured legacy dialect, and CES, the C-flavoured default for new configurations), a rich set of 98 built-in globals (92 functions + 6 procedures), and 11 first-class business-object types (Catalog, Document, Enumeration, Constant, InformationRegister, AccumulationRegister, AccountingRegister, ChartOfAccounts, ChartOfCharacteristicTypes, DataProcessor, Report — AccountingRegister's read path is currently non-functional, see [docs/ROADMAP.md](docs/ROADMAP.md) §4.1).

---

## Key Features

- **Integrated designer** — metadata tree editor, form builder, code editor with syntax highlighting and autocomplete
- **Bytecode compiler** — two-pass compiler (lexer → parser → bytecode) producing 75-opcode bytecode (`compiler/codeDef.h`); supports procedures, functions, modules, regions, lambdas with closure capture, and preprocessor directives (`#ifdef`/`#define`)
- **Visual form system** — 24 registered control types (TableBox, TextBox, ChartBox, GridBox, Notebook, ToolBar, sizers, etc.) rendered through wxWidgets; forms are described in metadata and instantiated at runtime
- **Multi-database back end** — Firebird (primary, embedded shipped with the distribution), PostgreSQL, ODBC; SQLite is embedded for tests and logging rather than production; unified `ibDatabaseLayer` API across all drivers
- **Remote TCP debugger** — client/server architecture over TCP (default port 1650); supports breakpoints, step-over, step-into, variable inspection, tooltips, and live code patching
- **Session management** — multi-user sessions tracked in the system database; launcher, daemon, designer, enterprise, and codeRunner modes
- **Role-based access control** — access rights on objects and operations defined in the metadata configuration
- **LGPL 2.1 license** — can be embedded in proprietary products under the LGPL terms

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
5. Build the solution (`Ctrl+Shift+B`). Binaries are placed in `bin\<Platform>\<Configuration>\`.
6. Run `enterprise.exe` or `designer.exe` from that folder.

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
    libfirebird-dev libpq-devlibsqlite3-dev

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
├── Common.props              # Shared MSBuild properties (paths, platforms)
├── ConfigurationDefs.props   # Preprocessor definitions per configuration
├── LICENSE.md                # LGPL 2.1
├── docs/
│   ├── ARCHITECTURE.md       # Architecture deep-dive
│   ├── BUILD.md              # Detailed build instructions
│   ├── devops-playbook/
│   └── engineering-playbook/
└── src/
    ├── 3rdparty/
    │   └── wxWidgets/        # Git submodule — wxWidgets 3.3.2
    └── engine/
        ├── backend/          # Core engine DLL (compiler, DB, metadata, debugger)
        │   ├── compiler/     # Lexer, parser, bytecode, interpreter (ibCompileCode, ibProcUnit)
        │   ├── databaseLayer/# DB abstraction + 4 drivers (Firebird, PG, SQLite, ODBC)
        │   ├── debugger/     # TCP debug server/client
        │   ├── metaCollection/  # Business object metadata classes
        │   │   └── partial/  # Catalog, Document, Enumeration, Constant, Registers, DataProcessor, Report
        │   ├── moduleManager/
        │   ├── propertyManager/
        │   ├── system/       # System manager, built-in functions
        │   └── utils/
        ├── frontend/         # UI DLL (wxWidgets controls, form renderer)
        │   ├── visualView/   # ibValueForm, 24 registered form control types
        │   │   └── ctrl/     # Individual control implementations
        │   ├── mainFrame/    # Main application window
        │   ├── docView/      # Document/view framework wrappers
        │   └── win/          # Windows-specific widgets and dialogs
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
| Optional databases | PostgreSQL, ODBC; SQLite (tests + logging) |
| Build (Windows) | MSBuild / Visual Studio 2019+ |
| Build (cross-platform) | CMake ≥ 3.20 — `CMakeLists.txt` at repo root (macOS / Linux) |
| License | LGPL 2.1 |

---

## Contributing

1. Fork the repository and create a feature branch from `develop`.
2. Follow existing naming conventions: `ib` prefix for public classes, `m_` prefix for member variables, `s_` for statics.
3. Match the existing code style (tabs, brace placement).
4. For new database queries use `ibPreparedStatement` instead of raw string formatting — this avoids the SQL injection patterns present in legacy code.
5. Submit a pull request against the `develop` branch with a clear description of the change.
6. The `master` branch is reserved for release-tagged commits.

Bug reports and feature requests are welcome via GitHub Issues.

---

## License

OES is distributed under the **GNU Lesser General Public License version 2.1**. See [LICENSE.md](LICENSE.md) for the full text.
