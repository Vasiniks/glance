#pragma once
#include "config.h"
#include <windows.h>
#include <shellapi.h>

namespace glance {

class AppBar {
public:
    AppBar(HWND hwnd, const Config& cfg);
    ~AppBar();

    bool register_bar();
    void unregister_bar();
    void reposition();
    bool is_vertical() const noexcept;
    RECT bar_rect() const noexcept { return bar_rect_; }

    static BOOL CALLBACK monitor_enum(HMONITOR hMon, HDC, LPRECT, LPARAM lp);
    static HMONITOR find_monitor(int index);

private:
    HWND hwnd_;
    Config cfg_;
    RECT bar_rect_{};
    bool registered_ = false;
};

// Tray helper
class Tray {
public:
    explicit Tray(HWND hwnd);
    ~Tray();
    bool add();
    void remove();
    void show_menu(POINT pt);
private:
    HWND hwnd_;
    bool added_ = false;
    NOTIFYICONDATAW nid_{};
};

} // namespace glance
