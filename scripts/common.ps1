# Shared helpers for the build/test/package scripts. Dot-source it: . "$PSScriptRoot\common.ps1"

$ErrorActionPreference = 'Stop'
$RepoRoot = Split-Path -Parent $PSScriptRoot

# Visual Studio 2022+ with the C++ workload, found the same way on a dev box or a CI runner.
function Get-VsPath {
    $vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vswhere.exe'
    if (-not (Test-Path $vswhere)) { throw 'vswhere.exe not found - is Visual Studio installed?' }
    $vs = & $vswhere -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
    if (-not $vs) { throw 'Visual Studio with the C++ workload was not found.' }
    return $vs
}

function Get-MSBuild {
    $msbuild = Join-Path (Get-VsPath) 'MSBuild\Current\Bin\MSBuild.exe'
    if (-not (Test-Path $msbuild)) { throw "MSBuild not found at $msbuild" }
    return $msbuild
}

# Runs a command line inside the x64 developer environment (cl.exe on PATH).
function Invoke-DevCmd([string]$CommandLine) {
    $vcvars = Join-Path (Get-VsPath) 'VC\Auxiliary\Build\vcvars64.bat'
    # vcvars itself calls vswhere by name; put it on PATH so it doesn't print a "not recognized" warning
    $installer = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer'
    cmd /c "set `"PATH=$installer;%PATH%`" && call `"$vcvars`" >nul && $CommandLine"
    if ($LASTEXITCODE -ne 0) { throw "Command failed ($LASTEXITCODE): $CommandLine" }
}
