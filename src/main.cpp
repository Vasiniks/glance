#include "config.h"
#include "telemetry.h"
#include "renderer.h"
#include "appbar.h"
#include <windows.h>
#include <windowsx.h>
#include <shellapi.h>
#include <string>
#include <memory>

namespace glance {

static const wchar_t* kClassName = L"GlanceBar";
static const wchar_t* kMutexName = L"Local\\GlanceSingleInstance";

struct AppState {
    Config cfg;
    std::unique_ptr<Collector> collector;
    std::unique_ptr<Renderer> renderer;
    std::unique_ptr<AppBar> appbar;
    std::unique_ptr<Tray> tray;
    Snapshot snapshot;
    uint64_t gen = 0;
    UINT dpi = 96;
    bool vertical = false;
};

static AppState* g_state = nullptr;

LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    AppState* st = g_state;
    if (!st) return DefWindowProcW(hwnd, msg, wParam, lParam);

    switch (msg) {
    case WM_CREATE: {
        st->appbar = std::make_unique<AppBar>(hwnd, st->cfg);
        st->appbar->register_bar();
        st->vertical = st->appbar->is_vertical();
        st->tray = std::make_unique<Tray>(hwnd);
        st->tray->add();
        st->renderer = std::make_unique<Renderer>();
        st->renderer->set_dpi(st->dpi);
        st->collector = std::make_unique<Collector>();
        st->collector->set_interval(st->cfg.interval_ms);
        st->collector->set_net_iface(st->cfg.net_iface);
        st->collector->start(st->cfg.net_iface);
        SetTimer(hwnd, 1, st->cfg.interval_ms, nullptr);
        return 0;
    }
    case WM_TIMER: {
        if (wParam == 1) {
            st->gen = st->collector->mailbox.try_consume(st->snapshot, st->gen);
            InvalidateRect(hwnd, nullptr, FALSE);
        }
        return 0;
    }
    case WM_PAINT: {
        PAINTSTRUCT ps;
        HDC hdc = BeginPaint(hwnd, &ps);
        RECT rc;
        GetClientRect(hwnd, &rc);
        // double buffer for GDI to avoid flicker — lightweight BitBlt
        HDC mem = CreateCompatibleDC(hdc);
        HBITMAP bmp = CreateCompatibleBitmap(hdc, rc.right, rc.bottom);
        HBITMAP oldBmp = (HBITMAP)SelectObject(mem, bmp);
        st->renderer->paint(mem, rc, st->snapshot, st->cfg, st->vertical);
        BitBlt(hdc, 0, 0, rc.right, rc.bottom, mem, 0, 0, SRCCOPY);
        SelectObject(mem, oldBmp);
        DeleteObject(bmp);
        DeleteDC(mem);
        EndPaint(hwnd, &ps);
        return 0;
    }
    case WM_LBUTTONUP: {
        if (st->cfg.open_taskmgr) ShellExecuteW(nullptr, L"open", L"taskmgr.exe", nullptr, nullptr, SW_NORMAL);
        return 0;
    }
    case WM_RBUTTONUP: {
        POINT pt; GetCursorPos(&pt);
        st->tray->show_menu(pt);
        return 0;
    }
    case WM_APP + 2: { // tray callback
        if (lParam == WM_RBUTTONUP || lParam == WM_LBUTTONDBLCLK) {
            POINT pt; GetCursorPos(&pt);
            st->tray->show_menu(pt);
        } else if (lParam == WM_LBUTTONUP) {
            // show tooltip-like info via title? just repaint
            InvalidateRect(hwnd, nullptr, FALSE);
        }
        return 0;
    }
    case WM_APP + 1: { // appbar notify
        if (wParam == ABN_POSCHANGED) {
            st->appbar->reposition();
        }
        return 0;
    }
    case WM_DISPLAYCHANGE:
    case WM_DPICHANGED: {
        if (msg == WM_DPICHANGED) {
            st->dpi = HIWORD(wParam);
            st->renderer->set_dpi(st->dpi);
            RECT* pr = reinterpret_cast<RECT*>(lParam);
            SetWindowPos(hwnd, nullptr, pr->left, pr->top, pr->right - pr->left, pr->bottom - pr->top,
                         SWP_NOZORDER | SWP_NOACTIVATE);
        }
        st->appbar->reposition();
        st->vertical = st->appbar->is_vertical();
        InvalidateRect(hwnd, nullptr, TRUE);
        return 0;
    }
    case WM_COMMAND: {
        switch (LOWORD(wParam)) {
        case 1001: { // settings — tiny dialog via MessageBox for now + ini edit
            std::wstring path = config_path();
            ShellExecuteW(nullptr, L"open", path.c_str(), nullptr, nullptr, SW_NORMAL);
            break;
        }
        case 1002:
            ShellExecuteW(nullptr, L"open", L"taskmgr.exe", nullptr, nullptr, SW_NORMAL);
            break;
        case 1003:
            PostMessageW(hwnd, WM_CLOSE, 0, 0);
            break;
        }
        return 0;
    }
    case WM_CLOSE: {
        DestroyWindow(hwnd);
        return 0;
    }
    case WM_DESTROY: {
        KillTimer(hwnd, 1);
        if (st->collector) st->collector->stop();
        if (st->tray) st->tray->remove();
        if (st->appbar) st->appbar->unregister_bar();
        PostQuitMessage(0);
        return 0;
    }
    }
    return DefWindowProcW(hwnd, msg, wParam, lParam);
}

int run() {
    // single instance per user session
    HANDLE mutex = CreateMutexW(nullptr, FALSE, kMutexName);
    if (GetLastError() == ERROR_ALREADY_EXISTS) {
        MessageBoxW(nullptr, L"Glance is already running.", L"Glance", MB_OK | MB_ICONINFORMATION);
        if (mutex) CloseHandle(mutex);
        return 0;
    }

    AppState state;
    load_config(state.cfg);
    g_state = &state;

    HINSTANCE hInst = GetModuleHandleW(nullptr);

    WNDCLASSW wc{};
    wc.lpfnWndProc = WndProc;
    wc.hInstance = hInst;
    wc.lpszClassName = kClassName;
    wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    wc.hbrBackground = (HBRUSH)GetStockObject(BLACK_BRUSH);
    RegisterClassW(&wc);

    // DPI awareness — lightweight, no shcore heavy init
    HMODULE shcore = LoadLibraryW(L"shcore.dll");
    if (shcore) {
        using SetProc = HRESULT(WINAPI*)(PROCESS_DPI_AWARENESS);
        auto fn = reinterpret_cast<SetProc>(GetProcAddress(shcore, "SetProcessDpiAwareness"));
        if (fn) fn(PROCESS_PER_MONITOR_DPI_AWARE);
        FreeLibrary(shcore);
    } else {
        SetProcessDPIAware();
    }

    HWND hwnd = CreateWindowExW(WS_EX_TOPMOST | WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE,
                                kClassName, L"Glance",
                                WS_POPUP | WS_VISIBLE,
                                0, 0, 400, 48,
                                nullptr, nullptr, hInst, nullptr);
    if (!hwnd) {
        MessageBoxW(nullptr, L"Failed to create bar window.", L"Glance", MB_ICONERROR);
        CloseHandle(mutex);
        return 1;
    }

    MSG msg;
    while (GetMessageW(&msg, nullptr, 0, 0)) {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }

    save_config(state.cfg);
    g_state = nullptr;
    CloseHandle(mutex);
    return (int)msg.wParam;
}

} // namespace glance

int WINAPI wWinMain(HINSTANCE, HINSTANCE, PWSTR, int) {
    return glance::run();
}
