#include "config_store.h"

#include <windows.h>
#include <shlobj.h>

#include <fstream>
#include <sstream>

#include <toml++/toml.h>

namespace autoterminal {

namespace fs = std::filesystem;

namespace {

std::wstring widen(const std::string& s) {
    std::wstring out;
    out.reserve(s.size());
    for (char c : s) out.push_back(static_cast<wchar_t>(static_cast<unsigned char>(c)));
    return out;
}

std::string narrow(const std::wstring& s) {
    std::string out;
    out.reserve(s.size());
    for (wchar_t c : s) out.push_back(static_cast<char>(c & 0xFF));
    return out;
}

fs::path appdata_dir() {
    PWSTR buf = nullptr;
    if (SUCCEEDED(SHGetKnownFolderPath(FOLDERID_RoamingAppData, 0, nullptr, &buf))) {
        fs::path p(buf);
        CoTaskMemFree(buf);
        return p;
    }
    wchar_t env[MAX_PATH]{};
    GetEnvironmentVariableW(L"APPDATA", env, MAX_PATH);
    return fs::path(env);
}

LogLevel parse_level(std::string_view s) {
    if (s == "debug") return LogLevel::Debug;
    if (s == "warn")  return LogLevel::Warn;
    if (s == "error") return LogLevel::Error;
    return LogLevel::Info;
}

std::string_view format_level(LogLevel l) {
    switch (l) {
        case LogLevel::Debug: return "debug";
        case LogLevel::Warn:  return "warn";
        case LogLevel::Error: return "error";
        default: return "info";
    }
}

} // namespace

LayoutMode parse_layout_mode(std::string_view s) {
    if (s == "stack")   return LayoutMode::Stack;
    if (s == "monocle") return LayoutMode::Monocle;
    return LayoutMode::Grid;   // "grid" and anything unrecognized
}

std::string_view layout_mode_name(LayoutMode m) {
    switch (m) {
        case LayoutMode::Stack:   return "stack";
        case LayoutMode::Monocle: return "monocle";
        default:                  return "grid";
    }
}

namespace {

const toml::table* as_table(const toml::node* n) {
    return n ? n->as_table() : nullptr;
}
const toml::array* as_array(const toml::node* n) {
    return n ? n->as_array() : nullptr;
}

} // namespace

fs::path config_dir() {
    fs::path d = appdata_dir() / L"AutoTerminal";
    std::error_code ec;
    fs::create_directories(d, ec);
    return d;
}

fs::path config_path() {
    return config_dir() / L"config.toml";
}

std::optional<Config> load_config(const fs::path& path) {
    Config cfg;

    if (!fs::exists(path)) {
        save_config(path, cfg);
        return cfg;
    }

    toml::table res;
    try {
        res = toml::parse_file(narrow(path.wstring()));
    } catch (const toml::parse_error&) {
        return std::nullopt;
    }

    if (const toml::table* t = as_table(res.get("targets")); t) {
        // Preferred schema: [[targets.process]] array of tables, each
        //   { name = "...", layout = "grid|stack|monocle", monitor = "..." }.
        // We read it into the three lockstep vectors (process_names,
        // process_layouts, process_monitors) so per-process layout mode and
        // monitor binding persist.
        if (const toml::array* a = as_array(t->get("process")); a) {
            cfg.process_names.clear();
            cfg.process_layouts.clear();
            cfg.process_monitors.clear();
            for (const auto& el : *a) {
                const toml::table* pt = as_table(&el);
                if (!pt) continue;
                std::string name = pt->get("name")->value_or(std::string{});
                if (name.empty()) continue;
                cfg.process_names.push_back(widen(name));
                if (const toml::node* lm = pt->get("layout"); lm && lm->is_string()) {
                    cfg.process_layouts.push_back(
                        parse_layout_mode(lm->value_or(std::string{"grid"})));
                } else {
                    cfg.process_layouts.push_back(LayoutMode::Grid);
                }
                if (const toml::node* mon = pt->get("monitor"); mon && mon->is_string()) {
                    cfg.process_monitors.push_back(widen(mon->value_or(std::string{})));
                } else {
                    cfg.process_monitors.push_back(std::wstring{});
                }
            }
            if (cfg.process_names.empty()) cfg.process_names.push_back(L"WindowsTerminal.exe");
        } else if (const toml::array* legacy = as_array(t->get("process_names")); legacy) {
            // Backward compatibility: a bare string array loads as Grid layout
            // on the inherited (global target_monitor) — process_layouts /
            // process_monitors stay empty so every rule inherits the defaults.
            cfg.process_names.clear();
            for (const auto& el : *legacy) {
                std::string s = el.value_or(std::string{});
                if (!s.empty()) cfg.process_names.push_back(widen(s));
            }
            if (cfg.process_names.empty()) cfg.process_names.push_back(L"WindowsTerminal.exe");
        }
        if (const toml::node* v = t->get("target_monitor"); v && v->is_string()) {
            cfg.target_monitor = widen(v->value_or(std::string{}));
        }
    }

    if (const toml::table* t = as_table(res.get("hotkeys")); t) {
        if (const toml::node* v = t->get("tile_now"); v && v->is_string()) {
            if (auto hk = parse_hotkey(widen(v->value_or(std::string{})))) cfg.hotkey_tile = *hk;
        }
        if (const toml::node* v = t->get("toggle_pause"); v && v->is_string()) {
            if (auto hk = parse_hotkey(widen(v->value_or(std::string{})))) cfg.hotkey_toggle_pause = *hk;
        }
        if (const toml::node* v = t->get("tile_specific"); v && v->is_string()) {
            if (auto hk = parse_hotkey(widen(v->value_or(std::string{})))) cfg.hotkey_tile_specific = *hk;
        }
        if (const toml::node* v = t->get("preview"); v && v->is_string()) {
            if (auto hk = parse_hotkey(widen(v->value_or(std::string{})))) cfg.hotkey_preview = *hk;
        }
    }

    if (const toml::table* t = as_table(res.get("ui")); t) {
        if (const toml::node* v = t->get("autostart"); v && v->is_boolean()) {
            cfg.autostart = v->value_or(false);
        }
        if (const toml::node* v = t->get("autostart_delay"); v && v->is_integer()) {
            cfg.autostart_delay = static_cast<int>(v->value_or<int64_t>(0));
            if (cfg.autostart_delay < 0) cfg.autostart_delay = 0;
        }
        if (const toml::node* v = t->get("log_level"); v && v->is_string()) {
            cfg.log_level = parse_level(v->value_or(std::string{"info"}));
        }
    }

    if (const toml::table* t = as_table(res.get("layout")); t) {
        if (const toml::node* v = t->get("padding"); v && v->is_integer()) {
            cfg.padding = static_cast<int>(v->value_or<int64_t>(0));
            if (cfg.padding < 0) cfg.padding = 0;
        }
    }
    return cfg;
}

void save_config(const fs::path& path, const Config& cfg) {
    toml::table root;
    {
        toml::table targets;
        // Emit the [[process]] table array only when some rule actually uses a
        // non-default layout or an explicit per-process monitor — otherwise we
        // keep the legacy bare `process_names` array so default configs stay
        // byte-compatible with older installs (and the round-trip tests).
        bool any_per_process = false;
        for (size_t i = 0; i < cfg.process_names.size(); ++i) {
            if (layout_for(cfg, i) != LayoutMode::Grid) { any_per_process = true; break; }
            if (!monitor_for(cfg, i).empty())           { any_per_process = true; break; }
        }
        if (any_per_process) {
            toml::array procs;
            for (size_t i = 0; i < cfg.process_names.size(); ++i) {
                toml::table pt;
                pt.insert("name", narrow(cfg.process_names[i]));
                pt.insert("layout", std::string(layout_mode_name(layout_for(cfg, i))));
                std::wstring mon = monitor_for(cfg, i);
                pt.insert("monitor", narrow(mon));
                procs.push_back(std::move(pt));
            }
            targets.insert("process", std::move(procs));
        } else {
            toml::array arr;
            for (const auto& n : cfg.process_names) arr.push_back(narrow(n));
            targets.insert("process_names", std::move(arr));
        }
        targets.insert("target_monitor", narrow(cfg.target_monitor));
        root.insert("targets", std::move(targets));
    }
    {
        toml::table hotkeys;
        hotkeys.insert("tile_now",     narrow(format_hotkey(cfg.hotkey_tile)));
        hotkeys.insert("toggle_pause", narrow(format_hotkey(cfg.hotkey_toggle_pause)));
        if (cfg.hotkey_tile_specific.vk != 0) {
            hotkeys.insert("tile_specific", narrow(format_hotkey(cfg.hotkey_tile_specific)));
        }
        if (cfg.hotkey_preview.vk != 0) {
            hotkeys.insert("preview", narrow(format_hotkey(cfg.hotkey_preview)));
        }
        root.insert("hotkeys", std::move(hotkeys));
    }
    {
        toml::table ui;
        ui.insert("autostart", cfg.autostart);
        ui.insert("autostart_delay", static_cast<int64_t>(cfg.autostart_delay));
        ui.insert("log_level", std::string(format_level(cfg.log_level)));
        root.insert("ui", std::move(ui));
    }
    {
        toml::table layout;
        layout.insert("padding", static_cast<int64_t>(cfg.padding));
        root.insert("layout", std::move(layout));
    }

    std::ofstream out(path);
    out << root;
}

std::optional<Hotkey> parse_hotkey(std::wstring_view text) {
    UINT mods = MOD_NOREPEAT;
    UINT vk = 0;
    bool got_key = false;
    size_t i = 0;
    auto consume_token = [&](size_t& i, std::wstring& tok) {
        while (i < text.size() && text[i] == L' ') ++i;
        size_t start = i;
        while (i < text.size() && text[i] != L'+' && text[i] != L' ') ++i;
        tok.assign(text.data() + start, i - start);
    };
    std::wstring tok;
    while (i < text.size()) {
        consume_token(i, tok);
        if (tok.empty()) return std::nullopt;
        std::wstring lower(tok.size(), L'\0');
        for (size_t k = 0; k < tok.size(); ++k) lower[k] = towlower(tok[k]);
        bool is_last = (i >= text.size());
        if (lower == L"ctrl" || lower == L"control") {
            mods |= MOD_CONTROL;
        } else if (lower == L"alt") {
            mods |= MOD_ALT;
        } else if (lower == L"shift") {
            mods |= MOD_SHIFT;
        } else if (lower == L"win" || lower == L"meta" || lower == L"super") {
            mods |= MOD_WIN;
        } else if (!is_last) {
            return std::nullopt;
        } else {
            if (lower.size() == 1) {
                wchar_t c = towupper(lower[0]);
                if (c >= L'A' && c <= L'Z') vk = c;
                else if (c >= L'0' && c <= L'9') vk = c;
                else if (c == L' ') vk = VK_SPACE;
                else if (c == L'\t') vk = VK_TAB;
                else return std::nullopt;
            } else if (lower.size() == 2 && lower[0] == L'f' &&
                       lower[1] >= L'1' && lower[1] <= L'9') {
                vk = VK_F1 + (lower[1] - L'1');
            } else if (lower == L"f10") vk = VK_F10;
            else if (lower == L"f11") vk = VK_F11;
            else if (lower == L"f12") vk = VK_F12;
            else if (lower == L"esc" || lower == L"escape") vk = VK_ESCAPE;
            else if (lower == L"enter" || lower == L"return") vk = VK_RETURN;
            else if (lower == L"left")  vk = VK_LEFT;
            else if (lower == L"right") vk = VK_RIGHT;
            else if (lower == L"up")    vk = VK_UP;
            else if (lower == L"down")  vk = VK_DOWN;
            else if (lower == L"space") vk = VK_SPACE;
            else if (lower == L"tab") vk = VK_TAB;
            else return std::nullopt;
            got_key = true;
        }
        if (i < text.size() && text[i] == L'+') ++i;
    }
    if (!got_key) return std::nullopt;
    return Hotkey{mods, vk};
}

std::wstring format_hotkey(const Hotkey& hk) {
    std::wostringstream oss;
    if (hk.modifiers & MOD_CONTROL) oss << L"Ctrl+";
    if (hk.modifiers & MOD_ALT)     oss << L"Alt+";
    if (hk.modifiers & MOD_SHIFT)   oss << L"Shift+";
    if (hk.modifiers & MOD_WIN)     oss << L"Win+";
    wchar_t key = static_cast<wchar_t>(hk.vk);
    if (key >= L'A' && key <= L'Z') {
        oss << key;
    } else if (key >= L'0' && key <= L'9') {
        oss << key;
    } else {
        switch (key) {
            case VK_F1: case VK_F2: case VK_F3: case VK_F4:
            case VK_F5: case VK_F6: case VK_F7: case VK_F8:
            case VK_F9:
                oss << L'F' << (1 + key - VK_F1); break;
            case VK_F10: oss << L"F10"; break;
            case VK_F11: oss << L"F11"; break;
            case VK_F12: oss << L"F12"; break;
            case VK_SPACE: oss << L"Space"; break;
            case VK_TAB:   oss << L"Tab"; break;
            case VK_ESCAPE:oss << L"Esc"; break;
            case VK_RETURN:oss << L"Enter"; break;
            case VK_LEFT:  oss << L"Left"; break;
            case VK_RIGHT: oss << L"Right"; break;
            case VK_UP:    oss << L"Up"; break;
            case VK_DOWN:  oss << L"Down"; break;
            default: oss << L"Key" << key; break;
        }
    }
    return oss.str();
}

} // namespace autoterminal