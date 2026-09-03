#!/usr/bin/env bash
#
# One-line Linux / Proton installer. Paste from the README:
#   curl -fsSL https://raw.githubusercontent.com/GrizzlyOne95/bzfile/main/scripts/install_linux.sh | bash -s -- --native
#   curl -fsSL https://raw.githubusercontent.com/GrizzlyOne95/bzfile/main/scripts/install_linux.sh | bash -s -- --snap
#
# Downloads a matched GitHub release zip (bzfile.dll + bzfile_replace_helper.exe).
#
set -euo pipefail

REPO_SLUG="${BZFILE_REPO:-GrizzlyOne95/bzfile}"
REF="${BZFILE_REF:-main}"
FLAVOR="all"
GAME_PATH="${BZR_GAME_PATH:-}"
DLL_PATH="${BZFILE_DLL:-}"

usage() {
    cat <<EOF
Usage:
  install_linux.sh [--native | --snap] [--game-path DIR] [--dll FILE] [--ref git-ref]

    --native      Native Steam and Flatpak installs only
    --snap        Snap Steam installs only
    --game-path   One game directory (overrides flavour filter)
    --dll         Advanced: Win32 bzfile.dll. Requires bzfile_replace_helper.exe
                  in the same directory.
    --ref         Git ref used only to fetch steam_game_paths.sh (default: $REF)

Environment:
  BZFILE_REPO / BZFILE_REF / BZFILE_DLL / BZR_GAME_PATH
EOF
}

validate_ref() {
    local ref="$1"
    if [[ -z "$ref" || ! "$ref" =~ ^[A-Za-z0-9._/-]+$ ]]; then
        echo "Refusing git ref '$ref'." >&2
        exit 1
    fi
    case "$ref" in
        -*|*..*|*//*|*/) echo "Refusing malformed git ref '$ref'." >&2; exit 1 ;;
    esac
}

download_to() {
    local url="$1"
    local out="$2"
    if command -v curl >/dev/null 2>&1; then
        curl -fsSL "$url" -o "$out"
        return
    fi
    if command -v wget >/dev/null 2>&1; then
        wget -qO "$out" "$url"
        return
    fi
    echo "Missing curl or wget." >&2
    exit 2
}

fetch_text() {
    local url="$1"
    if command -v curl >/dev/null 2>&1; then
        curl -fsSL "$url"
        return
    fi
    if command -v wget >/dev/null 2>&1; then
        wget -qO- "$url"
        return
    fi
    echo "Missing curl or wget." >&2
    exit 2
}

latest_asset_url() {
    local repo="$1"
    local needle="$2"
    local json
    json="$(fetch_text "https://api.github.com/repos/${repo}/releases/latest")"
    printf '%s\n' "$json" | tr '"' '\n' | grep -E "^https://github.com/.+/download/.+${needle}$" | head -n1
}

is_snap_game() {
    [[ "$1" == "$HOME/snap/steam/"* ]]
}

filter_flavor() {
    local flavor="$1"
    local kept=()
    local path
    for path in "${BZR_GAME_PATHS[@]:-}"; do
        case "$flavor" in
            all) kept+=("$path") ;;
            snap) is_snap_game "$path" && kept+=("$path") ;;
            native) is_snap_game "$path" || kept+=("$path") ;;
        esac
    done
    if [[ ${#kept[@]} -gt 0 ]]; then
        BZR_GAME_PATHS=("${kept[@]}")
    else
        BZR_GAME_PATHS=()
    fi
}

is_bzfile_dll() {
    local path="$1"
    [[ -f "$path" ]] && grep -a -q "bzfile Error:" "$path"
}

is_bzfile_helper() {
    local path="$1"
    [[ -f "$path" ]] && grep -a -q "bzfile replace helper" "$path"
}

find_helper() {
    local dir="$1"
    HELPER=""
    if [[ -f "$dir/bzfile_replace_helper.exe" ]]; then
        HELPER="$dir/bzfile_replace_helper.exe"
        return 0
    fi
    return 1
}

download_matched_release() {
    local dest="$1"
    mkdir -p "$dest"
    local zip_url
    zip_url="$(latest_asset_url "$REPO_SLUG" '\.zip')"
    if [[ -z "$zip_url" ]]; then
        return 1
    fi
    echo "Downloading matched release zip from $REPO_SLUG ..."
    download_to "$zip_url" "$dest/bzfile-release.zip"
    if ! command -v unzip >/dev/null 2>&1; then
        echo "error: unzip is required to extract the bzfile release zip." >&2
        return 1
    fi
    unzip -qo "$dest/bzfile-release.zip" -d "$dest"
    [[ -s "$dest/bzfile.dll" && -s "$dest/bzfile_replace_helper.exe" ]]
}

deploy_matched() {
    local game_dir="$1" dll="$2" helper="$3"
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
    cp -f "$dll" "$dest_dll"
    cp -f "$helper" "$dest_helper"
    echo "  deployed bzfile.dll ($(stat -c %s "$dest_dll") bytes)"
    echo "  deployed bzfile_replace_helper.exe ($(stat -c %s "$dest_helper") bytes)"
}

while [[ $# -gt 0 ]]; do
    case "$1" in
        --native) FLAVOR="native"; shift ;;
        --snap) FLAVOR="snap"; shift ;;
        --game-path)
            [[ $# -ge 2 ]] || { echo "Missing value for --game-path" >&2; exit 1; }
            GAME_PATH="$2"
            shift 2
            ;;
        --dll)
            [[ $# -ge 2 ]] || { echo "Missing value for --dll" >&2; exit 1; }
            DLL_PATH="$2"
            shift 2
            ;;
        --ref)
            [[ $# -ge 2 ]] || { echo "Missing value for --ref" >&2; exit 1; }
            REF="$2"
            shift 2
            ;;
        -h|--help)
            usage
            exit 0
            ;;
        *)
            echo "Unknown argument: $1" >&2
            usage >&2
            exit 1
            ;;
    esac
done

validate_ref "$REF"

work="$(mktemp -d)"
trap 'rm -rf "$work"' EXIT

src=""
script_dir=""
if [[ -n "${BASH_SOURCE[0]:-}" ]]; then
    script_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")" 2>/dev/null && pwd || true)"
fi
if [[ -n "$script_dir" && -f "$script_dir/steam_game_paths.sh" ]]; then
    src="$script_dir"
else
    download_to "https://raw.githubusercontent.com/${REPO_SLUG}/${REF}/scripts/steam_game_paths.sh" \
        "$work/steam_game_paths.sh"
    src="$work"
fi

# shellcheck source=scripts/steam_game_paths.sh
source "$src/steam_game_paths.sh"

if [[ -n "$GAME_PATH" ]]; then
    BZR_GAME_PATH="$GAME_PATH"
fi
detect_bzr_game_paths
if [[ -z "$GAME_PATH" ]]; then
    filter_flavor "$FLAVOR"
fi

if [[ ${#BZR_GAME_PATHS[@]} -eq 0 ]]; then
    echo "error: no Battlezone 98 Redux install found for this Steam flavour." >&2
    case "$FLAVOR" in
        native) echo "Use the Snap paste command if you installed Steam from Snap." >&2 ;;
        snap) echo "Use the Native/Flatpak paste command if you are not on Snap Steam." >&2 ;;
    esac
    exit 1
fi

dll=""
HELPER=""

if [[ -n "$DLL_PATH" ]]; then
    dll="$DLL_PATH"
    if ! find_helper "$(dirname "$dll")"; then
        echo "error: --dll requires bzfile_replace_helper.exe beside the DLL." >&2
        exit 1
    fi
    echo "Using explicit DLL with matched helper: $dll"
elif [[ -n "$script_dir" && -f "$script_dir/../Release/bzfile.dll" ]] \
    && find_helper "$(cd "$script_dir/.." && pwd)/Release"; then
    dll="$(cd "$script_dir/.." && pwd)/Release/bzfile.dll"
    echo "Using local Release build with matched helper: $dll"
elif download_matched_release "$work/release"; then
    dll="$work/release/bzfile.dll"
    HELPER="$work/release/bzfile_replace_helper.exe"
else
    echo "error: could not download a matched bzfile release zip from $REPO_SLUG." >&2
    echo "Pass --dll with the helper beside it, or set BZFILE_REPO to a repo that publishes releases." >&2
    exit 1
fi

if [[ -z "$dll" || ! -f "$dll" || -z "$HELPER" || ! -f "$HELPER" ]]; then
    echo "error: matched bzfile artifact set is incomplete." >&2
    exit 1
fi

echo "Installing to:"
printf '  %s\n' "${BZR_GAME_PATHS[@]}"

for game_dir in "${BZR_GAME_PATHS[@]}"; do
    deploy_matched "$game_dir" "$dll" "$HELPER"
    echo
done

cat <<'EOF'

Install complete.

bzfile.dll is a Win32 Lua module. Proton loads it as a Windows DLL; no
WINEDLLOVERRIDES entry is required (unlike OpenShim's winmm.dll proxy).
EOF
