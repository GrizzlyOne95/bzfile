# bzfile

**Lua file I/O and constrained update staging for Battlezone 98 Redux.** `bzfile.dll` gives mods access to text/binary files, directories, hashes, and version information without granting unrestricted access to the host filesystem. A separate helper can install a verified OpenShim update after Redux exits.

> **Release vs. development (October 24, 2026):** The latest [published release](https://github.com/GrizzlyOne95/bzfile/releases/latest) is **v1.0.0**; `main` identifies as **v1.1.0** and is not yet a tagged release. This README and the linked API reference describe the **v1.1.0 development source**. In particular, v1.0.0 **does not** include the expanded native-file write protections, disabled overwrite toggle, or five-file `StageOpenShimSuiteUpdateV3` API added later. Do not assume those guarantees apply to downloaded v1.0.0 binaries.

## Quick start

The module must be in Redux's Lua DLL lookup path (for example through the Campaign Reimagined bundle, or an appropriate `package.cpath` / require-fix configuration). It is a **32-bit Windows Lua 5.1 C module**, including when used under Proton/Wine.

```lua
local bzfile = require("bzfile")
local path = bzfile.GetWorkingDirectory() .. "\\addon\\myfile.txt"

local output, err = bzfile.Open(path, "w", "app")
if not output then error(err) end
output:Writeln("Hello world!")
output:Close()

local input, readErr = bzfile.Open(path, "r")
if not input then error(readErr) end
print(input:Dump())
input:Close()
```

`Open(path)` defaults to reading; `Open(path, "w")` appends by default, while `Open(path, "w", "trunc")` clears the existing file. Use `rb`/`wb` for exact bytes rather than Windows text-mode newline conversion. See the [complete Lua API reference](Docs/API.md) for modes, errors, limits, and examples.

### Available API groups

| Area | Main functions |
| --- | --- |
| File handles | `Open`; `Write`, `Writeln`, `Read`, `Readln`, `Dump`, `Flush`, `Close` |
| Filesystem | `GetWorkingDirectory`, `GetWorkshopDirectory`, `MakeDirectory`, `Exists`, `CopyFile`, `Delete`, `ListDirectory` |
| Validation | `GetFileHash` (SHA-256), `GetFileVersion` (PE file version) |
| Verified updates | `StageOpenShimUpdate`, `StageOpenShimSuiteUpdate`, `StageOpenShimSuiteUpdateV3`, `IsOpenShimUpdateActive` |

Deprecated compatibility exports `ReplaceFileOnExit` and `SetAllowWinmmOverwrite` remain callable on current `main` but cannot perform the former unsafe operations. The complete [update staging guide](Docs/UPDATER.md) distinguishes the legacy and split-loader APIs.

## Install and compatibility

| Host | Distribution | Runtime |
| --- | --- | --- |
| Windows | Steam or GOG | Win32 `bzfile.dll` with `bzfile_replace_helper.exe` beside it if updates are needed |
| Linux | Steam | Same Win32 binaries under Proton (native Steam, Flatpak, or Snap) |
| Linux | GOG | Same Win32 binaries under a compatible Wine/Proton prefix |

Windows users can obtain the two binaries from [Releases](https://github.com/GrizzlyOne95/bzfile/releases/latest) or a matched Campaign Reimagined package. Place `bzfile.dll` in the mod's Lua module search path; keep the replacement helper **beside the DLL** when using the updater. There are no special Steam launch options for bzfile itself.

On Linux, run the installer in a **host terminal**. It downloads the latest tagged release, **not** development `main` binaries:

Native Steam or Flatpak:

```bash
curl -fsSL https://raw.githubusercontent.com/GrizzlyOne95/bzfile/main/scripts/install_linux.sh | bash -s -- --native
```

Snap Steam:

```bash
curl -fsSL https://raw.githubusercontent.com/GrizzlyOne95/bzfile/main/scripts/install_linux.sh | bash -s -- --snap
```

GOG/Wine or another custom game folder:

```bash
curl -fsSL https://raw.githubusercontent.com/GrizzlyOne95/bzfile/main/scripts/install_linux.sh | bash -s -- --game-path "/path/to/Battlezone 98 Redux"
```

The game directory can contain `battlezone98redux.exe` **or** `BZR.exe`. Re-running the installer replaces the matched DLL/helper pair and keeps the three newest installer backups per file. Add `--uninstall` to remove bzfile-owned files/backups. To deploy locally compiled Windows binaries to a detected Proton install, use `./scripts/deploy_linux_proton.sh` from a checkout.

Linux does not build a native `.so`, and no DLL override or launch option is required for bzfile itself. See the [four-lane compatibility policy](Docs/BZR_PLATFORM_COMPATIBILITY.md); support policy does not substitute for live Steam/GOG/Proton/Wine qualification.

## Filesystem protection (current `main`)

- Ordinary file APIs resolve relative paths beneath the **game directory** and restrict access to that directory or the detected Steam Workshop content root. They do not offer unrestricted host filesystem access.
- Writes, copies, directory creation, and recursive deletion refuse protected native filenames/extensions (`.dll`, `.exe`, `.asi`), suite configuration such as `net.ini` and `patches.json`, and update transaction files (`.pending`, `.previous`). Names are normalized to account for Win32 case, trailing dots/spaces, and alternate-stream spellings.
- `Delete` refuses the game/Workshop roots and their ancestors, or a directory containing a protected file. **It can still recursively delete permitted mod/data directories**, so scripts must validate their targets.
- Normal Lua code cannot disable these protections on `main`. OpenShim's special updater uses fixed destinations, verified hashes, and a helper launched after game exit—not arbitrary writes through the generic API.

These hardening guarantees were introduced **after v1.0.0**. See the [API safety details](Docs/API.md), [updater contracts](Docs/UPDATER.md), and [write-protection implementation](src/bzfile_write_policy.h).

## Build, test, and release

- **Windows:** Visual Studio with C++ support; open `bzfile.sln` and build **Release | x86**. Both `bzfile.dll` and `bzfile_replace_helper.exe` are Win32 PE files. The module links the BZR-specific Lua 5.1 library described in [`lib/README.md`](lib/README.md).
- **Windows CI:** builds both binaries, checks embedded versions, and runs Lua-host and replacement-helper transaction tests, including the current split-suite path.
- **Linux host checks:** `bash tests/linux/run.sh` validates scripts and host-side policy. It neither builds nor executes the Win32 game module.
- **Releases:** version tags (`v*`) package the two binaries with SHA-256 checksums. The Linux installer accepts versioned `bzfile-v*.zip` assets, not the obsolete mutable `latest`-tag build.

Project-owned code is [MIT-licensed](LICENSE). Battlezone 98 Redux and any third-party assets retain their own rights.

### AI-assisted development

Maintainer-directed development uses AI tools to assist with code, review, tests, and documentation. Their output is treated as a starting point; API behavior and safety claims are checked against implementation and reproducible tests.
