# bzfile

This repo is part of the local Battlezone workspace opened via
`%USERPROFILE%\Documents\Battlezone98Redux_Shim.code-workspace`.

## Workspace Layout
- Sibling repos normally live under `%USERPROFILE%\Documents\GIT\...`.
- The primary local game install is typically `%USERPROFILE%\Documents\Battlezone 98 Redux`.
- Prefer the workspace file and these conventions over hardcoded profile-specific paths.

## Local Role
- Lua-accessible file I/O library for Battlezone 98 Redux.

## BZR Bundle Repository Map
- Treat the following four repositories as the core **BZR bundle**. They are separate repositories with separate ownership boundaries, but they are expected to be available together for cross-reference during Battlezone 98 Redux work.
- On the primary development PC, look for local sibling checkouts under `%USERPROFILE%\Documents\GIT` before searching GitHub. Prefer local source for fast code/reference lookup when the checkout is present and current; fall back to GitHub when a sibling repo is unavailable locally or when remote state must be verified.
- Do not assume the folder name from memory. Verify the checkout and its remote before editing; some repositories may use historical local directory names.
- **bzfile** = `GrizzlyOne95/bzfile` (this repo): Lua-accessible file I/O support used by Battlezone scripts and addon-side systems.
- **OpenShim** = `GrizzlyOne95/Battlezone98Redux_Shim`: native `winmm.dll` shim, engine hooks/patches, reverse engineering, SDK/native integration, and low-level Redux behavior.
- **EXU / ExtraUtilities** = `GrizzlyOne95/ExtraUtilities`: script extender and native utility library, especially reusable Lua-facing APIs and higher-level runtime features. When a task or document says **EXU**, it means this repository; do not rediscover or invent a separate EXU project.
- **Campaign Reimagined / CR** = `GrizzlyOne95/Battlezone98Redux_CampaignReimagined`: campaign/addon content, Lua consumers, materials/shaders, packaging, integration examples, and end-user validation. Its local Git checkout may be useful for reference, but its own `AGENTS.md` defines the authoritative source/edit and promotion paths.
- Cross-repo reading is encouraged when it avoids duplicating an API, misunderstanding ownership, or re-reverse-engineering something already solved elsewhere. Cross-repo editing is not automatic: modify another bundle repo only when the task actually requires a coordinated change and after reading that repo's own `AGENTS.md`.

### BZR Reference and Tooling Repositories
These repositories are especially useful for research and implementation reference, but are **not default edit targets** for new OpenShim/EXU/CR features. When working locally, first look for them under `%USERPROFILE%\Documents\GIT\<repository-name>` and verify the checkout/remote before relying on it.

- `GrizzlyOne95/BZ98RBlenderToolKit` — Redux asset, mesh/skeleton, animation, and Blender pipeline reference.
- `GrizzlyOne95/Battlezone98Redux_DedicatedServer` — dedicated-server behavior and multiplayer/server reference.
- `GrizzlyOne95/BZ1-GameWatcher` — Battlezone 1 game/server watching and related integration reference.
- `GrizzlyOne95/BZ1_Source` — Battlezone 1 source reference for legacy engine/game behavior.
- `GrizzlyOne95/BZ2_Source` — Battlezone II source reference for related engine/game concepts.
- `GrizzlyOne95/Battlezone_LobbyMonitor` — lobby/network monitoring reference.
- `GrizzlyOne95/BZNTools` — BZN/map tooling and format reference.
- `GrizzlyOne95/Battlezone98Redux_AudioTool` — Redux audio tooling/format reference.
- `GrizzlyOne95/Battlezone98Redux_WorldBuilder` — world/map-building tooling reference.
- `GrizzlyOne95/Battlezone98Redux_ZFSSpecialist` — ZFS/archive/content-format reference.
- Use these repos to answer questions, compare implementations, recover formats/behavior, and avoid duplicated investigation. Do not include them in a feature's change set merely because they were consulted.

## Cross-Repo Pointers
- Primary addon-side usage lives in Campaign Reimagined. Consult `GrizzlyOne95/Battlezone98Redux_CampaignReimagined` or its local checkout under `%USERPROFILE%\Documents\GIT` for usage examples, while following that repo's own canonical-source rules before editing it.
- Native engine and shim integration lives in `GrizzlyOne95/Battlezone98Redux_Shim`.
- Higher-level Lua/native utility integration lives in `GrizzlyOne95/ExtraUtilities` (EXU).

Open `%USERPROFILE%\Documents\Battlezone98Redux_Shim.code-workspace` when a task may span repos.
