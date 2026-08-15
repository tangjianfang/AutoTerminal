# Screenshot a window by HWND: .\screenshot_window.ps1 -Hwnd 0x3D0742 -Out path.png
param(
    [long]$Hwnd,
    [string]$Out = "$PSScriptRoot\..\build\window_shot.png"
)

Add-Type -AssemblyName System.Drawing
Add-Type -AssemblyName System.Windows.Forms
Add-Type -ReferencedAssemblies System.Drawing @"
using System;
using System.Runtime.InteropServices;
public class Shot {
  [DllImport("user32.dll")] public static extern bool GetWindowRect(IntPtr h, out R r);
  [DllImport("user32.dll")] public static extern bool SetForegroundWindow(IntPtr h);
  public struct R { public int L, T, Rt, B; }
  public static void Take(IntPtr h, string path) {
    R r; GetWindowRect(h, out r);
    int w = r.Rt - r.L, ht = r.B - r.T;
    SetForegroundWindow(h);
    System.Threading.Thread.Sleep(300);
    using (var bmp = new System.Drawing.Bitmap(w, ht)) {
      using (var g = System.Drawing.Graphics.FromImage(bmp)) {
        g.CopyFromScreen(r.L, r.T, 0, 0, new System.Drawing.Size(w, ht));
      }
      bmp.Save(path, System.Drawing.Imaging.ImageFormat.Png);
    }
  }
}
"@
[Shot]::Take([IntPtr]$Hwnd, $Out)
Write-Host "saved: $Out"
