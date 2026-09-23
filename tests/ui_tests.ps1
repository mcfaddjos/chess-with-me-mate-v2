<#
.SYNOPSIS
    Plays scripted games through the real window and checks the move log.
    Each case launches the game with -d (click/move log on stderr), clicks squares, sends keys,
    then checks the expected log lines appeared in order. Needs a desktop session; don't touch
    the mouse while it runs. Run via .\scripts\test.ps1 -UI

    Output, for reviewing afterwards (tests\build\ui\):
      <case>.log   the game's click/move log
      <case>.png   screenshot of the game window at the end of the case
      summary.txt  pass/fail for every case
#>
param([Parameter(Mandatory)][string]$Exe, [string]$OutDir = (Join-Path $PSScriptRoot 'build\ui'))

$ErrorActionPreference = 'Stop'
$RepoRoot = Split-Path -Parent $PSScriptRoot
New-Item -ItemType Directory -Force $OutDir | Out-Null

Add-Type -AssemblyName System.Windows.Forms
Add-Type -AssemblyName System.Drawing
Add-Type @"
using System;
using System.Runtime.InteropServices;
public class UiTestWin32 {
  [DllImport("user32.dll")] public static extern bool SetForegroundWindow(IntPtr h);
  [DllImport("user32.dll")] public static extern bool ClientToScreen(IntPtr h, ref POINT p);
  [DllImport("user32.dll")] public static extern bool SetCursorPos(int x, int y);
  [DllImport("user32.dll")] public static extern void mouse_event(int f, int x, int y, int d, int e);
  public struct POINT { public int X, Y; }
}
"@

# Where a square's center lands on screen for the default camera -- mirrors UpdateMatrices()
# in sample.cpp (fov 20, distance 620, pitch 68, looking at the origin, 700x700 window).
function Get-SquarePixel([string]$sq) {
    $f = [int][char]$sq[0] - 97; $r = [int]"$($sq[1])" - 1
    $x = ($f - 3.5) * 20; $z = (3.5 - $r) * 20
    $pitch = 68 * [math]::PI / 180; $dist = 620
    $eyeY = $dist * [math]::Sin($pitch); $eyeZ = $dist * [math]::Cos($pitch)
    # camera basis for a camera at (0, eyeY, eyeZ) looking at the origin:
    $len = [math]::Sqrt($eyeY * $eyeY + $eyeZ * $eyeZ)
    $fy = -$eyeY / $len; $fz = -$eyeZ / $len          # forward
    $uy = -$fz; $uz = $fy                              # camera up (right is +x)
    $dy = -$eyeY; $dz = $z - $eyeZ
    $cx = $x; $cy = $dy * $uy + $dz * $uz; $cz = $dy * $fy + $dz * $fz
    $t = [math]::Tan(10 * [math]::PI / 180)
    $px = [int][math]::Round(($cx / ($cz * $t) + 1) / 2 * 700)
    $py = [int][math]::Round((1 - $cy / ($cz * $t)) / 2 * 700)
    return @($px, $py)
}

# Screenshot of just the game window's drawing area (700x700), for reviewing a run afterwards.
function Save-WindowShot([IntPtr]$h, [string]$path) {
    $pt = New-Object UiTestWin32+POINT
    [UiTestWin32]::ClientToScreen($h, [ref]$pt) | Out-Null
    $bmp = New-Object System.Drawing.Bitmap 700, 700
    $g = [System.Drawing.Graphics]::FromImage($bmp)
    $g.CopyFromScreen($pt.X, $pt.Y, 0, 0, $bmp.Size)
    $bmp.Save($path)
    $g.Dispose(); $bmp.Dispose()
}

function Invoke-Case([string]$Name, [string]$GameArgs, [string]$Squares, [string]$Keys, [string[]]$Expect, [string[]]$Forbid = @()) {
    $log = Join-Path $OutDir "$Name.log"
    $p = Start-Process -FilePath $Exe -ArgumentList "-d $GameArgs" -WorkingDirectory $RepoRoot -PassThru -RedirectStandardError $log
    try {
        $deadline = (Get-Date).AddSeconds(10)
        while ($p.MainWindowHandle -eq 0 -and (Get-Date) -lt $deadline) { Start-Sleep -Milliseconds 200; $p.Refresh() }
        if ($p.MainWindowHandle -eq 0) { throw "window never appeared" }
        Start-Sleep -Milliseconds 800
        $h = $p.MainWindowHandle
        [UiTestWin32]::SetForegroundWindow($h) | Out-Null
        foreach ($sq in ($Squares -split ' ' | Where-Object { $_ })) {
            $xy = Get-SquarePixel $sq
            $pt = New-Object UiTestWin32+POINT; $pt.X = $xy[0]; $pt.Y = $xy[1]
            [UiTestWin32]::ClientToScreen($h, [ref]$pt) | Out-Null
            [UiTestWin32]::SetCursorPos($pt.X, $pt.Y) | Out-Null
            Start-Sleep -Milliseconds 100
            [UiTestWin32]::mouse_event(2, 0, 0, 0, 0); Start-Sleep -Milliseconds 40; [UiTestWin32]::mouse_event(4, 0, 0, 0, 0)
            Start-Sleep -Milliseconds 700      # let the move animation finish
        }
        if ($Keys) { [System.Windows.Forms.SendKeys]::SendWait($Keys); Start-Sleep -Milliseconds 700 }
        if ($p.HasExited) { throw "game exited (code $($p.ExitCode))" }
        Start-Sleep -Milliseconds 1200     # let capture animations finish before the screenshot
        Save-WindowShot $h (Join-Path $OutDir "$Name.png")
    }
    finally {
        if (-not $p.HasExited) { Stop-Process -Id $p.Id -Force }
        $p.WaitForExit()
    }

    $lines = @(Get-Content $log | Where-Object { $_ -notmatch '^Obj file' })
    $at = 0
    foreach ($e in $Expect) {
        $found = $false
        for (; $at -lt $lines.Count; $at++) { if ($lines[$at] -eq $e) { $found = $true; $at++; break } }
        if (-not $found) { throw "expected log line '$e' (in order) - see $log" }
    }
    foreach ($f in $Forbid) { if ($lines -contains $f) { throw "unexpected log line '$f' - see $log" } }
}

$cases = @(
    @{ Name = 'scholars-mate'; Squares = 'e2 e4 e7 e5 f1 c4 b8 c6 d1 h5 g8 f6 h5 f7 a7 a6'
       Expect = 'e2-e4', 'e7-e5', 'Bf1-c4', 'Nb8-c6', 'Qd1-h5', 'Ng8-f6', 'Qh5xf7#'; Forbid = 'a7-a6' },
    @{ Name = 'illegal-move'; Squares = 'd2 d5 d2 d4'
       Expect = 'not legal: d2-d5', 'd2-d4' },
    @{ Name = 'castle-by-rook-click'; GameArgs = '-fen "r3k2r/pppppppp/8/8/8/8/PPPPPPPP/R3K2R w KQkq - 0 1"'; Squares = 'e1 h1 e8 a8'
       Expect = 'O-O', 'O-O-O' },
    @{ Name = 'en-passant'; Squares = 'e2 e4 a7 a6 e4 e5 d7 d5 e5 d6'
       Expect = 'e4-e5', 'd7-d5', 'e5xd6' },
    @{ Name = 'promotion-auto-queen'; GameArgs = '-fen "8/P6k/8/8/8/8/8/K7 w - - 0 1"'; Squares = 'a7 a8'
       Expect = , 'a7-a8=Q' },
    @{ Name = 'stalemate'; GameArgs = '-fen "7k/8/6Q1/8/8/8/8/K7 w - - 0 1"'; Squares = 'g6 f7 h8 g8'
       Expect = , 'Qg6-f7'; Forbid = 'Kh8-g8' },
    @{ Name = 'undo'; Squares = 'e2 e4 e7 e5'; Keys = 'u'
       Expect = 'e7-e5', 'undo e7-e5' },
    @{ Name = 'no-undo-when-timed'; GameArgs = '-tc 3'; Squares = 'e2 e4'; Keys = 'u'
       Expect = , 'e2-e4'; Forbid = 'undo e2-e4' }
)

$failed = 0
$summary = @("UI test run $(Get-Date -Format s)  exe: $Exe")
Write-Host "== UI tests ($Exe)"
foreach ($c in $cases) {
    try {
        Invoke-Case -Name $c.Name -GameArgs $c.GameArgs -Squares $c.Squares -Keys $c.Keys -Expect $c.Expect -Forbid $(if ($c.Forbid) { $c.Forbid } else { @() })
        $line = "ok   {0}" -f $c.Name
    }
    catch {
        $failed++
        $line = "FAIL {0}: {1}" -f $c.Name, $_.Exception.Message
    }
    Write-Host $line
    $summary += $line
}
$summary += $(if ($failed -gt 0) { "$failed UI test(s) failed" } else { 'all UI tests passed' })
$summary | Set-Content -Encoding utf8 (Join-Path $OutDir 'summary.txt')
Write-Host "Logs, screenshots, and summary.txt are in $OutDir"
if ($failed -gt 0) { exit 1 }
exit 0
