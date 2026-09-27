# Prebuilt Lua core

`Lua5.1-BZR.lib` (Release) and `Lua5.1-BZR-debug.lib` (Debug) are static
builds of the Battlezone 98 Redux Lua 5.1.5 core. bzfile links one of them
and runs it on the game's `lua_State`, so the game and bzfile each execute
their own copy of the same core.

## Provenance

- Source: the `Lua5.1-BZR` project in Extra Utilities
  (`GrizzlyOne95/ExtraUtilities`, `Lua5.1-BZR/src`), the "repaired version"
  imported in b207ba5 (2025-02-19) and refreshed in e622a60 (2026-03-14).
- The headers in `include/` match that project's.
- The Release library exports the same members and symbols as Extra
  Utilities' build of that project. It is built with the static CRT (`/MT`)
  to match `bzfile.dll`; Extra Utilities builds it with `/MD`, so the objects
  are not byte-identical.
- The exact source commit of each snapshot was not recorded. Rebuilding
  these libraries from source in CI is the open follow-up to audit item P1-1
  (`Docs/CODE_AUDIT_20260927.md`).

## The shared dummynode

The two copies of the core agree on one sentinel only by address. The core
is built with `#define dummynode (0x86EEF0)` (ltable.c), the game's static
`dummynode_` in 2.2.301 (GOG and Steam). `luaH_resize` frees a table's node
array unless it is that sentinel, so on a game build where `dummynode_`
moved, bzfile would free static memory and corrupt the game's heap.

`luaopen_bzfile` therefore refuses to load unless the running game has an
empty Lua node at that address, in a data section, that its own code refers
to (`CheckHostLuaDummyNode` in `src/LuaExport.cpp`). `tests/lua_host` checks
both outcomes. A new game build needs the address in the core and in that
check updated together.
