一台 Windows 上，把所有打开的终端窗口按进程名（Windows Terminal / cmd / PowerShell / ConEmu / mintty...）找出来，按行×列平铺到指定显示器上，形成 3×3 之类的网格看板。

我之前给的思路是对的：

1.
EnumWindows 按进程名筛终端
2.
EnumDisplayMonitors 选目标显示器
3.
SetWindowPos 平铺