Add-Type @"
using System;
using System.Runtime.InteropServices;
using System.Text;
public class W {
  [DllImport("user32.dll", CharSet=CharSet.Unicode)]
  public static extern IntPtr FindWindowW(string c, string t);
  [DllImport("user32.dll", CharSet=CharSet.Unicode)]
  public static extern IntPtr FindWindowExW(IntPtr p, IntPtr c, string cls, string t);
  [DllImport("user32.dll", SetLastError=true)]
  [return: MarshalAs(UnmanagedType.Bool)]
  public static extern bool GetClassNameW(IntPtr h, StringBuilder sb, int n);
  [DllImport("user32.dll", CharSet=CharSet.Unicode)]
  public static extern bool EnumWindows(EnumProc cb, IntPtr l);
  public delegate bool EnumProc(IntPtr h, IntPtr l);
}
"@

# Try by class + null title
$h1 = [W]::FindWindowW("AutoTerminal.MessageWindow.v1", $null)
Write-Host ("FindWindowW(class, null) = 0x" + $h1.ToInt64().ToString("X"))

# Try by class + empty string
$h2 = [W]::FindWindowW("AutoTerminal.MessageWindow.v1", "")
Write-Host ("FindWindowW(class, '') = 0x" + $h2.ToInt64().ToString("X"))

# Enumerate all windows and look for our class
[W]::EnumWindows({
  param($h, $l)
  $sb = New-Object System.Text.StringBuilder 256
  if ([W]::GetClassNameW($h, $sb, 256)) {
    if ($sb.ToString().StartsWith("AutoTerminal")) {
      Write-Host ("  class='" + $sb.ToString() + "' hwnd=0x" + $h.ToInt64().ToString("X"))
    }
  }
  return $true
}, [IntPtr]::Zero) | Out-Null