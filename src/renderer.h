#pragma once
#include "telemetry.h"
#include "config.h"
#include <windows.h>

namespace glance {

class Renderer {
public:
    Renderer() = default;
    ~Renderer() { cleanup(); }

    void paint(HDC hdc, RECT rc, const Snapshot& snap, const Config& cfg, bool vertical);
    void set_dpi(UINT dpi) noexcept { dpi_ = dpi; }
    void cleanup() noexcept;

    // For tooltip tracking
    struct Hit {
        enum Type { None, Cpu, Ram, Disk, Net } type = None;
        int cpu_index = -1;
        int disk_index = -1;
    };
    Hit hit_test(int x, int y, RECT rc) const noexcept;

private:
    void paint_horizontal(HDC hdc, RECT rc, const Snapshot& snap, const Config& cfg);
    void paint_vertical(HDC hdc, RECT rc, const Snapshot& snap, const Config& cfg);
    UINT dpi_ = 96;
    // last layout for hit test
    mutable struct Layout {
        RECT cpu{0,0,0,0};
        RECT ram{0,0,0,0};
        RECT disks{0,0,0,0};
        RECT net{0,0,0,0};
        int cpu_count = 0;
        int disk_count = 0;
        RECT bar{0,0,0,0};
    } last_{};
};

COLORREF lerp_color(COLORREF a, COLORREF b, double t) noexcept;
COLORREF cpu_color(double pct) noexcept;
COLORREF ram_color(double pct) noexcept;

} // namespace glance
