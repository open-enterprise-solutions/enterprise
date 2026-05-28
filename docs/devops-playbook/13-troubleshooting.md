# 13. Troubleshooting

> Quick checklists for the OES C++ desktop application: crash dumps, debug symbols, remote diagnostics, common wxWidgets and Firebird issues.

---

## Quick checks: where to start

### Windows (application)

```powershell
# Is OES running?
Get-Process -Name "oes" -ErrorAction SilentlyContinue

# Check recent errors in the Event Log
Get-EventLog -LogName Application -Source "OES*" -Newest 20 | Format-List

# Crash dumps - any recent ones?
Get-ChildItem "$env:APPDATA\OES\CrashReports\new\" -Filter "*.dmp" | Sort-Object LastWriteTime -Descending | Select-Object -First 5

# Application log
Get-Content "$env:APPDATA\OES\Logs\oes.log" -Tail 50

# Database file - accessible?
Test-Path "$env:APPDATA\OES\data\*.fdb"
```

### macOS (application / daemon)

```bash
# Is OES running?
pgrep -l oes

# Daemon status (launchd)
sudo launchctl list | grep oes

# Daemon logs
tail -50 /var/log/oes/daemon.log
# or via unified logging:
log show --predicate 'process == "oes-daemon"' --last 1h

# Database file
ls -lh ~/Library/Application\ Support/OES/data/    # desktop
ls -lh /var/lib/oes/data/                           # daemon

# Process
ps aux | grep oes

# Files opened by the process
sudo lsof -p $(pgrep oes-daemon) | head -30
```

### Linux (daemon mode)

```bash
# Service status
sudo systemctl status oes-daemon

# Recent logs (journald)
sudo journalctl -u oes-daemon --since "1 hour ago" --no-pager

# Application logs
tail -50 /var/log/oes/oes-daemon.log

# Database file
ls -lh /var/lib/oes/data/

# Process
ps aux | grep oesd

# Files opened by the process
sudo lsof -p $(pgrep oesd) | head -30

# Memory usage
free -h
cat /proc/$(pgrep oesd)/status | grep -E "VmRSS|VmPeak|Threads"
```

---

## Crash dump analysis

### Open a dump in Visual Studio

```
1. Open Visual Studio
2. File -> Open -> File -> select the .dmp file
3. VS detects the dump type automatically
4. Click "Debug with Native Only"
5. VS shows the call stack at the moment of the crash

For correct analysis, .pdb files of the same version are needed:
  - Symbol path: Tools -> Options -> Debugging -> Symbols
  - Add the folder with .pdb files for the relevant OES version
```

### Open a dump in WinDbg

```
windbg -z "C:\path\to\crash.dmp"

Main WinDbg commands:
  .symfix                     - configure Microsoft symbols
  .sympath+ C:\OES\symbols    - add your own symbols
  .reload                     - reload symbols
  !analyze -v                 - automatic crash analysis
  k                           - current thread call stack
  ~*k                         - stacks of all threads
  .ecxr; k                    - stack at the exception
  lm                          - list of loaded modules
  !address                    - memory address info
```

### Automatic dump analysis (script)

```powershell
# scripts\analyze-crashes.ps1
param(
    [string]$DumpDir = "$env:APPDATA\OES\CrashReports",
    [string]$SymbolsDir = "C:\OES\Symbols",
    [string]$OutputDir = "C:\OES\CrashAnalysis"
)

# Requires Debugging Tools for Windows (WinDbg) installed
$cdb = "C:\Program Files (x86)\Windows Kits\10\Debuggers\x64\cdb.exe"

if (-not (Test-Path $cdb)) {
    Write-Error "WinDbg not found. Install Windows SDK."
    exit 1
}

New-Item -ItemType Directory -Force -Path $OutputDir | Out-Null

$dumps = Get-ChildItem -Path $DumpDir -Filter "*.dmp" -Recurse
Write-Host "Crash dumps found: $($dumps.Count)"

foreach ($dump in $dumps) {
    $reportFile = Join-Path $OutputDir "$($dump.BaseName)-analysis.txt"
    
    if (Test-Path $reportFile) {
        Write-Host "  Skip (already analyzed): $($dump.Name)"
        continue
    }
    
    Write-Host "  Analyzing: $($dump.Name)"
    
    $symPath = "srv*C:\symbols*https://msdl.microsoft.com/download/symbols;$SymbolsDir"
    
    $script = @"
.sympath $symPath
.reload /f
!analyze -v
.ecxr
k 30
~*k
q
"@
    
    $scriptFile = [System.IO.Path]::GetTempFileName()
    $script | Out-File $scriptFile -Encoding ASCII
    
    & $cdb -z $dump.FullName -c "`$`$<$scriptFile" 2>&1 | Out-File $reportFile -Encoding UTF8
    Remove-Item $scriptFile
    
    # Extract the summary
    $summary = Get-Content $reportFile | Select-String "EXCEPTION_CODE|FAILURE_BUCKET_ID|PROBABLE_CAUSE"
    Write-Host "    $($summary -join ' | ')"
}

Write-Host "`nAnalysis complete. Reports: $OutputDir"
```

---

## Common issues and solutions

### Application does not start (Windows)

```powershell
# 1. Check for missing DLLs
# Run from the command line (NOT double-click) to see the error:
cd "C:\Program Files\OES"
.\oes.exe

# "The program can't start because VCRUNTIME140.dll is missing"
# Fix: install Visual C++ Redistributable
# https://aka.ms/vs/17/release/vc_redist.x64.exe

# "The program can't start because fbclient.dll is missing"
# Fix: ensure Firebird is installed or fbclient.dll is in the .exe directory

# 2. Check the Event Viewer
Get-EventLog -LogName Application -EntryType Error -Newest 5 | Format-List Source, Message

# 3. Procmon (Sysinternals) - see which file is missing
# Filter: Process Name = oes.exe, Result = NAME NOT FOUND

# 4. Dependencies (tool) - .exe dependency analysis
# https://github.com/lucasg/Dependencies
```

### Firebird: connection errors

```
"Unable to complete network request to host"
  -> Firebird server is not running (server mode)
  -> Check: net start FirebirdServerDefaultInstance
  -> Or: services.msc -> Firebird Guardian

"I/O error during read/write, file: <path>"
  -> The .fdb file is corrupted or the disk is full
  -> gfix -validate: integrity check
  -> df -h (Linux) / dir (Windows): check disk space

"Lock time-out on wait transaction"
  -> The application did not commit a transaction
  -> gfix -kill: terminate stuck transactions (careful!)
  -> Or wait for the timeout

"database file appears corrupt"
  -> CRITICAL: run gfix -validate -full
  -> Restore from backup if damage is severe
  -> gbak may recover part of the data

"connection rejected by remote interface"
  -> Wrong user/password
  -> Make sure the SYSDBA password is correct
```

```bash
# Firebird diagnostics (Linux/macOS/Windows)

# DB integrity check
gfix -user SYSDBA -password masterkey -validate -full /path/to/oes.fdb

# Fix transactions
gfix -user SYSDBA -password masterkey -sweep /path/to/oes.fdb
gfix -user SYSDBA -password masterkey -mend /path/to/oes.fdb  # careful!

# DB statistics
gstat -user SYSDBA -password masterkey -header /path/to/oes.fdb

# Firebird logs
# Windows: C:\Program Files\Firebird\firebird.log
# macOS:   /usr/local/var/log/firebird.log  (Homebrew)
# Linux:   /var/log/firebird/
```

### wxWidgets: UI issues

```cpp
// === UI thread hang ===
// SYMPTOM: the UI stops responding to input

// Problem: long-running operation on the UI thread
void OnButtonClick(wxCommandEvent&) {
    // BAD: blocks the UI
    LoadHugeDocument();  // takes 5 seconds
}

// Fix 1: wxThread
void OnButtonClick(wxCommandEvent&) {
    auto thread = new LoadDocumentThread(this, m_filePath);
    thread->Run();
    // Progress via wxThreadEvent
}

// Fix 2: wxProgressDialog + wxYield
void OnButtonClick(wxCommandEvent&) {
    wxProgressDialog dlg("Loading...", "Processing file", 100, this);
    for (int i = 0; i < 100; i++) {
        ProcessChunk(i);
        dlg.Update(i);
        wxYield();  // process UI events
    }
}

// === Drawing artifacts ===
// SYMPTOM: flicker, incorrect redraw on resize

// Fix: double buffering
void OnPaint(wxPaintEvent&) {
    wxAutoBufferedPaintDC dc(this);  // instead of wxPaintDC
    // ... drawing
}
```

### Memory leaks: detection and fixing

```
Tools for memory leak detection:

1. Visual Studio Diagnostic Tools (built-in):
   Debug -> Windows -> Diagnostic Tools -> Memory Usage
   -> Take Snapshot before and after an operation
   -> Compare to find leaks

2. Application Verifier (Microsoft):
   appverif /enable Heaps /app oes.exe
   Run oes.exe - it will report leaks on close

3. Deleaker (commercial, VS integration):
   Shows the call stack at allocation time

4. AddressSanitizer (in Debug build):
   In vcxproj: <EnableASAN>true</EnableASAN>
   Reports heap overflow, use-after-free, double-free
   
5. valgrind (Linux):
   valgrind --leak-check=full --track-origins=yes ./oesd

Common leak sources in wxWidgets:
  - wxString and wxArrayString in loops (usually not a leak, wxWidgets manages them)
  - wxBitmap not released on replacement
  - Child windows created with new without a parent
  - Event handlers not unsubscribed (wxEvtHandler::Unbind)
```

---

## Remote diagnostics

### Collecting diagnostic info from a user

```powershell
# scripts\collect-diagnostics.ps1
# Run as the user with the problem

$outputDir = "$env:DESKTOP\OES-Diagnostics-$(Get-Date -Format 'yyyyMMdd-HHmmss')"
New-Item -ItemType Directory -Force -Path $outputDir | Out-Null

Write-Host "Collecting OES diagnostics..."

# OES version
$oesExe = "C:\Program Files\OES\oes.exe"
if (Test-Path $oesExe) {
    (Get-Item $oesExe).VersionInfo | Out-File "$outputDir\oes-version.txt"
}

# Application logs
Copy-Item "$env:APPDATA\OES\Logs\oes.log" "$outputDir\" -ErrorAction SilentlyContinue
Copy-Item "$env:APPDATA\OES\Logs\oes.log.1" "$outputDir\" -ErrorAction SilentlyContinue

# Crash dumps (only the latest 3)
$crashDir = "$env:APPDATA\OES\CrashReports"
if (Test-Path $crashDir) {
    Get-ChildItem $crashDir -Filter "*.dmp" -Recurse |
        Sort-Object LastWriteTime -Descending |
        Select-Object -First 3 |
        ForEach-Object { Copy-Item $_.FullName $outputDir\ }
}

# System information
systeminfo | Out-File "$outputDir\systeminfo.txt"
Get-EventLog -LogName Application -Source "OES*" -Newest 50 2>/dev/null |
    Format-List | Out-File "$outputDir\eventlog-oes.txt"

# Application Log events from the last 24h
Get-EventLog -LogName Application -EntryType Error,Warning -After (Get-Date).AddHours(-24) 2>/dev/null |
    Format-List | Out-File "$outputDir\eventlog-errors.txt"

# DLL versions
Get-ChildItem "C:\Program Files\OES\" -Filter "*.dll" |
    ForEach-Object { $_.VersionInfo } |
    Select-Object FileName, FileVersion, ProductVersion |
    Out-File "$outputDir\dll-versions.txt"

# Archive
$zipFile = "$outputDir.zip"
Compress-Archive -Path $outputDir -DestinationPath $zipFile
Remove-Item $outputDir -Recurse

Write-Host "Done. Send the file to support: $zipFile"
explorer.exe (Split-Path $zipFile)
```

### Enable verbose logging

```cpp
// In OES code: support log levels via command-line arguments
// oes.exe --log-level=debug --log-file=C:\temp\oes-verbose.log

// Or via the configuration file:
// [Logging]
// Level=debug       ; error, warning, info, debug, trace
// File=oes.log
// MaxSizeMb=50
```

---

## OES daemon does not respond

```bash
# 1. Is the daemon running?
sudo systemctl status oes-daemon
sudo ps aux | grep oesd

# 2. If not running - last logs before the crash
sudo journalctl -u oes-daemon --since "2 hours ago" --no-pager | tail -100

# 3. If hung (not responding but process exists) - get a stack trace
sudo kill -SIGUSR1 $(pgrep oesd)    # If OES implements it: dump stack
# or
sudo gdb -p $(pgrep oesd) -ex "thread apply all bt" -ex "detach" -ex "quit"

# 4. File descriptor exhaustion?
cat /proc/$(pgrep oesd)/limits | grep "open files"
ls /proc/$(pgrep oesd)/fd | wc -l

# 5. Memory exhaustion?
cat /proc/$(pgrep oesd)/status | grep VmRSS
dmesg | grep -i "oom\|out of memory" | tail -10

# 6. Force a dump for analysis
sudo kill -SIGABRT $(pgrep oesd)    # Creates a core dump (if ulimit is configured)
# or configure core dumps:
ulimit -c unlimited
echo '/var/log/oes/core.%p' | sudo tee /proc/sys/kernel/core_pattern

# 7. Restart (last resort)
sudo systemctl restart oes-daemon
```

---

## Performance diagnostics

```powershell
# Windows: OES performance

# CPU and memory of the process in real time
while ($true) {
    $p = Get-Process -Name "oes" -ErrorAction SilentlyContinue
    if ($p) {
        Write-Host "$(Get-Date -Format 'HH:mm:ss') CPU: $($p.CPU.ToString('F1'))s | RAM: $([math]::Round($p.WorkingSet64/1MB))MB | Threads: $($p.Threads.Count)"
    }
    Start-Sleep -Seconds 2
}

# Or via Perfmon (System Monitor):
# Add counters: Process\% Processor Time\oes
#               Process\Working Set\oes
#               .NET CLR Memory\# Gen 2 Collections (if managed code is present)
```

```bash
# Linux: daemon performance

# perf - CPU profiling (if available)
sudo perf top -p $(pgrep oesd)

# strace - system calls (too slow for production)
sudo strace -p $(pgrep oesd) -e trace=file,network -f 2>&1 | tail -100

# Real-time monitoring
watch -n 1 "cat /proc/$(pgrep oesd)/status | grep -E 'VmRSS|Threads|voluntary'"
```

---

## Checklist: what to check first

```
For any OES problem - check in this order:

Desktop - Windows:
  1. [ ] Fresh crash dumps? (%APPDATA%\OES\CrashReports\)
  2. [ ] Application log? (%APPDATA%\OES\Logs\oes.log)
  3. [ ] Event Viewer (Application)? Errors with Source="OES*"
  4. [ ] Disk space? (Disk Management)
  5. [ ] Antivirus not blocking? (temporarily disable to test)
  6. [ ] .fdb file accessible and not corrupted?
  7. [ ] Required DLLs present? (Dependencies tool)
  8. [ ] App version up to date? (Help -> About)

Desktop - macOS:
  1. [ ] Logs: ~/Library/Logs/OES/ or Console.app
  2. [ ] Data: ~/Library/Application Support/OES/data/*.fdb
  3. [ ] Disk space: df -h
  4. [ ] Process: pgrep -la oes
  5. [ ] Crash reports: ~/Library/Logs/DiagnosticReports/

Desktop - Linux:
  1. [ ] Logs: ~/.local/share/oes/logs/oes.log
  2. [ ] Data: ~/.local/share/oes/data/*.fdb
  3. [ ] Disk space: df -h

Daemon/Server (Linux):
  1. [ ] sudo systemctl status oes-daemon
  2. [ ] sudo journalctl -u oes-daemon --since "1h ago"
  3. [ ] Disk not full? (df -h)
  4. [ ] Memory not exhausted? (free -h)
  5. [ ] gfix -validate: DB integrity
  6. [ ] DB file permissions? (ls -la /var/lib/oes/)
  7. [ ] Firebird running? (sudo systemctl status firebird3.0-guardian)
  8. [ ] Firewall not blocking the port? (sudo ufw status)

Daemon/Server (macOS):
  1. [ ] sudo launchctl list | grep oes
  2. [ ] tail -50 /var/log/oes/daemon.log
  3. [ ] df -h
  4. [ ] Firebird: brew services list | grep firebird
```

---

## Useful OES commands

```powershell
# Windows

# Open the log folder
explorer "$env:APPDATA\OES\Logs"

# Open the crash dumps folder
explorer "$env:APPDATA\OES\CrashReports"

# Tail the log in real time
Get-Content "$env:APPDATA\OES\Logs\oes.log" -Wait -Tail 20

# User DB size
Get-ChildItem "$env:APPDATA\OES" -Filter "*.fdb" -Recurse |
    Select-Object Name, @{N='Size MB';E={[math]::Round($_.Length/1MB,1)}}

# Verify the signatures of installed files
Get-ChildItem "C:\Program Files\OES" -Filter "*.exe" |
    ForEach-Object { Get-AuthenticodeSignature $_.FullName } |
    Select-Object Path, Status, SignerCertificate

# Firebird client version
(Get-Item "C:\Program Files\OES\fbclient.dll").VersionInfo | Select-Object FileVersion
```

```bash
# Linux

# Tail daemon log
sudo journalctl -u oes-daemon -f

# DB size
du -sh /var/lib/oes/data/

# Firebird DB statistics
gstat -user SYSDBA -password "$FB_SYSDBA_PASSWORD" \
    -header /var/lib/oes/data/oes.fdb

# Active transaction count
gstat -user SYSDBA -password "$FB_SYSDBA_PASSWORD" \
    -record /var/lib/oes/data/oes.fdb | grep "transactions"

# Open Firebird connections
netstat -an | grep 3050  # Firebird default port

# Daemon thread count
cat /proc/$(pgrep oesd)/status | grep Threads
```
