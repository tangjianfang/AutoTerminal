Add-Type @"
using System;
using System.Runtime.InteropServices;
using System.Text;
using System.Threading;

public class W {
  [DllImport("user32.dll", CharSet=CharSet.Unicode, SetLastError=true)]
  public static extern IntPtr FindWindowW(string cls, string title);

  [DllImport("user32.dll", SetLastError=true)]
  public static extern bool ShowWindow(IntPtr h, int cmd);

  [DllImport("user32.dll", SetLastError=true)]
  [return: MarshalAs(UnmanagedType.Bool)]
  public static extern bool IsWindowVisible(IntPtr h);

  [DllImport("user32.dll", SetLastError=true)]
  [return: MarshalAs(UnmanagedType.Bool)]
  public static extern bool MoveWindow(IntPtr h, int x, int y, int w, int hh, bool repaint);

  [DllImport("user32.dll", CharSet=CharSet.Unicode)]
  public static extern int GetWindowTextW(IntPtr h, StringBuilder sb, int n);

  [DllImport("user32.dll")]
  public static extern int GetWindowTextLengthW(IntPtr h);

  [DllImport("user32.dll")]
  public static extern IntPtr GetDlgItem(IntPtr h, int id);
}

public class P {
  public static void Main() {
    IntPtr sw = W.FindWindowW("AutoTerminal.SettingsWindow.v1", null);
    Console.WriteLine("SettingsWindow hwnd=0x" + sw.ToInt64().ToString("X") +
                      " visible=" + W.IsWindowVisible(sw));

    if (sw == IntPtr.Zero) { Console.WriteLine("FAIL: settings window not created"); return; }

    // Move it on-screen and show it.
    W.MoveWindow(sw, 100, 100, 700, 460, true);
    W.ShowWindow(sw, 5); // SW_SHOW
    Thread.Sleep(500);

    Console.WriteLine("After ShowWindow: visible=" + W.IsWindowVisible(sw));
    int len = W.GetWindowTextLengthW(sw);
    var sb = new StringBuilder(len+1);
    W.GetWindowTextW(sw, sb, sb.Capacity);
    Console.WriteLine("  title='" + sb.ToString() + "'");

    // Count child windows (each control in our dialog should be a child window).
    int count = CountChildren(sw);
    Console.WriteLine("  child control count=" + count);
  }

  static int CountChildren(IntPtr parent) {
    return EnumChildCount(parent);
  }

  [DllImport("user32.dll", SetLastError=true)]
  public static extern IntPtr GetWindow(IntPtr h, uint cmd);
  // GW_CHILD = 5, GW_HWNDNEXT = 2
  const uint GW_CHILD = 5;
  const uint GW_HWNDNEXT = 2;

  static int EnumChildCount(IntPtr parent) {
    IntPtr c = GetWindow(parent, GW_CHILD);
    int n = 0;
    while (c != IntPtr.Zero) {
      n++;
      c = GetWindow(c, GW_HWNDNEXT);
    }
    return n;
  }
}
"@
[P]::Main()
