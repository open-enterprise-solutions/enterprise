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
| Visual Studio | 2022 (17.x) | "Desktop development with C++" workload; platform toolset is `v143` |
| Windows SDK | 10.0 (latest) | Installed by VS workload (`WindowsTargetPlatformVersion=10.0`) |
| Git | Any recent | For submodule initialisation |

MSBuild (`enterprise.sln`) is the shipping build on Windows. CMake (`CMakeLists.txt` at repo root) is the build for macOS / Linux — **and it also works on Windows**: `CMakePresets.json` ships four host-conditioned presets (`windows-x64-debug`, `windows-x64-release`, `windows-x86-debug`, `windows-x86-release`), and that is the path the Google Test targets are built through (see [engineering-playbook/10-testing.md](engineering-playbook/10-testing.md)).

```cmd
cmake --preset windows-x64-debug
cmake --build --preset windows-x64-debug
```

### macOS

| Requirement | Notes |
|---|---|
| Xcode Command Line Tools | `xcode-select --install` |
| CMake 3.20+ | `brew install cmake` |
| wxWidgets 3.3.x system library (optional) | The submodule can be built in-tree instead |
| Firebird client | `brew install firebird-client` (optional — Firebird embedded is bundled) |
| PostgreSQL client | `brew install postgresql` (optional) |

> **Note:** A top-level `CMakeLists.txt` is in place; the macOS / Linux build path is operational. The wxWidgets submodule is built in-tree, so external wx is optional.

### Linux (Ubuntu 22.04 / Debian 12)

```bash
sudo apt update
sudo apt install -y \
    build-essential \
    cmake \
    ninja-build \
    git \
    pkg-config \
    libgtk-3-dev \
    uuid-dev \
    libfirebird-dev \
    libpq-dev \
    libsqlite3-dev \
    unixodbc-dev
```

`libgtk-3-dev` and `uuid-dev` are **not optional**: the build defines `__WXGTK__` and the CMake configure step
pkg-checks `gtk+-3.0`, so wx will not configure without it; and under GTK `guid.cpp` generates guids through libuuid's `uuid_generate`, which the backend links against. `ninja-build` is what the
`linux-debug` / `linux-release` presets generate for. The driver `-dev` packages are only needed
for the `OES_USE_*` options you turn on — SQLite is always embedded.

**No system wxWidgets package is needed** — wxWidgets 3.3.2 is built from the in-tree submodule
by the CMake build (see below); a distro `libwxgtk` would be both unused and older.

For other distributions, install the equivalent packages providing Firebird, PostgreSQL, SQLite3,
and ODBC development headers.

---

## Cloning and Submodules

```bash
git clone https://github.com/open-enterprise-solutions/enterprise.git
cd enterprise

# Initialise wxWidgets (located at src/3rdparty/wxWidgets, pinned to the 3.2 branch)
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

1. Open `enterprise.sln` in Visual Studio 2022.
2. Select the desired configuration from the toolbar. The solution exposes four
   pairs (the `x86` solution platform maps to the `Win32` project platform):
   - `Debug|x86` — 32-bit debug build
   - `Debug|x64` — 64-bit debug build
   - `Release|x86` — 32-bit release build
   - `Release|x64` — 64-bit release build
3. Press **Build > Build Solution** (`Ctrl+Shift+B`).

### Command Line (MSBuild)

Open a **Developer Command Prompt for VS** (not a regular cmd.exe):

```cmd
cd C:\path\to\enterprise

# 64-bit release
msbuild enterprise.sln /p:Configuration=Release /p:Platform=x64 /m /nologo

# 32-bit debug
msbuild enterprise.sln /p:Configuration=Debug /p:Platform=x86 /m /nologo
```

Use `/p:Platform=x86` or `/p:Platform=x64` (the solution does not expose a
`Win32` platform name). The `/m` flag enables parallel compilation; note that
`Common.props` sets `MultiProcessorCompilation=false` for `Debug` (incremental
linking) and true for `Release`, **but `backend.vcxproj` overrides it back to
true per-configuration** — so `/MP` is active in all four configurations for the
largest project. Add `/v:m` for minimal verbosity or `/v:d`
for detailed output.

### Build Output (Windows)

After a successful build (the `Platform` value `x86`/`x64` maps to the output
folder `Win32`/`Win64`):

```
bin\
  Win32\            (Platform=x86)
    Debug\      or  Release\
      backend.dll
      frontend.dll
      wfrontend.dll
      enterprise.exe
      designer.exe
      launcher.exe
      daemon.exe
      codeRunner.exe
      wenterprise-server.exe
  Win64\            (Platform=x64)
    ...
```

`wfrontend.dll` + `wenterprise-server.exe` are the web runtime (headless
server + web frontend). `simplePlugin.dll` is the plugin example; it builds into
`bin\<Platform>\<Configuration>\plugins\` — the directory the plugin manager scans at
startup. Firebird embedded libraries (`fbclient.dll`,
`ib_util.dll`, ICU) are expected alongside the executables — they ship in the
repo's prebuilt `_fb` payload and are copied next to the binaries.

### Build Configurations

`ConfigurationDefs.props` defines preprocessor macros per configuration
(keyed on `$(Configuration)` only — platform-independent):

| Configuration | Macros defined |
|---|---|
| `Debug` (any platform) | `DEBUG` |
| `Release` (any platform) | `NDEBUG` |
| `Mixed` / `Mixed_Dedicated` | `DEBUG`, `MIXED` |

The shared `Common.props` adds `USE_TBB_PARALLEL` and the CRT-workaround
macros to every TU, and a `CopyMsvcCRT` target that copies
`msvcp140.dll` / `vcruntime140*.dll` next to `Release` binaries.

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
| `OES_USE_FIREBIRD` | OFF | Enable Firebird database driver (fbclient loaded dynamically at runtime) |
| `OES_USE_POSTGRESQL` | OFF | Enable PostgreSQL database driver (libpq loaded dynamically at runtime) |
| `OES_USE_ODBC` | OFF | Enable ODBC database driver |
| `OES_FB_LOCALSERVER` | ON | Firebird out-of-process local server (Phase 6, leader-election); needs `firebird.exe` in `_fb/` at runtime |
| `OES_USE_TBB` | (undeclared) | Intel TBB parallelism (`USE_TBB_PARALLEL`); **not declared via `option()`** in the root `CMakeLists.txt` — only read by `backend/CMakeLists.txt`, so it never appears in `cmake-gui` / `ccmake`. Pass `-DOES_USE_TBB=ON` explicitly if you want it |
| `BUILD_TESTING` | OFF | Build the Google Test suite under `tests/` |

SQLite is always enabled (embedded sources, no option). `OES_USE_FIREBIRD`
and `OES_USE_POSTGRESQL` link their clients dynamically, so a system library
is not required to build — CMake locates one only opportunistically.

Example with PostgreSQL:
```bash
cmake -B build -DCMAKE_BUILD_TYPE=Release -DOES_USE_POSTGRESQL=ON
```

### Build Targets

The root `CMakeLists.txt` adds eight engine subdirectories, each with its own
`CMakeLists.txt`:

| Target | Type |
|---|---|
| backend | shared lib (`BACKEND_EXPORTS`) |
| frontend | shared lib (`FRONTEND_EXPORTS`, links wx GUI) |
| daemon | executable (console) |
| enterprise | executable (GUI) |
| designer | executable (GUI) |
| launcher | executable (GUI) |
| codeRunner | executable (GUI) |
| simplePlugin | shared lib (plugin example) |

The web runtime targets (`wfrontend`, `wenterprise-server`) build under the
MSBuild solution only; they are not yet wired into the CMake build.

### macOS-specific Notes

- wxWidgets is built from the submodule via `add_subdirectory()` — no system wxWidgets needed
- Backend links against `wx::base`, `wx::core`, `wx::net`, `wx::xml`, `wx::propgrid`, `wx::aui`, `wx::stc`
- Firebird embedded is not available via Homebrew; use SQLite for local development
- App bundles (`designer.app`, `enterprise.app`) are created automatically by CMake, and this
  path is **exercised, not merely declared**: they have been built on a real Mac, with the
  icon placed into `Resources` and `Info.plist` filled in (`CFBundleIdentifier`,
  `CFBundleExecutable`, icon name). Treat the `if(APPLE)` branches in the designer /
  enterprise / launcher CMake files as working code, not scaffolding

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

wxWidgets 3.3.2 lives as a git submodule at `src/3rdparty/wxWidgets`, tracked
on the wxWidgets `3.2` maintenance branch.

```
.gitmodules entry:
  path   = src/3rdparty/wxWidgets
  url    = https://github.com/wxWidgets/wxWidgets.git
  branch = 3.2
```

- **Windows (MSBuild):** the solution links the **pre-built** wxWidgets
  binaries under `$(oes3rdParty)wxWidgets\lib\vc_dll` (x86) /
  `…\lib\vc_x64_dll` (x64), plus the `mswu` includes
  (`oes3rdParty` = `$(SolutionDir)src\3rdparty\`).
  The libraries are `wxmsw33u*` / `wxbase33u*` (release) and `wxmsw33ud*`
  (debug). The submodule source is not compiled by the MSBuild build.
- **macOS / Linux (CMake):** wxWidgets is built from the submodule via
  `add_subdirectory()` as **shared** libraries (`wxBUILD_SHARED=ON`,
  monolithic OFF; tests/samples/demos disabled). No system wxWidgets is
  needed, and no separate `configure` step is required.

---

## Database Setup

### Firebird (Primary)

Firebird Embedded is the default engine. The embedded libraries (`fbclient.dll` on Windows, `libfbclient.so` on Linux) must be present in the same directory as the executables.

- **Windows:** nothing to install. The embedded payload is vendored in-repo under `src/engine/backend/databaseLayer/firebird/engine/dll/` (`fbclient_x86.dll` / `fbclient_x64.dll`, `ib_util_*.dll`, ICU, `firebird.msg`, `firebird.conf`) and the `backend` project copies it to `bin\<Platform>\<Configuration>\_fb\` on build.
- **Linux/macOS:** Install the Firebird client development package; the runtime library must be on `LD_LIBRARY_PATH` / `DYLD_LIBRARY_PATH`.

The Launcher (`launcher.exe`) creates a new Firebird database file when you select **File mode**. No server process is required for embedded mode.

### PostgreSQL (Optional)

To use PostgreSQL as the storage backend, start a PostgreSQL server and use the Launcher's **Server mode**, selecting the PostgreSQL driver. The `libpq` client library must be installed.

### SQLite, ODBC

These drivers are compiled in but are intended for secondary/plugin use. No additional setup is required for SQLite (statically linked), which serves tests and logging rather than production. ODBC requires a configured DSN, and is the base an MSSQL layer derives from.

---

## Build Output

Paths use the `oesPlatform` macro (`Win32` for `x86`, `Win64` for `x64`):

| Directory | Contents |
|---|---|
| `bin\Win32\Debug\` (etc.) | DLLs and EXEs (Windows) |
| `lib\Win32\Debug\` (etc.) | Import / static libraries |
| `intermediate\Win32\Debug\<ProjectName>\` | Object files and PCH |

---

## Continuous integration

`.github/workflows/ci.yml` (added 2026-08-02) runs on pushes to `develop` / `master`, on PRs into
`develop`, and on demand (`workflow_dispatch`). Four jobs:

| Job | Runner | What it proves |
|---|---|---|
| **Tests (Linux, Debug)** | ubuntu-22.04 | The backend suite (`oes_tests`) passes. The primary signal. |
| **Build (Windows, x64 Debug)** | windows-2022 | The shipping platform still compiles under MSVC, and the suite passes there too. |
| **GUI tests (Linux, Xvfb)** | ubuntu-22.04 | `oes_frontend_runtime_test` — links `frontend.dll`, needs a live wxApp. Separate job: its failure mode (a modal on an assert, or a process that passes every test and then does not exit) is unlike a backend test's. Xvfb is started by the step itself rather than through `xvfb-run`, so `$!` is the process under test — see the § below on what the wrapper cost. |
| **Tests (macOS 14, arm64, Debug)** | macos-14 | The third toolchain, and a different CPU with it: AArch64 (unsigned `char`, a weaker memory model, its own alignment), Apple libc++ rather than libstdc++, and wx against Cocoa instead of GTK — including the `APPLE` branch of `guid.cpp` (CFUUID) that nothing else compiles. Added 2026-08-03. |

Every job also runs a **Build the applications** step. Until 2026-08-03 CI built only test
targets, so `enterprise` / `designer` / `daemon` / `launcher` / `codeRunner` / `simplePlugin`
were never compiled by it at all — a break in an executable's own sources reached a developer
before it reached the pipeline. The step is cheap: backend and frontend are already built by
the steps above it.

State on 2026-08-03, the day the matrix first agreed (job logs): **all four jobs green.** Linux
**919/919** (96.6 s, job ~33 min), Windows **919/919** (132 s, ~37 min), macOS arm64 **919/919**
(163 s, ~22 min — the fastest of the three), GUI **26/26** under Xvfb. Every platform links every
application.

State on 2026-08-07: the suite has grown to **1166** and the GUI job to **38**, and the three
general jobs still report the same number as each other — which is the property being watched;
919 was never the property. That run was **red on all three at once** (1157/1166): nine
`JobManager` cases, one cause in the engine rather than in any toolchain, fixed the same day
(ROADMAP § 1b). The GUI job did not exit — see below.

Three toolchains, three standard libraries, two CPU architectures, one identical test count.
The six defects the macOS toolchain surfaced on its first runs — and what the warning noise was
hiding — are in [portability.md § 3](portability.md).

**The macOS job is not a first compile.** A colleague has been building the tree on a Mac locally
— which is why [portability.md § 3](portability.md) listed macOS as "not in CI" rather than as
unbuilt — and the `APPLE` branches in the launcher / enterprise / designer CMake files are
worked out, not placeholders. What the job adds is not the first build but the *standing* one:
the platform stops depending on a single machine and on someone remembering to try it before a
release. Anything it does surface belongs in [portability.md](portability.md) as a rule.

Four notes on why it is shaped this way — each one paid for by a wasted round:

- **Everything goes through the CMake presets, including Windows.** `enterprise.sln` carries 14
  projects and none is wxWidgets, so MSBuild cannot bootstrap a clean runner — it expects wx
  libraries to exist already. The preset path builds wx from the submodule.
- **No build-directory cache.** Caching `build/` keyed on the submodule commit looks obvious and
  is wrong with Ninja: `actions/cache` restores files with the *current* timestamp, so every
  restored object looks newer than its source and the build skips work it needed to do. The
  Windows job spent a round failing to link symbols the committed code no longer referenced,
  because it was linking a stale object file. A compiler cache (ccache / sccache, keyed on
  content) is the right optimisation; a timestamp-keyed one lies.
- **The jobs build the driver set the release ships** (Firebird + PostgreSQL on). Neither needs a
  system client to *compile* — both carry in-tree headers and load their client at runtime — so
  enabling them costs no dependency and stops CI from testing a configuration nobody builds.
  ODBC stays off because nothing
  exercises it.
- **Build every non-GUI test target, not just `oes_tests`.** `ctest` registers them all, so
  building one reports the rest as `<name>_NOT_BUILT` failures.

On a crash, the Linux job runs the two smallest failing tests under `gdb` and prints a backtrace:
`ctest` reports only the signal, and a 40-minute round trip is too expensive to spend on a guess.

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

**Fix:** Ensure the **MSVC v143** toolset is selected (the projects set
`<PlatformToolset>v143</PlatformToolset>`). Go to **Project Properties >
General > Platform Toolset** and install the VS 2022 C++ toolset if missing.

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

### Build of another OES process is running

**Symptom:** `LNK1168: cannot open <name>.exe for writing` during link.

**Fix:** Close any running OES processes (`enterprise.exe`, `designer.exe`,
`launcher.exe`, `daemon.exe`) before linking — they hold the output file open.
