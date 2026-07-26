Add-Type -AssemblyName System.Windows.Forms
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
  [DllImport("user32.dll", SetLastError=true)]
  public static extern bool PostMessageW(IntPtr h, uint m, IntPtr w, IntPtr l);
  [DllImport("user32.dll", SetLastError=true)]
  [return: MarshalAs(UnmanagedType.Bool)]
  public static extern bool GetWindowRect(IntPtr h, out RECT r);
  [DllImport("user32.dll")]
  public static extern bool EnumWindows(EnumProc cb, IntPtr l);
  [DllImport("user32.dll")]
  public static extern IntPtr GetWindow(IntPtr h, uint cmd);
  [DllImport("user32.dll")]
  public static extern bool SetCursorPos(int x, int y);
  [DllImport("user32.dll")]
  public static extern void mouse_event(int f, int x, int y, int d, int e);
  public const uint GW_CHILD = 5;
  public const uint GW_HWNDNEXT = 2;
  public delegate bool EnumProc(IntPtr h, IntPtr l);
}
[StructLayout(LayoutKind.Sequential)]
public struct RECT { public int Left, Top, Right, Bottom; }
"@

# 1. Find the popup helper
$helper = [W]::FindWindowW("AutoTerminal.PopupHelper.v1", $null)
if ($helper -eq [IntPtr]::Zero) {
  Write-Host "FAIL: PopupHelper window not found"
  exit 1
}
Write-Host ("PopupHelper hwnd=0x{0:X}" -f $helper.ToInt64())

# 2. Set cursor at a safe screen position
$screen = [System.Windows.Forms.Screen]::PrimaryScreen.Bounds
$cx = [int]($screen.Width / 2)
$cy = [int]($screen.Height / 2)
[W]::SetCursorPos($cx, $cy) | Out-Null
Write-Host ("Cursor placed at ($cx, $cy)")

# 3. Post WM_AT_TRAYICON with WM_RBUTTONUP to the popup helper
$WM_AT_TRAYICON = 0x4C8
$WM_RBUTTONUP   = 0x0205
[W]::PostMessageW($helper, $WM_AT_TRAYICON, [IntPtr]::Zero, [IntPtr]$WM_RBUTTONUP) | Out-Null
Write-Host "Posted WM_AT_TRAYICON with WM_RBUTTONUP"

# 4. Wait for menu window
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
Write-Host ("Menu window hwnd=0x{0:X}" -f $script:menuHwnd.ToInt64())

# 5. List menu items
$child = [W]::GetWindow($script:menuHwnd, [W]::GW_CHILD)
$i = 0
while ($child -ne [IntPtr]::Zero) {
  $text = New-Object System.Text.StringBuilder 256
  [W]::GetWindowTextW($child, $text, 256) | Out-Null
  $r = New-Object RECT
  [void][W]::GetWindowRect($child, [ref]$r)
  Write-Host ("  [{0}] '{1}'" -f $i, $text.ToString())
  $child = [W]::GetWindow($child, [W]::GW_HWNDNEXT)
  $i++
}

Write-Host "PASS"
exit 0