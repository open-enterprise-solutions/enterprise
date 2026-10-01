# Smoke test -- parallel start of two enterprise.exe instances against
# the same database file. Verifies the FB driver's multi-process
# scenario (SuperClassic embedded for local paths, leader-mode +
# follower attach for UNC paths) does not regress.
#
# What this catches:
#   - SuperClassic regression: if ServerMode flips back to default
#     Super, the second instance fails with isc_io_error (335544344)
#     and either exits immediately or shows a modal error and never
#     opens the main window.
#   - Leader-mode startup race on UNC: both instances race to grab
#     the .lease byte-lock; one becomes leader, the other follower.
#     Both must survive; the follower's TCP attach to the leader's
#     spawned firebird.exe must succeed.
#
# What this does NOT catch:
#   - Host-match preference bias (requires two physical hosts).
#   - Failover during handoff (requires killing the leader mid-run).
#   - Long-running stability -- the test runs ~10 s.
#
# Usage:
#   .\smoke_parallel_start.ps1
#   .\smoke_parallel_start.ps1 -DbPath "\\NOUVERBE\share\fb_test253\SYS.FDB"
#   .\smoke_parallel_start.ps1 -IbUser "admin" -IbPwd "1"
#
# NOTE: this file is plain ASCII on purpose. PowerShell 5.1 parses
# .ps1 files via the active code page when there is no BOM, so any
# non-ASCII char (em-dash, Cyrillic, etc.) trips "string missing
# terminator" parse errors on Western locale hosts. Keep ASCII.
#
# DebugView (Sysinternals) running in parallel will surface the
# leader-mode log lines from LogThreadSafe (lease grab/follow,
# generation bumps, etc.) for postmortem of which instance became
# leader.

param(
    [string]$ExePath  = "F:\projects\oes-work\enterprise\bin\Win32\Debug\enterprise.exe",
    [string]$DbPath   = "F:\projects\oes-bin\examples\fb_test253\SYS.FDB",
    [string]$IbUser   = "",
    [string]$IbPwd    = "",
    [int]   $WaitSec  = 10
)

if (-not (Test-Path $ExePath)) {
    Write-Host "[FAIL] enterprise.exe not found at $ExePath" -ForegroundColor Red
    exit 1
}
if (-not (Test-Path $DbPath)) {
    Write-Host "[FAIL] database not found at $DbPath" -ForegroundColor Red
    exit 1
}

# Clean slate: any leftover enterprise.exe from a prior test or a
# crashed Debug session would skew the multi-process count.
Get-Process -Name enterprise -ErrorAction SilentlyContinue | ForEach-Object {
    Write-Host "Killing leftover enterprise.exe PID $($_.Id)"
    Stop-Process -Id $_.Id -Force
}
Start-Sleep -Milliseconds 500

# Build args. Shell-level quoting matters: paths with spaces would
# break otherwise. Empty IbUser/IbPwd are skipped (lets the auth
# dialog appear, which is fine for manual inspection).
$argList = @("--file=`"$DbPath`"")
if ($IbUser) { $argList += "--ibuser=`"$IbUser`"" }
if ($IbPwd)  { $argList += "--ibpwd=`"$IbPwd`""  }

Write-Host "Starting two instances in parallel:"
Write-Host "  ExePath = $ExePath"
Write-Host "  DbPath  = $DbPath"
Write-Host "  Args    = $($argList -join ' ')"
Write-Host ""

# Spawn both as close to simultaneously as PowerShell allows. The
# 150 ms head-start bias in firebirdLeaderMode.cpp triggers on UNC
# paths when one instance is host-match and the other isn't; on a
# single dev host both are host-match, so this just exercises the
# OS-level lock race.
$p1 = Start-Process -FilePath $ExePath -ArgumentList $argList -PassThru
$p2 = Start-Process -FilePath $ExePath -ArgumentList $argList -PassThru

Write-Host "Spawned PID $($p1.Id) and PID $($p2.Id)"
Write-Host "Waiting $WaitSec s for FB attach + UI init..."
Start-Sleep -Seconds $WaitSec

# Refresh process state: Start-Process returns a snapshot, we need
# the current HasExited flag for the verdict.
$alive1 = $false; $alive2 = $false
try { $alive1 = -not (Get-Process -Id $p1.Id -ErrorAction Stop).HasExited } catch {}
try { $alive2 = -not (Get-Process -Id $p2.Id -ErrorAction Stop).HasExited } catch {}

$state1 = if ($alive1) { 'ALIVE' } else { 'EXITED' }
$state2 = if ($alive2) { 'ALIVE' } else { 'EXITED' }

Write-Host ""
Write-Host "Result:"
Write-Host "  PID $($p1.Id): $state1"
Write-Host "  PID $($p2.Id): $state2"
Write-Host ""

if ($alive1 -and $alive2) {
    Write-Host "[PASS] both instances survived parallel start" -ForegroundColor Green
    Write-Host ""
    Write-Host "Manual verification still needed:"
    Write-Host "  - both windows opened without an isc_io_error modal"
    Write-Host "  - DebugView shows one 'self-promoted' / 'leader role'"
    Write-Host "    line and one follower 'connecting to leader' line"
    Write-Host "    (UNC paths only; local paths run plain embedded)"
    exit 0
} else {
    Write-Host "[FAIL] one or both instances exited -- likely the FB" -ForegroundColor Red
    Write-Host "       multi-process regression. Check ServerMode in"
    Write-Host "       bin\Win32\Debug\_fb\firebird.conf (must be"
    Write-Host "       SuperClassic), and look for isc_io_error 335544344"
    Write-Host "       in DebugView."
    exit 1
}
