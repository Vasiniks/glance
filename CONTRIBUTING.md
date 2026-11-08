# Contributing to Glance

Keep it **lightweight**. PRs adding heavy deps will be rejected.

Allowed: GDI, USER32, PDH, SHELL32 — already linked, <100 LOC helper.

Not allowed: D2D/DWrite, WIC, COM, third_party SDKs, external fonts, JSON libs.

Build:
```
cmake -B build
cmake --build build --config Release
./build/glance_tests.exe
```
