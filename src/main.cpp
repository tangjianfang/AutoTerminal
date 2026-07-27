// AutoTerminal — main entry point.

#include "about_dialog.h"
#include "config_store.h"
#include "event_source.h"
#include "logger.h"
#include "monitor_index.h"
#include "settings_dialog.h"
#include "tile_engine.h"
#include "ui_bridge.h"
#include "window_manager.h"

#include <nfui/Application.hpp>
#include <nfui/Dpi.hpp>

#include <windows.h>

#include <filesystem>
#include <mutex>

namespace {

std::mutex g_state_mutex;
autoterminal::Config g_config;
autoterminal::EventSource* g_events = nullptr;
autoterminal::UIBridge* g_ui = nullptr;
HWND g_settings_hwnd = nullptr;
bool g_silent = false;

// Returns true if `needle` appears as a whole flag in `cmdline`
// (e.g. "--silent" or "/silent").
bool has_flag(LPCWSTR cmdline, LPCWSTR flag) {
    if (!cmdline) return false;
    size_t flen = wcslen(flag);
    LPCWSTR p = cmdline;
    while (*p) {
        while (*p == L' ' || *p == L'\t') ++p;
        if (_wcsnicmp(p, flag, flen) == 0) {
            wchar_t after = p[flen];
            if (after == L'\0' || after == L' ' || after == L'\t') return true;
        }
        while (*p && *p != L' ') ++p;
    }
    return false;
}

bool perform_tile() {
    std::lock_guard<std::mutex> lock(g_state_mutex);
    auto monitors = autoterminal::enumerate_monitors();
    const auto* target = autoterminal::resolve_monitor(monitors, g_config.target_monitor);
    if (!target) {
        AT_LOG_WARN("Target monitor not found (%s) — falling back to primary",
                    autoterminal::monitor_log_label(g_config.target_monitor).c_str());
        target = autoterminal::resolve_monitor(monitors, L"");
    }
    if (!target) {
        AT_LOG_WARN("No monitors at all — nothing to tile");
        return false;
    }
    auto windows = autoterminal::collect_terminal_windows(g_config.process_names);
    if (windows.empty()) {
        AT_LOG_DEBUG("No terminal windows match configured process names");
        return false;
    }
    auto layout = autoterminal::compute_layout(target->rect,
                                               static_cast<int>(windows.size()),
                                               g_config.padding);
    int placed = autoterminal::apply_layout(windows, layout);
    AT_LOG_INFO("Tiled %d/%zu windows into %dx%d on '%s'",
                placed, windows.size(), layout.rows, layout.cols,
                autoterminal::monitor_log_label(target->friendly_name).c_str());
    return placed > 0;
}

void on_command(autoterminal::EventSource::Command cmd) {
    switch (cmd) {
        case autoterminal::EventSource::Command::TileNow:
            perform_tile();
            break;
        case autoterminal::EventSource::Command::TogglePause:
            if (g_events) {
                g_events->set_paused(!g_events->paused());
                if (!g_events->paused()) perform_tile();
            }
            break;
        case autoterminal::EventSource::Command::Reload:
            if (auto cfg = autoterminal::load_config(autoterminal::config_path())) {
                std::lock_guard<std::mutex> lock(g_state_mutex);
                g_config = *cfg;
                AT_LOG_INFO("Config reloaded");
            } else {
                AT_LOG_WARN("Config reload failed — keeping previous config");
            }
            break;
        case autoterminal::EventSource::Command::Exit:
            if (g_events) g_events->request_exit();
            break;
    }
}

// Force a window to the foreground, working around Win10/11's foreground-lock:
//   1. attach our thread to the current FG thread (so SetForegroundWindow is
//      allowed to take FG even if another process is currently FG);
//   2. SetForegroundWindow;
//   3. briefly take TOPMOST then drop back to NOTOPMOST — this Win32 trick
//      forces a z-order promotion that the FG-lock normally blocks.
void force_foreground(HWND target) {
    if (!target) return;

    ShowWindow(target, SW_SHOWNORMAL);

    HWND fg = GetForegroundWindow();
    DWORD fg_tid = fg ? GetWindowThreadProcessId(fg, nullptr) : 0;
    DWORD our_tid = GetCurrentThreadId();
    bool attached = false;
    if (fg_tid && fg_tid != our_tid) {
        AttachThreadInput(fg_tid, our_tid, TRUE);
        attached = true;
    }
    SetForegroundWindow(target);
    if (attached) {
        AttachThreadInput(fg_tid, our_tid, FALSE);
    }
    BringWindowToTop(target);
    // Promote to TOPMOST and back — bypasses FG lock for z-order, then drops
    // back into normal position so it doesn't stay always-on-top.
    SetWindowPos(target, HWND_TOPMOST,    0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
    SetWindowPos(target, HWND_NOTOPMOST,  0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
}

bool acquire_singleton_mutex(HANDLE& out) {
    out = CreateMutexW(nullptr, TRUE, autoterminal::kSingletonMutexName);
    if (!out) return false;
    if (GetLastError() == ERROR_ALREADY_EXISTS) {
        CloseHandle(out);
        out = nullptr;
        return false;
    }
    return true;
}

bool ping_existing_instance() {
    HWND existing = FindWindowW(autoterminal::kMessageWindowClass, nullptr);
    if (!existing) return false;
    PostMessageW(existing, autoterminal::WM_AT_RELOAD, 0, 0);
    return true;
}

} // namespace

int WINAPI wWinMain(HINSTANCE hinst, HINSTANCE, LPWSTR cmdline, int show_cmd) {
    using namespace autoterminal;

    // --silent: stay in the tray, don't pop the settings window.
    //   Used by the autostart entry so logon doesn't open a window.
    // /settings: same as default for manual double-click — show settings.
    g_silent = has_flag(cmdline, L"--silent") || has_flag(cmdline, L"/silent");

    // Initialize logging first so NFUI-init failures can be recorded.
    auto log_path = config_dir() / L"autoterminal.log";
    init_logger(log_path.wstring(), LogLevel::Info);

    // NativeFrameUI prerequisite initialization: PerMonitorV2 DPI awareness +
    // common-control classes. Must run before any HWND (settings window,
    // event-source message window, About / settings child controls) is
    // created. Safe to call once here in the daemon's main thread; the
    // static helpers are idempotent. A failure here is unrecoverable — we
    // would be creating themed controls that can't render correctly, so log
    // the reason and exit with a non-zero code rather than limping along.
    if (!nfui::Application::initialize_process_dpi()) {
        AT_LOG_ERROR("NFUI initialize_process_dpi failed — aborting startup");
        MessageBoxW(nullptr,
                    L"AutoTerminal: failed to enable per-monitor DPI awareness.",
                    L"AutoTerminal", MB_ICONERROR | MB_OK);
        return 2;
    }
    if (!nfui::Application::initialize_common_controls()) {
        AT_LOG_ERROR("NFUI initialize_common_controls failed — aborting startup");
        MessageBoxW(nullptr,
                    L"AutoTerminal: failed to initialize common controls.",
                    L"AutoTerminal", MB_ICONERROR | MB_OK);
        return 2;
    }

    AT_LOG_INFO("==== AutoTerminal starting (silent=%s) ====",
                g_silent ? "true" : "false");

    HANDLE mutex = nullptr;
    if (!acquire_singleton_mutex(mutex)) {
        AT_LOG_INFO("Another instance is already running — pinging it and exiting");
        ping_existing_instance();
        return 0;
    }

    auto cfg_path = config_path();
    bool first_run = !std::filesystem::exists(cfg_path);
    auto loaded = load_config(cfg_path);
    if (!loaded) {
        MessageBoxW(nullptr, L"AutoTerminal: failed to parse config.toml.\n"
                              L"Fix or delete the file and relaunch.",
                    L"AutoTerminal", MB_ICONERROR | MB_OK);
        return 1;
    }
    g_config = *loaded;
    set_log_level(g_config.log_level);

    EventSource events;
    events.set_tile_callback(perform_tile);
    events.set_reload_callback([]() {
        if (auto cfg = load_config(config_path())) {
            std::lock_guard<std::mutex> lock(g_state_mutex);
            g_config = *cfg;
            set_log_level(g_config.log_level);
            AT_LOG_INFO("Config reloaded from reload callback");
        }
    });
    events.set_pause_callback([](bool paused) {
        AT_LOG_INFO("Pause toggled: %s", paused ? "ON" : "OFF");
    });
    events.set_command_callback(on_command);
    if (!events.start()) {
        MessageBoxW(nullptr, L"AutoTerminal: failed to create message window.",
                    L"AutoTerminal", MB_ICONERROR | MB_OK);
        ReleaseMutex(mutex);
        CloseHandle(mutex);
        return 1;
    }
    g_events = &events;

    // Autostart delayed first-tile: when launched via --silent (the autostart
    // entry), suppress the WinEvent-driven auto-tile for N seconds so we
    // don't tile an empty/partial window set before the user's terminals open
    // at logon. After the delay one catch-up tile fires; explicit Tile-now
    // (hotkey/tray) still works during the delay.
    if (g_silent && g_config.autostart_delay > 0) {
        events.arm_startup_delay(g_config.autostart_delay);
    }

    UIBridge ui;
    ui.set_command_callback([&](TrayCommand c) {
        switch (c) {
            case TrayCmdTileNow:        events.post_tile_request(); break;
            case TrayCmdTogglePause:    events.toggle_pause();     break;
            case TrayCmdReload:         events.request_reload();   break;
            case TrayCmdExit:           events.request_exit();     break;
            case TrayCmdOpenConfig:     UIBridge::open_config_file(); break;
            case TrayCmdSettings:
                autoterminal::show_settings_window(g_settings_hwnd, true);
                force_foreground(g_settings_hwnd);
                break;
            case TrayCmdToggleAutostart: {
                bool new_state = !g_config.autostart;
                if (UIBridge::set_autostart(new_state)) {
                    std::lock_guard<std::mutex> lock(g_state_mutex);
                    g_config.autostart = new_state;
                    save_config(config_path(), g_config);
                    AT_LOG_INFO("Autostart = %s", new_state ? "ON" : "OFF");
                } else {
                    AT_LOG_WARN("Failed to toggle autostart");
                }
                break;
            }
            case TrayCmdAbout:
                show_about_dialog(events.hinstance(), g_settings_hwnd);
                force_foreground(g_settings_hwnd);
                break;
        }
    });
    g_ui = &ui;
    if (!ui.init(events.hwnd())) {
        AT_LOG_WARN("Failed to install tray icon — continuing without UI");
    }
    ui.apply_config(g_config);

    g_settings_hwnd = autoterminal::create_settings_window(
        events.hinstance(), g_config,
        autoterminal::SettingsCallbacks{
            .on_apply = [&](const autoterminal::Config& new_cfg) {
                std::lock_guard<std::mutex> lock(g_state_mutex);
                g_config = new_cfg;
                save_config(config_path(), g_config);
                if (g_ui) g_ui->apply_config(g_config);
                // Sync autostart registry with the checkbox state (idempotent).
                autoterminal::UIBridge::set_autostart(g_config.autostart);
                set_log_level(g_config.log_level);
                AT_LOG_INFO("Settings applied via dialog");
            },
            .on_exit = [&]() {
                AT_LOG_INFO("Exit requested via Settings dialog");
                if (g_events) g_events->request_exit();
            }
        });

    // Manual double-click → user wants to see UI immediately.
    // First run → show settings as a setup wizard.
    // --silent → tray-only (used by the autostart entry at logon).
    if (!g_silent || first_run) {
        // Position the dialog deterministically on the primary work area
        // (CW_USEDEFAULT can land it on an off-screen monitor). Size in DPI-
        // aware physical pixels so the first ShowWindow matches the dialog's
        // own layout grid at the host's current DPI.
        RECT primary{};
        SystemParametersInfoW(SPI_GETWORKAREA, 0, &primary, 0);
        auto sz = autoterminal::default_settings_window_size(
            nfui::dpi_of(g_settings_hwnd));
        SetWindowPos(g_settings_hwnd, nullptr,
                      primary.left + 80, primary.top + 80,
                      sz.cx, sz.cy,
                      SWP_NOZORDER | SWP_NOACTIVATE);
        autoterminal::show_settings_window(g_settings_hwnd, true);
        force_foreground(g_settings_hwnd);
        AT_LOG_INFO("Settings window shown (manual launch or first-run)");
    }

    MSG msg;
    while (GetMessageW(&msg, nullptr, 0, 0) > 0) {
        if (g_settings_hwnd && IsDialogMessageW(g_settings_hwnd, &msg)) {
            continue;
        }
        // Note: WM_AT_TRAYICON is delivered to the popup helper's HWND (see
        // UIBridge::init). Its wnd_proc (popup_helper_proc) handles the message
        // directly via DispatchMessageW, forwarding to UIBridge::on_tray_message
        // via GWLP_USERDATA. No special-case dispatch is needed here.
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }

    AT_LOG_INFO("==== AutoTerminal exiting ====");
    ui.shutdown();
    events.stop();
    if (mutex) {
        ReleaseMutex(mutex);
        CloseHandle(mutex);
    }
    return 0;
}