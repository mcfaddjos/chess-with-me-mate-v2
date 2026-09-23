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

    Case fields (all optional except Name):
      GameArgs    extra command line (default two-player; computer cases pass -bot)
      Cam         "yaw,pitch" to start the camera there (clicks are aimed to match)
      KeysBefore  keys sent before any clicks
      Squares     squares to click in order; "wait:N" pauses N seconds (e.g. for the bot)
      Keys        keys sent after the clicks
      Wait        seconds to wait at the end, before checking
      Expect      log lines that must appear, in order; a leading '~' makes it a regex
      Forbid      log lines that must not appear
    Every case gets its own throwaway rating profile, so your real one is never touched.
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

# Where a square's center lands on screen -- mirrors UpdateMatrices() in sample.cpp:
# fov 20, distance 620, camera orbiting the origin at (yaw, pitch), 700x700 window.
function Get-SquarePixel([string]$sq, [double]$yawDeg = 0, [double]$pitchDeg = 68) {
    $f = [int][char]$sq[0] - 97; $r = [int]"$($sq[1])" - 1
    $px = ($f - 3.5) * 20; $pz = (3.5 - $r) * 20
    $dist = 620; $p = $pitchDeg * [math]::PI / 180; $w = $yawDeg * [math]::PI / 180
    $ex = $dist * [math]::Cos($p) * [math]::Sin($w); $ey = $dist * [math]::Sin($p); $ez = $dist * [math]::Cos($p) * [math]::Cos($w)
    $len = [math]::Sqrt($ex * $ex + $ey * $ey + $ez * $ez)
    $fx = -$ex / $len; $fy = -$ey / $len; $fz = -$ez / $len          # forward
    $rx = -$fz; $rz = $fx                                              # right = forward x up
    $rl = [math]::Sqrt($rx * $rx + $rz * $rz); $rx /= $rl; $rz /= $rl
    $ux = -$rz * $fy; $uy = $rz * $fx - $rx * $fz; $uz = $rx * $fy     # camera up = right x forward
    $dx = $px - $ex; $dy = -$ey; $dz = $pz - $ez
    $cx = $dx * $rx + $dz * $rz
    $cy = $dx * $ux + $dy * $uy + $dz * $uz
    $cz = $dx * $fx + $dy * $fy + $dz * $fz
    $t = [math]::Tan(10 * [math]::PI / 180)
    return @([int][math]::Round(($cx / ($cz * $t) + 1) / 2 * 700), [int][math]::Round((1 - $cy / ($cz * $t)) / 2 * 700))
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

function Send-Keys([string]$keys) {
    if ($keys) { [System.Windows.Forms.SendKeys]::SendWait($keys); Start-Sleep -Milliseconds 700 }
}

function Invoke-Case([hashtable]$c) {
    # index with ['...']: on a hashtable, $c.Keys is the hashtable's own key list, not our field
    $name = $c['Name']
    $log = Join-Path $OutDir "$name.log"
    $profilePath = Join-Path $OutDir "$name.profile.txt"
    if (Test-Path $profilePath) { Remove-Item $profilePath }
    $yaw = 0.0; $pitch = 68.0
    $gameArgs = "-d -profile `"$profilePath`" "
    $gameArgs += if ($c['GameArgs'] -and $c['GameArgs'] -match '-bot') { $c['GameArgs'] } else { "-human $($c['GameArgs'])" }
    if ($c['Cam']) {
        $parts = $c['Cam'].Split(','); $yaw = [double]$parts[0]; $pitch = [double]$parts[1]
        $gameArgs += " -cam $($c['Cam'])"
    }

    $p = Start-Process -FilePath $Exe -ArgumentList $gameArgs -WorkingDirectory $RepoRoot -PassThru -RedirectStandardError $log
    try {
        $deadline = (Get-Date).AddSeconds(10)
        while ($p.MainWindowHandle -eq 0 -and (Get-Date) -lt $deadline) { Start-Sleep -Milliseconds 200; $p.Refresh() }
        if ($p.MainWindowHandle -eq 0) { throw "window never appeared" }
        Start-Sleep -Milliseconds 800
        $h = $p.MainWindowHandle
        [UiTestWin32]::SetForegroundWindow($h) | Out-Null

        Send-Keys $c['KeysBefore']
        foreach ($sq in ("$($c['Squares'])" -split ' ' | Where-Object { $_ })) {
            if ($sq -like 'wait:*') { Start-Sleep -Seconds ([double]$sq.Substring(5)); continue }
            $xy = Get-SquarePixel $sq $yaw $pitch
            $pt = New-Object UiTestWin32+POINT; $pt.X = $xy[0]; $pt.Y = $xy[1]
            [UiTestWin32]::ClientToScreen($h, [ref]$pt) | Out-Null
            [UiTestWin32]::SetCursorPos($pt.X, $pt.Y) | Out-Null
            Start-Sleep -Milliseconds 100
            [UiTestWin32]::mouse_event(2, 0, 0, 0, 0); Start-Sleep -Milliseconds 40; [UiTestWin32]::mouse_event(4, 0, 0, 0, 0)
            Start-Sleep -Milliseconds 700      # let the move animation finish
        }
        Send-Keys $c['Keys']
        if ($c['Wait']) { Start-Sleep -Seconds ([double]$c['Wait']) }
        Start-Sleep -Milliseconds 1200     # let capture animations finish before the screenshot
        if ($p.HasExited) { $p.WaitForExit(); throw "game exited early (exit code $($p.ExitCode))" }
        Save-WindowShot $h (Join-Path $OutDir "$name.png")
    }
    finally {
        if (-not $p.HasExited) { Stop-Process -Id $p.Id -Force }
        $p.WaitForExit()
    }

    $lines = @(Get-Content $log | Where-Object { $_ -notmatch '^Obj file' })
    $at = 0
    foreach ($e in @($c['Expect'])) {
        if (-not $e) { continue }
        $found = $false
        for (; $at -lt $lines.Count; $at++) {
            $hit = if ($e.StartsWith('~')) { $lines[$at] -match $e.Substring(1) } else { $lines[$at] -eq $e }
            if ($hit) { $found = $true; $at++; break }
        }
        if (-not $found) { throw "expected log line '$e' (in order) - see $log" }
    }
    foreach ($f in @($c['Forbid'])) { if ($f -and $lines -contains $f) { throw "unexpected log line '$f' - see $log" } }
}

$promoFen = '-fen "8/P6k/8/8/8/8/8/K7 w - - 0 1"'
$cases = @(
    # --- rules through the real UI (two players) ---
    @{ Name = 'scholars-mate'; Squares = 'e2 e4 e7 e5 f1 c4 b8 c6 d1 h5 g8 f6 h5 f7 a7 a6'
       Expect = 'e2-e4', 'e7-e5', 'Bf1-c4', 'Nb8-c6', 'Qd1-h5', 'Ng8-f6', 'Qh5xf7#'; Forbid = 'a7-a6' },
    @{ Name = 'illegal-move'; Squares = 'd2 d5 d2 d4'
       Expect = 'not legal: d2-d5', 'd2-d4' },
    @{ Name = 'castle-by-rook-click'; GameArgs = '-fen "r3k2r/pppppppp/8/8/8/8/PPPPPPPP/R3K2R w KQkq - 0 1"'; Squares = 'e1 h1 e8 a8'
       Expect = 'O-O', 'O-O-O' },
    @{ Name = 'en-passant'; Squares = 'e2 e4 a7 a6 e4 e5 d7 d5 e5 d6'
       Expect = 'e4-e5', 'd7-d5', 'e5xd6' },
    @{ Name = 'promotion-auto-queen'; GameArgs = $promoFen; Squares = 'a7 a8'
       Expect = , 'a7-a8=Q' },
    @{ Name = 'promotion-choose-bishop'; GameArgs = "$promoFen -choosepromo"; Squares = 'a7 a8'; Keys = '3'
       Expect = , 'a7-a8=B'; Forbid = 'a7-a8=Q' },
    @{ Name = 'stalemate'; GameArgs = '-fen "7k/8/6Q1/8/8/8/8/K7 w - - 0 1"'; Squares = 'g6 f7 h8 g8'
       Expect = , 'Qg6-f7'; Forbid = 'Kh8-g8' },
    @{ Name = 'undo'; Squares = 'e2 e4 e7 e5'; Keys = 'u'
       Expect = 'e7-e5', 'undo e7-e5' },

    # --- clicking from other camera angles ---
    @{ Name = 'click-after-orbit'; Cam = '35,50'; Squares = 'e2 e4 e7 e5 g1 f3'
       Expect = 'e2-e4', 'e7-e5', 'Ng1-f3' },
    @{ Name = 'click-from-black-side'; Cam = '180,68'; Squares = 'e2 e4 e7 e5'
       Expect = 'e2-e4', 'e7-e5' },
    @{ Name = 'click-top-down'; Cam = '0,89.5'; Squares = 'b1 c3 g8 f6'
       Expect = 'Nb1-c3', 'Ng8-f6' },

    # --- options ---
    @{ Name = 'hints-off-still-moves'; GameArgs = '-nohints'; Squares = 'e2 e4'
       Expect = , 'e2-e4' },
    @{ Name = 'auto-flip'; GameArgs = '-autoflip'; Squares = 'e2 e4'
       Expect = 'e2-e4', 'view: black side' },

    # --- clock ---
    @{ Name = 'no-undo-when-timed'; GameArgs = '-tc 3'; Squares = 'e2 e4'; Keys = 'u'
       Expect = , 'e2-e4'; Forbid = 'undo e2-e4' },
    @{ Name = 'pause-blocks-moves'; GameArgs = '-tc 3'; KeysBefore = 'p'; Squares = 'e2 e4'
       Forbid = , 'e2-e4' },
    @{ Name = 'clock-runs-out'; GameArgs = '-tc 1 -clock 3'; Squares = 'e2 e4'; Wait = 4
       Expect = 'e2-e4', 'time out: black' },

    # --- computer opponent (fixed rating and seed, so it's repeatable) ---
    @{ Name = 'bot-replies'; GameArgs = '-bot -color white -botelo 1000 -seed 7'; Squares = 'e2 e4'; Wait = 3
       Expect = '~^--- new game vs bot 1000, you play white', 'e2-e4', '~^bot: ' },
    @{ Name = 'bot-moves-first-as-white'; GameArgs = '-bot -color black -botelo 800 -seed 3'; Wait = 3
       Expect = '~you play black', '~^bot: ' },
    @{ Name = 'bot-undo-takes-back-both'; GameArgs = '-bot -color white -botelo 1000 -seed 7'; Squares = 'e2 e4 wait:3'; Keys = 'u'
       Expect = 'e2-e4', '~^bot: ', '~^undo ', 'undo e2-e4' },
    @{ Name = 'bot-leaving-counts-as-loss'; GameArgs = '-bot -color white -botelo 1000 -seed 7'; Squares = 'e2 e4 wait:3 d2 d3 wait:3'; Keys = 'n'
       Expect = 'e2-e4', '~^bot: ', 'd2-d3', 'left an unfinished game: counted as resigning', 'result: loss, RATING 1000 -> 980 (-20)' }
)

$failed = 0
$summary = @("UI test run $(Get-Date -Format s)  exe: $Exe")
Write-Host "== UI tests ($Exe)"
foreach ($c in $cases) {
    try {
        Invoke-Case $c
        $line = "ok   {0}" -f $c['Name']
    }
    catch {
        $failed++
        $line = "FAIL {0}: {1}" -f $c['Name'], $_.Exception.Message
    }
    Write-Host $line
    $summary += $line
}
$summary += $(if ($failed -gt 0) { "$failed UI test(s) failed" } else { 'all UI tests passed' })
$summary | Set-Content -Encoding utf8 (Join-Path $OutDir 'summary.txt')
Write-Host "Logs, screenshots, and summary.txt are in $OutDir"
if ($failed -gt 0) { exit 1 }
exit 0
