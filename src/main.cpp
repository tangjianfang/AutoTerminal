// AutoTerminal — main entry point.

#include "config_store.h"
#include "event_source.h"
#include "logger.h"
#include "monitor_index.h"
#include "tile_engine.h"
#include "ui_bridge.h"
#include "window_manager.h"

#include <windows.h>

#include <mutex>

namespace {

std::mutex g_state_mutex;
autoterminal::Config g_config;
autoterminal::EventSource* g_events = nullptr;

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

int WINAPI wWinMain(HINSTANCE, HINSTANCE, LPWSTR, int) {
    using namespace autoterminal;

    auto log_path = config_dir() / L"autoterminal.log";
    init_logger(log_path.wstring(), LogLevel::Info);

    AT_LOG_INFO("==== AutoTerminal starting ====");

    HANDLE mutex = nullptr;
    if (!acquire_singleton_mutex(mutex)) {
        AT_LOG_INFO("Another instance is already running — pinging it and exiting");
        ping_existing_instance();
        return 0;
    }

    auto cfg_path = config_path();
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

    UIBridge ui;
    ui.set_command_callback([&](TrayCommand c) {
        switch (c) {
            case TrayCmdTileNow:        events.post_tile_request(); break;
            case TrayCmdTogglePause:    events.toggle_pause();     break;
            case TrayCmdReload:         events.request_reload();   break;
            case TrayCmdExit:           events.request_exit();     break;
            case TrayCmdOpenConfig:     UIBridge::open_config_file(); break;
            case TrayCmdToggleAutostart: {
                bool new_state = !g_config.autostart;
                if (UIBridge::set_autostart(new_state)) {
                    std::lock_guard<std::mutex> lock(g_state_mutex);
                    g_config.autostart = new_state;
                    save_config(config_path(), g_config);
                }
                break;
            }
            case TrayCmdAbout:
                MessageBoxW(nullptr,
                    L"AutoTerminal\n"
                    L"Tiling terminal windows onto a chosen display.\n"
                    L"https://github.com/yourname/AutoTerminal",
                    L"About AutoTerminal", MB_ICONINFORMATION | MB_OK);
                break;
        }
    });
    if (!ui.init(events.hwnd())) {
        AT_LOG_WARN("Failed to install tray icon — continuing without UI");
    }
    ui.apply_config(g_config);

    MSG msg;
    while (GetMessageW(&msg, nullptr, 0, 0) > 0) {
        if (msg.message == WM_AT_TRAYICON) {
            ui.on_tray_message(msg.hwnd, msg.lParam);
            continue;
        }
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