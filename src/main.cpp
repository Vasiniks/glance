#include "config.h"
#include "telemetry.h"
#include "renderer.h"
#include "appbar.h"
#include <windows.h>
#include <shellapi.h>
#include <commctrl.h>

namespace glance {

static const wchar_t* kClassName = L"GlanceBar";
static const wchar_t* kMutexName = L"Local\\GlanceSingleInstance";

struct AppState {
    Config cfg{};
    Collector* collector = nullptr;
    Renderer* renderer = nullptr;
    AppBar* appbar = nullptr;
    Tray* tray = nullptr;
    HWND hwndTip = nullptr;
    Snapshot snapshot{};
    uint64_t gen = 0;
    UINT dpi = 96;
    bool vertical = false;
    bool tracking = false;
};

static AppState* g_state = nullptr;

static void set_dpi_awareness() noexcept {
    HMODULE u32 = GetModuleHandleW(L"user32.dll");
    if (u32) {
        using SetCtx = BOOL(WINAPI*)(HANDLE);
        auto fn = reinterpret_cast<SetCtx>(GetProcAddress(u32, "SetProcessDpiAwarenessContext"));
        if (fn) { fn((HANDLE)-4); return; }
    }
    HMODULE sh = LoadLibraryW(L"shcore.dll");
    if (sh) {
        using Fn = HRESULT(WINAPI*)(int);
        auto f = reinterpret_cast<Fn>(GetProcAddress(sh, "SetProcessDpiAwareness"));
        if (f) f(2);
        FreeLibrary(sh); return;
    }
    SetProcessDPIAware();
}

static void show_tooltip(AppState* st, int x, int y) {
    if (!st || !st->hwndTip || !st->renderer) return;
    RECT rc; GetClientRect(st->hwndTip ? GetParent(st->hwndTip) : nullptr, &rc);
    // Use renderer's last layout hit test
    HWND bar = FindWindowW(kClassName, nullptr);
    if (!bar) return;
    RECT barRc; GetClientRect(bar, &barRc);
    Renderer::Hit h = st->renderer->hit_test(x, y, barRc);
    if (h.type == Renderer::Hit::None) {
        // hide tooltip
        SendMessageW(st->hwndTip, TTM_POP, 0, 0);
        return;
    }
    wchar_t tip[256]{};
    switch (h.type) {
    case Renderer::Hit::Cpu: {
        wsprintfW(tip, L"CPU total %.0f%% • %zu cores • click Task Manager", st->snapshot.cpu.total, st->snapshot.cpu.count);
        break;
    }
    case Renderer::Hit::Ram: {
        double gbUsed = (double)st->snapshot.ram.used_bytes / 1024/1024/1024;
        double gbTotal = (double)st->snapshot.ram.total_bytes / 1024/1024/1024;
        wsprintfW(tip, L"RAM %.0f%% — %.1f / %.1f GB used", st->snapshot.ram.used_percent, gbUsed, gbTotal);
        break;
    }
    case Renderer::Hit::Disk: {
        if (h.disk_index >=0 && (size_t)h.disk_index < st->snapshot.disk_count) {
            auto &d = st->snapshot.disks[h.disk_index];
            wsprintfW(tip, L"Disk %ls — %.0f%% active • R %.0f B/s W %.0f B/s", d.name, d.active, d.read_bps, d.write_bps);
        } else wsprintfW(tip, L"Disks — %zu devices", st->snapshot.disk_count);
        break;
    }
    case Renderer::Hit::Net: {
        wsprintfW(tip, L"Net %ls — ↓ %.0f ↑ %.0f B/s", st->snapshot.net.name, st->snapshot.net.rx_bps, st->snapshot.net.tx_bps);
        break;
    }
    default: return;
    }
    TOOLINFOW ti{}; ti.cbSize=sizeof(ti); ti.hwnd = FindWindowW(kClassName,nullptr); ti.uId = 1;
    ti.lpszText = tip;
    SendMessageW(st->hwndTip, TTM_UPDATETIPTEXTW, 0, (LPARAM)&ti);
    // Position near cursor
    POINT pt{ x+16, y+16 }; ClientToScreen(ti.hwnd, &pt);
    SendMessageW(st->hwndTip, TTM_TRACKPOSITION, 0, MAKELPARAM(pt.x, pt.y));
    SendMessageW(st->hwndTip, TTM_TRACKACTIVATE, TRUE, (LPARAM)&ti);
}

LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    AppState* st = g_state;
    if (!st && msg != WM_CREATE) return DefWindowProcW(hwnd, msg, wParam, lParam);

    switch (msg) {
    case WM_CREATE: {
        st = g_state;
        // Common controls for tooltip
        InitCommonControls();
        // Tooltip as tracking tooltip
        st->hwndTip = CreateWindowExW(WS_EX_TOPMOST, TOOLTIPS_CLASSW, nullptr,
            WS_POPUP | TTS_NOPREFIX | TTS_ALWAYSTIP,
            0,0,0,0, hwnd, nullptr, GetModuleHandleW(nullptr), nullptr);
        if (st->hwndTip) {
            TOOLINFOW ti{}; ti.cbSize=sizeof(ti); ti.uFlags = TTF_TRACK | TTF_ABSOLUTE;
            ti.hwnd = hwnd; ti.uId = 1; ti.lpszText = (LPWSTR)L"Glance";
            GetClientRect(hwnd, &ti.rect);
            SendMessageW(st->hwndTip, TTM_ADDTOOLW, 0, (LPARAM)&ti);
            SendMessageW(st->hwndTip, TTM_SETMAXTIPWIDTH, 0, 360);
        }

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
        if (wParam==1 && st && st->collector) {
            st->gen = st->collector->mailbox.try_consume(st->snapshot, st->gen);
            InvalidateRect(hwnd, nullptr, FALSE);
        }
        return 0;
    case WM_PAINT: {
        PAINTSTRUCT ps; HDC hdc=BeginPaint(hwnd,&ps);
        RECT rc; GetClientRect(hwnd,&rc);
        HDC mem=CreateCompatibleDC(hdc);
        HBITMAP bmp=CreateCompatibleBitmap(hdc, rc.right?rc.right:1, rc.bottom?rc.bottom:1);
        HBITMAP old=(HBITMAP)SelectObject(mem,bmp);
        if(st&&st->renderer) st->renderer->paint(mem,rc,st->snapshot,st->cfg,st->vertical);
        BitBlt(hdc,0,0,rc.right,rc.bottom,mem,0,0,SRCCOPY);
        SelectObject(mem,old); DeleteObject(bmp); DeleteDC(mem);
        EndPaint(hwnd,&ps); return 0;
    }
    case WM_MOUSEMOVE: {
        if (!st->tracking) {
            TRACKMOUSEEVENT tme{}; tme.cbSize=sizeof(tme); tme.dwFlags=TME_LEAVE; tme.hwndTrack=hwnd; tme.dwHoverTime=HOVER_DEFAULT;
            TrackMouseEvent(&tme); st->tracking=true;
        }
        int x=(int)LOWORD(lParam), y=(int)HIWORD(lParam);
        show_tooltip(st,x,y);
        return 0;
    }
    case WM_MOUSELEAVE: {
        st->tracking=false;
        if(st->hwndTip){
            TOOLINFOW ti{}; ti.cbSize=sizeof(ti); ti.hwnd=hwnd; ti.uId=1;
            SendMessageW(st->hwndTip, TTM_TRACKACTIVATE, FALSE, (LPARAM)&ti);
        }
        return 0;
    }
    case WM_LBUTTONUP:
        if(st&&st->cfg.open_taskmgr) ShellExecuteW(nullptr,L"open",L"taskmgr.exe",nullptr,nullptr,SW_NORMAL);
        return 0;
    case WM_RBUTTONUP: {
        if(!st) return 0;
        POINT pt; GetCursorPos(&pt);
        st->tray->show_menu(pt); return 0;
    }
    case WM_APP+2: // tray
        if(lParam==WM_RBUTTONUP || lParam==WM_LBUTTONDBLCLK){ POINT pt; GetCursorPos(&pt); if(st&&st->tray) st->tray->show_menu(pt); }
        return 0;
    case WM_APP+1:
        if(wParam==ABN_POSCHANGED && st&&st->appbar) st->appbar->reposition();
        return 0;
    case WM_DISPLAYCHANGE:
    case WM_DPICHANGED: {
        if(!st) return 0;
        if(msg==WM_DPICHANGED){
            st->dpi=HIWORD(wParam);
            if(st->renderer) st->renderer->set_dpi(st->dpi);
            RECT* pr=reinterpret_cast<RECT*>(lParam);
            SetWindowPos(hwnd,nullptr,pr->left,pr->top,pr->right-pr->left,pr->bottom-pr->top,SWP_NOZORDER|SWP_NOACTIVATE);
        }
        if(st->appbar) st->appbar->reposition();
        st->vertical=st->appbar?st->appbar->is_vertical():false;
        InvalidateRect(hwnd,nullptr,TRUE); return 0;
    }
    case WM_COMMAND:
        switch(LOWORD(wParam)){
        case 1001:{ std::wstring p=config_path(); ShellExecuteW(nullptr,L"open",p.c_str(),nullptr,nullptr,SW_NORMAL); break; }
        case 1002: ShellExecuteW(nullptr,L"open",L"taskmgr.exe",nullptr,nullptr,SW_NORMAL); break;
        case 1003: PostMessageW(hwnd,WM_CLOSE,0,0); break;
        } return 0;
    case WM_CLOSE: DestroyWindow(hwnd); return 0;
    case WM_DESTROY: {
        KillTimer(hwnd,1);
        if(st){
            if(st->collector){ st->collector->stop(); delete st->collector; st->collector=nullptr; }
            if(st->tray){ st->tray->remove(); delete st->tray; st->tray=nullptr; }
            if(st->appbar){ st->appbar->unregister_bar(); delete st->appbar; st->appbar=nullptr; }
            if(st->renderer){ st->renderer->cleanup(); delete st->renderer; st->renderer=nullptr; }
            if(st->hwndTip){ DestroyWindow(st->hwndTip); st->hwndTip=nullptr; }
        }
        PostQuitMessage(0); return 0;
    }
    }
    return DefWindowProcW(hwnd,msg,wParam,lParam);
}

int run() {
    HANDLE mx=CreateMutexW(nullptr,FALSE,kMutexName);
    if(GetLastError()==ERROR_ALREADY_EXISTS){ HWND ex=FindWindowW(kClassName,nullptr); if(ex) SetForegroundWindow(ex); if(mx) CloseHandle(mx); return 0; }
    AppState s; load_config(s.cfg); g_state=&s;
    set_dpi_awareness();
    HINSTANCE h=GetModuleHandleW(nullptr);
    WNDCLASSW wc{}; wc.lpfnWndProc=WndProc; wc.hInstance=h; wc.lpszClassName=kClassName; wc.hCursor=LoadCursorW(nullptr,IDC_ARROW); wc.hbrBackground=(HBRUSH)GetStockObject(BLACK_BRUSH);
    RegisterClassW(&wc);
    HWND hwnd=CreateWindowExW(WS_EX_TOPMOST|WS_EX_TOOLWINDOW|WS_EX_NOACTIVATE,kClassName,L"Glance",WS_POPUP|WS_VISIBLE,0,0,520,56,nullptr,nullptr,h,nullptr);
    if(!hwnd){ CloseHandle(mx); return 1; }
    MSG m; while(GetMessageW(&m,nullptr,0,0)){ TranslateMessage(&m); DispatchMessageW(&m); }
    save_config(s.cfg); g_state=nullptr; CloseHandle(mx); return (int)m.wParam;
}
} // ns
int WINAPI wWinMain(HINSTANCE,HINSTANCE,PWSTR,int){ return glance::run(); }
