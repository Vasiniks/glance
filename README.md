# Glance

**Ultra-lightweight desktop system monitor — a minimal fork of [Vorta/Loadbar](https://github.com/Vorta/Loadbar).**

Glance shows per-core CPU, RAM, disk, and network in a native Windows AppBar. Place it on any screen edge; maximized windows sit beside it. That's it.

![glance](https://img.shields.io/badge/size-%3C150KB-blue) ![platform](https://img.shields.io/badge/platform-Windows%2010%2F11-lightgrey) ![lang](https://img.shields.io/badge/lang-C%2B%2B23-blue)

## Why Glance vs Loadbar?

Loadbar is excellent but heavy by design: Direct2D/DirectWrite, vendor temperature SDKs (NVAPI/IGCL/ADLX), bundled fonts, 30+ source files, pinned SDK/CMake.

Glance optimizes for **lightweight**:

| | Loadbar | Glance |
|---|---|---|
| Renderer | D2D + DWrite + embedded Geist Mono | **GDI only** |
| Telemetry | PDH + IPHLPAPI + DXGI + vendor SDKs + WMI temp | **PDH only** |
| Dependencies | 4 static libs, 3 third_party SDKs | **1 exe, 0 third_party** |
| Source files | ~35 | **~6** |
| Binary size | ~1–2 MB | **< 150 KB target** |
| Build pins | CMake 4.4.4 + SDK 10.0.28000.0 | **any recent MSVC + any Win10 SDK** |
| Config | Registry, versioned schemas | **simple INI next to exe** |

What was removed:
- GPU vendor temperature, ASUS temp, all third_party/
- Embedded font (uses Segoe UI / system font)
- Direct2D/DirectWrite/RuntimeObject
- Complex settings migration (11 schemas) → flat struct
- Heavy tooltip cache & text preferences
- Separate static libs — single target now

What is kept / improved:
- Per-core CPU tiles (PDH `\Processor(*)\% Processor Time`)
- RAM (GlobalMemoryStatusEx)
- Disk active time + read/write via PDH `\PhysicalDisk`
- Network up/down via PDH `\Network Interface`
- AppBar reserve, tray icon, Task Manager click
- Sampling timer (250–5000 ms), thickness 40–640 DIPs

## Build

Requirements: Windows 10/11 x64, MSVC 19.38+ (VS2022), any Windows 10 SDK, CMake 3.20+.

```powershell
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --config Release
.\build\Glance.exe
```

For smallest binary:

```powershell
cmake -B build -DCMAKE_BUILD_TYPE=Release -DGLANCE_TINY=ON
cmake --build build --config Release
```

`GLANCE_TINY=ON` adds `/O1 /Os /GL /OPT:REF,ICF /LTCG` and strips debug info.

## Usage

- Run `Glance.exe` — tray icon appears.
- Drag settings: monitor, edge, thickness, interval.
- Hover for numbers. Left-click → Task Manager. Right-click → Exit.
- Config saved to `glance.ini` alongside exe (portable). Falls back to `%APPDATA%\Glance\`.

## Architecture

```
src/
  main.cpp        — WinMain, single instance mutex, message loop
  config.h        — flat Config struct + INI read/write
  telemetry.h/.cpp— PDH collector thread, snapshot mailbox
  renderer.h/.cpp — GDI bar painter, no D2D
  appbar.h/.cpp   — AppBar register, placement, tray
```

Ownership:
- UI thread: window, AppBar, paint, tray.
- Worker jthread: PDH query, sleep, publish snapshot (mutex + 1 slot).

## License

MIT — see [LICENSE](LICENSE). Original idea by [Vorta/Loadbar](https://github.com/Vorta/Loadbar).
