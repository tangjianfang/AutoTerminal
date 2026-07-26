# Trigger right-click on running AT and see what happens.
# We don't know the message window HWND from outside the process, but we can
# enumerate all top-level windows belonging to AutoTerminal.exe and find the
# one whose class name starts with "AutoTerminal.MessageWindow".

Add-Type @"
using System;
using System.Runtime.InteropServices;
using System.Text;
using System.Diagnostics;
public class W {
  [DllImport("user32.dll", CharSet=CharSet.Unicode, SetLastError=true)]
  public static extern IntPtr FindWindowW(string c, string t);
  [DllImport("user32.dll", CharSet=CharSet.Unicode)]
  public static extern bool EnumWindows(EnumProc cb, IntPtr l);
  [DllImport("user32.dll", SetLastError=true)]
  [return: MarshalAs(UnmanagedType.Bool)]
  public static extern bool GetClassNameW(IntPtr h, StringBuilder sb, int n);
  [DllImport("user32.dll", CharSet=CharSet.Unicode)]
  public static extern int GetWindowTextW(IntPtr h, StringBuilder sb, int n);
  [DllImport("user32.dll", SetLastError=true)]
  [return: MarshalAs(UnmanagedType.Bool)]
  public static extern bool GetWindowThreadProcessId(IntPtr h, out UInt32 pid);
  [DllImport("user32.dll")]
  public static extern bool PostMessageW(IntPtr h, uint m, IntPtr w, IntPtr l);
  public delegate bool EnumProc(IntPtr h, IntPtr l);
}
"@

$at = Get-Process AutoTerminal -ErrorAction SilentlyContinue
if (-not $at) { Write-Host "FAIL: AutoTerminal not running"; exit 1 }
$atPid = $at.Id
Write-Host "AutoTerminal PID=$atPid started=$($at.StartTime)"

# Enumerate all top-level windows and find ones belonging to AT process
$found = New-Object System.Collections.Generic.List[object]
$script:result = [IntPtr]::Zero
[W]::EnumWindows({
  param($h, $l)
  [UInt32]$pid = 0
  if ([W]::GetWindowThreadProcessId($h, [ref]$pid)) {
    if ($pid -eq $script:atPid) {
      $cls = New-Object System.Text.StringBuilder 256
      [W]::GetClassNameW($h, $cls, 256) | Out-Null
      $title = New-Object System.Text.StringBuilder 256
      [W]::GetWindowTextW($h, $title, 256) | Out-Null
      $script:found.Add(@{ Hwnd=$h; Class=$cls.ToString(); Title=$title.ToString() })
    }
  }
  return $true
}, [IntPtr]::Zero) | Out-Null

Write-Host ("Found {0} top-level windows for AT:" -f $script:found.Count)
foreach ($w in $script:found) {
  Write-Host ("  hwnd=0x{0:X} class='{1}' title='{2}'" -f $w.Hwnd.ToInt64(), $w.Class, $w.Title)
}

# Find the message window
$mw = $script:found | Where-Object { $_.Class -like "AutoTerminal.MessageWindow*" } | Select-Object -First 1
if (-not $mw) {
  Write-Host "FAIL: no message window found"
  exit 2
}
Write-Host ("MessageWindow hwnd=0x{0:X}" -f $mw.Hwnd.ToInt64())

# Trigger right-click
$WM_AT_TRAYICON = 0x4C8
$WM_RBUTTONUP   = 0x0205
$ok = [W]::PostMessageW($mw.Hwnd, $WM_AT_TRAYICON, [IntPtr]::Zero, [IntPtr]$WM_RBUTTONUP)
Write-Host ("PostMessage -> {0}" -f $ok)

Start-Sleep -Seconds 8

# Check if menu window appeared
$script:menuFound = [IntPtr]::Zero
[W]::EnumWindows({
  param($h, $l)
  $cls = New-Object System.Text.StringBuilder 64
  [W]::GetClassNameW($h, $cls, 64) | Out-Null
  if ($cls.ToString() -eq "#32768") {
    $script:menuFound = $h
    return $false
  }
  return $true
}, [IntPtr]::Zero) | Out-Null

if ($script:menuFound -ne [IntPtr]::Zero) {
  Write-Host ("Menu window hwnd=0x{0:X} IS VISIBLE" -f $script:menuFound.ToInt64())
} else {
  Write-Host "NO menu window detected"
}