# bzfile

This repo is part of the local Battlezone workspace opened via
`%USERPROFILE%\Documents\Battlezone98Redux_Shim.code-workspace`.

## Workspace Layout
- Sibling repos normally live under `%USERPROFILE%\Documents\GIT\...`.
- The primary local game install is typically `%USERPROFILE%\Documents\Battlezone 98 Redux`.
- Prefer the workspace file and these conventions over hardcoded profile-specific paths.

## Local Role
- Lua-accessible file I/O library for Battlezone 98 Redux.

## Cross-Repo Pointers
- Primary addon-side usage lives in the deployed campaign addon under the workspace game install, usually `%USERPROFILE%\Documents\Battlezone 98 Redux\addon\campaignReimagined`.
- Other native support repos in this workspace include `%USERPROFILE%\Documents\GIT\Battlezone98Redux_Shim`, `%USERPROFILE%\Documents\GIT\BZR-Subtitles`, and `%USERPROFILE%\Documents\GIT\ExtraUtilities-G1`.

Open `%USERPROFILE%\Documents\Battlezone98Redux_Shim.code-workspace` when a task may span repos.
