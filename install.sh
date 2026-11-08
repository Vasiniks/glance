#!/usr/bin/env bash
# Glance — one-liner installer for Windows (Git Bash / MSYS2 / WSL)
# Usage:
#   curl -fsSL https://raw.githubusercontent.com/Vasiniks/glance/main/install.sh | bash
#   bash install.sh --portable
#   bash install.sh --no-startup --bin-url https://example.com/Glance.exe
set -euo pipefail

INSTALL_DIR="${LOCALAPPDATA:-$HOME/AppData/Local}/Glance"
BIN_URL=""
NO_STARTUP=0
PORTABLE=0
REPO="Vasiniks/glance"

info() { printf "\033[1;36m[glance]\033[0m %s\n" "$*"; }
warn() { printf "\033[1;33m[warn]\033[0m %s\n" "$*"; }
err()  { printf "\033[1;31m[err]\033[0m %s\n" "$*" >&2; }

for arg in "$@"; do
  case "$arg" in
    --portable) PORTABLE=1 ;;
    --no-startup) NO_STARTUP=1 ;;
    --bin-url=*) BIN_URL="${arg#*=}" ;;
    --dir=*) INSTALL_DIR="${arg#*=}" ;;
    --help|-h) echo "Usage: $0 [--portable] [--no-startup] [--dir=PATH] [--bin-url=URL]"; exit 0 ;;
  esac
done

# Resolve exe source
SRC=""
if [[ -n "$BIN_URL" ]]; then
  TMP="/tmp/Glance.exe"
  info "Downloading $BIN_URL → $TMP"
  if command -v curl >/dev/null; then curl -fsSL "$BIN_URL" -o "$TMP"
  elif command -v wget >/dev/null; then wget -q "$BIN_URL" -O "$TMP"
  else err "curl or wget required for --bin-url"; exit 1; fi
  SRC="$TMP"
else
  # Look for local builds
  CANDIDATES=(
    "build/Release/Glance.exe"
    "build/Glance.exe"
    "out/build/windows-x64-release/Glance.exe"
    "./Glance.exe"
    "$(dirname "$0")/build/Release/Glance.exe"
  )
  for c in "${CANDIDATES[@]}"; do
    if [[ -f "$c" ]]; then SRC="$c"; break; fi
  done

  if [[ -z "$SRC" ]]; then
    # Try to build if cmake available
    if command -v cmake >/dev/null 2>&1 && [[ -f "CMakeLists.txt" ]]; then
      info "No binary found — building (tiny)..."
      cmake -B build -DGLANCE_TINY=ON -DCMAKE_BUILD_TYPE=Release >/dev/null
      cmake --build build --config Release
      SRC="build/Release/Glance.exe"
      [[ -f "$SRC" ]] || SRC="build/Glance.exe"
    fi
  fi

  if [[ -z "$SRC" || ! -f "$SRC" ]]; then
    # Try latest release via GitHub API
    if command -v curl >/dev/null; then
      info "Checking GitHub releases..."
      API="https://api.github.com/repos/$REPO/releases/latest"
      DL=$(curl -fsSL "$API" 2>/dev/null | grep -o '"browser_download_url": *"[^"]*Glance\.exe"' | head -1 | cut -d'"' -f4 || true)
      if [[ -n "$DL" ]]; then
        TMP="/tmp/Glance.exe"
        info "Downloading release: $DL"
        curl -fsSL "$DL" -o "$TMP"
        SRC="$TMP"
      fi
    fi
  fi
fi

if [[ -z "$SRC" || ! -f "$SRC" ]]; then
  err "No Glance.exe found. Build first:"
  err "  cmake -B build -DGLANCE_TINY=ON && cmake --build build --config Release"
  err "  bash install.sh"
  exit 1
fi

# Install
mkdir -p "$INSTALL_DIR"
DEST="$INSTALL_DIR/Glance.exe"

# Kill running
if command -v taskkill.exe >/dev/null 2>&1; then taskkill.exe //F //IM Glance.exe 2>/dev/null || true; fi
sleep 0.3 2>/dev/null || true

info "Installing $SRC → $DEST"
cp -f "$SRC" "$DEST"
chmod +x "$DEST" 2>/dev/null || true

# Autostart shortcut (Windows only)
if [[ $PORTABLE -eq 0 && $NO_STARTUP -eq 0 ]]; then
  STARTUP="$(powershell.exe -NoProfile -Command "[Environment]::GetFolderPath('Startup')" 2>/dev/null | tr -d '\r' || echo "")"
  if [[ -n "$STARTUP" ]]; then
    # Create .lnk via PowerShell
    powershell.exe -NoProfile -Command "\$s=(New-Object -COM WScript.Shell).CreateShortcut('$STARTUP\\Glance.lnk'); \$s.TargetPath='$DEST'; \$s.WorkingDirectory='$INSTALL_DIR'; \$s.Description='Glance — lightweight monitor'; \$s.Save()" >/dev/null 2>&1 || true
    info "Autostart: $STARTUP\\Glance.lnk"
  fi
fi

# Run
info "Starting Glance..."
if command -v powershell.exe >/dev/null 2>&1; then
  powershell.exe -NoProfile -Command "Start-Process '$DEST'" >/dev/null 2>&1 &
else
  "$DEST" &
fi

info "Done — tray icon should appear. Right-click → Exit."
echo ""
echo "  Uninstall: bash uninstall.sh   or   powershell -File scripts/uninstall.ps1"
