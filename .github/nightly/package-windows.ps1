# Pack a CMake Release tree for Windows x64 into the nightly zip.
#
# What goes in: everything the build put beside the executables (the programs, backend / frontend, the
# wxWidgets DLLs, help/, plugins/, and the database clients the backend's post-build step copies: _fb/ and
# libpq with OpenSSL), plus what no build step places — the interface translations (lang/) and the MSVC
# runtime, which Common.props copies for a Visual Studio Release build and nothing does for CMake. The
# UCRT is part of Windows 10/11 and is not shipped, as there.
#
# It REFUSES rather than packs a tree that would not start: a missing piece fails the job, and the release
# is published only when every platform packed.
param(
    [Parameter(Mandatory = $true)][string]$Bin,
    [Parameter(Mandatory = $true)][string]$Out
)
$ErrorActionPreference = 'Stop'

$release = Join-Path $Bin 'Release'
$stage   = Join-Path $PWD 'oes'
if (Test-Path $stage) { Remove-Item -Recurse -Force $stage }
New-Item -ItemType Directory $stage | Out-Null

# The tree as built, without what only a developer uses.
Copy-Item -Recurse -Path (Join-Path $release '*') -Destination $stage
Get-ChildItem -Recurse -File $stage -Include *.pdb, *.ilk, *.exp, *.lib | Remove-Item -Force

# The translations — read from <exeDir>/lang/<code>/open_es.mo.
Copy-Item -Recurse -Path 'lang' -Destination (Join-Path $stage 'lang')

# The MSVC runtime, from the toolset that built it (vcvars sets VCToolsRedistDir).
$crt = Get-ChildItem -Directory (Join-Path $env:VCToolsRedistDir 'x64') -Filter 'Microsoft.VC*.CRT' | Select-Object -First 1
if ($null -eq $crt) { throw "No MSVC runtime under $($env:VCToolsRedistDir)x64" }
foreach ($dll in 'msvcp140.dll', 'vcruntime140.dll', 'vcruntime140_1.dll') {
    Copy-Item (Join-Path $crt.FullName $dll) $stage
}

$required = @(
    'enterprise.exe', 'designer.exe', 'launcher.exe', 'codeRunner.exe', 'daemon.exe',
    'backend.dll', 'frontend.dll', 'backend.conf',
    '_fb\fbclient.dll', '_fb\firebird.msg', '_fb\firebird.conf', '_fb\plugins\engine13.dll', '_fb\intl\fbintl.dll',
    'libpq.dll', 'libssl-3-x64.dll', 'libcrypto-3-x64.dll',
    'help\en.hlk', 'lang\ru\open_es.mo', 'plugins\simplePlugin.dll',
    'msvcp140.dll', 'vcruntime140.dll'
)
$missing = $required | Where-Object { -not (Test-Path (Join-Path $stage $_)) }
if ($missing) { throw "The package would not run - missing: $($missing -join ', ')" }
if (-not (Get-ChildItem $stage -Filter 'wxbase*.dll')) { throw 'The package would not run - no wxWidgets DLLs' }

if (Test-Path $Out) { Remove-Item -Force $Out }
Compress-Archive -Path $stage -DestinationPath $Out
Get-Item $Out | Select-Object Name, Length | Format-Table | Out-String | Write-Host
