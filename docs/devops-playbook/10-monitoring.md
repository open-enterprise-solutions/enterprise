# 10. Monitoring

> Crash reporting, telemetry, health checks for OES daemon mode, monitoring of the embedded Firebird DB.

---

## What to monitor in OES

```
Desktop application (client side):
  - Crashes: dump files, call stacks (Crashpad/Breakpad)
  - C++ exceptions (uncaught, SEH on Windows)
  - UI performance (wxWidgets hangs > 1 sec)
  - Process memory (leaks, growth > 500 MB)
  - Application launch time and document open time
  - DB connection errors (Firebird embedded)
  - File save errors

Daemon/Service (server side):
  - Service status (Windows Service / systemd)
  - Service process CPU / RAM
  - Active client connections
  - Request response time
  - Errors in logs
  - Firebird/PostgreSQL database size and state

Database:
  - Firebird: corruption presence (gfix -validate)
  - PostgreSQL: active connections, slow queries, size
  - SQLite: integrity (PRAGMA integrity_check)
  - Free disk space for the DB
```

---

## Crash Reporting: Crashpad (recommended for OES)

### Integrating Crashpad in C++

```cpp
// src/crash_reporter.cpp
#include "client/crashpad_client.h"
#include "client/crash_report_database.h"
#include "client/settings.h"

bool InitCrashpad(const std::string& reports_dir, const std::string& upload_url) {
    // Path to crashpad_handler.exe (next to the application)
    base::FilePath handler(L"crashpad_handler.exe");
    
    // Directory for storing dumps
    base::FilePath db_path(base::UTF8ToWide(reports_dir));
    
    // Environment metrics
    std::map<std::string, std::string> annotations;
    annotations["product"] = "OES";
    annotations["version"] = OES_VERSION_STRING;
    annotations["os"] = "Windows";
    
    // Initialization
    crashpad::CrashpadClient client;
    bool result = client.StartHandler(
        handler,
        db_path,
        db_path,
        upload_url,      // "" - local dumps only, no upload
        annotations,
        {},
        true,            // restartable
        true             // asynchronous_start
    );
    
    return result;
}

// In main() or WinMain():
// InitCrashpad(GetAppDataPath() + "\\OES\\CrashReports", "");
```

### Crash reports directory structure

```
%APPDATA%\OES\CrashReports\
|-- attachments\          - additional files (application log)
|-- completed\            - processed dumps
|-- new\                  - new dumps (not yet processed)
|-- pending\              - awaiting upload
'-- settings.dat
```

### Crash dump collection and analysis script (when supported)

```powershell
# scripts/analyze-crashes.ps1
# Analyze crash dumps from the user directory

param(
    [string]$DumpDir = "$env:APPDATA\OES\CrashReports",
    [string]$SymbolsDir = ".\symbols",
    [string]$ReportDir = ".\crash-analysis"
)

$dumps = Get-ChildItem -Path $DumpDir -Filter "*.dmp" -Recurse
Write-Host "Dumps found: $($dumps.Count)"

foreach ($dump in $dumps) {
    Write-Host "Analyzing: $($dump.Name)"
    
    # cdb (Windows Debugger) for analysis
    $analysis = & cdb -z $dump.FullName -c "!analyze -v; .ecxr; k; q" 2>&1
    
    $reportFile = Join-Path $ReportDir "$($dump.BaseName)-analysis.txt"
    $analysis | Out-File $reportFile -Encoding UTF8
    
    Write-Host "  Report: $reportFile"
}
```

### Symbol configuration for dump analysis

```
CI/CD on Release build:
  1. MSBuild generates .pdb files
  2. Save .pdb to a symbol server or GitHub Release artifacts
  3. When analyzing a dump: specify the path to symbols

In Visual Studio:
  Debug -> Options -> Debugging -> Symbols
  -> Symbol file (.pdb) locations: \\symbol-server\OES\1.2.3\

Commands for dump analysis:
  windbg -z crash.dmp -y "srv*c:\symbols*https://msdl.microsoft.com/download/symbols;.\symbols"
```

---

## Application logging

### Simple file logger for OES

```cpp
// src/logger.h
#pragma once
#include <wx/log.h>
#include <wx/file.h>

class OESLogger : public wxLog {
public:
    static void Init(const wxString& logPath) {
        wxLog::SetActiveTarget(new OESLogger(logPath));
        wxLog::SetLogLevel(wxLOG_Debug);
    }
    
    static void SetLevel(wxLogLevel level) {
        wxLog::SetLogLevel(level);
    }
    
protected:
    void DoLogTextAtLevel(wxLogLevel level, const wxString& msg) override {
        wxString prefix;
        switch (level) {
            case wxLOG_Error:   prefix = "[ERROR]"; break;
            case wxLOG_Warning: prefix = "[WARN] "; break;
            case wxLOG_Info:    prefix = "[INFO] "; break;
            default:            prefix = "[DEBUG]"; break;
        }
        
        wxString line = wxNow() + " " + prefix + " " + msg + "\n";
        m_file.Write(line);
        m_file.Flush();
    }
    
private:
    explicit OESLogger(const wxString& path) : m_file(path, wxFile::write_append) {}
    wxFile m_file;
};
```

### Log rotation

```cpp
// Cap log size at startup
void RotateLogIfNeeded(const wxString& logPath, size_t maxSizeMb = 10) {
    wxFileName fn(logPath);
    if (fn.GetSize() > maxSizeMb * 1024 * 1024) {
        wxString archivePath = logPath + ".1";
        wxRemoveFile(archivePath);
        wxRenameFile(logPath, archivePath);
    }
}
```

### Log file locations

```
Windows:
  %APPDATA%\OES\Logs\oes.log         - main log
  %APPDATA%\OES\Logs\oes.log.1       - previous log
  %APPDATA%\OES\Logs\oes-daemon.log  - daemon/service log

macOS (Desktop):
  ~/Library/Logs/OES/oes.log
  ~/Library/Logs/DiagnosticReports/  - crash reports
macOS (Daemon - launchd):
  /var/log/oes/daemon.log
  /var/log/oes/daemon-error.log

Linux (Desktop):
  ~/.local/share/OES/logs/oes.log
Linux (Daemon - systemd):
  /var/log/oes/oes-daemon.log        - in service mode
  journalctl -u oes-daemon           - via journald
```

---

## Monitoring the Windows Service (daemon mode)

### Service health-check script

```powershell
#!/usr/bin/env pwsh
# scripts/check-oes-service.ps1
# Run from Task Scheduler every 5 minutes

$ServiceName = "OESDaemon"
$TelegramToken = $env:TELEGRAM_BOT_TOKEN
$TelegramChatId = $env:TELEGRAM_CHAT_ID

function Send-TelegramAlert {
    param([string]$Message)
    if (-not $TelegramToken) { return }
    
    Invoke-RestMethod `
        -Uri "https://api.telegram.org/bot$TelegramToken/sendMessage" `
        -Method POST `
        -ContentType "application/json" `
        -Body (ConvertTo-Json @{
            chat_id = $TelegramChatId
            text = $Message
        }) | Out-Null
}

$service = Get-Service -Name $ServiceName -ErrorAction SilentlyContinue

if ($null -eq $service) {
    Send-TelegramAlert "CRITICAL: Service $ServiceName not found on $(hostname)"
    exit 1
}

if ($service.Status -ne "Running") {
    $msg = "CRITICAL: Service $ServiceName stopped on $(hostname). Attempting restart..."
    Write-Host $msg
    Send-TelegramAlert $msg
    
    # Auto-recovery attempt
    Start-Service -Name $ServiceName
    Start-Sleep -Seconds 5
    
    $service.Refresh()
    if ($service.Status -eq "Running") {
        Send-TelegramAlert "INFO: Service $ServiceName restarted successfully on $(hostname)"
    } else {
        Send-TelegramAlert "CRITICAL: Service $ServiceName could not be started on $(hostname)"
        exit 1
    }
} else {
    Write-Host "OK: Service $ServiceName is running"
}

# Check memory usage
$proc = Get-Process -Name "oesd" -ErrorAction SilentlyContinue
if ($proc -and $proc.WorkingSet64 -gt 500MB) {
    $memMb = [math]::Round($proc.WorkingSet64 / 1MB)
    Send-TelegramAlert "WARNING: OES daemon using ${memMb} MB memory on $(hostname)"
}
```

### macOS launchd monitoring

```bash
# Daemon status (macOS)
sudo launchctl list | grep oes

# Daemon logs
tail -f /var/log/oes/daemon.log

# Via unified logging (macOS 10.12+)
log show --predicate 'process == "oes-daemon"' --last 1h --style syslog

# Restart if the daemon crashed
sudo launchctl stop com.oes.daemon
sudo launchctl start com.oes.daemon
```

### Systemd unit for Linux (daemon mode)

```ini
# /etc/systemd/system/oes-daemon.service
[Unit]
Description=OES Enterprise Daemon
After=network.target firebird3.0-guardian.service
Wants=firebird3.0-guardian.service

[Service]
Type=simple
User=oes
Group=oes
ExecStart=/opt/oes/bin/oesd --config /etc/oes/daemon.conf
ExecReload=/bin/kill -HUP $MAINPID
Restart=on-failure
RestartSec=10s
StartLimitInterval=60s
StartLimitBurst=3

# Logging via journald
StandardOutput=journal
StandardError=journal
SyslogIdentifier=oes-daemon

# Security
NoNewPrivileges=yes
ProtectSystem=strict
ProtectHome=yes
ReadWritePaths=/var/lib/oes /var/log/oes

[Install]
WantedBy=multi-user.target
```

```bash
# Management commands
sudo systemctl enable oes-daemon
sudo systemctl start oes-daemon
sudo systemctl status oes-daemon
sudo journalctl -u oes-daemon -f          # Logs in real time
sudo journalctl -u oes-daemon --since "1 hour ago"
```

---

## Firebird database monitoring

### DB integrity check script

```bash
#!/bin/bash
# scripts/check-firebird-db.sh
# Run daily via cron

DB_PATH="/var/lib/oes/data/oes.fdb"
LOG_FILE="/var/log/oes/db-check.log"
TELEGRAM_TOKEN="$TELEGRAM_BOT_TOKEN"
TELEGRAM_CHAT="$TELEGRAM_CHAT_ID"

log() { echo "[$(date '+%Y-%m-%d %H:%M:%S')] $1" | tee -a "$LOG_FILE"; }

send_alert() {
    if [ -n "$TELEGRAM_TOKEN" ]; then
        curl -s -X POST \
            "https://api.telegram.org/bot${TELEGRAM_TOKEN}/sendMessage" \
            -d "chat_id=${TELEGRAM_CHAT}" \
            -d "text=$1" > /dev/null
    fi
}

# Verify the file exists and is not empty
if [ ! -f "$DB_PATH" ]; then
    log "ERROR: database file not found: $DB_PATH"
    send_alert "CRITICAL: OES DB not found on $(hostname)"
    exit 1
fi

DB_SIZE=$(du -h "$DB_PATH" | cut -f1)
log "DB size: $DB_SIZE"

# Integrity check via gfix
log "Running gfix -validate..."
VALIDATE_RESULT=$(gfix -user SYSDBA -password masterkey -validate -full "$DB_PATH" 2>&1)

if echo "$VALIDATE_RESULT" | grep -qi "error\|corruption\|damaged"; then
    log "INTEGRITY ERROR: $VALIDATE_RESULT"
    send_alert "CRITICAL: OES DB integrity error on $(hostname)! Check immediately."
    exit 1
else
    log "Integrity check: OK"
fi

# Free space check (at least 1 GB)
FREE_KB=$(df "$(dirname "$DB_PATH")" | tail -1 | awk '{print $4}')
if [ "$FREE_KB" -lt 1048576 ]; then
    FREE_MB=$((FREE_KB / 1024))
    log "WARNING: low free space for DB: ${FREE_MB} MB"
    send_alert "WARNING: Low free space for OES DB on $(hostname): ${FREE_MB} MB"
fi

log "DB check completed successfully"
```

### PostgreSQL monitoring for OES (if used)

```bash
#!/bin/bash
# scripts/check-oes-postgresql.sh

PG_HOST="localhost"
PG_PORT="5432"
PG_DB="oes_db"
PG_USER="oes_user"

# Availability check
if ! pg_isready -h "$PG_HOST" -p "$PG_PORT" -U "$PG_USER" > /dev/null 2>&1; then
    echo "CRITICAL: PostgreSQL unavailable"
    exit 1
fi

# Check active connections
CONN_COUNT=$(PGPASSWORD="$PG_PASSWORD" psql -h "$PG_HOST" -U "$PG_USER" -d "$PG_DB" \
    -t -c "SELECT count(*) FROM pg_stat_activity WHERE datname = '$PG_DB';" | tr -d ' ')

MAX_CONN=$(PGPASSWORD="$PG_PASSWORD" psql -h "$PG_HOST" -U "$PG_USER" -d "$PG_DB" \
    -t -c "SHOW max_connections;" | tr -d ' ')

echo "Connections: $CONN_COUNT / $MAX_CONN"

# Alert if > 80%
THRESHOLD=$((MAX_CONN * 80 / 100))
if [ "$CONN_COUNT" -gt "$THRESHOLD" ]; then
    echo "WARNING: many active connections ($CONN_COUNT/$MAX_CONN)"
fi

# Slow queries (> 5 seconds)
SLOW_QUERIES=$(PGPASSWORD="$PG_PASSWORD" psql -h "$PG_HOST" -U "$PG_USER" -d "$PG_DB" \
    -t -c "SELECT count(*) FROM pg_stat_activity 
           WHERE state = 'active' AND query_start < now() - interval '5 seconds';")

if [ "$SLOW_QUERIES" -gt 0 ]; then
    echo "WARNING: $SLOW_QUERIES slow queries"
fi
```

---

## Simple usage telemetry (opt-in)

```cpp
// src/telemetry.cpp
// Anonymous telemetry - only with explicit user consent

class Telemetry {
public:
    static Telemetry& Instance() {
        static Telemetry instance;
        return instance;
    }
    
    void SetEnabled(bool enabled) { m_enabled = enabled; }
    bool IsEnabled() const { return m_enabled; }
    
    // Record a usage event into a local file
    void TrackEvent(const wxString& category, const wxString& action) {
        if (!m_enabled) return;
        
        // Local file only, no transmission without an explicit button
        wxLogMessage("[TELEMETRY] %s / %s", category, action);
    }
    
    // Launch count (for diagnostics)
    void IncrementLaunchCount() {
        wxConfig config("OES", "Tetracode");
        long count = 0;
        config.Read("/Telemetry/LaunchCount", &count, 0);
        config.Write("/Telemetry/LaunchCount", count + 1);
        config.Write("/Telemetry/LastLaunch", wxDateTime::Now().FormatISOCombined());
    }
    
private:
    bool m_enabled = false;  // Disabled by default
};
```

---

## Docker container monitoring (for daemon mode in a container)

```bash
#!/bin/bash
# scripts/check-oes-containers.sh

REQUIRED="oes-daemon oes-db"
ISSUES=""

for name in $REQUIRED; do
    STATUS=$(docker inspect --format='{{.State.Status}}' "$name" 2>/dev/null)
    if [ "$STATUS" != "running" ]; then
        ISSUES="$ISSUES\nCRITICAL: $name not running ($STATUS)"
    else
        echo "OK: $name running"
    fi
done

if [ -n "$ISSUES" ]; then
    echo -e "$ISSUES"
    curl -s -X POST "https://api.telegram.org/bot$TELEGRAM_BOT_TOKEN/sendMessage" \
        -d chat_id="$TELEGRAM_CHAT_ID" \
        -d "text=OES Docker Alert:$(echo -e "$ISSUES")" > /dev/null
fi
```

---

## OES monitoring checklist

```
Crash reporting:
  [ ] Crashpad initialized at application start
  [ ] .pdb symbols saved for every Release build
  [ ] Crash dump directory configured and accessible
  [ ] Crash dumps included in user error reports

Logging:
  [ ] File logger initialized in main()
  [ ] Log level depends on the mode (Debug/Release)
  [ ] Log rotation configured (max size 10 MB)
  [ ] Logs included in diagnostic bundles

Daemon/Service (if applicable):
  [ ] Windows Service configured to autostart
  [ ] Systemd unit configured with Restart=on-failure
  [ ] Service check script in Task Scheduler / cron
  [ ] Telegram alerts on service stop

Database:
  [ ] Daily Firebird integrity check (gfix)
  [ ] Monitoring free space for the DB
  [ ] PostgreSQL: connection and slow-query monitoring
  [ ] Alert on DB connection errors

Performance:
  [ ] Process memory usage monitored
  [ ] Application launch time logged
```
