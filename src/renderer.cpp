#include "renderer.h"
#include <cmath>
#include <algorithm>

namespace glance {

COLORREF lerp_color(COLORREF a, COLORREF b, double t) noexcept {
    t = std::clamp(t, 0.0, 1.0);
    int ar = GetRValue(a), ag = GetGValue(a), ab = GetBValue(a);
    int br = GetRValue(b), bg = GetGValue(b), bb = GetBValue(b);
    return RGB(int(ar + (br - ar) * t), int(ag + (bg - ag) * t), int(ab + (bb - ab) * t));
}
COLORREF cpu_color(double pct) noexcept {
    if (pct < 50) return lerp_color(RGB(80, 200, 80), RGB(240, 200, 50), pct / 50.0);
    return lerp_color(RGB(240, 200, 50), RGB(230, 50, 50), (pct - 50) / 50.0);
}
COLORREF ram_color(double pct) noexcept { return cpu_color(pct); }

struct GdiCache {
    HFONT hSmall = nullptr;
    HFONT hSmallBold = nullptr;
    int pxSmall = 0;
    HBRUSH hbr = nullptr;
    COLORREF brColor = CLR_INVALID;
    void clear() noexcept {
        if (hSmall) { DeleteObject(hSmall); hSmall = nullptr; }
        if (hSmallBold) { DeleteObject(hSmallBold); hSmallBold = nullptr; }
        if (hbr) { DeleteObject(hbr); hbr = nullptr; }
        brColor = CLR_INVALID;
    }
    HFONT small(int px) {
        if (hSmall && pxSmall == px) return hSmall;
        if (hSmall) DeleteObject(hSmall);
        hSmall = CreateFontW(-px, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
                             DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                             CLEARTYPE_QUALITY, DEFAULT_PITCH, L"Segoe UI");
        pxSmall = px; return hSmall;
    }
    HBRUSH brush(COLORREF c) {
        if (hbr && brColor == c) return hbr;
        if (hbr) DeleteObject(hbr);
        hbr = CreateSolidBrush(c); brColor = c; return hbr;
    }
};
static thread_local GdiCache t_cache;

void Renderer::cleanup() noexcept { t_cache.clear(); }

static inline void fill_fast(HDC hdc, int x, int y, int w, int h, COLORREF c) {
    if (w <= 0 || h <= 0) return;
    RECT r{ x, y, x + w, y + h };
    FillRect(hdc, &r, t_cache.brush(c));
}

static void draw_cpu_tiles(HDC hdc, int x, int y, int w, int h, const CpuSample& cpu, bool vertical) {
    if (cpu.count == 0) return;
    size_t n = cpu.count;
    int cols = (int)std::ceil(std::sqrt((double)n));
    if (cols < 1) cols = 1;
    int rows = (int)std::ceil((double)n / cols);
    if (vertical) std::swap(cols, rows);
    const int gap = 2;
    int cell_w = (w - gap * (cols - 1)) / cols;
    int cell_h = (h - gap * (rows - 1)) / rows;
    if (cell_w < 2) cell_w = 2;
    if (cell_h < 2) cell_h = 2;
    for (size_t i = 0; i < n; ++i) {
        int col = (int)(i % cols);
        int row = (int)(i / cols);
        fill_fast(hdc, x + col * (cell_w + gap), y + row * (cell_h + gap), cell_w, cell_h, cpu_color(cpu.per_core[i]));
    }
}

void Renderer::paint(HDC hdc, RECT rc, const Snapshot& snap, const Config& cfg, bool vertical) {
    fill_fast(hdc, rc.left, rc.top, rc.right - rc.left, rc.bottom - rc.top, RGB(22, 22, 24));
    if (!snap.valid) {
        SetBkMode(hdc, TRANSPARENT); SetTextColor(hdc, RGB(160, 160, 160));
        HFONT old = (HFONT)SelectObject(hdc, t_cache.small(12));
        DrawTextW(hdc, L"Glance — sampling…", -1, &rc, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
        SelectObject(hdc, old); return;
    }
    if (vertical) paint_vertical(hdc, rc, snap, cfg);
    else paint_horizontal(hdc, rc, snap, cfg);
}

void Renderer::paint_horizontal(HDC hdc, RECT rc, const Snapshot& snap, const Config& cfg) {
    const int pad = 4;
    int x = rc.left + pad, y = rc.top + pad;
    int aw = (rc.right - rc.left) - pad * 2;
    int ah = (rc.bottom - rc.top) - pad * 2;
    if (aw <= 0 || ah <= 0) return;
    SetBkMode(hdc, TRANSPARENT);
    HFONT old = (HFONT)SelectObject(hdc, t_cache.small(10));
    SetTextColor(hdc, RGB(200, 200, 200));

    int secs = (cfg.show_cpu ? 1 : 0) + (cfg.show_ram ? 1 : 0) + ((cfg.show_disks && snap.disk_count) ? 1 : 0) + (cfg.show_net ? 1 : 0);
    if (secs == 0) { SelectObject(hdc, old); return; }
    int gap = 12;
    int sw = (aw - gap * (secs - 1)) / secs;
    if (sw < 20) sw = 20;
    int cur = x;
    auto lbl = [&](int sx, const wchar_t* t){ RECT r{ sx, y, sx + sw, y + 14 }; DrawTextW(hdc, t, -1, &r, DT_LEFT | DT_TOP | DT_SINGLELINE | DT_END_ELLIPSIS); };
    if (cfg.show_cpu) { lbl(cur, L"CPU"); draw_cpu_tiles(hdc, cur, y + 16, sw, ah - 16, snap.cpu, false); cur += sw + gap; }
    if (cfg.show_ram) {
        lbl(cur, L"RAM");
        int by = y + 18, bh = 8;
        fill_fast(hdc, cur, by, sw, bh, RGB(50, 50, 52));
        int f = (int)(sw * (snap.ram.used_percent / 100.0)); if (f) fill_fast(hdc, cur, by, f, bh, ram_color(snap.ram.used_percent));
        if (cfg.always_numbers) {
            wchar_t b[32]; wsprintfW(b, L"%.0f%%", snap.ram.used_percent);
            RECT tr{ cur, by + bh + 2, cur + sw, by + bh + 16 }; DrawTextW(hdc, b, -1, &tr, DT_LEFT | DT_SINGLELINE);
        }
        cur += sw + gap;
    }
    if (cfg.show_disks && snap.disk_count) {
        lbl(cur, L"DSK"); int by = y + 18;
        for (size_t i = 0; i < snap.disk_count && by + 8 < y + ah; ++i) {
            fill_fast(hdc, cur, by, sw, 3, RGB(50, 50, 52));
            int f = (int)(sw * std::clamp(snap.disks[i].active / 100.0, 0.0, 1.0)); if (f) fill_fast(hdc, cur, by, f, 3, cpu_color(snap.disks[i].active));
            by += 5;
        }
        cur += sw + gap;
    }
    if (cfg.show_net) {
        lbl(cur, L"NET");
        double tot = snap.net.rx_bps + snap.net.tx_bps;
        if (tot > 1) { int rx = (int)(sw * (snap.net.rx_bps / tot)); if (rx) fill_fast(hdc, cur, y + 18, rx, 8, RGB(70,160,220)); if (sw - rx) fill_fast(hdc, cur + rx, y + 18, sw - rx, 8, RGB(220,160,70)); }
        else fill_fast(hdc, cur, y + 18, sw, 8, RGB(50,50,52));
        wchar_t b[64]; wsprintfW(b, L"\x2193 %.0f \x2191 %.0f", snap.net.rx_bps, snap.net.tx_bps);
        RECT tr{ cur, y + 28, cur + sw, y + 42 }; DrawTextW(hdc, b, -1, &tr, DT_LEFT | DT_SINGLELINE | DT_END_ELLIPSIS);
    }
    SelectObject(hdc, old);
}
void Renderer::paint_vertical(HDC hdc, RECT rc, const Snapshot& snap, const Config& cfg) {
    const int pad = 4;
    int x = rc.left + pad, y = rc.top + pad;
    int aw = (rc.right - rc.left) - pad * 2, ah = (rc.bottom - rc.top) - pad * 2;
    if (aw <= 0 || ah <= 0) return;
    HFONT old = (HFONT)SelectObject(hdc, t_cache.small(10));
    SetBkMode(hdc, TRANSPARENT); SetTextColor(hdc, RGB(200, 200, 200));
    int sh = ah / 4; if (sh < 20) sh = 20; int cy = y;
    if (cfg.show_cpu) { RECT r{ x, cy, x + aw, cy + 14 }; DrawTextW(hdc, L"CPU", -1, &r, DT_LEFT | DT_SINGLELINE); draw_cpu_tiles(hdc, x, cy + 16, aw, sh - 16, snap.cpu, true); cy += sh + 4; }
    if (cfg.show_ram && cy < y + ah) {
        wchar_t b[32]; wsprintfW(b, L"RAM %.0f%%", snap.ram.used_percent);
        RECT r{ x, cy, x + aw, cy + 14 }; DrawTextW(hdc, b, -1, &r, DT_LEFT | DT_SINGLELINE);
        fill_fast(hdc, x, cy + 16, aw, 6, RGB(50,50,52)); int f = (int)(aw * (snap.ram.used_percent / 100.0)); if (f) fill_fast(hdc, x, cy + 16, f, 6, ram_color(snap.ram.used_percent)); cy += sh + 4;
    }
    if (cfg.show_disks) { for (size_t i = 0; i < snap.disk_count && cy + 8 <= y + ah; ++i){ fill_fast(hdc, x, cy, aw, 3, RGB(50,50,52)); int f=(int)(aw*std::clamp(snap.disks[i].active/100.0,0.0,1.0)); if(f) fill_fast(hdc,x,cy,f,3,cpu_color(snap.disks[i].active)); cy+=5; } cy+=6; }
    if (cfg.show_net && cy < y + ah) { wchar_t b[48]; wsprintfW(b, L"\x2193%.0f \x2191%.0f", snap.net.rx_bps, snap.net.tx_bps); RECT r{ x, cy, x + aw, cy + 14 }; DrawTextW(hdc, b, -1, &r, DT_LEFT | DT_SINGLELINE | DT_END_ELLIPSIS); }
    SelectObject(hdc, old);
}

} // namespace glance
