<#
.SYNOPSIS
    Builds Release, runs the rules tests, and zips a playable copy into dist\.
.EXAMPLE
    .\scripts\package.ps1 -Version 2.0.0
    .\scripts\package.ps1 -Version 2.0.0 -UI     # also run the UI tests first
#>
param(
    [Parameter(Mandatory)][ValidatePattern('^\d+\.\d+\.\d+(-[0-9A-Za-z.]+)?$')][string]$Version,
    [switch]$UI
)

. "$PSScriptRoot\common.ps1"

& "$PSScriptRoot\build.ps1" -Configuration Release
& "$PSScriptRoot\test.ps1" -Configuration Release -UI:$UI

$name = "ChessWithMeMate-$Version-win32"
$dist = Join-Path $RepoRoot 'dist'
$stage = Join-Path $dist $name
if (Test-Path $stage) { Remove-Item -Recurse -Force $stage }
New-Item -ItemType Directory -Force (Join-Path $stage 'pieces') | Out-Null

# the game looks for pieces\ next to where it runs, and the DLLs next to the exe:
Copy-Item (Join-Path $RepoRoot 'Release\Sample.exe') (Join-Path $stage 'ChessWithMeMate.exe')
Copy-Item (Join-Path $RepoRoot 'freeglut.dll'), (Join-Path $RepoRoot 'glew32.dll'), (Join-Path $RepoRoot 'README.md') $stage
foreach ($p in 'Pawn', 'Knight', 'Bishop', 'Rook', 'Queen', 'King') {
    Copy-Item (Join-Path $RepoRoot "pieces\$p.obj") (Join-Path $stage 'pieces')
}

$zip = Join-Path $dist "$name.zip"
if (Test-Path $zip) { Remove-Item -Force $zip }
Compress-Archive -Path $stage -DestinationPath $zip
Write-Host "Packaged $zip"
