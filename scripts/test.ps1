<#
.SYNOPSIS
    Runs the tests.
    Always: the rules tests (perft move counts + checkmate/stalemate/draw detection).
    With -UI: also launches the game and clicks through scripted games, checking the move log.
    The UI tests need a real desktop session, so they are for local runs, not CI.
.EXAMPLE
    .\scripts\test.ps1
    .\scripts\test.ps1 -UI
#>
param([switch]$UI, [ValidateSet('Release', 'Debug')][string]$Configuration = 'Release',
      [string]$Only = '')    # with -UI: only the UI cases matching this wildcard, e.g. -Only 'review*'

. "$PSScriptRoot\common.ps1"

$buildDir = Join-Path $RepoRoot 'tests\build'
New-Item -ItemType Directory -Force $buildDir | Out-Null

Write-Host '== rules tests'
Push-Location (Join-Path $RepoRoot 'tests')
try {
    Invoke-DevCmd "cl /nologo /O2 /EHsc /std:c++17 /W4 /WX perft.cpp /Fe:build\perft.exe /Fo:build\ >nul"
    & (Join-Path $buildDir 'perft.exe')
    if ($LASTEXITCODE -ne 0) { throw 'Rules tests failed' }

    Write-Host '== engine tests'
    Invoke-DevCmd "cl /nologo /O2 /EHsc /std:c++17 /W4 /WX engine_tests.cpp /Fe:build\engine_tests.exe /Fo:build\ >nul"
    & (Join-Path $buildDir 'engine_tests.exe')
    if ($LASTEXITCODE -ne 0) { throw 'Engine tests failed' }
}
finally { Pop-Location }

if ($UI) {
    & (Join-Path $RepoRoot 'tests\ui_tests.ps1') -Exe (Join-Path $RepoRoot "$Configuration\Sample.exe") -Only $Only
    if ($LASTEXITCODE -ne 0) { throw 'UI tests failed' }
}

Write-Host 'All tests passed.'
