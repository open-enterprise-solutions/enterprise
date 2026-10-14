<#
.SYNOPSIS
  What the thin designer has not taken over from the desktop — files, and in the classes of the same name, the
  properties, categories, events and methods.

.DESCRIPTION
  The thin designer is the desktop's code moved over (frontend/X -> frmclient/X, designer/X -> designer-thin/X) with
  the backend behind stubs. What is moved by hand is moved in parts, and a part left behind is not seen by anyone
  until a person misses it on the screen. This check lists it instead:

    FILE    a desktop file with no file of the same path on the thin side
    CLASS   a desktop class (declared in a header) the thin side declares nowhere
    MEMBER  a member of a class both sides declare — m_property*, m_category*, m_event*, a method — only the desktop has

  A difference that stays on purpose is written in thin-parity.accepted.txt, one per line, with the reason after '#':
      MEMBER ibValueTextCtrl.m_propertySource   # the source is the server's; the inspector reads it from the node
  Anything else is printed and the exit code is 1.

.EXAMPLE
  powershell -File tools/thin-parity/thin-parity.ps1                  # everything
  powershell -File tools/thin-parity/thin-parity.ps1 -Kind MEMBER     # only the members
  powershell -File tools/thin-parity/thin-parity.ps1 -Filter widgets  # only what mentions 'widgets'
#>
param(
    [ValidateSet('ALL', 'FILE', 'CLASS', 'MEMBER')]
    [string]$Kind = 'ALL',
    [string]$Filter = ''
)

$ErrorActionPreference = 'Stop'
$engine = Join-Path $PSScriptRoot '..\..\src\engine' | Resolve-Path
$acceptedPath = Join-Path $PSScriptRoot 'thin-parity.accepted.txt'

# The desktop's trees and where each is moved to.
$pairs = @(
    @{ Desktop = 'frontend'; Thin = 'frmclient' },
    @{ Desktop = 'designer'; Thin = 'designer-thin' }
)

function Get-Sources([string]$root) {
    $full = Join-Path $engine $root
    Get-ChildItem -Path $full -Recurse -File -Include *.h, *.cpp |
        ForEach-Object { $_.FullName.Substring($full.Length + 1).Replace('\', '/') }
}

# The classes a header declares: name -> set of members. A line scan with a stack for nested classes — enough for the
# code style here (one declaration a line, braces on the class line or the next).
function Read-Classes([string[]]$files, [hashtable]$into) {
    $classStart = '^\s*(?:(?:public|protected|private)\s*:\s*)?(?:template\s*<[^>]*>\s*)?(?:class|struct)\s+(?:\w+_API\s+)?(\w+)\s*(?:final\s*)?(?::[^;{]*)?(\{.*)?$'
    $method     = '^\s*(?:virtual\s+|static\s+|inline\s+|explicit\s+|constexpr\s+)*(?:[\w:<>,\*&\s~]+?\s+[\*&]*)?(~?\w+)\s*\([^;{]*\)?'
    $field      = '\b(m_(?:property|category|event)\w*)\b\s*(?:=|;|\{)'
    foreach ($file in $files) {
        $stack = New-Object System.Collections.Stack
        $depth = 0
        $pending = $null
        foreach ($raw in [System.IO.File]::ReadAllLines($file)) {
            $line = $raw -replace '//.*$', '' -replace '"(?:\\.|[^"\\])*"', '""'
            if ($line -match $classStart) {
                $pending = $Matches[1]
            }
            foreach ($ch in $line.ToCharArray()) {
                if ($ch -eq '{') {
                    $depth++
                    if ($pending) {
                        $stack.Push(@{ Name = $pending; Depth = $depth })
                        if (-not $into.ContainsKey($pending)) { $into[$pending] = @{ File = $file; Members = @{} } }
                        $pending = $null
                    }
                }
                elseif ($ch -eq '}') {
                    if ($stack.Count -gt 0 -and $stack.Peek().Depth -eq $depth) { [void]$stack.Pop() }
                    $depth--
                }
                elseif ($ch -eq ';' -and $pending) {
                    $pending = $null   # a forward declaration
                }
            }
            if ($stack.Count -eq 0 -or $stack.Peek().Depth -ne $depth) { continue }   # only the class's own level
            $members = $into[$stack.Peek().Name].Members
            foreach ($m in [regex]::Matches($line, $field)) { $members[$m.Groups[1].Value] = $true }
            if ($line -match $method -and $line -notmatch '^\s*(?:return|if|for|while|switch|case|using|typedef|friend)\b') {
                $name = $Matches[1]
                if ($name -notmatch '^(?:wxDECLARE\w*|DECLARE\w*|BACKEND_STUB|operator|if|for|while|switch|sizeof|_)$' -and
                    $name -cnotmatch '^[A-Z_0-9]+$') {
                    $members["$name()"] = $true
                }
            }
        }
    }
}

$accepted = New-Object System.Collections.Generic.List[string]   # -like patterns: 'FILE frontend/uikit/*'
if (Test-Path $acceptedPath) {
    foreach ($l in Get-Content $acceptedPath) {
        $entry = ($l -replace '#.*$', '' -replace '\s+', ' ').Trim()
        if ($entry) { $accepted.Add($entry) }
    }
}

$report = New-Object System.Collections.Generic.List[string]
function Add-Finding([string]$kind, [string]$what) {
    $entry = ("$kind $what" -replace '\s+', ' ')
    foreach ($pattern in $accepted) { if ($entry -like $pattern) { return } }
    if ($Kind -ne 'ALL' -and $Kind -ne $kind) { return }
    if ($Filter -and $entry -notmatch [regex]::Escape($Filter)) { return }
    $report.Add($entry)
}

# FILES — the same path on the thin side.
foreach ($pair in $pairs) {
    $thin = @{}
    foreach ($f in Get-Sources $pair.Thin) { $thin[$f] = $true }
    foreach ($f in Get-Sources $pair.Desktop) {
        if (-not $thin.ContainsKey($f)) { Add-Finding 'FILE' "$($pair.Desktop)/$f" }
    }
}

# CLASSES and their MEMBERS — the thin side is all its trees together: a desktop class may land in another file.
$desktopClasses = @{}
$thinClasses = @{}
foreach ($pair in $pairs) {
    Read-Classes (Get-ChildItem (Join-Path $engine $pair.Desktop) -Recurse -File -Include *.h | ForEach-Object FullName) $desktopClasses
    Read-Classes (Get-ChildItem (Join-Path $engine $pair.Thin) -Recurse -File -Include *.h | ForEach-Object FullName) $thinClasses
}
foreach ($name in ($desktopClasses.Keys | Sort-Object)) {
    $rel = $desktopClasses[$name].File.Substring($engine.Path.Length + 1).Replace('\', '/')
    if (-not $thinClasses.ContainsKey($name)) {
        Add-Finding 'CLASS' "$name   ($rel)"
        continue
    }
    $thinMembers = $thinClasses[$name].Members
    foreach ($m in ($desktopClasses[$name].Members.Keys | Sort-Object)) {
        if (-not $thinMembers.ContainsKey($m)) { Add-Finding 'MEMBER' "$name.$m" }
    }
}

$report | ForEach-Object { $_ }
$counts = $report | Group-Object { $_.Split(' ')[0] } | ForEach-Object { "$($_.Name)=$($_.Count)" }
Write-Host ("thin-parity: " + ($(if ($report.Count) { $counts -join ' ' } else { 'nothing left behind' })))
if ($report.Count) { exit 1 }
