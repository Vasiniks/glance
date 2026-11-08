#!/usr/bin/env bash
# Build build/Glance.exe with a mingw-w64 toolchain (llvm-mingw, MSYS2 mingw64, or a Linux cross compiler).
set -euo pipefail
cd "$(dirname "$0")"

pick() { for c in "$@"; do if command -v "$c" >/dev/null 2>&1; then echo "$c"; return; fi; done; }
CXX=${CXX:-$(pick x86_64-w64-mingw32-clang++ x86_64-w64-mingw32-g++ g++)}
RC=${RC:-$(pick x86_64-w64-mingw32-windres llvm-windres windres)}
if [ -z "$CXX" ] || [ -z "$RC" ]; then
  echo "build.sh: needs a mingw-w64 C++ compiler and windres, e.g. https://github.com/mstorsjo/llvm-mingw" >&2
  exit 1
fi

mkdir -p build
"$RC" -O coff -i res/version.rc -o build/version.o
"$RC" -O coff -i res/manifest.rc -o build/manifest.o
"$CXX" -std=c++17 -Os -Wall -Wextra -municode -mwindows -static -s \
  -fno-exceptions -fno-rtti -fno-threadsafe-statics -fno-ident \
  -ffunction-sections -fdata-sections -Wl,--gc-sections \
  src/glance.cpp build/version.o build/manifest.o -o build/Glance.exe \
  -lpdh -lcomctl32 -lshell32 -lgdi32 -luser32 -ladvapi32
echo "build/Glance.exe: $(wc -c < build/Glance.exe) bytes"
