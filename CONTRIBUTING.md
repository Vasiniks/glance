# Contributing to Glance

Keep it **lightweight**. Any PR that adds a heavy dependency will be rejected.

Allowed:
- GDI, USER32, PDH, SHELL32 — already linked
- < 100 LOC helper, no extra lib

Not allowed:
- D2D/DWrite, WIC, COM, third_party SDKs, external fonts, JSON lib, etc
- Registry schema migration — config is flat INI

Build before PR:
```
cmake -B build
cmake --build build --config Release
./build/glance_tests.exe
```
