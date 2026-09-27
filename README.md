# bzfile
File IO Library for Battlezone 98 Redux

Windows/GOG, Windows/Steam, Linux/Steam via Proton, and Linux/GOG through a
compatible Wine/Proton prefix are maintained together. See the shared
[`BZR platform and distribution compatibility policy`](Docs/BZR_PLATFORM_COMPATIBILITY.md).

## Installation (Linux / Proton)

The shipped `bzfile.dll` is a **Win32** Lua C module. Linux hosts deploy that same DLL into a Proton game folder; there is no native Linux `.so`.

Native Steam or Flatpak — paste in a terminal:

```bash
curl -fsSL https://raw.githubusercontent.com/GrizzlyOne95/bzfile/main/scripts/install_linux.sh | bash -s -- --native
```

Snap Steam — paste in a terminal:

```bash
curl -fsSL https://raw.githubusercontent.com/GrizzlyOne95/bzfile/main/scripts/install_linux.sh | bash -s -- --snap
```

No Steam launch options are required. Proton loads `bzfile.dll` as a Windows DLL (this is not an OpenShim `winmm.dll` proxy).

GOG under Wine (or any install the scripts do not find), point at the game
folder, which must contain `battlezone98redux.exe`:

```bash
curl -fsSL https://raw.githubusercontent.com/GrizzlyOne95/bzfile/main/scripts/install_linux.sh | bash -s -- --game-path "$HOME/.wine/drive_c/GOG Games/Battlezone 98 Redux"
```

Re-running the installer upgrades in place and keeps the three most recent
backups. To remove bzfile again (only files that identify as bzfile's own),
add `--uninstall` to the same command.

To copy a local Windows build instead of a GitHub release:

```bash
./scripts/deploy_linux_proton.sh
```

Windows still builds `bzfile.sln` as **Release | x86**. Host-side Linux checks are `bash tests/linux/run.sh`.

## High-Level Summary

- Lightweight Lua-facing file I/O for Battlezone 98 Redux mods and addon tools.
- Covers the core text-file workflow: open, read, write, flush, close, working
  directory discovery, workshop directory discovery, directory creation, and
  existence checks.
- All paths are confined to the game folder and, on Steam, the Workshop
  content folder. Native code and update files are write-protected (see
  [Write protection](#write-protection)).
- File hashing and PE version reading support update verification, and a
  constrained, hash-verified path stages OpenShim updates that a helper
  installs once the game exits.
- bzfile links its own copy of the game's Lua core and refuses to load on a
  game build it does not match (see `lib/README.md`).

Quick and dirty tutorial:

```lua
-- Make sure to use require fix or manually set your package.cpath in order for the game
-- to load dlls
local bzfile = require("bzfile")

local filePath = bzfile.GetWorkingDirectory() .. "\\addon\\myfile.txt"

local file = bzfile.Open(filePath, "w", "app")

file:Writeln("Hello world!")

-- Optionally if you want the text to appear right away...
-- file:Flush()

-- Once you are done you can optionally close the file manually:
file:Close()

local readFile = bzfile.Open(filePath, "r")

-- Read line by line
local contents = readFile:Readln()
while contents ~= nil do
    print(contents)
    contents = readFile:Readln()
end

-- Or dump the whole contents of the file into a Lua string:
local bigString = readFile:Dump()
readFile:Close()
```

Specification:

```lua
bzfile.Open(filePath: string, openMode: string?, writeParameter: string?) -> handle
```
Opens a handle to a file, creating a new one if write mode is specified and the file does not exist.

Valid options for open mode are:
- "r": read mode
- "w": write mode

Valid options for write parameters are:
- "app": append to file
- "trunc": truncate file (clears the file before writing)

Default options if only path is specified is "r" (read). In "w" (write) mode the default option is "app" (append).
Add "b" to the mode ("rb", "wb") for binary I/O; text mode translates CRLF line endings.

On failure `Open` returns `nil, errorMessage` instead of a handle, for example
when the path is outside the allowed roots or write-protected (see
[Write protection](#write-protection)).

### File Methods:

```lua
file:Write(content: string) -> self (reference to the handle to allow method chaining)
```
Unformatted write with no newline.

```lua
file:Writeln(content: string) -> self
```
Write with newline.

```lua
file:Read(count: int?) -> content: string
```
Unformatted read of up to count characters, defaults to 1 if not specified.
Returns `nil` at end of file. A count above 64 MiB raises an argument error.

```lua
file:Readln() -> content: string
```
Reads a line.

```lua
file:Dump() -> content: string
```
Dumps the entire contents of the file into one string. On a text-mode handle
line endings are translated, so open with "rb" to get the exact bytes. Files
over 64 MiB return `nil, errorMessage`; a write-only handle returns `""`.

```lua
file:Flush() -> self
```
Flushes the input/output buffer, makes text appear immediately in the file, may affect performance if called frequently.

```lua
file:Close() -> nil
```
Closes the handle. The variable still refers to the closed handle; further
method calls raise "file is not open".

### Filesystem Functions

```lua
bzfile.GetWorkingDirectory() -> path: string
```
Gets the game folder: the directory holding the game executable (for Steam,
`...\steamapps\common\Battlezone 98 Redux`). Relative paths passed to any
bzfile function are resolved from here. Returns `nil, errorMessage` if the
path cannot be represented in the system code page.

```lua
bzfile.GetWorkshopDirectory() -> path: string
```
Gets the Steam Workshop content directory (`...\steamapps\workshop\content\301650`).
Returns `""` when the game is not inside a Steam library (for example GOG); there
is then no Workshop write root.

```lua
bzfile.MakeDirectory(path: string) -> success: boolean, errorMessage?: string
```
Makes a new directory (and any missing parents) at the given path.

```lua
bzfile.Exists(path: string) -> exists: boolean
```
Checks whether a file or directory exists.

```lua
bzfile.CopyFile(sourcePath: string, destinationPath: string, overwriteExisting: boolean?) -> success: boolean, errorMessage?: string
```
Copies a file as raw bytes. Both paths must be inside the game root or Workshop
root, and the destination must not be write-protected. The copy is written
beside the destination and renamed into place, so an existing destination is
untouched unless a complete copy exists. Without `overwriteExisting` an
existing destination is refused. Copying a file onto itself is refused.

```lua
bzfile.Delete(path: string) -> success: boolean, errorMessage?: string
```
Deletes a file, or a directory recursively. Refused for the game or Workshop
root (or a directory containing one), for write-protected files, and for a
directory holding any write-protected file.

```lua
bzfile.ListDirectory(path: string) -> names: {string}?, errorMessage?: string
```
Returns the names of the entries directly inside a directory.

```lua
bzfile.ReplaceFileOnExit(sourcePath: string, destinationPath: string) -> false, errorMessage: string
```
Removed. It let a script choose any destination for a deferred replacement with
no hash or backup. Use `StageOpenShimUpdate` or `StageOpenShimSuiteUpdate`.
The function still exists and always returns `false` with an explanation.

```lua
bzfile.SetAllowWinmmOverwrite(allow: boolean) -> false, errorMessage: string
bzfile.GetAllowWinmmOverwrite() -> false
```
Retained for compatibility only. Write protection can no longer be disabled.

### Write protection

Every function that creates, overwrites, copies onto or deletes a path refuses
these names, anywhere in the allowed roots:

- native code: any `.dll`, `.exe` or `.asi` (this covers `winmm.dll`,
  `bzloader.dll`, `openshim.dll`, `bzfile.dll`, `bzfile_replace_helper.exe` and
  the game executable);
- native configuration replaced only by the verified suite update: `net.ini`,
  `patches.json`, `openshim_net.ini.payload`, `openshim_patches.json.payload`;
- update transaction files: any name containing `.pending`, or ending in
  `.previous`.

Names are compared the way Windows resolves them: case-insensitively, ignoring
trailing dots and spaces and any `:stream` suffix. `openshim.ini` and the
`*_update.status` files remain writable.

Argument type errors raise as usual in Lua. Internal failures (out of memory, a
path the system code page cannot represent, a file-system error) come back as
`nil, errorMessage` instead of escaping into the game.

```lua
bzfile.GetFileHash(path: string, algorithm: string?) -> hash: string, errorMessage?: string
```
Returns a lowercase hex file hash. Currently `sha256` is supported.

```lua
bzfile.GetFileVersion(path: string) -> version: string, errorMessage?: string
```
Returns the fixed Windows file version (for example `1.0.0.5`) from a PE file.

```lua
bzfile.StageOpenShimUpdate(sourcePath: string, expectedSha256: string)
    -> success: boolean, stateOrError: string, helperLogPath?: string
```
Validates and stages a constrained OpenShim update. Unlike the generic file APIs,
the script cannot choose the destination: it is always `winmm.dll` in the game
root. The source must be an x86 DLL named `winmm.dll` beside the loaded
`bzfile.dll`, and its SHA-256 must match `expectedSha256`. Both staging
functions work only when `bzfile.dll` is loaded from the Campaign Reimagined
Workshop item folder (`3686673790`). The hidden helper waits
for Battlezone to exit, backs up the previous shim, atomically promotes the
payload, verifies the installed hash, and rolls back on verification failure
(removing the new file instead when there was no previous shim).
Progress is written to `winmm_update.status` and details to
`winmm_replace.log` in the game root.

```lua
bzfile.StageOpenShimSuiteUpdate(
    winmmSource: string, winmmSha256: string,
    networkSource: string, networkSha256: string,
    patchesSource: string, patchesSha256: string)
    -> success: boolean, stateOrError: string, helperLogPath?: string
```
Stages the complete Campaign Reimagined native suite as one verified
transaction. Sources are restricted to `winmm.dll`,
`openshim_net.ini.payload`, and `openshim_patches.json.payload` beside the
loaded `bzfile.dll`; destinations are fixed to the game-root `winmm.dll`,
`net.ini`, and `scripts/patches.json`. The helper validates every staged hash
before game exit, backs up all existing destinations, promotes and verifies all
three payloads, and rolls the suite back if any promotion or verification
fails. Progress is written to `openshim_update.status` and details to
`openshim_update.log`.

Both staging functions refuse while an update helper is already running (see
`IsOpenShimUpdateActive`), and write `state=failed` if the helper cannot be
launched. The helper waits for the game to exit however long that takes,
re-checks every staged hash before promoting, installs each file by copying it
beside the destination and renaming it into place, and on failure rolls back
only the files it actually replaced. Status states are `staged`,
`waiting_for_exit`, `promoting`, `complete` and `failed`.

```lua
bzfile.IsOpenShimUpdateActive() -> active: boolean
```
True while an update helper owns a staged OpenShim update. A status file that
still reads `staged` or `waiting_for_exit` while this is false was left behind
by a helper that never finished (crash, kill, power loss); the update can be
staged again.

## Builds and releases

The game-facing binaries (`bzfile.dll`, `bzfile_replace_helper.exe`) are Win32
and are built with MSVC. Linux CI runs host-side path/script checks only; it
does not produce a MinGW DLL.

Every pull request and push to `main` builds the x86 DLL and replacement helper,
then exercises the helper's verified single-file and three-file transaction
paths against isolated temporary files. Successful CI runs publish short-lived
GitHub Actions artifacts for testing.

Permanent GitHub Releases are created only from version tags matching `v*`.
A tagged release contains both binaries plus SHA-256 checksums, and the Linux
installer only accepts that `bzfile-v*.zip` asset. An older "Latest Build"
release under a mutable `latest` tag predates the path hardening and is not a
supported distribution.
