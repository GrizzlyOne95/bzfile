#!/usr/bin/env bash
#
# Host-side Linux checks for bzfile. These do not build the Win32 DLL.
#
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
cd "$ROOT"

TMP_ROOT="$(mktemp -d)"
trap 'rm -rf "$TMP_ROOT"' EXIT

fail() {
    echo "FAIL: $*" >&2
    exit 1
}

pass() {
    echo "OK: $*"
}

canon() {
    if command -v realpath >/dev/null 2>&1; then
        realpath "$1"
    else
        (cd "$1" && pwd -P)
    fi
}

workdir() {
    mktemp -d "$TMP_ROOT/$1.XXXXXX"
}

make_game_dir() {
    local dir="$1"
    mkdir -p "$dir"
    : >"$dir/battlezone98redux.exe"
}

# Stand-ins for the release binaries, carrying the same identifying text in the
# same encoding: the DLL's "bzfile Error:" messages are ASCII, the helper's log
# text is a UTF-16LE literal.
make_stub_binaries() {
    local dir="$1" text="bzfile replace helper started." i
    printf 'MZ bzfile Error: stub\n' >"$dir/bzfile.dll"
    {
        printf 'MZ'
        for ((i = 0; i < ${#text}; i++)); do
            printf '%s\0' "${text:i:1}"
        done
    } >"$dir/bzfile_replace_helper.exe"
}

write_libraryfolders() {
    local vdf="$1" library_root="$2"
    mkdir -p "$(dirname "$vdf")"
    {
        printf '"libraryfolders"\n{\n'
        printf '\t"0"\n\t{\n\t\t"path"\t\t"%s"\n\t}\n' "$library_root"
        printf '}\n'
    } >"$vdf"
}

# Detection in an isolated HOME, printing "<flavour>|<path>" per install.
detect_in_home() {
    local home="$1"
    env -u BZR_GAME_PATH -u STEAM_ROOT HOME="$home" bash -c '
        set -uo pipefail
        # shellcheck source=scripts/steam_game_paths.sh
        source "$1/scripts/steam_game_paths.sh"
        detect_bzr_game_paths
        for ((i = 0; i < ${#BZR_GAME_PATHS[@]}; i++)); do
            printf "%s|%s\n" "${BZR_GAME_FLAVORS[$i]}" "${BZR_GAME_PATHS[$i]}"
        done
    ' _ "$ROOT"
}

# Run the installer against an isolated HOME; echoes output, returns its status.
run_installer() {
    local home="$1"
    shift
    local status=0
    env -u BZR_GAME_PATH -u STEAM_ROOT HOME="$home" \
        "$ROOT/scripts/install_linux.sh" "$@" 2>&1 || status=$?
    return $status
}

# bzfile_version.h spells one version several ways: the four numbers, the
# resource tuple (what GetFileVersion and Explorer report), the release string
# and the resource string. CI only compares the resource string with the tag,
# so every spelling must agree with the four numbers.
test_version_header() {
    local header="$ROOT/include/bzfile_version.h"
    macro() {
        sed -n -E "s/^#define[[:space:]]+BZFILE_VERSION_$1[[:space:]]+(.*[^[:space:]])[[:space:]]*\$/\1/p" "$header" \
            | tr -d '\r'
    }
    local major minor patch build
    major="$(macro MAJOR)"
    minor="$(macro MINOR)"
    patch="$(macro PATCH)"
    build="$(macro BUILD)"
    [[ "$major$minor$patch$build" =~ ^[0-9]+$ ]] \
        || fail "BZFILE_VERSION_MAJOR/MINOR/PATCH/BUILD missing or not numeric"

    local expected_tuple="$major,$minor,$patch,$build"
    local expected_string="\"$major.$minor.$patch\""
    local expected_file_string="\"$major.$minor.$patch.$build\\0\""
    [[ "$(macro TUPLE)" == "$expected_tuple" ]] \
        || fail "BZFILE_VERSION_TUPLE is '$(macro TUPLE)', expected '$expected_tuple'"
    [[ "$(macro STRING)" == "$expected_string" ]] \
        || fail "BZFILE_VERSION_STRING is $(macro STRING), expected $expected_string"
    [[ "$(macro FILE_STRING)" == "$expected_file_string" ]] \
        || fail "BZFILE_VERSION_FILE_STRING is $(macro FILE_STRING), expected $expected_file_string"
    pass "version header spellings agree ($major.$minor.$patch.$build)"
}

# The write-protection rule shared by every mutating bzfile binding is pure
# C++, so it is tested here even though the DLL itself only builds with MSVC.
test_write_policy() {
    local cxx="${CXX:-}" candidate
    if [[ -z "$cxx" ]]; then
        for candidate in g++ clang++; do
            if command -v "$candidate" >/dev/null 2>&1; then
                cxx="$candidate"
                break
            fi
        done
    fi
    [[ -n "$cxx" ]] || fail "no C++ compiler for tests/host/write_policy_tests.cpp"

    "$cxx" -std=c++17 -Wall -Wextra -Werror \
        -o "$TMP_ROOT/write_policy_tests" "$ROOT/tests/host/write_policy_tests.cpp" \
        || fail "tests/host/write_policy_tests.cpp did not compile"
    "$TMP_ROOT/write_policy_tests" || fail "write policy checks failed"
    pass "write policy"
}

# The shared BZR documents must stay byte-identical across ExtraUtilities,
# BZR-OpenShim, Campaign Reimagined and bzfile. Each repository pins the same
# SHA-256 hashes (of the file bytes with CRLF normalised to LF, so Windows and
# Linux checkouts agree); see GrizzlyOne95/ExtraUtilities#65.
test_shared_bzr_docs() {
    local -A expected=(
        [Docs/BZR_LUA_AGENT_REFERENCE.md]=ed560acf91206732bacb173e3b56ddb501db85f84393bc8f2e89b00ca4df6bca
        [Docs/BZR_PLATFORM_COMPATIBILITY.md]=b9af9f6452996080a046949f3164e102ec8aa4eefaa9d0c4b8d194e52b516fb3
    )
    local hasher doc actual
    if command -v sha256sum >/dev/null 2>&1; then
        hasher=(sha256sum)
    elif command -v shasum >/dev/null 2>&1; then
        hasher=(shasum -a 256)
    else
        fail "no sha256sum or shasum for the shared BZR document check"
    fi
    for doc in "${!expected[@]}"; do
        [[ -f "$ROOT/$doc" ]] || fail "shared document $doc is missing"
        actual="$(perl -0777 -pe 's/\r\n/\n/g' "$ROOT/$doc" | "${hasher[@]}" | cut -d' ' -f1)"
        [[ "$actual" == "${expected[$doc]}" ]] \
            || fail "$doc has SHA-256 $actual, expected ${expected[$doc]}. Make the same change in ExtraUtilities, BZR-OpenShim, Campaign Reimagined and bzfile, then update the pinned hashes in each repository's check."
    done
    pass "shared BZR documents match the pinned hashes"
}

test_script_syntax() {
    local script
    for script in \
        "$ROOT/scripts/steam_game_paths.sh" \
        "$ROOT/scripts/deploy_linux_proton.sh" \
        "$ROOT/scripts/install_linux.sh" \
        "$ROOT/tests/linux/run.sh"
    do
        bash -n "$script" || fail "bash -n failed: $script"
    done
    pass "installer scripts parse"
}

test_steam_path_override() {
    # shellcheck source=scripts/steam_game_paths.sh
    source "$ROOT/scripts/steam_game_paths.sh"
    local fake
    fake="$(workdir override)"
    : >"$fake/battlezone98redux.exe"
    BZR_GAME_PATH="$fake"
    detect_bzr_game_paths
    local ok=0
    [[ ${#BZR_GAME_PATHS[@]} -eq 1 && "${BZR_GAME_PATHS[0]}" == "$fake" ]] && ok=1
    unset BZR_GAME_PATH
    [[ "$ok" -eq 1 ]] || fail "BZR_GAME_PATH override was not honoured"
    pass "Steam path override"
}

test_help_exits_clean() {
    "$ROOT/scripts/install_linux.sh" --help >/dev/null
    "$ROOT/scripts/deploy_linux_proton.sh" --help >/dev/null
    pass "installer --help"
}

# No install must stay a zero-length array. An empty-string element would slip
# past the "no install found" guard and deploy to "/bzfile.dll".
test_no_install_is_empty() {
    local home
    home="$(workdir emptyhome)"

    local detected
    detected="$(detect_in_home "$home")"
    [[ -z "$detected" ]] || fail "detection with no install returned: $detected"

    local out status=0
    out="$(run_installer "$home" --native)" || status=$?
    [[ $status -eq 1 ]] \
        || fail "install_linux.sh --native with no install exited $status (expected 1)"
    grep -q "no Battlezone 98 Redux install found" <<<"$out" \
        || fail "install_linux.sh --native with no install printed: $out"
    if grep -q "Installing bzfile to:" <<<"$out"; then
        fail "install_linux.sh attempted a deployment with no install detected"
    fi

    pass "no install: detection stays empty and --native fails cleanly"
}

# ~/.steam/steam and ~/.steam/root are commonly symlinks to the same physical
# Steam root; one install must not be deployed to (and backed up) repeatedly.
test_symlink_aliases_dedupe() {
    local home game expected detected
    home="$(workdir aliashome)"
    game="$home/.local/share/Steam/steamapps/common/Battlezone 98 Redux"
    make_game_dir "$game"
    mkdir -p "$home/.steam"
    ln -s "$home/.local/share/Steam" "$home/.steam/steam"
    ln -s "$home/.local/share/Steam" "$home/.steam/root"

    detected="$(detect_in_home "$home")"
    expected="native|$(canon "$game")"
    [[ "$detected" == "$expected" ]] \
        || fail "symlinked Steam aliases returned: $detected (expected exactly: $expected)"

    pass "Steam symlink aliases collapse to one install"
}

# libraryfolders.vdf can point anywhere on disk, so flavour has to come from the
# Steam root that listed the library, not from the final game pathname.
test_external_library_flavours() {
    local home ext game detected

    home="$(workdir nativehome)"
    ext="$(workdir nativelib)"
    game="$ext/steamapps/common/Battlezone 98 Redux"
    make_game_dir "$game"
    write_libraryfolders "$home/.local/share/Steam/steamapps/libraryfolders.vdf" "$ext"
    detected="$(detect_in_home "$home")"
    [[ "$detected" == "native|$(canon "$game")" ]] \
        || fail "external native library returned: $detected"

    home="$(workdir snaphome)"
    ext="$(workdir snaplib)"
    game="$ext/steamapps/common/Battlezone 98 Redux"
    make_game_dir "$game"
    write_libraryfolders \
        "$home/snap/steam/common/.local/share/Steam/steamapps/libraryfolders.vdf" "$ext"
    detected="$(detect_in_home "$home")"
    [[ "$detected" == "snap|$(canon "$game")" ]] \
        || fail "external Snap library returned: $detected"

    pass "external Steam libraries keep their discovering flavour"
}

# The same external pathname must not be cross-classified by --native/--snap.
test_flavour_filter_uses_discovering_root() {
    local home ext game artifacts out status

    home="$(workdir filterhome)"
    ext="$(workdir filterlib)"
    game="$ext/steamapps/common/Battlezone 98 Redux"
    make_game_dir "$game"
    write_libraryfolders "$home/.local/share/Steam/steamapps/libraryfolders.vdf" "$ext"

    status=0
    out="$(run_installer "$home" --snap)" || status=$?
    [[ $status -eq 1 ]] \
        || fail "--snap accepted an external native library (exit $status)"
    grep -q "no Battlezone 98 Redux install found" <<<"$out" \
        || fail "--snap on an external native library printed: $out"

    artifacts="$(workdir artifacts)"
    make_stub_binaries "$artifacts"

    status=0
    out="$(run_installer "$home" --native --dll "$artifacts/bzfile.dll")" || status=$?
    [[ $status -eq 0 ]] \
        || fail "--native rejected an external native library (exit $status): $out"
    [[ -s "$game/bzfile.dll" && -s "$game/bzfile_replace_helper.exe" ]] \
        || fail "--native did not deploy into the external library: $out"

    pass "flavour filter follows the discovering Steam root"
}

# Installing again over our own files is the upgrade path. The helper's
# identity check used to look for ASCII text the real (UTF-16) helper does not
# contain, so every second install refused to replace it.
test_reinstall_over_existing() {
    local home game artifacts out status run

    home="$(workdir reinstallhome)"
    game="$home/.local/share/Steam/steamapps/common/Battlezone 98 Redux"
    make_game_dir "$game"
    artifacts="$(workdir reinstallartifacts)"
    make_stub_binaries "$artifacts"

    for run in first second; do
        status=0
        out="$(run_installer "$home" --native --dll "$artifacts/bzfile.dll")" || status=$?
        [[ $status -eq 0 ]] || fail "$run install exited $status: $out"
    done
    cmp -s "$artifacts/bzfile_replace_helper.exe" "$game/bzfile_replace_helper.exe" \
        || fail "re-install did not refresh the helper"

    # A foreign file under the helper's name must still be refused.
    printf 'not ours\n' >"$game/bzfile_replace_helper.exe"
    status=0
    out="$(run_installer "$home" --native --dll "$artifacts/bzfile.dll")" || status=$?
    [[ $status -ne 0 ]] || fail "installer overwrote a foreign bzfile_replace_helper.exe"

    pass "re-install replaces our own helper and refuses a foreign one"
}

# An explicit game path must be a game folder, like an auto-detected one.
test_game_path_must_be_a_game() {
    local home not_game artifacts out status
    home="$(workdir gamepathhome)"
    not_game="$(workdir notagame)"
    artifacts="$(workdir gamepathartifacts)"
    make_stub_binaries "$artifacts"

    status=0
    out="$(run_installer "$home" --game-path "$not_game" --dll "$artifacts/bzfile.dll")" || status=$?
    [[ $status -ne 0 ]] || fail "--game-path accepted a folder without the game: $out"
    [[ ! -e "$not_game/bzfile.dll" ]] || fail "--game-path deployed into a non-game folder"
    pass "--game-path requires a game folder"
}

# Backups are capped, --uninstall removes only our files, and the deploy
# script (now a wrapper) still works with its original arguments.
test_backups_uninstall_and_deploy_wrapper() {
    local home game artifacts out status stamp count
    home="$(workdir lifecyclehome)"
    game="$(workdir lifecyclegame)"
    make_game_dir "$game"
    artifacts="$(workdir lifecycleartifacts)"
    make_stub_binaries "$artifacts"

    status=0
    out="$(env -u BZR_GAME_PATH -u STEAM_ROOT HOME="$home" \
        "$ROOT/scripts/deploy_linux_proton.sh" "$game" "$artifacts/bzfile.dll" 2>&1)" || status=$?
    [[ $status -eq 0 && -s "$game/bzfile.dll" && -s "$game/bzfile_replace_helper.exe" ]] \
        || fail "deploy_linux_proton.sh GAME_DIR DLL_PATH failed (exit $status): $out"

    for stamp in 20260101-000001 20260101-000002 20260101-000003 20260101-000004 20260101-000005; do
        cp "$game/bzfile.dll" "$game/bzfile.dll.bak-$stamp"
    done
    run_installer "$home" --game-path "$game" --dll "$artifacts/bzfile.dll" >/dev/null \
        || fail "re-install for backup pruning failed"
    count="$(find "$game" -maxdepth 1 -name 'bzfile.dll.bak-*' | wc -l)"
    [[ "$count" -eq 3 ]] || fail "expected 3 bzfile.dll backups after pruning, found $count"
    [[ ! -e "$game/bzfile.dll.bak-20260101-000001" ]] || fail "pruning kept the oldest backup"
    [[ ! -e "$game/bzfile.dll.installing" ]] || fail "install left a staging file behind"

    printf 'not ours\n' >"$game/bzfile_replace_helper.exe"
    run_installer "$home" --game-path "$game" --uninstall >/dev/null || fail "--uninstall failed"
    [[ ! -e "$game/bzfile.dll" ]] || fail "--uninstall kept bzfile.dll"
    [[ -z "$(find "$game" -maxdepth 1 -name 'bzfile.dll.bak-*')" ]] || fail "--uninstall kept DLL backups"
    [[ -e "$game/bzfile_replace_helper.exe" ]] || fail "--uninstall removed a foreign helper"
    [[ -e "$game/battlezone98redux.exe" ]] || fail "--uninstall touched the game"

    pass "backups are capped, --uninstall removes only bzfile's files, deploy wrapper works"
}

test_version_header
test_shared_bzr_docs
test_write_policy
test_script_syntax
test_steam_path_override
test_help_exits_clean
test_no_install_is_empty
test_symlink_aliases_dedupe
test_external_library_flavours
test_flavour_filter_uses_discovering_root
test_reinstall_over_existing
test_game_path_must_be_a_game
test_backups_uninstall_and_deploy_wrapper

echo "All Linux host checks passed."
