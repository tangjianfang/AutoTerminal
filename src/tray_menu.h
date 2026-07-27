#pragma once

#include "config_store.h"

#include <windows.h>

namespace autoterminal {

// Build the tray-icon right-click popup menu from the current Config.
// Returns an owned HMENU that the caller is responsible for DestroyMenu'ing.
// Uses nfui::Menu under the hood to install a themed MENUINFO background
// brush matching the application palette.
HMENU build_tray_menu(const Config& cfg);

} // namespace autoterminal