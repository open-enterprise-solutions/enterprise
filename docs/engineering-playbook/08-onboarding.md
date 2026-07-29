# 08. Onboarding

## Goal

A new team member should, within their first week:
- Have all required access
- Build the project locally
- Understand the workflow
- Open their first PR

---

## Day 1: Access and tools

### Get access

| Service | Granted by | Access level |
|--------|-----------|-----------------|
| GitHub (OES org) | Tech lead | Write (to their repositories) |
| Servers (SSH) | Tech lead/DevOps | Personal SSH key, deploy user |
| Task tracker (GitHub Issues) | Tech lead | Full access |
| Corporate messenger | Tech lead | Full access |
| Dependency license keys | Tech lead | As needed |

### Install development tools

**Supported platforms:**
- **Windows** — primary platform, build via `enterprise.sln` (MSBuild / Visual Studio 2022 — the projects pin toolset `v143`, which 2017/2019 cannot open without retargeting)
- **macOS / Linux** — cross-platform target, build via CMake (created separately)

**Windows (primary platform):**

```powershell
# 1. Visual Studio 2019/2022 with components:
#    - Desktop development with C++
#    - Windows SDK (latest)
#    - CMake tools for Visual Studio
# Download: https://visualstudio.microsoft.com/

# 2. Git for Windows
# Download: https://git-scm.com/download/win
git --version

# 3. CMake (if not installed through VS)
winget install Kitware.CMake

# 4. vcpkg — the C++ package manager
git clone https://github.com/microsoft/vcpkg.git C:\vcpkg
C:\vcpkg\bootstrap-vcpkg.bat
# Add C:\vcpkg to PATH

# 5. Firebird (for local development)
# Download Firebird 4.0 from https://firebirdsql.org/
# Install as a service

# 6. IBExpert or FlameRobin — Firebird GUI
# IBExpert: https://ibexpert.net/

# 7. IDE — Visual Studio 2019/2022 or CLion
# CLion: https://www.jetbrains.com/clion/

# 8. Claude Code CLI (optional)
# Requires Node.js: https://nodejs.org/
npm install -g @anthropic-ai/claude-code
```

**Linux (for cross-platform development):**

```bash
# 1. Compiler and tools
sudo apt update
sudo apt install -y \
    build-essential \
    cmake \
    ninja-build \
    git \
    pkg-config \
    libgtk-3-dev \
    libfirebird-dev \
    libpq-dev \
    libsqlite3-dev
# libgtk-3-dev   — for wxWidgets
# libfirebird-dev — Firebird headers (package may be named firebird3.0-dev on older distros)
# libpq-dev      — PostgreSQL
# libsqlite3-dev — SQLite

# 2. wxWidgets 3.3.2
wget https://github.com/wxWidgets/wxWidgets/releases/download/v3.3.2/wxWidgets-3.3.2.tar.bz2
tar -xf wxWidgets-3.3.2.tar.bz2
cd wxWidgets-3.3.2
./configure --enable-debug --with-gtk=3
make -j$(nproc)
sudo make install

# 3. Firebird 4.x
# Firebird 4.x is not available in the standard Ubuntu/Debian repos.
# Install from the official repository: https://firebirdsql.org/en/firebird-4-0/
# Or from a .deb package on GitHub Releases:
# https://github.com/FirebirdSQL/firebird/releases
# Example for Ubuntu 22.04 (x86_64):
# wget https://github.com/FirebirdSQL/firebird/releases/download/v4.0.4/Firebird-4.0.4.3010-0.amd64.tar.gz
# ... follow the instructions in the archive (INSTALL.sh)

# 4. Google Test (for tests)
sudo apt install libgtest-dev
```

### Configure Git

```bash
# Name and email (matching your GitHub account)
git config --global user.name "Your Name"
git config --global user.email "your.email@company.com"

# Default main branch
git config --global init.defaultBranch master

# Automatic tracking branch cleanup
git config --global fetch.prune true

# Rebase by default on pull (instead of merge)
git config --global pull.rebase true

# For Windows: normalize line endings
git config --global core.autocrlf true
```

### Configure SSH key for GitHub

```bash
# 1. Create the key
ssh-keygen -t ed25519 -C "your.email@company.com"
# Press Enter for the default path (~/.ssh/id_ed25519)
# Set a passphrase — recommended

# 2. Add to SSH agent
eval "$(ssh-agent -s)"
ssh-add ~/.ssh/id_ed25519

# 3. Copy the public key
cat ~/.ssh/id_ed25519.pub

# 4. Add to GitHub
# GitHub → Settings → SSH and GPG keys → New SSH key

# 5. Verify the connection
ssh -T git@github.com
# "Hi username! You've successfully authenticated..."
```

---

## Day 2: Project build

### Clone the repository

```bash
# Create a working folder
mkdir -p ~/projects/oes
cd ~/projects/oes

# Clone the OES repository
git clone git@github.com:oes-org/enterprise.git
cd enterprise
```

### Set up configuration

```bash
# 1. Copy the configuration template
cp config.ini.example config.ini

# 2. Edit config.ini
# Fill in Firebird parameters for the local DB
# Ask the tech lead for specific settings if needed

# Minimal configuration for local development:
# [database]
# type=firebird
# host=localhost
# port=3050
# database=C:\OES\oes_dev.fdb   (Windows)
# database=/var/lib/oes/oes_dev.fdb  (Linux)
# user=SYSDBA
# password=masterkey   (local development only!)
```

### Build with MSBuild (Windows — primary path)

```powershell
# Open enterprise.sln in Visual Studio 2022 (toolset v143)
# Choose the configuration: Debug | x64

# Or from the command line (Developer Command Prompt)
cd C:\projects\oes\enterprise

# Check that MSBuild is available
msbuild --version

# Build the Debug configuration
msbuild enterprise.sln /p:Configuration=Debug /p:Platform=x64 /m

# Run the Designer
.\x64\Debug\OESDesigner.exe

# Run Enterprise (runtime)
.\x64\Debug\OESEnterprise.exe
```

### Build with CMake (cross-platform path, in progress)

```bash
# Create the build directory
mkdir build && cd build

# Configure (Debug)
cmake .. \
    -DCMAKE_BUILD_TYPE=Debug \
    -DwxWidgets_ROOT_DIR=/usr/local \
    -DCMAKE_EXPORT_COMPILE_COMMANDS=ON

# Build
cmake --build . --config Debug -- -j$(nproc)

# Run
./bin/oes
```

### Create a test Firebird database

```bash
# Through isql-fb (provide user and password explicitly)
isql-fb -user SYSDBA -password masterkey

# In isql:
CREATE DATABASE '/path/to/oes_dev.fdb'
    USER 'SYSDBA' PASSWORD 'masterkey'
    PAGE_SIZE 16384
    DEFAULT CHARACTER SET UTF8;
EXIT;

# Or through IBExpert: File → Create New Database
```

### Verify everything works

1. The application starts without errors
2. Local Firebird DB connection succeeds
3. Core features (opening forms, navigation) work
4. No critical errors in the logs

If something is broken — first check:
- Is the Firebird service running (`sc query FirebirdServerDefaultInstance` on Windows)
- Is `config.ini` filled in correctly (path to the .fdb file, password)
- Were all dependencies built (wxWidgets, IBPP)
- Any compiler errors in the Output Window

---

## Days 2–3: Learn the standards

### Required reading (in this order)

1. **README.md** — what it is, how to build
2. **CLAUDE.md** — architecture, business rules, key decisions
3. **This playbook** — every document in the engineering playbook
4. **Comments on key classes** — `DBLayer`, `MainFrame`, `Designer`

### OES architecture — brief overview

```
src/engine/
├── backend/                  — Platform core
│   ├── appData.cpp           — ibApplicationData: authentication, AuthenticateUser()
│   ├── appDataQuery.cpp      — Session and user queries
│   ├── compiler/
│   │   ├── compileCode.cpp   — ibCompileCode, ibTranslateCode
│   │   ├── procUnit.cpp      — ibProcUnit: bytecode interpreter
│   │   ├── value.h           — ibValue, ibValueTypes
│   │   └── fnumber.h         — ibNumber (self-contained exact-decimal, no ttmath)
│   ├── databaseLayer/        — ibDatabaseLayer + ibDatabaseLayerFirebird, Postgres, etc.
│   │   └── databaseLayer.h   — ibPreparedStatement, ibDatabaseResultSet
│   ├── metaCollection/partial/
│   │   └── commonObjectQuery.cpp — CRUD, CreateAndUpdateTableDB()
│   ├── metadataConfiguration.cpp — Metadata configuration management
│   └── debugger/debugServer.cpp  — ibDebuggerServer
├── frontend/                 — wxWidgets UI components
│   └── visualView/ctrl/      — ibValueForm, ibValueFrame, ibValueTextCtrl,
│                               ibValueButton, ibValueModelTableBox
├── designer/mainApp.cpp      — Designer entry point (ibValueModuleManager, ibCompileModule)
└── enterprise/mainApp.cpp    — Enterprise entry point (runtime)
```

### Key things to remember

- Branches: `master` (production), `develop` (integration), `feature/*` (development), `fix/*` (bugs)
- Commits: `type: description` in English
- PR: from feature/fix into `develop`; from `develop` into `master` only at release
- Code: C++17, RAII, ibTransactionGuard for transactions, ibPreparedStatement for queries
- Exceptions: ibBackendCoreException (engine errors), ibBackendInterruptException (script interrupt)
- AI: a partner, not an autopilot. A human reviews everything

---

## First week: first PR

### Pick a task

The tech lead will assign the first task — usually something small:
- Fix a compiler warning
- Add input validation
- Replace a raw pointer with `unique_ptr`
- Fix an empty catch block (add logging)
- Update documentation

A task labelled `good first issue` is the ideal start.

### Process

```bash
# 1. Update develop
git checkout develop
git pull origin develop

# 2. Create a branch
git checkout -b fix/add-input-validation-login

# 3. Make changes
# ... write code ...

# 4. Build and check
# In Visual Studio: Build → Build Solution (Ctrl+Shift+B)
# Make sure there are no new compiler warnings

# 5. Run tests (if already set up)
ctest --output-on-failure

# 6. Run static analysis
cppcheck --enable=all --std=c++17 src/

# 7. Commit
git add src/engine/frontend/visualView/ctrl/ibValueLoginDialog.cpp
git add src/engine/frontend/visualView/ctrl/ibValueLoginDialog.h
git commit -m "fix: add input length validation in login dialog"

# 8. Push
git push -u origin fix/add-input-validation-login

# 9. Create the PR on GitHub
# Base: develop ← Compare: fix/add-input-validation-login
# Fill in the description (what, why, how to test)
```

### Expectations for the first PR

- It's fine if there are review comments — that's normal
- The reviewer will help with the code standards
- The goal is to go through the full cycle: task → branch → code → PR → review → merge

---

## New member checklist

### Day 1
- [ ] GitHub access granted (OES org)
- [ ] Server access (SSH key added)
- [ ] Task tracker access
- [ ] Visual Studio 2019/2022 installed with C++ components
- [ ] Git for Windows installed
- [ ] Firebird 4.0 installed (server and client)
- [ ] IBExpert or FlameRobin installed
- [ ] git config set (name, email, autocrlf)
- [ ] SSH key generated and added to GitHub

### Day 2
- [ ] Repository cloned
- [ ] config.ini configured
- [ ] Project built (Debug/x64)
- [ ] Test Firebird DB created
- [ ] OES launched, confirmed working
- [ ] README.md read
- [ ] CLAUDE.md read

### Days 3–5
- [ ] Engineering playbook read end-to-end
- [ ] Understand the architecture: DBLayer, MainFrame, Designer
- [ ] First task received from the tech lead
- [ ] feature/fix branch created
- [ ] Code written
- [ ] PR created
- [ ] Review completed
- [ ] PR merged (with the reviewer's help)

### End of first week
- [ ] I understand the process: task → branch → PR → review → merge
- [ ] I can build the project locally without help
- [ ] I know where to find tasks (GitHub Issues)
- [ ] I know how to use AI tools (Claude Code)
- [ ] I know who to ask when stuck

---

## Useful day-to-day commands

```bash
# Git — common operations
git status                          # Current state
git diff                            # Unstaged changes
git log --oneline -10               # Last 10 commits
git stash                           # Temporarily hide changes
git stash pop                       # Restore hidden changes

# CMake — build
cmake --build build --config Debug             # Build
cmake --build build --config Debug --target oes_tests  # Build tests
ctest --test-dir build --output-on-failure     # Run tests

# MSBuild (Windows)
msbuild enterprise.sln /p:Configuration=Debug /p:Platform=x64 /m
msbuild enterprise.sln /p:Configuration=Release /p:Platform=x64 /m

# cppcheck — static analysis
cppcheck --enable=all --std=c++17 src/

# Firebird — DB operations
isql-fb -user SYSDBA -password masterkey localhost:path/to/oes.fdb    # Connect to DB
gbak -b -user SYSDBA -pass masterkey localhost:oes.fdb oes_backup.fbk  # Backup

# Claude Code
claude                              # Run Claude Code in the current folder
```

---

## Questions?

If something is unclear — ask. There are no stupid questions, only bad documentation. If the docs didn't answer your question — that's a reason to improve them (and a great first PR).
