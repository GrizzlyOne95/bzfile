# Worksheet A: Lua bindings, file I/O, path policy, write protection, staging (2026-09-27)

Reviewer scope: `src/LuaExport.cpp` (1589), `src/dllmain.cpp` (19), `src/version.cpp` (8), `include/bzfile_version.h` (12), `src/bzfile_version.rc` (36). Context read: `README.md` (226, API spec section in full), `AGENTS.md` (32), `.jules/sentinel.md` (4), `Docs/BZR_PLATFORM_COMPATIBILITY.md` (87), `Docs/BZR_LUA_AGENT_REFERENCE.md` (1214, grepped for bzfile/io/debug/package), `include/lua.h` (388), `luaconf.h` (766), `lauxlib.h` (174), `lualib.h` (53), `lua.hpp` (9), `src/bzfile_replace_helper.cpp` (640, read in full but only judged as the receiver of LuaExport's arguments), `src/bzfile_replace_helper_version.rc` (36), `bzfile.vcxproj` (settings only). Consumers (read-only, `Campaign-Reimagined` git checkout at `origin/main` e7ab84a; the four files are identical to the working tree): `Scripts/OpenShimInstaller.lua` (1137), `AutoSave.lua` (412), `CareerStats.lua` (661), `LogPaths.lua` (40), plus grep hits in `RequireFix.lua`, `RuntimeEnhancements.lua`, `ScriptSubtitles.lua`, `PersistentConfig.lua`. Base: origin/main 804ebae.

Method notes: every in-scope file read end to end at 804ebae. Every `luaL_check*`/`luaL_opt*`/`luaL_error` site, every `std::filesystem::` call and every `.string()` narrow conversion was enumerated with grep (§2b). MSVC STL behaviour was checked against the installed toolset source (`VC/Tools/MSVC/14.38.33130/crt/src/stl/filesystem.cpp:282-320` for the narrow/wide conversions, `include/filesystem:2979-2995` for `absolute`, `:3016-3056` for `_Canonical` prefix stripping, `:4088-4130` for `weakly_canonical`). The bundled `lib/Lua5.1-BZR.lib` was inspected with a Python byte scan (30 `.obj` members, 0 `__imp_` symbols, so it is a static Lua core; 7 occurrences of the immediate `F0 EE 86 00`, i.e. the EXU `dummynode` patch). Installed Steam and GOG game roots were listed read-only for the census of critical files. Consumer usage was grepped across `Campaign-Reimagined`, `ExtraUtilities` and `BZR-OpenShim` Lua. Nothing was built or executed; no game install was modified. Note: the refreshed `AGENTS.md` names the Google Drive tree as CR's editable source; consumer findings here are against the GIT checkout and should be re-checked there by whoever edits CR.

Threat model used for severities: bzfile's allowed roots (game root and Workshop root) make it a write-anything-in-the-install API by design, and BZR's Lua has the `package` library (CR's `RequireFix.lua` drives `package.cpath`), so a script that can write bytes into a root can almost certainly `package.loadlib` them. The write-protection list is therefore a guard against accidental clobbering and a policy contract (sentinel.md), not a security boundary against a malicious script. Findings are rated against that contract.

## 1. Section map

### src/LuaExport.cpp (1589), status: production (one debug-only helper is dead)
| Lines | What | Status |
|---|---|---|
| 1-14 | Includes (`lua.hpp`, `Windows.h`, `wincrypt.h`, STL) | production |
| 19 | `g_AllowWinmmOverwrite` process-global flag | production |
| 21-47 | `ToLower`, `NormalizePath` (`absolute` then `weakly_canonical`, lexical fallback) | production |
| 49-96 | `ResolveGameRootPathOnce` (exe dir via `GetModuleFileNameW(nullptr)`, cwd fallback) and cached `GetWorkingDirectoryPath` | production |
| 98-105 | `GetLogsDirectoryPath` (creates `<root>\logs`) | production |
| 107-153 | `FindSteamAppsDirectory` walk-up, `ResolveWorkshopDirectoryPathOnce` (fallback `root/../..`), cached | production |
| 155-211 | `IsPathInsideRoot` (component-wise, case-insensitive), `IsSandboxRootOrAncestor` | production |
| 213-262 | `TryResolveAllowedPath` (non-raising), `PushPathRejectionNil/False` | production |
| 264-304 | `QuoteCommandLineArgument` (CommandLineToArgvW rules) | production |
| 306-337 | `GetCurrentModulePath` (bzfile.dll path) | production |
| 339-353 | `IsWriteProtected` (leaf name `winmm.dll`/`bzfile.dll`, bypassed by the flag) | production |
| 355-405 | `NarrowSystemError`, `LaunchHiddenProcess` (`CreateProcessW`, no inheritance, breakaway) | production |
| 408-415 | `DebugPrint` (`_DEBUG` only, never called) | dead |
| 417-497 | `Open` (mode/option parse, write-protect check, placement-new `std::fstream` in userdata) | production |
| 499-647 | `Cleanup` (`__gc`), `Write`, `Writeln`, `Read`, `Readln`, `Dump`, `Flush`, `Close` | production |
| 649-700 | `GetWorkingDirectory`, `GetWorkshopDirectory`, `MakeDirectory`, `Exists` | production |
| 702-763 | `CopyFile` (remove-then-copy on overwrite) | production |
| 765-853 | `ReplaceFileOnExit` (generic deferred replace, 4-arg helper mode) | production (no known consumer) |
| 855-1000 | `CryptProvider/CryptHash` RAII, `ComputeSha256`, `IsSha256`, `IsX86PortableExecutable`, `SamePath` | production |
| 1002-1080 | `GetFileHash`, `GetFileVersion` | production |
| 1082-1203 | `StageOpenShimUpdate` (single, 7-arg hardened helper mode) | production (no known consumer) |
| 1205-1406 | `StageOpenShimSuiteUpdate` (3 payloads, `--suite` mode) | production |
| 1408-1468 | `Delete` (root/ancestor refusal, protected-file scan, `remove_all`) | production |
| 1470-1506 | `ListDirectory` | production (no known consumer) |
| 1508-1518 | `SetAllowWinmmOverwrite` / `GetAllowWinmmOverwrite` | production (no known consumer) |
| 1521-1562 | `lua_Init`: method table (also global `_bzfile_impl_file_table`), `FileMetatable` with `__gc`/`__index` | production |
| 1564-1589 | `luaopen_bzfile`: `luaL_register(L, "bzfile", ...)`, returns 0 | production |

### src/dllmain.cpp (19), status: production
| Lines | What | Status |
|---|---|---|
| 1-19 | `DllMain`: `DisableThreadLibraryCalls` on attach, nothing else | production |

### src/version.cpp (8), include/bzfile_version.h (12), src/bzfile_version.rc (36), status: production
| Lines | What | Status |
|---|---|---|
| version.cpp 1-8 | `extern "C" BZFILE_GetVersion()` export returning `BZFILE_VERSION_STRING` | production (no consumer found; undocumented) |
| bzfile_version.h 6-9 | `MAJOR/MINOR/PATCH/BUILD` | dead (never referenced) |
| bzfile_version.h 10-12 | `TUPLE`, `STRING`, `FILE_STRING`: three independent literals | production |
| .rc 6-36 | VERSIONINFO (fixed tuple + string table, 0409/1200) | production |

## 2. Findings

| ID | [Sev/Conf] | file:line | Finding | Why it matters | Suggested fix | How verified |
|---|---|---|---|---|---|---|
| A-1 | [High/Med] | src/LuaExport.cpp:541, 564-565; 609-624 | Size handling in `Read`/`Dump` is unbounded. `Read(count)` allocates `std::vector<char>(count)` for any `count` up to `INT_MAX` before reading, whatever the file size. `Dump` does `content.resize(static_cast<size_t>(tellg()))`. On this 32-bit module a large request throws `std::bad_alloc`, which nothing catches (§2b). For a file over 4 GiB the `streamoff`→`size_t` cast truncates: `resize` allocates only the low 32 bits of the size, and `read(content.data(), size)` is then given the full 64-bit size, which overflows the heap buffer. | `f:Read(2^31-1)` (or `2^30`) from any mission script throws a C++ exception through the C-built Lua core (`LUAI_THROW` is `longjmp`, luaconf.h:618-625, and there is no `catch` anywhere) into the engine. That crashes the game or, if something upstream catches it, leaves `L->nCcalls`/`ci`/`errorJmp` inconsistent. `Dump` of a 2-4 GiB file does the same. Above 4 GiB it corrupts memory. Neither install has such a file today (none >1 GB), but a script can create one by appending. | Clamp `count` to the bytes remaining and to a hard cap (e.g. 64 MiB), and read in chunks through `luaL_Buffer`. In `Dump`, reject sizes above the cap (and above `SIZE_MAX`), read in chunks, and push `gcount()` bytes. Also wrap each binding in `try/catch(...)` (see A-7). | Code read. `luaL_optint` is `(int)luaL_optinteger` (lauxlib.h:105) and `lua_Integer` is `ptrdiff_t` (luaconf.h:146). MSVC's `fistp` conversion (luaconf.h:566) sends out-of-range values to `INT_MIN`, so the largest reachable count is `INT_MAX`. I did not verify how the game handles the escaped exception, hence Med confidence. |
| A-2 | [High/High] | src/LuaExport.cpp:726-760 | `CopyFile(src, dst, true)` deletes an existing destination **before** trying the copy (`SetFileAttributesW(NORMAL)` + `remove`, then `copy_file`). It checks neither the source nor `SamePath`. So if the source is missing, unreadable, a directory, or the same file as the destination under any spelling that canonicalizes to it, the destination is deleted and the call returns `false, msg`. | Data loss on an ordinary failure path: copying a payload that Steam has not finished downloading over an existing file, or `CopyFile(p, p, true)`. CR uses this call to install `openshim.ini` (`OpenShimInstaller.lua:907-911`). CR is safe there because it backs the file up first (896-900); other callers are not. The call also silently strips read-only/hidden/system attributes. | Check `is_regular_file(src)` and `!SamePath(src, dst)` first. Copy to a tmp file next to `dst`, then `MoveFileExW(tmp, dst, MOVEFILE_REPLACE_EXISTING or MOVEFILE_WRITE_THROUGH)`, which is atomic on NTFS and a `rename` under Wine. Remove the tmp on failure. | Traced `CopyFile` end to end. Grepped consumers: only `OpenShimInstaller.lua:897,908`. |
| A-3 | [Med/High] | src/LuaExport.cpp:609-624; Campaign-Reimagined/Scripts/AutoSave.lua:35-41, 300-319 | `Dump` sizes its buffer from `tellg()`, which counts raw bytes. But the handle is in text mode unless the script passed `"b"`, and `Open` defaults to `"r"`. MSVC text-mode reads convert CRLF to LF and stop at `0x1A`, so fewer bytes arrive than were allocated. `lua_pushlstring(content.data(), content.size())` then returns the string padded with one `\0` per CR, or with NULs for the whole tail after a `0x1A`. | CR's AutoSave backs up the current `.sav` with `Open(filename, "r")` + `Dump()` and writes it with `Open(backup, "w", "trunc")`. So every backup gains trailing NULs, and a binary save (`-binarysave`) is corrupted: lone LFs become CRLF, and everything after a `0x1A` becomes NULs. That backup is the only recovery copy AutoSave keeps. | Open the read side of `Dump` in binary, or push `gcount()` instead of `content.size()`. Document the text/binary behaviour of `Read`/`Readln`/`Dump`, and have AutoSave pass `"rb"`/`"wb"`. | Code read. MSVC `basic_filebuf` opens with `_wfsopen(..., "r")` (text) unless `ios::binary` is set. Consumer read. Not executed; confidence rests on the documented CRT text-mode behaviour. |
| A-4 | [Med/High] | src/LuaExport.cpp:19, 339-344, 1508-1518 | `bzfile.SetAllowWinmmOverwrite(true)` sets a process-global flag that makes `IsWriteProtected` return `false` for everything. That includes `bzfile.dll` and the recursive scan in `Delete`. Any script in the state can call it, and it stays set for every script until the DLL unloads. The name suggests it only concerns `winmm.dll`. | The sentinel.md "centralized critical-DLL protection" becomes an opt-out that any script can take with one call. No consumer calls either function (grep over CR, EXU and OpenShim Lua: 0 hits). The sanctioned OpenShim path (`Stage*`) never needs it, because its destinations are fixed. | Remove both functions. If a debug escape hatch is wanted, gate it on a build flag, not on a Lua call. | Grepped every `g_AllowWinmmOverwrite` use (19, 341, 1510, 1516) and all consumers. `git log -S`: the setter has existed since the initial commit 897de24. |
| A-5 | [Med/High] | src/LuaExport.cpp:339-353; callers 457, 719, 779, 1433, 1448; missing at 661-682 | The protection list is two leaf names (`winmm.dll`, `bzfile.dll`), matched on `filename()` only. (a) Several files that matter to the bundle and sit inside the allowed roots are not protected: `bzfile_replace_helper.exe` (which `Stage*` launch later), `net.ini`, `scripts\patches.json`, `openshim.ini`, `bzloader.dll`, `battlezone98redux.exe`, the `.previous` backups, the staged `*.pending*` payloads (A-9), and the `*_update.status` files that CR trusts (A-8). (b) `MakeDirectory` never calls `IsWriteProtected`: `MakeDirectory(root.."\\winmm.dll")` on an install without OpenShim creates a directory that later makes the helper's `MoveFileExW` fail. (c) For a protected name that does **not exist yet**, a trailing dot or space (`winmm.dll.`, `winmm.dll `) survives `weakly_canonical` as part of the lexical tail. Win32 strips it when the file is created, so `Open`/`CopyFile` create `winmm.dll`. For files that already exist, canonicalization closes this hole, and likewise 8.3 names, case, `::$DATA` and junctions (§7). | This is a policy gap against sentinel.md ("all functions that can modify or remove files"). Any script can overwrite the net/patch configs that the suite transaction takes care to verify, or replace the helper exe that `Stage*` then launches with breakaway. It is not a security boundary (see the threat model), but these are exactly the accidental-clobber cases the rule exists for. | Replace the name check with one table of protected paths anchored to the roots and compared after canonicalization: `<root>\winmm.dll`, `net.ini`, `scripts\patches.json`, `openshim.ini`, `bzloader.dll`, `*.exe`, `<moduleDir>\bzfile.dll`, `<moduleDir>\bzfile_replace_helper.exe`, `*.pending*`, `*.previous`, `*_update.status`. Reject names ending in a dot or space. Call the check from `MakeDirectory` as well. | Enumerated every mutating path (§2c). Listed both installed roots read-only: Steam and GOG both contain `bzloader.dll`, `net.ini`, `net.ini.previous`, `openshim.ini` and `openshim_update.status`. The trailing-dot behaviour is reasoned from `weakly_canonical` (filesystem:4088-4130) and Win32 path normalization, not executed. |
| A-6 | [Med/High] | src/LuaExport.cpp:136-145 | If no `steamapps` ancestor is found, the Workshop root falls back to `bzrRoot.parent_path().parent_path() / "workshop/content/301650"`. For the installed GOG game that is `C:\Program Files (x86)\GOG Galaxy\workshop\content\301650`. For `D:\GOG Games\Battlezone 98 Redux` it is `D:\workshop\content\301650`, and under Heroic/Lutris it is `~/Games/workshop/...`. That made-up directory becomes a second **allowed write root**, and `GetWorkshopDirectory` returns it. | Scripts can `MakeDirectory` and write into a tree outside the install that belongs to nothing (a drive root, or GOG Galaxy's own directory). This contradicts the platform rule against hard-coding one store's layout. CR's workshop discovery (`OpenShimInstaller.lua:64-70`) takes whatever this returns. | Use an empty workshop root and return `nil` from `GetWorkshopDirectory` when no `steamapps` ancestor exists. `TryResolveAllowedPath` already skips an empty root (238). | Traced the walk-up for the installed GOG path. `IsPathInsideRoot` rejects an empty root (157). |
| A-7 | [Med/Med] | src/LuaExport.cpp:651, 657, 1493 (plus the message sites in §2b); 233; 1491; 89 | C++ exceptions can reach the game. (1) `std::filesystem::path::string()` throws `std::system_error` (ERROR_NO_UNICODE_TRANSLATION) for any character the ANSI code page cannot represent. MSVC converts with `WC_NO_BEST_FIT_CHARS` and treats `_Used_default_char` as an error. (2) The inbound `path(const char*)` conversion uses `MB_ERR_INVALID_CHARS` and throws on invalid DBCS input. (3) `ListDirectory`'s range-for uses the throwing `directory_iterator::operator++`. (4) `current_path()` (89) is the throwing overload. (5) `bad_alloc` from A-1. None of the 15 bindings has a `try`. | `GetWorkingDirectory()` throws on every call when the install path is not representable in the ACP. Examples: a CJK Steam library on a 1252 system, or a non-ASCII Linux user name under Proton, where Wine's ACP follows the locale and is usually 1252. CR calls it while scripts load, and `pcall` cannot catch a C++ exception, so the game crashes at mission start. `ListDirectory` crashes on any directory that contains one such file name. | Wrap each binding in `try/catch(...)` and turn the exception into `nil/false, message` after the locals are destroyed. Convert at the Lua boundary explicitly: either lossy and non-throwing (`WideCharToMultiByte(CP_ACP, 0, ...)`), or documented UTF-8 via `u8string()`. Use the `error_code` form of `increment` in `ListDirectory`. | Read the installed MSVC STL source (filesystem.cpp:282-320). Grepped all 18 `.string()` sites and all `std::filesystem::` calls. Not run with a non-ACP path. |
| A-8 | [Med/Med] | src/LuaExport.cpp:1381-1400, 1182-1197, 382-395; Campaign-Reimagined/Scripts/OpenShimInstaller.lua:703-710, 960-966 | Both `Stage*` write `state=staged` and the expected hash to the status file **before** `CreateProcessW`. If the launch fails they delete the staged payloads but leave the status at `staged`. The launch always passes `CREATE_BREAKAWAY_FROM_JOB`. Inside a job without `JOB_OBJECT_LIMIT_BREAKAWAY_OK`, that makes `CreateProcessW` fail with `ERROR_ACCESS_DENIED`, and there is no retry without the flag. The first `staged` write also carries no PID or timestamp. | CR treats `staged`, `waiting_for_exit` and `already_staged` with a matching hash as `RESTART_REQUIRED`, and `Apply` then returns `already_staged` without staging again. Any of these wedges the installer for good: one failed launch, a helper that dies, or an old helper that exits 2 on the `--suite` argument-count check without writing status. Every launch then says "restart required" and nothing ever installs. | Write the status only after a successful launch, or write `state=failed` with the error when the launch fails. On `ERROR_ACCESS_DENIED`, retry without breakaway and record that. Put `pid=` and `updated=` in the first write, so CR can expire a pending state older than the helper's 10-minute limit. | Read both `Stage*`, the helper's argument-count checks (bzfile_replace_helper.cpp:403-406, 536-540), and CR's state machine. Breakaway behaviour is from the CreateProcess documentation. Whether GOG Galaxy's job allows breakaway is unverified. |
| A-9 | [Med/Med] | src/LuaExport.cpp:1149, 1164, 1330-1331, 1348-1352; 786-794 | Staged payloads sit in script-writable roots under predictable names (`<moduleDir>\winmm.dll.pending.<hash12>`, `<moduleDir>\openshim_suite_<n>.pending.<hash12>`, `<dst>.pending`), and none of them is write-protected (A-5). The helper hashes them once at startup (bzfile_replace_helper.cpp:441-456, 578-590). It then waits up to 10 minutes while scripts keep running, and promotes **without hashing again**; only the installed destination is verified afterwards. Suite mode rolls back correctly. Single mode takes no backup on a fresh install (no existing `winmm.dll`, 598-610), so when verification fails there is nothing to restore (626) and the unverified file stays installed as `winmm.dll`. If a stale `winmm.dll.previous` exists, single mode restores that old file instead. `ReplaceFileOnExit` never hashes at all. | TOCTOU between the hash check and promotion. Anything that runs during the wait can swap the payload: a script through `Open`/`CopyFile`, or Steam updating the Workshop item. Only the single-file path can end with an unverified DLL in the game root, and that path has no consumer today. | Lua side: protect `*.pending*` (A-5), and stage in a directory scripts cannot address. Helper side (worksheet B): hash the staged file again right before `MoveFileExW`. In single mode, delete the destination when verification fails and this run took no backup, as `RollBackSuite` does. | Traced LuaExport's argument construction and both helper paths. Worksheet B should confirm the helper side. |
| A-10 | [Med/Med] | src/LuaExport.cpp:1116-1121, 1226-1231, 1149, 1330 | Both `Stage*` require the directory that holds `bzfile.dll` to be named `3686673790` (the Workshop item id), and they write the staged payloads into that directory. The platform doc says Workshop content is "a read-only downloaded source" and forbids hard-coding one store's layout. CR also searches `addon\`, `mods\` and `packaged_mods\` for the id `campaignReimagined` (`RequireFix.lua:18`, `OpenShimInstaller.lua:278-280`). | GOG installs and local `addon\campaignReimagined` layouts can never use the hardened update path. The call fails with "restricted to Workshop item 3686673790", so GOG users get no OpenShim install or update through CR. Writes into Workshop content can also be reverted by Steam verification or an item update during the wait. | Identify the payload directory by its contents (the manifest and exact files), or accept a fixed set of mod roots under the game root. Stage into a bzfile-owned directory under the game root instead of the Workshop folder. | Code read. Read CR's search roots and quoted the platform rules. GOG behaviour not executed. |
| A-11 | [Med/High] | src/LuaExport.cpp:765-853 | `ReplaceFileOnExit(src, dst)` is a generic deferred replace, and the script chooses the destination: anything inside either root except the two protected names. It uses no hash and no backup. It also takes no named mutex, unlike `Stage*`, so it can race the OpenShim helper on the same file. The staged `dst.pending` sits next to the target, and a detached helper force-moves it after the game exits. | This is the arbitrary-destination update primitive that AGENTS.md says update helpers must not grow. Any script can schedule the replacement of a loaded `OgreMain.dll`, `steam_api.dll` or `bzloader.dll`, or of the game exe. No consumer uses it (grep: 0 hits). | Remove it, or restrict it to the `Stage*` model: a fixed destination table, an expected hash, the mutex, a backup and verification. | Code read. Consumer grep across CR, EXU and OpenShim Lua. |
| A-12 | [Med/Med] | bzfile.vcxproj:75, 104; lib/Lua5.1-BZR.lib, lib/Lua5.1-BZR-debug.lib | bzfile statically links a prebuilt Lua 5.1 core (30 `.obj` members, no imports) that exists only as a binary in this repo. The core carries EXU's `#define dummynode (0x86EEF0)` patch (7 immediates in each lib). So every table resize or free done by bzfile's copy compares against a hard-wired exe address: `lua_newtable` in `lua_Init`, `luaL_register`, and the result table of `ListDirectory`. There is no build gate and no provenance note. | This is EXU G-1, inherited. On any exe whose `dummynode_` has moved, bzfile's `luaH_resize`/`luaH_free` would free the host's static node, corrupting the heap as soon as the module loads. Only the GOG address has been verified (EXU G-1); the Steam address has not. | Build the core from source in the repo (or from a pinned copy of EXU's `Lua5.1-BZR`), document the two-core model, and ideally share one gated anchor with EXU. Worksheet C owns provenance. | Python byte scan of both libs (`F0EE8600` x7, 0 `__imp_` symbols). Cross-checked against ExtraUtilities/Lua5.1-BZR/src/ltable.c:72-74 and EXU G-1. |
| A-13 | [Low/High] | src/LuaExport.cpp:1217-1245 | `StageOpenShimSuiteUpdate` builds `modulePath` and `moduleDirectory` (`std::filesystem::path`) before its six `luaL_checkstring` calls. A non-string argument therefore `longjmp`s past both objects, which is the leak the comment at 1233-1236 says was fixed. When the module is not in `3686673790`, the function returns before validating any argument. | A leak on every bad call, and formally undefined behaviour. Every other binding fetches its arguments first. | Move the argument fetches to the top of the function, as `StageOpenShimUpdate` does. | §2a enumeration. |
| A-14 | [Low/High] | src/LuaExport.cpp:1301-1308 | The rejection message prints the **actual** file name ("must be foo.txt beside ...") instead of `payload.expectedName`. | Misleading installer diagnostics. | Print `expectedName`. | Code read. |
| A-15 | [Low/High] | src/LuaExport.cpp:1439-1467 | `Delete` is a recursive `remove_all`. (a) The protected-file scan stops at the first iteration error (`!scanError` is in the loop condition) and deletion goes ahead anyway: it fails open. (b) `remove_all` is not transactional, so a failure part-way leaves a half-deleted tree. (c) It can erase another Workshop item or any game subdirectory (`addon`, `scripts`, `Save`) unless the scan finds a protected leaf. (d) README does not document it. | Large blast radius for an undocumented call. CR only uses it to delete one status file (`OpenShimInstaller.lua:862`). | Split it into a non-recursive `DeleteFile` (all CR needs) and an explicit `DeleteDirectory(path, recursive)`. Abort on a scan error. Document both. | Code read. Consumer grep. |
| A-16 | [Low/High] | src/LuaExport.cpp:30-38, 233 | The root is pinned to the exe directory because the cwd can change (comment at 49-55), yet relative Lua paths are still resolved against the cwd by `absolute()`. | Whether `Open("logs\\x.txt")` is allowed depends on the current cwd. CR always builds absolute paths, so it is not affected. | Resolve relative paths against the game root, or reject them. | MSVC `absolute` calls `GetFullPathNameW` (filesystem:2979-2995). |
| A-17 | [Low/Med] | src/LuaExport.cpp:40-46 | When `weakly_canonical` fails with anything other than not-found (access denied, an unsupported reparse tag), `NormalizePath` falls back to the lexical path. The root decision is then made on a path whose links were never resolved, while the OS call that follows does resolve them. | Fails open. Scripts cannot create links through bzfile, so this needs a hand-crafted install. | For mutating calls, reject when canonicalization fails with any error other than not-found. | Read `weakly_canonical`. |
| A-18 | [Low/High] | src/LuaExport.cpp:549-561, 592-596, 509-534, 628-647 | File-method edge cases. `Read(0)` and `Read(-5)` return one byte, because the test is `count <= 1`. `Readln` uses `lua_pushstring(line.c_str())`, which truncates at an embedded NUL. `Write`, `Writeln`, `Flush` and `Close` never check `fail()`/`bad()`, so a full disk or a write to a read-only handle is reported as success. `Dump` on a write-only handle returns `""`. | AutoSave writes its only backup and then overwrites the save, with no way to detect a failed backup write. | Return `""` for `n <= 0`. Push the line with `lua_pushlstring`. Return `nil, msg` when `!good()` after a write or flush, and have `Close` return `true` or `nil, msg`. | Code read and consumer read. |
| A-19 | [Low/Med] | src/LuaExport.cpp:892-907 | `ComputeSha256` stops reading on `!good()` or `gcount() <= 0`, so a `badbit` read error looks like EOF and the hash of a truncated read is returned as success. The helper has the same loop (bzfile_replace_helper.cpp:197-212). | A wrong hash where an error should be reported. The update path then fails closed on the mismatch. | Check `input.bad()` after the loop. Share one implementation between the DLL and the helper. | Code read. |
| A-20 | [Low/High] | src/LuaExport.cpp:1524-1527, 1552-1559, 1564-1589, 499-507 | Module hygiene. The registry key `"FileMetatable"` is generic, so another module using the same name would alias it (type confusion in `luaL_checkudata`). There is no `__metatable` lock, so a script can `getmetatable(f).__gc = nil`, which leaks the stream and its file lock. The method table is also published as the global `_bzfile_impl_file_table`, which nothing reads. `luaopen_bzfile` returns 0 and relies on `luaL_register` side effects (global `bzfile`, `package.loaded.bzfile`). After `__gc` the userdata holds a destroyed `fstream` with no flag, so a reference resurrected by another finalizer would call methods on a destroyed object. | Low: needs misuse or the `debug` library. | Use `"bzfile.File"`, set `__metatable = false`, drop the global, `return 1`, and keep a `destroyed` flag next to the stream. | Code read. grep: `_bzfile_impl_file_table` is only ever set. |
| A-21 | [Low/High] | README.md:84-211 vs code | Docs drift. `Open` returns `nil, msg` on failure, and accepts `"b"`/`"rb"`/`"wb"`/`"rw"` while ignoring unknown characters. `Close` does not make the variable nil. `MakeDirectory` returns `true` or `false, msg`, not `nil`, and CR's stub copies the wrong claim (`RequireFix.lua:368-370`). `Exists` returns `false, msg` for a rejected path. `GetWorkingDirectory` returns a canonical path with no trailing separator (`...\common\Battlezone 98 Redux`). `GetWorkshopDirectory` returns an absolute `...\steamapps\workshop\content\301650`, or a made-up path on GOG (A-6). `GetFileHash` raises on an unknown algorithm. The `ReplaceFileOnExit` log goes to `<root>\logs\<stem>_replace.log`, not "next to the target". The `StageOpenShimUpdate` log is in `<root>\logs`, not the game root. `Delete`, `ListDirectory`, `Set/GetAllowWinmmOverwrite` and the `BZFILE_GetVersion` export are undocumented. | Consumers and stubs are written against contracts that are wrong. | Rewrite the spec section from the code once A-2, A-4, A-11 and A-15 are settled. | Line-by-line comparison. |
| A-22 | [Low/High] | include/bzfile_version.h:6-12; .github/workflows/build.yml:22-60 | `MAJOR/MINOR/PATCH/BUILD` are never used. `TUPLE`, `STRING` and `FILE_STRING` are three literals maintained by hand. CI compares only the **string** resource (`VersionInfo.FileVersion`) with `STRING`. But `bzfile.GetFileVersion`, and CR's version comparison that uses it, read the **fixed** `FILEVERSION` tuple, which nothing checks. | If one literal is missed in a bump, the shipped binary's fixed version (the one Lua sees) disagrees with its tag. | Derive all three from the four components with stringizing macros, and have CI also check `FileMajorPart..FilePrivatePart`. | Grepped every `BZFILE_VERSION_*` use and read build.yml. |

### High/Med quotes

A-1 (LuaExport.cpp:541, 564-565, 611, 620-622):
```cpp
int count = luaL_optint(L, 2, 1);
std::vector<char> buffer(count);            // up to INT_MAX, bad_alloc uncaught
auto size = handle->tellg();                // 64-bit streampos
content.resize(static_cast<size_t>(size));  // truncates above 4 GiB
handle->read(content.data(), size);         // reads the untruncated size
```

A-2 (LuaExport.cpp:740-753):
```cpp
SetFileAttributesW(destinationWide.c_str(), FILE_ATTRIBUTE_NORMAL);
if (std::filesystem::remove(destinationPath, removeError)) { copyOptions = ...none; }
bool copied = std::filesystem::copy_file(sourcePath, destinationPath, copyOptions, error);
```

A-3 (LuaExport.cpp:621-624; AutoSave.lua:300-318):
```cpp
content.resize(static_cast<size_t>(size));
handle->read(content.data(), size);                  // text mode: fewer bytes arrive
lua_pushlstring(L, content.data(), content.size());  // NUL-padded
```
```lua
local existingSave = bzfile.Open(filename, "r")
local data = existingSave:Dump()
local backupFile = bzfile.Open(backupname, "w", "trunc")
```

A-4 (LuaExport.cpp:341-344, 1510):
```cpp
if (g_AllowWinmmOverwrite) { return false; }
g_AllowWinmmOverwrite = lua_toboolean(L, 1) != 0;
```

A-5 (LuaExport.cpp:346-350, 673):
```cpp
auto fileName = ToLower(path.filename().wstring());
if (fileName == L"winmm.dll" || fileName == L"bzfile.dll") { return true; }
std::filesystem::create_directories(directory, error);   // MakeDirectory, no IsWriteProtected
```

A-6 (LuaExport.cpp:139-144):
```cpp
if (steamapps.empty()) { steamapps = bzrRoot.parent_path().parent_path(); }
return NormalizePath(steamapps / "workshop" / "content" / "301650");
```

A-7 (LuaExport.cpp:651, 1491-1493):
```cpp
lua_pushstring(L, GetWorkingDirectoryPath().string().c_str());   // system_error for non-ACP
for (const auto& entry : std::filesystem::directory_iterator(path, error))   // throwing ++
```

A-8 (LuaExport.cpp:1385, 1391-1399, 391):
```cpp
status << "state=staged\nexpected_sha256=" << payloads[0].expectedHash   // written before launch
if (!LaunchHiddenProcess(...)) { /* remove staged payloads; status stays "staged" */ }
CREATE_NO_WINDOW | DETACHED_PROCESS | CREATE_BREAKAWAY_FROM_JOB,
```

A-9 (LuaExport.cpp:1149):
```cpp
const std::filesystem::path stagedPath = moduleDirectory / (L"winmm.dll.pending." + hashPrefix);
```

A-10 (LuaExport.cpp:1116):
```cpp
if (ToLower(moduleDirectory.filename().wstring()) != L"3686673790")
```

A-11 (LuaExport.cpp:786-787, 835-840):
```cpp
auto stagedPath = destinationPath; stagedPath += ".pending";
std::vector<std::wstring> arguments = { pid, stagedPath, destinationPath, logPath };   // unhashed mode
```

A-12 (the patch as linked; source in ExtraUtilities/Lua5.1-BZR/src/ltable.c:72-74):
```c
#define dummynode		(0x86EEF0)
```

### 2a. Lua error-path sites (longjmp past C++ locals)
All raising calls were enumerated (`grep -nE 'luaL_error|lua_error|luaL_arg|luaL_check|luaL_opt'`: 38 lines, 3 of them comments).
- `Open`, `MakeDirectory`, `Exists`, `CopyFile`, `ReplaceFileOnExit`, `GetFileHash`, `GetFileVersion`, `StageOpenShimUpdate`, `Delete`, `ListDirectory`: every `luaL_check*`/`luaL_opt*` (and `GetFileHash`'s `luaL_error` at 1009) runs before the first C++ object is constructed. Clean.
- `Write`, `Writeln`, `Read`, `Readln`, `Dump`, `Flush`, `Close`: `luaL_checkudata`, `luaL_error("file is not open")` and `luaL_checklstring(L, 2)` all come before any C++ object. Clean.
- `StageOpenShimSuiteUpdate` 1217-1245: two `path` objects are live across six `luaL_checkstring` calls (A-13). This is the only real violation.
- Raises on out-of-memory only (a `lua_push*`/`lua_newuserdata` raising `LUA_ERRMEM`): `Open` 483 (`filePath`, `modeStr` and `options` live), every `PushPathRejection*` call (temporary `std::string`), `Readln` 595, `Dump` 624 (`content`, as large as the file), `GetFileHash` 1024-1028, `GetFileVersion` 1078, both `Stage*` result pushes, `ListDirectory` 1493-1494 (iterator live). These leak only on OOM.
- `Open` constructs the stream inside the userdata **before** `lua_setmetatable`, so a failed construction can never reach `Cleanup`. Correct.

### 2b. C++ exceptions that can reach the Lua VM (no binding has a `try`)
- `std::bad_alloc`: `Read` 564 and `Dump` 621 (A-1). Everywhere else, only on OOM.
- `std::system_error` from narrow conversions (A-7). `.string()` at 245, 460, 492, 651, 657, 722, 782, 818, 1024, 1201, 1307, 1404, 1430, 1436, 1452, 1454, 1485, 1493. The sites that format a path which came in from Lua throw only if canonicalization added a component outside the ACP. 651, 657 and 1493 throw purely because of on-disk names. `path(const char*)` at 233 (DBCS ACPs).
- `std::filesystem::filesystem_error`: the range-for `operator++` at 1491; `current_path()` at 89 (fallback only; it runs inside a function-local static initializer, so it is retried on the next call).
- Every other `std::filesystem` call (26 sites) uses the `error_code` overload.

### 2c. Mutating paths vs `IsWriteProtected` (sentinel.md)
| Path | Mutation | Root check | `IsWriteProtected` | Canonical path? |
|---|---|---|---|---|
| `Open` with `w` (455-460) | create / truncate / append | yes (427) | yes | yes |
| `CopyFile` (719) | remove dst, then create it | src and dst | dst only | yes |
| `ReplaceFileOnExit` (779) | creates `dst.pending`; the helper replaces dst | src and dst | dst only (not `dst.pending`) | yes |
| `Delete` (1425-1456) | `remove_all` | yes, plus the root/ancestor refusal | the target and every scanned leaf | yes (leaves are enumerated names) |
| `MakeDirectory` (661-682) | `create_directories` | yes | **no** (A-5b) | n/a |
| `StageOpenShimUpdate` (1147-1188) | staged copy in moduleDir, status file in root; the helper replaces `<root>\winmm.dll` and writes `.previous` | source only; destinations fixed | not called (a sanctioned fixed-destination exception, not documented as such) | n/a |
| `StageOpenShimSuiteUpdate` (1258-1388) | 3 staged copies and a status file; the helper replaces `winmm.dll`, `net.ini`, `scripts\patches.json` | sources only; destinations fixed | not called (same exception) | n/a |
| `GetLogsDirectoryPath` (98-105) | creates `<root>\logs` | fixed path | n/a | n/a |
| `SetAllowWinmmOverwrite` | switches off every check above | n/a | n/a | A-4 |

## 3. Dead / unreferenced code
Search: `grep -rn 'DebugPrint\|_bzfile_impl_file_table\|BZFILE_GetVersion\|BZFILE_VERSION_\(MAJOR\|MINOR\|PATCH\|BUILD\)'` over the repo, excluding `.git/` and build output. For the API functions: `grep -rIl --include=*.lua -F '<name>'` over `Campaign-Reimagined`, `ExtraUtilities` and `BZR-OpenShim`.
- `File::DebugPrint` (LuaExport.cpp:408-415): `_DEBUG` only and never called.
- `BZFILE_VERSION_MAJOR/MINOR/PATCH/BUILD` (bzfile_version.h:6-9): defined, never referenced. The tuple and the strings are separate literals (A-22).
- Global `_bzfile_impl_file_table` (LuaExport.cpp:1526-1527): written, never read by C or by any consumer.
- `bzfile.GetAllowWinmmOverwrite`: meaningful only alongside the setter (A-4).

Count: 4 items that are dead within the repo.

Exported API with **no consumer** in the three sibling repos. It is not dead, because bzfile is a public library and third-party mods may call it, but it is a candidate for removal or restriction: `ReplaceFileOnExit` (A-11), `StageOpenShimUpdate` (superseded by the suite call), `ListDirectory`, `SetAllowWinmmOverwrite`/`GetAllowWinmmOverwrite` (A-4), and the native `BZFILE_GetVersion` export (undocumented). CR does use: `Open`, `Write`, `Writeln`, `Read`, `Readln`, `Dump`, `Flush`, `Close`, `GetWorkingDirectory`, `GetWorkshopDirectory`, `MakeDirectory`, `Exists`, `CopyFile`, `GetFileHash`, `GetFileVersion`, `StageOpenShimSuiteUpdate`, `Delete`.

All includes in LuaExport.cpp are used (`cwctype`: `towlower`; `iomanip`/`sstream`: hex and version formatting; `wincrypt.h`: CryptoAPI; `system_error`: `NarrowSystemError`).

## 4. Raw literal census
No hard-coded absolute paths or drive letters in scope. Store- and layout-specific literals:

| Literal | Where | Use | Should live |
|---|---|---|---|
| `"301650"` (Steam app id) | LuaExport.cpp:144 | Workshop root | one constants header, also used by CR's `OpenShimInstaller.lua:73,108` and `RequireFix.lua` (they repeat it) |
| `"steamapps"`, `"workshop"`, `"content"` | 112, 118, 144 | Steam layout walk-up | same header; never used as a fallback outside Steam (A-6) |
| `L"3686673790"` (Workshop item id) | 1116, 1226 | staging gate | derive from payload content or a manifest; not a folder name (A-10) |
| `winmm.dll`, `bzfile.dll` | 347, 1124, 1147, 1263-1264 | protection and destination | one protected/destination table (A-5) |
| `bzfile_replace_helper.exe` | 812, 1150, 1334 | helper path | shared header with the helper project |
| `net.ini`, `scripts\patches.json`, `openshim_net.ini.payload`, `openshim_patches.json.payload` | 1271-1282 | suite table | shared header; CR's manifest duplicates it (`OpenShimInstaller.lua:419-422`) |
| `.pending`, `winmm.dll.pending.`, `openshim_suite_<n>.pending.` | 787, 1149, 1331 | staged names | same table (and protect them, A-9) |
| `.previous` backups | 1152, 1266, 1274, 1282 | helper backups | same table |
| `winmm_update.status`, `openshim_update.status` | 1153, 1336 | status files | same table (and protect them) |
| `logs`, `winmm_replace.log`, `openshim_update.log`, `<stem>_replace.log` | 101, 834, 1151, 1335 | helper logs | same table; README disagrees (A-21) |
| hash prefix length `12`, buffer `64 * 1024`, `MAX_PATH`/`32768` | 1148, 1329, 892, 58, 82 | sizing | fine inline |
| `"FileMetatable"`, `"_bzfile_impl_file_table"` | 486, 511..641, 1552; 1527 | Lua registry key and global | `"bzfile.File"`, and drop the global (A-20) |
| `0x86EEF0` (`dummynode`) | inside `lib/Lua5.1-BZR*.lib` | Lua core | a gated, documented anchor (A-12, EXU G-1) |

## 5. Lifetime / ownership notes
- **DLL load/unload.** The DLL loads through `require` (the game's `package` loader), so `luaopen_bzfile` runs once per Lua state. It is not pinned, so the host's loadlib finalizer `FreeLibrary`s it at `lua_close`. The Lua 5.1 finalizers run newest-first, and the loadlib handle udata is created before any file userdata, so every open `fstream` is destroyed by `Cleanup` before the image unloads. That ordering is correct. Nothing keeps function pointers into the image beyond the state (the `_bzfile_impl_file_table` global dies with the state).
- **Static state.** `g_AllowWinmmOverwrite` (per image; resets at reload, shared by every script in the state: A-4). The game-root and workshop-root caches are function-local statics: thread-safe, computed on first use and not in `DllMain`, never recomputed. A failed resolution is cached too; for the workshop root on GOG that caches the invented path (A-6).
- **Handles.** `CreateProcessW` process and thread handles are closed immediately (402-403). No handle is inherited (`bInheritHandles = FALSE`). CryptoAPI provider and hash use RAII. `ifstream`/`ofstream` are scoped. No threads are created. Staged payloads outlive the call by design; when the helper times out or fails they are never cleaned up (§9, B).
- **CRT.** `/MT` static CRT (bzfile.vcxproj:95), so each `fstream` is allocated and destroyed inside bzfile's own CRT. Only a foreign module that registers the same `"FileMetatable"` name could break this pairing (A-20). `DllMain` calls `DisableThreadLibraryCalls` although the CRT is static, which Microsoft's guidance advises against. It is harmless with the UCRT (§8).
- **Lua core.** A private static Lua 5.1 core operates on the game's `lua_State`, with `dummynode` reconciled to a hard-wired exe address (A-12). Allocation goes through the host's `frealloc`, so Lua objects are shared correctly. `luaO_nilobject_` is per copy, but bzfile only compares against it inside its own copy.
- **Stack.** No binding pushes more than 3 values, and `lua_Init` holds at most 3. Every call fits `LUA_MINSTACK` = 20 (lua.h:87), so no `lua_checkstack` is needed (contrast EXU F-2).

## 6. Performance notes
- **Path checks.** `IsPathInsideRoot` re-normalizes the already-canonical cached root on every call (163). One allowed-path decision therefore costs 2-3 `GetFullPathNameW` + `weakly_canonical` passes, each with a `CreateFileW` + `GetFinalPathNameByHandleW` + `CloseHandle` on an existing prefix. `Delete` adds 4 more (`IsSandboxRootOrAncestor`) plus a full recursive scan. Fix: cache the normalized roots once, since they are already canonical. The hottest CR caller is `RuntimeEnhancements.lua:316`, which opens and closes a log per line when debug visual logging is on. That costs roughly hundreds of microseconds per line (reasoned, not measured). No per-frame path in the default configuration.
- **Line reading.** `Readln` uses a buffered `std::getline` through `filebuf`, not a syscall per byte. The costs are one `std::string` and one `strlen` per line. CareerStats loads with it once and saves at most every 5 s when dirty (`CareerStats.lua:7`).
- **Byte reads.** `Read(n)` allocates and zero-fills `n` bytes on every call: 64 KiB per chunk in `OpenShimInstaller.lua:443`, which is negligible. `luaL_Buffer` would avoid it and fix A-1 at the same time.
- **Dump.** `Dump` holds the file twice (the `std::string` plus the Lua copy).
- **Hashing.** `GetFileHash` acquires a new CryptoAPI context per call. One CR install check hashes the ~MB `winmm.dll` about 4 times (Inspect, Apply revalidation, `Stage*`, then the helper twice). That is tens of milliseconds at mission start, not per frame.

## 7. Patterns worth keeping
- **Root pinning.** The root is pinned once to the exe directory, not the cwd (49-96), and the comment explains why. The Workshop root is cached as well.
- **Wide paths internally.** Everything below the Lua boundary is wide: `GetModuleFileNameW`, W APIs, and `std::filesystem` on `wchar_t`.
- **Canonicalization.** `weakly_canonical` resolves an existing prefix through `GetFinalPathNameByHandleW`. Inputs that name existing files therefore have their 8.3 names, case, junctions/symlinks, trailing dots/spaces and `::$DATA` stream suffixes resolved before the root and protection checks. The **same canonical path** is then used for the actual operation, so there is no check/use split. `IsPathInsideRoot` compares component by component and case-insensitively, so `...\BZR2` is not accepted as inside `...\BZR`. An empty input is rejected, because `absolute("")` stays empty. DOS device paths such as `...\CON` become `\\.\CON` under `GetFullPathNameW` (on Windows 10) and fall outside the roots, so they fail closed.
- **Non-raising rejections.** Rejections are ordinary `nil/false, message` returns (213-262), and all but one binding fetch every argument before building C++ objects (§2a).
- **Delete guards.** `Delete` refuses a root or any ancestor of one (a recursive delete of the install was possible before 4e6f864). It scans for protected leaves, and it uses a non-following `recursive_directory_iterator` together with `remove_all`, which does not follow links.
- **Constrained staging.** Destinations are fixed. The sources must sit beside the loaded `bzfile.dll` under exact names. Hashes are lower-cased and validated as SHA-256. The winmm payload is checked to be x86 and a DLL. Staged names carry a hash prefix. A partial staging failure cleans up the payloads already copied. The three-file suite is one transaction behind the helper's named mutex, with verification and rollback.
- **Process launch.** `CreateProcessW` gets an absolute `lpApplicationName`, so there is no search-path hijack. Quoting follows the `CommandLineToArgvW` rules, including trailing backslashes and empty arguments. `bInheritHandles` is FALSE, and both handles are closed.
- **Resource handling.** CryptoAPI objects are RAII-wrapped. Hex output is lowercase. `GetFileVersion` checks the `VS_FIXEDFILEINFO` signature and size, and it works on loaded DLLs.
- **Module entry points.** `DllMain` is minimal. A native `BZFILE_GetVersion` export allows provenance checks without Lua.

## 8. Low-severity items
- LuaExport.cpp:391: `CREATE_NO_WINDOW` is ignored when combined with `DETACHED_PROCESS`. The helper is a GUI-subsystem exe (`wWinMain`) anyway. Drop one of the two flags.
- LuaExport.cpp:318-336: `GetCurrentModulePath` doubles its buffer with no cap, unlike the exe resolver (82-85).
- LuaExport.cpp:815, 1156, 1338: the helper is checked only with `exists()`. There is no version or signature check against `BZFILE_VERSION_*`; `GetFileVersion` could supply one. An older helper silently exits 2 on the suite argument count (A-8).
- LuaExport.cpp:834: `ReplaceFileOnExit` names its log after the destination stem, so `...\winmm.ini` shares `logs\winmm_replace.log` with `StageOpenShimUpdate`, and two same-stem targets share one log.
- LuaExport.cpp:1182-1188, 1381-1388: status files are written in text mode with `\n` (the CRT turns it into CRLF), while the helper writes explicit CRLF. The two are consistent today, but neither side documents it.
- An alternate data stream on a protected file (`winmm.dll:foo`, when the stream does not exist) passes `IsWriteProtected` and creates a stream. The main stream is unaffected. Harmless, but worth rejecting `:` in the leaf.
- `ToLower` uses `towlower` in the "C" locale, so it folds ASCII only. Existing components come back in on-disk case after canonicalization, so this only matters for spellings of paths that do not exist yet.
- dllmain.cpp:13: `DisableThreadLibraryCalls` with the static CRT (`/MT`). Microsoft's documentation advises against the combination. It has no practical effect with the UCRT; drop the call or keep it knowingly.
- LuaExport.cpp:944: `std::isxdigit` relies on `<cctype>` arriving through another header.
- `Dump` calls `clear()` and rewinds, leaving the stream at EOF. Later `Readln` calls then return nil, which is undocumented.
- Unverified: how Proton/Wine's `GetFinalPathNameByHandleW` spells canonical paths (`Z:\...`, and whether Unix symlinks in the Steam library are resolved). The root and candidates go through the same function, so the results should agree, but no Linux lane was run.
- Unverified: whether BZR's Lua exposes `package.loadlib` and `debug`. The threat-model framing (the protection list is not a security boundary) assumes `package` is present, as `RequireFix.lua` requires.

## 9. Notes for other worksheets
- **B (helper/transaction/tests):**
  - Single hardened mode leaves an unverified payload installed on a fresh install, or restores a stale `.previous` (bzfile_replace_helper.cpp:598-633); suite mode handles this correctly (A-9).
  - Neither mode hashes the staged file again before `MoveFileExW` (A-9).
  - The 10-minute `WaitForProcessExit` bound means a player who keeps playing after CR stages mid-campaign gets `failed`, and the staged payloads are left in the Workshop folder with no cleanup.
  - Argument-count mismatches (`return 2`) write no status, which wedges CR (A-8).
  - `ComputeSha256` treats `badbit` as EOF and is duplicated here (A-19).
  - Unverified: if the game process is UAC-virtualized (an unmanifested 32-bit exe writing under Program Files) while the manifested helper is not, the two see different files.
  - No test exercises LuaExport's path policy, write protection or argument construction: `tests/Test-ReplaceHelper.ps1` drives the helper directly.
- **C (scripts/CI/build/hygiene/docs):**
  - The `lib/Lua5.1-BZR*.lib` provenance and the hard-wired `dummynode` (A-12).
  - The tracked `lib/Lua5.1-BZR.lib.bak-20260314`.
  - The version tuple is not checked in CI (A-22).
  - README spec drift (A-21).
  - The shared docs are byte-identical across all four repos (SHA-256 prefixes: platform `88cedde9324b52ad`, Lua reference `15732942b6771ead`).
  - `include/lua*.h` and `luaconf.h` are identical to EXU's `include/` copies.
- **CR (for the coordinator; CR's editable source is the Google Drive tree per the refreshed AGENTS.md, and these reads were from the GIT checkout):**
  - AutoSave should open saves in binary (A-3).
  - The `RequireFix.lua:368-370` stub comment states the wrong `MakeDirectory` contract (A-21).
  - `OpenShimInstaller.Inspect` should expire a pending status that is older than the helper's bound, or that has no live helper (A-8).
  - GOG and local-addon layouts cannot reach `StageOpenShimSuiteUpdate` (A-10).
