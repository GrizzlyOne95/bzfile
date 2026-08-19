# bzfile

Lua-accessible file I/O library for Battlezone 98 Redux, including constrained update/deployment helpers.

## Local Environment
- Sibling Battlezone repos normally live under `%USERPROFILE%\Documents\GIT`. Prefer local sibling source for reference when present; verify its `origin` before editing because historical folder names may differ.

## BZR Bundle
- **bzfile** — `GrizzlyOne95/bzfile` (this repo): Lua file I/O and constrained update/deployment support.
- **OpenShim** — `GrizzlyOne95/Battlezone98Redux_Shim`: low-level hooks, patches, RE, SDK/native engine integration.
- **EXU / ExtraUtilities** — `GrizzlyOne95/ExtraUtilities`: reusable native/Lua-facing runtime features. **EXU always means this repository.**
- **Campaign Reimagined / CR** — `GrizzlyOne95/Battlezone98Redux_CampaignReimagined`: addon content, Lua consumers, assets, packaging, and end-user integration/validation; its own `AGENTS.md` defines authoritative source/deploy paths.

Cross-repo reading is encouraged. Do not edit another repo merely because it was consulted; read that repo's `AGENTS.md` before coordinated changes.

## Shared BZR Lua Reference
Before writing, reviewing, or changing BZR Lua behavior—or adding Lua-facing native APIs—read `Docs/BZR_LUA_AGENT_REFERENCE.md`. This document is mirrored across the four core BZR repos and should remain byte-identical. Repo-specific `AGENTS.md`/architecture docs still govern implementation ownership. When the shared reference changes, mirror the same content to OpenShim, EXU, Campaign Reimagined, and bzfile in the same workstream.

Reference/tooling repos under `%USERPROFILE%\Documents\GIT` (reference, not default edit targets): `BZ98RBlenderToolKit`, `Battlezone98Redux_DedicatedServer`, `BZ1-GameWatcher`, `BZ1_Source`, `BZ2_Source`, `Battlezone_LobbyMonitor`, `BZNTools`, `Battlezone98Redux_AudioTool`, `Battlezone98Redux_WorldBuilder`, `Battlezone98Redux_ZFSSpecialist`.

## Git Workflow
- Before editing, inspect `git status -sb` and the relevant diff; preserve pre-existing user changes.
- Normal work goes on a task branch, usually `agent/<short-description>`, never directly on the default/protected branch.
- Agents may commit and push coherent task-owned checkpoints without repeatedly asking. Prefer validated milestones; a clearly labeled `WIP:` checkpoint is acceptable when preserving valuable intermediate work.
- Stage only task-owned files. Never blanket-stage, clean, restore, or otherwise absorb/destroy unrelated changes in a mixed worktree.
- Do not rewrite shared history or force-push unless explicitly requested.
- PR merges, releases/tags, Workshop publication, and other external release/deployment actions require explicit user instruction.
- Do not commit secrets, machine credentials, transient build/runtime output, crash dumps, or scratch artifacts the repo does not intentionally track.

## Safety / Task Routing
- `README.md` is the API/behavior reference; read the relevant section when changing exposed Lua APIs or update helpers rather than loading it for unrelated tasks.
- When changing write, copy, delete, replacement, or update behavior, read `.jules/sentinel.md` first. Critical DLL write protection must remain centralized and consistently applied across every mutating filesystem path.
- Preserve the constrained nature of OpenShim update helpers: scripts must not gain arbitrary destinations merely for convenience.
- Campaign-side consumers and deployment behavior belong in CR; low-level native engine behavior belongs in OpenShim; reusable runtime feature logic belongs in EXU.
