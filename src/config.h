#pragma once
#include <cstdint>
#include <string>
#include <windows.h>

namespace glance {

enum class Edge : uint8_t { Auto = 0, Left, Top, Right, Bottom };
enum class Align : uint8_t { Start = 0, Center, End };

struct Config {
    Edge edge = Edge::Auto;
    Align align = Align::Start;
    int thickness = 48;              // DIPs
    uint32_t interval_ms = 1000;     // 250..5000
    int monitor = 0;                 // 0 = primary, else index
    bool show_cpu = true;
    bool show_ram = true;
    bool show_disks = true;
    bool show_net = true;
    bool always_numbers = false;
    bool open_taskmgr = true;
    std::wstring net_iface;          // empty = auto
};

std::wstring config_path();               // glance.ini location
bool load_config(Config& out);
bool save_config(const Config& cfg);

const wchar_t* edge_name(Edge e) noexcept;
const wchar_t* align_name(Align a) noexcept;

} // namespace glance
