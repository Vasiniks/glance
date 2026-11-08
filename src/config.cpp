#include "config.h"
#include <shlobj.h>
#include <string>
#include <fstream>
#include <sstream>

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

static Edge parse_edge(std::wstring s) {
    if (s == L"left") return Edge::Left;
    if (s == L"top") return Edge::Top;
    if (s == L"right") return Edge::Right;
    if (s == L"bottom") return Edge::Bottom;
    return Edge::Auto;
}
static Align parse_align(std::wstring s) {
    if (s == L"center") return Align::Center;
    if (s == L"end") return Align::End;
    return Align::Start;
}

std::wstring config_path() {
    wchar_t exe_path[MAX_PATH]{};
    GetModuleFileNameW(nullptr, exe_path, MAX_PATH);
    std::wstring exe(exe_path);
    auto pos = exe.find_last_of(L"\\/");
    std::wstring dir = (pos == std::wstring::npos) ? L"" : exe.substr(0, pos + 1);
    std::wstring local = dir + L"glance.ini";
    DWORD attr = GetFileAttributesW(local.c_str());
    if (attr != INVALID_FILE_ATTRIBUTES) return local;

    // Try write test to local dir
    HANDLE h = CreateFileW(local.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS,
                           FILE_ATTRIBUTE_TEMPORARY | FILE_FLAG_DELETE_ON_CLOSE, nullptr);
    if (h != INVALID_HANDLE_VALUE) {
        CloseHandle(h);
        DeleteFileW(local.c_str());
        return local; // portable mode possible
    }
    // fallback to appdata
    wchar_t appdata[MAX_PATH]{};
    if (SUCCEEDED(SHGetFolderPathW(nullptr, CSIDL_APPDATA, nullptr, 0, appdata))) {
        std::wstring d = std::wstring(appdata) + L"\\Glance\\";
        CreateDirectoryW(d.c_str(), nullptr);
        return d + L"glance.ini";
    }
    return local;
}

static std::wstring trim(std::wstring s) {
    size_t a = 0;
    while (a < s.size() && iswspace(s[a])) ++a;
    size_t b = s.size();
    while (b > a && iswspace(s[b - 1])) --b;
    return s.substr(a, b - a);
}

bool load_config(Config& out) {
    auto path = config_path();
    std::wifstream f(path);
    if (!f) return false;
    std::wstring line;
    while (std::getline(f, line)) {
        line = trim(line);
        if (line.empty() || line[0] == L'#' || line[0] == L';') continue;
        auto eq = line.find(L'=');
        if (eq == std::wstring::npos) continue;
        auto key = trim(line.substr(0, eq));
        auto val = trim(line.substr(eq + 1));
        if (key == L"edge") out.edge = parse_edge(val);
        else if (key == L"align") out.align = parse_align(val);
        else if (key == L"thickness") { try { out.thickness = std::stoi(val); } catch (...) {} }
        else if (key == L"interval") { try { out.interval_ms = (uint32_t)std::stoul(val); } catch (...) {} }
        else if (key == L"monitor") { try { out.monitor = std::stoi(val); } catch (...) {} }
        else if (key == L"show_cpu") out.show_cpu = (val == L"1" || val == L"true");
        else if (key == L"show_ram") out.show_ram = (val == L"1" || val == L"true");
        else if (key == L"show_disks") out.show_disks = (val == L"1" || val == L"true");
        else if (key == L"show_net") out.show_net = (val == L"1" || val == L"true");
        else if (key == L"always_numbers") out.always_numbers = (val == L"1" || val == L"true");
        else if (key == L"open_taskmgr") out.open_taskmgr = (val == L"1" || val == L"true");
        else if (key == L"net_iface") out.net_iface = val;
    }
    // clamp
    if (out.thickness < 40) out.thickness = 40;
    if (out.thickness > 640) out.thickness = 640;
    if (out.interval_ms < 250) out.interval_ms = 250;
    if (out.interval_ms > 5000) out.interval_ms = 5000;
    return true;
}

bool save_config(const Config& cfg) {
    auto path = config_path();
    std::wofstream f(path, std::ios::trunc);
    if (!f) return false;
    f << L"# Glance config — minimal INI\n";
    f << L"edge=" << edge_name(cfg.edge) << L"\n";
    f << L"align=" << align_name(cfg.align) << L"\n";
    f << L"thickness=" << cfg.thickness << L"\n";
    f << L"interval=" << cfg.interval_ms << L"\n";
    f << L"monitor=" << cfg.monitor << L"\n";
    f << L"show_cpu=" << (cfg.show_cpu ? 1 : 0) << L"\n";
    f << L"show_ram=" << (cfg.show_ram ? 1 : 0) << L"\n";
    f << L"show_disks=" << (cfg.show_disks ? 1 : 0) << L"\n";
    f << L"show_net=" << (cfg.show_net ? 1 : 0) << L"\n";
    f << L"always_numbers=" << (cfg.always_numbers ? 1 : 0) << L"\n";
    f << L"open_taskmgr=" << (cfg.open_taskmgr ? 1 : 0) << L"\n";
    if (!cfg.net_iface.empty())
        f << L"net_iface=" << cfg.net_iface << L"\n";
    return true;
}

} // namespace glance
