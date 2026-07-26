Add-Type @"
using System;
using System.Runtime.InteropServices;
public class P {
  [DllImport("user32.dll", CharSet=CharSet.Unicode)]
  public static extern IntPtr FindWindowW(string c, string t);
  [DllImport("user32.dll")]
  public static extern bool GetWindowRect(IntPtr h, out RECT r);
  [DllImport("user32.dll")]
  public static extern IntPtr GetForegroundWindow();

  [StructLayout(LayoutKind.Sequential)]
  public struct RECT { public int Left, Top, Right, Bottom; }

  public static void Main() {
    IntPtr h = FindWindowW("AutoTerminal.SettingsWindow.v1", null);
    if (h == IntPtr.Zero) { Console.WriteLine("no settings window"); return; }
    RECT r;
    GetWindowRect(h, out r);
    int w = r.Right - r.Left;
    int ht = r.Bottom - r.Top;
    Console.WriteLine("settings: left=" + r.Left + " top=" + r.Top + " w=" + w + " h=" + ht);
    IntPtr fg = GetForegroundWindow();
    Console.WriteLine("FG hwnd=0x" + fg.ToInt64().ToString("X"));
    Console.WriteLine("FG == settings? " + (fg == h));
  }
}
"@
[P]::Main()
