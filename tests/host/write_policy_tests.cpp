// Host test for src/bzfile_write_policy.h. Built by tests/linux/run.sh.

#include "../../src/bzfile_write_policy.h"

#include <cstdio>
#include <string>

namespace
{
	int g_failures = 0;

	void Expect(bool actual, bool expected, const wchar_t* leaf)
	{
		if (actual != expected)
		{
			std::fprintf(stderr, "FAIL: IsProtectedLeafName(\"%ls\") = %s, expected %s\n",
				leaf, actual ? "true" : "false", expected ? "true" : "false");
			++g_failures;
		}
	}

	void Protected(const wchar_t* leaf)
	{
		Expect(bzfile::policy::IsProtectedLeafName(leaf), true, leaf);
	}

	void Writable(const wchar_t* leaf)
	{
		Expect(bzfile::policy::IsProtectedLeafName(leaf), false, leaf);
	}
}

int main()
{
	// Shipped binaries and any other native code.
	Protected(L"winmm.dll");
	Protected(L"bzfile.dll");
	Protected(L"bzloader.dll");
	Protected(L"openshim.dll");
	Protected(L"exu.dll");
	Protected(L"bzfile_replace_helper.exe");
	Protected(L"battlezone98redux.exe");
	Protected(L"anything.asi");

	// Spellings Win32 resolves to the same file.
	Protected(L"WINMM.DLL");
	Protected(L"winmm.dll.");
	Protected(L"winmm.dll. . ");
	Protected(L"winmm.dll::$DATA");
	Protected(L"winmm.dll:evil");
	Protected(L"Net.INI");

	// Suite configuration and payloads.
	Protected(L"net.ini");
	Protected(L"patches.json");
	Protected(L"openshim_net.ini.payload");
	Protected(L"openshim_patches.json.payload");

	// Update transaction state.
	Protected(L"winmm.dll.pending.0123456789ab");
	Protected(L"openshim_suite_2.pending.0123456789ab");
	Protected(L"net.ini.previous");
	Protected(L"winmm.dll.previous");

	// Files Campaign Reimagined legitimately writes, copies or deletes.
	Writable(L"openshim.ini");
	Writable(L"openshim.ini.pre-workshop.bak");
	Writable(L"openshim_update.status");
	Writable(L"winmm_update.status");
	Writable(L"career_stats.cfg");
	Writable(L"auto.sav");
	Writable(L"cr_config.lua");
	Writable(L"subtitles.log");

	// Near misses.
	Writable(L"dll");
	Writable(L"winmm.dll.txt");
	Writable(L"net.ini.bak");
	Writable(L"mynet.ini");
	Writable(L"previous.txt");
	Writable(L"");
	Writable(L"...");

	if (g_failures != 0)
	{
		std::fprintf(stderr, "%d write-policy check(s) failed\n", g_failures);
		return 1;
	}

	std::puts("write policy: all checks passed");
	return 0;
}
