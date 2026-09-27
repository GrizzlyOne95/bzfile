# bzfile repository code audit (2026-09-27)

This is a repository-wide review of `bzfile.dll`, `bzfile_replace_helper.exe`, the Linux installers, CI and repository hygiene. It covers safety, correctness, performance, efficiency and unused code, taken at `origin/main` 804ebae. This document is the consolidated, prioritized view. The per-area reviewer worksheets are in `Docs/audit_20260927/`, with code quotes, section maps and reproduction notes:

| Worksheet | Scope | Findings |
|-----------|-------|----------|
| A | `src/LuaExport.cpp`, path policy, write protection, staging, version plumbing | 2 High, 10 Med, 10 Low |
| B | `src/bzfile_replace_helper.cpp`, update transaction, `tests/Test-ReplaceHelper.ps1`, OpenShim/CR contract | 1 High, 7 Med, 8 Low |
| C | `scripts/`, CI, both `.vcxproj`, vendored Lua, `.gitignore`/`.gitattributes`, docs | 0 High, 3 Med, 12 Low |

The audit itself changed no source. Fixes landed afterwards on 2026-09-27 in GrizzlyOne95/bzfile#14, #15, #16 and #18 to #22, with Campaign Reimagined follow-ups in GrizzlyOne95/Battlezone98Redux_CampaignReimagined#109; each backlog row says what was resolved and what is still open.

## 1. Scope and method

- Every tracked source, script, workflow and project file was read end to end. That is about 3,500 lines of first-party code and scripts, plus the vendored Lua headers and the members of the prebuilt `lib/Lua5.1-BZR*.lib`.
- Consumers were read to judge real exposure:
  - Campaign Reimagined `Scripts/OpenShimInstaller.lua`, `AutoSave.lua`, `CareerStats.lua`, `LogPaths.lua`, `PersistentConfig.lua` and `RequireFix.lua`. These came from CR's canonical `Documents\GIT\Campaign-Reimagined` checkout.
  - OpenShim `src/patches/openshim_updater.cpp`.
- Reproductions ran against scratch files only, never a game install:
  - The prebuilt `Release\bzfile_replace_helper.exe` was run twice (worksheet B, exp1 and exp2):
    - through a three-file suite with the second destination locked;
    - with two concurrent helpers sharing one status file.
  - `install_linux.sh` was run twice into a scratch game dir with the real, locally built binaries (worksheet C).
  - `File::Dump` was re-implemented line for line and built with the MSVC x86 STL (this document, P0-3).
  - NTFS path spellings (`::$DATA`, trailing dot or space, 8.3 names, `CON`) were run through `weakly_canonical`/`realpath` on this host. Correction found while fixing P0-1: for a file that does not exist yet, `weakly_canonical` keeps the `::$DATA` suffix, so the old DLL did let a script create `winmm.dll` as `winmm.dll::$DATA`. The Lua host test now covers it.
- `bash tests/linux/run.sh` on the Windows git-bash host: every check passes except `test_symlink_aliases_dedupe`. That one fails only because git-bash `ln -s` copies directories instead of linking them.
- The two shared docs are byte-identical across bzfile, BZR-OpenShim, ExtraUtilities and Campaign-Reimagined, in the working trees and on `origin/main`:
  - `BZR_LUA_AGENT_REFERENCE.md` (sha256 `15732942…`);
  - `BZR_PLATFORM_COMPATIBILITY.md` (`88cedde9…`).

## 2. Executive summary

bzfile is small, and most of it is careful:
- Lua argument handling was already reworked so that path rejections return `nil, msg` instead of long-jumping past C++ destructors.
- The sandbox root is pinned to the exe directory rather than the working directory.
- The `StageOpenShim*` paths:
  - have fixed destinations;
  - hash payloads before the wait and re-hash after install;
  - back up all three suite files before the first rename.
- The helper's argv handling admits no injection.
- CI builds the helper and runs its transaction test on every PR.

The risks cluster in four places:

1. **The write-protection model is weaker than `.jules/sentinel.md` says** (P0-1).
   - `IsWriteProtected` is centralized, but it protects only two leaf names, `winmm.dll` and `bzfile.dll`.
   - Any mission script can switch it off for the whole process with the undocumented `bzfile.SetAllowWinmmOverwrite(true)`.
   - `MakeDirectory` never calls it.
   - `ReplaceFileOnExit` gives scripts a generic "replace any file in the roots at exit" primitive with no hash, no backup and no mutex. Nothing in CR or OpenShim uses it.
   - Together these bypass every check that `StageOpenShim*` makes.
2. **A single script call can destroy a file or crash the game** (P0-2, P0-3, P0-4).
   - `CopyFile(src, dst, true)` deletes the destination before it knows the copy can succeed.
   - `Read(huge)`, and `Dump` of a large file, throw `std::bad_alloc` through the C-built Lua core into the engine.
   - `Dump` on a text-mode handle returns the file padded with NULs. CR's AutoSave backups hit this on every autosave.
3. **The update transaction can get stuck or report the wrong outcome** (P0-5 to P0-8).
   - A 10-minute cap on waiting for the game to exit abandons the update for anyone who plays longer.
   - The status file is written as `staged` before the helper launches and is never reset if the launch fails. CR never re-stages from a pending state.
   - A second staging while a helper is active corrupts the shared status.
   - Suite rollback reports "incomplete" when it was actually correct.
4. **Distribution** (P0-9, P1-1).
   - The Linux installer cannot upgrade its own install. Its "is this our helper" probe greps for an ASCII string that the real helper holds only as UTF-16. Reproduced: the first run returns 0, the second returns 1.
   - bzfile statically links a prebuilt Lua core that hard-codes the game's `dummynode` address `0x86EEF0`, with no build gate. This is the same hazard as EXU G-1.

## 3. Prioritized backlog

Severity and confidence are the reviewer's rating, re-checked where noted. "Ref" points to the worksheet finding(s).

### P0: safety and correctness, schedule next

| ID | Finding | Where | Ref |
|----|---------|-------|-----|
| P0-1 | **Any script can switch write protection off, and it covers too little.** (a) `SetAllowWinmmOverwrite(true)` is a process-global kill switch for `IsWriteProtected`. It covers `bzfile.dll` itself and the recursive scan in `Delete`. The function is exported, undocumented and unused by CR or OpenShim. Remove it, or make it a no-op that returns false. (b) Extend the protected set to the native bundle and the update state: `bzloader.dll`, `plugins\openshim.dll`, the game exe, `bzfile_replace_helper.exe`, `net.ini`, `scripts\patches.json`, `openshim.ini`, `*.pending*`, `*.previous` and `*_update.status`. Match on the canonical path after stripping trailing dots and spaces, which otherwise get past the name check for a protected file that does not exist yet. (c) Call the check from `MakeDirectory`. (d) `ReplaceFileOnExit` replaces a destination the script chooses, with no hash, backup or mutex, which is exactly the shape AGENTS.md forbids. Remove it (nothing calls it), or restrict it to a fixed allow-list that uses the hardened helper mode. Re-verified against `LuaExport.cpp:339-353, 765-853, 1508-1518`. [High/High] **Resolved 2026-09-27** (GrizzlyOne95/bzfile#14): Rule moved to `src/bzfile_write_policy.h` (all `.dll`/`.exe`/`.asi`, `net.ini`, `patches.json`, suite payload names, `.pending`/`.previous`; names normalized as Win32 resolves them) and host-tested; `MakeDirectory` checks it; `SetAllowWinmmOverwrite` is a no-op and `ReplaceFileOnExit` is retired (both exports kept, returning `false, msg`). | `LuaExport.cpp` | A-4, A-5, A-11, B-1 |
| P0-2 | **`CopyFile(src, dst, true)` deletes the destination before trying the copy.** It checks neither that the source exists nor that source and destination differ. A missing or unreadable source, or `CopyFile(p, p, true)`, destroys `dst`. Fix: copy to `dst.tmp`, then `MoveFileExW(REPLACE_EXISTING)`, and refuse when `SamePath(src, dst)`. CR's `OpenShimInstaller.lua:890` calls this API. Re-verified: the `remove` at 743 runs before the `copy_file` at 753. [High/High] **Resolved 2026-09-27** (GrizzlyOne95/bzfile#14): Copies to `<dst>.bzfile-copy` and renames into place; refuses self-copies and missing sources. | `LuaExport.cpp:726-760` | A-2 |
| P0-3 | **`Dump` returns NUL-padded content on text-mode handles.** It sizes the buffer from `tellg()`, which counts raw bytes, but a text-mode `read` collapses CRLF and stops at `0x1A`. **Reproduced** with the MSVC x86 STL: a 3-line CRLF file returns 21 bytes, 18 of content and 3 NULs. On a write-only handle it returns 21 NULs. CR `AutoSave.lua:300-319` copies saves through `Dump` on a text handle, so every pre-autosave backup gets trailing NULs and binary saves are corrupted. Fix: resize to `gcount()` after the read, and refuse `Dump` on a handle opened without `in`. CR should open saves with `"rb"`/`"wb"`. [Med/High] **Resolved 2026-09-27** (GrizzlyOne95/bzfile#14): Trims to `gcount()`; a write-only handle returns `""`. | `LuaExport.cpp:604-626` | A-3 |
| P0-4 | **C++ exceptions escape into the game.** `Read(count)` allocates `count` bytes (up to `INT_MAX`) before reading, and `Dump` resizes to the file size; both can throw `bad_alloc`. For a file over 4 GiB, `Dump` truncates the size for the buffer but not for the read, which overflows the heap. `path::string()` throws `system_error` for any character outside the ANSI code page, so `GetWorkingDirectory()` on such an install path throws at mission start. The `ListDirectory` range-for can throw `filesystem_error`. No binding has a `try/catch`. Fix: cap `count` at the remaining file size, and cap `Dump` at a documented limit. Return UTF-8 via `u8string()`. Wrap every binding body in one `try/catch` that converts to `nil, msg`. [High/Med: the escape is certain; how the game reacts was not observed] **Resolved 2026-09-27** (GrizzlyOne95/bzfile#14): Every binding runs behind `File::Guarded`; `Read` is chunked, `Read`/`Dump` are capped at 64 MiB; paths convert without throwing. | `LuaExport.cpp:541-578, 604-626, 651, 657, 1491` | A-1, A-7, B-13 |
| P0-5 | **Updates are abandoned after 10 minutes of play.** `WaitForProcessExit` caps the wait at 10 minutes, then writes `failed`. CR and OpenShim both stage early in a session. The PID-reuse reason given for the cap does not apply once the process handle is open. Fix: wait `INFINITE` on the handle, and verify the handle's image path and creation time when opening it. [Med/High] **Resolved 2026-09-27** (GrizzlyOne95/bzfile#15): Unbounded wait, with a process-creation-time check as the PID-reuse guard. | `bzfile_replace_helper.cpp:285-310` | B-2 |
| P0-6 | **The status file can stay "pending" forever.** The DLL writes `state=staged` before `CreateProcessW` and does not reset it when the launch fails; OpenShim's updater does reset it. The helper also leaves `staged` or `waiting_for_exit` behind when it exits with code 2 (an argument-count mismatch from a helper of another version), crashes or is killed. CR's installer checks pending states first and never re-stages, so the player sees "restart required" on every launch. Fix: write `failed` when the launch fails. Have the helper write `failed` on every early exit, including exit 2. Have CR treat a pending status as stale when the update mutex does not exist. [Med/High] **Resolved 2026-09-27** (GrizzlyOne95/bzfile#15): The DLL writes `failed` on a launch failure; the helper writes a terminal status on every exit it can (bad argument count, pid 0, exceptions); new `bzfile.IsOpenShimUpdateActive()` lets CR detect a stale pending status. | `LuaExport.cpp:1182-1197, 1381-1400`; helper 403-406, 536-540 | A-8, B-3 |
| P0-7 | **A second staging while a helper is active corrupts the shared state.** **Reproduced** (worksheet B exp2): helper B exits 0 and writes `already_staged` with B's hash. When the game exits, helper A installs A's payloads and writes `complete` with A's hash, and B's staged files are orphaned. Fix: check the `Local\BZR_OpenShim_Update` mutex in the DLL before staging, as `openshim_updater.cpp:576-584` does. [Med/High] **Resolved 2026-09-27** (GrizzlyOne95/bzfile#15): Both `Stage*` refuse while the mutex exists; a second helper exits 3 without touching the status. | `LuaExport.cpp:1205-1406`; helper 424-436 | B-4 |
| P0-8 | **Suite rollback "restores" payloads that were never promoted.** **Reproduced** (worksheet B exp1): with destination 2 locked, payload 1 was correctly restored and payloads 2 and 3 were never touched. The status still said "rollback incomplete", and staged files 2 and 3 were left behind. Fix: roll back only the promoted payloads, in reverse order, and remove orphaned staged files. Related (B-9): on a fresh install, the hardened single mode can restore a stale `winmm.dll.previous` left by an earlier update. [Med/High] **Resolved 2026-09-27** (GrizzlyOne95/bzfile#15): Rolls back only promoted payloads, newest first, removes staged files on failure; single mode removes a new destination instead of restoring a stale `.previous`. | `bzfile_replace_helper.cpp:370-397, 494-510, 598-633` | B-5, B-9 |
| P0-9 | **The Linux installer cannot re-install or upgrade.** `is_bzfile_helper` greps for the ASCII text "bzfile replace helper", but the real helper holds that text only as a UTF-16 literal. **Reproduced:** the first run returns 0; the second refuses to overwrite the genuine helper and returns 1. `tests/linux/run.sh:203` uses an ASCII stub, so CI cannot see it, and v1.0.0 has the same source. Fix: add an ASCII identity marker to the helper that survives `/OPT:REF` (a named section, or a version-resource check), and test against the real binary. [Med/High] **Resolved 2026-09-27** (GrizzlyOne95/bzfile#16): Probe strips NULs before matching (checked against three real helper builds); `run.sh` stubs are UTF-16 and a re-install test was added. | `install_linux.sh:120-123,170-173`; `deploy_linux_proton.sh:79-82,94-97` | C-1 |

### P1: correctness and hardening worth scheduling

| ID | Finding | Where | Ref |
|----|---------|-------|-----|
| P1-1 | **Prebuilt Lua core with a hard-wired engine address.** `lib/Lua5.1-BZR.lib` and its `-debug` twin exist only as binaries, with no provenance in this repo. Their `ltable.obj` carries EXU's `#define dummynode (0x86EEF0)` (7 references). The core runs on the game's Lua state with no exe check. On a build where `dummynode_` has moved, `luaH_resize` frees the host's static node. The headers match EXU's, and the Release lib has the same 993 symbols as EXU's build (built `/MT` here against EXU's `/MD`). Fix: build the core from EXU's `Lua5.1-BZR` sources in CI (or vendor the sources), and document the two-core model. Make `luaopen_bzfile` fail closed on an unqualified exe, sharing EXU G-1's gate. **Partly resolved 2026-09-27** (GrizzlyOne95/bzfile#19): `luaopen_bzfile` refuses to load unless the running exe keeps an empty Lua node at 0x86EEF0 in a data section that its own code references (checked offline against the GOG and Steam 2.2.301 exes; the Lua host test covers both outcomes); `lib/README.md` records the libraries' known provenance and the stale `.bak` library is gone. **Open:** building the libraries from source in CI; one in-game launch on GOG and on Steam to confirm the in-process check. | `bzfile.vcxproj:75,104`; `lib/` | A-12, C-2 |
| P1-2 | **On non-Steam installs the Workshop root is invented, and it becomes a write root.** With no `steamapps` ancestor, the root is `<game>/../../workshop/content/301650`. For the installed GOG game that is `C:\Program Files (x86)\GOG Galaxy\workshop\content\301650`. Fix: return an empty root when no `steamapps` is found; an empty root is already handled as "no Workshop root". **Resolved 2026-09-27** (GrizzlyOne95/bzfile#14): No `steamapps` ancestor now means no Workshop root (`GetWorkshopDirectory()` returns `""`). | `LuaExport.cpp:136-145` | A-6 |
| P1-3 | **`CREATE_BREAKAWAY_FROM_JOB` has no fallback.** Inside a job without `BREAKAWAY_OK`, `CreateProcessW` fails with `ERROR_ACCESS_DENIED`, which combined with P0-6 leaves CR's installer stuck. Fix: retry without breakaway and log it. OpenShim `openshim_updater.cpp:514-517` has the same problem. [Med/Med; behaviour under GOG Galaxy, Steam and Wine job objects unverified] **Resolved 2026-09-27** (GrizzlyOne95/bzfile#15): Retries without breakaway on `ERROR_ACCESS_DENIED`. OpenShim still has the same shape. | `LuaExport.cpp:382-395` | B-6 |
| P1-4 | **The helper is launched unverified, and payload origin is whatever the calling script claims.** The helper exe is started by path with no hash or version check, and scripts can overwrite it through `Open`/`CopyFile` (see P0-1b). The expected hashes come from the calling script. The "Workshop item 3686673790" gate only checks a folder name. Staged payloads sit under predictable names in folders scripts can write, and the single-file mode does not re-hash between staging and promotion. Minimum fix: protect the helper and the staged file names (P0-1b), and pin the helper's hash into the DLL at build time. This mirrors OpenShim P0-8. **Partly resolved 2026-09-27** (GrizzlyOne95/bzfile#14, #15): the helper exe, the staged payload names and the suite payload names are write-protected from Lua, and the helper re-hashes every staged file after the wait. **Accepted:** the expected hashes still come from the calling script and the Workshop-item gate checks a folder name; the Workshop item's own content is the trust root. **Open:** pinning the helper's hash into the DLL at build time. | `LuaExport.cpp:812, 1094-1160, 1286-1343` | A-9, B-7, B-8 |
| P1-5 | **Staging only works when `bzfile.dll` is loaded from a folder named `3686673790`**, and it writes into Workshop content. GOG and local-addon layouts can never update, and the installer's game-root copy becomes a stale second bzfile (C-6). Decide which layouts are supported and document them in the README and the platform doc. **Open, needs a decision:** which layouts (Steam Workshop only, GOG, local addon) the OpenShim staging path should support. | `LuaExport.cpp:1116-1121, 1226-1231` | A-10, C-6 |
| P1-6 | **`Delete` fails open.** The protected-file scan stops at the first iteration error, and the delete then goes ahead. Fix: treat a scan error as a refusal. **Resolved 2026-09-27** (GrizzlyOne95/bzfile#14): A scan error refuses the delete. | `LuaExport.cpp:1439-1467` | A-15 |
| P1-7 | **Version plumbing.** `bzfile_version.h` spells one version four ways, and the `MAJOR`, `MINOR`, `PATCH` and `BUILD` macros are unused. CI compares only the string FileVersion, never the numeric tuple that `GetFileVersion` (and therefore CR) reads. Fix: derive `TUPLE`, `STRING` and `FILE_STRING` from the four macros, and check the tuple in CI. Separately, the stale `latest` tag and release (ca3fd70, 2026-06-24, before the sandbox hardening) still exist, and the installer's `.zip` fallback would accept them. `SHA256SUMS.txt` is shipped but never checked. **Resolved 2026-09-27** (GrizzlyOne95/bzfile#18): the Linux lane checks every spelling against MAJOR/MINOR/PATCH/BUILD, CI checks the numeric FILEVERSION, and the installer accepts only `bzfile-v*.zip` and verifies its `SHA256SUMS.txt`. **Open:** retiring the stale `latest` release (needs the owner). | `include/bzfile_version.h`; `build.yml:54-64`; `install_linux.sh` | A-22, C-3, C-4 |
| P1-8 | **Helper robustness.** Six calls use the throwing `filesystem::exists`, and `wWinMain` has no `try/catch`. A malformed pid makes `wcstoul` return 0, which means "do not wait", so the helper promotes while the game is still running. `MOVEFILE_COPY_ALLOWED` makes a cross-volume promotion non-atomic. `GetLastError` is read after `CloseHandle`. `ComputeSha256` in both binaries treats a read error as end of file. **Resolved 2026-09-27** (GrizzlyOne95/bzfile#15): Non-throwing existence checks, `try/catch` barrier in `wWinMain`, pid 0 rejected, promotion via same-directory copy plus rename, `GetLastError` captured before `CloseHandle`, hash read errors detected (helper side; the DLL copy of `ComputeSha256` is unchanged). | `bzfile_replace_helper.cpp` | B-10, B-11, B-12, B-15, A-19 |
| P1-9 | **Test coverage.** `Test-ReplaceHelper.ps1` needs `RUNNER_TEMP`, so it fails outside CI, and it takes the global update mutex. It has no rollback, locked-file, second-helper, legacy-mode or argument-count cases. The Linux lane never runs the helper under Wine. Nothing tests `LuaExport`'s path policy, write protection or argument construction, although `TryResolveAllowedPath`, `IsWriteProtected` and `QuoteCommandLineArgument` are self-contained enough for a host test. `run.sh` uses stub binaries, which is why P0-9 passes. **Mostly resolved 2026-09-27** (GrizzlyOne95/bzfile#14, #15, #16, #21, #22): `tests/lua_host` loads `bzfile.dll` into real Lua in CI and covers path policy, write protection, CopyFile, Dump/Read and the build gate; `Test-ReplaceHelper.ps1` runs outside CI and covers waiting, a second helper, rollback, bad arguments and pid 0; `run.sh` uses UTF-16 stubs and covers re-install, `--game-path`, backups and `--uninstall`. **Open:** running the helper under Wine in the Linux lane. | `tests/` | B-16, C-11 |

### P2: cleanup and maintainability

| ID | Item | Ref |
|----|------|-----|
| P2-1 | README drift. Several exports are undocumented: `Delete`, `ListDirectory` and `Set/GetAllowWinmmOverwrite`. Several documented behaviours are wrong: `Open` returns `nil, msg` and accepts `b`; `Close` does not nil the variable; `MakeDirectory` returns `true` or `false, msg`, not nil. CR's `RequireFix.lua:368-370` stub copies the wrong `MakeDirectory` contract. The `GetWorkingDirectory`/`GetWorkshopDirectory` path examples are wrong. The `3686673790` restriction is not mentioned, and the `latest` wording is out of date. **Resolved 2026-09-27** (GrizzlyOne95/bzfile#14, #15, #22). | A-21, C-14 |
| P2-2 | Scripts. `--game-path` skips the exe check (reproduced: a write into a non-game directory). The DLL and helper are copied in place with `cp -f`, with no rollback if the second copy fails, and `.bak-<stamp>` files accumulate. There is no uninstaller, and no GOG/Wine discovery although the README promises it. `steam_game_paths.sh` is sourced from `main` without a hash. `deploy_linux_proton.sh` duplicates `install_linux.sh`. **Resolved 2026-09-27** (GrizzlyOne95/bzfile#21): `--game-path` requires the game exe, installs stage and rename, three backups are kept, `--uninstall` exists, GOG under Wine is documented via `--game-path`, and `deploy_linux_proton.sh` wraps `install_linux.sh`. **Open:** `steam_game_paths.sh` is still fetched unhashed when the installer is piped from curl. | C-4, C-5, C-7, C-8, C-13 |
| P2-3 | Hygiene. `.gitattributes` has no `*.sh eol=lf` pin, and all four scripts are CRLF in this checkout. `lib/Lua5.1-BZR.lib.bak-20260314` is tracked and unreferenced; it is a `/GL` build that only the exact same compiler can link. `lib-temp/` is an empty leftover. `RootNamespace` is `DeathmatchPlus`, and `LargeAddressAware` is set on a DLL. The shipped DLLs embed the maintainer's absolute PDB path (fix with `/PDBALTPATH:%_PDB%`). **Resolved 2026-09-27** (GrizzlyOne95/bzfile#19, #20): LF pin for `*.sh`, `.bak` library removed, `RootNamespace` fixed, `LargeAddressAware` dropped from the DLL, `/PDBALTPATH:%_PDB%`. `lib-temp/` is untracked local litter. | C-9, C-10, C-15 |
| P2-4 | CI. Actions are pinned by tag, including `softprops/action-gh-release@v2` with `contents: write`. There is no `timeout-minutes` or `concurrency`, and PDBs are discarded. The tag release does not wait for the Linux lane, `bzfile.dll` is never loaded into a Lua host, and the Debug config is never built. No CI enforces the rule that the shared docs stay byte-identical. **Mostly resolved 2026-09-27** (GrizzlyOne95/bzfile#14, #20): actions pinned by SHA, timeouts and concurrency, the tag release waits for the Linux checks, and CI loads `bzfile.dll` into a Lua host. **Open:** PDBs are not archived, the Debug configuration is never built, and nothing enforces byte-identity of the shared docs. | C-12 |
| P2-5 | Small binding fixes. `StageOpenShimSuiteUpdate` builds paths before its `luaL_checkstring` calls, so a type error leaks them. The suite name-mismatch message prints the actual name instead of the expected one. `Read(0)` and `Read(-5)` return one byte. `Readln` truncates at an embedded NUL. The `"FileMetatable"` registry key is generic, and the module leaks a `_bzfile_impl_file_table` global. Relative paths resolve against the working directory, although the root deliberately does not. `NormalizePath` falls back to a lexical path on any error other than not-found. **Resolved 2026-09-27** (GrizzlyOne95/bzfile#14, #22). | A-13, A-14, A-16, A-17, A-18, A-20, B-14 |

## 4. Dead code and unused surface

- `BZFILE_VERSION_MAJOR/MINOR/PATCH/BUILD` are never referenced (P1-7).
- `SetAllowWinmmOverwrite`/`GetAllowWinmmOverwrite`, `ReplaceFileOnExit`, `ListDirectory` and `Delete` are exported, but nothing in CR, OpenShim or EXU calls them. The first three are recommended for removal (P0-1). `Delete` and `ListDirectory` should be documented if kept.
- The helper's legacy 5-argument (non-hardened) mode is reachable only from `ReplaceFileOnExit`, so it goes with it.
- `ComputeSha256` is duplicated in the DLL and the helper; share one header.
- `lib/Lua5.1-BZR.lib.bak-20260314` and `lib-temp/` are leftovers, and `scripts/deploy_linux_proton.sh` duplicates `install_linux.sh`.
- CR: `GetOpenShimReplaceLogPath` is unused.

Worksheet A section 3 has the full list, with the greps used.

## 5. Hardening and build notes

- Both binaries use:
  - the static CRT, so no VC++ redistributable is needed;
  - `/W4`, `/WX` and `/sdl`;
  - `/DYNAMICBASE`, `/NXCOMPAT` and `/SAFESEH`.
- There is no `/guard:cf`. That is acceptable for a plain Lua module, and it costs nothing to add here, because bzfile writes no trampolines (unlike OpenShim).
- `luaopen_bzfile` is exported with `__declspec(dllexport)`, and the DLL imports only KERNEL32, ADVAPI32 and VERSION. The helper carries an `asInvoker` manifest.
- SHA-256 uses the deprecated CryptoAPI (`CryptAcquireContext` with `PROV_RSA_AES`). It works under Wine, but BCrypt (`BCryptHash`) would drop the provider handling.

## 6. Patterns worth keeping

- Lua arguments are read before any destructible object exists. Path rejections return `nil|false, msg` instead of raising (`TryResolveAllowedPath` and the `PushPathRejection*` helpers).
- The sandbox root is pinned once to the exe directory, not the working directory, and the Workshop root is cached.
- `IsSandboxRootOrAncestor` stops `Delete(GetWorkingDirectory())` from erasing the install.
- `StageOpenShim*`:
  - fixed sources and destinations;
  - an x86 DLL PE check;
  - hashing before the wait and re-hashing after install;
  - backups of all suite files before the first promotion;
  - a `MoveFileExW(REPLACE_EXISTING | WRITE_THROUGH)` rename;
  - a named mutex against concurrent helpers.
- Helper arguments cannot inject extra arguments or change a destination:
  - the DLL quotes them correctly for `CommandLineToArgvW`;
  - the helper checks the exact argument count.
- CI runs the helper's single-file and suite transaction test on every PR.
- The installers avoid OpenShim's F13 `grep -q`/`pipefail` SIGPIPE pattern.
- The shared cross-repo docs are byte-identical today.

## 7. Cross-repository notes

- **CR:**
  - Open saves with `"rb"`/`"wb"` in `AutoSave.lua` (P0-3).
  - Treat a pending status as stale when `Local\BZR_OpenShim_Update` does not exist (P0-6).
  - Fix the `MakeDirectory` stub contract in `RequireFix.lua` (P2-1).
  - CR's `Bin/bzfile.dll` is a local build that differs from the Workshop copy, yet both report 1.0.0.0 (P1-7).
- **OpenShim:**
  - It has the same breakaway-without-fallback problem (`openshim_updater.cpp:514-517`).
  - Its check of the update mutex before staging is the pattern for P0-7.
  - Its P0-8 (helper launched unverified) is the other half of P1-4.
- **EXU:** P1-1 should share EXU G-1's `dummynode` gate and, ideally, one Lua core build.
- **AGENTS.md (bzfile and CR):** `main` still names a Google Drive tree as
  CR's only editable source. CR has moved to `Documents\GIT\Campaign-Reimagined`,
  which is what this audit read; the correction is commit 2ae86bb on the
  unmerged `agent/campaign-path-migration` branch, and CR's own `AGENTS.md:7`
  needs the same update. This supersedes the AGENTS.md part of C-14.
