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
    void cleanup() noexcept; // free cached GDI

private:
    void paint_horizontal(HDC hdc, RECT rc, const Snapshot& snap, const Config& cfg);
    void paint_vertical(HDC hdc, RECT rc, const Snapshot& snap, const Config& cfg);
    UINT dpi_ = 96;
};

COLORREF lerp_color(COLORREF a, COLORREF b, double t) noexcept;
COLORREF cpu_color(double pct) noexcept;
COLORREF ram_color(double pct) noexcept;

} // namespace glance
