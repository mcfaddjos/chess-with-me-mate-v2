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
Write-Host "Building $Configuration..."
& $msbuild (Join-Path $RepoRoot 'Sample.sln') "-p:Configuration=$Configuration" '-p:Platform=Win32' '-v:minimal' '-nologo'
if ($LASTEXITCODE -ne 0) { throw "Build failed ($Configuration)" }

$exe = Join-Path $RepoRoot "$Configuration\Sample.exe"
Write-Host "Built $exe"
