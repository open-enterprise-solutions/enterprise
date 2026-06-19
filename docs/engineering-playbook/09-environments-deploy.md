# 09. Environments and Deployment

## Three environments

| Environment | Branch | Purpose |
|-----------|-------|-----------|
| **Local** | any | Development and debugging on the work machine |
| **Staging** | `develop` or `release/*` | Testing a release before delivery to a customer |
| **Production** | `master` | Release to customers (installer, distribution) |

### Principle

Code travels: **Local → Staging → Production**. A new version is not delivered to customers without verification on the staging environment.

Unlike a web service, OES "deployment" is the build of an installer and its distribution (via GitHub Releases, a shared drive, or direct delivery to the customer). The server side is just the DBMS (Firebird/PostgreSQL), which the customer deploys themselves or with help from the support team.

---

## Environment configuration

### Configuration files

| File | Purpose | In git? |
|------|-----------|--------|
| `config.ini.example` | Template with all parameters (no real values) | Yes |
| `config.ini` | Real configuration for the current environment | No |
| `config.debug.ini` | Overrides for local debugging (e.g. more verbose log level, test DB); loaded on top of `config.ini` only in Debug builds | No |

### Differences between environments

```ini
; config.ini (local development)
[app]
environment=development
log_level=debug
log_to_file=true
log_file=oes_debug.log

[database]
type=firebird
host=localhost
port=3050
database=C:\OES\dev\oes_dev.fdb
user=SYSDBA
password=masterkey

; config.ini (staging — test stand)
[app]
environment=staging
log_level=debug
log_to_file=true
log_file=C:\OES\logs\oes_staging.log

[database]
type=firebird
host=10.0.0.10
port=3050
database=D:\Databases\oes_staging.fdb
user=OES_APP
password=<staging_password>

; config.ini (production — at the customer)
[app]
environment=production
log_level=info
log_to_file=true
log_file=C:\ProgramData\OES\logs\oes.log

[database]
type=firebird
host=localhost
port=3050
database=C:\ProgramData\OES\oes.fdb
user=OES_APP
password=<client_password>
```

### Rule: config.ini.example is ALWAYS up to date

When adding a new configuration option:
1. Add it to `config.ini.example` with a comment
2. Add it to your own `config.ini`
3. Tell the team (and update staging/production configs)
4. Update CLAUDE.md if the parameter matters for understanding the project

---

## Local development

### Installing dependencies (Windows)

OES does not auto-use a package manager. Dependencies are installed manually or through vcpkg:

```powershell
# Through vcpkg (recommended for new dependencies)
vcpkg install wxwidgets:x64-windows
vcpkg install gtest:x64-windows
vcpkg install libpq:x64-windows    # PostgreSQL client
vcpkg install sqlite3:x64-windows

# Integration with Visual Studio
vcpkg integrate install
```

Firebird and IBPP are installed separately (see Firebird docs and `third-party/IBPP/`).

### Running locally

```powershell
# 1. Make sure Firebird is running
sc query FirebirdServerDefaultInstance
# If not running:
sc start FirebirdServerDefaultInstance

# 2. Create a test DB (if not yet created)
isql-fb -user SYSDBA -password masterkey
# CREATE DATABASE 'C:\OES\dev\oes_dev.fdb' PAGE_SIZE 16384 DEFAULT CHARACTER SET UTF8;
# EXIT;

# 3. Build the project
msbuild enterprise.sln /p:Configuration=Debug /p:Platform=x64 /m

# 4. Run (launcher is the connection chooser; enterprise.exe is the thick client)
.\bin\Win64\Debug\launcher.exe
```

### Why no Docker for local development

OES is a desktop application that uses a wxWidgets GUI. Docker is not suitable for GUI development. For local development:
- Firebird server is installed natively
- The application is built and launched directly through Visual Studio (Windows) or CMake (macOS/Linux)
- Debug through MSVC (Windows) or LLDB/GDB (macOS/Linux)

**Supported platforms:**
- **Windows** — primary platform, build via `enterprise.sln` (MSBuild/Visual Studio 2017+)
- **macOS / Linux** — cross-platform development target; build via CMake (created separately)

For cross-platform development (macOS/Linux) dependencies are installed natively through the distro's package manager.

---

## Staging

### What staging means for OES

Staging is a test stand that mirrors the real customer environment as closely as possible:
- A separate machine (physical or VM) with Windows
- A separate Firebird DB with a schema close to production
- Test data (not real customer data!)
- Release build (not Debug)

### Deploying to staging

**Manual deploy to the staging machine:**

```powershell
# 1. Build the Release version
msbuild enterprise.sln /p:Configuration=Release /p:Platform=x64 /m

# 2. Build the installer (NSIS or WiX)
# scripts/build-installer.ps1
.\scripts\build-installer.ps1 -Version "1.2.3"

# 3. Copy the installer to the staging machine
# (via shared folder, sftp, or manually)
Copy-Item ".\dist\OES-1.2.3-setup.exe" "\\staging-server\deploy\"

# 4. On the staging machine: run the installer
# \\staging-server\deploy\OES-1.2.3-setup.exe /S

# 5. Run smoke tests
# ... manually verify core features ...
```

**Via GitHub Actions (automatic build, see 17-ci-cd.md):**

```yaml
# .github/workflows/build-staging.yml
name: Build Staging

on:
  push:
    branches: [develop]

jobs:
  build:
    runs-on: windows-latest
    steps:
      - uses: actions/checkout@v4
      - uses: microsoft/setup-msbuild@v2

      - name: Build Release
        run: msbuild enterprise.sln /p:Configuration=Release /p:Platform=x64 /m
        # enterprise.sln — the primary OES build system (Windows/MSBuild)

      - name: Run tests
        run: ctest --test-dir build --output-on-failure

      - name: Build installer
        run: .\scripts\build-installer.ps1 -Version "${{ github.run_number }}"

      - name: Upload artifact
        uses: actions/upload-artifact@v4
        with:
          name: oes-staging-${{ github.run_number }}
          path: dist/OES-*-setup.exe
```

### Checks after staging deploy

- [ ] Installer installs without errors
- [ ] Application starts
- [ ] Firebird DB connection succeeds
- [ ] Core features work (opening forms, designer)
- [ ] Logs in C:\ProgramData\OES\logs\ contain no critical errors
- [ ] DB schema matches expectations (migrations applied)
- [ ] Performance is acceptable (no visible lag)

---

## Production

### Production deploy (releasing)

A production deploy for OES means publishing a new release that is installed on customer machines.

**Process:**

```bash
# 1. On GitHub: create a PR dev → master (or release → master)
# 2. Describe what's in the release (changelog)
# 3. Get tech lead approval
# 4. Merge

# 5. Create the version tag
git tag -a v1.2.0 -m "Release v1.2.0: change description"
git push origin v1.2.0
```

```powershell
# 6. Build the final Release
msbuild enterprise.sln /p:Configuration=Release /p:Platform=x64 /m

# 7. Run the full test suite
ctest --test-dir build -C Release --output-on-failure

# 8. Build the signed installer
# (requires a code signing certificate)
.\scripts\build-installer.ps1 -Version "1.2.0" -Sign

# 9. Upload to GitHub Releases
gh release create v1.2.0 `
    --title "OES v1.2.0" `
    --notes-file CHANGELOG.md `
    dist/OES-1.2.0-setup.exe

# 10. Notify customers / support team
```

### Database migrations

When updating the DB schema you must provide migration scripts:

```
db/migrations/
├── v1.1.0_to_v1.2.0.sql    — SQL migration script
└── v1.1.0_to_v1.2.0.sh     — Script that applies the migration
```

```sql
-- db/migrations/v1.1.0_to_v1.2.0.sql
-- Migration: add the email field to the users table
-- Author: Ivan Petrov
-- Date: 2026-03-15

ALTER TABLE USERS ADD EMAIL VARCHAR(255);
CREATE INDEX IDX_USERS_EMAIL ON USERS(EMAIL);

-- Update schema version
UPDATE DB_VERSION SET VERSION = '1.2.0', UPDATED_AT = CURRENT_TIMESTAMP;
```

```bash
#!/bin/bash
# db/migrations/v1.1.0_to_v1.2.0.sh
# Apply through isql-fb

isql-fb -user "$DB_USER" -password "$DB_PASSWORD" \
    "$DB_HOST:$DB_PATH" \
    -i "$(dirname "$0")/v1.1.0_to_v1.2.0.sql"

echo "Migration v1.1.0 → v1.2.0 applied successfully"
```

**Important:** Before applying a migration — back up the DB!

### Important: back up before upgrading at the customer

```powershell
# scripts/pre-upgrade-backup.ps1
param(
    [string]$DbPath = "C:\ProgramData\OES\oes.fdb",
    [string]$BackupDir = "C:\ProgramData\OES\backups"
)

$timestamp = Get-Date -Format "yyyyMMdd_HHmm"
$backupFile = Join-Path $BackupDir "oes_pre_upgrade_${timestamp}.fbk"

# Create the directory if it doesn't exist
New-Item -ItemType Directory -Force -Path $BackupDir | Out-Null

# Backup via gbak
& gbak -backup -user SYSDBA -password $env:SYSDBA_PASSWORD `
    "localhost:$DbPath" $backupFile

Write-Host "Backup created: $backupFile"
```

---

## Distribution layout

### Typical file layout in the OES distribution

```
OES-1.2.0-setup.exe         — Installer (NSIS/WiX)
    ↓ installs into:
C:\Program Files\OES\
├── enterprise.exe          — Thick client (also launcher.exe / designer.exe / daemon.exe)
├── config.ini.example      — Configuration template
├── wxbase33u_vc_custom.dll — wxWidgets runtime
├── fbclient.dll            — Firebird client
├── plugins\               — Plugins and extensions
│   ├── db_firebird.dll
│   ├── db_postgresql.dll
│   └── db_sqlite.dll
└── resources\              — Icons, translations
    ├── i18n\
    └── icons\

C:\ProgramData\OES\         — Application data (not in Program Files)
├── config.ini              — Working configuration
├── oes.fdb                 — Firebird DB (if embedded)
├── logs\
│   └── oes.log
└── backups\                — Automatic backups
```

### Installer build script

```powershell
# scripts/build-installer.ps1
param(
    [string]$Version = "0.0.0",
    [switch]$Sign = $false
)

Write-Host "Building OES installer v$Version"

# 1. Make sure Release is built
if (-not (Test-Path "bin\Win64\Release\enterprise.exe")) {
    throw "Release build not found. Run MSBuild first."
}

# 2. Build the NSIS installer
& makensis /DVERSION=$Version installer\oes-setup.nsi

# 3. Sign (if requested)
if ($Sign) {
    $cert = Get-Item "Cert:\CurrentUser\My\<THUMBPRINT>"
    Set-AuthenticodeSignature -FilePath "dist\OES-$Version-setup.exe" -Certificate $cert
}

Write-Host "Installer created: dist\OES-$Version-setup.exe"
```

---

## Rollback

### If a new version breaks something at the customer

OES is a desktop app. Rollback means installing the previous version.

```powershell
# 1. Restore the previous installer from GitHub Releases
gh release download v1.1.0 --pattern "*.exe" --dir ./rollback/

# 2. Save the customer's config (if it changed during the upgrade)
Copy-Item "C:\Program Files\OES\config.ini" "C:\Temp\config_backup.ini"

# 3. Uninstall the new version
# Control Panel → Programs → OES → Uninstall

# 4. Install the previous version
.\rollback\OES-1.1.0-setup.exe

# 5. Restore the config
Copy-Item "C:\Temp\config_backup.ini" "C:\Program Files\OES\config.ini"
```

### DB migration rollback

```bash
# Before migration ALWAYS take a backup (see above)
# Rollback — restore from backup

gbak -restore -user SYSDBA -password "$PASSWORD" \
    /backups/oes_pre_upgrade_20260315.fbk \
    /path/to/oes.fdb

echo "Database restored from backup"
```

---

## Monitoring and logging

### OES logs

```cpp
// OES writes logs through wxLog
wxLogMessage("Application started, version %s", OES_VERSION);
wxLogWarning("Database query took %ldms (threshold: %ldms)", elapsed, threshold);
wxLogError("Failed to connect to database: %s", error.c_str());

// Log file: C:\ProgramData\OES\logs\oes.log (production)
//           ./oes_debug.log (development)
```

### Startup health check

```cpp
// src/engine/enterprise/mainApp.cpp (enterprise entry point)
// src/engine/designer/mainApp.cpp   (designer entry point)
//
// ibApplicationData (appData.cpp) — handles application initialization:
//   - AuthenticateUser() — user authentication
//   - Connection to the ibDatabaseLayer of the required type (Firebird/Postgres/etc.)
//   - Metadata loading through ibValueMetaObjectCatalog / ibValueMetaObjectDocument

bool OESApp::OnInit() {
    // 1. Validate the configuration
    if (!m_config->IsValid()) {
        wxLogFatalError("Invalid configuration. Check config.ini");
        return false;
    }

    // 2. Initialize ibApplicationData and connect to the DB
    ibApplicationData* appData = ibApplicationData::Get();
    if (!appData->Connect(m_config)) {
        wxMessageBox(
            "Cannot connect to database.\n"
            "Check config.ini and ensure Firebird server is running.",
            "Connection Error",
            wxOK | wxICON_ERROR
        );
        return false;
    }

    // 3. User authentication
    if (!appData->AuthenticateUser(user, password)) {
        wxLogWarning("Authentication failed.");
        return false;
    }

    wxLogMessage("OES started successfully");
    return true;
}
```

### What to monitor at customers

```
Check regularly (when remote access is available):
- C:\ProgramData\OES\logs\oes.log — any critical errors?
- Size of the .fdb file — running out of disk space?
- Query performance (from the log at DEBUG level)
- Presence of fresh backups in C:\ProgramData\OES\backups\
```

### Simple log monitoring script

```powershell
# scripts/check-logs.ps1
# Find critical errors in the log for the last 24 hours

$logFile = "C:\ProgramData\OES\logs\oes.log"
$yesterday = (Get-Date).AddDays(-1)

$errors = Get-Content $logFile |
    Where-Object { $_ -match "ERROR|FATAL" } |
    Where-Object {
        if ($_ -match '(\d{4}-\d{2}-\d{2})') {
            [datetime]$Matches[1] -gt $yesterday
        }
    }

if ($errors) {
    Write-Warning "Found $($errors.Count) error(s) in OES log:"
    $errors | ForEach-Object { Write-Host $_ -ForegroundColor Red }
}
else {
    Write-Host "No errors in OES log (last 24h)" -ForegroundColor Green
}
```

---

## Versioning

### Versioning scheme

OES uses **Semantic Versioning**: `MAJOR.MINOR.PATCH`

| Type | Example | When |
|-----|--------|-------|
| **PATCH** | 1.2.0 → 1.2.1 | Bug fixes, no DB schema changes |
| **MINOR** | 1.2.0 → 1.3.0 | New features, backward-compatible schema changes |
| **MAJOR** | 1.2.0 → 2.0.0 | Breaking changes, incompatible schema changes |

### Version in code

```cpp
// src/engine/backend/version.h  (or equivalent file in backend/)
#pragma once

#define OES_VERSION_MAJOR 1
#define OES_VERSION_MINOR 2
#define OES_VERSION_PATCH 0

#define OES_VERSION_STRING "1.2.0"
#define OES_VERSION_BUILD  __DATE__ " " __TIME__
```

### Changelog

Before every release, update `CHANGELOG.md`:

```markdown
## [1.2.0] - 2026-03-15

### Added
- PostgreSQL 16 support
- New component type: DateRangePicker

### Fixed
- Fixed crash when opening designer on Windows 11
- Fixed SQL injection in the reports module

### Security
- Updated OpenSSL to 3.2.1 (CVE-2024-XXXX)
```
