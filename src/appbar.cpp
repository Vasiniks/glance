#include "appbar.h"
#include <shellapi.h>
#include <vector>

namespace glance {

struct MonInfo {
    int index = 0;
    int target = 0;
    HMONITOR found = nullptr;
};

BOOL CALLBACK AppBar::monitor_enum(HMONITOR hMon, HDC, LPRECT, LPARAM lp) {
    auto* info = reinterpret_cast<MonInfo*>(lp);
    if (info->index == info->target) {
        info->found = hMon;
        return FALSE;
    }
    info->index++;
    return TRUE;
}
HMONITOR AppBar::find_monitor(int index) {
    if (index <= 0) return MonitorFromWindow(nullptr, MONITOR_DEFAULTTOPRIMARY);
    MonInfo mi{};
    mi.target = index;
    EnumDisplayMonitors(nullptr, nullptr, monitor_enum, reinterpret_cast<LPARAM>(&mi));
    if (mi.found) return mi.found;
    return MonitorFromWindow(nullptr, MONITOR_DEFAULTTOPRIMARY);
}

AppBar::AppBar(HWND hwnd, const Config& cfg) : hwnd_(hwnd), cfg_(cfg) {}

AppBar::~AppBar() { unregister_bar(); }

bool AppBar::is_vertical() const noexcept {
    Edge e = cfg_.edge;
    // Auto: pick primary edge? We'll compute from bar_rect if known, else assume bottom
    if (e == Edge::Auto) return false; // treat auto as horizontal bottom for now
    return e == Edge::Left || e == Edge::Right;
}

bool AppBar::register_bar() {
    APPBARDATA abd{};
    abd.cbSize = sizeof(abd);
    abd.hWnd = hwnd_;
    abd.uCallbackMessage = WM_APP + 1;
    if (SHAppBarMessage(ABM_NEW, &abd) == 0) return false;
    registered_ = true;
    reposition();
    return true;
}

void AppBar::unregister_bar() {
    if (!registered_) return;
    APPBARDATA abd{};
    abd.cbSize = sizeof(abd);
    abd.hWnd = hwnd_;
    SHAppBarMessage(ABM_REMOVE, &abd);
    registered_ = false;
}

void AppBar::reposition() {
    HMONITOR mon = find_monitor(cfg_.monitor);
    MONITORINFO mi{};
    mi.cbSize = sizeof(mi);
    GetMonitorInfoW(mon, &mi);
    RECT work = mi.rcWork;
    RECT full = mi.rcMonitor;

    static auto getDpi = []() -> UINT(WINAPI*)(HWND) {
        HMODULE u32 = GetModuleHandleW(L"user32.dll");
        if (!u32) return nullptr;
        return reinterpret_cast<UINT(WINAPI*)(HWND)>(GetProcAddress(u32, "GetDpiForWindow"));
    }();
    UINT dpi = 96;
    if (getDpi) { UINT d = getDpi(hwnd_); if (d) dpi = d; }
    int thickness_px = MulDiv(cfg_.thickness, (int)dpi, 96);

    Edge edge = cfg_.edge;
    if (edge == Edge::Auto) edge = Edge::Bottom; // simple default

    APPBARDATA abd{};
    abd.cbSize = sizeof(abd);
    abd.hWnd = hwnd_;

    switch (edge) {
    case Edge::Left:
        abd.uEdge = ABE_LEFT;
        abd.rc = { full.left, full.top, full.left + thickness_px, full.bottom };
        break;
    case Edge::Right:
        abd.uEdge = ABE_RIGHT;
        abd.rc = { full.right - thickness_px, full.top, full.right, full.bottom };
        break;
    case Edge::Top:
        abd.uEdge = ABE_TOP;
        abd.rc = { full.left, full.top, full.right, full.top + thickness_px };
        break;
    case Edge::Bottom:
    default:
        abd.uEdge = ABE_BOTTOM;
        abd.rc = { full.left, full.bottom - thickness_px, full.right, full.bottom };
        break;
    }

    if (!registered_) {
        bar_rect_ = abd.rc;
        SetWindowPos(hwnd_, HWND_TOPMOST, abd.rc.left, abd.rc.top,
                     abd.rc.right - abd.rc.left, abd.rc.bottom - abd.rc.top,
                     SWP_NOACTIVATE);
        return;
    }

    SHAppBarMessage(ABM_QUERYPOS, &abd);
    // ABE adjust: keep thickness
    switch (abd.uEdge) {
    case ABE_LEFT: abd.rc.right = abd.rc.left + thickness_px; break;
    case ABE_RIGHT: abd.rc.left = abd.rc.right - thickness_px; break;
    case ABE_TOP: abd.rc.bottom = abd.rc.top + thickness_px; break;
    case ABE_BOTTOM: abd.rc.top = abd.rc.bottom - thickness_px; break;
    }
    SHAppBarMessage(ABM_SETPOS, &abd);
    bar_rect_ = abd.rc;
    SetWindowPos(hwnd_, HWND_TOPMOST, abd.rc.left, abd.rc.top,
                 abd.rc.right - abd.rc.left, abd.rc.bottom - abd.rc.top,
                 SWP_NOACTIVATE | SWP_NOZORDER);
}

// Tray

Tray::Tray(HWND hwnd) : hwnd_(hwnd) {
    nid_.cbSize = sizeof(nid_);
    nid_.hWnd = hwnd;
    nid_.uID = 1;
    nid_.uFlags = NIF_MESSAGE | NIF_ICON | NIF_TIP;
    nid_.uCallbackMessage = WM_APP + 2;
    nid_.hIcon = LoadIconW(nullptr, IDI_APPLICATION);
    wcscpy_s(nid_.szTip, L"Glance — lightweight monitor");
}

Tray::~Tray() { remove(); }

bool Tray::add() {
    if (added_) return true;
    if (!Shell_NotifyIconW(NIM_ADD, &nid_)) return false;
    added_ = true;
    return true;
}
void Tray::remove() {
    if (!added_) return;
    Shell_NotifyIconW(NIM_DELETE, &nid_);
    added_ = false;
}

void Tray::show_menu(POINT pt) {
    HMENU menu = CreatePopupMenu();
    AppendMenuW(menu, MF_STRING, 1001, L"Settings…");
    AppendMenuW(menu, MF_STRING, 1002, L"Task Manager");
    AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(menu, MF_STRING, 1003, L"Exit Glance");
    SetForegroundWindow(hwnd_);
    int cmd = TrackPopupMenu(menu, TPM_RETURNCMD | TPM_NONOTIFY, pt.x, pt.y, 0, hwnd_, nullptr);
    DestroyMenu(menu);
    if (cmd) PostMessageW(hwnd_, WM_COMMAND, cmd, 0);
}

} // namespace glance
