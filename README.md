# Glance

**Ultra-lightweight desktop system monitor — a minimal fork of [Vorta/Loadbar](https://github.com/Vorta/Loadbar).**

Glance shows per-core CPU, RAM, disk, and network in a native Windows AppBar. Place it on any screen edge; maximized windows sit beside it. That's it.

![glance](https://img.shields.io/badge/size-%3C90KB-blue) ![platform](https://img.shields.io/badge/platform-Windows%2010%2F11-lightgrey) ![lang](https://img.shields.io/badge/lang-C%2B%2B20-blue) ![deps](https://img.shields.io/badge/deps-0-green)

## Install / Uninstall (one-liner)

> Requires PowerShell 5+ on Windows 10/11 x64.

**Install latest build + autostart:**
```powershell
iwr https://raw.githubusercontent.com/Vorta/Loadbar/main/scripts/install.ps1 -UseBasicParsing | iex; & "$env:LOCALAPPDATA\Glance\Glance.exe"
```
Local repo (after `git clone`):
```powershell
# build + install in one line — copies to %LOCALAPPDATA%\Glance + adds Startup shortcut + runs
powershell -ExecutionPolicy Bypass -Command "cmake -B build -DGLANCE_TINY=ON; cmake --build build --config Release; powershell -File scripts/install.ps1"
```

**Compact one-liners (this repo):**
```powershell
# Install (builds tiny, installs to LocalAppData, autostart on login)
cmake -B build -DGLANCE_TINY=ON && cmake --build build --config Release && powershell -ExecutionPolicy Bypass -File scripts/install.ps1

# Uninstall (kill, remove bar, delete binary, remove autostart)
powershell -ExecutionPolicy Bypass -File scripts/uninstall.ps1

# Portable: just run
.\build\Release\Glance.exe
```

**Scoop / winget style (future release):**
```powershell
# when releases exist:
# scoop install https://raw.githubusercontent.com/yourfork/glance/main/scoop/glance.json
# or: winget install glance
```

Portable mode: if `glance.ini` is next to the exe, it stays portable. No registry, no services.

## Why Glance vs Loadbar?

Loadbar is excellent but heavy by design: Direct2D/DirectWrite, vendor temperature SDKs (NVAPI/IGCL/ADLX), bundled fonts, 30+ source files, pinned SDK/CMake.

Glance optimizes for **lightweight**:

| | Loadbar | Glance (new) |
|---|---|---|
| Renderer | D2D + DWrite + embedded Geist Mono | **GDI only, cached font + cached brush, no per-tile GDI allocs** |
| Telemetry | PDH + IPHLPAPI + DXGI + vendor SDKs + WMI temp | **PDH only, fixed arrays (no heap per sample), 64 cores / 16 disks / 32 nets max** |
| Config I/O | Registry, 11 schemas, 2M char limit | **Win32 Get/WritePrivateProfileString (kernel32, already resident), no `<fstream>`** |
| Deps | 4 static libs, 3 third_party SDKs | **1 exe, 0 third_party, `user32+gdi32+shell32+shcore+pdh` only** |
| Source files | ~35 | **~6, ~900 LOC** |
| Binary size | ~1–2 MB | **<90KB target (`/O1 /Os /GL /GR- /GF /Gy /OPT:REF,ICF /LTCG /DEBUG:NONE`)** |
| Build pins | CMake 4.4.4 + SDK 10.0.28000.0 | **any MSVC + any Win10 SDK** |

What was removed:
- GPU vendor temperature, ASUS temp, all third_party/
- Embedded font (Segoe UI system)
- Direct2D/DirectWrite/RuntimeObject + tooltip cache + text preferences
- `<iostream>`, `<fstream>`, RTTI, exception overhead

What is kept / improved:
- Per-core CPU tiles (PDH `\Processor(*)\% Processor Time`)
- RAM (GlobalMemoryStatusEx)
- Disk active time + read/write via PDH `\PhysicalDisk`
- Network up/down via PDH `\Network Interface` (auto-picks most active, or hinted)
- AppBar reserve, tray icon, Task Manager click, timer coalescing

## Build

Requirements: Windows 10/11 x64, MSVC 19.38+ (VS2022), any Windows 10 SDK, CMake 3.20+.

```powershell
# fastest
cmake -B build -DGLANCE_TINY=ON
cmake --build build --config Release
.\build\Release\Glance.exe

# with tests
ctest --test-dir build --output-on-failure
```

`GLANCE_TINY=ON` = `/O1 /Os /GL /GR- /GF /Gy /LTCG` + stripped debug, no RTTI, func-level linking.

## Usage

- Run `Glance.exe` — tray icon appears.
- Hover for numbers. Left-click → Task Manager. Right-click → menu (Settings opens `glance.ini`).
- Edit `glance.ini`:
  ```ini
  [Glance]
  edge=bottom      # auto/left/top/right/bottom
  thickness=48     # 40..640 DIPs
  interval=1000    # 250..5000 ms
  monitor=0        # 0=primary, 1..N
  show_cpu=1
  show_ram=1
  show_disks=1
  show_net=1
  always_numbers=0
  open_taskmgr=1
  ```

## Architecture

```
src/
  main.cpp        — WinMain, DPI awareness (SetProcessDpiAwarenessContext no DLL), single instance mutex
  config.h/.cpp   — flat INI via kernel32 API, portable first, no <fstream>
  telemetry.h/.cpp— PDH single query, fixed stack buffers (no per-sample heap), published via 1-slot mailbox
  renderer.h/.cpp — GDI only, thread_local cached HFONT+HBRUSH, FillRect batch
  appbar.h/.cpp   — AppBar register, placement, tray (SHAppBarMessage)
scripts/
  install.ps1     — copies exe, optional Startup .lnk, runs
  uninstall.ps1   — kills, removes AppBar, deletes binary + autostart
```

## More optimizations (done this iteration)

- **No `<fstream>` / `<iostream>`** → saves ~200KB CRT formatting code. Config uses `GetPrivateProfileIntW`/`WritePrivateProfileStringW` (already in kernel32).
- **`/GR-` (no RTTI)** — no RTTI data/type_info bloat.
- **`/GF /Gy`** — string pool + function-level linking → linker can dead-strip ~15% more.
- **Fixed-size snapshot**: `CpuSample per_core[64]`, `DiskSample disks[16]`, `NetSample` inline wchar — zero heap allocations on hot path. Mailbox copies by value, no `vector<wstring>` churn.
- **Thread-local GDI cache**: one `HFONT` + one reused `HBRUSH` (recreated only on color change) instead of `CreateSolidBrush`/`CreateFont` per tile/frame. Reduces GDI object churn from ~N cores * 60fps to 1.
- **DPI awareness without shcore LoadLibrary each resize**: uses `SetProcessDpiAwarenessContext((HANDLE)-4)` if available (user32 already loaded), falls back to shcore.
- **PDH expand once**: counters collected at init, never re-enumerated per sample.
- **wsprintfW** instead of snprintf/ostringstream for number formatting in paint path (user32 already loaded).
- `main.cpp` uses raw `new`/`delete` instead of `std::make_unique` chain to avoid extra template instantiations.

See `OPTIMIZATIONS.md` for measured WS / handles / CPU.

## License

MIT — see [LICENSE](LICENSE). Original idea by [Vorta/Loadbar](https://github.com/Vorta/Loadbar).
