<#
.SYNOPSIS
    Builds the game with MSBuild.
.EXAMPLE
    .\scripts\build.ps1                 # Release
    .\scripts\build.ps1 -Configuration Debug
#>
param([ValidateSet('Release', 'Debug')][string]$Configuration = 'Release')

. "$PSScriptRoot\common.ps1"

$msbuild = Get-MSBuild

# a running copy of the game locks its exe and the link step would fail; say so plainly
$exe = Join-Path $RepoRoot "$Configuration\Sample.exe"
if (Test-Path $exe) {
    try { [IO.File]::Open($exe, 'Open', 'ReadWrite', 'None').Close() }
    catch { throw "The game is still running from $Configuration\ - close it (or build the other configuration) and try again." }
}

Write-Host "Building $Configuration..."
& $msbuild (Join-Path $RepoRoot 'Sample.sln') "-p:Configuration=$Configuration" '-p:Platform=Win32' '-v:minimal' '-nologo'
if ($LASTEXITCODE -ne 0) { throw "Build failed ($Configuration)" }

Write-Host "Built $exe"
