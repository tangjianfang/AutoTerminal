#include "tray_menu.h"

#include "ui_bridge.h"   // TrayCommand enum values

#include <nfui/Menu.hpp>
#include <nfui/Theme.hpp>

namespace autoterminal {

HMENU build_tray_menu(const Config& cfg) {
    // nfui::Menu provides a fluent builder + applies the palette to MENUINFO.
    // We immediately release() the popup so the caller owns the HMENU and
    // can DestroyMenu it after TrackPopupMenu returns.
    nfui::Menu menu(nfui::theme_palette(nfui::ThemeMode::system));
    auto popup = menu.make_popup();
    menu.builder(popup)
        .item      (L"&Tile now",             TrayCmdTileNow)
        .item      (L"&Pause auto-tile",      TrayCmdTogglePause)
        .separator ()
        .check_item(L"Start with &Windows",  TrayCmdToggleAutostart, cfg.autostart)
        .item      (L"Open &config file...",  TrayCmdOpenConfig)
        .item      (L"&Settings...",          TrayCmdSettings)
        .item      (L"&Reload config",        TrayCmdReload)
        .separator ()
        .item      (L"&About AutoTerminal",   TrayCmdAbout)
        .item      (L"E&xit AutoTerminal",    TrayCmdExit);
    menu.apply_palette(popup);
    return popup.release();
}

} // namespace autoterminal