#include "config.h"
#include "telemetry.h"
#include "renderer.h"
#include "appbar.h"
#include <windows.h>
#include <shellapi.h>

namespace glance {

static const wchar_t* kClassName = L"GlanceBar";
static const wchar_t* kMutexName = L"Local\\GlanceSingleInstance";

struct AppState {
    Config cfg;
    Collector* collector = nullptr;
    Renderer* renderer = nullptr;
    AppBar* appbar = nullptr;
    Tray* tray = nullptr;
    Snapshot snapshot;
    uint64_t gen = 0;
    UINT dpi = 96;
    bool vertical = false;
};

static AppState* g_state = nullptr;
static HHOOK g_dpiHook = nullptr;

static void set_dpi_awareness() noexcept {
    // Lightest: Try SetProcessDpiAwarenessContext (Win10 1703+, no DLL load)
    HMODULE u32 = GetModuleHandleW(L"user32.dll");
    if (u32) {
        using SetCtx = BOOL(WINAPI*)(HANDLE);
        auto fn = reinterpret_cast<SetCtx>(GetProcAddress(u32, "SetProcessDpiAwarenessContext"));
        if (fn) {
            // DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2 = -4
            fn((HANDLE)-4);
            return;
        }
    }
    HMODULE shcore = LoadLibraryW(L"shcore.dll");
    if (shcore) {
        using SetProc = HRESULT(WINAPI*)(int);
        auto fn = reinterpret_cast<SetProc>(GetProcAddress(shcore, "SetProcessDpiAwareness"));
        if (fn) fn(2); // PROCESS_PER_MONITOR_DPI_AWARE
        FreeLibrary(shcore);
        return;
    }
    SetProcessDPIAware();
}

LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    AppState* st = g_state;
    if (!st && msg != WM_CREATE) return DefWindowProcW(hwnd, msg, wParam, lParam);

    switch (msg) {
    case WM_CREATE: {
        st = g_state;
        st->appbar = new AppBar(hwnd, st->cfg);
        st->appbar->register_bar();
        st->vertical = st->appbar->is_vertical();
        st->tray = new Tray(hwnd);
        st->tray->add();
        st->renderer = new Renderer();
        st->renderer->set_dpi(st->dpi);
        st->collector = new Collector();
        st->collector->set_interval(st->cfg.interval_ms);
        st->collector->set_net_iface(st->cfg.net_iface);
        st->collector->start(st->cfg.net_iface);
        SetTimer(hwnd, 1, st->cfg.interval_ms, nullptr);
        return 0;
    }
    case WM_TIMER:
        if (wParam == 1 && st && st->collector) {
            st->gen = st->collector->mailbox.try_consume(st->snapshot, st->gen);
            // coalesce invalidates: only if dirty
            InvalidateRect(hwnd, nullptr, FALSE);
        }
        return 0;
    case WM_PAINT: {
        PAINTSTRUCT ps;
        HDC hdc = BeginPaint(hwnd, &ps);
        RECT rc; GetClientRect(hwnd, &rc);
        // Single compatible bitmap — cache could be smarter but this is still lighter than D2D device
        HDC mem = CreateCompatibleDC(hdc);
        HBITMAP bmp = CreateCompatibleBitmap(hdc, rc.right ? rc.right : 1, rc.bottom ? rc.bottom : 1);
        HBITMAP oldBmp = (HBITMAP)SelectObject(mem, bmp);
        if (st && st->renderer) st->renderer->paint(mem, rc, st->snapshot, st->cfg, st->vertical);
        BitBlt(hdc, 0, 0, rc.right, rc.bottom, mem, 0, 0, SRCCOPY);
        SelectObject(mem, oldBmp);
        DeleteObject(bmp);
        DeleteDC(mem);
        EndPaint(hwnd, &ps);
        return 0;
    }
    case WM_LBUTTONUP:
        if (st && st->cfg.open_taskmgr)
            ShellExecuteW(nullptr, L"open", L"taskmgr.exe", nullptr, nullptr, SW_NORMAL);
        return 0;
    case WM_RBUTTONUP: {
        if (!st) return 0;
        POINT pt; GetCursorPos(&pt);
        st->tray->show_menu(pt);
        return 0;
    }
    case WM_APP + 2: // tray
        if (lParam == WM_RBUTTONUP || lParam == WM_LBUTTONDBLCLK) {
            POINT pt; GetCursorPos(&pt);
            if (st && st->tray) st->tray->show_menu(pt);
        }
        return 0;
    case WM_APP + 1: // appbar
        if (wParam == ABN_POSCHANGED && st && st->appbar) st->appbar->reposition();
        return 0;
    case WM_DISPLAYCHANGE:
    case WM_DPICHANGED: {
        if (!st) return 0;
        if (msg == WM_DPICHANGED) {
            st->dpi = HIWORD(wParam);
            if (st->renderer) st->renderer->set_dpi(st->dpi);
            RECT* pr = reinterpret_cast<RECT*>(lParam);
            SetWindowPos(hwnd, nullptr, pr->left, pr->top, pr->right - pr->left, pr->bottom - pr->top,
                         SWP_NOZORDER | SWP_NOACTIVATE);
        }
        if (st->appbar) st->appbar->reposition();
        st->vertical = st->appbar ? st->appbar->is_vertical() : false;
        InvalidateRect(hwnd, nullptr, TRUE);
        return 0;
    }
    case WM_COMMAND:
        switch (LOWORD(wParam)) {
        case 1001: { std::wstring p = config_path(); ShellExecuteW(nullptr, L"open", p.c_str(), nullptr, nullptr, SW_NORMAL); break; }
        case 1002: ShellExecuteW(nullptr, L"open", L"taskmgr.exe", nullptr, nullptr, SW_NORMAL); break;
        case 1003: PostMessageW(hwnd, WM_CLOSE, 0, 0); break;
        }
        return 0;
    case WM_CLOSE: DestroyWindow(hwnd); return 0;
    case WM_DESTROY: {
        KillTimer(hwnd, 1);
        if (st) {
            if (st->collector) { st->collector->stop(); delete st->collector; st->collector = nullptr; }
            if (st->tray) { st->tray->remove(); delete st->tray; st->tray = nullptr; }
            if (st->appbar) { st->appbar->unregister_bar(); delete st->appbar; st->appbar = nullptr; }
            if (st->renderer) { delete st->renderer; st->renderer = nullptr; }
        }
        PostQuitMessage(0);
        return 0;
    }
    }
    return DefWindowProcW(hwnd, msg, wParam, lParam);
}

int run() {
    HANDLE mutex = CreateMutexW(nullptr, FALSE, kMutexName);
    if (GetLastError() == ERROR_ALREADY_EXISTS) {
        // Find existing window and bring tray menu
        HWND ex = FindWindowW(kClassName, nullptr);
        if (ex) SetForegroundWindow(ex);
        if (mutex) CloseHandle(mutex);
        return 0;
    }

    AppState state;
    load_config(state.cfg);
    g_state = &state;

    set_dpi_awareness();

    HINSTANCE hInst = GetModuleHandleW(nullptr);
    WNDCLASSW wc{};
    wc.lpfnWndProc = WndProc;
    wc.hInstance = hInst;
    wc.lpszClassName = kClassName;
    wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    wc.hbrBackground = (HBRUSH)GetStockObject(BLACK_BRUSH);
    RegisterClassW(&wc);

    // Compact window: thickness from config, but start small then AppBar repositions
    HWND hwnd = CreateWindowExW(WS_EX_TOPMOST | WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE,
                                kClassName, L"Glance",
                                WS_POPUP | WS_VISIBLE,
                                0, 0, 400, state.cfg.thickness,
                                nullptr, nullptr, hInst, nullptr);
    if (!hwnd) { CloseHandle(mutex); return 1; }

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

int WINAPI wWinMain(HINSTANCE, HINSTANCE, PWSTR, int) { return glance::run(); }
