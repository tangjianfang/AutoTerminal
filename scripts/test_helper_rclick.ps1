Add-Type @"
using System;
using System.Runtime.InteropServices;
public class T {
  [DllImport("user32.dll", CharSet=CharSet.Unicode, SetLastError=true)]
  public static extern IntPtr FindWindowW(string cls, string title);

  [DllImport("user32.dll", SetLastError=true)]
  public static extern bool PostMessageW(IntPtr h, uint m, IntPtr w, IntPtr l);

  [DllImport("user32.dll")]
  [return: MarshalAs(UnmanagedType.Bool)]
  public static extern bool IsWindowVisible(IntPtr h);
}
"@

# The popup helper's window class is registered in ui_bridge.cpp.
$helper = [T]::FindWindowW("AutoTerminal.PopupHelper.v1", $null)
Write-Host ("PopupHelper hwnd=0x{0:X}" -f $helper.ToInt64())
if ($helper -eq [IntPtr]::Zero) {
  Write-Host "FAIL: PopupHelper window not found"
  exit 1
}

# WM_AT_TRAYICON = WM_USER + 200 = 0x4C8
# lParam variants: WM_RBUTTONUP = 0x0205, WM_CONTEXTMENU = 0x007B
$WM_AT_TRAYICON = 0x4C8
$WM_RBUTTONUP   = 0x0205

$ok = [T]::PostMessageW($helper, $WM_AT_TRAYICON, [IntPtr]::Zero, [IntPtr]$WM_RBUTTONUP)
Write-Host ("PostMessage(WM_AT_TRAYICON, lParam=WM_RBUTTONUP) -> {0}" -f $ok)