# Worksheet B: replace helper, update transaction and tests (2026-09-27)

Reviewer scope, read end to end:
- `src/bzfile_replace_helper.cpp` (640)
- `src/bzfile_replace_helper_version.rc` (36)
- `bzfile_replace_helper.vcxproj` (110)
- `tests/Test-ReplaceHelper.ps1` (126)
- `include/bzfile_version.h` (12)
- `.github/workflows/build.yml` (119), read only for how the test is invoked
- The staging half of `src/LuaExport.cpp` (1589 lines in total):
  - `QuoteCommandLineArgument` 264-304
  - `GetCurrentModulePath` 306-337
  - `IsWriteProtected` 339-353
  - `LaunchHiddenProcess` 361-405
  - `ReplaceFileOnExit` 765-853
  - hash/PE/path helpers 855-1000
  - `StageOpenShimUpdate` 1082-1203
  - `StageOpenShimSuiteUpdate` 1205-1406
  - `Set/GetAllowWinmmOverwrite` 1508-1518
  - registration 1564-1589

Context: `README.md` (226, §160-226), `AGENTS.md` (32), `.jules/sentinel.md` (4), `Docs/BZR_PLATFORM_COMPATIBILITY.md` (87), and the helper-related parts of `scripts/deploy_linux_proton.sh` and `tests/linux/run.sh`.

Cross-repo, read-only:
- `BZR-OpenShim/src/patches/openshim_updater.cpp` (872; read 380-800) at 45ca0b65
- `BZR-OpenShim/src/patches/openshim_update_manifest.cpp` 120-165
- `BZR-OpenShim/docs/CODE_AUDIT_20260925.md` §2-4
- `Campaign-Reimagined/Scripts/OpenShimInstaller.lua` (1137, read in full) and `Scripts/LogPaths.lua`, at bfc24f5

The CR copy under `Documents\GIT` was the only one present on this machine. The Google Drive path that the current AGENTS.md names as CR's canonical source does not exist here. The CR evidence therefore needs a re-check against the canonical tree.

Base: origin/main 804ebae.

Method notes:
- **Reading and history.** Every in-scope line was read. I grepped all of `%USERPROFILE%\Documents\GIT` for callers and consumers of `ReplaceFileOnExit`, `StageOpenShim*`, `SetAllowWinmmOverwrite`, the status file names and the mutex name. I used `git log -S` and `git blame` for the wait timeout (added in 4e6f864, 2026-08-06) and for `SetAllowWinmmOverwrite` (b950a22, bot-authored).
- **Manifests.** Read with `pefile`. Both `Release\bzfile_replace_helper.exe` and the GOG `battlezone98redux.exe` declare `requestedExecutionLevel level='asInvoker'`.
- **Prebuilt helper matches this source.** Its UTF-16 strings "Timed out after 10 minutes", "suite promotion failed; rollback incomplete" and `Local\BZR_OpenShim_Update` are present. SHA-256 `152c8423...d587`, built Sep 19, after the last helper change on Aug 6.

**The helper was executed** twice, only against files created in the scratchpad, after reading the test and the source. `Release\bzfile_replace_helper.exe` was first copied to `<scratchpad>\helperrun\`.

1. **exp1:** `pwsh -NoProfile -File <scratchpad>\exp1.ps1`.
   - Setup: a suite with pid `0` and three staged/destination pairs. `dest2.bin` was held open with `[IO.File]::Open(...,'Open','Read','Read')`.
   - Result: exit 1 after 32 s. `dest1` was correctly restored to `old-1`, and `dest2`/`dest3` were untouched.
   - But the status said `detail=suite promotion failed; rollback incomplete`, and `staged2`/`staged3` were left behind (B-5).
2. **exp2:** `pwsh -NoProfile -File <scratchpad>\exp2.ps1`.
   - Setup: helper A waited on the PID of a 6 s `pwsh Start-Sleep` process I launched. Then helper B ran with a different hash set and the same status path.
   - Result: B exited 0 and the status became `state=already_staged` with **B's** hash.
   - After the sleeper ended, A promoted **A's** payloads and wrote `state=complete` with A's hash. B's staged files were orphaned (B-4).

Nothing was built. No game install was modified; only read-only `ls` and `pefile` reads were done in the GOG directory.

## 1. Section map

### src/bzfile_replace_helper.cpp (640), status: production
| Lines | What | Status |
|---|---|---|
| 1-31 | Includes; RAII `CryptProvider`, `CryptHash`, `ScopedHandle` | production |
| 33-106 | `Utf8FromWide`, `FormatWindowsError`, `TimestampNow` | production |
| 108-172 | `AppendLogLine` (open, append, close per line, UTF-8); `WriteStatus` (CREATE_ALWAYS; `state/expected_sha256/detail/updated`) | production |
| 174-237 | `ComputeSha256` (CryptoAPI PROV_RSA_AES, 64 KiB chunks, lowercase hex) | production |
| 239-260 | `RestoreBackup` (CopyFileW from backup to destination) | production |
| 262-314 | `WaitForProcessExit`: pid 0 means no wait. Then `OpenProcess(SYNCHRONIZE)`; `ERROR_INVALID_PARAMETER` counts as already exited. The wait is capped at 10 minutes and followed by Sleep(1000). | production; the pid-0 branch is diagnostic (tests only) |
| 316-359 | `PromoteReplacement`: up to 120 attempts, 250 ms apart, of `MoveFileExW(REPLACE_EXISTING, COPY_ALLOWED, WRITE_THROUGH)` | production |
| 361-397 | `SuitePayload`; `RollBackSuite` (restores **every** payload from backup, or removes it if it had no destination before) | production |
| 399-517 | `RunSuiteUpdate`: 17-arg check, mutex, validate 3 hashes, wait, back up all 3, promote and verify each, write `complete` | production |
| 520-640 | `wWinMain`: `CommandLineToArgvW`, then dispatch. `--suite` goes to the suite path. 5 args is legacy mode (no hash, backup, mutex or status). 8 args is hardened mode. | production (legacy mode is reachable only via `ReplaceFileOnExit`) |

### src/LuaExport.cpp, staging half, status: production
| Lines | What | Status |
|---|---|---|
| 264-304 | `QuoteCommandLineArgument` (MSVCRT / `CommandLineToArgvW` rules) | production |
| 306-337 | `GetCurrentModulePath` (wide, grows the buffer as needed) | production |
| 339-353 | `IsWriteProtected`: blocks the leaf names `winmm.dll` and `bzfile.dll`. `g_AllowWinmmOverwrite` disables it entirely. | production |
| 361-405 | `LaunchHiddenProcess`: `CreateProcessW`, `bInheritHandles=FALSE`, `CREATE_NO_WINDOW|DETACHED_PROCESS|CREATE_BREAKAWAY_FROM_JOB`; both handles closed at once | production |
| 765-853 | `ReplaceFileOnExit`: any allowed-root destination; stages `<dest>.pending`; launches the helper in **legacy 5-arg mode** | production (no consumer in sibling repos) |
| 855-1000 | Duplicate `CryptProvider`/`CryptHash`/`ComputeSha256` (narrow strings), `IsSha256`, `IsX86PortableExecutable`, `SamePath` | production |
| 1082-1203 | `StageOpenShimUpdate`: the module directory must be named `3686673790`. The source must be `winmm.dll` beside the DLL, checked against the caller's hash and a PE check. Destination, backup, status and log are fixed. Launches the helper with 8 args. | production (no consumer; nobody reads `winmm_update.status`) |
| 1205-1406 | `StageOpenShimSuiteUpdate`: 3 fixed payload names and destinations, caller-supplied hashes. Stages `openshim_suite_N.pending.<hash12>`, writes `state=staged`, launches `--suite`. | production (CR `OpenShimInstaller.lua:1044`) |
| 1508-1518 | `SetAllowWinmmOverwrite` / `GetAllowWinmmOverwrite` (process-global bool) | production (no consumer, undocumented) |

### tests/Test-ReplaceHelper.ps1 (126), status: CI test
| Lines | What |
|---|---|
| 1-45 | Params and asserts. `Invoke-Helper` uses `ProcessStartInfo.ArgumentList`, so it passes exact argv and gets the real exit code. |
| 47-93 | Root is `$env:RUNNER_TEMP`. Cases: hardened single-file success; hash mismatch leaves the destination unchanged; missing staged file fails. |
| 95-120 | Suite success: checks 3 destinations and 3 backups |
| 124-126 | `finally` removes the temp root |

`build.yml:67-69` runs this test on every push to main, every PR to main, and every tag. It runs after the PE-version gate (`build.yml:49-65`), which requires both binaries to carry `BZFILE_VERSION_STRING`.0.

## 2. Findings

| ID | [Sev/Conf] | file:line | Finding | Why it matters | Suggested fix | How verified |
|---|---|---|---|---|---|---|
| B-1 | [High/High] | src/LuaExport.cpp:339-353, 765-853, 1508-1512, 1580; src/bzfile_replace_helper.cpp:536-548, 567-617 | `ReplaceFileOnExit` replaces **any** file under the game or Workshop root when the game exits. It has no hash, no backup and no mutex. Its only guard is the two-name leaf filter `IsWriteProtected`, and any script can switch that off with `bzfile.SetAllowWinmmOverwrite(true)` (process-global, undocumented, no consumer). After that, `ReplaceFileOnExit(<any allowed file>, <root>\winmm.dll)` installs arbitrary bytes as the game-root `winmm.dll` through the helper's legacy mode, which skips hash, backup, status and mutex. That bypasses every `StageOpenShim*` constraint: the item-3686673790 directory, the fixed source name, the SHA-256, the x86-PE check, and the post-verify with rollback. Even with the flag off, it can replace loaded native files that the filter never names, such as `bzloader.dll`, `plugins\openshim.dll`, `battlezone98redux.exe`, other mods' DLLs and `bzfile_replace_helper.exe` itself. `CopyFile` cannot touch these while the game runs because they are locked. | AGENTS.md says "scripts must not gain arbitrary destinations". sentinel.md wants one central critical-DLL guard, and a global switch makes it optional. This is a hash bypass for the exact file the hardened path protects. There is no backup, so a bad payload breaks OpenShim with nothing to roll back to. Calibration: A should confirm whether the generic write APIs already reach code execution (§10). Even if they do, this still crosses the documented policy line. | Delete `Set/GetAllowWinmmOverwrite`. Make `ReplaceFileOnExit` refuse PE images and `.dll/.exe/.asi` destinations, or remove it (OpenShim, EXU and CR do not use it). Make the helper reject legacy 5-arg mode, so every launch carries hash + backup + status. Extend the protected set (§10 A). | Traced DLL → argv → helper legacy path. Grep found no caller of `ReplaceFileOnExit`, `StageOpenShimUpdate` or `SetAllowWinmmOverwrite` in any sibling repo. README does not mention the latter. |
| B-2 | [Med/High] | src/bzfile_replace_helper.cpp:285-310 | The wait is capped at 10 minutes. On timeout the helper gives up (`state=failed`, "could not wait for game process exit"). Both launchers stage early in a session: CR at mission start via `PersistentConfig.lua:7094` → `EnsureOnce`, and OpenShim when its update check completes. A player who keeps playing for more than 10 minutes after staging gets no update. The code comment justifies the cap by PID reuse holding the mutex forever. But the handle is opened by PID (270) after hashing (439-456), and once it is open it cannot be reused. | The common path, "play a mission, then quit", silently fails. CR then reports UPDATE_FAILED and re-stages on the next mission load, so the user sees repeated "restart required" prompts. The update only lands if the user quits within 10 minutes of the latest staging. | Open the target process as the helper's first action. Better, have the DLL pass an inheritable SYNCHRONIZE handle (`PROC_THREAD_ATTRIBUTE_HANDLE_LIST`), which removes PID reuse completely. Then wait INFINITE, or use a long ceiling such as 24 h. The mutex then reflects a live wait. | Code read. `git log -S kMaxWaitMilliseconds` points to 4e6f864. CR caller chain read. |
| B-3 | [Med/High] | src/LuaExport.cpp:1381-1400 (also 1182-1197); src/bzfile_replace_helper.cpp:403-406, 536-540; CR Scripts/OpenShimInstaller.lua:703-710, 960-966 | The status file claims "pending" forever whenever the helper never finishes. The DLL writes `state=staged` **before** `CreateProcessW`. If the launch fails it deletes the staged files but leaves that status in place, whereas OpenShim's updater writes `failed` (openshim_updater.cpp:634). The status is also left at `staged` or `waiting_for_exit` when the helper: returns 2 on an argument-count mismatch without writing status (version skew; the DLL never checks the helper's version); dies in `std::terminate` (B-12); is killed at logoff, shutdown or power loss; or cannot start because breakaway is denied (B-6). CR's `Inspect` checks the pending states **before** anything else, and `Apply` then returns `already_staged` without re-staging. | Every launch reports RESTART_REQUIRED, permanently, until someone deletes `openshim_update.status` by hand. The status file does not tell the truth. | DLL: on launch failure, write `state=failed detail=launch failed: ...`. Expose liveness, for example `bzfile.IsOpenShimUpdatePending()` via `OpenMutexW(SYNCHRONIZE, FALSE, L"Local\\BZR_OpenShim_Update")`. CR: treat a pending state as stale when that mutex does not exist. Helper: before returning 2, write status if the status path argument can be read; add a top-level try/catch (B-12). Optionally the DLL checks that the helper's `GetFileVersion` equals its own. | DLL and CR code traced. The helper's exit-2 paths write nothing (403-406, 537-540). |
| B-4 | [Med/High] | src/LuaExport.cpp:1205-1406; src/bzfile_replace_helper.cpp:424-436, 550-564 | Staging a second time while a helper is active corrupts the shared state. The DLL does not check the mutex first (OpenShim does, `openshim_updater.cpp:576-584`). It overwrites the status with `state=staged <new hash>`, copies new staged files and launches a second helper. That helper writes `already_staged` with **its own** hash into the same status file and exits 0. The first helper later installs the **old** payload and writes `complete <old hash>`, and the new staged files are orphaned. | The status says the second suite is staged when it will never install. The next launch has to rediscover the drift, and the staged files leak into the Workshop item folder. It needs two disagreeing stagers in one session, but the misleading line is written by the helper itself. | DLL: call `OpenMutexW` first. If the mutex is held, return `true, "already_staged"` without touching the status or staged files. Helper: the losing instance should only append to the log and never rewrite the shared status. | **Reproduced** with exp2 (method notes). |
| B-5 | [Med/High] | src/bzfile_replace_helper.cpp:370-397, 494-510, 258 | `RollBackSuite` restores every payload, including ones that were never promoted. If an untouched destination is locked or unwritable, a correct rollback is reported as `rollback incomplete`. Restores run in forward order, and the log line "Restored previous OpenShim backup." does not name the file. Staged files of payloads that were not promoted are left in place. | The status and log are wrong on exactly the path an operator reads after a failure. Files that were never changed get rewritten. | Track a `promoted` flag per payload. Roll back only promoted payloads, in reverse order. Name the file in every log line. Delete leftover `*.pending.*` files after a failed or rolled-back suite. | **Reproduced** with exp1: dest1 was restored and dest2 was never changed, yet the status says "incomplete". |
| B-6 | [Med/Med] | src/LuaExport.cpp:382-395 | `CREATE_BREAKAWAY_FROM_JOB` has no fallback. Per the CreateProcess docs, if the caller is in a job without `JOB_OBJECT_LIMIT_BREAKAWAY_OK`, the call fails with `ERROR_ACCESS_DENIED`. Separately, `DETACHED_PROCESS` and `CREATE_NO_WINDOW` should not be combined (the latter is ignored), and neither matters for a GUI-subsystem child. | Under any launcher whose job denies breakaway, every staging attempt fails. Combined with B-3, the status then stays stuck at `staged`. | On `ERROR_ACCESS_DENIED`, retry without breakaway and log that the helper may die with the job. Or check `IsProcessInJob` and `QueryInformationJobObject` first. Drop `CREATE_NO_WINDOW`. OpenShim `openshim_updater.cpp:514-517` has the same code. | Code read against the MS docs. Not tested against a real non-breakaway job. The Galaxy comment implies Galaxy's job allows breakaway. |
| B-7 | [Med/High] | src/LuaExport.cpp:812, 843, 1150-1160, 1191, 1334-1343, 1391 | The helper exe is launched by path (`<bzfile dir>\bzfile_replace_helper.exe`) with no hash, version or signature check. Scripts can write that path through bzfile's own `Open(path,"wb")` and `CopyFile`, because the name is not protected and the Workshop root is an allowed root. So any mission script can write an exe there, call any of the three staging APIs, and bzfile runs it with `CreateProcessW`. | This is the same issue as OpenShim P0-8 (rated Med/High there), but with a weaker precondition: any Lua script, not a Workshop publisher. Raise it to High if A finds that Lua-written DLLs cannot be `require`d. | Pin the helper's SHA-256 in bzfile.dll at build time (both binaries come from one CI build) and check it before `CreateProcessW`. Add `bzfile_replace_helper.exe` to the protected set. | Traced: Open → IsWriteProtected (name list) → TryResolveAllowedPath (Workshop root) → staging API → `CreateProcessW(helperPath)`. |
| B-8 | [Med/Med] | src/LuaExport.cpp:1094-1129, 1286-1328 | The expected hashes come from the caller, and the "Workshop item 3686673790" gate only checks a directory name. `openshim_net.ini.payload` and `openshim_patches.json.payload` are unprotected names in an allowed root. Any script can rewrite them, hash them with `GetFileHash`, and stage a suite that installs its own `scripts\patches.json` (OpenShim's byte-patch table) and `net.ini`. The hash only proves that what was staged is what gets promoted; it says nothing about where the file came from. | This is a policy gap rather than a new exploit: `CopyFile(x, <root>\scripts\patches.json, true)` already works directly (A). But the README presents these APIs as the constrained path. | Tie payloads to their origin: verify against a manifest shipped and pinned with the DLL, or a signed one, instead of trusting caller hashes. At minimum, document that the gate is a name check. | Code trace. B-1 and §10 A cover the direct path. |
| B-9 | [Low/High] | src/bzfile_replace_helper.cpp:598-610, 623-632, 244 | Hardened single mode backs up only when the destination already exists. If it did not exist and post-verify fails, `RestoreBackup` copies back any **stale** `winmm.dll.previous` from an earlier update instead of deleting the new file. | An old shim gets installed where none existed. Rare, because it needs a post-verify mismatch. | Record `destinationExisted` the way the suite path does, and remove the file on rollback when it was absent. | Code read. |
| B-10 | [Low/Med] | src/bzfile_replace_helper.cpp:329-333, 612-617; src/LuaExport.cpp:790-794, 1164, 1348-1352 | `MOVEFILE_COPY_ALLOWED` turns the promotion into copy-then-delete when source and destination are on different volumes (for example a staged dir behind a junction). A failed copy can leave the destination truncated or deleted, and the single path does not restore on promotion failure. Separately, the DLL's `copy_file` staging is never flushed to disk. NTFS journals metadata but not file data, so a power loss shortly after an atomic same-volume rename can still leave a zero-filled or short file. | Rare, but it is the one way the single path can leave `winmm.dll` missing. The game then starts on the system winmm without OpenShim. | Drop `COPY_ALLOWED` and fail if not on the same volume (staging is always beside the DLL). Call `FlushFileBuffers` on the staged file, in the DLL or in the helper before the rename. Restore the backup when a single-path promotion fails. | Code read. `MoveFileEx` cross-volume failure semantics not tested. |
| B-11 | [Low/High] | src/bzfile_replace_helper.cpp:264-268, 408, 542 | `wcstoul` returns 0 for an empty or unparsable pid, and 0 means "do not wait". That is a test hook in production code. A malformed argv promotes immediately while the game is still running, then fails on the locked winmm after 30 s of retries. | Argument parsing fails open. | Parse strictly: check the end pointer and require non-zero. Put the no-wait behaviour behind an explicit `--no-wait` flag that only the test uses. | Code read. |
| B-12 | [Low/Med] | src/bzfile_replace_helper.cpp:244, 334, 442, 476, 571, 598, 520-640 | Six calls use the **throwing** overload of `std::filesystem::exists`, and `wWinMain` has no try/catch. Any filesystem error other than "not found" (UNC or permission edge cases) calls `std::terminate` in the middle of the transaction, including inside `RestoreBackup` during rollback. | A partial suite with status stuck at `waiting_for_exit`, which feeds B-3. A hidden process may also raise a WER dialog. | Use the `error_code` overloads. Wrap the body in try { ... } catch (...) { WriteStatus(failed); }. | Code read. A throwing path was not forced. |
| B-13 | [Low/Med] | src/LuaExport.cpp:1201, 1404 (also 782, 818) | `path::string()` converts to the ANSI code page and throws `system_error` when the game root contains characters outside it (MSVC `_Convert_wide_to_narrow`). Lines 1201 and 1404 run **after** the helper has been launched. The C++ exception then passes through a Lua C function, and Lua is built as C with longjmp. | A crash right after a successful stage, on non-ACP install paths. | Return UTF-8 (`u8string`) or use a non-throwing conversion. The wider pattern belongs to A (`GetWorkingDirectory` 651/657, 245). | Code read. The MSVC throwing behaviour is Med confidence. |
| B-14 | [Low/High] | src/LuaExport.cpp:1305-1307 | The error text "suite payload %d must be %s beside..." is filled with the **actual** filename (`payload.source.filename()`), not `expectedName`. | Misleading error for a misnamed payload. | Format `expectedName`. | Code read. |
| B-15 | [Low/High] | src/bzfile_replace_helper.cpp:293-312 | `GetLastError()` at 312 is read after `CloseHandle(process)` at 294. | The error text on `WAIT_FAILED` may be wrong. | Capture the error right after `WaitForSingleObject`. | Code read. |
| B-16 | [Low/High] | tests/Test-ReplaceHelper.ps1:47, whole file | The test needs `$env:RUNNER_TEMP`: run locally, `Join-Path $null` throws under StrictMode, so it only runs in CI. It also takes the global `Local\BZR_OpenShim_Update` mutex, so on a dev box with a real update pending, the hardened cases exit 0 as `already_staged` and fail. It covers only 4 happy-path and early-fail cases. Uncovered: rollback after payload 2 fails to promote (exp1 shows about 32 s with a FileStream lock), rollback after post-verify fails, a destination that did not exist before (the remove branch), `already_staged`, legacy 5-arg mode, arg-count exit 2, suite hash mismatch, a real PID wait, `scripts\` creation, backup overwrite. | These gaps are exactly where B-3 to B-5 and B-9 live, so regressions there go unnoticed. | Fall back to `[IO.Path]::GetTempPath()`. Add the exp1 (locked dest2) and exp2 (second helper) cases, a suite case with a missing destination, and arg-count cases. About 35 s in total. | Read the test and build.yml:67-69. |

### High/Med quotes

B-1 (LuaExport.cpp:341-347, 1508-1511; helper 536, 546):
```cpp
if (g_AllowWinmmOverwrite) { return false; }            // IsWriteProtected
if (fileName == L"winmm.dll" || fileName == L"bzfile.dll") { return true; }
static int SetAllowWinmmOverwrite(lua_State* L) { g_AllowWinmmOverwrite = lua_toboolean(L, 1) != 0; return 0; }
const bool hardenedMode = arguments.size() == 8;          // ReplaceFileOnExit passes 4 args -> legacy
const std::wstring expectedHash = hardenedMode ? arguments[5] : L"";
```

B-2 (helper 290-309):
```cpp
constexpr DWORD kMaxWaitMilliseconds = 10 * 60 * 1000;
DWORD waitResult = WaitForSingleObject(process, kMaxWaitMilliseconds);
... L"Timed out after 10 minutes ... abandoning this update so the next launch can retry."
```

B-3 (LuaExport.cpp:1381-1398; CR OpenShimInstaller.lua:703-709):
```cpp
std::ofstream status(statusPath, std::ios::trunc); ... status << "state=staged\nexpected_sha256=" ...
if (!LaunchHiddenProcess(helperPath.wstring(), arguments, launchError))
{ for (...) std::filesystem::remove(payload.staged, error);  /* status left at "staged" */ ... return 2; }
```
```lua
local pendingState = report.updateStatus.matchesBundled and
    (report.updateStatus.state == "staged" or report.updateStatus.state == "waiting_for_exit" or ...)
if pendingState then report.state = OpenShimInstaller.States.RESTART_REQUIRED return report end
```

B-4 (helper 431-435):
```cpp
if (GetLastError() == ERROR_ALREADY_EXISTS)
{ ... WriteStatus(statusPath, L"already_staged", suiteHash, L"another helper is active"); return 0; }
```

B-5 (helper 375-383):
```cpp
for (const auto& payload : payloads)          // all three, promoted or not
{ if (payload.destinationExisted) { if (!RestoreBackup(payload.backupPath, payload.destinationPath, logPath)) allRestored = false; } ...
```

B-6 (LuaExport.cpp:391):
```cpp
CREATE_NO_WINDOW | DETACHED_PROCESS | CREATE_BREAKAWAY_FROM_JOB,
```

B-7 (LuaExport.cpp:1334, 1391):
```cpp
const std::filesystem::path helperPath = moduleDirectory / L"bzfile_replace_helper.exe";
if (!LaunchHiddenProcess(helperPath.wstring(), arguments, launchError))   // no hash/version check
```

B-8 (LuaExport.cpp:1226, 1313):
```cpp
if (ToLower(moduleDirectory.filename().wstring()) != L"3686673790")      // name check only
if (!ComputeSha256(payload.source, sourceHash, validationError) || sourceHash != payload.expectedHash) // caller's hash
```

### Answers to the eight questions (evidence pointers)

1. **Argument contract.**
   - Quoting and parsing are sound. `QuoteCommandLineArgument` (264-304) follows the MSVCRT rules: 2n+1 backslashes before `"`, and trailing backslashes doubled. The helper parses with `CommandLineToArgvW` and accepts only exactly 5, 8 or 17 arguments, so an injected argument changes the count and yields exit 2.
   - Windows paths cannot contain `"` or newlines, and hashes are checked as hex by `IsSha256`.
   - The DLL fixes every `StageOpenShim*` destination, backup, status and log path (1147-1153, 1259-1284, 1335-1336).
   - **No argument injection or destination change is possible through the staging APIs.**
   - The helper trusts argv completely and enforces no destination policy. That is acceptable only because it is not a privilege boundary (it runs asInvoker). All policy therefore lives in the DLL, where B-1 breaks it.
   - Command lines are about 3-5 k characters, well under 32 767.
   - Unicode is wide end to end except the `const char*` Lua inputs (ACP) and the `.string()` outputs (B-13). There is no longPathAware manifest, so paths longer than MAX_PATH fail (Low).
2. **Wait for exit.**
   - The helper opens a handle by PID after hashing (270). The PID-reuse window between `CreateProcessW` and `OpenProcess` is milliseconds (Low; the B-2 fix closes it).
   - If the game has already exited, `ERROR_INVALID_PARAMETER` means proceed after 1 s (274-279). Wine maps `STATUS_INVALID_CID` to the same code (Med confidence).
   - If the game never exits, the 10-minute timeout applies (B-2).
   - Two helpers are serialized by the mutex, and the loser overwrites the status (B-4).
   - Legacy mode takes no mutex. Two `ReplaceFileOnExit` calls for the same destination race: the second finds its `.pending` already moved and fails after 30 s (Low).
   - If the game is relaunched within about 1 s of exit, `MoveFileEx` onto the loaded `winmm.dll` fails, the helper rolls back and the status says `failed` (possibly "incomplete", B-5). Nothing is corrupted.
3. **Transaction semantics.**
   - **Hashing:** SHA-256 over the staged files before the wait. Each **destination** is re-hashed right after its rename (504, 623). If a staged file is swapped during the wait, the swap is caught after promotion and rolled back, so there is no hash bypass via TOCTOU.
   - **Backups:** `.previous` files with fixed names, made for **all** destinations before any promotion (465-489). Each run overwrites the older backup. A full disk fails before promotion.
   - **Promotion:** a `MoveFileExW` rename, atomic on NTFS and a `rename(2)` under Wine (see B-10 for cross-volume).
   - **Rollback when payload 2 of 3 fails:** payload 1 is restored from its fresh backup with CopyFileW, payload 2 was never changed, and payload 3 is rewritten with identical bytes (B-5).
   - **Crash at each step:** see §3.
   - **Read-only and locked destinations:** attributes are reset with `SetFileAttributesW NORMAL` before each attempt, which handles read-only files. A locked destination retries for 30 s, then rolls back.
   - **Missing `scripts\`:** created at 468 and not removed on rollback (harmless).
   - **Cleanup:** staged files are consumed on success and orphaned on every failure path (Low).
4. **Status and log files.**
   - Writers:
     - The DLL writes `state=staged` via ofstream (1182-1188, 1381-1388).
     - OpenShim's updater writes `staged` or `failed`.
     - The helper writes `waiting_for_exit`, `complete`, `failed` or `already_staged` (CREATE_ALWAYS, not atomic).
   - Readers: only CR reads `openshim_update.status` (OpenShimInstaller.lua:499-511, tolerates CRLF). Nothing reads `winmm_update.status`.
   - All of these files live in the game root, so any script can pre-seed or forge them with `bzfile.Open`. A forged pending state triggers B-3.
   - The `helperLogPath` returned to Lua is the same `logPath` variable passed to the helper (`logs\...`, or the game root if `logs` cannot be created), so the two match. The README says something different (§9).
   - The logs contain full absolute paths, which can include the Windows user name.
5. **Privilege.**
   - Both the helper and the game are asInvoker (verified), so there is no UAC virtualization and no elevation request.
   - In a non-writable `Program Files (x86)` install, the DLL fails **early and clearly**: its first write is the staging copy into the bzfile directory, which on GOG is under the game root. The helper never runs.
   - Proton: `CreateProcessW` runs in the same prefix and wineserver, and a GUI-subsystem child creates no window.
   - Not verified: whether Proton or pressure-vessel kills a helper that is still waiting when Steam marks the game as stopped.
6. **Code quality.**
   - Win32 return values are checked, except the second `WideCharToMultiByte` and `WriteFile` in the logging helpers (best effort, acceptable).
   - `GetLastError` is captured in the right order everywhere except 312 (B-15).
   - `exists` uses the throwing overload (B-12).
   - The helper is wide-char throughout. RAII covers every handle, and `LocalFree` is used correctly.
   - Duplication:
     - The CryptoAPI hash is implemented twice (helper 15-25/174-237, DLL 857-933).
     - `LaunchHiddenProcess` and `QuoteCommandLineArgument` are copied into OpenShim.
     - The hardened single path re-implements a one-payload suite with weaker rollback (B-9).
   - `hashError` is never logged in the suite path (449-453, 503-506).
   - The version resource is consistent with `bzfile_version.h`, and CI checks both PE versions.
7. **Tests:** see B-16. CI runs the test on every PR and cleans up in `finally`.
8. **Cross-repo contract.**
   - **OpenShim → helper:** OpenShim's `StageSuite` (`openshim_updater.cpp:563-638`) passes exactly the 17-argument layout the helper expects (399-421). The staged names, destinations, `.previous` backups, `logs\openshim_update.log`, `openshim_update.status` and the mutex name all match. Its manifest hashes are lowercased (`openshim_update_manifest.cpp:31`), which matches the helper's lowercase comparison.
   - **Exit codes:** nobody reads them. Both launchers close the process handle immediately, so the status file is the only channel back.
   - **CR:** parses every state the helper writes. `matchesBundled` compares `expected_sha256` with `manifest.sha256`. CR's manifest check forces that to equal `winmm.sha256` (OpenShimInstaller.lua:424), and that is exactly what the helper writes (`suiteHash = payloads.front()`, 422). The contract holds, but only implicitly (§9).
   - **Version skew:** the DLL never checks the helper's version or hash (B-3, B-7). A helper from before the suite path, given `--suite`, exits 2 silently.

## 3. Transaction state table (suite path)

| Step | Where | Files touched | State if the helper/process dies here | Status file says | Recoverable? |
|---|---|---|---|---|---|
| 0 | DLL 1286-1365 | hash 3 sources; copy them to `<item>\openshim_suite_N.pending.<h12>` | nothing installed; on a copy error, partial staged files are removed | unchanged (previous value) | yes |
| 1 | DLL 1381-1388 | write `state=staged` | nothing installed | `staged` | yes, but a failed launch (1391) leaves the status at `staged`, so **CR is stuck** (B-3) |
| 2 | helper 403-406 | none; argument mismatch exits 2 | nothing installed | `staged` | **CR stuck** (B-3) |
| 3 | helper 424-436 | mutex | the losing helper exits 0 | `already_staged` with the loser's hash (B-4) | yes, once the winning helper finishes |
| 4 | helper 439-456 | read staged files | hash mismatch or missing file | `failed` | yes (CR: UPDATE_FAILED, re-stages) |
| 5 | helper 458-463 | none; wait of up to 10 min | killed while waiting, or timeout | `waiting_for_exit` / `failed` | killed: **CR stuck**; timeout: yes (B-2) |
| 6 | helper 465-489 | `create_directories`; CopyFileW dest to `.previous`, x3 | originals intact; some fresh backups | `waiting_for_exit` | originals intact; **CR stuck** |
| 7 | helper 494 (payload 1) | rename staged1 to `winmm.dll` | new winmm with old net.ini/patches.json; all backups present | `waiting_for_exit` | manually (from `.previous`); CR stuck; OpenShim's own updater would re-stage |
| 8 | helper 504 | hash `winmm.dll` | same as 7 | `waiting_for_exit` | same as 7 |
| 9 | helper 494/504 (payload 2) | rename to `net.ini`, then hash | 2 of 3 new | `waiting_for_exit` | same as 7 |
| 10 | helper 494/504 (payload 3) | rename to `scripts\patches.json`, then hash | all 3 new but not acknowledged | `waiting_for_exit` | CR sees a stale pending state and reports RESTART_REQUIRED forever, although the install is complete |
| 11 | helper 514 | write status | complete | `complete` + winmm hash | CR acknowledges and deletes the status (OpenShimInstaller.lua:853-864) |
| R | helper 496/507 → 370-397 | CopyFileW `.previous` to each destination that existed; remove the ones that did not | a crash mid-rollback leaves a mix | `waiting_for_exit` until 497/508 | manually; the status can say "incomplete" because of untouched files (B-5) |

Between steps 5 and 11 there is no journal. The only record is `waiting_for_exit` plus the `.previous` files, and nothing reconciles them on the next launch. Everything needed for a safe rollback is on disk before the first rename.

## 4. Dead / unreferenced code

Grep used: `grep -c "\b<name>\b"` for every function and struct in the helper and in the staging helpers of LuaExport.cpp. Every definition has at least one caller (e.g. `RollBackSuite` 3, `PromoteReplacement` 3, `IsX86PortableExecutable` 3, `NarrowSystemError` 2). No dead functions.

What remains:
- **Diagnostic-only branch:** the pid==0 case in `WaitForProcessExit` (264-268). Only the test reaches it (B-11).
- **Public APIs with no consumer** in OpenShim, EXU or CR (grep of the whole `Documents\GIT` tree): `ReplaceFileOnExit`, `StageOpenShimUpdate`, `SetAllowWinmmOverwrite`, `GetAllowWinmmOverwrite`. The helper's legacy 5-arg and hardened 8-arg modes exist only to serve them. Workshop mods outside these repos cannot be checked.
- **Unread file:** the DLL and helper write `winmm_update.status`, and nothing reads it.
- **Unused value:** `hashError` in the suite path (449, 503) is filled but never logged.
- **Cross-repo:** CR `OpenShimInstaller.lua:218-228` `GetOpenShimReplaceLogPath` has no caller.

Count: 0 dead functions, 1 diagnostic branch, 4 unconsumed exports, 1 unread status file, 1 unused value.

## 5. Raw literal census

| Literal | Where | Notes |
|---|---|---|
| `Local\BZR_OpenShim_Update` | helper 425, 553; OpenShim updater 577 | 3 copies across 2 repos; no shared header |
| `3686673790` | LuaExport 1116, 1226; CR OpenShimInstaller.lua:15; OpenShim `kWorkshopItemId` | the gate only checks the name (B-8) |
| `bzfile_replace_helper.exe` | LuaExport 812, 1150, 1334; OpenShim updater 568 | launched unverified (B-7) |
| `winmm.dll`, `net.ini`, `scripts\patches.json` + `.previous` | LuaExport 1147-1153, 1263-1282; OpenShim updater 667-673 | duplicated across repos; consistent today |
| `openshim_suite_N.pending.<hash12>`, `winmm.dll.pending.<hash12>`, `<dest>.pending` | LuaExport 1149, 1330-1331, 786-787; OpenShim 590-592 | 48-bit hash prefix; a collision does not matter because the helper re-hashes |
| `openshim_net.ini.payload`, `openshim_patches.json.payload` | LuaExport 1271, 1279 | names are not write-protected (B-8) |
| `winmm_update.status`, `openshim_update.status` | LuaExport 1153, 1336 | game root |
| `winmm_replace.log`, `openshim_update.log`, `<stem>_replace.log` | LuaExport 1151, 1335, 834 | `logs\`, or the game root if `logs` cannot be created |
| 10 min (`kMaxWaitMilliseconds`) | helper 290 | B-2 |
| 120 attempts × 250 ms = 30 s | helper 321-322 | per payload; worst-case suite about 90 s plus rollback |
| 1000 ms sleep after exit | helper 277, 299 | a guess at handle-release time; fine |
| 64 KiB hash buffer | helper 197; LuaExport 892 | fine |
| argument counts 17 / 8 / 5 | helper 403, 536-537 | this is the contract, and it is not versioned (B-3) |

## 6. Lifetime / ownership notes
- **Helper process:**
  - The named mutex is held from `CreateMutexW` until the helper exits (`ScopedHandle` destructor or process exit).
  - The target process handle is closed right after the wait.
  - Crypt handles, file handles and the `CommandLineToArgvW` block are released by RAII or freed immediately.
  - The helper inherits the game's CWD (`lpCurrentDirectory=nullptr`), so that directory stays pinned while it waits (harmless).
- **DLL:**
  - The thread and process handles from `CreateProcessW` are closed immediately (402-403). `bInheritHandles=FALSE`, so no game handles leak into the helper.
  - `g_AllowWinmmOverwrite` is a process-global static. Once one script sets it, it applies to every later script until bzfile.dll unloads.
  - Every staging API reads all of its Lua arguments before building C++ objects, and every rejection returns instead of raising, so no destructor is skipped. The exceptions are:
    - the `.string()` throws (B-13);
    - `luaL_error` in `GetFileHash` (1009), which fires before any object is constructed.
- **On disk:**
  - `.previous` backups persist indefinitely: one generation, overwritten on every update.
  - Orphaned `*.pending.*` files stay in the Workshop item folder after any failure.
  - The status file stays until CR acknowledges `complete`.

## 7. Performance notes
- All staging runs synchronously on the game thread: three SHA-256 passes and three copies totalling about 240 KB (winmm 125 KB, patches.json 103 KB, net.ini 13 KB on this machine). That takes a few ms, which is fine. CR calls `Inspect` up to three times per `Apply`, and each call hashes about 8 files; also small.
- The helper waits with `WaitForSingleObject`, not by polling. The retry loop only burns time on failure (up to 30 s per payload). `AppendLogLine` opens and closes the log per line, with fewer than 50 lines per run. Nothing to fix.

## 8. Patterns worth keeping
- The DLL fixes every destination, backup, status and log path in the staging APIs (1147-1153, 1259-1284). The script only chooses sources, and those must sit beside the loaded DLL under fixed names.
- MSVCRT quoting is correct, and the helper checks the exact argv count, so argument injection is not possible (Q1).
- The suite runs in three phases:
  1. Verify every staged hash **before** waiting.
  2. Back up **every** destination before the first rename.
  3. Rename and re-hash each **installed** file, rolling back on any failure.

  The post-install re-hash closes the staged-file TOCTOU after the fact. exp2 confirmed the happy path, and exp1 confirmed that a promoted payload is restored correctly.
- `MoveFileExW REPLACE_EXISTING` is an atomic rename on NTFS and a `rename(2)` under Wine. File attributes are reset to normal before rename, backup and restore.
- The mutex check reads `GetLastError()` immediately after `CreateMutexW`, with no call in between.
- The helper is wide-char end to end and uses `CommandLineToArgvW` instead of a hand-written parser.
- There is no elevation request (asInvoker verified), and on a non-writable install the DLL fails early and clearly.
- One version header drives both PE resources. CI enforces FileVersion and ProductVersion on both binaries, and that the tag matches the version (build.yml:22-65).
- The CI test drives the real binary through `ProcessStartInfo.ArgumentList` (exact argv, real exit code) and cleans up in `finally`.
- The DLL removes its staged copies on every early failure (1355-1359, 1393-1396).

## 9. Low-severity items
- **README drift:**
  - Line 166 says "force-copying"; the helper renames.
  - Line 170 says the log is "next to the target"; it is `logs\<stem>_replace.log`.
  - Line 194 says `winmm_replace.log` is "in the game root"; it is in `logs\`.
  - Line 192 says "atomically"; that holds only on one volume (B-10).
  - `Set/GetAllowWinmmOverwrite` are undocumented (B-1).
- **Suite identity in the status file:** the status is keyed only by the winmm hash (`suiteHash`, helper 422). A suite that changes only `net.ini` or `patches.json` has the same `expected_sha256`, so a stale status from the previous suite is attributed to the new one. Write a suite digest (a hash of the three hashes) and `payload_count`.
- **Status writes are not atomic:** `WriteStatus` uses CREATE_ALWAYS in place, so a crash mid-write leaves a truncated file. Write to `.tmp`, then `MoveFileEx`.
- **Logs leak paths:** they contain full absolute staged and destination paths, including the user name when the library is under the profile. CR sanitizes its own report but not the helper's log.
- **Legacy mode (`ReplaceFileOnExit`) writes no status:** Lua's `true` only means "launched". Two calls for the same destination race (Q2).
- **No `longPathAware` manifest:** paths longer than MAX_PATH fail in the helper's Win32 calls.
- **Legacy CryptoAPI:** `CryptAcquireContextW PROV_RSA_AES` works on Windows and Wine, but `BCryptHash` is the supported API. Deduplicate the two copies at the same time.
- **Duplicated single-file path:** the hardened single path duplicates the suite logic with weaker rollback. Implement it as a one-payload suite.
- **Orphaned staged files:** `*.pending.*` files are never cleaned up after B-4, B-5, timeouts or failed hashes. They sit in a Steam-managed Workshop folder.
- **Missing include:** `IsSha256` uses `std::isxdigit` without `<cctype>`; it only compiles because the header comes in transitively.

## 10. Notes for other worksheets
- **A (Lua bindings / path policy):**
  - **Protected-name list is too short.** `IsWriteProtected` (339-353) protects only two leaf names. It misses `bzloader.dll`, `plugins\openshim.dll`, `scripts\patches.json`, `net.ini`, `openshim.ini`, `bzfile_replace_helper.exe`, `*.pending*`, the status files and `battlezone98redux.exe`. `SetAllowWinmmOverwrite` turns it off for every API (B-1).
  - **Possible name-check bypass.** Please check whether the leaf-name compare can be sidestepped with `winmm.dll.` (trailing dot) or `winmm.dll::$DATA` when `weakly_canonical` cannot canonicalize the path because the file does not exist. Not verified here.
  - **Possible code-execution route.** Please confirm whether any `package.cpath` directory lies inside an allowed root. If one does, `Open(x.dll,"wb")` followed by `require` already turns Lua into native code execution, which calibrates B-1, B-7 and B-8.
  - **`.string()` throw sites** in LuaExport.cpp: 245, 651, 657, 722, 760, 782, 818, 1024, 1201, 1404.
- **C (scripts / CI / build / hygiene):**
  - **Redeploy refuses a genuine helper.** `scripts/deploy_linux_proton.sh:79-82` (`is_bzfile_helper`) identifies the helper by grepping for the ASCII text "bzfile replace helper". The real helper contains no such ASCII string: `grep -a -c` on `Release\bzfile_replace_helper.exe` returns 0, because all its strings are UTF-16. `tests/linux/run.sh:203` writes an ASCII stub, so the Linux test cannot catch this.
  - **Wrong deploy location.** The same script deploys beside bzfile.dll in the game directory. `StageOpenShim*` always refuse there, because they require the module directory to be named `3686673790`.
  - **Test gaps.** `Test-ReplaceHelper.ps1` depends on `RUNNER_TEMP` (B-16). The Linux lane never runs the helper under Wine.
- **Cross-repo (for the rollup; nothing was edited):**
  - **CR:** `OpenShimInstaller.lua:703-710` should demote pending states when the update mutex is absent (B-3). `GetOpenShimReplaceLogPath` (218) is unused. These CR reads come from the `Documents\GIT` copy; re-check them against the canonical CR tree that AGENTS.md names.
  - **OpenShim:** `openshim_updater.cpp:514-517` has the same problem as B-6. Its pre-stage `OpenMutexW` check (576-584) is the pattern bzfile's DLL should copy (B-4).

### Not verified
- Breakaway behaviour under Steam, GOG Galaxy and Wine job objects (B-6).
- Whether Proton kills a helper that is still waiting.
- Wine's `OpenProcess` error code for a dead PID.
- Which MSVC `exists` errors throw (B-12).
- Whether `path::string()` throws on unmappable characters (B-13).
- `MoveFileEx` cross-volume failure semantics (B-10).
- The rollback path after a post-install hash mismatch.

Nothing was built, and nothing was run in-game.
