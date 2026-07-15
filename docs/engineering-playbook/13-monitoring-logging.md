# 13. Monitoring and Logging

> **Two distinct logging paths exist — don't conflate them:**
> - **`wxLog*`** (this document) — developer trace output to file / DebugView.
> - **`ibLogger`** (`backend/logger/`, reached via the `ibLog` macro) — the
>   structured **audit trail** (the activity log): business events
>   (login, document write, DDL apply, session open/close) persisted to
>   per-month SQLite `.olg` files, viewed in-app. See
>   [`../audit-log.md`](../audit-log.md). Use `ibLog->Audit(...)` for
>   business events, `wxLog*` for technical traces.

## Monitoring stack

| Component | Tool | Purpose |
|-----------|-----------|------------|
| Application logs | wxLog + file output | Event capture and storage |
| DB logs | ibDatabaseLayer custom error handler | Slow queries, connection errors |
| Performance metrics | Intel VTune / Windows Performance Monitor | CPU, RAM, I/O, execution time |
| Crash dumps | Windows Error Reporting / MiniDump | Postmortem of crashes |
| Runtime diagnostics | DebugView (Sysinternals) | Capture OutputDebugString |
| Performance testing | Very Sleepy / Superluminal | CPU profiling |

---

## Logging

### Principles

1. **Levels** — `wxLogError`, `wxLogWarning`, `wxLogMessage` (info), `wxLogDebug` — in Release only Warning and above
2. **Context** — every message contains: module, operation, key parameters
3. **No sensitive data** — no passwords, connection strings in clear text, no user PII
4. **Don't log SQL with user data** — only query templates
5. **Atomicity** — one logical operation = one log block with its result

### wxLog levels

| Macro | Level | When to use |
|--------|---------|-------------------|
| `wxLogError` | Error | Error needing attention; operation failed |
| `wxLogWarning` | Warning | Abnormal situation, work continues with limitations |
| `wxLogMessage` | Info | Significant events: opening a DB, saving a document |
| `wxLogVerbose` | Verbose | Detailed diagnostics (Debug build only) |
| `wxLogDebug` | Debug | Debug information (Debug build only) |

### Message format

A uniform format for structured parsing:

```
[MODULE] Action: result | param1=value1 param2=value2
```

Examples:

```
[Database] Open connection: success | dsn=firebird://localhost/mydb
[Database] Execute query: error | table=documents duration_ms=0
[Designer] Save document: success | doc_id=1042 rows=15
[Report] Generate report: error | report=balance detail=No data for period
```

### Setting up wxLog in the application

```cpp
// main.cpp — initialize logging

#include <wx/log.h>
#include <wx/ffile.h>

class OesFileLog : public wxLogFile
{
public:
    explicit OesFileLog(const wxString& filename)
        : wxLogFile(filename)
    {}

protected:
    void DoLogRecord(wxLogLevel level,
                     const wxString& msg,
                     const wxLogRecordInfo& info) override
    {
        wxString formatted;
        formatted.Printf("[%s] [%s:%d] %s",
            wxDateTime::Now().Format("%Y-%m-%d %H:%M:%S"),
            info.filename ? wxString::FromUTF8(info.filename) : wxString("?"),
            info.line,
            msg);
        wxLogFile::DoLogRecord(level, formatted, info);
    }
};

// In App::OnInit()
wxString logPath = wxStandardPaths::Get().GetUserDataDir() + "/logs/oes.log";
wxLog* fileLog = new OesFileLog(logPath);
wxLog::SetActiveTarget(fileLog);

#ifdef NDEBUG
    wxLog::SetLogLevel(wxLOG_Warning);   // Release: Warning and above
#else
    wxLog::SetLogLevel(wxLOG_Debug);     // Debug: everything
#endif
```

### What to log

| Event | Level | Example |
|---------|---------|--------|
| DB open/close | Info | `[Database] Connection opened: firebird://localhost/db` |
| DB connection error | Error | `[Database] Connection error: connection refused` |
| Slow query (> 1 sec) | Warning | `[Database] Slow query: table=reports duration_ms=1540` |
| Save document | Info | `[Designer] Document saved: id=42` |
| Save error | Error | `[Designer] Save error: disk full` |
| Module initialization | Info | `[Module] Reports module initialized` |
| Unexpected exception | Error | `[Core] Unhandled exception: what()=...` |
| User/session change | Info | `[Auth] Login: user=admin` |

### What NOT to log

- Passwords and full connection strings
- SQL parameters with personal data (names, tax IDs, phone numbers)
- Full binary buffers
- Private keys and tokens

---

## Log files

### Location

```
Windows: %APPDATA%\OES\logs\
  oes.log          — current log
  oes_2026-04-10.log  — rotated (by date)

Cross-platform (wxStandardPaths):
  wxStandardPaths::Get().GetUserDataDir() + "/logs/"
```

### Log rotation

```cpp
// Simple rotation: rename the old log when the application starts
void RotateLogFile(const wxString& logPath)
{
    if (wxFileExists(logPath))
    {
        wxString dated = logPath.BeforeLast('.') + "_"
            + wxDateTime::Now().Format("%Y-%m-%d")
            + ".log";
        wxRenameFile(logPath, dated);
    }

    // Delete logs older than 30 days
    wxString logDir = wxFileName(logPath).GetPath();
    wxArrayString oldLogs;
    wxDir::GetAllFiles(logDir, &oldLogs, "oes_*.log");
    wxDateTime cutoff = wxDateTime::Now() - wxDateSpan::Days(30);

    for (const auto& f : oldLogs)
    {
        wxFileName fn(f);
        wxDateTime modified;
        fn.GetTimes(nullptr, &modified, nullptr);
        if (modified.IsEarlierThan(cutoff))
            wxRemoveFile(f);
    }
}
```

---

## DB error handling

### Logging ibDatabaseLayer errors

```cpp
// Wrapper for safe query execution with logging
bool ExecuteQuery(ibDatabaseLayer* db, const wxString& sql,
                  const wxString& context)
{
    wxStopWatch sw;
    sw.Start();

    bool ok = db->RunQuery(sql);
    long elapsed = sw.Time();

    if (!ok)
    {
        wxLogError("[Database] Query error | context=%s error=%s",
            context, db->GetErrorMessage());
        return false;
    }

    if (elapsed > 1000)
    {
        wxLogWarning("[Database] Slow query | context=%s duration_ms=%ld",
            context, elapsed);
    }
    else
    {
        wxLogVerbose("[Database] Query completed | context=%s duration_ms=%ld",
            context, elapsed);
    }

    return true;
}
```

### Logging connection errors

```cpp
bool ibDatabaseLayer::OpenConnection(const DbConnectionParams& params)
{
    wxLogMessage("[Database] Opening connection | driver=%s host=%s db=%s",
        params.driver, params.host, params.database);

    if (!Open(params.BuildDSN()))
    {
        wxLogError("[Database] Connection error | driver=%s host=%s error=%s",
            params.driver, params.host, GetErrorMessage());
        return false;
    }

    wxLogMessage("[Database] Connection opened successfully | driver=%s",
        params.driver);
    return true;
}
```

---

## Crash dumps

> **Note: this section applies to Windows only.**
> On Linux crashes are handled via signal handlers (`SIGSEGV`, `SIGABRT`) and core dump files.
> To analyze a core dump on Linux use `gdb -c core ./OES` or `coredumpctl debug`.

### Windows MiniDump

```cpp
// crash_handler.cpp
#include <windows.h>
#include <dbghelp.h>
#pragma comment(lib, "dbghelp.lib")

LONG WINAPI OesUnhandledExceptionFilter(EXCEPTION_POINTERS* exInfo)
{
    wxString dumpPath = wxStandardPaths::Get().GetUserDataDir()
        + wxString::Format("/crash_%s.dmp",
            wxDateTime::Now().Format("%Y%m%d_%H%M%S"));

    HANDLE hFile = CreateFile(dumpPath.wc_str(), GENERIC_WRITE,
        0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);

    if (hFile != INVALID_HANDLE_VALUE)
    {
        MINIDUMP_EXCEPTION_INFORMATION dumpInfo{};
        dumpInfo.ThreadId          = GetCurrentThreadId();
        dumpInfo.ExceptionPointers = exInfo;
        dumpInfo.ClientPointers    = FALSE;

        MiniDumpWriteDump(GetCurrentProcess(), GetCurrentProcessId(),
            hFile, MiniDumpWithDataSegs, &dumpInfo, nullptr, nullptr);

        CloseHandle(hFile);
    }

    wxLogError("[Core] Critical exception. Dump saved: %s", dumpPath);
    return EXCEPTION_EXECUTE_HANDLER;
}

// In App::OnInit() — before any other initialization
SetUnhandledExceptionFilter(OesUnhandledExceptionFilter);
```

---

## Performance diagnostics

### Measuring operation time

```cpp
// Helper RAII class for measuring and logging
class OesScopeTimer
{
public:
    OesScopeTimer(const wxString& operation, long warnThresholdMs = 500)
        : m_operation(operation), m_warnMs(warnThresholdMs)
    {
        m_sw.Start();
    }

    ~OesScopeTimer()
    {
        long elapsed = m_sw.Time();
        if (elapsed >= m_warnMs)
        {
            wxLogWarning("[Perf] Slow operation: %s | duration_ms=%ld",
                m_operation, elapsed);
        }
        else
        {
            wxLogVerbose("[Perf] Operation completed: %s | duration_ms=%ld",
                m_operation, elapsed);
        }
    }

private:
    wxString   m_operation;
    wxStopWatch m_sw;
    long       m_warnMs;
};

// Usage
void ReportModule::GenerateReport(const ReportParams& params)
{
    OesScopeTimer timer("GenerateReport", 1000);
    // ... generation logic
}
```

---

## Health check

### Application health check function

```cpp
struct OesHealthStatus
{
    bool  databaseOk    = false;
    bool  configOk      = false;
    bool  diskSpaceOk   = false;
    wxString summary;
};

OesHealthStatus OesApp::CheckHealth()
{
    OesHealthStatus status;

    // Check the DB connection
    if (m_db && m_db->IsOpen())
    {
        ibDatabaseResultSet* rs = m_db->RunQueryWithResults("SELECT 1 FROM RDB$DATABASE");
        status.databaseOk = (rs != nullptr);
        if (rs) rs->Close();
    }

    // Check configuration
    status.configOk = wxFileExists(m_configPath);

    // Check free disk space (at least 100 MB)
    // Check the disk where application data is stored,
    // not the current working directory (wxGetCwd() may point to the system drive).
    wxDiskspaceSize_t freeSpace = 0;
    wxString appDataDir = wxStandardPaths::Get().GetUserDataDir();
    wxGetDiskSpace(appDataDir, nullptr, &freeSpace);
    status.diskSpaceOk = (freeSpace > 100LL * 1024 * 1024);

    // Final status
    if (!status.databaseOk)
        status.summary += "Database: ERROR; ";
    if (!status.configOk)
        status.summary += "Config: ERROR; ";
    if (!status.diskSpaceOk)
        status.summary += "DiskSpace: WARNING; ";

    if (status.summary.IsEmpty())
        status.summary = "OK";

    return status;
}
```

---

## Monitoring checklist

### During development

- [ ] All public DB layer methods log errors via `wxLogError`
- [ ] Slow operations (> 500 ms) are logged via `wxLogWarning`
- [ ] Module initialization is logged via `wxLogMessage`
- [ ] No sensitive data in logs

### When building Release

- [ ] Log level set to `wxLOG_Warning` (not Debug)
- [ ] Crash handler hooked up (MiniDump)
- [ ] Log path points to `%APPDATA%\OES\logs\`
- [ ] Log rotation enabled

### Periodically

- [ ] Review logs for recurring errors
- [ ] Analyze warnings about slow queries
- [ ] Ensure crash dumps (if any) have been analyzed
- [ ] Clean up old log files (> 30 days)
