#!/usr/bin/env bash
# Glance uninstaller (Git Bash / MSYS2)
set -euo pipefail
INSTALL_DIR="${LOCALAPPDATA:-$HOME/AppData/Local}/Glance"

info(){ printf "\033[1;36m[glance]\033[0m %s\n" "$*"; }

info "Uninstalling..."

if command -v taskkill.exe >/dev/null 2>&1; then taskkill.exe //F //IM Glance.exe 2>/dev/null || true; fi
sleep 0.5 2>/dev/null || true

STARTUP="$(powershell.exe -NoProfile -Command "[Environment]::GetFolderPath('Startup')" 2>/dev/null | tr -d '\r' || echo "")"
if [[ -n "$STARTUP" && -f "$STARTUP/Glance.lnk" ]]; then rm -f "$STARTUP/Glance.lnk"; info "Removed $STARTUP/Glance.lnk"; fi
if [[ -f "$INSTALL_DIR/Glance.exe" ]]; then rm -f "$INSTALL_DIR/Glance.exe"; info "Removed $INSTALL_DIR/Glance.exe"; fi

if [[ -d "$INSTALL_DIR" ]]; then
  if [[ -z "$(ls -A "$INSTALL_DIR" 2>/dev/null)" ]]; then rmdir "$INSTALL_DIR"; info "Removed $INSTALL_DIR"; else info "$INSTALL_DIR not empty — kept"; fi
fi

info "Uninstalled. AppBar reservation released."
