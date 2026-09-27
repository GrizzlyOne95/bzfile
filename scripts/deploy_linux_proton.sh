#!/usr/bin/env bash
#
# deploy_linux_proton.sh - Copy a locally built Win32 bzfile.dll and its
# replace helper into Battlezone 98 Redux installs running under Proton.
#
# Build Release | x86 on Windows, then deploy from Linux:
#   ./scripts/deploy_linux_proton.sh [GAME_DIR] [DLL_PATH]
#
# This is a thin wrapper around install_linux.sh --dll, which owns detection,
# the identity checks, backups and the atomic copy. It keeps this script's
# original command line.
#
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
REPO_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"

usage() {
    cat <<USAGE
Usage:
  $0 [GAME_DIR] [DLL_PATH]

Environment:
  BZR_GAME_PATH   Deploy to this directory only (overrides auto-detect)
  STEAM_ROOT      Extra Steam root to scan for libraryfolders.vdf entries

Defaults:
  DLL_PATH        $REPO_ROOT/Release/bzfile.dll (bzfile_replace_helper.exe beside it)
USAGE
}

if [[ "${1:-}" == "-h" || "${1:-}" == "--help" ]]; then
    usage
    exit 0
fi

GAME_DIR="${1:-}"
DLL="${2:-$REPO_ROOT/Release/bzfile.dll}"

if [[ ! -f "$DLL" ]]; then
    echo "error: bzfile.dll not found: $DLL" >&2
    echo "Build Release | x86 on Windows, copy Release/bzfile.dll here, or pass the DLL path." >&2
    exit 1
fi

args=(--dll "$DLL")
if [[ -n "$GAME_DIR" ]]; then
    args+=(--game-path "$GAME_DIR")
fi
exec "$SCRIPT_DIR/install_linux.sh" "${args[@]}"
