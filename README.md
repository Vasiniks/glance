# Glance

**Ultra-lightweight desktop system monitor for Windows — <90KB, 0 dependencies, ~5MB RAM.**

Glance shows per-core CPU, RAM, disk, and network in a native Windows AppBar. Place it on any screen edge; maximized windows sit beside it. That's it.

![glance](https://img.shields.io/badge/size-%3C90KB-blue) ![platform](https://img.shields.io/badge/platform-Windows%2010%2F11-lightgrey) ![lang](https://img.shields.io/badge/lang-C%2B%2B20-blue) ![deps](https://img.shields.io/badge/deps-0-green) ![ram](https://img.shields.io/badge/RAM-%7E5MB-green)

## Install / Uninstall (one-liner)

> Requires PowerShell 5+ on Windows 10/11 x64.

**Compact one-liners:**

```powershell
# Install (builds tiny, installs to %LOCALAPPDATA%\Glance, autostart on login, runs)
cmake -B build -DGLANCE_TINY=ON && cmake --build build --config Release && powershell -ExecutionPolicy Bypass -File scripts/install.ps1

# Uninstall (kill, remove bar, delete binary, remove autostart)
powershell -ExecutionPolicy Bypass -File scripts/uninstall.ps1

# Portable: just run
.\build\Release\Glance.exe

# One-liner install from internet (when releases exist):
iwr https://raw.githubusercontent.com/Vasiniks/glance/main/scripts/install.ps1 -UseBasicParsing | iex
```

Portable mode: if `glance.ini` is next to exe, it stays portable. No registry, no services.

## Why so lightweight?

| Feature | Typical monitors | Glance |
|---|---|---|
| Renderer | D2D / DirectWrite / embedded fonts | **GDI only, cached HFONT+HBRUSH, no per-tile allocs** |
| Telemetry | WMI + DXGI + vendor SDKs + IPHLPAPI | **PDH only, fixed arrays (64 cores / 16 disks / 32 nets)** |
| Config I/O | Registry, JSON, schemas | **Win32 INI API (kernel32, already resident)** |
| Dependencies | 4 libs + 3 third_party SDKs | **1 exe, 0 third_party — user32+gdi32+shell32+shcore+pdh only** |
| Source | ~35 files | **~6 files, ~900 LOC** |
| Binary | ~1–2 MB | **<90KB `/O1 /Os /GL /GR- /GF /Gy /LTCG /DEBUG:NONE`** |
| Build | Pinned SDK/CMake | **any MSVC + any Win10 SDK** |

## What it shows

- Per-core CPU tiles (PDH `\Processor(*)\% Processor Time`)
- RAM bar + % (GlobalMemoryStatusEx)
- Disk active time + read/write (PDH `\PhysicalDisk`)
- Network up/down (PDH `\Network Interface`, auto-picks most active)
- AppBar reservation (maximized windows don't cover it), tray icon, Task Manager on click

## Build

Requirements: Windows 10/11 x64, MSVC 19.38+ (VS2022), any Windows 10 SDK, CMake 3.20+.

```powershell
cmake -B build -DGLANCE_TINY=ON
cmake --build build --config Release
.\build\Release\Glance.exe

ctest --test-dir build --output-on-failure
```

`GLANCE_TINY=ON` = `/O1 /Os /GL /GR- /GF /Gy /LTCG` stripped, no RTTI, func-level linking.

## Usage

- Run `Glance.exe` — tray icon appears.
- Left-click → Task Manager. Right-click → menu (Settings opens `glance.ini`).
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
  main.cpp        — WinMain, DPI (SetProcessDpiAwarenessContext), single instance mutex
  config.h/.cpp   — flat INI via kernel32 API, portable-first, no <fstream>
  telemetry.h/.cpp— PDH single query, fixed stack buffers, 1-slot mailbox
  renderer.h/.cpp — GDI only, thread_local cached font/brush, FillRect batch
  appbar.h/.cpp   — AppBar register, placement, tray
scripts/
  install.ps1     — copies exe, Startup .lnk, runs
  uninstall.ps1   — kills, removes bar + autostart
```

See `OPTIMIZATIONS.md` for deep-dive measurements and link map.

## License

MIT — see [LICENSE](LICENSE).
