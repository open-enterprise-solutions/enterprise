# 21. Distribution, Updates, and Resilience

## Build and distribution

### Target platforms

| Platform | Distribution format | Build tool |
|-----------|--------------------|--------------------|
| Windows (x64) | Inno Setup Installer (.exe) | MSBuild + Inno Setup |
| Linux (x64) | AppImage / .deb / .rpm | CMake + CPack |
| macOS (x64 / ARM64) | .dmg / .pkg | CMake + CPack |

### Build configuration matrix

| Configuration | Purpose | Optimizations |
|-------------|-----------|-------------|
| Debug | Local development | None, full debug symbols |
| Release | Production builds | /O2 (MSVC) / -O2 (GCC/Clang), no debug symbols |
| RelWithDebInfo | For crash reports | /O2 + PDB / -O2 + DWARF, symbols separate |

**Rule:** every public release is built in `RelWithDebInfo`. Symbols (PDB / DWARF) are stored in secure storage and used only for crash dump analysis.

### CMake / MSBuild — basic structure

```cmake
# CMakeLists.txt (root)
cmake_minimum_required(VERSION 3.22)
project(OES VERSION 2.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Application version (from git tag or manual)
configure_file(src/version.h.in src/version.h @ONLY)

# wxWidgets
find_package(wxWidgets 3.3.2 REQUIRED COMPONENTS core base aui adv)
include(${wxWidgets_USE_FILE})

# Main executable
add_executable(oes WIN32
    src/main.cpp
    src/app.cpp
    # ...
)
target_link_libraries(oes PRIVATE ${wxWidgets_LIBRARIES})

# Packaging
include(CPack)
set(CPACK_PACKAGE_NAME "OpenEnterpriseSolutions")
set(CPACK_PACKAGE_VERSION ${PROJECT_VERSION})
set(CPACK_PACKAGE_VENDOR "OES Team")
```

```cpp
// src/version.h.in
#pragma once
#define OES_VERSION_MAJOR @PROJECT_VERSION_MAJOR@
#define OES_VERSION_MINOR @PROJECT_VERSION_MINOR@
#define OES_VERSION_PATCH @PROJECT_VERSION_PATCH@
#define OES_VERSION_STRING "@PROJECT_VERSION@"
```

---

## Automatic update mechanism

### Architecture

```
┌──────────────────────────────────────────────────────────┐
│  OES Application (client)                                │
│                                                          │
│  UpdateChecker (background thread)                       │
│    ↓  HTTP GET /api/updates/check?version=2.0.0&os=win   │
│  Update Server (HTTPS)                                   │
│    ↓  JSON: { "latest": "2.1.0", "url": "...", ... }     │
│  Notification dialog to the user                         │
│    ↓  User confirms                                      │
│  Installer download (with SHA-256 check)                 │
│    ↓                                                     │
│  Run installer → close the application                   │
└──────────────────────────────────────────────────────────┘
```

### Update check implementation

```cpp
// update_checker.h
#pragma once
#include <string>
#include <functional>

struct UpdateInfo {
    std::string version;      // "2.1.0"
    std::string downloadUrl;
    std::string sha256;       // hex string
    std::string releaseNotes;
    bool        isMandatory;  // forced update
};

class UpdateChecker {
public:
    // Run the check in a background wxThread
    void CheckAsync(std::function<void(const UpdateInfo&)> onUpdateAvailable);

    // Download and verify integrity
    bool DownloadAndVerify(const UpdateInfo& info, const std::string& destPath);

private:
    static constexpr const char* UPDATE_URL =
        "https://updates.oes-platform.com/api/v1/check";

    std::string GetPlatformString();  // "win64", "linux64", "macos"
    bool VerifySha256(const std::string& filePath,
                      const std::string& expected);
};
```

```cpp
// update_checker.cpp (implementation outline)
//
// IMPORTANT: std::thread().detach() is dangerous — the thread keeps running
// after the UpdateChecker object is destroyed, leading to use-after-free
// (accessing this after the destructor). Instead of detach use:
//   - wxThread (wxTHREAD_DETACHED) — wxWidgets manages it, deletes itself safely
//   - joinable std::thread held as a class member (joined in the destructor)
//
// Recommended option: wxThread (already used in OesReportGeneratorThread)

class UpdateCheckerThread : public wxThread
{
public:
    UpdateCheckerThread(UpdateChecker* owner,
                        std::function<void(const UpdateInfo&)> cb)
        : wxThread(wxTHREAD_DETACHED)
        , m_owner(owner)
        , m_cb(std::move(cb))
    {}

protected:
    ExitCode Entry() override
    {
        std::string url = std::string(UpdateChecker::UPDATE_URL)
            + "?version=" + OES_VERSION_STRING
            + "&os=" + m_owner->GetPlatformString();

        // HTTP GET via wxHTTP or libcurl
        // ...

        // Parse the JSON response
        // If version > current → invoke callback on the main thread
        // wxTheApp->CallAfter([cb = m_cb, info]() { cb(info); });
        return 0;
    }

private:
    UpdateChecker*                       m_owner;
    std::function<void(const UpdateInfo&)> m_cb;
};

void UpdateChecker::CheckAsync(
    std::function<void(const UpdateInfo&)> onUpdateAvailable)
{
    // wxTHREAD_DETACHED — wxWidgets deletes the thread object on completion
    auto* thread = new UpdateCheckerThread(this, onUpdateAvailable);
    if (thread->Run() != wxTHREAD_NO_ERROR)
    {
        delete thread;
        wxLogWarning("[UpdateChecker] Failed to start update check thread");
    }
}
```

### Update server response format

```json
{
    "latest_version": "2.1.0",
    "min_required_version": "1.9.0",
    "download_url": "https://cdn.oes-platform.com/releases/oes-2.1.0-win64.exe",
    "sha256": "a3f8c2d1e9b0...",
    "size_bytes": 42567890,
    "release_date": "2026-04-01",
    "mandatory": false,
    "release_notes_url": "https://oes-platform.com/changelog/2.1.0",
    "release_notes": "Fixed crash when working with large Firebird tables..."
}
```

### Installer digital signature verification

**Windows:** the installer must be signed with a code signing certificate.

```bash
# Verify before launching (in app code via WinAPI)
# WinVerifyTrust() — verifies the Authenticode signature
```

```cpp
// Simplified signature verification on Windows
#ifdef _WIN32
#include <wintrust.h>
#include <softpub.h>
#pragma comment(lib, "wintrust")

bool VerifyCodeSignature(const std::wstring& filePath) {
    WINTRUST_FILE_INFO fileInfo = {};
    fileInfo.cbStruct = sizeof(WINTRUST_FILE_INFO);
    fileInfo.pcwszFilePath = filePath.c_str();

    GUID action = WINTRUST_ACTION_GENERIC_VERIFY_V2;
    WINTRUST_DATA trustData = {};
    trustData.cbStruct = sizeof(WINTRUST_DATA);
    trustData.dwUIChoice = WTD_UI_NONE;
    trustData.fdwRevocationChecks = WTD_REVOKE_WHOLECHAIN;
    trustData.dwUnionChoice = WTD_CHOICE_FILE;
    trustData.pFile = &fileInfo;
    trustData.dwStateAction = WTD_STATEACTION_VERIFY;

    LONG result = WinVerifyTrust(nullptr, &action, &trustData);
    return result == ERROR_SUCCESS;
}
#endif
```

---

## Crash reports (Windows Minidumps)

### Overview

When OES crashes on Windows the system creates a minidump file (`.dmp`) — a snapshot of the call stack, registers, and part of the memory at the time of the crash. It's the primary tool for diagnosing user-side issues.

### Crash defense layers

```
Layer 1: Vectored Exception Handler
├── Catches ACCESS_VIOLATION, STACK_OVERFLOW, etc.
├── Writes a minidump
└── Shows a dialog to the user + offers to send the report

Layer 2: Structured Exception Handling (SEH) on critical paths
├── Wrappers around DB, file, and plugin operations
└── Fallback logic on driver errors

Layer 3: wxApp::OnUnhandledException()
└── Last resort for wxWidgets C++ exceptions
```

### Crash handler implementation (Windows)

```cpp
// crash_handler.h
#pragma once
#ifdef _WIN32
#include <windows.h>
#include <dbghelp.h>
#pragma comment(lib, "dbghelp")

class CrashHandler {
public:
    static void Install();
    static void SetReportDir(const std::wstring& dir);

private:
    static LONG WINAPI UnhandledExceptionFilter(
        EXCEPTION_POINTERS* exceptionInfo);

    static bool WriteMiniDump(EXCEPTION_POINTERS* exceptionInfo,
                               const std::wstring& dumpPath);

    static std::wstring s_reportDir;
};
#endif
```

```cpp
// crash_handler.cpp
#ifdef _WIN32
#include "crash_handler.h"
#include <shlobj.h>
#include <ctime>

std::wstring CrashHandler::s_reportDir;

void CrashHandler::Install() {
    // Obtain AppData\Local\OES\CrashReports
    wchar_t appData[MAX_PATH];
    SHGetFolderPathW(nullptr, CSIDL_LOCAL_APPDATA, nullptr, 0, appData);
    s_reportDir = std::wstring(appData) + L"\\OES\\CrashReports";
    CreateDirectoryW(s_reportDir.c_str(), nullptr);

    SetUnhandledExceptionFilter(UnhandledExceptionFilter);
}

LONG WINAPI CrashHandler::UnhandledExceptionFilter(
    EXCEPTION_POINTERS* exceptionInfo)
{
    // Build the filename with a timestamp
    time_t t = time(nullptr);
    wchar_t dumpName[256];
    swprintf_s(dumpName, L"oes_%lld.dmp", (long long)t);
    std::wstring dumpPath = s_reportDir + L"\\" + dumpName;

    WriteMiniDump(exceptionInfo, dumpPath);

    // Show a dialog (no recursion — straight through WinAPI)
    MessageBoxW(nullptr,
        L"Open Enterprise Solutions has crashed.\n"
        L"A crash report has been saved. Please send it to the developers.",
        L"Application error",
        MB_OK | MB_ICONERROR);

    return EXCEPTION_EXECUTE_HANDLER;
}

bool CrashHandler::WriteMiniDump(EXCEPTION_POINTERS* exceptionInfo,
                                  const std::wstring& dumpPath)
{
    HANDLE hFile = CreateFileW(dumpPath.c_str(),
        GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS,
        FILE_ATTRIBUTE_NORMAL, nullptr);

    if (hFile == INVALID_HANDLE_VALUE)
        return false;

    MINIDUMP_EXCEPTION_INFORMATION mei = {};
    mei.ThreadId = GetCurrentThreadId();
    mei.ExceptionPointers = exceptionInfo;
    mei.ClientPointers = FALSE;

    MINIDUMP_TYPE dumpType = (MINIDUMP_TYPE)(
        MiniDumpWithDataSegs |
        MiniDumpWithProcessThreadData |
        MiniDumpWithHandleData |
        MiniDumpWithThreadInfo);

    BOOL ok = MiniDumpWriteDump(
        GetCurrentProcess(), GetCurrentProcessId(),
        hFile, dumpType, &mei, nullptr, nullptr);

    CloseHandle(hFile);
    return ok == TRUE;
}
#endif
```

### Minidump analysis

```bash
# WinDbg / cdb (with PDB symbols)
cdb -z crash_20260410_143200.dmp
# Commands:
# .sympath srv*c:\symbols*https://msdl.microsoft.com/download/symbols
# .sympath+ C:\OES\Symbols\2.0.0
# !analyze -v
# k        ← call stack
# ~*k      ← stacks of all threads

# Or via Visual Studio:
# File → Open → Crash Dump → open the .dmp
# "Debug with Native Only" → loads symbols automatically
```

### Linux — core dumps

```bash
# Enable core dumps
ulimit -c unlimited
# /proc/sys/kernel/core_pattern = /var/crash/core-%e-%p-%t

# Set the limit for production
# /etc/security/limits.conf:
# oes_user soft core unlimited

# Analyze with GDB
gdb /usr/bin/oes /var/crash/core-oes-12345-1712345678
# (gdb) bt       ← backtrace
# (gdb) thread apply all bt  ← all threads
```

### Sending crash reports

```cpp
// Report submission dialog
class CrashReportDialog : public wxDialog {
public:
    CrashReportDialog(const wxString& dumpPath)
        : wxDialog(nullptr, wxID_ANY, "Send crash report",
                   wxDefaultPosition, wxSize(500, 320))
    {
        // Show the dump path, user comment,
        // "Send" / "Don't send" buttons
    }

    void OnSend(wxCommandEvent&) {
        // Upload .dmp + log + comment to the server
        // via multipart/form-data (libcurl or wxHTTP)
        // POST https://crashes.oes-platform.com/api/v1/report
    }
};
```

---

## Database connection resilience

### Defense layers

```
Layer 1: Connection Pool
├── Minimum connection pool (2-5 for desktop)
├── Periodic SELECT 1 heartbeat (ibDatabaseLayer)
└── Lazy reconnect when a break is detected

Layer 2: Retry logic
├── Automatic retry (3 attempts with exponential backoff)
├── Distinguishing transient errors from fatal ones
└── Notify UI only on final failure

Layer 3: Graceful degradation
├── Cache last data in memory when connectivity is lost
├── Read-only mode when the master DB is unavailable
└── Reconnect dialog without data loss
```

### Retry logic implementation

```cpp
// db_connection.h
#pragma once
#include <functional>
#include <stdexcept>
#include <thread>
#include <chrono>

class DatabaseConnection {
public:
    // Execute an operation with automatic retry
    template<typename Func>
    auto ExecuteWithRetry(Func&& func,
                          int maxRetries = 3,
                          int baseDelayMs = 500) -> decltype(func())
    {
        int attempt = 0;
        while (true) {
            try {
                EnsureConnected();
                return func();
            } catch (const DatabaseTemporaryError& e) {
                if (++attempt >= maxRetries)
                    throw;
                // Exponential backoff: 500ms, 1000ms, 2000ms
                int delayMs = baseDelayMs * (1 << (attempt - 1));
                std::this_thread::sleep_for(
                    std::chrono::milliseconds(delayMs));
            }
            // DatabaseFatalError and others — not caught, rethrown
        }
    }

    bool IsConnected() const;
    void Reconnect();

private:
    void EnsureConnected();
    bool Ping();  // SELECT 1 through ibDatabaseLayer (works for Firebird, PostgreSQL, SQLite)
};
```

### Handling Firebird connection loss

```cpp
// Example usage in the data layer
std::vector<Record> DataLayer::GetRecords(int projectId) {
    return m_db.ExecuteWithRetry([&]() {
        FBStatement stmt(m_db,
            "SELECT * FROM RECORDS WHERE PROJECT_ID = ?");
        stmt.SetParam(1, projectId);
        return stmt.FetchAll<Record>();
    });
}

// In the UI layer — handling final errors
void MainFrame::OnLoadData(wxCommandEvent&) {
    try {
        auto records = m_dataLayer->GetRecords(m_currentProjectId);
        m_grid->LoadRecords(records);
    } catch (const DatabaseFatalError& e) {
        wxMessageBox(
            wxString::Format(
                "Failed to connect to the database:\n%s\n\n"
                "Check the connection settings.",
                e.what()),
            "DB error", wxOK | wxICON_ERROR);
    }
}
```

### Connection settings with reconnect

```ini
; oes.ini — connection settings
[Database]
Type=Firebird
Host=localhost
Port=3050
Database=/var/db/oes/main.fdb
Username=SYSDBA
ConnectionTimeout=10
StatementTimeout=30
MaxRetries=3
RetryDelayMs=500
HeartbeatIntervalSec=60
```

---

## User data backup

### 3-2-1 strategy for a desktop application

- **3** copies: original DB + local backup + remote (if configured)
- **2** media: main disk + external/network
- **1** offsite: optional sync with the company server

### Automatic local backup (Firebird gbak)

```cpp
// backup_manager.h
class BackupManager {
public:
    // Runs at app startup and on a schedule
    void ScheduleDailyBackup();

    // Creates a backup using gbak (Firebird utility)
    bool CreateFirebirdBackup(const std::string& dbPath,
                               const std::string& backupDir);

    // Rotation: keep the last N backups
    void RotateBackups(const std::string& backupDir,
                       int keepCount = 7);
};
```

```cpp
bool BackupManager::CreateFirebirdBackup(
    const std::string& dbPath, const std::string& backupDir)
{
    auto now = std::chrono::system_clock::now();
    std::time_t t = std::chrono::system_clock::to_time_t(now);
    char timestamp[32];
    std::strftime(timestamp, sizeof(timestamp), "%Y%m%d_%H%M", std::localtime(&t));

    std::string backupFile = backupDir + "/oes_backup_"
        + timestamp + ".fbk";

    // Call gbak through wxExecute
    // NOTE: the "-service service_mgr" mode uses the Firebird Services API
    // and requires a running Firebird server (won't work with Embedded builds).
    // For embedded mode use a direct file connection:
    //   gbak -backup -user SYSDBA -password masterkey path_to_file.fdb backup.fbk
    // For server mode through the Services API:
    //   gbak -backup -service service_mgr -user SYSDBA -password masterkey ...
    wxString cmd = wxString::Format(
        "gbak -backup -user SYSDBA -password masterkey %s %s",
        dbPath, backupFile);

    long exitCode = wxExecute(cmd, wxEXEC_SYNC);
    return exitCode == 0;
}
```

### What to back up and how often

| What | How often | Where to store | Retention |
|-----|-----------|-------------|---------|
| Firebird (.fdb) | On startup + once a day | AppData\OES\Backups | 7 days |
| SQLite (.db) | On change | Next to the file | 5 versions |
| Configuration (.ini) | On change | AppData\OES\Config | Indefinite |
| License keys | On registration | AppData\OES\License | Indefinite |
| Templates/projects | On request | User-selected path | User's call |

### Backup testing

**Rule: a backup that hasn't been tested isn't a backup.**

```bash
# Verify restore from fbk (Firebird)
gbak -restore -replace oes_backup_20260410.fbk /tmp/test_restore.fdb
isql /tmp/test_restore.fdb -user SYSDBA -pass masterkey \
  -e "SELECT COUNT(*) FROM PROJECTS;"
```

---

## Installer (Windows — Inno Setup)

The installer build tool — **Inno Setup** (consistent with 17-ci-cd.md).
The script lives in `installer\setup.iss` and is built with:

```
iscc /DAppVersion=2.0.0 installer\setup.iss
```

### Basic Inno Setup script structure

```iss
; installer\setup.iss
#define AppName    "Open Enterprise Solutions"
#define AppVersion "2.0.0"
#define AppPublisher "OES Team"
#define AppExeName "oes.exe"

[Setup]
AppName={#AppName}
AppVersion={#AppVersion}
AppPublisher={#AppPublisher}
DefaultDirName={autopf64}\OES
DefaultGroupName={#AppName}
OutputBaseFilename=OES_Setup_{#AppVersion}
OutputDir=installer\Output
Compression=lzma2/ultra64
SolidCompression=yes
ArchitecturesInstallIn64BitMode=x64compatible
PrivilegesRequired=admin

[Files]
; Main application files
Source: "release\*"; DestDir: "{app}"; Flags: recursesubdirs

; Visual C++ Redistributable (installed silently if not already present)
Source: "redist\vcredist_x64.exe"; DestDir: "{tmp}"; Flags: deleteafterinstall

[Icons]
Name: "{group}\{#AppName}"; Filename: "{app}\{#AppExeName}"
Name: "{commondesktop}\{#AppName}"; Filename: "{app}\{#AppExeName}"

[Run]
; Install VC++ Redistributable before launching the application
Filename: "{tmp}\vcredist_x64.exe"; Parameters: "/quiet /norestart"; \
  StatusMsg: "Installing Visual C++ Redistributable..."; \
  Flags: waituntilterminated

; Offer to launch the application after install
Filename: "{app}\{#AppExeName}"; Description: "Launch {#AppName}"; \
  Flags: nowait postinstall skipifsilent

[Code]
// Optional .NET / VC++ check before installation
procedure InitializeWizard();
begin
  // Additional checks if needed
end;
```

---

## Distribution and resilience checklist

### Minimum (every release):
- [ ] Built in RelWithDebInfo with separate symbols (PDB/DWARF)
- [ ] Symbols stored in secure storage for this version
- [ ] CrashHandler installed at app startup (Windows)
- [ ] Installer integrity verification (SHA-256 on the site)
- [ ] Installer signed with a code signing certificate (Windows)
- [ ] Automatic local data backup on startup
- [ ] Retry logic for DB connections
- [ ] Notification dialog for available updates
- [ ] App version shown in About + embedded in resources

### Advanced (critical deployments):
- [ ] Automatic crash report submission (with user consent)
- [ ] Crash report monitoring server (crash.oes-platform.com)
- [ ] Forced updates on critical vulnerabilities
- [ ] Rollback mechanism (previous installer retained)
- [ ] Silent install for corporate deployment (SCCM/MSI)
- [ ] Monthly backup restore testing
- [ ] Runbook for every type of incident
- [ ] On-call rota for major releases
