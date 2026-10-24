# bzfile Lua API reference

This describes the **v1.1.0 development source on `main` as of October 24, 2026**, exported by `src/LuaExport.cpp`, not necessarily the latest published DLL. At that date, the [latest tagged release](https://github.com/GrizzlyOne95/bzfile/releases/latest) was v1.0.0 and predates important write-protection changes and `StageOpenShimSuiteUpdateV3`.

The runtime is Battlezone 98 Redux's **Lua 5.1**. Make the DLL visible through the game's Lua module search path (`package.cpath` or the appropriate require fix), then use `local bzfile = require("bzfile")`.

## Errors and path rules

All path arguments are confined to the directory containing the game executable or, if detected, Steam's Workshop content root (`steamapps/workshop/content/301650`). Relative paths resolve against the **game directory**, not the process's mutable current working directory. Path comparisons resolve links and prevent traversing out of allowed roots.

Functions that normally return a handle/string may return `nil, errorMessage`; functions that normally return success/boolean can return `false, errorMessage`. Lua argument type/count errors can still raise. A method invoked on a closed file handle raises `bzfile Error: file is not open`.

```lua
local file, err = bzfile.Open("addon/savedata.txt", "w", "app")
if not file then
    print("Could not open save:", err)
    return
end
file:Writeln("Saved")
file:Close()
```

### File handles

`bzfile.Open(filePath: string, openMode: string?, writeParameter: string?) -> handle? , errorMessage?`

Opens a file, creating it if write mode is requested and the file is absent. Valid common modes are `"r"` (default), `"w"`, `"rb"`, and `"wb"`. The `b` flag selects binary mode; text mode uses Windows text line-ending translation. For a write mode, `writeParameter` is `"app"` (default: append) or `"trunc"` (clear contents first). Writes to protected paths fail. Other invalid modes/options return `nil, errorMessage`.

| Method | Return | Behavior |
| --- | --- | --- |
| `file:Write(content: string)` | `self` | Write bytes without adding a newline. |
| `file:Writeln(content: string)` | `self` | Write content followed by a newline. |
| `file:Read(count: int?)` | `string?` | Read up to `count` bytes/characters from the current position (default 1). Returns `nil` at EOF. `0` yields `""`; negative counts or counts over 64 MiB raise an argument error. |
| `file:Readln()` | `string?` | Read the next line; `nil` at EOF. |
| `file:Dump()` | `string? , errorMessage?` | Seek to the beginning and read the **entire** file, regardless of prior `Read`/`Readln` calls. Over 64 MiB returns `nil, errorMessage`; a write-only handle returns `""`. Text mode translates line endings, so use `rb` for exact bytes. |
| `file:Flush()` | `self` | Flush buffered writes. May reduce performance if called excessively. |
| `file:Close()` | `nil` | Close the handle. The Lua variable is not automatically set to `nil`; further methods raise. |

Example reading one line at a time:

```lua
local file, err = bzfile.Open("addon/savedata.txt", "r")
if not file then error(err) end
while true do
    local line = file:Readln()
    if line == nil then break end
    print(line)
end
-- Dump() seeks back to the beginning, if you also need the full file.
local allText = file:Dump()
file:Close()
```

## Filesystem functions

### Paths and directories

`bzfile.GetWorkingDirectory() -> path: string? , errorMessage?`

Returns the directory containing the game executable (e.g. `...\steamapps\common\Battlezone 98 Redux`). It can fail if the path cannot be represented in the system code page.

`bzfile.GetWorkshopDirectory() -> path: string? , errorMessage?`

Finds `...\steamapps\workshop\content\301650` from a Steam library installation. Returns `""` outside Steam (for example GOG), where no Workshop root is allowed. Unrepresentable paths return `nil, errorMessage`.

`bzfile.MakeDirectory(path: string) -> success: boolean , errorMessage?`

Creates a directory and any missing parents, within an allowed root and outside write-protected names.

`bzfile.Exists(path: string) -> exists: boolean , errorMessage?`

Returns whether a file or directory exists. A rejected path can return `false` with an explanation.

`bzfile.ListDirectory(path: string) -> names: {string}? , errorMessage?`

Returns names of **direct children**, not a recursive listing.

### Copy and delete

`bzfile.CopyFile(sourcePath: string, destinationPath: string, overwriteExisting: boolean?) -> success: boolean , errorMessage?`

Copies raw bytes. Both source and destination must be inside the allowed roots, and the destination must not be write-protected. Without `overwriteExisting`, an existing destination is refused. The function also rejects copying a file to itself. It copies to a temporary file **beside the destination** and then renames into place so a failed or incomplete copy does not discard the existing destination.

`bzfile.Delete(path: string) -> success: boolean , errorMessage?`

Deletes a file or **recursively deletes a directory**. It refuses the game/Workshop root or an ancestor, protected file targets, and any directory containing a protected file. It is not a recycle-bin operation: validate path selection and scope before calling it, especially when paths come from user or mod data.

## Validation functions

`bzfile.GetFileHash(path: string, algorithm: string?) -> hash: string? , errorMessage?`

Returns a lowercase hexadecimal SHA-256 hash (default and currently only supported algorithm). Unsupported algorithm names raise a Lua error; filesystem and hashing failures return `nil, errorMessage`.

`bzfile.GetFileVersion(path: string) -> version: string? , errorMessage?`

Reads the fixed Windows PE file version, e.g. `1.1.0.0`. Returns `nil, errorMessage` when readable version metadata is unavailable.

## Write protection (current `main`)

All normal mutating APIs share `src/bzfile_write_policy.h`:

- Native code: any path whose filename ends in `.dll`, `.exe`, or `.asi`.
- OpenShim suite configuration: `net.ini`, `patches.json`, `openshim_net.ini.payload`, `openshim_patches.json.payload`.
- Update transaction files: names containing `.pending` or ending in `.previous`.

Names are checked case-insensitively, normalizing trailing spaces/dots and `:stream` suffixes. `openshim.ini` and `*_update.status` remain writable. Scripts cannot disable the policy through the current deprecated compatibility exports.

**Version warning:** In published **v1.0.0**, write protection only explicitly covered `winmm.dll` and `bzfile.dll`, and `SetAllowWinmmOverwrite(true)` still disabled those checks. Do not rely on the expanded rules until a build containing the v1.1.0 changes is qualified and shipped.

### Retained compatibility exports (main)

```lua
bzfile.ReplaceFileOnExit(sourcePath, destinationPath) -> false, errorMessage
bzfile.SetAllowWinmmOverwrite(allow: boolean) -> false, errorMessage
bzfile.GetAllowWinmmOverwrite() -> false
```

`ReplaceFileOnExit` no longer accepts an arbitrary replacement destination. `SetAllowWinmmOverwrite` no longer disables write protection. These names remain callable so old callers receive an explicit failure instead of a missing-function error. Their behavior **differs from older v1.0.0**.

## OpenShim update APIs

The update functions are deliberately **not** generic file replacement APIs. They require fixed Workshop item 3686673790 placement, hashes, source names, and destination paths. See [UPDATER.md](UPDATER.md) for all three signatures, live-helper behavior, transaction states, safety boundaries, and release availability.

```lua
bzfile.IsOpenShimUpdateActive() -> active: boolean
```

Returns whether the update helper currently owns a staged transaction. A stale status file can survive a failed process and does not itself mean the helper is still active.
