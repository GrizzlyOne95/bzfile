# Worksheet C: scripts, CI, build, vendored libs, docs and repository hygiene (2026-09-27)

Reviewer scope (all read end to end): `scripts/install_linux.sh` (300), `scripts/deploy_linux_proton.sh` (124), `scripts/steam_game_paths.sh` (132), `tests/linux/run.sh` (224), `.github/workflows/build.yml` (119), `.github/workflows/linux.yml` (31), `bzfile.sln` (31), `bzfile.vcxproj` (124), `bzfile_replace_helper.vcxproj` (110), `.gitignore` (457), `.gitattributes` (66), `LICENSE` (21), `README.md` (226), `AGENTS.md` (32), `.jules/sentinel.md` (4), `branding/repo_icon.svg` (1 line, 645 bytes, no trailing newline), `include/lua.h` (388), `lauxlib.h` (174), `lualib.h` (53), `luaconf.h` (766), `lua.hpp` (9), `include/bzfile_version.h` (12), `src/bzfile_version.rc` (36), `src/bzfile_replace_helper_version.rc` (36), `src/version.cpp` (8), `lib/Lua5.1-BZR.lib` (1,194,898 B), `lib/Lua5.1-BZR-debug.lib` (1,368,644 B), `lib/Lua5.1-BZR.lib.bak-20260314` (2,224,490 B), `Docs/BZR_LUA_AGENT_REFERENCE.md` (1214) and `Docs/BZR_PLATFORM_COMPATIBILITY.md` (87) (hashed; platform doc read for policy), untracked/ignored `Release/`, `bzfile/Release/`, `bzfile_r.F743A764/Release/`, `lib-temp/`. Read only for claim-checking (owned by worksheets A/B): `src/LuaExport.cpp` (1589: marker strings, root resolution 30-152, helper lookup 812/1150/1334, Workshop-item gate 1116-1121, `luaopen_bzfile` 1564-1588), `src/bzfile_replace_helper.cpp` (640: marker strings 567/638), `tests/Test-ReplaceHelper.ps1` (126: parse check and CI wiring only). Base: origin/main 804ebae.

Commands run (all read-only): `git status --short --ignored` (clean; `!! Release/ bzfile/ bzfile_r.F743A764/`; `lib-temp/` is an empty directory, invisible to git); `git ls-files --eol` (every text file `i/lf w/crlf`, `bzfile.sln` `w/mixed`); `git ls-files -s scripts tests` (all `.sh` mode 100755); `git log -S IsWriteProtected` (first in e7807df 2026-06-23); `git tag` (`latest`, `v1.0.0`), `git describe` (`v1.0.0-10-g804ebae`), `git diff --stat v1.0.0..HEAD -- src include lib *.vcxproj *.sln` (empty: no binary-affecting change since the tag); `gh release list` / `gh api .../releases/latest` (Latest = v1.0.0 with `bzfile-v1.0.0.zip` + `SHA256SUMS.txt`; a second release "Latest Build" on tag `latest` = ca3fd70, 2026-06-24, asset `bzfile-release.zip`); `bash -n` on all four scripts (rc 0); shellcheck not installed (not run); `pwsh` AST parse of `tests/Test-ReplaceHelper.ps1` (0 errors); `llvm-ar x` / `llvm-objdump -d -r` / `llvm-nm` / `llvm-readobj` (VS 2022 bundled LLVM) on the three tracked libs, EXU's locally built `ExtraUtilities/lib/Lua5.1-BZR.lib`, the local `Release/bzfile.dll` + helper, and the Workshop item 3686673790 DLL (read only); decoded the MSBuild tlogs under `bzfile/Release` and `bzfile_r.F743A764/Release` (UTF-16) for the real compiler/linker command lines; `sha256sum` of the shared docs in all four repos plus `git rev-parse origin/main:<doc>` blobs; `git archive 804ebae` into the scratchpad and ran `bash tests/linux/run.sh` there (confirmed read-only on this host after reading it end to end: it writes only under `mktemp -d`, runs the installer with `HOME=<tmp>` and never reaches the network path): 5 checks OK, then **FAIL** at `test_symlink_aliases_dedupe` because MSYS `ln -s` deep-copies directories (with `MSYS=winsymlinks:nativestrict`: "Operation not permitted"); with that one call removed from a scratch copy, all remaining checks pass (rc 0). Reproduced C-1 and C-5 entirely inside the scratchpad (fake game dir with an empty `battlezone98redux.exe`, copies of the local `Release/` binaries). Read-only look at the GOG and Steam installs: no bzfile in either game root; `Lua 5.1.5` string present in the GOG exe; Workshop item 3686673790 carries `bzfile.dll` + `bzfile_replace_helper.exe`.

## 1. File disposition table

| Path | Purpose (1 line) | Referenced by | Verdict |
|---|---|---|---|
| `scripts/install_linux.sh` | one-line Proton installer: fetch latest release zip, deploy DLL + helper to detected Steam installs | README:15,21, AGENTS:23, run.sh | keep-fix (C-1, C-4, C-5, C-6, C-7, C-8) |
| `scripts/deploy_linux_proton.sh` | copy a local `Release/` pair into detected installs | README:29, AGENTS:23, run.sh (`bash -n`, `--help` only) | remove, or reduce to a wrapper over `install_linux.sh --dll` (C-13); shares C-1/C-5/C-7 |
| `scripts/steam_game_paths.sh` | sourced Steam library discovery (native/Flatpak/Snap) with flavour tags | install_linux (local, or fetched from `main`), deploy, run.sh | keep-fix (C-5, C-8) |
| `tests/linux/run.sh` | Linux host lane (8 checks, section 3) | linux.yml | keep-fix (C-11) |
| `tests/Test-ReplaceHelper.ps1` | helper transaction test | build.yml:69 | keep (worksheet B) |
| `.github/workflows/build.yml` | Windows build + helper test + tag release | GitHub | keep-fix (C-3, C-12) |
| `.github/workflows/linux.yml` | runs run.sh | GitHub | keep-fix (C-12: tags, shellcheck) |
| `bzfile.sln` | 2 projects, `x86` -> `Win32` mapping, GUIDs match both vcxproj | CI, VS | keep (working tree mixed EOL, local only) |
| `bzfile.vcxproj` | bzfile.dll | sln | keep-fix (C-15) |
| `bzfile_replace_helper.vcxproj` | helper exe | sln; ProjectReference from bzfile.vcxproj:117-120 (build order) | keep-fix (C-15) |
| `include/lua.h`, `lauxlib.h`, `lualib.h`, `lua.hpp`, `luaconf.h` | vendored Lua 5.1.5 headers | vcxproj include path | keep (identical to EXU `include/`, section 5) |
| `include/bzfile_version.h` | version identity | both `.rc`, version.cpp, build.yml, run.sh | keep-fix (C-3) |
| `src/bzfile_version.rc`, `src/bzfile_replace_helper_version.rc` | VERSIONINFO from header macros (ASCII, diffable) | vcxproj | keep |
| `lib/Lua5.1-BZR.lib` | Release static Lua 5.1.5 core, `/MT`, non-LTCG, `dummynode` patched | bzfile.vcxproj:104 | keep; document provenance (C-2, C-10) |
| `lib/Lua5.1-BZR-debug.lib` | Debug static Lua core, `/MTd` (LIBCMTD), `dummynode` patched | bzfile.vcxproj:75 (Debug only, never built in CI) | keep, or drop together with the Debug config |
| `lib/Lua5.1-BZR.lib.bak-20260314` | previous Release lib: `/GL` LTCG IL objects (toolset-locked) | nothing | remove (C-10) |
| `.gitignore` | toptal windows/visualstudio/c++ template | git | keep; add `*.bak*`, `lib-temp/` (C-10) |
| `.gitattributes` | VS template + `include/** linguist-vendored` | git | keep-fix (C-9) |
| `LICENSE` | MIT, "Copyright (c) 2025 VTrider" | GitHub | keep (section 9) |
| `README.md` | install + API + release docs | users | keep-fix (C-14) |
| `AGENTS.md` | agent rules | agents | keep-fix (C-14: line 8 CR path) |
| `.jules/sentinel.md` | write-protection learning note | AGENTS:16 | keep; fix date (section 9) |
| `branding/repo_icon.svg` | 512x512 repo icon | GitHub settings only | keep; valid XML (`xml.dom.minidom` parses, root `svg`), not empty; `wc -l` = 0 only because there is no trailing newline |
| `Docs/BZR_LUA_AGENT_REFERENCE.md`, `Docs/BZR_PLATFORM_COMPATIBILITY.md` | shared cross-repo docs | AGENTS:13-17 | keep; byte-identical in all four repos (section 6) |
| `Release/` (untracked) | MSBuild OutDir (`$(SolutionDir)$(Configuration)\`): dll/exe/pdb/implib/exp, built 2026-09-19 | install_linux.sh:268-271 and deploy:37 read it | ignored by `[Rr]elease/` (.gitignore:85). OK |
| `bzfile/Release/`, `bzfile_r.F743A764/Release/` (untracked) | MSBuild IntDirs: VS 17 defaults to `$(ProjectName)\$(Configuration)\` when two projects share one directory; the helper's name is shortened with its GUID prefix (`F743A764`) | tlogs only | ignored by `[Rr]elease/`. OK; an explicit `<IntDir>obj\$(ProjectName)\$(Configuration)\</IntDir>` would make this self-explanatory |
| `lib-temp/` (untracked, empty, 2026-03-14) | leftover from the lib refresh (e622a60, same day) | nothing | delete locally; git cannot see it |

## 2. Findings

| ID | [Sev/Conf] | file:line | Finding | Why it matters | Suggested fix | How verified |
|---|---|---|---|---|---|---|
| C-1 | [Med/High] | `scripts/install_linux.sh:120-123,170-173`; `scripts/deploy_linux_proton.sh:79-82,94-97`; `src/bzfile_replace_helper.cpp:567,638`; `tests/linux/run.sh:202-203` | The "is this our helper?" probe is `grep -a -q "bzfile replace helper"`, but the real helper contains that text only as UTF-16 wide literals (`L"bzfile replace helper started."`); there is no ASCII occurrence (the VERSIONINFO strings are UTF-16 too, and say "deferred replacement helper"). After the first install, every re-run of either script stops with `refusing to overwrite unexpected bzfile_replace_helper.exe` (rc 1). The Linux lane cannot see it because its fixture writes an ASCII stub. | Upgrades through the documented one-liner are impossible; users must hand-delete the helper, which is the habit the guard exists to discourage. Fails closed (the refusal happens before any copy), hence Med not High. | Give the helper an ASCII marker that survives `/OPT:REF` (worksheet B), or match the UTF-16LE bytes (`grep -a -q -P 'b\x00z\x00f\x00i\x00l\x00e\x00 \x00r\x00e\x00p\x00l\x00a\x00c\x00e\x00'`). In run.sh assert that the chosen marker literally exists in the source file so the stub cannot drift. | Python byte search of local `Release/bzfile_replace_helper.exe`: ASCII not found, UTF-16LE at offset 236352; `git show v1.0.0:src/bzfile_replace_helper.cpp` has the same `L"..."` so the v1.0.0 release is affected too; scratchpad reproduction: `install_linux.sh --game-path <tmp game> --dll <copy of Release/bzfile.dll>` run 1 rc 0, run 2 rc 1 with the refusal. The DLL probe `bzfile Error:` is ASCII and present 3x (fine). |
| C-2 | [Med/High] | `lib/Lua5.1-BZR.lib` (member `Lua5.1-BZR\Release\ltable.obj`); `lib/Lua5.1-BZR-debug.lib`; `bzfile.vcxproj:75,104` | bzfile.dll statically links its own Lua 5.1.5 core (imports only KERNEL32/ADVAPI32/VERSION; no Lua import) and runs it on the game's `lua_State`. It inherits EXU's cross-copy fix: `ltable.obj` holds 7 immediate references to `0x0086EEF0` (the exe's `dummynode_`) in both the Release and Debug libs. bzfile has no build/exe qualification of any kind, the lib has no source, recipe or note in this repo, and README/AGENTS never mention the two-core model. | On an exe whose `dummynode_` moved, `luaH_resize` would free the host's static node (the heap-corruption class in EXU G-1) silently, as soon as a bzfile-created table grows. Nobody maintaining bzfile would know to look. | Document the lib (source = ExtraUtilities `Lua5.1-BZR`, `/MT`, `dummynode` = 0x86EEF0 for BZR 2.2.301) with the rebuild recipe; add a tripwire (extract `ltable.obj`, assert the constant); follow EXU G-1's fail-closed gate in `luaopen_bzfile` (worksheet A). | `llvm-objdump -d` of extracted `ltable.obj` (7 hits Release, 7 Debug; Debug also has the unused `_dummynode_` static); `llvm-readobj --coff-imports` on bzfile.dll; GOG exe contains `Lua 5.1.5`. The address itself was verified on the GOG exe by EXU G-1; the Steam exe is SteamStub-encrypted on disk (not verifiable offline). |
| C-3 | [Med/High] | `include/bzfile_version.h:6-12`; `.github/workflows/build.yml:54-64`; `tests/linux/run.sh:74-79` | Four independent spellings of one version: `MAJOR/MINOR/PATCH/BUILD` (referenced nowhere), `TUPLE 1,0,0,0` (FILEVERSION/PRODUCTVERSION), `STRING "1.0.0"` (tag check, `BZFILE_GetVersion`), `FILE_STRING "1.0.0.0\0"` (string table). CI compares against `VersionInfo.FileVersion`/`ProductVersion`, which PowerShell reads from the string table, not the fixed `FILEVERSION`. Nothing checks `TUPLE`, yet `bzfile.GetFileVersion` (what Lua updaters use) returns the fixed tuple. | A release bumped to 1.1.0 with `TUPLE` forgotten passes every gate and reports 1.0.0.0 to Lua version checks; the header comment "cannot drift independently" is not enforced. | Derive `TUPLE`, `STRING`, `FILE_STRING` from the numeric macros via stringizing macros; in build.yml also compare `FileVersionRaw`/`ProductVersionRaw` with `[version]"$sourceVersion.0"`. | Read header, both `.rc`, build.yml; `(Get-Item Release\bzfile.dll).VersionInfo` exposes `FileVersion` (string) and `FileVersionRaw` (tuple) separately; `git grep BZFILE_VERSION_MAJOR` -> header only. |
| C-4 | [Low/High] | `scripts/install_linux.sh:74-80,135-159,233-239` | (a) The release zip carries `SHA256SUMS.txt` (build.yml:109-110) and the release publishes it separately, but the installer never checks it. TLS + `unzip` CRC already give integrity, and sums from the same release cannot give authenticity, so this is consistency with OpenShim/EXU rather than a hole. (b) Asset selection falls back to `bzfile-[^/]*\.zip` and then any `\.zip`: the stale release "Latest Build" (tag `latest` -> ca3fd70, 2026-06-24, asset `bzfile-release.zip`) predates the sandbox hardening of 4e6f864 (2026-08-06) and would be accepted by fallback 3 if `/releases/latest` ever resolved to it. (c) When piped from curl, `steam_game_paths.sh` is fetched separately from `main` and `source`d unhashed; the two fetches can straddle a push. (d) An unauthenticated GitHub API failure (60 req/h) is reported as "could not download a matched bzfile release zip". | Low probability, but this is the path that writes executables into player installs. | `sha256sum -c SHA256SUMS.txt` in the extract dir; drop fallbacks 2-3 (fail closed); delete the `latest` tag/release (README:224-226 already disowns it); inline `steam_game_paths.sh` or default `--ref` to the release tag; print `VERSION.txt` after install. | Read script; `gh release list`, `gh api .../releases/latest`, `gh release view latest`; `git log` for ca3fd70 vs 4e6f864. |
| C-5 | [Low/High] | `scripts/steam_game_paths.sh:128-131`; `install_linux.sh:241-243`; `deploy_linux_proton.sh:45-47` | `BZR_GAME_PATH` / `--game-path` / positional `GAME_DIR` replace the result list without the exe-presence check, `-d` or canonicalisation, so the pair is copied into any directory (typo, `$HOME`, ...). A foreign `bzfile.dll` is still refused. | Litter in the wrong place; no signal that the path was wrong. | Route the override through `_bzr_add_game_path` and fail with a clear message when it is rejected. | Scratchpad: `--game-path <empty dir>` -> rc 0, both files written. |
| C-6 | [Low/Med] | `install_linux.sh:161-164`; `README.md:8-30`; `src/LuaExport.cpp:1116-1121` | The installer writes the pair into the **game root**, but the shipping consumer (CR) delivers bzfile through Workshop item 3686673790 and CR's RequireFix searches the mod root first; `StageOpenShimUpdate`/`SuiteUpdate` refuse unless the loaded DLL's directory is named `3686673790`. For Steam-on-Proton users the Workshop already delivers the same Win32 files; the root copy is reached only through the stock `.\?.dll` cpath, where it becomes a stale shadow for any mod that requires `bzfile` without RequireFix, and it cannot be upgraded (C-1). README does not say who the installer is for. | Two bzfile versions in one install, the root one silently preferred by non-RequireFix consumers. | Say in README that CR/Workshop users need nothing; position the installer for standalone mod authors and GOG (C-8). | Read CR `Scripts/RequireFix.lua:230-296,405-437`; listed Workshop 3686673790 and both game roots. Runtime cpath order of the stock game not verified. |
| C-7 | [Low/Med] | `install_linux.sh:175-186`; `deploy_linux_proton.sh:100-111` | Deploy is `cp -f` in place (truncate and rewrite the same inode), DLL first, helper second, no rollback: a failure between the two leaves a mismatched pair; a refusal in the second of several installs exits after the first was changed; each run adds `*.bak-<stamp>` files to the game root forever; no uninstall. | Rewriting a DLL that a running Proton game has mapped is undefined; a partial pair breaks `ReplaceFileOnExit`. | Copy to `dest.tmp.$$` then `mv -f` (atomic rename, new inode); stage both, then rename both; keep only the newest backup; add `--uninstall`. | Read scripts; not exercised against a running game. |
| C-8 | [Low/High] | `scripts/steam_game_paths.sh:107-115`; `README.md:4-6`; `install_linux.sh:249-255` | README promises "Linux/GOG through a compatible Wine/Proton prefix" and the platform policy requires validating GOG separately, but discovery covers only Steam roots (no Heroic/Lutris/Bottles/`~/.wine` GOG layouts), the no-install error only suggests the Steam flavour commands, and `--game-path` (the only GOG route) is not in README. | Linux/GOG users get "no Battlezone 98 Redux install found for this Steam flavour" with no next step. | Document `--game-path` in README and in the error text; optionally scan common Heroic/Lutris prefixes. | Read scripts, README, `Docs/BZR_PLATFORM_COMPATIBILITY.md:14-17,54,65`. |
| C-9 | [Low/High] | `.gitattributes:1-66` | No `*.sh text eol=lf` pin (EXU has one): on this Windows host all four scripts are `i/lf w/crlf`. The deploy header and README recommend "build on Windows, then deploy from Linux"; running the scripts from that checkout through WSL (`/mnt/c/...`) would fail on `set -euo pipefail\r`. `include/** linguist-vendored` also marks first-party `include/bzfile_version.h` vendored while `lib/**` is unmarked; no rules for `*.ps1`/`*.bat`/`*.lib`. | Documented cross-host workflow breaks for WSL users; Linguist stats wrong. | Add `*.sh text eol=lf`, `*.lib binary`; narrow vendored to `include/lua*.h` and add `lib/**`. | `git ls-files --eol`. git-bash tolerates CRLF (run.sh ran from a CRLF copy), so the Linux-bash failure is inferred, not reproduced. |
| C-10 | [Low/High] | `lib/Lua5.1-BZR.lib.bak-20260314`; `.gitignore` | A 2.2 MB backup of the old Release lib is tracked (added in e622a60 with its replacement). Its objects are `/GL` LTCG IL (llvm-objdump: "not recognized as a valid object file"; no readable `.drectve`), linkable only by the exact MSVC that produced them, which is presumably why it was replaced by the plain-COFF `/MT` build. Nothing references it; `.gitignore` has no `*.bak*` rule; the empty `lib-temp/` is the same day's leftover. The provenance of both live libs is recorded nowhere. | Permanent clone weight and a "which lib is real?" question. | `git rm` the `.bak`; add `*.bak*` and `lib-temp/` to `.gitignore`; add provenance and rebuild command (see C-2). | `llvm-ar t` member paths; `.drectve` DEFAULTLIB per lib; `git show --stat e622a60 b207ba5`; `git grep` (no references). |
| C-11 | [Low/High] | `tests/linux/run.sh:74-222` | The lane checks script syntax, the header string, discovery (override, empty HOME, symlink dedupe, external libraries per flavour) and one `--dll` deploy, but uses stub binaries (so C-1 passes), never re-installs over an existing pair, never exercises `deploy_linux_proton.sh` beyond `--help`, the asset-name regex, `--game-path` validation (C-5) or tuple consistency (C-3). No lane runs shellcheck although the scripts carry `# shellcheck source=` directives. Under Windows git-bash `test_symlink_aliases_dedupe` cannot pass (MSYS `ln -s` copies directories), so README:32's `bash tests/linux/run.sh` is Linux-only in practice. | The lane would not catch the defect classes the scripts actually have. | Add: install twice over real marker bytes; asset regex test against a canned `releases/latest` JSON with `bzfile-v1.0.0.zip`; `--game-path` on a non-game dir must fail; tuple/string parity; `shellcheck -x` in linux.yml; skip the symlink test with a message when `ln -s` produces a copy. | Ran it (Commands run). |
| C-12 | [Low/Med] | `.github/workflows/build.yml:3-9,17,20,44,72,84-85,92,113`; `linux.yml:3-11` | Actions pinned by major tag (including third-party `softprops/action-gh-release@v2` in the `contents: write` job); no `timeout-minutes` or `concurrency` in build.yml; `windows-latest` with a floating v143 minor toolset; PDBs are built but discarded, so release crashes cannot be symbolised; Debug never built; linux.yml has no `tags:` trigger and `release` `needs: build` only (installer scripts are served from `main`, not the tag, so the practical impact is small); CI never loads `bzfile.dll` into a Lua 5.1 host, although the platform policy asks for Lua 5.1 checks. | Supply-chain and runaway-runner exposure on the release job; no symbols; `luaopen_bzfile` never exercised. | Pin by SHA; `timeout-minutes: 30`; a concurrency group per ref; keep PDBs as a CI artifact or separate asset; add `tags: ['v*']` to linux.yml and gate release on a host job; optional tiny Win32 Lua 5.1 host that `require`s bzfile and calls `GetFileHash`. | Read both workflows. |
| C-13 | [Low/High] | `scripts/deploy_linux_proton.sh:1-124` vs `install_linux.sh:115-123,161-187,268-271` | `deploy_linux_proton.sh` duplicates `is_bzfile_dll`, `is_bzfile_helper` and the deploy loop verbatim, and `install_linux.sh` already prefers `../Release/bzfile.dll` + helper when run from a checkout. `APPID` (line 15) is used only in an error message; the positional `DLL_PATH` is never checked with `is_bzfile_dll`. | Two copies of a player-facing write path (C-1/C-5/C-7 each need fixing twice). OpenShim's audit removed its equivalent. | Replace with a 3-line wrapper that execs `install_linux.sh --game-path/--dll`, or delete it and update README:26-30 and AGENTS:23. | Compared the functions by reading. |
| C-14 | [Low/High] | `AGENTS.md:8`; `README.md:44-46,140,145,183-211,224-226`; README API list vs `src/LuaExport.cpp:1566-1581` | Doc drift. AGENTS.md:8 (and CR's own `AGENTS.md:7`) name `%USERPROFILE%\Documents\Google Drive\...\Open Patch - CampaignReimagined` as CR's only editable source and call the `Documents\GIT` copy stale; that Google Drive path does not exist on this machine, while `Documents\GIT\Campaign-Reimagined` exists with origin `Battlezone98Redux_CampaignReimagined` and is what this audit's brief names as canonical. README: `GetWorkingDirectory` "(..\common\Battlezone98Redux\)" is actually the exe directory (`Battlezone 98 Redux`, with spaces; GOG has no `common`); `GetWorkshopDirectory` "(..\content\301650)" is `steamapps\workshop\content\301650` and a synthetic path on GOG (note for A); `Delete`, `ListDirectory`, `SetAllowWinmmOverwrite`, `GetAllowWinmmOverwrite` are exported but undocumented; `StageOpenShim*` are documented without their Workshop-item-3686673790 restriction; "bundled Lua library snapshot has been refreshed" is unverifiable (C-2); README says `latest` is no longer a distribution while the `latest` tag and release still exist (C-4); there is no Windows install section. | Agents following AGENTS.md:8 will refuse the real CR checkout; mission authors cannot discover 4 of 15 functions. | Fix AGENTS.md:8 (and CR's) to the verified path; update the README API list and path wording; delete the `latest` release/tag. | `ls` of the Google Drive path (absent); `git -C Campaign-Reimagined remote get-url origin`; read `luaopen_bzfile`. |
| C-15 | [Low/High] | `bzfile.vcxproj:17,74,103`; `bzfile_replace_helper.vcxproj:73,99` | `RootNamespace` is `DeathmatchPlus` (copy-paste origin); `LargeAddressAware` on a DLL (ignored by the loader); the default `%(AdditionalDependencies)` list pulls user32/gdi32/winspool/comdlg32/ole32/oleaut32/uuid/odbc32/odbccp32 (dropped by `/OPT:REF`, noise); no `/PDBALTPATH:%_PDB%`, so local builds embed `C:\Users\iestu\Documents\GIT\bzfile\Release\bzfile.pdb`, and local builds are what ships: the Workshop 3686673790 DLL and CR `Bin/bzfile.dll` both carry that path. No `/guard:cf` (acceptable, section 4). | Username leak in a published binary; misleading project metadata. | `RootNamespace bzfile`; drop LAA on the DLL; `/PDBALTPATH:%_PDB%` in both Release Link groups; ship the CI artifact instead of a local build (note for CR). | tlogs; `llvm-readobj --coff-debug-directory` on local Release and Workshop DLLs; sha256 of CR `Bin/bzfile.dll` = local `Release/bzfile.dll` (bbd983ba...), Workshop DLL differs (9e7c3b64...), both FileVersion 1.0.0.0. |

### Quoted lines for the Medium items

C-1 `scripts/install_linux.sh:120-123` (same in `deploy_linux_proton.sh:79-82`), `src/bzfile_replace_helper.cpp:567`, fixture `tests/linux/run.sh:203`
```bash
is_bzfile_helper() {
    local path="$1"
    [[ -f "$path" ]] && grep -a -q "bzfile replace helper" "$path"
}
```
```cpp
	AppendLogLine(logPath, L"bzfile replace helper started.");
```
```bash
    printf 'bzfile replace helper stub\n' >"$artifacts/bzfile_replace_helper.exe"
```
Scratchpad reproduction, second run over the first install:
```
error: refusing to overwrite unexpected bzfile_replace_helper.exe in .../scratchpad/sim/game dir
rc=1
```

C-2 `bzfile.vcxproj:104` and the disassembly result
```xml
      <AdditionalDependencies>Lua5.1-BZR.lib;Version.lib;%(AdditionalDependencies)</AdditionalDependencies>
```
```
llvm-objdump -d ltable.obj | grep -ci 86eef0   ->  7   (bzfile Release lib, Debug lib and EXU's lib alike)
bzfile.dll imports: VERSION.dll, KERNEL32.dll, ADVAPI32.dll   (no Lua import: static core)
```

C-3 `include/bzfile_version.h:6-12` and `.github/workflows/build.yml:58-60`
```c
#define BZFILE_VERSION_MAJOR 1
...
#define BZFILE_VERSION_TUPLE 1,0,0,0
#define BZFILE_VERSION_STRING "1.0.0"
#define BZFILE_VERSION_FILE_STRING "1.0.0.0\0"
```
```powershell
            $versionInfo = (Get-Item -LiteralPath $path).VersionInfo
            if ($versionInfo.FileVersion -ne $expectedFileVersion) {
```

## 3. CI lane matrix

| Check | PR -> main | push main | push `agent/**` | tag `v*` (release) | linux.yml | Notes |
|---|---|---|---|---|---|---|
| MSVC Release x86 build, `/W4 /WX /sdl` | yes | yes | no (PR covers) | yes | - | `windows-latest`, toolset floating |
| Debug x86 build | no | no | no | no | - | never built (C-12) |
| Tag == `v` + `BZFILE_VERSION_STRING` | step runs; compares only on tags | same | - | yes (build.yml:36-41), again in release (108) | - | good |
| PE string FileVersion/ProductVersion == header | yes | yes | - | yes | - | good |
| PE numeric FILEVERSION tuple | no | no | - | no | - | missing (C-3) |
| `tests/Test-ReplaceHelper.ps1` (single + suite transactions) | yes | yes | - | yes | - | runs every PR |
| Lua 5.1 host loads `luaopen_bzfile` | no | no | no | no | no | missing (C-12) |
| Upload artifact (dll, exe, VERSION.txt; 14 d) | yes | yes | - | yes | - | PDBs not kept |
| Release zip + SHA256SUMS.txt (inner files) | - | - | - | yes, `needs: build` | - | installer does not verify (C-4) |
| `tests/linux/run.sh` (bash -n, discovery, installer) | via linux.yml | via linux.yml | via linux.yml | **no** | PR/main/`agent/**` | not in release `needs` (C-12) |
| shellcheck | no | no | no | no | no | missing |
| Installer vs real release asset / real helper bytes | no | no | no | no | no | missing (C-1, C-11) |
| Shared-doc hash parity | no | no | no | no | no | missing (section 6) |
| Lua lib provenance / `dummynode` tripwire | no | no | no | no | no | missing (C-2) |
| Timeouts / concurrency | none in build.yml | | | none | 10 min + concurrency | C-12 |

## 4. Hardening flags table

Release columns come from the actual command lines in the local tlogs (built 2026-09-19 from this tree) and the built PE; Debug columns from the vcxproj only (not built, per ground rules).

| Flag | bzfile Release | bzfile Debug | helper Release | helper Debug |
|---|---|---|---|---|
| Platforms / configs | Win32 only; sln maps `x86`->`Win32` | Win32 | Win32 | Win32 |
| `/W4` | present | present | present | present |
| `/sdl` | present | present | present | present |
| `/WX` | present | present | present | present |
| `/permissive-`, `/std:c++20`, `/EHsc`, `/GS` | present | present (vcxproj) | present | present (vcxproj) |
| External headers | `/external:anglebrackets /external:W0` | same | same | same |
| `/DYNAMICBASE` | default, on (PE 0x40) | default | default, on | default |
| `/NXCOMPAT` | default, on (PE 0x100) | default | default, on | default |
| `/SAFESEH` | on (111 handlers) | default (unverified) | on (66 handlers) | default (unverified) |
| `/guard:cf` | absent (no `GUARD_CF` characteristic) | absent | absent | absent |
| `/Qspectre` | absent | absent | absent | absent |
| CRT | `/MT` static: imports only KERNEL32, ADVAPI32, VERSION; no VC++ redist needed (game ships VC2013 only) | `/MTd` (matches LIBCMTD debug lib) | `/MT`: KERNEL32, ADVAPI32, SHELL32 | `/MTd` |
| Delay-loads | none | none | none | none |
| `luaopen_bzfile` export | `extern "C" __declspec(dllexport)` (LuaExport.cpp:1564), undecorated name confirmed in the export table with `BZFILE_GetVersion`; no `.def` | same | n/a | n/a |
| Lua lib | `Lua5.1-BZR.lib` (`/MT`, non-LTCG) | `Lua5.1-BZR-debug.lib` (`/MTd`) | none | none |
| `WholeProgramOptimization` | true: `/GL` + `/LTCG:incremental` | n/a | true | n/a |
| `LinkIncremental` | not set (LTCG) | not set (default on) | not set | not set |
| `LargeAddressAware` | set (meaningless on a DLL) | set | set (meaningful) | set |
| PDB path | absolute maintainer path embedded (C-15) | same | same | same |
| Include dirs | `$(ProjectDir)\include` only; no stale dirs | same | none needed (`.rc` uses `../include/`) | same |
| IntDir / OutDir | `bzfile\Release\` / `Release\` | `bzfile\Debug\` / `Debug\` | `bzfile_r.F743A764\Release\` / `Release\` | analogous |

`/guard:cf` is not needed for correctness (bzfile writes no code), but it is free for a static-CRT DLL/exe and, unlike OpenShim/EXU, there is no trampoline argument against it. As recommended for OpenShim and EXU, pin `RandomizedBaseAddress`, `DataExecutionPrevention` and `ImageHasSafeExceptionHandlers` explicitly so a toolset change cannot drop them.

## 5. Vendored Lua drift

Headers (LF-normalised sha256, first 12 hex):

| Header | bzfile `include/` | EXU `include/` | EXU `Lua5.1-BZR/src/` | Result |
|---|---|---|---|---|
| `lua.h` | 470551c185f0 | 470551c185f0 | 470551c185f0 | identical |
| `lauxlib.h` | c741f9c1587f | c741f9c1587f | c741f9c1587f | identical |
| `lualib.h` | 13f880e9dd99 | 13f880e9dd99 | 13f880e9dd99 | identical |
| `luaconf.h` | dcb19a58e515 | dcb19a58e515 | 0410ff22f66c | identical to EXU `include/`; vs the Lua source copy differs only in `LUA_CPATH_DEFAULT` (LuaBinaries-style `clibs\`/`?51.dll` entries, lines 94-96/106-107). `LUA_NUMBER double`, `LUAI_MAXCSTACK`, `LUA_MINSTACK`, `LUA_API`/`LUALIB_API` identical. `LUA_CPATH_DEFAULT` is only consumed by `loadlib.c` inside the lib, which bzfile never registers: harmless |
| `lua.hpp` | fd83f7e823cf | fd83f7e823cf | (none) | identical |

Libraries (members extracted with `llvm-ar`):

| Lib | Members | CRT (`.drectve`) | Code | `dummynode` 0x86EEF0 refs in `ltable.obj` | vs EXU `lib/Lua5.1-BZR.lib` (built 2026-09-27 from `Lua5.1-BZR`, `/MD`) |
|---|---|---|---|---|---|
| `lib/Lua5.1-BZR.lib` (Release) | 30 (`lapi`..`lzio`, `print`), paths `Lua5.1-BZR\Release\` | LIBCMT (`/MT`) | plain COFF | 7 | same member set; same 993 external defined symbols; 17/30 objects have identical disassembly + relocations; the other 13 (`lauxlib`, `lbaselib`, `ldblib`, `ldo`, `liolib`, `llex`, `lmathlib`, `loadlib`, `lobject`, `loslib`, `lstrlib`, `lvm`, `print`) differ at CRT call sites, consistent with `/MT` vs `/MD` (spot-checked 3; not proven line by line) |
| `lib/Lua5.1-BZR-debug.lib` (Debug only) | 30, paths `Debug\` | LIBCMTD (`/MTd`), JustMyCode, RTC | plain COFF | 7 (+ unused `_dummynode_` static) | debug build of the same sources |
| `lib/Lua5.1-BZR.lib.bak-20260314` (unreferenced) | 30, paths `Release\` | none readable | `/GL` IL, toolset-locked | n/a | superseded (C-10) |

Conclusion: the tracked Release lib is ABI-equivalent to what EXU's `Lua5.1-BZR` produces, built with bzfile's `/MT` CRT, and carries the `dummynode` patch; it cannot be reproduced from anything in this repo (C-2, C-10). The game's own Lua is 5.1.5 (string in the GOG exe), matching `LUA_RELEASE`.

## 6. Shared-doc hash table

Working-tree sha256 (CRLF checkouts on this host) and `origin/main` blob ids.

| Repo (path) | `BZR_LUA_AGENT_REFERENCE.md` sha256 | blob @ origin/main | `BZR_PLATFORM_COMPATIBILITY.md` sha256 | blob @ origin/main | Identical |
|---|---|---|---|---|---|
| bzfile (`Docs/`) | 15732942b6771ead... | 62ddcefe6f8c | 88cedde9324b52ad... | b8ca219f31d9 | yes |
| BZR-OpenShim (`docs/` in working tree, `Docs/` in the origin/main tree) | 15732942b6771ead... | 62ddcefe6f8c | 88cedde9324b52ad... | b8ca219f31d9 | yes |
| ExtraUtilities (`Docs/`) | 15732942b6771ead... | 62ddcefe6f8c | 88cedde9324b52ad... | b8ca219f31d9 | yes |
| Campaign-Reimagined (`Documents\GIT\Campaign-Reimagined\Docs/`) | 15732942b6771ead... | 62ddcefe6f8c | 88cedde9324b52ad... | b8ca219f31d9 | yes |

All eight copies are byte-identical in the working trees and in `origin/main`. A stray extra copy exists under `BZR-OpenShim/BZR-OpenShim-cmpB/Docs/` (same hashes). The Google Drive CR path named by AGENTS.md:8 does not exist, so it could not be hashed (C-14). Nothing enforces parity in any repo's CI.

## 7. Version plumbing table

| Source | Value | Enforced by |
|---|---|---|
| `include/bzfile_version.h` STRING | `1.0.0` | build.yml regex + tag check; run.sh format check |
| `include/bzfile_version.h` TUPLE | `1,0,0,0` | nothing (C-3) |
| `include/bzfile_version.h` FILE_STRING | `1.0.0.0` | build.yml (PE string table) |
| `MAJOR/MINOR/PATCH/BUILD` macros | 1/0/0/0 | unused anywhere |
| `src/bzfile_version.rc`, `src/bzfile_replace_helper_version.rc` | macros from the header (single source) | compile |
| Native export `BZFILE_GetVersion` (version.cpp) | `1.0.0` | none; there is no Lua-side version API (Lua can only call `GetFileVersion` on the DLL path, i.e. the tuple) |
| README | no version stated | - |
| Git tags | `v1.0.0` (f585c30, 2026-08-31); `latest` (ca3fd70, 2026-06-24, stale) | release job runs on `v*` only |
| `git describe` | `v1.0.0-10-g804ebae`; 6 non-merge commits since, none touch `src/`, `include/`, `lib/` or the projects | - |
| GitHub releases | Latest = v1.0.0 (`bzfile-v1.0.0.zip`, `SHA256SUMS.txt`); "Latest Build" on `latest` (`bzfile-release.zip`, pre-hardening) | installer reads `/releases/latest` (C-4) |
| Installer asset regex | `bzfile-v[^/]*\.zip` matches `.../download/v1.0.0/bzfile-v1.0.0.zip` exactly (build.yml:110,116) | not tested (C-11) |
| Deployed copies seen | Workshop 3686673790 `bzfile.dll` 1.0.0.0 sha 9e7c3b64...; CR `Bin/bzfile.dll` 1.0.0.0 sha bbd983ba... (= local `Release/`) | two different builds carry the same version; neither is known to be the CI artifact (release zip not downloaded) |
| Anything enforcing `v*` tag == header | yes: build.yml:36-41 and release step 108 | - |

## 8. Patterns worth keeping

- Version identity centralised in one header consumed by both `.rc` files and the native export; CI refuses a tag that disagrees with the header, re-checks it in the release job from `VERSION.txt`, and checks both PE string versions.
- Release publishes the exact binaries the build job tested (artifact hand-off, `if-no-files-found: error`), from tags only; top-level `permissions: contents: read`, write only on the release job.
- Helper transactions (single-file, hash mismatch, missing stage, three-file suite) run on every PR against the real binary.
- Static CRT for both binaries: only KERNEL32/ADVAPI32/VERSION/SHELL32 are imported, so no VC++ redist dependency under Windows or Proton (contrast EXU G-21).
- Discovery design: flavour comes from the discovering Steam root, canonical-path dedupe of `~/.steam/*` aliases, zero-length array when nothing is found, each with a regression test (including the "deploy to /bzfile.dll" hazard).
- Installers fail closed on a foreign `bzfile.dll`, validate `--ref`, use `mktemp -d` + `trap` cleanup and quote every path (a game dir with spaces is exercised by run.sh). The OpenShim F13 SIGPIPE shape is absent: `is_bzfile_*` grep files directly, run.sh uses here-strings, and the one pipeline `... | grep -E | head -n1` in `latest_asset_url` runs only in `if`/`elif` context (so `set -e`/`pipefail` cannot abort there) and emits a handful of lines in one write.
- `/W4 /WX /sdl /permissive-` in all four configurations, with external headers silenced rather than warnings disabled.
- `.rc` files are ASCII (diffable), unlike EXU's UTF-16 resource.

## 9. Low-severity items

- `.jules/sentinel.md:1` is dated `2025-01-24`; the file and `IsWriteProtected` both arrived in e7807df on 2026-06-23 (google-labs-jules bot), reworked in 4e6f864 (2026-08-06). Fix the date.
- `LICENSE` is MIT "Copyright (c) 2025 VTrider"; no first-party file (`src/*`, scripts, tests) carries a header and the fork's own copyright is not stated. The Lua MIT notice is in `include/lua.h:365`, but the release zip ships the statically linked Lua without any notice; add it to README/release notes.
- `steam_game_paths.sh:22` also accepts `BZR.exe`; no known build uses that name. Harmless.
- `steam_game_paths.sh:80` regex `"path"[[:space:]]+"(.*)"` is greedy and does not unescape `\"`; fine for real Steam files.
- `install_linux.sh:185-186` and `deploy:110-111` use GNU `stat -c`; fine on Linux.
- `install_linux.sh:268-271`: run from a checkout, a possibly stale local `Release/` pair silently wins over the release download; it prints the path but not the version.
- `bzfile.sln` working tree has mixed EOL (index LF); `branding/repo_icon.svg` has no trailing newline and uses the game's name as wordmark text.
- `.gitignore` is a 457-line generator dump of which ~15 rules matter. `*.lib` is correctly not ignored (so `lib/*.lib` stays tracked); `*.dll`/`*.exe` are ignored, which is right for build output.
- `bzfile_replace_helper.vcxproj` gets a default `/IMPLIB` for an exe with no exports (no file produced); harmless.
- README:32 "Host-side Linux checks are `bash tests/linux/run.sh`" should say Linux (or WSL) only (C-11).
- For provenance, CR should package the CI `bzfile-v*.zip` contents and record their sha256 in `shipping.lock.json` rather than a local build (C-15).

## 10. Notes for other worksheets

- **A (Lua bindings / path policy):** (1) Two-core Lua model (C-2): bzfile's static Lua 5.1.5 runs on the game's `lua_State`; only `dummynode` is reconciled. Check every comparison against bzfile's own statics and whether a fail-closed gate is feasible. (2) `ResolveWorkshopDirectoryPathOnce` (`LuaExport.cpp:136-145`): on GOG `FindSteamAppsDirectory` finds nothing and the fallback is `<exe dir>\..\..\workshop\content\301650`, i.e. `C:\Program Files (x86)\GOG Galaxy\workshop\content\301650` on this machine, which then becomes an allowed write root; the platform policy asks for equivalent behaviour across stores. (3) The ASCII `bzfile Error:` literals are the installers' identity probe for the DLL; keep at least one and pin it by test. (4) `luaopen_bzfile` returns 0 and relies on `luaL_register` having set `package.loaded.bzfile`; fine in 5.1, worth a comment. (5) README omits `Delete`, `ListDirectory`, `Set/GetAllowWinmmOverwrite` and the 3686673790 restriction (C-14).
- **B (helper / transactions / tests):** (1) Give the helper an ASCII identity marker that survives `/OPT:REF`, or confirm which bytes C should match (C-1). (2) `StageOpenShim*` require the loaded DLL's directory to be named `3686673790`; the Linux installer's game-root copy can never use them (correct, but undocumented). (3) `Test-ReplaceHelper.ps1` is Windows-only; nothing exercises the helper under Wine (process wait, `MoveFileEx` semantics, hidden launch), which the platform policy lists as a Proton lane.
- **CR (cross-repo, not a worksheet here):** CR `Bin/bzfile.dll` is a local build (maintainer PDB path) and differs from the Workshop copy while both report 1.0.0.0; CR's `AGENTS.md:7` repeats the nonexistent Google Drive canonical path (C-14).

Could not verify: Debug configurations (not built); byte-level equality of the 13 CRT-differing Lua objects beyond spot checks; the Steam exe's `dummynode` address (SteamStub); real Linux bash behaviour on CRLF scripts (git-bash tolerates it); the v1.0.0 release zip contents (not downloaded; the v1.0.0 source has the same UTF-16-only helper marker, so C-1 applies); the stock game's runtime cpath order (C-6); shellcheck results (not installed).
