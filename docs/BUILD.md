# Build Instructions

This document covers how to build OES on Windows (MSBuild), macOS (CMake), and Linux (CMake).

---

## Table of Contents

1. [Prerequisites](#prerequisites)
2. [Cloning and Submodules](#cloning-and-submodules)
3. [Windows — MSBuild](#windows--msbuild)
4. [macOS — CMake](#macos--cmake)
5. [Linux — CMake](#linux--cmake)
6. [wxWidgets Submodule Details](#wxwidgets-submodule-details)
7. [Database Setup](#database-setup)
8. [Build Output](#build-output)
9. [Common Issues and Solutions](#common-issues-and-solutions)

---

## Prerequisites

### Windows

| Requirement | Minimum version | Notes |
|---|---|---|
| Visual Studio | 2019 (16.x) | "Desktop development with C++" workload; 2022 also supported |
| Windows SDK | 10.0.17763 or later | Installed by VS workload |
| Git | Any recent | For submodule initialisation |

MSBuild is the only supported build system on Windows. CMake support is planned but not yet available.

### macOS

| Requirement | Notes |
|---|---|
| Xcode Command Line Tools | `xcode-select --install` |
| CMake 3.20+ | `brew install cmake` |
| wxWidgets 3.3.x system library (optional) | The submodule can be built in-tree instead |
| Firebird client | `brew install firebird-client` (optional — Firebird embedded is bundled) |
| PostgreSQL client | `brew install postgresql` (optional) |

> **Note:** CMake support is under active development. A top-level `CMakeLists.txt` does not yet exist in the repository. The steps below represent the intended workflow.

### Linux (Ubuntu 22.04 / Debian 12)

```bash
sudo apt update
sudo apt install -y \
    build-essential \
    cmake \
    git \
    pkg-config \
    libwxgtk3.2-dev \
    wx3.2-headers \
    libfirebird-dev \
    libpq-dev \
    libsqlite3-dev \
    libmysqlclient-dev \
    unixodbc-dev
```

For other distributions, install the equivalent packages providing wxWidgets 3.2+, Firebird, PostgreSQL, SQLite3, MySQL, and ODBC development headers.

---

## Cloning and Submodules

```bash
git clone https://github.com/open-enterprise-solutions/enterprise.git
cd enterprise

# Initialise wxWidgets (located at src/3rdparty/wxWidgets)
git submodule update --init --recursive
```

The `--recursive` flag is required because wxWidgets itself contains submodules.

To update the wxWidgets submodule to the pinned commit after a `git pull`:

```bash
git submodule update --recursive
```

---

## Windows — MSBuild

### Visual Studio IDE

1. Open `enterprise.sln` in Visual Studio 2019 or 2022.
2. Select the desired configuration from the toolbar:
   - `Debug|Win32` — 32-bit debug build
   - `Debug|x64` — 64-bit debug build
   - `Release|Win32` — 32-bit release build
   - `Release|x64` — 64-bit release build
3. Press **Build > Build Solution** (`Ctrl+Shift+B`).

### Command Line (MSBuild)

Open a **Developer Command Prompt for VS** (not a regular cmd.exe):

```cmd
cd C:\path\to\enterprise

# 64-bit release
msbuild enterprise.sln /p:Configuration=Release /p:Platform=x64 /m /nologo

# 32-bit debug
msbuild enterprise.sln /p:Configuration=Debug /p:Platform=Win32 /m /nologo
```

The `/m` flag enables parallel compilation. Add `/v:m` for minimal verbosity or `/v:d` for detailed output.

### Build Output (Windows)

After a successful build:

```
bin\
  Win32\
    Debug\      or  Release\
      backend.dll
      frontend.dll
      enterprise.exe
      designer.exe
      launcher.exe
      daemon.exe
      codeRunner.exe
      classChecker.exe
      simplePlugin.dll
  Win64\
    ...
```

Firebird embedded libraries are expected alongside the executables. Copy them from the Firebird installation or the CI artefact package.

### Build Configurations

The `ConfigurationDefs.props` file defines preprocessor macros per configuration:

| Configuration | Macros defined |
|---|---|
| `Debug\|Win32` | `DEBUG` |
| `Debug\|x64` | `DEBUG` |
| `Release\|Win32` | `NDEBUG` |
| `Release\|x64` | `NDEBUG` |

> **Known issue:** The `Debug|Win32` configuration in `backend.vcxproj` currently defines `NDEBUG`, which disables assertions. This means `wxASSERT` calls are silently skipped in 32-bit debug builds. This is a build-configuration bug; it does not affect 64-bit debug or any release build.

---

## macOS — CMake

CMake cross-platform build is available. wxWidgets 3.3.2 is built from the in-tree submodule.

```bash
cd /path/to/enterprise

# Ensure submodules are initialised
git submodule update --init --recursive

# Configure (Debug)
cmake -B build -DCMAKE_BUILD_TYPE=Debug

# Configure (Release)
cmake -B build -DCMAKE_BUILD_TYPE=Release

# Build — IMPORTANT: limit parallel jobs on 16GB RAM machines
cmake --build build --parallel 3

# For 32GB+ RAM, you can use more parallelism:
cmake --build build --parallel 6
```

> **Warning:** Using `--parallel` without a limit (all CPU cores) on machines with 16GB RAM will cause an OOM crash during wxWidgets compilation. Always limit to 3 jobs on 16GB.

### CMake Options

| Option | Default | Description |
|---|---|---|
| `OES_USE_FIREBIRD` | OFF | Enable Firebird database driver |
| `OES_USE_POSTGRESQL` | OFF | Enable PostgreSQL database driver |
| `OES_USE_MYSQL` | OFF | Enable MySQL database driver |
| `OES_USE_ODBC` | OFF | Enable ODBC database driver |
| `OES_USE_TBB` | OFF | Enable Intel TBB parallelism |

SQLite is always enabled (embedded).

Example with PostgreSQL:
```bash
cmake -B build -DCMAKE_BUILD_TYPE=Release -DOES_USE_POSTGRESQL=ON
```

### Build Targets

| Target | Type | Status |
|---|---|---|
| backend | shared lib | Builds |
| frontend | shared lib | Builds |
| enterprise | executable | In progress |
| designer | executable | In progress |
| launcher | executable | Builds |
| daemon | executable | Builds |
| codeRunner | executable | Builds |
| classChecker | executable | Builds |
| simplePlugin | shared lib | Builds |

### macOS-specific Notes

- wxWidgets is built from the submodule via `add_subdirectory()` — no system wxWidgets needed
- Backend links against `wx::base`, `wx::core`, `wx::net`, `wx::xml`, `wx::propgrid`, `wx::aui`, `wx::stc`
- Firebird embedded is not available via Homebrew; use SQLite for local development
- App bundles (`designer.app`, `enterprise.app`) are created automatically by CMake

---

## Linux — CMake

```bash
cd /path/to/enterprise

git submodule update --init --recursive

# Configure
cmake -B build -DCMAKE_BUILD_TYPE=Release

# Build (limit parallelism based on available RAM)
cmake --build build --parallel 3    # 16GB RAM
cmake --build build --parallel 6    # 32GB+ RAM
```

wxWidgets 3.3.2 is built from the in-tree submodule automatically. No system wxWidgets required.

The same CMake options and build targets apply as described in the [macOS section](#macos--cmake).

---

## wxWidgets Submodule Details

wxWidgets 3.3.2 is pinned as a git submodule at `src/3rdparty/wxWidgets`.

```
.gitmodules entry:
  path = src/3rdparty/wxWidgets
  url  = https://github.com/wxWidgets/wxWidgets.git
```

On Windows the solution uses the pre-built wxWidgets binaries that should be placed at the path referenced by the `oes3rdParty` MSBuild macro (`$(SolutionDir)src\3rdparty\`). The submodule is used to build wxWidgets from source for macOS and Linux.

To build wxWidgets separately on Linux:

```bash
cd src/3rdparty/wxWidgets
./configure --prefix=$(pwd)/install --disable-shared --with-gtk=3
make -j$(nproc)
make install
```

Then point CMake to the install prefix via `-DwxWidgets_ROOT_DIR=src/3rdparty/wxWidgets/install`.

---

## Database Setup

### Firebird (Primary)

Firebird Embedded is the default engine. The embedded libraries (`fbclient.dll` on Windows, `libfbclient.so` on Linux) must be present in the same directory as the executables.

- **Windows:** Download the Firebird embedded package from https://firebirdsql.org/ and extract to the `bin\` folder.
- **Linux/macOS:** Install the Firebird client development package; the runtime library must be on `LD_LIBRARY_PATH` / `DYLD_LIBRARY_PATH`.

The Launcher (`launcher.exe`) creates a new Firebird database file when you select **File mode**. No server process is required for embedded mode.

### PostgreSQL (Optional)

To use PostgreSQL as the storage backend, start a PostgreSQL server and use the Launcher's **Server mode**, selecting the PostgreSQL driver. The `libpq` client library must be installed.

### SQLite, MySQL, ODBC

These drivers are compiled in but are intended for secondary/plugin use. No additional setup is required for SQLite (statically linked). MySQL requires `libmysqlclient` at runtime; ODBC requires a configured DSN.

---

## Build Output

| Directory | Contents |
|---|---|
| `bin\<Platform>\<Configuration>\` | All DLLs and EXEs (Windows) |
| `lib\<Platform>\<Configuration>\` | Static libraries, if any |
| `intermediate\<Platform>\<Configuration>\` | Object files and PCH |

---

## Common Issues and Solutions

### wxWidgets headers not found

**Symptom:** `fatal error: wx/wx.h: No such file or directory`

**Fix:** Run `git submodule update --init --recursive`. On Windows, verify the wxWidgets pre-built binaries are in `src\3rdparty\wxWidgets\`.

### Firebird embedded not found at runtime

**Symptom:** `Could not find Firebird client library` dialog on startup.

**Fix:** Copy `fbclient.dll` (Windows) or `libfbclient.so` (Linux) and the Firebird message files (`firebird.msg`) next to the executable.

### MSVC C++17 features not available

**Symptom:** Compilation errors about `std::execution` or structured bindings.

**Fix:** Ensure the **MSVC v142** or **v143** toolset is selected. Go to **Project Properties > General > Platform Toolset**.

### Linker error: unresolved external symbol in `backend.dll`

**Symptom:** `LNK2001` errors when building frontend or executables.

**Fix:** Build `backend` first. In the solution, right-click the failing project, select **Project Dependencies**, and ensure `backend` is checked.

### CMake cannot find wxWidgets

**Symptom:** `Could not find wxWidgets`

**Fix:** Run `git submodule update --init --recursive`. The CMake build uses the in-tree submodule automatically.

### Out of memory during build (macOS/Linux)

**Symptom:** System freezes, "Your system has run out of application memory" dialog, or OOM killer terminates compiler.

**Fix:** Limit parallel build jobs. On 16GB RAM use `--parallel 3`, on 8GB use `--parallel 2`. Close heavy applications (browsers, IDEs) before building.

### wxWidgets submodule corrupted after OOM

**Symptom:** `fatal: Unable to find current revision in submodule path 'src/3rdparty/wxWidgets'`

**Fix:**
```bash
rm -rf src/3rdparty/wxWidgets
git submodule update --init --recursive
```

### Debug build has assertions disabled (Win32 only)

**Symptom:** `wxASSERT` failures are silently ignored.

**Cause:** `backend.vcxproj` defines `NDEBUG` in the `Debug|Win32` configuration (known bug).

**Workaround:** Manually remove `NDEBUG` from the preprocessor definitions for that configuration, or use `Debug|x64`.
