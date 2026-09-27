// Minimal stand-in for the game's Lua host, so bzfile.dll can be exercised
// by real Lua outside Battlezone 98 Redux.
//
//   bzfile_lua_host.exe <script.lua>
//
// Like the game, this process runs its own copy of the BZR Lua core
// (lib/Lua5.1-BZR.lib) and loads bzfile.dll, which links a second copy. Both
// copies are built for the game and share one sentinel by address: the
// game's static dummynode at 0x0086EEF0 (ltable.c). The game provides it from
// its own zero-filled data section, so this host does the same: it links at
// the game's fixed base (0x00400000, no ASLR) with a zero-filled .bss span
// that covers that address. Mapping the page at run time does not work,
// because the CRT heap already reserves that range before wmain runs.
//
// bzfile pins its sandbox root to the directory of the host executable, so
// the test copies this program and bzfile.dll into a scratch "game" folder.

#include <Windows.h>

#include <lua.hpp>

#include <cstdio>
#include <string>

namespace
{
	constexpr uintptr_t kGameDummyNode = 0x0086EEF0;
	constexpr size_t kGameDummyNodeSize = 32; // sizeof(Node) in the BZR core

	// Lands in .bss past the host's code and data; wmain checks that it
	// really covers the dummynode before running any Lua.
	unsigned char g_gameDataSpan[0x00600000];
}

int wmain(int argc, wchar_t** argv)
{
	if (argc != 2)
	{
		std::fputs("usage: bzfile_lua_host.exe <script.lua>\n", stderr);
		return 2;
	}

	const auto spanBegin = reinterpret_cast<uintptr_t>(g_gameDataSpan);
	const auto spanEnd = spanBegin + sizeof(g_gameDataSpan);
	if (kGameDummyNode < spanBegin || kGameDummyNode + kGameDummyNodeSize > spanEnd)
	{
		std::fprintf(stderr,
			"the host's data span 0x%08X-0x%08X does not cover the dummynode at 0x%08X; "
			"link with /BASE:0x400000 /FIXED\n",
			static_cast<unsigned>(spanBegin), static_cast<unsigned>(spanEnd),
			static_cast<unsigned>(kGameDummyNode));
		return 3;
	}

	wchar_t exePath[MAX_PATH] = {};
	const DWORD exeLength = GetModuleFileNameW(nullptr, exePath, MAX_PATH);
	if (exeLength == 0 || exeLength >= MAX_PATH)
	{
		std::fputs("could not resolve the host path\n", stderr);
		return 3;
	}
	std::wstring dllPath(exePath, exeLength);
	dllPath.resize(dllPath.find_last_of(L"\\/") + 1);
	dllPath += L"bzfile.dll";

	HMODULE bzfile = LoadLibraryExW(dllPath.c_str(), nullptr, LOAD_WITH_ALTERED_SEARCH_PATH);
	if (bzfile == nullptr)
	{
		std::fprintf(stderr, "could not load bzfile.dll (error %lu)\n", GetLastError());
		return 3;
	}

	const auto openBzfile = reinterpret_cast<lua_CFunction>(GetProcAddress(bzfile, "luaopen_bzfile"));
	if (openBzfile == nullptr)
	{
		std::fputs("bzfile.dll does not export luaopen_bzfile\n", stderr);
		return 3;
	}

	char script[MAX_PATH * 3] = {};
	if (WideCharToMultiByte(CP_ACP, 0, argv[1], -1, script, sizeof(script), nullptr, nullptr) == 0)
	{
		std::fputs("could not convert the script path\n", stderr);
		return 2;
	}

	lua_State* L = luaL_newstate();
	luaL_openlibs(L);

	lua_pushcfunction(L, openBzfile);
	if (lua_pcall(L, 0, 0, 0) != 0)
	{
		std::fprintf(stderr, "luaopen_bzfile failed: %s\n", lua_tostring(L, -1));
		lua_close(L);
		return 1;
	}

	int status = 0;
	if (luaL_dofile(L, script) != 0)
	{
		std::fprintf(stderr, "%s\n", lua_tostring(L, -1));
		status = 1;
	}

	lua_close(L);
	return status;
}
