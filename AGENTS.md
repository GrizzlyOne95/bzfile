# bzfile

Lua-accessible file I/O for Battlezone 98 Redux, including tightly constrained update/deployment helpers. Keep this root file repo-specific; general model prompting belongs in Codex configuration rather than every task's repository context.

## Ownership and local sources

- **bzfile** owns Lua file I/O and constrained update/replacement primitives. Route low-level hooks to **OpenShim**, reusable runtime APIs to **EXU**, and campaign consumers/packaging to **CR**.
- Sibling repositories normally live under `%USERPROFILE%\Documents\GIT`; verify `origin` and branch before relying on them. CR's only editable source is `%USERPROFILE%\Documents\Google Drive\Ian Files\Battlezone Files\Redux Maps\Open Patch - CampaignReimagined`; never edit or reverse-sync a stale CR copy under `Documents\GIT` or its deployed runtime.
- Read a sibling's `AGENTS.md` before editing it.

## Load only when relevant

- Lua behavior or Lua-facing APIs: read `Docs/BZR_LUA_AGENT_REFERENCE.md`.
- Native loading, paths, filesystem/process behavior, discovery, installers, deployment, packaging, or updates: read `Docs/BZR_PLATFORM_COMPATIBILITY.md` and account for Windows/GOG, Windows/Steam, Proton, and Wine.
- Exposed APIs or update helpers: read the relevant `README.md` section.
- Any write, copy, delete, replacement, or update behavior: read `.jules/sentinel.md` first. Keep critical-DLL protection centralized and applied to every mutating path.
- The shared Lua and platform documents must remain byte-identical across bzfile, OpenShim, EXU, and CR; update all four in one workstream if either changes.

## Safety and platform boundaries

- Preserve the constrained nature of OpenShim update helpers; scripts must not gain arbitrary destinations for convenience.
- Game-facing binaries remain Win32 (`bzfile.dll`, `bzfile_replace_helper.exe`) and build with MSVC. Linux runs those binaries through Proton/Wine; do not add or claim a native Linux `.so` or MinGW DLL target.
- Linux host checks use `tests/linux/run.sh`. Proton installation/deployment uses `scripts/install_linux.sh` and `scripts/deploy_linux_proton.sh`; live deployment requires explicit user instruction.

## Work and validation

- Inspect `git status -sb` and the relevant diff before editing. Preserve unrelated work.
- Use one `agent/<short-description>` branch per workstream, normally from current `origin/main`; do not reuse finished branches or mix unrelated follow-ups.
- Start with the smallest implementation and targeted checks. Run the broader platform matrix only when the affected behavior or a release gate requires it.
- Stage only task-owned files. Never blanket-stage, clean, restore, or overwrite unrelated work. Do not force-push or rewrite shared history unless explicitly requested.
- Agents may commit and push coherent task-owned checkpoints. PR merges, releases/tags, Workshop publication, and live deployment require explicit user instruction.
- Do not commit secrets, credentials, transient build/runtime output, crash dumps, or scratch artifacts.
