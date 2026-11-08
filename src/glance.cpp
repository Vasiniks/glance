// Glance: a tiny system-monitor bar for Windows.
// Per-core CPU, memory, disks and network in a docked AppBar, drawn as history graphs.
// Win32 + GDI + PDH only. No C++ runtime, no threads, no third-party code. Build: ./build.sh
#ifndef UNICODE
#define UNICODE
#endif
#ifndef _UNICODE
#define _UNICODE
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#undef _WIN32_WINNT
#define _WIN32_WINNT 0x0A00
#include <windows.h>
#include <shellapi.h>
#include <commctrl.h>
#include <pdh.h>
#include <pdhmsg.h>
#include <dxgi.h>
#include <wchar.h>

#define HEX(h) RGB(((h) >> 16) & 255, ((h) >> 8) & 255, (h) & 255)

enum { HIST = 480, MAXCPU = 256, MAXDISK = 8, MAXAD = 16, MAXW = 16, NAME = 256 };
enum { K_CPU, K_RAM, K_GPU, K_DISK, K_NET };
enum { WM_APPBAR = WM_APP + 1, WM_TRAY = WM_APP + 2 };
static const wchar_t CLS[] = L"GlanceBar";
static const wchar_t RUNKEY[] = L"Software\\Microsoft\\Windows\\CurrentVersion\\Run";
static const float DISK_FLOOR = 1048576.0f, NET_FLOOR = 131072.0f;  // smallest full-scale rate

// ---------------------------------------------------------------- palette
static const COLORREF C_BG = HEX(0x0E1116), C_BORDER = HEX(0x1B2029), C_DIV = HEX(0x232832),
                      C_TXT = HEX(0xE8EDF4), C_TXT2 = HEX(0x9AA4B2);
static const COLORREF C_ICON[5] = {HEX(0xAEB7C4), HEX(0x8EB6DC), HEX(0xB1AADB), HEX(0x96BD96), HEX(0xD79FAF)};
struct Hue { COLORREF fill, track, text; };
static const Hue H_RAM = {HEX(0x55ABF7), HEX(0x1A2129), HEX(0x93CAFF)};
static const Hue H_G3D = {HEX(0x9C81F7), HEX(0x201F28), HEX(0xBFB1FF)};
static const Hue H_GMEM = {HEX(0x6D5CBF), HEX(0x201F28), HEX(0xA199E6)};
static const Hue H_GDEC = {HEX(0xD3BEFD), HEX(0x201F28), HEX(0xDECEFE)};
static const Hue H_ACT = {HEX(0x65C55B), HEX(0x1B231B), HEX(0x98E090)};
static const Hue H_RD = {HEX(0xB9DF9B), HEX(0x1B231B), HEX(0xB9DF9B)};
static const Hue H_WR = {HEX(0x36884D), HEX(0x1B231B), HEX(0x66BA7F)};
static const Hue H_DN = {HEX(0xEC5775), HEX(0x281D20), HEX(0xF78C9C)};
static const Hue H_UP = {HEX(0xA93F75), HEX(0x281D20), HEX(0xDB7AA7)};

static float U = 1;  // pixels per compact DIP (= bar thickness / 40)
static int P(float d) { return (int)(d * U + 0.5f); }
static int P1(float d) { int v = P(d); return v < 1 ? 1 : v; }
static int pc(float v) { return (int)(v + 0.5f); }
static float clampf(float v) { return v < 0 ? 0 : v > 1 ? 1 : v; }
static int clampi(int v, int lo, int hi) { return v < lo ? lo : v > hi ? hi : v; }
static COLORREF mix(COLORREF a, COLORREF b, float t) {
    return RGB(GetRValue(a) + (int)((GetRValue(b) - GetRValue(a)) * t),
               GetGValue(a) + (int)((GetGValue(b) - GetGValue(a)) * t),
               GetBValue(a) + (int)((GetBValue(b) - GetBValue(a)) * t));
}
// CPU tile colours, idle -> busy: a preset name, or `cpu_colors=#rrggbb,#rrggbb[,...]` in glance.ini.
struct Scheme { const wchar_t *name, *label; int n; float pos[5]; DWORD hex[5]; };
static const Scheme SCHEMES[] = {
    {L"teal", L"Teal", 3, {0, 50, 100}, {0x1E2A2F, 0x1F9486, 0x6EF2DE}},
    {L"ice", L"Ice", 3, {0, 50, 100}, {0x252B35, 0x7D8794, 0xF2F5FA}},
    {L"blue", L"Blue", 3, {0, 50, 100}, {0x1C2430, 0x2E73C2, 0x8CCBFF}},
    {L"violet", L"Violet", 3, {0, 50, 100}, {0x221F2E, 0x6D5CBF, 0xD3C6FF}},
    {L"amber", L"Amber", 3, {0, 50, 100}, {0x2A2419, 0xB9821C, 0xFFD166}},
    {L"traffic", L"Traffic light", 4, {0, 25, 65, 100}, {0x24302A, 0x3FBF6A, 0xE8C547, 0xF0524F}},
    {L"heat", L"Heat", 5, {0, 30, 60, 82, 100}, {0x3A4856, 0x34B6A6, 0xF2B840, 0xFF623A, 0xFF3456}},
};
enum { NSCHEME = sizeof SCHEMES / sizeof SCHEMES[0] };
static int cs_n;
static float cs_pos[5];
static COLORREF cs_col[5];
static int hexval(wchar_t c) {
    if (c >= L'0' && c <= L'9') return c - L'0';
    c |= 32;
    return c >= L'a' && c <= L'f' ? c - L'a' + 10 : -1;
}
static void set_scheme(const wchar_t* s) {
    for (int i = 0; i < NSCHEME; ++i)
        if (!lstrcmpiW(s, SCHEMES[i].name)) {
            cs_n = SCHEMES[i].n;
            for (int k = 0; k < cs_n; ++k) { cs_pos[k] = SCHEMES[i].pos[k]; cs_col[k] = HEX(SCHEMES[i].hex[k]); }
            return;
        }
    int n = 0;  // custom: two to five colours, evenly spaced
    for (const wchar_t* p = s; *p && n < 5;) {
        while (*p == L' ' || *p == L',' || *p == L'#') ++p;
        DWORD v = 0;
        int d = 0;
        while (d < 6 && hexval(*p) >= 0) { v = v * 16 + (DWORD)hexval(*p++); ++d; }
        if (d != 6) break;
        cs_col[n++] = HEX(v);
    }
    if (n < 2) { set_scheme(SCHEMES[0].name); return; }
    cs_n = n;
    for (int k = 0; k < n; ++k) cs_pos[k] = 100.0f * k / (n - 1);
}
static COLORREF cpu_color(float p) {
    if (p <= cs_pos[0]) return cs_col[0];
    for (int i = 1; i < cs_n; ++i)
        if (p <= cs_pos[i]) return mix(cs_col[i - 1], cs_col[i], (p - cs_pos[i - 1]) / (cs_pos[i] - cs_pos[i - 1]));
    return cs_col[cs_n - 1];
}
static bool contains_ci(const wchar_t* hay, const wchar_t* needle) {
    wchar_t a[NAME], b[128];
    lstrcpynW(a, hay, NAME); lstrcpynW(b, needle, 128);
    CharLowerBuffW(a, (DWORD)lstrlenW(a)); CharLowerBuffW(b, (DWORD)lstrlenW(b));
    return wcsstr(a, b) != nullptr;
}

// ---------------------------------------------------------------- settings (glance.ini)
struct Cfg {
    int edge, size, interval, speed, display, graphs, readouts, ram, gpu, disks, net, taskmgr;
    wchar_t adapter[128], gpu_adapter[128], cpu_colors[96];
};
static Cfg cfg;
static wchar_t ini[MAX_PATH + 32];
static const wchar_t* const EDGE[4] = {L"top", L"bottom", L"left", L"right"};

static void ini_locate() {  // next to the exe when writable (portable), else %APPDATA%\Glance
    DWORD n = GetModuleFileNameW(nullptr, ini, MAX_PATH);
    while (n && ini[n - 1] != L'\\') --n;
    lstrcpyW(ini + n, L"glance.ini");
    HANDLE h = CreateFileW(ini, GENERIC_READ | GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr,
                           OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (h != INVALID_HANDLE_VALUE) { CloseHandle(h); return; }
    n = GetEnvironmentVariableW(L"APPDATA", ini, MAX_PATH);
    if (!n || n >= MAX_PATH) return;
    lstrcatW(ini, L"\\Glance");
    CreateDirectoryW(ini, nullptr);
    lstrcatW(ini, L"\\glance.ini");
}
static int geti(const wchar_t* k, int def, int lo, int hi) { return clampi((int)GetPrivateProfileIntW(L"glance", k, def, ini), lo, hi); }
static void puti(const wchar_t* k, int v) { wchar_t b[16]; wsprintfW(b, L"%d", v); WritePrivateProfileStringW(L"glance", k, b, ini); }
static void load_cfg() {
    wchar_t s[16];
    GetPrivateProfileStringW(L"glance", L"edge", L"top", s, 16, ini);
    cfg.edge = 0;
    for (int i = 0; i < 4; ++i) if (!lstrcmpiW(s, EDGE[i])) cfg.edge = i;
    cfg.size = geti(L"size", 40, 24, 200);
    cfg.interval = geti(L"interval", 1000, 100, 5000);
    cfg.speed = geti(L"graph_step", 1000, 100, 60000);
    cfg.display = geti(L"display", 0, 0, 16);
    cfg.graphs = geti(L"graphs", 1, 0, 1);
    cfg.readouts = geti(L"readouts", 1, 0, 1);
    cfg.ram = geti(L"ram", 1, 0, 1);
    cfg.gpu = geti(L"gpu", 1, 0, 1);
    cfg.disks = geti(L"disks", 1, 0, 1);
    cfg.net = geti(L"network", 1, 0, 1);
    cfg.taskmgr = geti(L"taskmgr", 1, 0, 1);
    GetPrivateProfileStringW(L"glance", L"cpu_colors", SCHEMES[0].name, cfg.cpu_colors, 96, ini);
    GetPrivateProfileStringW(L"glance", L"adapter", L"", cfg.adapter, 128, ini);
    GetPrivateProfileStringW(L"glance", L"gpu_adapter", L"", cfg.gpu_adapter, 128, ini);
    set_scheme(cfg.cpu_colors);
}
static void save_cfg() {
    WritePrivateProfileStringW(L"glance", L"edge", EDGE[cfg.edge], ini);
    puti(L"size", cfg.size); puti(L"interval", cfg.interval); puti(L"graph_step", cfg.speed); puti(L"display", cfg.display);
    puti(L"graphs", cfg.graphs); puti(L"readouts", cfg.readouts); puti(L"ram", cfg.ram); puti(L"gpu", cfg.gpu);
    puti(L"disks", cfg.disks); puti(L"network", cfg.net); puti(L"taskmgr", cfg.taskmgr);
    WritePrivateProfileStringW(L"glance", L"cpu_colors", cfg.cpu_colors, ini);
    WritePrivateProfileStringW(L"glance", L"adapter", cfg.adapter, ini);
    WritePrivateProfileStringW(L"glance", L"gpu_adapter", cfg.gpu_adapter, ini);
}

// ---------------------------------------------------------------- readings + history
// Every reading is fed in; once per graph step the average of those readings becomes one graph point.
// So the refresh rate (how live the numbers are) and graph speed (how fast it scrolls) are independent.
struct Series { float v[HIST]; int head, n, cnt; float cur, acc, peak, gpeak; };
static void feed(Series& s, float x) { s.cur = x; s.acc += x; ++s.cnt; if (x > s.peak) s.peak = x; }
static void commit(Series& s) {
    if (!s.cnt) return;
    float x = s.acc / (float)s.cnt;
    s.acc = 0; s.cnt = 0;
    s.head = (s.head + 1) % HIST; s.v[s.head] = x;
    if (s.n < HIST) ++s.n;
    if (x > s.gpeak) s.gpeak = x;
}
static float last(const Series& s) { return s.cur; }
static float ago(const Series& s, int k) { int i = s.head - k; return s.v[i < 0 ? i + HIST : i]; }

static int ncpu, np, ne, gbase[64];
static bool hybrid;
static BYTE cls[MAXCPU], pcls;
static short order[MAXCPU];  // tile order: performance class first
static float cpu[MAXCPU], cpu_total;
static ULONGLONG ram_used, ram_total;
static Series ram;
struct Disk { wchar_t name[NAME]; Series act, rd, wr; };
static Disk disk[MAXDISK], disk_tmp[MAXDISK];
static int ndisk;
static wchar_t net_name[NAME];
static Series net_dn, net_up;
static bool net_ok;
static struct { wchar_t name[NAME]; double total; } ad[MAXAD];
static int nad;
static bool gpu_found;
static wchar_t gpu_name[128], gpu_luid[40];
static ULONGLONG gpu_vram, gpu_shared, gpu_mem_used;
static Series gpu_util, gpu_mem, gpu_dec;
static bool gpu_dedicated() { return gpu_vram >= (512ull << 20); }  // integrated GPUs: use shared memory

static float cpu_mean(bool perf) {
    float s = 0; int n = 0;
    for (int i = 0; i < ncpu; ++i) if ((cls[i] == pcls) == perf) { s += cpu[i]; ++n; }
    return n ? s / n : 0;
}

static void detect_cpu() {
    WORD ng = GetActiveProcessorGroupCount();
    int base = 0;
    for (WORD g = 0; g < ng && g < 64; ++g) { gbase[g] = base; base += (int)GetActiveProcessorCount(g); }
    ncpu = clampi(base, 1, MAXCPU);
    DWORD len = 0;
    GetLogicalProcessorInformationEx(RelationProcessorCore, nullptr, &len);
    BYTE* b = len ? (BYTE*)HeapAlloc(GetProcessHeap(), 0, len) : nullptr;
    int lo = 255, hi = 0;
    if (b && GetLogicalProcessorInformationEx(RelationProcessorCore, (PSYSTEM_LOGICAL_PROCESSOR_INFORMATION_EX)b, &len)) {
        for (DWORD off = 0; off < len;) {
            auto* p = (PSYSTEM_LOGICAL_PROCESSOR_INFORMATION_EX)(b + off);
            BYTE ec = p->Processor.EfficiencyClass;
            for (WORD gi = 0; gi < p->Processor.GroupCount; ++gi) {
                const GROUP_AFFINITY& ga = p->Processor.GroupMask[gi];
                for (int bit = 0; bit < 64; ++bit)
                    if ((ga.Mask >> bit) & 1) {
                        int idx = (ga.Group < 64 ? gbase[ga.Group] : 0) + bit;
                        if (idx < ncpu) cls[idx] = ec;
                    }
            }
            if (ec < lo) lo = ec;
            if (ec > hi) hi = ec;
            off += p->Size;
        }
    }
    if (b) HeapFree(GetProcessHeap(), 0, b);
    hybrid = hi > lo;
    pcls = (BYTE)(hybrid ? hi : 0);
    if (!hybrid) for (int i = 0; i < ncpu; ++i) cls[i] = 0;
    int k = 0; np = ne = 0;
    for (int i = 0; i < ncpu; ++i) if (cls[i] == pcls) { order[k++] = (short)i; ++np; }
    for (int i = 0; i < ncpu; ++i) if (cls[i] != pcls) { order[k++] = (short)i; ++ne; }
}

// Pick the GPU to show: `gpu_adapter` from the settings, else the one with the most dedicated memory.
// DXGI is only loaded for this lookup (name, memory size and the LUID that keys its PDH counters).
static void detect_gpu() {
    static const GUID IID_F1 = {0x770aae78, 0xf26f, 0x4dba, {0xa8, 0x29, 0x25, 0x3c, 0x83, 0xd1, 0xb3, 0x87}};
    typedef HRESULT(WINAPI * Create)(REFIID, void**);
    wchar_t before[40];
    lstrcpyW(before, gpu_luid);
    gpu_found = false;
    gpu_vram = 0;
    HMODULE m = LoadLibraryW(L"dxgi.dll");
    if (!m) return;
    Create create = (Create)(void*)GetProcAddress(m, "CreateDXGIFactory1");
    IDXGIFactory1* f = nullptr;
    if (create && SUCCEEDED(create(IID_F1, (void**)&f))) {
        IDXGIAdapter1* a = nullptr;
        for (UINT i = 0; SUCCEEDED(f->EnumAdapters1(i, &a)); ++i) {
            DXGI_ADAPTER_DESC1 d;
            if (SUCCEEDED(a->GetDesc1(&d)) && !(d.Flags & DXGI_ADAPTER_FLAG_SOFTWARE)) {
                bool pick = cfg.gpu_adapter[0] ? !gpu_found && contains_ci(d.Description, cfg.gpu_adapter)
                                               : !gpu_found || d.DedicatedVideoMemory > gpu_vram;
                if (pick) {
                    gpu_found = true;
                    gpu_vram = d.DedicatedVideoMemory;
                    gpu_shared = d.SharedSystemMemory;
                    lstrcpynW(gpu_name, d.Description, 128);
                    wsprintfW(gpu_luid, L"luid_0x%08X_0x%08X", (unsigned)d.AdapterLuid.HighPart, (unsigned)d.AdapterLuid.LowPart);
                }
            }
            a->Release();
        }
        f->Release();
    }
    FreeLibrary(m);
    if (lstrcmpW(before, gpu_luid)) {
        ZeroMemory(&gpu_util, sizeof gpu_util); ZeroMemory(&gpu_mem, sizeof gpu_mem); ZeroMemory(&gpu_dec, sizeof gpu_dec);
    }
}

// PDH: one query of wildcard counters, read as arrays (English names work on any locale).
// Counters for hidden widgets are removed from the query, so hiding something also stops collecting it.
enum { C_CPU, C_IDLE, C_RD, C_WR, C_RX, C_TX, C_GPU, C_GDED, C_GSHR, C_N };
static const wchar_t* const PATH[C_N] = {
    nullptr, L"\\PhysicalDisk(*)\\% Idle Time", L"\\PhysicalDisk(*)\\Disk Read Bytes/sec",
    L"\\PhysicalDisk(*)\\Disk Write Bytes/sec", L"\\Network Interface(*)\\Bytes Received/sec",
    L"\\Network Interface(*)\\Bytes Sent/sec", L"\\GPU Engine(*)\\Utilization Percentage",
    L"\\GPU Adapter Memory(*)\\Dedicated Usage", L"\\GPU Adapter Memory(*)\\Shared Usage"};
static PDH_HQUERY query;
static PDH_HCOUNTER ctr[C_N];
static struct { BYTE* p; DWORD cap; } abuf[C_N];
static bool cpu_info;
typedef PDH_FMT_COUNTERVALUE_ITEM_W Item;

static void sync_counters() {
    if (!query) return;
    const bool want[4] = {true, cfg.disks != 0, cfg.net != 0, cfg.gpu && gpu_found};
    bool added = false;
    for (int k = C_IDLE; k < C_N; ++k) {
        bool w = want[k <= C_WR ? 1 : k <= C_TX ? 2 : 3];
        if (w && !ctr[k]) {
            if (PdhAddEnglishCounterW(query, PATH[k], 0, &ctr[k]) == ERROR_SUCCESS) added = true; else ctr[k] = nullptr;
        } else if (!w && ctr[k]) {
            PdhRemoveCounter(ctr[k]);
            ctr[k] = nullptr;
        }
    }
    if (added) PdhCollectQueryData(query);  // prime new rate counters
}
static void pdh_open() {
    if (PdhOpenQueryW(nullptr, 0, &query) != ERROR_SUCCESS) { query = nullptr; return; }
    cpu_info = PdhAddEnglishCounterW(query, L"\\Processor Information(*)\\% Processor Utility", 0, &ctr[C_CPU]) == ERROR_SUCCESS;
    if (!cpu_info && PdhAddEnglishCounterW(query, L"\\Processor(*)\\% Processor Time", 0, &ctr[C_CPU]) != ERROR_SUCCESS)
        ctr[C_CPU] = nullptr;
    sync_counters();
    PdhCollectQueryData(query);
}
static Item* items(int k, DWORD* n) {
    *n = 0;
    if (!ctr[k]) return nullptr;
    const DWORD fmt = PDH_FMT_DOUBLE | PDH_FMT_NOCAP100;
    DWORD size = 0, count = 0;
    if (PdhGetFormattedCounterArrayW(ctr[k], fmt, &size, &count, nullptr) != (PDH_STATUS)PDH_MORE_DATA) return nullptr;
    if (size > abuf[k].cap) {
        BYTE* p = abuf[k].p ? (BYTE*)HeapReAlloc(GetProcessHeap(), 0, abuf[k].p, size)
                            : (BYTE*)HeapAlloc(GetProcessHeap(), 0, size);
        if (!p) return nullptr;
        abuf[k].p = p; abuf[k].cap = size;
    }
    if (PdhGetFormattedCounterArrayW(ctr[k], fmt, &size, &count, (Item*)abuf[k].p) != ERROR_SUCCESS) return nullptr;
    *n = count;
    return (Item*)abuf[k].p;
}
static bool valid(const Item& it) {
    return it.FmtValue.CStatus == PDH_CSTATUS_VALID_DATA || it.FmtValue.CStatus == PDH_CSTATUS_NEW_DATA;
}
static float val(const Item& it) { double v = it.FmtValue.doubleValue; return valid(it) && v > 0 ? (float)v : 0; }
static int find(const Item* a, DWORD n, const wchar_t* name, DWORD hint) {
    if (!a) return -1;
    if (hint < n && !lstrcmpW(a[hint].szName, name)) return (int)hint;
    for (DWORD i = 0; i < n; ++i) if (!lstrcmpW(a[i].szName, name)) return (int)i;
    return -1;
}
static int num(const wchar_t*& s) { int v = 0; while (*s >= L'0' && *s <= L'9') v = v * 10 + (*s++ - L'0'); return v; }

static void sample_cpu() {
    DWORD n;
    Item* a = items(C_CPU, &n);
    for (DWORD i = 0; a && i < n; ++i) {
        if (!valid(a[i])) continue;
        float v = val(a[i]);
        if (v > 100) v = 100;
        const wchar_t* s = a[i].szName;
        if (!lstrcmpW(s, L"_Total")) { cpu_total = v; continue; }
        if (wcschr(s, L'_')) continue;
        int idx;
        if (cpu_info) {  // "group,number"
            int g = num(s);
            if (*s++ != L',') continue;
            idx = (g < 64 ? gbase[g] : 0) + num(s);
        } else idx = num(s);
        if (idx < ncpu) cpu[idx] = v;
    }
}
static void sample_disks() {
    DWORD ni, nr, nw;
    Item *I = items(C_IDLE, &ni), *R = items(C_RD, &nr), *W = items(C_WR, &nw);
    const wchar_t* names[MAXDISK];
    float act[MAXDISK], rd[MAXDISK], wr[MAXDISK];
    int cnt = 0;
    for (DWORD i = 0; I && i < ni && cnt < MAXDISK; ++i) {
        if (I[i].szName[0] == L'_') continue;
        float idle = valid(I[i]) ? val(I[i]) : 100;
        act[cnt] = 100 - (idle > 100 ? 100 : idle);
        int r = find(R, nr, I[i].szName, i), w = find(W, nw, I[i].szName, i);
        rd[cnt] = r >= 0 ? val(R[r]) : 0;
        wr[cnt] = w >= 0 ? val(W[w]) : 0;
        names[cnt++] = I[i].szName;
    }
    bool same = cnt == ndisk;
    for (int k = 0; same && k < cnt; ++k) same = !lstrcmpW(names[k], disk[k].name);
    if (!same) {  // drives came or went: keep history for the ones that stayed
        for (int k = 0; k < cnt; ++k) {
            int j = 0;
            while (j < ndisk && lstrcmpW(disk[j].name, names[k])) ++j;
            if (j < ndisk) disk_tmp[k] = disk[j];
            else { ZeroMemory(&disk_tmp[k], sizeof(Disk)); lstrcpynW(disk_tmp[k].name, names[k], NAME); }
        }
        CopyMemory(disk, disk_tmp, sizeof(Disk) * cnt);
        ndisk = cnt;
    }
    for (int k = 0; k < cnt; ++k) { feed(disk[k].act, act[k]); feed(disk[k].rd, rd[k]); feed(disk[k].wr, wr[k]); }
}
static void sample_net() {
    DWORD nr, nt;
    Item *R = items(C_RX, &nr), *T = items(C_TX, &nt);
    int pick = -1;
    double best = -1;
    for (DWORD i = 0; R && i < nr; ++i) {
        const wchar_t* nm = R[i].szName;
        if (cfg.adapter[0]) {  // configured adapter: first name containing the text
            if (contains_ci(nm, cfg.adapter)) { pick = (int)i; break; }
            continue;
        }
        int t = find(T, nt, nm, i);  // otherwise: the adapter that has moved the most bytes
        double bytes = ((double)val(R[i]) + (t >= 0 ? val(T[t]) : 0)) * cfg.interval / 1000.0;
        int j = 0;
        while (j < nad && lstrcmpW(ad[j].name, nm)) ++j;
        if (j == nad) {
            if (nad == MAXAD) continue;
            lstrcpynW(ad[nad].name, nm, NAME);
            ad[nad++].total = 0;
        }
        ad[j].total += bytes;
        if (ad[j].total > best) { best = ad[j].total; pick = (int)i; }
    }
    net_ok = pick >= 0;
    if (!net_ok) return;
    if (lstrcmpW(net_name, R[pick].szName)) {
        lstrcpynW(net_name, R[pick].szName, NAME);
        ZeroMemory(&net_dn, sizeof net_dn);
        ZeroMemory(&net_up, sizeof net_up);
    }
    int t = find(T, nt, R[pick].szName, (DWORD)pick);
    feed(net_dn, val(R[pick]));
    feed(net_up, t >= 0 ? val(T[t]) : 0);
}
static void sample_gpu() {  // like Task Manager: busiest engine wins; per-engine load is summed over processes
    DWORD n;
    Item* a = items(C_GPU, &n);
    if (!a) return;
    float eng[64] = {};
    bool dec[64] = {};
    for (DWORD i = 0; i < n; ++i) {
        const wchar_t* s = a[i].szName;  // pid_<p>_luid_0x..._0x..._phys_0_eng_<k>_engtype_<type>
        if (!wcsstr(s, gpu_luid)) continue;
        const wchar_t* e = wcsstr(s, L"_eng_");
        if (!e) continue;
        e += 5;
        int k = num(e);
        if (k >= 64) continue;
        eng[k] += val(a[i]);
        if (wcsstr(e, L"VideoDecode")) dec[k] = true;
    }
    float util = 0, vd = 0;
    for (int k = 0; k < 64; ++k) {
        if (eng[k] > util) util = eng[k];
        if (dec[k] && eng[k] > vd) vd = eng[k];
    }
    feed(gpu_util, util > 100 ? 100 : util);
    feed(gpu_dec, vd > 100 ? 100 : vd);
    Item* m = items(gpu_dedicated() ? C_GDED : C_GSHR, &n);
    double used = 0;
    for (DWORD i = 0; m && i < n; ++i) if (wcsstr(m[i].szName, gpu_luid)) used += m[i].FmtValue.doubleValue;
    ULONGLONG total = gpu_dedicated() ? gpu_vram : gpu_shared;
    gpu_mem_used = (ULONGLONG)used;
    feed(gpu_mem, total ? (float)(100.0 * used / (double)total) : 0);
}
static void commit_all() {  // one graph step: average everything read since the last step
    commit(ram); commit(gpu_util); commit(gpu_mem); commit(gpu_dec);
    for (int k = 0; k < ndisk; ++k) { commit(disk[k].act); commit(disk[k].rd); commit(disk[k].wr); }
    commit(net_dn); commit(net_up);
}
static void sample() {
    if (!query || PdhCollectQueryData(query) != ERROR_SUCCESS) return;
    sample_cpu();
    MEMORYSTATUSEX ms;
    ms.dwLength = sizeof ms;
    if (GlobalMemoryStatusEx(&ms) && ms.ullTotalPhys) { ram_total = ms.ullTotalPhys; ram_used = ms.ullTotalPhys - ms.ullAvailPhys; }
    feed(ram, ram_total ? (float)(100.0 * (double)ram_used / (double)ram_total) : 0);
    if (ctr[C_GPU]) sample_gpu();
    if (ctr[C_IDLE]) sample_disks();
    if (ctr[C_RX]) sample_net();
}

// ---------------------------------------------------------------- number formats (no CRT printf)
static const wchar_t UNIT[] = L"BKMGT";
static int unit_of(double& v) { int u = 0; while (v >= 999.5 && u < 4) { v /= 1024; ++u; } return u; }
static void fmt_rate(wchar_t* o, double b) {  // compact binary: 0, 512B, 4.0K, 56M
    if (b < 0.5) { lstrcpyW(o, L"0"); return; }
    int u = unit_of(b);
    if (u && b < 9.95) { int t = (int)(b * 10 + 0.5); wsprintfW(o, L"%d.%d%c", t / 10, t % 10, UNIT[u]); }
    else wsprintfW(o, L"%d%c", (int)(b + 0.5), UNIT[u]);
}
static void fmt_long(wchar_t* o, double b) {  // tooltip: 12.4 MB/s
    int u = unit_of(b), t = (int)(b * 10 + 0.5);
    if (!u) wsprintfW(o, L"%d B/s", (int)(b + 0.5));
    else wsprintfW(o, L"%d.%d %cB/s", t / 10, t % 10, UNIT[u]);
}
static int tenths_gib(ULONGLONG b) { return (int)((double)b / 107374182.4 + 0.5); }

// ---------------------------------------------------------------- layout
struct Wd { int kind, idx; RECT ic, gx, tx, hit; };
static Wd wd[MAXW];
static int nwd;
static RECT dvd;
static bool hdiv;
static struct { bool hy; int side, cols, pw, ph, ew, eh, w, h; } grid;

static float readout_w(int kind) { static const float W[5] = {30, 56, 62, 72, 42}; return kind == K_CPU && hybrid ? 52 : W[kind]; }
static int readout_lines(int kind) { static const int L[5] = {1, 2, 3, 3, 2}; return kind == K_CPU && hybrid ? 3 : L[kind]; }

static void fit_grid(int band, int budget, bool vert) {
    int g = P1(2), n = ncpu;
    grid.hy = false;
    if (hybrid && !vert) {  // performance tiles above, efficiency tiles below (14/6 heights)
        int ph = (band - g) * 14 / 20, eh = band - g - ph;
        for (int e = 10; e >= 6; --e) {
            int ew = P1((float)e), pw = 2 * ew + g;
            int wp = np ? np * pw + (np - 1) * g : 0, we = ne ? ne * ew + (ne - 1) * g : 0;
            int w = wp > we ? wp : we;
            if (w <= budget) { grid.hy = true; grid.pw = pw; grid.ph = ph; grid.ew = ew; grid.eh = eh; grid.w = w; grid.h = band; return; }
        }
    }
    if (vert) {
        int s = P1(10), cols = clampi((budget + g) / (s + g), 1, n);
        s = (budget - (cols - 1) * g) / cols;
        if (s > P(22)) s = P(22);
        int rows = (n + cols - 1) / cols;
        grid.side = s; grid.cols = cols; grid.w = cols * s + (cols - 1) * g; grid.h = rows * s + (rows - 1) * g;
        return;
    }
    for (int r = 1;; ++r) {  // fewest rows of squares that fit the width budget
        int s = (band - (r - 1) * g) / r, cols = (n + r - 1) / r, w = cols * s + (cols - 1) * g;
        if (w <= budget || s <= P1(6) || r >= n) { grid.side = s; grid.cols = cols; grid.w = w; grid.h = r * s + (r - 1) * g; return; }
    }
}

static void layout(int W, int H) {
    nwd = 0; hdiv = false;
    auto add = [](int kind, int idx) { wd[nwd].kind = kind; wd[nwd].idx = idx; ++nwd; };
    add(K_CPU, 0);
    if (cfg.ram) add(K_RAM, 0);
    if (cfg.gpu && gpu_found) add(K_GPU, 0);
    if (cfg.disks) for (int k = 0; k < ndisk; ++k) add(K_DISK, k);
    if (cfg.net) add(K_NET, 0);
    int ndev = nwd - 1, ico = P1(12), band = P1(22), rgap = P(6);
    if (cfg.edge < 2) {  // horizontal: [icon][graphic][readout] per widget, CPU | devices
        int m = P(10.67f), col = P(21.33f), ig = P(6.67f), wg = cfg.readouts ? P(5.33f) : P(10.67f);
        int top = (H - band) / 2, budget = W * 3 / 10;
        if (budget > P(190)) budget = P(190);
        fit_grid(band, budget, false);
        int fixed = 2 * m + grid.w + (nwd - 1) * wg + (ndev ? wg + 1 : 0);
        for (int i = 0; i < nwd; ++i) fixed += col + ig + (cfg.readouts ? rgap + P(readout_w(wd[i].kind)) : 0);
        int gw = ndev ? (W - fixed) / ndev : 0;
        if (gw < P(24)) gw = P(24);
        int x = m;
        for (int i = 0; i < nwd; ++i) {
            Wd& w = wd[i];
            int x0 = x, gwi = w.kind == K_CPU ? grid.w : gw;
            w.ic = {x + (col - ico) / 2, (H - ico) / 2, x + (col - ico) / 2 + ico, (H - ico) / 2 + ico};
            x += col + ig;
            w.gx = {x, top, x + gwi, top + band};
            x += gwi;
            if (cfg.readouts) { x += rgap; w.tx = {x, top, x + P(readout_w(w.kind)), top + band}; x = w.tx.right; }
            w.hit = {x0 - wg / 2, 0, x + wg / 2, H};
            if (i + 1 < nwd) {
                x += wg;
                if (w.kind == K_CPU) { int dh = P(20); dvd = {x, (H - dh) / 2, x + 1, (H - dh) / 2 + dh}; hdiv = true; x += 1 + wg; }
            }
        }
    } else {  // vertical: widgets stacked, each icon / graphic / readout
        int m = P(5), x0 = m, x1 = W - m, iw = x1 - x0, y = P(10);
        fit_grid(0, iw, true);
        for (int i = 0; i < nwd; ++i) {
            Wd& w = wd[i];
            int y0 = y;
            w.ic = {(W - ico) / 2, y, (W - ico) / 2 + ico, y + ico};
            y += ico + P(6);
            if (w.kind == K_CPU) { int l = x0 + (iw - grid.w) / 2; w.gx = {l, y, l + grid.w, y + grid.h}; y += grid.h; }
            else { w.gx = {x0, y, x1, y + band}; y += band; }
            if (cfg.readouts) {  // one readout part per line
                int lh = P(13) + (readout_lines(w.kind) - 1) * P(11);
                y += P(4); w.tx = {x0, y, x1, y + lh}; y += lh;
            }
            w.hit = {0, y0 - P(6), W, y + P(6)};
            y += P(12);
            if (w.kind == K_CPU && i + 1 < nwd) { dvd = {x0 + iw / 4, y, x1 - iw / 4, y + 1}; hdiv = true; y += 1 + P(12); }
        }
    }
}

// ---------------------------------------------------------------- drawing (GDI, stock DC brush/pen)
static HFONT f1, f2;
static int fkey;
static void fonts(HDC dc) {
    int key = P1(11) * 1000 + P1(9);
    if (f1 && key == fkey) return;
    static const wchar_t* const faces[2] = {L"Cascadia Mono", L"Consolas"};
    for (int i = 0; i < 2; ++i) {
        if (f1) { DeleteObject(f1); DeleteObject(f2); }
        f1 = CreateFontW(-P1(11), 0, 0, 0, FW_SEMIBOLD, 0, 0, 0, DEFAULT_CHARSET, OUT_TT_PRECIS, CLIP_DEFAULT_PRECIS,
                         CLEARTYPE_QUALITY, FIXED_PITCH | FF_MODERN, faces[i]);
        f2 = CreateFontW(-P1(9), 0, 0, 0, FW_NORMAL, 0, 0, 0, DEFAULT_CHARSET, OUT_TT_PRECIS, CLIP_DEFAULT_PRECIS,
                         CLEARTYPE_QUALITY, FIXED_PITCH | FF_MODERN, faces[i]);
        wchar_t got[LF_FACESIZE];
        HGDIOBJ o = SelectObject(dc, f1);
        GetTextFaceW(dc, LF_FACESIZE, got);
        SelectObject(dc, o);
        if (!lstrcmpiW(got, faces[i])) break;
    }
    fkey = key;
}
static void fill(HDC dc, const RECT& r, COLORREF c) {
    SetDCBrushColor(dc, c);
    FillRect(dc, &r, (HBRUSH)GetStockObject(DC_BRUSH));
}
static void rrect(HDC dc, RECT r, int rad, COLORREF c) {
    int w = r.right - r.left, h = r.bottom - r.top;
    if (w <= 0 || h <= 0) return;
    if (rad > w / 2) rad = w / 2;
    if (rad > h / 2) rad = h / 2;
    if (rad <= 2) {  // small radii: pixel-exact stepped corners (GDI ellipses look octagonal here)
        if (rad < 1) { fill(dc, r, c); return; }
        RECT a = {r.left + 1, r.top, r.right - 1, r.bottom}, b = {r.left, r.top + 1, r.right, r.bottom - 1};
        if (rad == 2) { a.left++; a.right--; b.top++; b.bottom--; RECT m = {r.left + 1, r.top + 1, r.right - 1, r.bottom - 1}; fill(dc, m, c); }
        fill(dc, a, c); fill(dc, b, c);
        return;
    }
    SetDCBrushColor(dc, c);
    HGDIOBJ op = SelectObject(dc, GetStockObject(NULL_PEN)), ob = SelectObject(dc, GetStockObject(DC_BRUSH));
    RoundRect(dc, r.left, r.top, r.right + 1, r.bottom + 1, rad * 2, rad * 2);
    SelectObject(dc, op); SelectObject(dc, ob);
}
static int text(HDC dc, int x, int y, const wchar_t* s, COLORREF c, const RECT& clip) {
    int n = lstrlenW(s);
    SIZE sz;
    SetTextColor(dc, c);
    ExtTextOutW(dc, x, y, ETO_CLIPPED, &clip, s, (UINT)n, nullptr);
    GetTextExtentPoint32W(dc, s, n, &sz);
    return x + sz.cx;
}

static void icon(HDC dc, int kind, const RECT& r) {  // pixel glyphs on a 12x12 grid, integer-scaled
    int u = (int)(U + 0.3f);
    if (u < 1) u = 1;
    int x = r.left + (r.right - r.left - 12 * u) / 2, y = r.top + (r.bottom - r.top - 12 * u) / 2;
    COLORREF c = C_ICON[kind];
    auto px = [&](int l, int t, int rr, int b) { RECT q = {x + l * u, y + t * u, x + (rr + 1) * u, y + (b + 1) * u}; fill(dc, q, c); };
    auto box = [&](int l, int t, int rr, int b) { px(l, t, rr, t); px(l, b, rr, b); px(l, t, l, b); px(rr, t, rr, b); };
    switch (kind) {
    case K_CPU:  // chip with two pins per side
        box(2, 2, 9, 9); px(4, 4, 7, 7);
        px(4, 0, 4, 1); px(7, 0, 7, 1); px(4, 10, 4, 11); px(7, 10, 7, 11);
        px(0, 4, 1, 4); px(0, 7, 1, 7); px(10, 4, 11, 4); px(10, 7, 11, 7);
        break;
    case K_RAM:  // memory stick
        box(0, 3, 11, 8); px(2, 5, 4, 6); px(7, 5, 9, 6);
        px(2, 9, 2, 10); px(4, 9, 4, 10); px(7, 9, 7, 10); px(9, 9, 9, 10);
        break;
    case K_GPU:  // graphics card: fan, vent, bracket and connector
        box(0, 1, 9, 8); box(2, 3, 5, 6); px(7, 3, 7, 6);
        px(10, 1, 11, 1); px(11, 1, 11, 10); px(2, 10, 6, 10);
        break;
    case K_DISK:  // drive with activity light
        box(0, 2, 11, 9); px(0, 5, 11, 5); px(8, 7, 9, 7);
        break;
    default:  // network port
        box(1, 1, 10, 10); px(3, 3, 3, 5); px(5, 3, 6, 5); px(8, 3, 8, 5); px(4, 8, 7, 10);
    }
}

static POINT pts[HIST + 3];
// rate_floor 0: a percentage (full scale 100). Otherwise a rate, scaled to the fastest seen (at least rate_floor).
static void meter(HDC dc, RECT r, const Series& s, float rate_floor, const Hue& h) {
    int w = r.right - r.left, ht = r.bottom - r.top, rad = P(1.33f);
    float full = 100;
    if (rate_floor > 0) { full = cfg.graphs ? s.gpeak : s.peak; if (full < rate_floor) full = rate_floor; }
    rrect(dc, r, rad, h.track);
    if (w < 2 || ht < 1) return;
    if (!cfg.graphs) {  // bar: length = value, brighter when fuller
        float f = clampf(last(s) / full);
        RECT b = r;
        b.right = r.left + (int)(f * w + 0.5f);
        if (b.right > b.left) rrect(dc, b, rad, mix(h.track, h.fill, 0.7f + 0.3f * f));
        return;
    }
    if (!s.n) return;
    int step = P1(2), need = (w + HIST - 2) / (HIST - 1);  // newest sample at the right edge
    if (step < need) step = need;
    int m = s.n, cap = w / step + 1, k = 0, x = r.right;
    if (m > cap) m = cap;
    if (ht < P(8)) {  // thin band: a heat strip (16 shades, equal neighbours merged into one rectangle)
        int rq = 0, rl = 0, rr = 0;
        for (int i = 0; i <= m; ++i) {
            int q = 0, right = r.right - i * step, left = right - step;
            if (left < r.left) left = r.left;
            if (i < m) q = (int)(clampf(ago(s, i) / full) * 16 + 0.5f);
            if (i < m && q == rq) { rl = left; continue; }
            if (rq) { RECT c = {rl, r.top, rr, r.bottom}; fill(dc, c, mix(h.track, h.fill, 0.25f + 0.75f * (float)rq / 16)); }
            rq = q; rl = left; rr = right;
        }
        return;
    }
    for (int i = 0; i < m; ++i) {
        x = r.right - i * step;
        if (x < r.left) x = r.left;
        pts[k++] = {x, r.bottom - (int)(clampf(ago(s, i) / full) * ht + 0.5f)};
    }
    pts[k] = {x, r.bottom};
    pts[k + 1] = {r.right, r.bottom};
    int sd = SaveDC(dc);
    IntersectClipRect(dc, r.left, r.top, r.right, r.bottom);
    SetDCBrushColor(dc, mix(h.track, h.fill, 0.42f));
    SelectObject(dc, GetStockObject(NULL_PEN));
    SelectObject(dc, GetStockObject(DC_BRUSH));
    Polygon(dc, pts, k + 2);
    HPEN pen = CreatePen(PS_SOLID, P1(1), h.fill);
    SelectObject(dc, pen);
    Polyline(dc, pts, k);
    RestoreDC(dc, sd);
    DeleteObject(pen);
}

static void draw_cpu(HDC dc, const RECT& g) {
    int gap = P1(2), top = g.top + ((g.bottom - g.top) - grid.h) / 2, kp = 0, ke = 0;
    for (int k = 0; k < ncpu; ++k) {
        int i = order[k];
        RECT t;
        int rad;
        if (grid.hy && cls[i] != pcls) {
            t.left = g.left + ke++ * (grid.ew + gap); t.top = top + grid.ph + gap;
            t.right = t.left + grid.ew; t.bottom = t.top + grid.eh; rad = P(1.33f);
        } else if (grid.hy) {
            t.left = g.left + kp++ * (grid.pw + gap); t.top = top;
            t.right = t.left + grid.pw; t.bottom = top + grid.ph; rad = P(2.67f);
        } else {
            t.left = g.left + (k % grid.cols) * (grid.side + gap); t.top = top + (k / grid.cols) * (grid.side + gap);
            t.right = t.left + grid.side; t.bottom = t.top + grid.side; rad = grid.side / 7;
        }
        rrect(dc, t, rad, cpu_color(cpu[i]));
    }
}
static void draw_dev(HDC dc, const Wd& w) {
    RECT r = w.gx;
    int H = r.bottom - r.top;
    if (w.kind == K_RAM) { meter(dc, r, ram, 0, H_RAM); return; }
    if (w.kind == K_GPU || w.kind == K_DISK) {  // three bands in 10:4:4
        int h1 = H * 10 / 22, h2 = H * 4 / 22, gp = (H - h1 - 2 * h2) / 2;
        RECT a = r, b = r, c = r;
        a.bottom = r.top + h1;
        b.top = a.bottom + gp; b.bottom = b.top + h2;
        c.top = r.bottom - h2;
        if (w.kind == K_GPU) {  // load / memory / video decode
            meter(dc, a, gpu_util, 0, H_G3D);
            meter(dc, b, gpu_mem, 0, H_GMEM);
            meter(dc, c, gpu_dec, 0, H_GDEC);
            return;
        }
        const Disk& d = disk[w.idx];  // active / read / write
        meter(dc, a, d.act, 0, H_ACT);
        meter(dc, b, d.rd, DISK_FLOOR, H_RD);
        meter(dc, c, d.wr, DISK_FLOOR, H_WR);
        return;
    }
    int h1 = H * 14 / 22, gp = H * 2 / 22;  // network: download / upload in 14:6
    RECT a = r, b = r;
    a.bottom = r.top + h1;
    b.top = a.bottom + (gp < 1 ? 1 : gp);
    meter(dc, a, net_dn, NET_FLOOR, H_DN);
    meter(dc, b, net_up, NET_FLOOR, H_UP);
}
static void readout(HDC dc, const Wd& w) {  // value on top, details below (one per line on side edges)
    wchar_t s[3][24];
    COLORREF col[3];
    int n = 0;
    bool vert = cfg.edge >= 2;
    auto seg = [&](COLORREF c) { col[n] = c; return s[n++]; };
    switch (w.kind) {
    case K_CPU:
        wsprintfW(seg(C_TXT), L"%d%%", pc(cpu_total));
        if (hybrid) { wsprintfW(seg(C_TXT2), L"P%d", pc(cpu_mean(true))); wsprintfW(seg(C_TXT2), L"E%d", pc(cpu_mean(false))); }
        break;
    case K_RAM: {
        int u = tenths_gib(ram_used), t = (int)((double)ram_total / 1073741824.0 + 0.5);
        wsprintfW(seg(H_RAM.text), L"%d%%", pc(last(ram)));
        if (vert) wsprintfW(seg(mix(H_RAM.text, C_BG, 0.2f)), L"%d.%dG", u / 10, u % 10);
        else wsprintfW(seg(mix(H_RAM.text, C_BG, 0.2f)), L"%d.%d/%dG", u / 10, u % 10, t);
        break;
    }
    case K_GPU: {
        int u = tenths_gib(gpu_mem_used);
        wsprintfW(seg(H_G3D.text), L"%d%%", pc(last(gpu_util)));
        wsprintfW(seg(H_GMEM.text), L"%d.%dG", u / 10, u % 10);
        wsprintfW(seg(H_GDEC.text), L"\x25B6%d%%", pc(last(gpu_dec)));
        break;
    }
    case K_DISK: {
        const Disk& d = disk[w.idx];
        wsprintfW(seg(H_ACT.text), L"%d%%", pc(last(d.act)));
        wchar_t* r = seg(H_RD.text); r[0] = L'R'; fmt_rate(r + 1, last(d.rd));
        wchar_t* x = seg(H_WR.text); x[0] = L'W'; fmt_rate(x + 1, last(d.wr));
        break;
    }
    default: {
        wchar_t* dn = seg(H_DN.text); dn[0] = 0x2193; fmt_rate(dn + 1, last(net_dn));
        wchar_t* up = seg(H_UP.text); up[0] = 0x2191; fmt_rate(up + 1, last(net_up));
    }
    }
    const RECT& c = w.tx;
    if (vert) {
        for (int i = 0; i < n; ++i) {
            SelectObject(dc, i == 0 && w.kind != K_NET ? f1 : f2);
            text(dc, c.left, c.top + P(9) + i * P(11), s[i], col[i], c);
        }
        return;
    }
    SelectObject(dc, f1);
    text(dc, c.left, c.top + P(9), s[0], col[0], c);
    SelectObject(dc, f2);
    for (int i = 1, x = c.left; i < n; ++i) x = text(dc, x, c.top + P(20), s[i], col[i], c) + P(5);
}
static void paint(HDC dc, int W, int H) {
    U = (cfg.edge < 2 ? H : W) / 40.0f;
    fonts(dc);
    RECT all = {0, 0, W, H}, edge = all;
    fill(dc, all, C_BG);
    switch (cfg.edge) {  // one-pixel border on the side facing the desktop
    case 0: edge.top = H - 1; break;
    case 1: edge.bottom = 1; break;
    case 2: edge.left = W - 1; break;
    default: edge.right = 1;
    }
    fill(dc, edge, C_BORDER);
    layout(W, H);
    SetBkMode(dc, TRANSPARENT);
    SetTextAlign(dc, TA_BASELINE | TA_LEFT);
    HGDIOBJ of = SelectObject(dc, f1);
    for (int i = 0; i < nwd; ++i) {
        icon(dc, wd[i].kind, wd[i].ic);
        if (wd[i].kind == K_CPU) draw_cpu(dc, wd[i].gx); else draw_dev(dc, wd[i]);
        if (cfg.readouts) readout(dc, wd[i]);
    }
    if (hdiv) fill(dc, dvd, C_DIV);
    SelectObject(dc, of);
}

// ---------------------------------------------------------------- window, AppBar, tray, menu
static HWND hw, tip;
static HICON ico;
static UINT WM_TASKBAR;
static bool registered, fullscreen, warm;
static RECT bar;
static NOTIFYICONDATAW nid;
typedef HRESULT(WINAPI* DpiForMonitor)(HMONITOR, int, UINT*, UINT*);
static DpiForMonitor dpi_for_monitor;
static HMONITOR mons[16];
static int nmon;
static RECT tool_rc[MAXW];
static int ntool;
static wchar_t tipbuf[512];

static BOOL CALLBACK enum_mon(HMONITOR m, HDC, LPRECT, LPARAM) { if (nmon < 16) mons[nmon++] = m; return TRUE; }
static HMONITOR pick_monitor() {
    nmon = 0;
    EnumDisplayMonitors(nullptr, nullptr, enum_mon, 0);
    if (cfg.display >= 1 && cfg.display <= nmon) return mons[cfg.display - 1];
    POINT o = {0, 0};
    return MonitorFromPoint(o, MONITOR_DEFAULTTOPRIMARY);
}
static void thick(RECT& r, int t) {
    switch (cfg.edge) {
    case 0: r.bottom = r.top + t; break;
    case 1: r.top = r.bottom - t; break;
    case 2: r.right = r.left + t; break;
    default: r.left = r.right - t;
    }
}
static void ab_register() {
    APPBARDATA a = {};
    a.cbSize = sizeof a; a.hWnd = hw; a.uCallbackMessage = WM_APPBAR;
    registered = SHAppBarMessage(ABM_NEW, &a) != 0;
}
static void ab_place() {
    static const UINT ABE[4] = {ABE_TOP, ABE_BOTTOM, ABE_LEFT, ABE_RIGHT};
    HMONITOR mon = pick_monitor();
    MONITORINFO mi;
    mi.cbSize = sizeof mi;
    GetMonitorInfoW(mon, &mi);
    UINT dx = 96, dy = 96;
    if (dpi_for_monitor) dpi_for_monitor(mon, 0, &dx, &dy);
    int t = MulDiv(cfg.size, (int)dx, 96);
    APPBARDATA a = {};
    a.cbSize = sizeof a; a.hWnd = hw; a.uEdge = ABE[cfg.edge]; a.rc = mi.rcMonitor;
    thick(a.rc, t);
    if (registered) {
        SHAppBarMessage(ABM_QUERYPOS, &a);
        thick(a.rc, t);
        if (EqualRect(&a.rc, &bar)) return;  // settled: avoid notification ping-pong
        SHAppBarMessage(ABM_SETPOS, &a);
    }
    bar = a.rc;
    SetWindowPos(hw, fullscreen ? HWND_BOTTOM : HWND_TOPMOST, bar.left, bar.top, bar.right - bar.left,
                 bar.bottom - bar.top, SWP_NOACTIVATE | SWP_SHOWWINDOW);
    InvalidateRect(hw, nullptr, FALSE);
}

static bool startup_on() {
    return RegGetValueW(HKEY_CURRENT_USER, RUNKEY, L"Glance", RRF_RT_REG_SZ, nullptr, nullptr, nullptr) == ERROR_SUCCESS;
}
static void startup_set(bool on) {
    if (!on) { RegDeleteKeyValueW(HKEY_CURRENT_USER, RUNKEY, L"Glance"); return; }
    wchar_t v[MAX_PATH + 3] = L"\"";
    DWORD n = GetModuleFileNameW(nullptr, v + 1, MAX_PATH);
    lstrcpyW(v + 1 + n, L"\"");
    RegSetKeyValueW(HKEY_CURRENT_USER, RUNKEY, L"Glance", REG_SZ, v, (DWORD)((lstrlenW(v) + 1) * sizeof(wchar_t)));
}
static void open_taskmgr() { ShellExecuteW(nullptr, L"open", L"taskmgr.exe", nullptr, nullptr, SW_SHOWNORMAL); }

static HICON make_icon() {  // 32x32 heat-bar glyph, built at runtime so the repo has no binaries
    const int S = 32;
    BITMAPINFO bi = {};
    bi.bmiHeader.biSize = sizeof bi.bmiHeader;
    bi.bmiHeader.biWidth = S; bi.bmiHeader.biHeight = -S; bi.bmiHeader.biPlanes = 1; bi.bmiHeader.biBitCount = 32;
    DWORD* px = nullptr;
    HBITMAP col = CreateDIBSection(nullptr, &bi, DIB_RGB_COLORS, (void**)&px, nullptr, 0);
    if (!col) return LoadIconW(nullptr, IDI_APPLICATION);
    static const DWORD bc[4] = {0x34B6A6, 0xF2B840, 0xFF623A, 0x55ABF7};
    static const int bh[4] = {11, 19, 14, 23};
    for (int y = 0; y < S; ++y)
        for (int x = 0; x < S; ++x) {
            int cx = x < 6 ? 6 - x : x > 25 ? x - 25 : 0, cy = y < 6 ? 6 - y : y > 25 ? y - 25 : 0;
            DWORD v = cx * cx + cy * cy <= 36 ? 0xFF0E1116 : 0;
            int b = (x - 4) / 6, bx = (x - 4) % 6;
            if (x >= 4 && b < 4 && bx < 5 && y >= 27 - bh[b] && y < 27) v = 0xFF000000 | bc[b];
            px[y * S + x] = v;
        }
    static const BYTE zero[S * S / 8] = {};
    HBITMAP mask = CreateBitmap(S, S, 1, 1, zero);
    ICONINFO ii = {TRUE, 0, 0, mask, col};
    HICON h = CreateIconIndirect(&ii);
    DeleteObject(mask); DeleteObject(col);
    return h;
}
static void tray_add() {
    nid.cbSize = sizeof nid; nid.hWnd = hw; nid.uID = 1;
    nid.uFlags = NIF_MESSAGE | NIF_ICON | NIF_TIP; nid.uCallbackMessage = WM_TRAY; nid.hIcon = ico;
    lstrcpyW(nid.szTip, L"Glance");
    Shell_NotifyIconW(NIM_ADD, &nid);
}
static void tray_tip() {
    int n = wsprintfW(nid.szTip, L"Glance  \x00B7  CPU %d%%  \x00B7  RAM %d%%", pc(cpu_total), pc(last(ram)));
    if (ctr[C_GPU]) wsprintfW(nid.szTip + n, L"  \x00B7  GPU %d%%", pc(last(gpu_util)));
    nid.uFlags = NIF_TIP;
    Shell_NotifyIconW(NIM_MODIFY, &nid);
}

static void tip_text(int i) {
    wchar_t a[32], b[32];
    tipbuf[0] = 0;
    if (i < 0 || i >= nwd) return;
    const Wd& w = wd[i];
    if (w.kind == K_CPU) {
        if (hybrid)
            wsprintfW(tipbuf, L"CPU  %d%%\nPerformance cores %d%%  \x00B7  efficiency cores %d%%\n%d logical processors",
                      pc(cpu_total), pc(cpu_mean(true)), pc(cpu_mean(false)), ncpu);
        else wsprintfW(tipbuf, L"CPU  %d%%\n%d logical processors", pc(cpu_total), ncpu);
    } else if (w.kind == K_RAM) {
        int u = tenths_gib(ram_used), t = tenths_gib(ram_total);
        wsprintfW(tipbuf, L"Memory  %d%%\n%d.%d GB of %d.%d GB in use", pc(last(ram)), u / 10, u % 10, t / 10, t % 10);
    } else if (w.kind == K_GPU) {
        int u = tenths_gib(gpu_mem_used), t = tenths_gib(gpu_dedicated() ? gpu_vram : gpu_shared);
        wsprintfW(tipbuf, L"%s\nLoad %d%%  \x00B7  video decode %d%%\n%s memory %d.%d GB of %d.%d GB", gpu_name,
                  pc(last(gpu_util)), pc(last(gpu_dec)), gpu_dedicated() ? L"Dedicated" : L"Shared", u / 10, u % 10, t / 10, t % 10);
    } else if (w.kind == K_DISK) {
        const Disk& d = disk[w.idx];
        fmt_long(a, last(d.rd)); fmt_long(b, last(d.wr));
        wsprintfW(tipbuf, L"Disk %s\nActive %d%%\nRead %s  \x00B7  write %s", d.name, pc(last(d.act)), a, b);
    } else {
        fmt_long(a, last(net_dn)); fmt_long(b, last(net_up));
        wsprintfW(tipbuf, L"%s\n\x2193 %s   \x2191 %s", net_ok ? net_name : L"No network adapter", a, b);
    }
}
static void sync_tools() {  // one tooltip region per widget; rebuilt only when the layout moves
    bool same = ntool == nwd;
    for (int i = 0; same && i < nwd; ++i) same = EqualRect(&tool_rc[i], &wd[i].hit) != 0;
    if (same || !tip) return;
    TOOLINFOW ti = {};
    ti.cbSize = sizeof ti; ti.hwnd = hw;
    for (int i = 0; i < ntool; ++i) { ti.uId = (UINT_PTR)i + 1; SendMessageW(tip, TTM_DELTOOLW, 0, (LPARAM)&ti); }
    for (int i = 0; i < nwd; ++i) {
        ti.uFlags = TTF_SUBCLASS; ti.uId = (UINT_PTR)i + 1; ti.rect = wd[i].hit; ti.lpszText = LPSTR_TEXTCALLBACKW;
        SendMessageW(tip, TTM_ADDTOOLW, 0, (LPARAM)&ti);
        tool_rc[i] = wd[i].hit;
    }
    ntool = nwd;
}

enum { ID_EDGE = 100, ID_SIZE = 110, ID_DISPLAY = 120, ID_SHOW = 140, ID_SCHEME = 150, ID_RATE = 170, ID_SPEED = 180,
       ID_GRAPHS = 200, ID_NUMBERS, ID_STARTUP, ID_EDIT = 210, ID_RELOAD, ID_TASKMGR, ID_EXIT = 299 };
static const int SIZES[4] = {32, 40, 52, 64}, RATES[5] = {100, 250, 500, 1000, 2000};
static const int SPEEDS[6] = {100, 250, 1000, 5000, 15000, 60000};  // milliseconds per graph step
static void apply(bool place) {
    save_cfg();
    sync_counters();
    if (place) { bar = RECT{}; ab_place(); }
    SetTimer(hw, 1, (UINT)cfg.interval, nullptr);
    InvalidateRect(hw, nullptr, FALSE);
}
static void command(int c) {
    int* show[4] = {&cfg.ram, &cfg.gpu, &cfg.disks, &cfg.net};
    if (c >= ID_EDGE && c < ID_EDGE + 4) { cfg.edge = c - ID_EDGE; apply(true); }
    else if (c >= ID_SIZE && c < ID_SIZE + 4) { cfg.size = SIZES[c - ID_SIZE]; apply(true); }
    else if (c >= ID_DISPLAY && c <= ID_DISPLAY + 16) { cfg.display = c - ID_DISPLAY; apply(true); }
    else if (c >= ID_SHOW && c < ID_SHOW + 4) { *show[c - ID_SHOW] ^= 1; apply(false); }
    else if (c >= ID_SCHEME && c < ID_SCHEME + NSCHEME) { lstrcpyW(cfg.cpu_colors, SCHEMES[c - ID_SCHEME].name); set_scheme(cfg.cpu_colors); apply(false); }
    else if (c >= ID_RATE && c < ID_RATE + 5) { cfg.interval = RATES[c - ID_RATE]; apply(false); }
    else if (c >= ID_SPEED && c < ID_SPEED + 6) {  // a step can't be shorter than a reading
        cfg.speed = SPEEDS[c - ID_SPEED];
        if (cfg.interval > cfg.speed) cfg.interval = cfg.speed;
        apply(false);
    }
    else switch (c) {
    case ID_GRAPHS: cfg.graphs ^= 1; apply(false); break;
    case ID_NUMBERS: cfg.readouts ^= 1; apply(false); break;
    case ID_STARTUP: startup_set(!startup_on()); break;
    case ID_EDIT: ShellExecuteW(nullptr, nullptr, L"notepad.exe", ini, nullptr, SW_SHOWNORMAL); break;
    case ID_RELOAD: load_cfg(); detect_gpu(); apply(true); break;
    case ID_TASKMGR: open_taskmgr(); break;
    case ID_EXIT: DestroyWindow(hw); break;
    }
}
static void menu() {
    static const wchar_t* const en[4] = {L"Top", L"Bottom", L"Left", L"Right"};
    static const wchar_t* const sn[4] = {L"Compact", L"Default", L"Large", L"Huge"};
    static const wchar_t* const vn[4] = {L"Memory", L"GPU", L"Disks", L"Network"};
    static const wchar_t* const rn[5] = {L"10 times a second", L"4 times a second", L"Twice a second", L"Every second", L"Every 2 seconds"};
    static const wchar_t* const gn[6] = {L"Very fast (0.1 s per step)", L"Fast (0.25 s per step)", L"Normal (1 s per step)",
                                         L"Slow (5 s per step)", L"Slower (15 s per step)", L"Slowest (1 min per step)"};
    const int* show[4] = {&cfg.ram, &cfg.gpu, &cfg.disks, &cfg.net};
    HMENU m = CreatePopupMenu(), e = CreatePopupMenu(), s = CreatePopupMenu(), d = CreatePopupMenu();
    HMENU v = CreatePopupMenu(), cc = CreatePopupMenu(), rr = CreatePopupMenu(), gs = CreatePopupMenu();
    for (int i = 0; i < 6; ++i) AppendMenuW(gs, MF_STRING | (cfg.speed == SPEEDS[i] ? MF_CHECKED : 0), ID_SPEED + i, gn[i]);
    for (int i = 0; i < 4; ++i) AppendMenuW(e, MF_STRING | (cfg.edge == i ? MF_CHECKED : 0), ID_EDGE + i, en[i]);
    for (int i = 0; i < 4; ++i) AppendMenuW(s, MF_STRING | (cfg.size == SIZES[i] ? MF_CHECKED : 0), ID_SIZE + i, sn[i]);
    for (int i = 0; i < 4; ++i)
        AppendMenuW(v, MF_STRING | (*show[i] ? MF_CHECKED : 0) | (i == 1 && !gpu_found ? MF_GRAYED : 0), ID_SHOW + i, vn[i]);
    bool preset = false;
    for (int i = 0; i < NSCHEME; ++i) {
        bool on = !lstrcmpiW(cfg.cpu_colors, SCHEMES[i].name);
        preset |= on;
        AppendMenuW(cc, MF_STRING | (on ? MF_CHECKED : 0), ID_SCHEME + i, SCHEMES[i].label);
    }
    if (!preset) AppendMenuW(cc, MF_STRING | MF_CHECKED | MF_GRAYED, 0, L"Custom (from settings)");
    for (int i = 0; i < 5; ++i) AppendMenuW(rr, MF_STRING | (cfg.interval == RATES[i] ? MF_CHECKED : 0), ID_RATE + i, rn[i]);
    pick_monitor();
    AppendMenuW(d, MF_STRING | (cfg.display == 0 ? MF_CHECKED : 0), ID_DISPLAY, L"Primary");
    for (int i = 1; i <= nmon; ++i) {
        wchar_t t[16];
        wsprintfW(t, L"Display %d", i);
        AppendMenuW(d, MF_STRING | (cfg.display == i ? MF_CHECKED : 0), ID_DISPLAY + i, t);
    }
    AppendMenuW(m, MF_POPUP, (UINT_PTR)v, L"Show");
    AppendMenuW(m, MF_POPUP, (UINT_PTR)cc, L"CPU colors");
    AppendMenuW(m, MF_POPUP, (UINT_PTR)rr, L"Refresh");
    AppendMenuW(m, MF_POPUP, (UINT_PTR)gs, L"Graph speed");
    AppendMenuW(m, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(m, MF_POPUP, (UINT_PTR)e, L"Edge");
    AppendMenuW(m, MF_POPUP, (UINT_PTR)s, L"Size");
    if (nmon > 1) AppendMenuW(m, MF_POPUP, (UINT_PTR)d, L"Display"); else DestroyMenu(d);
    AppendMenuW(m, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(m, MF_STRING | (cfg.graphs ? MF_CHECKED : 0), ID_GRAPHS, L"History graphs");
    AppendMenuW(m, MF_STRING | (cfg.readouts ? MF_CHECKED : 0), ID_NUMBERS, L"Numbers");
    AppendMenuW(m, MF_STRING | (startup_on() ? MF_CHECKED : 0), ID_STARTUP, L"Start with Windows");
    AppendMenuW(m, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(m, MF_STRING, ID_EDIT, L"Edit settings\x2026");
    AppendMenuW(m, MF_STRING, ID_RELOAD, L"Reload settings");
    AppendMenuW(m, MF_STRING, ID_TASKMGR, L"Task Manager");
    AppendMenuW(m, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(m, MF_STRING, ID_EXIT, L"Exit");
    POINT p;
    GetCursorPos(&p);
    SetForegroundWindow(hw);
    int c = (int)TrackPopupMenu(m, TPM_RETURNCMD | TPM_RIGHTBUTTON | TPM_NONOTIFY, p.x, p.y, 0, hw, nullptr);
    PostMessageW(hw, WM_NULL, 0, 0);
    DestroyMenu(m);
    command(c);
}

static HDC mdc;
static HBITMAP mbm;
static HGDIOBJ mold;
static int bw, bh;

static LRESULT CALLBACK wndproc(HWND h, UINT msg, WPARAM wp, LPARAM lp) {
    if (msg == WM_TASKBAR && WM_TASKBAR) {  // Explorer restarted
        ab_register(); bar = RECT{}; ab_place(); tray_add();
        return 0;
    }
    switch (msg) {
    case WM_TIMER: {
        static DWORD tray_at, step_at;
        if (!warm) { warm = true; SetTimer(h, 1, (UINT)cfg.interval, nullptr); }
        sample();
        DWORD now = GetTickCount(), step = (DWORD)(cfg.speed > cfg.interval ? cfg.speed : cfg.interval);
        if (now - step_at + (DWORD)cfg.interval / 2 >= step) { step_at = now; commit_all(); }  // tolerate timer jitter
        InvalidateRect(h, nullptr, FALSE);
        if (now - tray_at >= 1000) { tray_at = now; tray_tip(); }  // talks to Explorer: at most once a second
        if (tip && IsWindowVisible(tip)) SendMessageW(tip, TTM_UPDATE, 0, 0);
        return 0;
    }
    case WM_PAINT: {
        PAINTSTRUCT ps;
        HDC dc = BeginPaint(h, &ps);
        RECT r;
        GetClientRect(h, &r);
        if (r.right > 0 && r.bottom > 0) {
            if (r.right != bw || r.bottom != bh) {
                if (mdc) { SelectObject(mdc, mold); DeleteObject(mbm); } else mdc = CreateCompatibleDC(dc);
                mbm = CreateCompatibleBitmap(dc, r.right, r.bottom);
                mold = SelectObject(mdc, mbm);
                bw = r.right; bh = r.bottom;
            }
            paint(mdc, bw, bh);
            BitBlt(dc, 0, 0, bw, bh, mdc, 0, 0, SRCCOPY);
            sync_tools();
        }
        EndPaint(h, &ps);
        return 0;
    }
    case WM_ERASEBKGND: return 1;
    case WM_NOTIFY:
        if (((NMHDR*)lp)->code == TTN_GETDISPINFOW) {
            NMTTDISPINFOW* d = (NMTTDISPINFOW*)lp;
            tip_text((int)d->hdr.idFrom - 1);
            d->lpszText = tipbuf;
        }
        return 0;
    case WM_LBUTTONUP: if (cfg.taskmgr) open_taskmgr(); return 0;
    case WM_RBUTTONUP: menu(); return 0;
    case WM_TRAY:
        if (LOWORD(lp) == WM_LBUTTONUP || LOWORD(lp) == WM_RBUTTONUP) menu();
        return 0;
    case WM_COMMAND: command(LOWORD(wp)); return 0;
    case WM_APPBAR:
        if (wp == ABN_POSCHANGED) ab_place();
        else if (wp == ABN_FULLSCREENAPP) {
            fullscreen = lp != 0;
            SetWindowPos(h, fullscreen ? HWND_BOTTOM : HWND_TOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
        }
        return 0;
    case WM_ACTIVATE:
    case WM_WINDOWPOSCHANGED: {
        APPBARDATA a = {};
        a.cbSize = sizeof a; a.hWnd = h;
        SHAppBarMessage(msg == WM_ACTIVATE ? ABM_ACTIVATE : ABM_WINDOWPOSCHANGED, &a);
        break;
    }
    case WM_DISPLAYCHANGE:
    case WM_DPICHANGED:
        bar = RECT{}; ab_place();
        return 0;
    case WM_DESTROY: {
        KillTimer(h, 1);
        APPBARDATA a = {};
        a.cbSize = sizeof a; a.hWnd = h;
        SHAppBarMessage(ABM_REMOVE, &a);
        Shell_NotifyIconW(NIM_DELETE, &nid);
        if (query) PdhCloseQuery(query);
        PostQuitMessage(0);
        return 0;
    }
    }
    return DefWindowProcW(h, msg, wp, lp);
}

static int quit_running() {  // glance --quit: close the running bar and wait for it
    HWND h = FindWindowW(CLS, nullptr);
    if (!h) return 0;
    DWORD pid = 0;
    GetWindowThreadProcessId(h, &pid);
    HANDLE p = OpenProcess(SYNCHRONIZE, FALSE, pid);
    PostMessageW(h, WM_CLOSE, 0, 0);
    if (p) { WaitForSingleObject(p, 5000); CloseHandle(p); }
    return 0;
}

int WINAPI wWinMain(HINSTANCE hi, HINSTANCE, PWSTR cmd, int) {
    if (wcsstr(cmd, L"--quit")) return quit_running();
    if (wcsstr(cmd, L"--startup=on")) { startup_set(true); return 0; }
    if (wcsstr(cmd, L"--startup=off")) { startup_set(false); return 0; }
    CreateMutexW(nullptr, TRUE, L"Local\\GlanceBar");
    if (GetLastError() == ERROR_ALREADY_EXISTS) return 0;

    ini_locate();
    load_cfg();
    save_cfg();  // write defaults so "Edit settings" shows every key
    if (HMODULE sh = LoadLibraryW(L"shcore.dll")) dpi_for_monitor = (DpiForMonitor)(void*)GetProcAddress(sh, "GetDpiForMonitor");
    INITCOMMONCONTROLSEX icc = {sizeof icc, ICC_BAR_CLASSES};
    InitCommonControlsEx(&icc);

    WNDCLASSEXW wc = {};
    wc.cbSize = sizeof wc; wc.lpfnWndProc = wndproc; wc.hInstance = hi;
    wc.hCursor = LoadCursorW(nullptr, IDC_ARROW); wc.lpszClassName = CLS;
    RegisterClassExW(&wc);
    hw = CreateWindowExW(WS_EX_TOOLWINDOW | WS_EX_TOPMOST | WS_EX_NOACTIVATE, CLS, L"Glance", WS_POPUP, 0, 0, 0, 0,
                         nullptr, nullptr, hi, nullptr);
    if (!hw) return 1;
    WM_TASKBAR = RegisterWindowMessageW(L"TaskbarCreated");
    tip = CreateWindowExW(WS_EX_TOPMOST, TOOLTIPS_CLASSW, nullptr, WS_POPUP | TTS_NOPREFIX | TTS_ALWAYSTIP, 0, 0, 0, 0,
                          hw, nullptr, hi, nullptr);
    if (tip) {
        SendMessageW(tip, TTM_SETMAXTIPWIDTH, 0, 600);
        SendMessageW(tip, TTM_SETDELAYTIME, TTDT_INITIAL, 400);
        SendMessageW(tip, TTM_SETDELAYTIME, TTDT_AUTOPOP, 30000);
    }
    detect_cpu();
    detect_gpu();
    pdh_open();
    ico = make_icon();
    ab_register();
    ab_place();
    tray_add();
    SetTimer(hw, 1, 300, nullptr);  // first reading quickly, then every `interval` ms

    MSG m;
    while (GetMessageW(&m, nullptr, 0, 0) > 0) { TranslateMessage(&m); DispatchMessageW(&m); }
    return 0;
}
