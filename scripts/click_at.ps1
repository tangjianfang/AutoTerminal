# Move cursor to a position and click there to dismiss any popup menu.
param([int]$X = 100, [int]$Y = 100)

Add-Type @"
using System;
using System.Runtime.InteropServices;
public class M {
  [DllImport("user32.dll")]
  public static extern bool SetCursorPos(int x, int y);
  [DllImport("user32.dll")]
  public static extern void mouse_event(int f, int x, int y, int d, int e);
}
"@
[M]::SetCursorPos($X, $Y)
Start-Sleep -Milliseconds 100
# LEFTDOWN=0x02, LEFTUP=0x04
[M]::mouse_event(0x0002, 0, 0, 0, 0)
Start-Sleep -Milliseconds 50
[M]::mouse_event(0x0004, 0, 0, 0, 0)
Write-Host "clicked at ($X, $Y)"