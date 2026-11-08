# Glance

**Ultra-lightweight desktop system monitor for Windows — <90KB, 0 deps, ~5MB RAM, premium UI.**

Glance shows per-core CPU, RAM, disk, and network in a native Windows AppBar. Place it on any screen edge; maximized windows sit beside it. Rounded tiles, subtle highlights, tracking tooltips.

![glance](https://img.shields.io/badge/size-%3C90KB-blue) ![platform](https://img.shields.io/badge/platform-Windows%2010%2F11-lightgrey) ![lang](https://img.shields.io/badge/lang-C%2B%2B20-blue) ![deps](https://img.shields.io/badge/deps-0-green) ![ram](https://img.shields.io/badge/RAM-%7E5MB-green) ![ui](https://img.shields.io/badge/UI-premium%20GDI-purple)

## Install / Uninstall (one-liner)

> Windows 10/11 x64 — Git Bash or PowerShell.

**`.sh` one-liner (recommended — works in Git Bash, MSYS2, WSL):**

```bash
# curl | bash — builds tiny, installs to %LOCALAPPDATA%\Glance, autostart, runs
curl -fsSL https://raw.githubusercontent.com/Vasiniks/glance/main/install.sh | bash

# local clone:
bash install.sh                  # build + install
bash install.sh --portable       # no autostart shortcut
bash install.sh --bin-url=https://example.com/Glance.exe  # from URL

# uninstall
bash uninstall.sh
```

**PowerShell one-liners (alternative):**

```powershell
# build + install (autostart)
cmake -B build -DGLANCE_TINY=ON; cmake --build build --config Release; powershell -ExecutionPolicy Bypass -File scripts/install.ps1

# uninstall
powershell -ExecutionPolicy Bypass -File scripts/uninstall.ps1

# portable
.\build\Release\Glance.exe

# remote PS install
iwr https://raw.githubusercontent.com/Vasiniks/glance/main/scripts/install.ps1 -UseBasicParsing | iex
```

Portable mode: if `glance.ini` sits next to exe, it stays portable. No registry.

## UI — premium GDI, still tiny

- **Background** `#0F0F12` with 1px border `#2A2A32` edge glow
- **Pill labels** `#24242A` bg / `#B0B2BE` text / symbols ✦ ◍ ◫ ⇆
- **CPU**: rounded tiles (r=4px, gap 3px), emerald→amber→rose heatmap, cached GDI, double-pass clean fill
- **RAM**: rounded track `#26262C`, fill with adaptive color + top shine highlight (lerp to white 22%)
- **Disk**: 3–4 slim rows, name + rounded 5px bar `#2D2D34` → heat color
- **Net**: segmented track — ▼ blue `#60A5FA` / ▲ amber `#FBBF24` plus formatted B/KB/MB, tooltip with iface name
- **Separators** 1px `#26262E` between sections
- **Tooltip** tracking tooltip (`TOOLTIPS_CLASS` + `TTM_TRACK`) on hover — shows exact values
- **Double-buffered** `BitBlt` to avoid flicker, `Thread_local` cached `HFONT` + `HBRUSH` + `HPEN` = 1 GDI alloc vs N per frame

## Why so lightweight?

| Feature | Typical | Glance |
|---|---|---|
| Renderer | D2D/DWrite + fonts | **GDI only, cached, rounded, 0 per-frame allocs** |
| Telemetry | WMI+DXGI+vendor SDKs | **PDH only, fixed arrays 64c/16d/32n** |
| Config | Registry/JSON | **Win32 INI API (kernel32)** |
| Deps | 4 libs + 3 SDKs | **1 exe, user32+gdi32+shell32+shcore+pdh+comctl32** |
| Source | ~35 files | **~6 files ~950 LOC** |
| Binary | 1–2 MB | **<90KB `/O1 /Os /GL /GR- /GF /Gy /LTCG`** |
| Build | Pinned | **any MSVC + any Win10 SDK** |

## Build

```
cmake -B build -DGLANCE_TINY=ON
cmake --build build --config Release
.\build\Release\Glance.exe
ctest --test-dir build
```

## Usage

- Run `Glance.exe` — tray icon appears.
- Hover sections → tooltip with details. Left-click → Task Manager. Right-click → menu.
- `glance.ini`:

```ini
[Glance]
edge=bottom      # auto/left/top/right/bottom
thickness=56     # 40..640 DIPs (56 looks best for new UI)
interval=1000    # 250..5000 ms
monitor=0        # 0=primary
show_cpu=1
show_ram=1
show_disks=1
show_net=1
always_numbers=1
open_taskmgr=1
```

## Architecture

```
src/
  main.cpp        — WinMain, DPI fast path, tooltip tracking, single instance
  config.h/.cpp   — INI via kernel32, portable-first
  telemetry.h/.cpp— PDH single query, stack buffers, mailbox
  renderer.h/.cpp — premium GDI: rounded tiles, pills, shine, hit-test
  appbar.h/.cpp   — AppBar + tray
install.sh        — bash installer (curl|bash friendly)
uninstall.sh      — bash uninstaller
scripts/
  install.ps1     — PS installer (Windows)
  uninstall.ps1   — PS uninstaller
```

See `OPTIMIZATIONS.md` for size breakdown.

## License

MIT — see [LICENSE](LICENSE).
