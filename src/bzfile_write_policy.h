#pragma once

// Which file names a Lua script may never create, overwrite, copy onto or
// delete through bzfile. This is the single write-protection rule that every
// mutating binding in LuaExport.cpp applies (see .jules/sentinel.md). It is
// pure and has no Win32 dependency so the Linux host lane can test it.

#include <cwctype>
#include <string>
#include <string_view>

namespace bzfile::policy
{
	// The leaf name as the Windows file system will resolve it: lower case,
	// any ":stream" suffix removed (winmm.dll::$DATA is winmm.dll), and
	// trailing dots and spaces stripped, because Win32 drops them when it
	// opens a file ("winmm.dll." creates winmm.dll).
	inline std::wstring EffectiveLeafName(std::wstring leaf)
	{
		const size_t stream = leaf.find(L':');
		if (stream != std::wstring::npos)
		{
			leaf.resize(stream);
		}

		while (!leaf.empty() && (leaf.back() == L'.' || leaf.back() == L' '))
		{
			leaf.pop_back();
		}

		for (auto& ch : leaf)
		{
			ch = static_cast<wchar_t>(std::towlower(static_cast<std::wint_t>(ch)));
		}
		return leaf;
	}

	inline bool EndsWith(const std::wstring& value, std::wstring_view suffix)
	{
		return value.size() >= suffix.size()
			&& value.compare(value.size() - suffix.size(), suffix.size(), suffix) == 0;
	}

	inline bool IsProtectedLeafName(const std::wstring& rawLeaf)
	{
		const std::wstring leaf = EffectiveLeafName(rawLeaf);
		if (leaf.empty())
		{
			return false;
		}

		// Native code. Every shipped binary is one (winmm.dll, bzloader.dll,
		// plugins\openshim.dll, bzfile.dll, the helper, the game), and a
		// script able to write a new DLL anywhere Lua's cpath or the Windows
		// loader searches could run native code.
		for (const std::wstring_view extension : { L".dll", L".exe", L".asi" })
		{
			if (EndsWith(leaf, extension))
			{
				return true;
			}
		}

		// Native configuration that only the verified suite update may
		// replace, and that update's own payload names. openshim.ini is the
		// player's config and is deliberately not listed: Campaign
		// Reimagined installs it with CopyFile.
		for (const std::wstring_view name : {
			L"net.ini",
			L"patches.json",
			L"openshim_net.ini.payload",
			L"openshim_patches.json.payload" })
		{
			if (leaf == name)
			{
				return true;
			}
		}

		// Update transaction files owned by bzfile_replace_helper.exe: staged
		// payloads (winmm.dll.pending.<hash>, openshim_suite_<n>.pending.<hash>)
		// and the backups it restores on rollback.
		return leaf.find(L".pending") != std::wstring::npos
			|| EndsWith(leaf, L".previous");
	}
}
