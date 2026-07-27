# Check the shell's tray icon cache (IconStreams) for AT entries
$path = "$env:LOCALAPPDATA\Microsoft\Windows\Explorer"
Write-Host "=== Explorer tray cache ==="
if (Test-Path $path) {
  Get-ChildItem $path | Where-Object { $_.Name -match "IconStreams|tray" } | Format-Table Name, Length, LastWriteTime -AutoSize
}
Write-Host ""

# Also check for the icon GUID in the IconStreams binary
$isPath = "$env:LOCALAPPDATA\Microsoft\Windows\Explorer\IconStreams.cache"
if (Test-Path $isPath) {
  $bytes = [System.IO.File]::ReadAllBytes($isPath)
  Write-Host "IconStreams.cache size = $($bytes.Length) bytes"
  # Search for our GUID bytes
  $guidBytes1 = [System.BitConverter]::GetBytes([uint32]0x4f9db5e0)
  $guidBytes2 = [System.BitConverter]::GetBytes([uint32]0x3a21)
  $guidBytes3 = [System.BitConverter]::GetBytes([uint32]0x4e47)
  $guidBytes4 = @(0xb5, 0xc9, 0xa8, 0xe2, 0xb1, 0xc0, 0xd1, 0x11)
  $needle = $guidBytes1 + $guidBytes2 + $guidBytes3 + $guidBytes4
  $pattern = [System.Text.Encoding]::ASCII.GetString($needle)
  Write-Host "Looking for GUID bytes pattern (raw and ASCII)..."
  for ($i = 0; $i -lt $bytes.Length - $needle.Length; $i++) {
    $match = $true
    for ($j = 0; $j -lt $needle.Length; $j++) {
      if ($bytes[$i + $j] -ne $needle[$j]) { $match = $false; break }
    }
    if ($match) {
      Write-Host "  Found GUID at offset $i"
      # Print surrounding context
      $start = [Math]::Max(0, $i - 32)
      $end = [Math]::Min($bytes.Length - 1, $i + $needle.Length + 32)
      $ctx = $bytes[$start..$end]
      Write-Host ("  Context: " + ([BitConverter]::ToString($ctx)))
    }
  }
} else {
  Write-Host "IconStreams.cache not found"
}

# Check past icons
$pastPath = "$env:LOCALAPPDATA\Microsoft\Windows\Explorer\PastIconsStream.cache"
if (Test-Path $pastPath) {
  $bytes = [System.IO.File]::ReadAllBytes($pastPath)
  Write-Host ""
  Write-Host "PastIconsStream.cache size = $($bytes.Length) bytes"
} else {
  Write-Host "PastIconsStream.cache not found"
}

# Count tray icons via Windows API
Write-Host ""
Write-Host "=== Enumerating tray icons ==="
Add-Type @"
using System;
using System.Runtime.InteropServices;
using System.Text;
public class Tray {
  [DllImport("user32.dll", SetLastError=true)]
  [return: MarshalAs(UnmanagedType.Bool)]
  public static extern bool EnumWindows(EnumProc cb, IntPtr l);
  [DllImport("user32.dll", CharSet=CharSet.Unicode, SetLastError=true)]
  [return: MarshalAs(UnmanagedType.Bool)]
  public static extern bool GetClassNameW(IntPtr h, StringBuilder sb, int n);
  [DllImport("user32.dll", SetLastError=true)]
  [return: MarshalAs(UnmanagedType.Bool)]
  public static extern bool GetWindowRect(IntPtr h, out RECT r);
  [DllImport("user32.dll")]
  public static extern IntPtr FindWindowW(string cls, string title);
  public delegate bool EnumProc(IntPtr h, IntPtr l);
}
[StructLayout(LayoutKind.Sequential)]
public struct RECT { public int Left, Top, Right, Bottom; }
"@

$trayWnd = [Tray]::FindWindowW("Shell_TrayWnd", $null)
Write-Host ("Shell_TrayWnd hwnd=0x{0:X}" -f $trayWnd.ToInt64())
if ($trayWnd -ne [IntPtr]::Zero) {
  $r = New-Object RECT
  [Tray]::GetWindowRect($trayWnd, [ref]$r) | Out-Null
  Write-Host ("  rect=({0},{1}) {2}x{3}" -f $r.Left, $r.Top, ($r.Right-$r.Left), ($r.Bottom-$r.Top))
}

# Find notify area
$notify = [Tray]::FindWindowW("TrayNotifyWnd", $null)
Write-Host ("TrayNotifyWnd hwnd=0x{0:X}" -f $notify.ToInt64())
if ($notify -ne [IntPtr]::Zero) {
  $r = New-Object RECT
  [Tray]::GetWindowRect($notify, [ref]$r) | Out-Null
  Write-Host ("  rect=({0},{1}) {2}x{3}" -f $r.Left, $r.Top, ($r.Right-$r.Left), ($r.Bottom-$r.Top))
}