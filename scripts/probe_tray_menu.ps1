Add-Type @"
using System;
using System.Runtime.InteropServices;
using System.Text;
using System.Threading;

public class W {
  [DllImport("user32.dll", CharSet=CharSet.Unicode, SetLastError=true)]
  public static extern IntPtr FindWindowW(string cls, string title);

  [DllImport("user32.dll")]
  public static extern IntPtr GetForegroundWindow();

  [DllImport("user32.dll", SetLastError=true)]
  [return: MarshalAs(UnmanagedType.Bool)]
  public static extern bool IsWindowVisible(IntPtr h);

  [DllImport("user32.dll", SetLastError=true)]
  [return: MarshalAs(UnmanagedType.Bool)]
  public static extern bool GetWindowRect(IntPtr h, out RECT r);

  [DllImport("user32.dll")]
  public static extern bool PostMessageW(IntPtr h, uint m, IntPtr w, IntPtr l);

  [DllImport("user32.dll", SetLastError=true)]
  [return: MarshalAs(UnmanagedType.Bool)]
  public static extern bool GetClassNameW(IntPtr h, StringBuilder sb, int n);

  [DllImport("user32.dll", CharSet=CharSet.Unicode)]
  public static extern bool GetWindowTextW(IntPtr h, StringBuilder sb, int n);
}

[StructLayout(LayoutKind.Sequential)]
public struct RECT { public int Left, Top, Right, Bottom; }

public class P {
  public static void Main() {
    IntPtr mw = W.FindWindowW("AutoTerminal.MessageWindow.v1", null);
    Console.WriteLine("MessageWindow hwnd=0x" + mw.ToInt64().ToString("X"));
    if (mw == IntPtr.Zero) { Console.WriteLine("FAIL: not running"); return; }

    IntPtr helper = W.FindWindowW("AutoTerminal.PopupHelper.v1", null);
    Console.WriteLine("PopupHelper hwnd=0x" + (helper == IntPtr.Zero ? "0 (NOT FOUND)" : helper.ToInt64().ToString("X")));
    if (helper != IntPtr.Zero) {
      Console.WriteLine("  visible=" + W.IsWindowVisible(helper));
      RECT r2;
      W.GetWindowRect(helper, out r2);
      Console.WriteLine("  rect=(" + r2.Left + "," + r2.Top + ") " + (r2.Right-r2.Left) + "x" + (r2.Bottom-r2.Top));
    }

    IntPtr fgBefore = W.GetForegroundWindow();
    var sb1 = new StringBuilder(64);
    W.GetClassNameW(fgBefore, sb1, 64);
    Console.WriteLine("FG before: hwnd=0x" + fgBefore.ToInt64().ToString("X") + " class='" + sb1.ToString() + "'");

    uint WM_AT_TRAYICON = 0x4C8;
    uint WM_RBUTTONUP   = 0x0205;
    bool ok = W.PostMessageW(mw, WM_AT_TRAYICON, IntPtr.Zero, (IntPtr)WM_RBUTTONUP);
    Console.WriteLine("PostMessage -> " + ok);

    // Sample foreground over the next 2 seconds to see if a menu ever takes FG
    for (int i = 0; i < 4; ++i) {
      Thread.Sleep(500);
      IntPtr fg = W.GetForegroundWindow();
      var sb = new StringBuilder(256);
      W.GetClassNameW(fg, sb, 64);
      var sbT = new StringBuilder(256);
      W.GetWindowTextW(fg, sbT, 256);
      Console.WriteLine("+" + (500*(i+1)) + "ms: FG=0x" + fg.ToInt64().ToString("X") + " class='" + sb.ToString() + "' title='" + sbT.ToString() + "'");
    }
  }
}
"@
[P]::Main()