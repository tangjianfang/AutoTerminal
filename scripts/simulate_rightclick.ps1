Add-Type @"
using System;
using System.Runtime.InteropServices;
public class S {
  [DllImport("user32.dll", CharSet=CharSet.Unicode, SetLastError=true)]
  public static extern IntPtr FindWindowW(string cls, string title);

  [DllImport("user32.dll", SetLastError=true)]
  public static extern bool PostMessageW(IntPtr h, uint m, IntPtr w, IntPtr l);

  [DllImport("user32.dll")]
  [return: MarshalAs(UnmanagedType.Bool)]
  public static extern bool IsWindowVisible(IntPtr h);
}

public class Sim {
  public static void Main() {
    IntPtr mw = S.FindWindowW("AutoTerminal.MessageWindow.v1", null);
    Console.WriteLine("MessageWindow hwnd=0x" + mw.ToInt64().ToString("X"));
    if (mw == IntPtr.Zero) { Console.WriteLine("FAIL: not running"); return; }

    // WM_AT_TRAYICON = WM_USER + 200 = 0x400 + 200 = 0x4C8 = 1224
    // lParam = WM_RBUTTONUP = 0x0205
    uint WM_AT_TRAYICON = 0x4C8;
    uint WM_RBUTTONUP   = 0x0205;
    bool ok = S.PostMessageW(mw, WM_AT_TRAYICON, IntPtr.Zero, (IntPtr)WM_RBUTTONUP);
    Console.WriteLine("PostMessage(WM_AT_TRAYICON, lParam=WM_RBUTTONUP) -> " + ok);
  }
}
"@
[Sim]::Main()
