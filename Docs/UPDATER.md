# Verified OpenShim update staging

`bzfile` contains deliberately constrained APIs that allow Campaign Reimagined to **stage specific, hash-verified OpenShim files** for replacement **after Battlezone 98 Redux exits**. They are not general-purpose native file writers. Normal Lua file APIs refuse critical DLLs and update transaction files.

This documents the **v1.1.0 development source on `main` as of October 24, 2026**. The [last published v1.0.0 release at that date](https://github.com/GrizzlyOne95/bzfile/releases/latest) predates the five-file `StageOpenShimSuiteUpdateV3` API and later write-protection hardening. Do not call V3 or rely on the newer guarantees with v1.0.0 DLLs.

## Common requirements

- The loaded `bzfile.dll` must reside directly in **Campaign Reimagined Workshop item `3686673790`**. Other placements, including a manually copied game-root DLL, cannot stage these special updates.
- Payload files must have the **exact expected names** and reside beside that loaded `bzfile.dll`—Lua does not choose the destinations.
- Each payload must match its caller-supplied **SHA-256**. Native DLL payloads must be 32-bit x86 PE DLLs.
- The matching `bzfile_replace_helper.exe` must reside beside `bzfile.dll`. The five-file V3 path additionally requires the helper's SHA-256 and opens it under a restrictive sharing lock until process creation.
- Staging refuses to start while an update helper is already active. The helper waits for the game process to exit, checks staged hashes again, and either promotes the complete permitted payload set or rolls back the files it replaced.

For an actual release, the expected hashes should come from a trusted, version-matched manifest. A hash provided by the same untrusted payload does not establish authenticity.

## Update API selection

| API | Files replaced | Use |
| --- | --- | --- |
| `StageOpenShimUpdate` | One file: `winmm.dll` | Legacy single-DLL installation. |
| `StageOpenShimSuiteUpdate` | Three files: `winmm.dll`, `net.ini`, `scripts/patches.json` | Legacy monolithic native suite compatibility. |
| `StageOpenShimSuiteUpdateV3` | Five files: those three plus `bzloader.dll`, `plugins/openshim.dll` | **Current split OpenShim load chain**; requires 11 arguments including helper hash. |

Use **V3** for the current three-DLL loader/runtime architecture. The old single-file/three-file entry points remain for older packages, not as equivalent install choices.

### `StageOpenShimUpdate` (legacy)

```lua
bzfile.StageOpenShimUpdate(
    sourcePath: string,
    expectedSha256: string)
    -> success: boolean, stateOrError: string, helperLogPath?: string
```

`sourcePath` must be `winmm.dll` beside the loaded `bzfile.dll`. The destination is always `<game>/winmm.dll`. A successful call returns `true, "staged", logPath`; it does **not** mean replacement has already completed.

Status: `<game>/winmm_update.status`. Log: ordinarily `<game>/logs/winmm_replace.log` (the game root is a fallback if the logs directory cannot be created).

### `StageOpenShimSuiteUpdate` (legacy three-file)

```lua
bzfile.StageOpenShimSuiteUpdate(
    winmmSource: string, winmmSha256: string,
    networkSource: string, networkSha256: string,
    patchesSource: string, patchesSha256: string)
    -> success: boolean, stateOrError: string, helperLogPath?: string
```

The source files beside the module must be named `winmm.dll`, `openshim_net.ini.payload`, and `openshim_patches.json.payload`. Fixed destinations are, respectively, `<game>/winmm.dll`, `<game>/net.ini`, and `<game>/scripts/patches.json`.

The helper verifies all staged hashes, backs up existing destinations, promotes the set after exit, checks the installed hashes, and rolls back changed files if promotion or verification fails.

### `StageOpenShimSuiteUpdateV3` (current split suite)

```lua
bzfile.StageOpenShimSuiteUpdateV3(
    winmmSource: string, winmmSha256: string,
    networkSource: string, networkSha256: string,
    patchesSource: string, patchesSha256: string,
    loaderSource: string, loaderSha256: string,
    pluginSource: string, pluginSha256: string,
    helperSha256: string)
    -> success: boolean, stateOrError: string, helperLogPath?: string
```

All **11 arguments** are required. In addition to the legacy three source names, `bzloader.dll` and `openshim.dll` must be beside the module. The additional fixed destinations are `<game>/bzloader.dll` and `<game>/plugins/openshim.dll`. The `helperSha256` verifies the helper executable itself and the helper is locked against replacement before being launched.

V3 uses a separate `--suite-v3` helper protocol. An old helper cannot successfully install only part of the new chain. The loader, plugin, proxy, `net.ini`, and patch table form one verified transaction.

## Status and lifecycle

The suite variants write `<game>/openshim_update.status` and ordinarily log details to `<game>/logs/openshim_update.log` (falling back to the game root if necessary). Relevant states are:

| State | Meaning |
| --- | --- |
| `staged` | Accepted payloads are staged and the helper launch was requested. |
| `waiting_for_exit` | Helper has verified the staged data and is waiting for Redux to close. |
| `promoting` | Game exited; replacement/verification is underway. |
| `complete` | Installed hashes verified. |
| `failed` | Validation, launch, promotion, or verification failed. Inspect the log and rollback status. |

```lua
bzfile.IsOpenShimUpdateActive() -> active: boolean
```

This checks whether a helper currently owns an update. A status file stuck at `staged` or `waiting_for_exit` while this returns `false` is **stale**, for example after a crash, kill, or power failure; staging can be retried. A fresh game launch should use a complete, version-matched set—never mix a DLL, configuration, or patch table from unrelated commits/tags.

## Removed unsafe paths

```lua
bzfile.ReplaceFileOnExit(sourcePath, destinationPath) -> false, errorMessage
bzfile.SetAllowWinmmOverwrite(allow: boolean) -> false, errorMessage
bzfile.GetAllowWinmmOverwrite() -> false
```

On current `main`, generic deferred replacement and the write-protection toggle have been disabled. Their exports remain for compatibility diagnostics only. **Older released v1.0.0 behaves differently:** do not treat these functions as harmless on that version.

## Validation boundaries

Automated tests cover the Lua API and helper's single-file, legacy three-file, and V3 five-file transaction paths using isolated test directories. Source-level test success is not equivalent to release-level qualification across Windows Steam/GOG and Proton/Wine. A new tagged release should verify the exact frozen DLL/helper pair and representative in-game update lifecycle before publication.
