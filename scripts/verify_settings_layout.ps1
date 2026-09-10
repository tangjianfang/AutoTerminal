# Geometry-based verification of the Settings dialog layout (no screenshots).
# Enumerates every child control, converts to client coordinates, and asserts:
#   1. every control is fully inside the client area (no clipping)
#   2. no two sibling controls overlap (>= 2px in both axes)
#   3. above the button row, left-column controls stay left of the gutter and
#      right-column controls start after it (two-column separation)
# Exit code 0 = all checks pass; 1 = failures (listed).
param(
    [int]$LeftColEnd = 374,   # logical px: left margin 14 + left col 360
    [int]$GutterEnd  = 390,   # logical px: + gutter 16
    [double]$Scale   = 1.0    # logical->physical scale; auto-detected if 0
)

$ErrorActionPreference = 'Stop'
Add-Type @"
using System;
using System.Runtime.InteropServices;
public class V {
  [DllImport("user32.dll", CharSet=CharSet.Unicode)] public static extern IntPtr FindWindowW(string c, string t);
  [DllImport("user32.dll")] public static extern bool GetWindowRect(IntPtr h, out R r);
  [DllImport("user32.dll")] public static extern bool GetClientRect(IntPtr h, out R r);
  public delegate bool EnumCB(IntPtr h, IntPtr lp);
  [DllImport("user32.dll")] public static extern bool EnumWindows(EnumCB cb, IntPtr lp);
  [DllImport("user32.dll")] public static extern bool EnumChildWindows(IntPtr p, EnumCB cb, IntPtr lp);
  [DllImport("user32.dll")] public static extern int GetDlgCtrlID(IntPtr h);
  [DllImport("user32.dll", CharSet=CharSet.Unicode)] public static extern int GetClassNameW(IntPtr h, System.Text.StringBuilder s, int n);
  [DllImport("user32.dll")] public static extern bool ScreenToClient(IntPtr h, ref P p);
  [DllImport("user32.dll")] public static extern uint GetDpiForWindow(IntPtr h);
  [DllImport("user32.dll")] public static extern bool IsWindowVisible(IntPtr h);
  public struct R { public int L, T, Rt, B; }
  public struct P { public int X, Y; }
}
"@

# Locate the settings window by class via EnumWindows (FindWindowW proved
# unreliable across sessions on this box).
$dlg = [IntPtr]::Zero
$find = {
    param($h, $lp)
    $sb0 = New-Object System.Text.StringBuilder 64
    [void][V]::GetClassNameW($h, $sb0, 64)
    if ($sb0.ToString() -eq 'AutoTerminal.SettingsWindow.v1' -and [V]::IsWindowVisible($h)) { $script:dlg = $h }
    return $true
}
[V]::EnumWindows($find, [IntPtr]::Zero) | Out-Null
if ($dlg -eq [IntPtr]::Zero) { Write-Host 'FAIL: settings window not found'; exit 1 }

$dpi = [V]::GetDpiForWindow($dlg)
if ($Scale -le 0) { $Scale = $dpi / 96.0 }
Write-Host ("hwnd=0x{0:X} dpi={1} scale={2}" -f $dlg.ToInt64(), $dpi, $Scale)

$cr = New-Object V+R
[void][V]::GetClientRect($dlg, [ref]$cr)
$cx = $cr.Rt - $cr.L; $cy = $cr.B - $cr.T
Write-Host ("client: {0}x{1} (physical)" -f $cx, $cy)

# Collect controls: id, class, client-space rect
$ctl = New-Object System.Collections.ArrayList
$cb = {
    param($h, $lp)
    $wr = New-Object V+R
    [void][V]::GetWindowRect($h, [ref]$wr)
    $tl = New-Object V+P; $tl.X = $wr.L; $tl.Y = $wr.T
    $br = New-Object V+P; $br.X = $wr.Rt; $br.Y = $wr.B
    [void][V]::ScreenToClient($dlg, [ref]$tl)
    [void][V]::ScreenToClient($dlg, [ref]$br)
    $sb = New-Object System.Text.StringBuilder 64
    [void][V]::GetClassNameW($h, $sb, 64)
    [void]$ctl.Add([pscustomobject]@{
        Id = [V]::GetDlgCtrlID($h); Class = $sb.ToString()
        L = $tl.X; T = $tl.Y; R = $br.X; B = $br.Y
    })
    return $true
}
[V]::EnumChildWindows($dlg, $cb, [IntPtr]::Zero) | Out-Null
Write-Host ("controls: {0}" -f $ctl.Count)

$fail = 0

# --- check 1: containment ---------------------------------------------------
foreach ($c in $ctl) {
    if ($c.L -lt 0 -or $c.T -lt 0 -or $c.R -gt $cx -or $c.B -gt $cy) {
        Write-Host ("FAIL out-of-client id={0} class={1} rect=({2},{3})-({4},{5})" -f $c.Id, $c.Class, $c.L, $c.T, $c.R, $c.B)
        $fail++
    }
}

# --- check 2: pairwise overlap (>=2px both axes) ------------------------------
for ($i = 0; $i -lt $ctl.Count; $i++) {
    for ($j = $i + 1; $j -lt $ctl.Count; $j++) {
        $a = $ctl[$i]; $b = $ctl[$j]
        $ox = [Math]::Min($a.R, $b.R) - [Math]::Max($a.L, $b.L)
        $oy = [Math]::Min($a.B, $b.B) - [Math]::Max($a.T, $b.T)
        if ($ox -ge 2 -and $oy -ge 2) {
            Write-Host ("FAIL overlap id={0}<->id={1} overlap={2}x{3}px a=({4},{5})-({6},{7}) b=({8},{9})-({10},{11})" -f `
                $a.Id, $b.Id, $ox, $oy, $a.L, $a.T, $a.R, $a.B, $b.L, $b.T, $b.R, $b.B)
            $fail++
        }
    }
}

# --- check 3: column separation above the button row --------------------------
# Button row = the bottom-most 4 buttons (ids 1016-1019 by CtrlId order: open/apply/cancel/exit).
# Every OTHER control with y above the button row must sit fully left of the
# gutter start or fully right of the gutter end.
$btnTop = ($ctl | Where-Object { $_.Id -in 1016,1017,1018,1019 } | Measure-Object -Property T -Minimum).Minimum
foreach ($c in $ctl | Where-Object { $_.B -le $btnTop }) {
    $gutL = [int]($LeftColEnd * $Scale); $gutR = [int]($GutterEnd * $Scale)
    if (-not ($c.R -le $gutL -or $c.L -ge $gutR)) {
        Write-Host ("FAIL crosses-gutter id={0} class={1} rect=({2},{3})-({4},{5}) gutter={6}..{7}" -f $c.Id, $c.Class, $c.L, $c.T, $c.R, $c.B, $gutL, $gutR)
        $fail++
    }
}

if ($fail -eq 0) { Write-Host 'PASS: containment, no-overlap, two-column separation all OK'; exit 0 }
Write-Host ("{0} failure(s)" -f $fail); exit 1
