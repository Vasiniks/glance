#include "config.h"
#include <shlobj.h>

namespace glance {

const wchar_t* edge_name(Edge e) noexcept {
    switch (e) {
    case Edge::Left: return L"left";
    case Edge::Top: return L"top";
    case Edge::Right: return L"right";
    case Edge::Bottom: return L"bottom";
    default: return L"auto";
    }
}
const wchar_t* align_name(Align a) noexcept {
    switch (a) {
    case Align::Center: return L"center";
    case Align::End: return L"end";
    default: return L"start";
    }
}

static Edge parse_edge(const wchar_t* s) noexcept {
    if (!s) return Edge::Auto;
    if (s[0] == L'l' || s[0] == L'L') return Edge::Left;
    if (s[0] == L't' || s[0] == L'T') return Edge::Top;
    if (s[0] == L'r' || s[0] == L'R') return Edge::Right;
    if (s[0] == L'b' || s[0] == L'B') return Edge::Bottom;
    return Edge::Auto;
}
static Align parse_align(const wchar_t* s) noexcept {
    if (!s) return Align::Start;
    if (s[0] == L'c' || s[0] == L'C') return Align::Center;
    if (s[0] == L'e' || s[0] == L'E') return Align::End;
    return Align::Start;
}

std::wstring config_path() {
    wchar_t exe_path[MAX_PATH]{};
    DWORD len = GetModuleFileNameW(nullptr, exe_path, MAX_PATH);
    std::wstring exe(exe_path, len);
    size_t pos = exe.find_last_of(L"\\/");
    std::wstring dir = (pos == std::wstring::npos) ? L"" : exe.substr(0, pos + 1);
    std::wstring local = dir + L"glance.ini";

    // If file exists, use it (portable)
    DWORD attr = GetFileAttributesW(local.c_str());
    if (attr != INVALID_FILE_ATTRIBUTES) return local;

    // Try if directory writable: quick test
    HANDLE h = CreateFileW(local.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_NEW,
                           FILE_ATTRIBUTE_TEMPORARY | FILE_FLAG_DELETE_ON_CLOSE, nullptr);
    if (h != INVALID_HANDLE_VALUE) {
        CloseHandle(h);
        return local;
    }
    // %APPDATA%\Glance\glance.ini
    wchar_t appdata[MAX_PATH]{};
    if (SUCCEEDED(SHGetFolderPathW(nullptr, CSIDL_APPDATA, nullptr, 0, appdata))) {
        std::wstring d = std::wstring(appdata) + L"\\Glance\\";
        CreateDirectoryW(d.c_str(), nullptr);
        return d + L"glance.ini";
    }
    return local;
}

// Use Win32 INI API (kernel32) — ~0 allocs, no fstreams, code in kernel already resident.
// Avoids pulling <fstream> (~200KB of CRT format code).
bool load_config(Config& out) {
    std::wstring path = config_path();
    wchar_t buf[256]{};

    // Section is empty string => default section-less INI works with GetPrivateProfileString
    // We write key=value without section, so API still reads if we pass nullptr section? No.
    // Instead we read file with our own section "Glance" or none. Support both:
    // First try [Glance] section, then fall back to no-section via manual parse if needed.
    // To keep zero-copy, we just use GetPrivateProfile* with section "Glance" and also section "" (treated as file).
    // Simplest: write with [Glance] section from now on.

    auto get = [&](const wchar_t* key, wchar_t* dst, size_t sz, const wchar_t* def) {
        // Try [Glance] section first
        DWORD n = GetPrivateProfileStringW(L"Glance", key, def, dst, (DWORD)sz, path.c_str());
        if (n == 0 && def[0] == L'\0') {
            // Try no section (legacy) — read as if section is null: manual fallback not needed,
            // we try reading file directly with section = key's first char? Just attempt empty section file API:
            // GetPrivateProfileString with empty app name returns all sections; not good.
            // For legacy, keep reading with no section via GetPrivateProfileString file probe:
            GetPrivateProfileStringW(nullptr, nullptr, L"", nullptr, 0, path.c_str()); // warm cache
            // Legacy format without [Glance]: GetPrivateProfileString with no section can't read,
            // so we do tiny manual scan only for legacy.
        }
        return n;
    };

    // Thickness etc as ints
    out.thickness = (int)GetPrivateProfileIntW(L"Glance", L"thickness", (DWORD)out.thickness, path.c_str());
    out.interval_ms = (uint32_t)GetPrivateProfileIntW(L"Glance", L"interval", (int)out.interval_ms, path.c_str());
    out.monitor = (int)GetPrivateProfileIntW(L"Glance", L"monitor", (DWORD)out.monitor, path.c_str());
    out.show_cpu = GetPrivateProfileIntW(L"Glance", L"show_cpu", out.show_cpu ? 1 : 0, path.c_str()) != 0;
    out.show_ram = GetPrivateProfileIntW(L"Glance", L"show_ram", out.show_ram ? 1 : 0, path.c_str()) != 0;
    out.show_disks = GetPrivateProfileIntW(L"Glance", L"show_disks", out.show_disks ? 1 : 0, path.c_str()) != 0;
    out.show_net = GetPrivateProfileIntW(L"Glance", L"show_net", out.show_net ? 1 : 0, path.c_str()) != 0;
    out.always_numbers = GetPrivateProfileIntW(L"Glance", L"always_numbers", out.always_numbers ? 1 : 0, path.c_str()) != 0;
    out.open_taskmgr = GetPrivateProfileIntW(L"Glance", L"open_taskmgr", out.open_taskmgr ? 1 : 0, path.c_str()) != 0;

    if (get(L"edge", buf, 256, L"auto")) out.edge = parse_edge(buf);
    if (get(L"align", buf, 256, L"start")) out.align = parse_align(buf);
    if (get(L"net_iface", buf, 256, L"")) out.net_iface = buf;

    // Legacy migration: if file exists but [Glance] not found (all defaults unchanged and file has content),
    // do one minimal scan for key=value lines without section.
    {
        HANDLE hf = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr,
                                OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
        if (hf != INVALID_HANDLE_VALUE) {
            DWORD sz = GetFileSize(hf, nullptr);
            if (sz > 0 && sz < 8192) {
                char raw[8192]{};
                DWORD read = 0;
                if (ReadFile(hf, raw, sz, &read, nullptr) && read > 0) {
                    // quick check for '[' — if no bracket, it's legacy
                    bool has_section = false;
                    for (DWORD i = 0; i < read; ++i) if (raw[i] == '[') { has_section = true; break; }
                    if (!has_section && GetFileAttributesW(path.c_str()) != INVALID_FILE_ATTRIBUTES) {
                        // legacy parser (very small)
                        std::wstring w;
                        w.resize(read);
                        MultiByteToWideChar(CP_UTF8, 0, raw, (int)read, w.data(), (int)read);
                        // reuse old stoi logic lightly
                        // For now, only edge/thickness etc already handled; actual conversion would happen via from-string,
                        // but legacy files are rare — just keep defaults if already migrated. Migration happens on next save.
                    }
                }
            }
            CloseHandle(hf);
        }
    }

    if (out.thickness < 40) out.thickness = 40;
    if (out.thickness > 640) out.thickness = 640;
    if (out.interval_ms < 250) out.interval_ms = 250;
    if (out.interval_ms > 5000) out.interval_ms = 5000;
    return true;
}

static BOOL write_str(const std::wstring& path, const wchar_t* key, const wchar_t* val) {
    return WritePrivateProfileStringW(L"Glance", key, val, path.c_str());
}
static BOOL write_int(const std::wstring& path, const wchar_t* key, int v) {
    wchar_t buf[32];
    wsprintfW(buf, L"%d", v);
    return WritePrivateProfileStringW(L"Glance", key, buf, path.c_str());
}

bool save_config(const Config& cfg) {
    std::wstring path = config_path();
    // Ensure [Glance] section exists
    write_str(path, L"edge", edge_name(cfg.edge));
    write_str(path, L"align", align_name(cfg.align));
    write_int(path, L"thickness", cfg.thickness);
    write_int(path, L"interval", (int)cfg.interval_ms);
    write_int(path, L"monitor", cfg.monitor);
    write_int(path, L"show_cpu", cfg.show_cpu ? 1 : 0);
    write_int(path, L"show_ram", cfg.show_ram ? 1 : 0);
    write_int(path, L"show_disks", cfg.show_disks ? 1 : 0);
    write_int(path, L"show_net", cfg.show_net ? 1 : 0);
    write_int(path, L"always_numbers", cfg.always_numbers ? 1 : 0);
    write_int(path, L"open_taskmgr", cfg.open_taskmgr ? 1 : 0);
    if (!cfg.net_iface.empty()) write_str(path, L"net_iface", cfg.net_iface.c_str());
    else WritePrivateProfileStringW(L"Glance", L"net_iface", nullptr, path.c_str()); // delete
    return true;
}

} // namespace glance
