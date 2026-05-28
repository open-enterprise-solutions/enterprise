# 18. Incident Management

## Definition of an incident

An incident is a situation where OES does not start, crashes, loses data, or critically misbehaves on user machines. Planned updates, bugs in test environments, and cosmetic issues without data impact are not incidents.

---

## Incident severity

| Severity | Description | Examples | Response time |
|---------|----------|---------|---------------|
| **P1 Critical** | App won't start or data is lost | Crash at startup, DB loss/corruption, cannot save document | 30 minutes |
| **P2 High** | A key module is broken, workaround exists | Designer won't open, reports won't generate, DB server connection fails | 2 hours |
| **P3 Medium** | Bug hurts usability but isn't blocking | Slow data loading, wrong output in one report, calculation error | 1 working day |
| **P4 Low** | Cosmetic bug, minimal impact | Menu typo, misalignment, wrong icon | Next sprint |

---

## Response process

### 1. Detection

An incident can be detected by:
- A user (support contact, message in the team chat)
- A developer during testing or review
- Automatically — through the Windows Event Log or a crash dump

### 2. Team notification

```
Format of a message in Telegram / team chat:

INCIDENT P1: OES crashes when opening a document
What: Access violation when opening a document with sections > 100
Version: OES 2.4.3
OS: Windows 10 Pro 22H2
When: detected at 10:15
Who's on it: @developer_name
Dump: attached / path to .dmp file
```

### 3. Escalation

```
0-30 min   → Developer diagnoses on their own
30-60 min  → Escalate to tech lead, bring in a second developer
60+ min    → Escalate to management (for P1)
Data loss  → Immediate escalation to management + halt file operations
```

### 4. Diagnostics

Order of checks for a desktop application:

```
1. Application version and OS
   — Help → About menu
   — winver, Windows 10/11?

2. Crash dump
   — %APPDATA%\OES\logs\crash_YYYYMMDD_HHMMSS.dmp
   — Open in WinDbg / Visual Studio: Debug → Open Dump File

3. Application log file
   — %APPDATA%\OES\logs\oes.log
   — Find the last lines before the incident

4. Database state
   — Firebird: gfix -validate -full -user SYSDBA -password masterkey path_to_file.fdb
   — Check DB file integrity
   — Inspect recent operations in the log

5. System state
   — Free disk space
   — Antivirus blocked a file?
   — File system permissions on %APPDATA%\OES

6. Reproduction
   — Reproduce on a clean machine or VM
   — Minimal reproduction steps
```

### 5. Collecting diagnostics from the user

```
Ask the user to provide:
1. File: %APPDATA%\OES\logs\oes.log
2. File: %APPDATA%\OES\logs\crash_*.dmp (if any)
3. App version (Help → About)
4. OS version and Visual C++ Redistributable version
5. Exact reproduction steps
6. Screenshot or video of the error
```

### 6. Rollback to the previous version

If a fast fix is impossible — deploy the previous installer:

```
1. Stop the user from working with the broken version
2. Back up the user's files:
   — %APPDATA%\OES\config\
   — DB file (path from configuration)
3. Uninstall the current version through "Programs and Features"
4. Install the previous version from the installer archive
5. Verify the previous version works
6. Notify the user of recovery
```

### 7. Recovery notification

```
RESOLVED: OES crashes when opening a document
Cause: buffer overflow on sections > 100 rows
Fix: rollback to version 2.4.2
Downtime: 10:15 - 11:40
Permanent fix: in version 2.4.4, ETA: 2 days
```

---

## Crash dump analysis

### WinDbg — quick analysis

```
1. Open WinDbg (x64)
2. File → Open Crash Dump → choose the .dmp file
3. Load symbols (if .pdb is available):
   .sympath+ C:\OES\symbols\
   .reload

4. Call stack at the moment of the crash:
   !analyze -v
   k         — current thread stack
   ~*k       — all threads stacks

5. Identify the cause:
   — "Access violation" → null pointer or out-of-bounds access
   — "Stack overflow"   → infinite recursion
   — "Heap corruption"  → double-free or buffer overflow
```

### Visual Studio — opening a dump

```
File → Open → File → choose the .dmp
Debug → Start Debugging (F5) — starts at the crash point
Tabs: Call Stack, Locals, Autos — inspect state
```

### Enabling symbols in release builds

```
In the MSBuild project (.vcxproj):
Configuration Properties → Linker → Debugging:
  Generate Debug Info: Yes (/DEBUG)
  Generate Program Database File: $(OutDir)$(TargetName).pdb

On release: keep .pdb files in a secure location next to the installer
Symbol server: a local Symbol Server can be configured
```

---

## Postmortems

After every P1 and P2 incident — write a postmortem within 48 hours. Keep them in `docs/postmortems/`.

### Postmortem template

```markdown
# Postmortem: [Brief incident description]

**Date:** 2026-04-10
**Severity:** P1
**Duration:** 85 minutes (10:15 - 11:40)
**OES version:** 2.4.3
**Author:** Name Surname

## What happened

OES crashed with Access Violation when opening documents
that contained more than 100 rows in sections. Three users were
unable to continue working.

## Timeline

- 10:10 — Released 2.4.3 with the new section renderer
- 10:15 — User reported an error opening a document
- 10:18 — Received log and dump
- 10:35 — Reproduced locally on a document with 120 rows
- 10:50 — Root cause located: array out-of-bounds in SectionRenderer
- 11:00 — Decided to roll back to 2.4.2
- 11:20 — Installed version 2.4.2 for the user
- 11:40 — Confirmed working, incident closed

## Root Cause

In `SectionRenderer::RenderRows()` the buffer size was allocated as
`new wxString[100]` (a hardcoded limit). Processing a document with
120+ rows caused a buffer overrun → Access Violation.

How it slipped in: a 2.4.3 refactor for optimization, but the hard limit
was not removed from the old code.

## Prevent recurrence

1. [ ] Replace the static buffer with std::vector<wxString> — @developer — by 12.04
2. [ ] Add an integration test with a document > 100 rows — @developer — by 13.04
3. [ ] Enable Address Sanitizer in the Debug CI build — @developer — by 14.04
4. [ ] Stress-test before release (documents > 500 rows) — by 16.04

## Lessons

- Magic numbers in buffer sizes are an anti-pattern, use std::vector
- Integration tests are needed for boundary values (1, 100, 1000 rows)
- .pdb files for release builds cut diagnosis time from hours to minutes
```

### Postmortem rules

- No blame — look for systemic causes, not culprits
- Action items with specific owners and deadlines
- Focus on prevention — what to change in processes and code
- Store in `docs/postmortems/YYYYMMDD_description.md`

---

## Runbooks

For common incidents — a "what to do" document. Keep them in `docs/runbooks/`.

### Runbook: OES won't start

```markdown
# OES won't start

## Symptoms
- Window doesn't appear
- Icon flashes in the taskbar and vanishes
- User reports an error on startup

## Diagnostics

1. Check the startup log:
   %APPDATA%\OES\logs\oes.log
   → Find the last lines, the error message

2. Check the dump:
   %APPDATA%\OES\logs\crash_*.dmp
   → Open in WinDbg: !analyze -v

3. Check the VC Redistributable:
   Control Panel → Programs → "Microsoft Visual C++ Redistributable"
   → Need x64 2017 or newer
   → If missing: install from the OES\redist\ folder of the distribution

4. Check DB integrity (if the log points to a DB error):
   Firebird:
     gfix -validate -full -user SYSDBA -password masterkey path_to_file.fdb
   SQLite:
     sqlite3 path_to_file.db "PRAGMA integrity_check;"

5. Reset settings (when nothing else helps):
   Rename %APPDATA%\OES\config\ to config.bak
   Launch OES — it will create the default configuration

## Escalation
If steps 1-5 did not help → forward the dump + log to a developer
```

### Runbook: Firebird database is corrupt

```markdown
# Firebird database is corrupt

## Symptoms
- "database file appears corrupt" error in the log
- Documents won't open
- OES hangs when connecting to the DB

## Steps

1. Stop OES for every user (important!)
2. Make a copy of the .fdb file: xcopy original.fdb original.fdb.bak
3. Validate and repair:
   gfix -validate -full -mend path\to\file.fdb -user SYSDBA -password masterkey
4. If gfix didn't help — restore from backup:
   gbak -c path\to\backup.fbk path\to\restored.fdb -user SYSDBA -password masterkey
5. Verify integrity of the restored DB:
   gbak -b -v path\to\restored.fdb path\to\verify.fbk -user SYSDBA -password masterkey

## Contacts
Developer: @developer (Telegram)
```

---

## Monitoring (for system administrators)

### What to monitor on OES workstations

| Event | Tool | Action |
|---------|-----------|----------|
| Crash dump appeared | Windows Task Scheduler / script | Notify the developer |
| Errors in the OES log | Scheduled task | Review daily |
| DB file size grows abnormally | Disk monitoring | Check for duplication |
| DB file not updated > 24h | Date-check script | Verify OES is being launched |

### Dump monitoring (Windows Event Log + script)

```powershell
# check_crashes.ps1 — run on a schedule
$dumpDir = "$env:APPDATA\OES\logs"
$dumps = Get-ChildItem $dumpDir -Filter "crash_*.dmp" |
         Where-Object { $_.CreationTime -gt (Get-Date).AddDays(-1) }

if ($dumps.Count -gt 0) {
    $msg = "OES crash dump detected on $env:COMPUTERNAME:`n"
    $msg += ($dumps | Select-Object -ExpandProperty Name) -join "`n"
    # Send a notification (email, Teams, Telegram)
    Write-EventLog -LogName Application -Source "OES Monitor" `
        -EventId 1001 -EntryType Warning -Message $msg
}
```

---

## Incident response checklist

### When receiving an error report

- [ ] Determine the severity (P1-P4) from the description
- [ ] Assign someone to diagnose
- [ ] Ask the user for: log + dump + reproduction steps + version
- [ ] Try to reproduce locally
- [ ] Keep the team posted on diagnostics

### When a quick fix is impossible

- [ ] Decide to roll back the version
- [ ] Help the user install the previous version
- [ ] Make sure user data is intact
- [ ] Update the team on status

### After incident closure

- [ ] Write a postmortem (for P1 and P2)
- [ ] Open a fix task in the tracker
- [ ] Add a test that reproduces the bug
- [ ] Update the runbook if diagnostics revealed new patterns
