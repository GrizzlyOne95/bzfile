#!/usr/bin/env bash
#
# deploy_linux_proton.sh — Copy a Win32 bzfile.dll + replace helper into
# Battlezone 98 Redux installs running under Steam Proton (native, Flatpak,
# or Snap Steam).
#
# bzfile.dll is a Win32 Lua C module. Linux does not produce a native .so.
# Build Release | x86 on Windows, then deploy from Linux:
#   ./scripts/deploy_linux_proton.sh [GAME_DIR] [DLL_PATH]
#
# The helper must sit beside the DLL. Default DLL path is Release/bzfile.dll.
#
set -euo pipefail

APPID=301650
SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
REPO_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"

# shellcheck source=scripts/steam_game_paths.sh
source "$SCRIPT_DIR/steam_game_paths.sh"

usage() {
    cat <<EOF
Usage:
  $0 [GAME_DIR] [DLL_PATH]

Environment:
  BZR_GAME_PATH   Deploy to this directory only (overrides auto-detect)
  STEAM_ROOT      Extra Steam root to scan for libraryfolders.vdf entries

Defaults:
  DLL_PATH        $REPO_ROOT/Release/bzfile.dll
EOF
}

GAME_DIR="${1:-}"
DLL="${2:-$REPO_ROOT/Release/bzfile.dll}"
HELPER="$(dirname "$DLL")/bzfile_replace_helper.exe"

if [[ "${1:-}" == "-h" || "${1:-}" == "--help" ]]; then
    usage
    exit 0
fi

if [[ -n "$GAME_DIR" ]]; then
    BZR_GAME_PATH="$GAME_DIR"
fi

detect_bzr_game_paths

if [[ ${#BZR_GAME_PATHS[@]} -eq 0 ]]; then
    echo "error: could not find Battlezone 98 Redux (AppID $APPID)." >&2
    echo "Install the game in Steam, or pass the game directory explicitly." >&2
    echo >&2
    echo "Typical paths:" >&2
    echo "  Native:  ~/.local/share/Steam/steamapps/common/Battlezone 98 Redux" >&2
    echo "  Flatpak: ~/.var/app/com.valvesoftware.Steam/data/Steam/steamapps/common/Battlezone 98 Redux" >&2
    echo "  Snap:    ~/snap/steam/common/.local/share/Steam/steamapps/common/Battlezone 98 Redux" >&2
    exit 1
fi

if [[ ! -f "$DLL" ]]; then
    echo "error: bzfile.dll not found: $DLL" >&2
    echo "Build Release | x86 on Windows, copy Release/bzfile.dll here, or pass the DLL path." >&2
    exit 1
fi

if [[ ! -f "$HELPER" ]]; then
    echo "error: bzfile_replace_helper.exe not found beside the DLL: $HELPER" >&2
    echo "Ship the helper with the DLL; deferred replace needs both." >&2
    exit 1
fi

is_bzfile_dll() {
    local path="$1"
    [[ -f "$path" ]] && grep -a -q "bzfile Error:" "$path"
}

is_bzfile_helper() {
    local path="$1"
    [[ -f "$path" ]] && grep -a -q "bzfile replace helper" "$path"
}

deploy_one() {
    local game_dir="$1"
    local dest_dll="$game_dir/bzfile.dll"
    local dest_helper="$game_dir/bzfile_replace_helper.exe"

    if [[ -f "$dest_dll" ]] && ! is_bzfile_dll "$dest_dll"; then
        echo "error: refusing to overwrite non-bzfile bzfile.dll in $game_dir" >&2
        echo "Remove or rename that file first if you intend to replace it." >&2
        return 1
    fi
    if [[ -f "$dest_helper" ]] && ! is_bzfile_helper "$dest_helper"; then
        echo "error: refusing to overwrite unexpected bzfile_replace_helper.exe in $game_dir" >&2
        return 1
    fi

    echo "Installing bzfile to: $game_dir"
    local stamp
    stamp="$(date +%Y%m%d-%H%M%S)"
    if [[ -f "$dest_dll" ]]; then
        cp -f "$dest_dll" "$dest_dll.bak-$stamp"
    fi
    if [[ -f "$dest_helper" ]]; then
        cp -f "$dest_helper" "$dest_helper.bak-$stamp"
    fi
    cp -f "$DLL" "$dest_dll"
    cp -f "$HELPER" "$dest_helper"
    echo "  deployed bzfile.dll ($(stat -c %s "$dest_dll") bytes)"
    echo "  deployed bzfile_replace_helper.exe ($(stat -c %s "$dest_helper") bytes)"
}

for game_dir in "${BZR_GAME_PATHS[@]}"; do
    deploy_one "$game_dir"
    echo
done

cat <<'EOF'
Install complete.

bzfile.dll is a Win32 Lua module. Proton loads it as a Windows DLL; no
WINEDLLOVERRIDES entry is required (unlike OpenShim's winmm.dll proxy).
EOF
