# bzfile
File IO Library for Battlezone 98 Redux

## High-Level Summary

- Lightweight Lua-facing file I/O for Battlezone 98 Redux mods and addon tools.
- Covers the core text-file workflow: open, read, write, flush, close, working
  directory discovery, workshop directory discovery, directory creation, and
  existence checks.
- Current repo-side improvements focus on safer update and deployment workflows:
  guarded copy operations, a deferred replace-on-exit path for files that may be
  locked while the game is running, and stricter resolution of allowed game and
  workshop write roots.
- File hashing support is included for patch/update verification, and the
  bundled Lua library snapshot has been refreshed to stay aligned with the
  repaired runtime used by the surrounding Battlezone tooling stack.

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
local contents = file:Readln()
while contents ~= nil do
    print(contents)
    contents = file:Readln()
end

-- Or dump the whole contents of the file into a lua stirng:
local bigString = file:Dump()
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
Unformatted read of count characters, defaults to 1 if not specified.

```lua
file:Readln() -> content: string
```
Reads a line.

```lua
file:Dump() -> content: string
```
Dumps the entire contents of the file into one string

```lua
file:Flush() -> self
```
Flushes the input/output buffer, makes text appear immediately in the file, may affect performance if called frequently.

```lua
file:Close() -> nil
```
Closes the handle to the file, file object becomes nil.

### Filesystem Functions

```lua
bzfile.GetWorkingDirectory() -> path: string
```
Gets the root directory of the game (..\common\Battlezone98Redux\).

```lua
bzfile.GetWorkshopDirectory() -> path: string
```
Gets the workshop directory (..\content\301650).

```lua
bzfile.MakeDirectory(path: string) -> nil
```
Makes a new directory at the given path.

```lua
bzfile.Exists(path: string) -> exists: boolean
```
Checks whether a file or directory exists.

```lua
bzfile.CopyFile(sourcePath: string, destinationPath: string, overwriteExisting: boolean?) -> success: boolean, errorMessage?: string
```
Copies a file as raw bytes. The destination must still be inside the game root or workshop root.

```lua
bzfile.ReplaceFileOnExit(sourcePath: string, destinationPath: string) -> success: boolean, errorMessage?: string
```
Stages a replacement file under the allowed write roots, then launches a hidden
helper that waits for the current game process to exit before force-copying the
staged file onto the destination. This is useful for replacing loaded DLLs such
as `winmm.dll` that cannot always be overwritten in-place while Battlezone is
still running.

```lua
bzfile.GetFileHash(path: string, algorithm: string?) -> hash: string, errorMessage?: string
```
Returns a lowercase hex file hash. Currently `sha256` is supported.
