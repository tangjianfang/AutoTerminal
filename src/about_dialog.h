#pragma once

#include <windows.h>

namespace autoterminal {

// Open a themed About window. Modeless; auto-destroys when the user clicks OK
// or closes the window. The window is parented to the supplied HWND if
// non-null so it stays on top of the settings dialog.
void show_about_dialog(HINSTANCE hinst, HWND parent);

} // namespace autoterminal