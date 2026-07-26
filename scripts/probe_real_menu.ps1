Add-Type @"
using System;
using System.Runtime.InteropServices;
using System.Text;
using System.Threading;

public class W {
  [DllImport("user32.dll", CharSet=CharSet.Unicode, SetLastError=true)]
  public static extern IntPtr FindWindowW(string cls, string title);

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
  public static extern IntPtr GetForegroundWindow();

  [DllImport("user32.dll")]
  public static extern bool EnumWindows(EnumProc cb, IntPtr l);

  [DllImport("user32.dll", SetLastError=true)]
  public static extern bool SetCursorPos(int x, int y);

  public delegate bool EnumProc(IntPtr h, IntPtr l);
}

[StructLayout(LayoutKind.Sequential)]
public struct RECT { public int Left, Top, Right, Bottom; }

public class P {
  static IntPtr trayIconHwnd = IntPtr.Zero;
  static bool EnumCb(IntPtr h, IntPtr l) {
    var sb = new StringBuilder(256);
    W.GetClassNameW(h, sb, 256);
    string cls = sb.ToString();
    if (cls == "Shell_TrayWnd") {
      // Find the notify icon area within the tray
      IntPtr notify = W.FindWindowW("TrayNotifyWnd", null);
      if (notify != IntPtr.Zero) trayIconHwnd = notify;
      else trayIconHwnd = h;
      return false;
    }
    return true;
  }

  static IntPtr findMenuWindow() {
    IntPtr found = IntPtr.Zero;
    W.EnumWindows((hwnd, l) => {
      var sb = new StringBuilder(64);
      W.GetClassNameW(hwnd, sb, 64);
      if (sb.ToString() == "#32768") {
        found = hwnd;
        return false;
      }
      return true;
    }, IntPtr.Zero);
    return found;
  }

  public static void Main() {
    IntPtr mw = W.FindWindowW("AutoTerminal.MessageWindow.v1", null);
    Console.WriteLine("MessageWindow hwnd=0x" + mw.ToInt64().ToString("X"));
    if (mw == IntPtr.Zero) { Console.WriteLine("FAIL: not running"); return; }

    W.EnumWindows(EnumCb, IntPtr.Zero);
    if (trayIconHwnd == IntPtr.Zero) {
      Console.WriteLine("Tray icon host not found — placing cursor at screen center");
      W.SetCursorPos(960, 540);
      Thread.Sleep(100);
    } else {
      RECT r;
      W.GetWindowRect(trayIconHwnd, out r);
      Console.WriteLine("Tray host rect=(" + r.Left + "," + r.Top + ") " +
                        (r.Right-r.Left) + "x" + (r.Bottom-r.Top));
      // Move cursor to the right side of the tray
      W.SetCursorPos(r.Right - 30, (r.Top + r.Bottom) / 2);
      Thread.Sleep(100);
    }

    IntPtr fgBefore = W.GetForegroundWindow();
    var sbFgBefore = new StringBuilder(64);
    W.GetClassNameW(fgBefore, sbFgBefore, 64);
    Console.WriteLine("FG before: class='" + sbFgBefore.ToString() + "'");

    uint WM_AT_TRAYICON = 0x4C8;
    uint WM_RBUTTONUP   = 0x0205;
    W.PostMessageW(mw, WM_AT_TRAYICON, IntPtr.Zero, (IntPtr)WM_RBUTTONUP);
    Console.WriteLine("PostMessage(WM_AT_TRAYICON, WM_RBUTTONUP) sent");

    bool sawMenu = false;
    for (int i = 0; i < 6; ++i) {
      Thread.Sleep(250);
      IntPtr menu = findMenuWindow();
      IntPtr fg = W.GetForegroundWindow();
      var sbFg = new StringBuilder(64);
      W.GetClassNameW(fg, sbFg, 64);
      if (menu != IntPtr.Zero) {
        RECT m;
        W.GetWindowRect(menu, out m);
        Console.WriteLine("+" + (250*(i+1)) + "ms: MENU FOUND hwnd=0x" + menu.ToInt64().ToString("X") +
                          " at (" + m.Left + "," + m.Top + ") " + (m.Right-m.Left) + "x" + (m.Bottom-m.Top));
        sawMenu = true;
      } else {
        Console.WriteLine("+" + (250*(i+1)) + "ms: no menu window, FG class='" + sbFg.ToString() + "'");
      }
    }

    Console.WriteLine();
    Console.WriteLine(sawMenu ? "RESULT: tray menu DID appear" : "RESULT: NO menu window detected");
  }
}
"@
[P]::Main()