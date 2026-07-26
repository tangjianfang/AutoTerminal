param([string]$Action = "exit")

# Full E2E test:
#   1. Verify AT message window exists
#   2. Trigger tray right-click via PostMessage (cursor at screen center)
#   3. Wait for menu window (#32768) to appear
#   4. List all menu items by enumerating child windows
#   5. Click the "Exit AutoTerminal" item
#   6. Verify the process exits

Add-Type @"
using System;
using System.Runtime.InteropServices;
using System.Text;
public class W {
  [DllImport("user32.dll", CharSet=CharSet.Unicode)]
  public static extern IntPtr FindWindowW(string c, string t);
  [DllImport("user32.dll", SetLastError=true)]
  [return: MarshalAs(UnmanagedType.Bool)]
  public static extern bool GetClassNameW(IntPtr h, StringBuilder sb, int n);
  [DllImport("user32.dll", CharSet=CharSet.Unicode)]
  public static extern bool GetWindowTextW(IntPtr h, StringBuilder sb, int n);
  [DllImport("user32.dll")]
  public static extern bool PostMessageW(IntPtr h, uint m, IntPtr w, IntPtr l);
  [DllImport("user32.dll", SetLastError=true)]
  [return: MarshalAs(UnmanagedType.Bool)]
  public static extern bool GetWindowRect(IntPtr h, out RECT r);
  [DllImport("user32.dll")]
  public static extern bool EnumWindows(EnumProc cb, IntPtr l);
  [DllImport("user32.dll")]
  public static extern IntPtr GetWindow(IntPtr h, uint cmd);
  public const uint GW_CHILD = 5;
  public const uint GW_HWNDNEXT = 2;
  [DllImport("user32.dll")]
  public static extern bool SetCursorPos(int x, int y);
  [DllImport("user32.dll")]
  public static extern void mouse_event(int f, int x, int y, int d, int e);
  public delegate bool EnumProc(IntPtr h, IntPtr l);
}
[StructLayout(LayoutKind.Sequential)]
public struct RECT { public int Left, Top, Right, Bottom; }
"@

$mw = [W]::FindWindowW("AutoTerminal.MessageWindow.v1", $null)
if ($mw -eq [IntPtr]::Zero) { Write-Host "FAIL: AutoTerminal not running"; exit 1 }
Write-Host ("MessageWindow hwnd=0x" + $mw.ToInt64().ToString("X"))

# Set cursor at a safe screen position (so the menu appears there, not at 0,0)
$screen = [System.Windows.Forms.Screen]::PrimaryScreen.Bounds
$cx = [int]($screen.Width / 2)
$cy = [int]($screen.Height / 2)
[W]::SetCursorPos($cx, $cy) | Out-Null
Start-Sleep -Milliseconds 100
Write-Host ("Cursor placed at ($cx, $cy)")

# Trigger right-click
$WM_AT_TRAYICON = 0x4C8
$WM_RBUTTONUP   = 0x0205
[W]::PostMessageW($mw, $WM_AT_TRAYICON, [IntPtr]::Zero, [IntPtr]$WM_RBUTTONUP) | Out-Null
Write-Host "Posted WM_AT_TRAYICON with WM_RBUTTONUP"

# Wait for menu window
$script:menuHwnd = [IntPtr]::Zero
$script:menuRect = New-Object RECT
$sw = [System.Diagnostics.Stopwatch]::StartNew()
while ($sw.ElapsedMilliseconds -lt 4000) {
  $script:menuHwnd = [IntPtr]::Zero
  [void][W]::EnumWindows({
    param($h, $l)
    $sb = New-Object System.Text.StringBuilder 64
    if ([W]::GetClassNameW($h, $sb, 64)) {
      if ($sb.ToString() -eq "#32768") {
        $script:menuHwnd = $h
        [void][W]::GetWindowRect($h, [ref]$script:menuRect)
        return $false
      }
    }
    return $true
  }, [IntPtr]::Zero)
  if ($script:menuHwnd -ne [IntPtr]::Zero) { break }
  Start-Sleep -Milliseconds 100
}

if ($script:menuHwnd -eq [IntPtr]::Zero) {
  Write-Host "FAIL: no menu window appeared within 4s"
  exit 2
}
Write-Host ("Menu window hwnd=0x" + $script:menuHwnd.ToInt64().ToString("X") +
           " at ($($script:menuRect.Left),$($script:menuRect.Top)) " +
           "$($script:menuRect.Right - $script:menuRect.Left)x$($script:menuRect.Bottom - $script:menuRect.Top)")

# List menu items
$child = [W]::GetWindow($script:menuHwnd, [W]::GW_CHILD)
$items = @()
$i = 0
while ($child -ne [IntPtr]::Zero) {
  $text = New-Object System.Text.StringBuilder 256
  [W]::GetWindowTextW($child, $text, 256) | Out-Null
  $cls = New-Object System.Text.StringBuilder 64
  [W]::GetClassNameW($child, $cls, 64) | Out-Null
  $r = New-Object RECT
  [void][W]::GetWindowRect($child, [ref]$r)
  Write-Host ("  [{0}] '{1}' at ({2},{3}) {4}x{5}" -f $i, $text.ToString(), $r.Left, $r.Top, ($r.Right-$r.Left), ($r.Bottom-$r.Top))
  $items += ,@{ Text=$text.ToString(); Rect=$r }
  $child = [W]::GetWindow($child, [W]::GW_HWNDNEXT)
  $i++
}

if ($Action -ne "exit") {
  Write-Host "Items listed — done"
  exit 0
}

$target = $items | Where-Object { $_.Text -eq "Exit AutoTerminal" } | Select-Object -First 1
if ($target -eq $null) { Write-Host "FAIL: 'Exit AutoTerminal' item not found"; exit 3 }

Write-Host ("Clicking 'Exit AutoTerminal' at ($($target.Rect.Left + 5),$($target.Rect.Top + 5))")
[W]::SetCursorPos($target.Rect.Left + 5, $target.Rect.Top + 5) | Out-Null
Start-Sleep -Milliseconds 100
[W]::mouse_event(0x0002, 0, 0, 0, 0) | Out-Null
Start-Sleep -Milliseconds 50
[W]::mouse_event(0x0004, 0, 0, 0, 0) | Out-Null

$wait = [System.Diagnostics.Stopwatch]::StartNew()
while ($wait.ElapsedMilliseconds -lt 5000) {
  $mw2 = [W]::FindWindowW("AutoTerminal.MessageWindow.v1", $null)
  if ($mw2 -eq [IntPtr]::Zero) {
    Write-Host ("AutoTerminal exited after $($wait.ElapsedMilliseconds)ms")
    exit 0
  }
  Start-Sleep -Milliseconds 200
}
Write-Host "FAIL: process still running after 5s"
exit 4