# 04. OES service management

> OES can run as a Windows Service (sc.exe, NSSM) or a Linux systemd daemon.
> Unlike Node.js with PM2, a C++ application is registered as a native system service.

---

## Overview: OES launch modes

```
Desktop mode:
  - Regular launch as a GUI application (wxWidgets)
  - Entry point: src/engine/enterprise/mainApp.cpp
  - Started by the user, runs while open
  - No process manager required

Windows Service mode:
  - OES runs as a background Windows Service
  - Started automatically at boot
  - Tools: sc.exe (built-in), NSSM (recommended), WinSW
  - OES can register itself as a service

macOS Daemon mode:
  - OES daemon runs via launchd (launchctl)
  - Entry point: src/engine/daemon/daemon.cpp
  - Plist file in /Library/LaunchDaemons/ (system) or ~/Library/LaunchAgents/ (user)
  - Tools: launchctl

Linux Daemon mode:
  - OES runs as a systemd unit
  - Entry point: src/engine/daemon/daemon.cpp
  - Started automatically at boot
  - Tools: systemctl, journalctl
```

---

## Windows Service

### Option 1: Registration via OES built-in mechanism

```cmd
REM OES supports command-line parameters for service management
REM (implemented through the Windows Service API in C++)

REM Install as a service (as Administrator)
"C:\Program Files\OES\oes-daemon.exe" --install-service

REM Remove the service
"C:\Program Files\OES\oes-daemon.exe" --uninstall-service

REM Start
net start OESDaemon
REM or
sc start OESDaemon

REM Stop
net stop OESDaemon
REM or
sc stop OESDaemon
```

### Option 2: sc.exe (built into Windows)

```cmd
REM Register the service
sc create OESDaemon ^
  binPath= "\"C:\Program Files\OES\oes-daemon.exe\" --config \"C:\ProgramData\OES\oes.conf\" --service" ^
  DisplayName= "OES Enterprise Platform Daemon" ^
  start= auto ^
  obj= LocalSystem ^
  description= "Open Enterprise Solutions low-code platform server daemon"

REM Set the description
sc description OESDaemon "OES Enterprise low-code/no-code platform running in server mode"

REM Configure failure recovery (restart after 10 seconds)
sc failure OESDaemon reset= 86400 actions= restart/10000/restart/30000/restart/60000

REM Start
sc start OESDaemon

REM Check status
sc query OESDaemon

REM Stop
sc stop OESDaemon

REM Remove
sc delete OESDaemon
```

### Option 3: NSSM (Non-Sucking Service Manager) — recommended

NSSM lets you run any executable as a service with advanced settings without changing application code.

#### Installing NSSM

```powershell
# Via Chocolatey
choco install nssm

# Or download from https://nssm.cc/
# Copy nssm.exe to C:\Windows\System32\
```

#### Registering OES as a service

```cmd
REM Interactive installation (opens a GUI)
nssm install OESDaemon

REM Installation via command line
nssm install OESDaemon "C:\Program Files\OES\oes-daemon.exe"

REM Configure parameters
nssm set OESDaemon AppParameters "--config \"C:\ProgramData\OES\oes.conf\" --daemon"
nssm set OESDaemon AppDirectory "C:\Program Files\OES"
nssm set OESDaemon DisplayName "OES Enterprise Platform Daemon"
nssm set OESDaemon Description "Open Enterprise Solutions low-code platform in server mode"
nssm set OESDaemon Start SERVICE_AUTO_START

REM Logs via NSSM (stdout/stderr -> file)
nssm set OESDaemon AppStdout "C:\ProgramData\OES\Logs\daemon-out.log"
nssm set OESDaemon AppStderr "C:\ProgramData\OES\Logs\daemon-err.log"
nssm set OESDaemon AppRotateFiles 1
nssm set OESDaemon AppRotateOnline 1
nssm set OESDaemon AppRotateSeconds 86400
nssm set OESDaemon AppRotateBytes 52428800

REM Configure restart on failure
nssm set OESDaemon AppExit Default Restart
nssm set OESDaemon AppRestartDelay 5000

REM Dependencies (start after PostgreSQL / Firebird)
nssm set OESDaemon DependOnService FirebirdGuardianDefaultInstance

REM Start
nssm start OESDaemon

REM Check status
nssm status OESDaemon

REM Stop
nssm stop OESDaemon

REM Restart
nssm restart OESDaemon

REM Remove the service
nssm remove OESDaemon confirm
```

#### Management via GUI

```cmd
REM Open the service configuration editor
nssm edit OESDaemon
```

---

## Windows Service: management via PowerShell

```powershell
# Check status
Get-Service OESDaemon

# Start
Start-Service OESDaemon

# Stop
Stop-Service OESDaemon

# Restart
Restart-Service OESDaemon

# Enable autostart
Set-Service OESDaemon -StartupType Automatic

# Disable autostart
Set-Service OESDaemon -StartupType Disabled

# List all OES-related services
Get-Service | Where-Object { $_.DisplayName -like "*OES*" }

# Detailed information
Get-Service OESDaemon | Select-Object *

# Service event logs (Windows Event Log)
Get-EventLog -LogName Application -Source OESDaemon -Newest 20
Get-EventLog -LogName System -Source "Service Control Manager" -Newest 20 |
  Where-Object { $_.Message -like "*OESDaemon*" }
```

---

## Windows Service: management scripts

### deploy-windows-service.ps1

```powershell
#!/usr/bin/env pwsh
# deploy-windows-service.ps1 - update OES daemon on Windows Server
param(
    [string]$InstallerPath = ".\OES-Setup.exe",
    [string]$ServiceName = "OESDaemon",
    [int]$StopTimeoutSec = 30
)

$ErrorActionPreference = "Stop"

Write-Host "=== OES Daemon deploy ===" -ForegroundColor Cyan

# Verify the installer exists
if (-not (Test-Path $InstallerPath)) {
    Write-Error "Installer not found: $InstallerPath"
    exit 1
}

# Stop the service
$service = Get-Service -Name $ServiceName -ErrorAction SilentlyContinue
if ($service -and $service.Status -eq "Running") {
    Write-Host "Stopping service $ServiceName..."
    Stop-Service -Name $ServiceName -Force
    $service.WaitForStatus("Stopped", [TimeSpan]::FromSeconds($StopTimeoutSec))
    Write-Host "Service stopped."
}

# Run the installer (silent install)
Write-Host "Installing update..."
$process = Start-Process -FilePath $InstallerPath -ArgumentList "/SILENT /NORESTART" -Wait -PassThru
if ($process.ExitCode -ne 0) {
    Write-Error "Installation failed: exit code $($process.ExitCode)"
    exit 1
}

# Start the service
Write-Host "Starting service $ServiceName..."
Start-Service -Name $ServiceName
Start-Sleep -Seconds 3

# Check status
$service = Get-Service -Name $ServiceName
if ($service.Status -eq "Running") {
    Write-Host "=== Deploy completed successfully ===" -ForegroundColor Green
    Write-Host "Status: $($service.Status)"
} else {
    Write-Error "Service failed to start! Status: $($service.Status)"
    Get-EventLog -LogName Application -Source $ServiceName -Newest 10 |
      Format-List TimeGenerated, EntryType, Message
    exit 1
}
```

---

## macOS launchd (Daemon mode)

### Plist file

```bash
sudo nano /Library/LaunchDaemons/com.oes.daemon.plist
```

```xml
<?xml version="1.0" encoding="UTF-8"?>
<!DOCTYPE plist PUBLIC "-//Apple//DTD PLIST 1.0//EN"
  "http://www.apple.com/DTDs/PropertyList-1.0.dtd">
<plist version="1.0">
<dict>
    <key>Label</key>
    <string>com.oes.daemon</string>

    <key>ProgramArguments</key>
    <array>
        <string>/usr/local/opt/oes/bin/oes-daemon</string>
        <string>--config</string>
        <string>/etc/oes/oes.conf</string>
        <string>--foreground</string>
    </array>

    <key>UserName</key>
    <string>oes</string>

    <key>WorkingDirectory</key>
    <string>/var/lib/oes</string>

    <key>RunAtLoad</key>
    <true/>

    <key>KeepAlive</key>
    <true/>

    <key>StandardOutPath</key>
    <string>/var/log/oes/daemon.log</string>

    <key>StandardErrorPath</key>
    <string>/var/log/oes/daemon-error.log</string>

    <key>EnvironmentVariables</key>
    <dict>
        <key>OES_LOG_LEVEL</key>
        <string>info</string>
    </dict>
</dict>
</plist>
```

### Management via launchctl

```bash
# Load and start
sudo launchctl load /Library/LaunchDaemons/com.oes.daemon.plist

# Stop and unload
sudo launchctl unload /Library/LaunchDaemons/com.oes.daemon.plist

# Start manually (already loaded)
sudo launchctl start com.oes.daemon

# Stop
sudo launchctl stop com.oes.daemon

# Status
sudo launchctl list | grep oes

# Logs
tail -f /var/log/oes/daemon.log
```

---

## Linux systemd (Daemon mode)

### Unit file

```bash
sudo nano /etc/systemd/system/oes-daemon.service
```

```ini
[Unit]
Description=OES Enterprise Platform Daemon
Documentation=https://docs.oes-vendor.com/daemon
After=network-online.target postgresql.service
Wants=network-online.target
# If using only Firebird embedded - remove the postgresql dependency

[Service]
Type=simple
User=oes
Group=oes
WorkingDirectory=/var/lib/oes
ExecStart=/opt/oes/oes-daemon \
    --config /etc/oes/oes.conf \
    --log-file /var/log/oes/daemon.log \
    --pid-file /run/oes/daemon.pid
ExecReload=/bin/kill -HUP $MAINPID
ExecStop=/bin/kill -TERM $MAINPID

# Restart
Restart=on-failure
RestartSec=10s
StartLimitInterval=120s
StartLimitBurst=5

# Logs (journald + file via --log-file)
StandardOutput=journal
StandardError=journal
SyslogIdentifier=oes-daemon

# PID file
RuntimeDirectory=oes
RuntimeDirectoryMode=0755

# Security
NoNewPrivileges=yes
ProtectSystem=strict
ProtectHome=yes
ReadWritePaths=/var/lib/oes /var/log/oes /tmp
PrivateTmp=yes
PrivateDevices=yes
CapabilityBoundingSet=CAP_NET_BIND_SERVICE
AmbientCapabilities=CAP_NET_BIND_SERVICE

[Install]
WantedBy=multi-user.target
```

### Management via systemctl

```bash
# Apply unit-file changes
sudo systemctl daemon-reload

# Enable autostart
sudo systemctl enable oes-daemon

# Start
sudo systemctl start oes-daemon

# Stop
sudo systemctl stop oes-daemon

# Restart (with downtime)
sudo systemctl restart oes-daemon

# Reload configuration (no downtime, if OES supports SIGHUP)
sudo systemctl reload oes-daemon

# Status
sudo systemctl status oes-daemon

# Disable autostart
sudo systemctl disable oes-daemon
```

### Log journal (journalctl)

```bash
# Last 50 lines
sudo journalctl -u oes-daemon -n 50

# Follow in real time
sudo journalctl -u oes-daemon -f

# Logs from today
sudo journalctl -u oes-daemon --since today

# Logs from the last hour
sudo journalctl -u oes-daemon --since "1 hour ago"

# Logs from a specific time
sudo journalctl -u oes-daemon --since "2025-01-15 10:00:00"

# Errors only
sudo journalctl -u oes-daemon -p err

# JSON format (for parsing)
sudo journalctl -u oes-daemon -o json | jq '.'

# Export logs to a file
sudo journalctl -u oes-daemon --since "1 week ago" > /tmp/oes-logs.txt
```

---

## Deploy script (Linux)

```bash
#!/bin/bash
# /opt/scripts/deploy-oes-daemon.sh
set -e

SERVICE_NAME="oes-daemon"
BINARY_PATH="/opt/oes/oes-daemon"
NEW_BINARY="${1:-/tmp/oes-daemon-new}"
HEALTH_CHECK_URL="http://localhost:8765/health"

echo "=== OES Daemon deploy (Linux) ==="

# Verify the new binary exists
if [ ! -f "$NEW_BINARY" ]; then
  echo "ERROR: binary not found: $NEW_BINARY"
  exit 1
fi

# Ensure the binary is executable
chmod +x "$NEW_BINARY"

# Save the old version as a backup
if [ -f "$BINARY_PATH" ]; then
  cp "$BINARY_PATH" "${BINARY_PATH}.backup"
  echo "Backup: ${BINARY_PATH}.backup"
fi

# Stop the service
echo "Stopping $SERVICE_NAME..."
sudo systemctl stop "$SERVICE_NAME"

# Replace the binary
echo "Updating binary..."
sudo cp "$NEW_BINARY" "$BINARY_PATH"
sudo chown oes:oes "$BINARY_PATH"
sudo chmod 755 "$BINARY_PATH"

# Start the service
echo "Starting $SERVICE_NAME..."
sudo systemctl start "$SERVICE_NAME"

# Health check
sleep 3
if curl -sf "$HEALTH_CHECK_URL" > /dev/null 2>&1; then
  echo "=== Deploy completed successfully ==="
  sudo systemctl status "$SERVICE_NAME" --no-pager
else
  echo "WARNING: health check failed"
  echo "Check the logs: sudo journalctl -u $SERVICE_NAME -n 30"
  # Do NOT rollback automatically - the daemon may have started but the endpoint
  # may not respond immediately
fi

# Remove the temporary file
rm -f "$NEW_BINARY"
```

```bash
# Make executable
chmod +x /opt/scripts/deploy-oes-daemon.sh

# Usage
./deploy-oes-daemon.sh /tmp/oes-daemon-new
```

### Rollback

```bash
#!/bin/bash
# /opt/scripts/rollback-oes-daemon.sh
set -e

BINARY_PATH="/opt/oes/oes-daemon"
SERVICE_NAME="oes-daemon"

if [ ! -f "${BINARY_PATH}.backup" ]; then
  echo "ERROR: backup not found"
  exit 1
fi

echo "=== OES Daemon rollback ==="
sudo systemctl stop "$SERVICE_NAME"
sudo cp "${BINARY_PATH}.backup" "$BINARY_PATH"
sudo chown oes:oes "$BINARY_PATH"
sudo chmod 755 "$BINARY_PATH"
sudo systemctl start "$SERVICE_NAME"

echo "Rollback complete."
sudo systemctl status "$SERVICE_NAME" --no-pager
```

---

## Process monitoring

### Linux

```bash
# Process status
sudo systemctl is-active oes-daemon

# PID
cat /run/oes/daemon.pid

# Resource usage
ps aux | grep oes-daemon
top -p $(cat /run/oes/daemon.pid)

# Open files / sockets
sudo lsof -p $(cat /run/oes/daemon.pid)

# Active DB connections (via strace / netstat)
sudo ss -tlnp | grep oes
sudo netstat -tlnp | grep oes-daemon
```

### Windows

```powershell
# Detailed status with resource usage
Get-Process oes-daemon | Select-Object CPU, WorkingSet, VirtualMemorySize, Id

# Or via Task Manager / Process Explorer

# Active connections
netstat -ano | findstr "8765"

# Logs from Event Log
Get-EventLog -LogName Application -Newest 20 |
  Where-Object { $_.Source -eq "OESDaemon" } |
  Format-List TimeGenerated, EntryType, Message

# NSSM logs (if NSSM is used)
Get-Content "C:\ProgramData\OES\Logs\daemon-out.log" -Tail 50 -Wait
```

---

## Common issues

```
Service does not start on Windows:
  1. Check the Event Log: eventvwr.msc -> Windows Logs -> Application
  2. Check permissions on oes.conf (must be readable by the service account)
  3. Check the path to oes-daemon.exe in sc/NSSM
  4. Run manually as Administrator for debugging:
     "C:\Program Files\OES\oes-daemon.exe" --config "C:\ProgramData\OES\oes.conf" --foreground

Service crashes and restarts (Windows):
  sc query OESDaemon          - check the state
  nssm status OESDaemon       - if using NSSM
  Check C:\ProgramData\OES\Logs\daemon-err.log

systemd unit does not start (Linux):
  sudo journalctl -u oes-daemon -n 50   - last 50 lines
  sudo systemd-analyze verify /etc/systemd/system/oes-daemon.service
  sudo -u oes /opt/oes/oes-daemon --config /etc/oes/oes.conf --check-config

DB permission issues:
  Windows: ensure the service runs as an account with access to C:\ProgramData\OES\
  Linux:   check chown oes:oes /var/lib/oes/databases/
```

---

## Tool comparison

```
sc.exe (Windows built-in):
  + No installation required
  + Low-level management
  - No stdout/stderr capture to a log file (must be implemented in code)
  - Limited restart settings

NSSM (recommended for Windows):
  + Runs any exe as a service without code changes
  + Captures stdout/stderr to log files automatically
  + Advanced restart settings and a GUI editor
  + Log rotation
  - Requires installation (choco install nssm)

WinSW:
  + XML configuration, easy to version
  + No separate manager required
  - Less flexible than NSSM

launchd (macOS):
  + Built into macOS, native integration
  + Plist configuration format
  + Autostart at boot via RunAtLoad
  + Restart via KeepAlive
  - No journald-compatible centralized logging
  - Less flexible isolation settings compared to systemd

systemd (Linux):
  + Built into modern distributions
  + journald (centralized logging)
  + Dependency management
  + Security isolation (namespaces, capabilities)
  + Monitoring and auto-restart
```
