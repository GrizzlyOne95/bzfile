#!/usr/bin/env bash
#
# steam_game_paths.sh — Locate Battlezone 98 Redux installs under native,
# Flatpak, and Snap Steam layouts.
#
# Usage:
#   source scripts/steam_game_paths.sh
#   detect_bzr_game_paths
#   printf '%s\n' "${BZR_GAME_PATHS[@]}"
#
# Each returned path contains battlezone98redux.exe or BZR.exe, is canonical
# (symlink aliases of one physical install collapse to a single entry), and has
# a matching entry in BZR_GAME_FLAVORS describing the Steam package that found
# it: native (native or Flatpak Steam), snap, or any (found by both, or given
# explicitly by the caller).

: "${BZR_GAME_NAME:=Battlezone 98 Redux}"
: "${BZR_STEAM_APPID:=301650}"

_bzr_game_exe_present() {
    local game_dir="$1"
    [[ -f "$game_dir/battlezone98redux.exe" || -f "$game_dir/BZR.exe" ]]
}

# Resolve symlinks so that ~/.steam/steam and ~/.local/share/Steam aliases of
# one physical Steam root do not yield the same install more than once.
_bzr_canonical_path() {
    local path="$1"
    local resolved=""

    if command -v realpath >/dev/null 2>&1; then
        resolved="$(realpath "$path" 2>/dev/null || true)"
    fi
    if [[ -z "$resolved" ]] && command -v readlink >/dev/null 2>&1; then
        resolved="$(readlink -f "$path" 2>/dev/null || true)"
    fi
    if [[ -z "$resolved" && -d "$path" ]]; then
        resolved="$(cd "$path" 2>/dev/null && pwd -P || true)"
    fi

    printf '%s' "${resolved:-$path}"
}

_bzr_add_game_path() {
    local candidate="$1"
    local flavor="${2:-${_BZR_CURRENT_FLAVOR:-any}}"
    [[ -n "$candidate" ]] || return 0
    [[ -d "$candidate" ]] || return 0
    _bzr_game_exe_present "$candidate" || return 0

    candidate="$(_bzr_canonical_path "$candidate")"

    local i
    for ((i = 0; i < ${#BZR_GAME_PATHS[@]}; i++)); do
        if [[ "${BZR_GAME_PATHS[$i]}" == "$candidate" ]]; then
            # Same physical install reachable from more than one Steam
            # package: keep it for either flavour filter.
            [[ "${BZR_GAME_FLAVORS[$i]}" == "$flavor" ]] || BZR_GAME_FLAVORS[$i]="any"
            return 0
        fi
    done

    BZR_GAME_PATHS+=("$candidate")
    BZR_GAME_FLAVORS+=("$flavor")
}

_bzr_add_library_root() {
    local library_root="$1"
    [[ -n "$library_root" ]] || return 0
    _bzr_add_game_path "$library_root/steamapps/common/$BZR_GAME_NAME"
}

_bzr_parse_libraryfolders_vdf() {
    local vdf="$1"
    [[ -f "$vdf" ]] || return 0

    local line path
    while IFS= read -r line || [[ -n "$line" ]]; do
        # "path" "<steam library root>"
        if [[ "$line" =~ \"path\"[[:space:]]+\"(.*)\" ]]; then
            path="${BASH_REMATCH[1]}"
            path="${path//\\\\//}"
            _bzr_add_library_root "$path"
        fi
    done < "$vdf"
}

# Libraries reached through libraryfolders.vdf can live anywhere on disk, so
# the flavour has to come from the Steam root that listed them, never from the
# final game pathname.
_bzr_add_steam_root() {
    local steam_root="$1"
    local flavor="${2:-any}"
    [[ -d "$steam_root" ]] || return 0

    local _BZR_CURRENT_FLAVOR="$flavor"
    _bzr_add_library_root "$steam_root"
    _bzr_parse_libraryfolders_vdf "$steam_root/steamapps/libraryfolders.vdf"
    _bzr_parse_libraryfolders_vdf "$steam_root/config/libraryfolders.vdf"
}

detect_bzr_game_paths() {
    BZR_GAME_PATHS=()
    BZR_GAME_FLAVORS=()

    # "<flavour>|<steam root>"; Flatpak counts as native for install flavours.
    local steam_roots=(
        "native|$HOME/.local/share/Steam"
        "native|$HOME/.steam/steam"
        "native|$HOME/.steam/root"
        "native|$HOME/.var/app/com.valvesoftware.Steam/data/Steam"
        "native|$HOME/.var/app/com.valvesoftware.Steam/.local/share/Steam"
        "snap|$HOME/snap/steam/common/.local/share/Steam"
        "snap|$HOME/snap/steam/current/.local/share/Steam"
    )

    local entry flavor steam_root
    for entry in "${steam_roots[@]}"; do
        flavor="${entry%%|*}"
        steam_root="${entry#*|}"
        _bzr_add_steam_root "$steam_root" "$flavor"
    done

    if [[ -n "${STEAM_ROOT:-}" ]]; then
        _bzr_add_steam_root "$STEAM_ROOT" "any"
    fi

    if [[ -n "${BZR_GAME_PATH:-}" ]]; then
        BZR_GAME_PATHS=("$BZR_GAME_PATH")
        BZR_GAME_FLAVORS=("any")
    fi
}
