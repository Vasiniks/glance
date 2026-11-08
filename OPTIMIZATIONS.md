# How Glance is super lightweight

## Binary size: <90KB

| Technique | Saving |
|---|---|
| No `<fstream>` / `<iostream>` — Config via `GetPrivateProfile*` (kernel32) | ~200KB CRT |
| `/GR-` no RTTI | ~30KB RTTI |
| `/GF` string pool + `/Gy` func-level linking | ~15% dead-strip |
| Fixed-size Snapshot (no vector growth per sample) | ~50KB + no heap thrash |
| GDI cache: 1 HFONT + 1 HBRUSH vs N per frame | GDI syscalls |
| Single exe target, no static libs | ~400KB link overhead |
| GDI only (no d2d1/dwrite import) | ~300KB |
| `/O1 /Os /GL /LTCG /DEBUG:NONE` | ~150KB strip |

MSVC TINY flags:
```
/O1 /Os /GL /GR- /GF /Gy
/DYNAMICBASE /NXCOMPAT /HIGHENTROPYVA
/INCREMENTAL:NO /OPT:REF /OPT:ICF /LTCG /DEBUG:NONE /SUBSYSTEM:WINDOWS
libs: user32,gdi32,shell32,shcore,pdh only
```

MinGW (even smaller): `g++ -Os -ffunction-sections -fdata-sections -Wl,--gc-sections -s → ~70KB, UPX → ~35KB`

## CPU: ~0.05%

- PDH single query, enum once
- No temp polling
- Paint only on new gen
- Cached brush, `wsprintfW` (no CRT locale)
- Fixed buffers: no allocator lock
- DPI awareness via user32 fast path

## RAM: ~5MB WS

- No D2D device, no font factories
- 1-slot mailbox, few KB snapshot
- No fstream lines vector

## Measured i7-12700, 1000ms

```
WS: 5.2 MB
Private: 2.1 MB
CPU: 0.04% avg
GDI: 5, USER: 4, Handles: 41
File: ~78KB on disk (TINY)
```
