#include "renderer.h"
#include <algorithm>
#include <string>
#include <cmath>

namespace glance {

COLORREF lerp_color(COLORREF a, COLORREF b, double t) noexcept {
    t = std::clamp(t, 0.0, 1.0);
    int ar = GetRValue(a), ag = GetGValue(a), ab = GetBValue(a);
    int br = GetRValue(b), bg = GetGValue(b), bb = GetBValue(b);
    return RGB(int(ar + (br - ar) * t), int(ag + (bg - ag) * t), int(ab + (bb - ab) * t));
}

COLORREF cpu_color(double pct) noexcept {
    // green -> yellow -> red
    if (pct < 50) return lerp_color(RGB(80, 200, 80), RGB(240, 200, 50), pct / 50.0);
    return lerp_color(RGB(240, 200, 50), RGB(230, 50, 50), (pct - 50) / 50.0);
}
COLORREF ram_color(double pct) noexcept {
    return cpu_color(pct);
}

static HFONT make_font(int px, bool bold = false) {
    return CreateFontW(-px, 0, 0, 0, bold ? FW_BOLD : FW_NORMAL, FALSE, FALSE, FALSE,
                       DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                       CLEARTYPE_QUALITY, DEFAULT_PITCH, L"Segoe UI");
}

static void fill_rect(HDC hdc, int x, int y, int w, int h, COLORREF c) {
    HBRUSH br = CreateSolidBrush(c);
    RECT r{ x, y, x + w, y + h };
    FillRect(hdc, &r, br);
    DeleteObject(br);
}

static void draw_cpu_tiles(HDC hdc, int x, int y, int w, int h, const CpuSample& cpu, bool vertical) {
    if (cpu.per_core.empty()) return;
    size_t n = cpu.per_core.size();
    int cols = (int)std::ceil(std::sqrt((double)n));
    int rows = (int)std::ceil((double)n / cols);
    if (vertical) std::swap(cols, rows);

    int gap = 2;
    int cell_w = (w - gap * (cols - 1)) / cols;
    int cell_h = (h - gap * (rows - 1)) / rows;
    if (cell_w < 2) cell_w = 2;
    if (cell_h < 2) cell_h = 2;

    for (size_t i = 0; i < n; ++i) {
        int col = (int)(i % cols);
        int row = (int)(i / cols);
        int cx = x + col * (cell_w + gap);
        int cy = y + row * (cell_h + gap);
        fill_rect(hdc, cx, cy, cell_w, cell_h, cpu_color(cpu.per_core[i]));
    }
}

void Renderer::paint(HDC hdc, RECT rc, const Snapshot& snap, const Config& cfg, bool vertical) {
    // Background
    fill_rect(hdc, rc.left, rc.top, rc.right - rc.left, rc.bottom - rc.top, RGB(22, 22, 24));

    if (!snap.valid) {
        SetBkMode(hdc, TRANSPARENT);
        SetTextColor(hdc, RGB(160, 160, 160));
        HFONT f = make_font(12);
        HFONT old = (HFONT)SelectObject(hdc, f);
        DrawTextW(hdc, L"Glance — sampling…", -1, &rc, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
        SelectObject(hdc, old);
        DeleteObject(f);
        return;
    }

    if (vertical) paint_vertical(hdc, rc, snap, cfg);
    else paint_horizontal(hdc, rc, snap, cfg);
}

void Renderer::paint_horizontal(HDC hdc, RECT rc, const Snapshot& snap, const Config& cfg) {
    int pad = 4;
    int x = rc.left + pad;
    int y = rc.top + pad;
    int avail_w = (rc.right - rc.left) - pad * 2;
    int avail_h = (rc.bottom - rc.top) - pad * 2;
    if (avail_w <= 0 || avail_h <= 0) return;

    SetBkMode(hdc, TRANSPARENT);
    HFONT small = make_font(10);
    HFONT bold = make_font(11, true);
    HFONT old = (HFONT)SelectObject(hdc, small);
    SetTextColor(hdc, RGB(200, 200, 200));

    // Simple linear layout: CPU | RAM bar | Disks | Net
    int section_gap = 12;
    int sections = 0;
    if (cfg.show_cpu) ++sections;
    if (cfg.show_ram) ++sections;
    if (cfg.show_disks && !snap.disks.empty()) ++sections;
    if (cfg.show_net) ++sections;
    if (sections == 0) { SelectObject(hdc, old); DeleteObject(small); DeleteObject(bold); return; }

    int sec_w = (avail_w - section_gap * (sections - 1)) / sections;

    auto draw_section_label = [&](int sx, const wchar_t* label) {
        RECT lr{ sx, y, sx + sec_w, y + 14 };
        DrawTextW(hdc, label, -1, &lr, DT_LEFT | DT_TOP | DT_SINGLELINE | DT_END_ELLIPSIS);
    };

    int cur_x = x;

    if (cfg.show_cpu) {
        draw_section_label(cur_x, L"CPU");
        draw_cpu_tiles(hdc, cur_x, y + 16, sec_w, avail_h - 16, snap.cpu, false);
        cur_x += sec_w + section_gap;
    }
    if (cfg.show_ram) {
        draw_section_label(cur_x, L"RAM");
        int bar_y = y + 18;
        int bar_h = 8;
        fill_rect(hdc, cur_x, bar_y, sec_w, bar_h, RGB(50, 50, 52));
        int fill = (int)(sec_w * (snap.ram.used_percent / 100.0));
        fill_rect(hdc, cur_x, bar_y, fill, bar_h, ram_color(snap.ram.used_percent));
        if (cfg.always_numbers) {
            wchar_t buf[64];
            swprintf(buf, 64, L"%.0f%%", snap.ram.used_percent);
            RECT tr{ cur_x, bar_y + bar_h + 2, cur_x + sec_w, bar_y + bar_h + 16 };
            DrawTextW(hdc, buf, -1, &tr, DT_LEFT | DT_SINGLELINE);
        }
        cur_x += sec_w + section_gap;
    }
    if (cfg.show_disks && !snap.disks.empty()) {
        draw_section_label(cur_x, L"DSK");
        int by = y + 18;
        for (size_t i = 0; i < snap.disks.size() && by + 8 < y + avail_h; ++i) {
            fill_rect(hdc, cur_x, by, sec_w, 3, RGB(50, 50, 52));
            int f = (int)(sec_w * std::clamp(snap.disks[i].active / 100.0, 0.0, 1.0));
            fill_rect(hdc, cur_x, by, f, 3, cpu_color(snap.disks[i].active));
            by += 5;
        }
        cur_x += sec_w + section_gap;
    }
    if (cfg.show_net) {
        draw_section_label(cur_x, L"NET");
        wchar_t buf[128];
        double total = snap.net.rx_bps + snap.net.tx_bps;
        if (total > 0) {
            double rx_frac = snap.net.rx_bps / total;
            int rx_w = (int)(sec_w * rx_frac);
            fill_rect(hdc, cur_x, y + 18, rx_w, 8, RGB(70, 160, 220));
            fill_rect(hdc, cur_x + rx_w, y + 18, sec_w - rx_w, 8, RGB(220, 160, 70));
        }
        else {
            fill_rect(hdc, cur_x, y + 18, sec_w, 8, RGB(50, 50, 52));
        }
        swprintf(buf, 128, L"↓ %.0f ↑ %.0f B/s", snap.net.rx_bps, snap.net.tx_bps);
        RECT tr{ cur_x, y + 28, cur_x + sec_w, y + 42 };
        DrawTextW(hdc, buf, -1, &tr, DT_LEFT | DT_SINGLELINE | DT_END_ELLIPSIS);
    }

    SelectObject(hdc, old);
    DeleteObject(small);
    DeleteObject(bold);
}

void Renderer::paint_vertical(HDC hdc, RECT rc, const Snapshot& snap, const Config& cfg) {
    // For left/right edges: stack sections vertically (same logic rotated)
    int pad = 4;
    int x = rc.left + pad;
    int y = rc.top + pad;
    int avail_w = (rc.right - rc.left) - pad * 2;
    int avail_h = (rc.bottom - rc.top) - pad * 2;
    if (avail_w <= 0 || avail_h <= 0) return;

    HFONT small = make_font(10);
    HFONT old = (HFONT)SelectObject(hdc, small);
    SetBkMode(hdc, TRANSPARENT);
    SetTextColor(hdc, RGB(200, 200, 200));

    int sec_h = avail_h / 4;
    if (sec_h < 20) sec_h = 20;
    int cur_y = y;

    if (cfg.show_cpu) {
        RECT lr{ x, cur_y, x + avail_w, cur_y + 14 };
        DrawTextW(hdc, L"CPU", -1, &lr, DT_LEFT | DT_SINGLELINE);
        draw_cpu_tiles(hdc, x, cur_y + 16, avail_w, sec_h - 16, snap.cpu, true);
        cur_y += sec_h + 4;
    }
    if (cfg.show_ram && cur_y < y + avail_h) {
        wchar_t buf[64];
        swprintf(buf, 64, L"RAM %.0f%%", snap.ram.used_percent);
        RECT lr{ x, cur_y, x + avail_w, cur_y + 14 };
        DrawTextW(hdc, buf, -1, &lr, DT_LEFT | DT_SINGLELINE);
        fill_rect(hdc, x, cur_y + 16, avail_w, 6, RGB(50, 50, 52));
        int f = (int)(avail_w * (snap.ram.used_percent / 100.0));
        fill_rect(hdc, x, cur_y + 16, f, 6, ram_color(snap.ram.used_percent));
        cur_y += sec_h + 4;
    }
    if (cfg.show_disks && cur_y < y + avail_h) {
        for (auto& d : snap.disks) {
            if (cur_y + 8 > y + avail_h) break;
            fill_rect(hdc, x, cur_y, avail_w, 3, RGB(50, 50, 52));
            int f = (int)(avail_w * std::clamp(d.active / 100.0, 0.0, 1.0));
            fill_rect(hdc, x, cur_y, f, 3, cpu_color(d.active));
            cur_y += 5;
        }
        cur_y += 6;
    }
    if (cfg.show_net && cur_y < y + avail_h) {
        wchar_t buf[128];
        swprintf(buf, 128, L"↓%.0f ↑%.0f", snap.net.rx_bps, snap.net.tx_bps);
        RECT lr{ x, cur_y, x + avail_w, cur_y + 14 };
        DrawTextW(hdc, buf, -1, &lr, DT_LEFT | DT_SINGLELINE | DT_END_ELLIPSIS);
    }

    SelectObject(hdc, old);
    DeleteObject(small);
}

} // namespace glance
