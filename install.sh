#!/usr/bin/env bash
# Glance installer for Windows. Run it from Git Bash, MSYS2 or WSL:
#   curl -fsSL https://raw.githubusercontent.com/Vasiniks/glance/master/install.sh | bash
#   curl -fsSL https://raw.githubusercontent.com/Vasiniks/glance/master/install.sh | bash -s -- --uninstall
# Options:
#   --uninstall    close Glance and remove it, including its sign-in entry
#   --no-startup   install without starting Glance when you sign in
#   --exe PATH     install a local Glance.exe instead of downloading the latest release
set -euo pipefail

REPO="Vasiniks/glance"
URL="https://github.com/$REPO/releases/latest/download/Glance.exe"
RUNKEY='HKCU\Software\Microsoft\Windows\CurrentVersion\Run'

say() { printf '\033[1;36m==>\033[0m %s\n' "$*"; }
die() { printf '\033[1;31merror:\033[0m %s\n' "$*" >&2; exit 1; }
# Run a Windows tool with its /switches left alone by MSYS path conversion and stdin detached
# (so nothing can swallow the rest of a piped script).
wincmd() { MSYS2_ARG_CONV_EXCL='*' MSYS_NO_PATHCONV=1 "$@" </dev/null; }
winenv() { (cd /mnt/c 2>/dev/null; cmd.exe /d /c "echo %$1%" </dev/null 2>/dev/null) | tr -d '\r'; }

main() {
  local uninstall=0 startup=1 src=""
  while [ $# -gt 0 ]; do
    case "$1" in
      --uninstall) uninstall=1 ;;
      --no-startup) startup=0 ;;
      --exe) src="${2:?--exe needs a path}"; shift ;;
      -h|--help) echo "usage: install.sh [--uninstall] [--no-startup] [--exe PATH]"; return 0 ;;
      *) die "unknown option: $1 (see --help)" ;;
    esac
    shift
  done

  local localappdata roaming
  if command -v cygpath >/dev/null 2>&1; then                # Git Bash, MSYS2, Cygwin
    win() { cygpath -w "$1"; }
    localappdata=$(cygpath -u "${LOCALAPPDATA:?LOCALAPPDATA is not set}")
    roaming=$(cygpath -u "${APPDATA:?APPDATA is not set}")
  elif grep -qi microsoft /proc/version 2>/dev/null; then    # WSL
    win() { wslpath -w "$1"; }
    local l r
    l=$(winenv LOCALAPPDATA); r=$(winenv APPDATA)
    [ -n "$l" ] && [ -n "$r" ] || die "can't reach Windows from WSL (cmd.exe not found; is interop enabled?)"
    localappdata=$(wslpath -u "$l")
    roaming=$(wslpath -u "$r")
  else
    die "Glance is a Windows app: run this from Git Bash, MSYS2 or WSL on Windows."
  fi

  local dir="$localappdata/Programs/Glance"
  local exe="$dir/Glance.exe" new="$dir/Glance.new.exe"

  if [ "$uninstall" = 1 ]; then
    say "Closing Glance"
    stop_running "$exe"
    wincmd reg.exe delete "$RUNKEY" /v Glance /f >/dev/null 2>&1 || true
    rm -rf "$dir"
    cleanup_legacy "$localappdata" "$roaming"
    say "Glance is uninstalled."
    return 0
  fi

  mkdir -p "$dir"
  if [ -n "$src" ]; then
    cp -f "$src" "$new"
  else
    say "Downloading the latest Glance.exe"
    if command -v curl >/dev/null 2>&1; then
      curl -fL --retry 2 --progress-bar -o "$new" "$URL" </dev/null || die "download failed: $URL"
    elif command -v wget >/dev/null 2>&1; then
      wget -q -O "$new" "$URL" </dev/null || die "download failed: $URL"
    else
      die "curl or wget is required"
    fi
  fi
  [ "$(head -c 2 "$new" 2>/dev/null)" = "MZ" ] || { rm -f "$new"; die "that file is not a Windows program"; }

  stop_running "$new"   # the new build understands --quit and also closes older ones
  mv -f "$new" "$exe"
  if [ "$startup" = 1 ]; then "$exe" --startup=on </dev/null; else "$exe" --startup=off </dev/null; fi
  cleanup_legacy "$localappdata" "$roaming"
  launch "$exe"

  say "Glance is running. Installed to $(win "$exe")"
  if [ "$startup" = 1 ]; then say "It starts when you sign in (toggle that in its right-click menu)."; fi
  say "Uninstall: curl -fsSL https://raw.githubusercontent.com/$REPO/master/install.sh | bash -s -- --uninstall"
}

# Close a running bar politely so Windows gives its screen edge back, then make sure it is gone.
stop_running() {
  if [ -f "$1" ]; then "$1" --quit </dev/null >/dev/null 2>&1 || true; fi
  wincmd taskkill.exe /IM Glance.exe /F >/dev/null 2>&1 || true
}

# Start Glance detached from this terminal.
launch() {
  local w q="'"
  w=$(win "$1")
  w=${w//$q/$q$q}   # PowerShell single-quote escaping
  if command -v powershell.exe >/dev/null 2>&1; then
    (cd "$(dirname "$1")" && powershell.exe -NoProfile -NonInteractive -Command "Start-Process -FilePath '$w'" </dev/null >/dev/null 2>&1) && return
  fi
  ("$1" </dev/null >/dev/null 2>&1 &)
}

# Files left by earlier, broken versions of this installer.
cleanup_legacy() {
  rm -f "$2/Microsoft/Windows/Start Menu/Programs/Startup/Glance.lnk" "$1/Glance/Glance.exe"
  rmdir "$1/Glance" 2>/dev/null || true
}

main "$@"
