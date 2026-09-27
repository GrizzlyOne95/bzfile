#include <lua.hpp>

#include <Windows.h>
#include <wincrypt.h>

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <cwctype>
#include <new>
#include <string>
#include <system_error>
#include <sstream>
#include <iomanip>
#include <vector>

#include "bzfile_write_policy.h"

namespace File
{
	namespace
	{
		// Upper bound for a single Read or Dump. The game is a 32-bit process;
		// an unbounded request used to be allocated up front and could throw
		// bad_alloc straight into the engine.
		constexpr size_t kMaxReadBytes = 64u * 1024u * 1024u;

		std::wstring ToLower(std::wstring value)
		{
			for (auto& ch : value)
			{
				ch = static_cast<wchar_t>(towlower(ch));
			}
			return value;
		}

		// Lua strings reach std::filesystem::path as ANSI (the active code
		// page), so paths handed back to Lua use the same encoding. The
		// conversion reports, rather than throws on, characters the code page
		// cannot represent: path::string() throws system_error there, which
		// used to escape into the game.
		std::string ToAnsi(const std::wstring& wide, bool* lossy)
		{
			if (lossy != nullptr)
			{
				*lossy = false;
			}
			if (wide.empty())
			{
				return {};
			}

			// With a UTF-8 active code page every character maps, and
			// WideCharToMultiByte rejects the default-char arguments.
			const bool detectLoss = GetACP() != CP_UTF8;
			const DWORD flags = detectLoss ? WC_NO_BEST_FIT_CHARS : 0;
			BOOL usedDefault = FALSE;
			BOOL* usedDefaultOut = detectLoss ? &usedDefault : nullptr;

			const int wideLength = static_cast<int>(wide.size());
			const int size = WideCharToMultiByte(
				CP_ACP, flags, wide.data(), wideLength, nullptr, 0, nullptr, usedDefaultOut);
			if (size <= 0)
			{
				if (lossy != nullptr)
				{
					*lossy = true;
				}
				return {};
			}

			std::string narrow(static_cast<size_t>(size), '\0');
			WideCharToMultiByte(
				CP_ACP, flags, wide.data(), wideLength, narrow.data(), size, nullptr, usedDefaultOut);
			if (lossy != nullptr && usedDefault)
			{
				*lossy = true;
			}
			return narrow;
		}

		// For messages only: unmappable characters become '?'.
		std::string DisplayPath(const std::filesystem::path& path)
		{
			return ToAnsi(path.native(), nullptr);
		}

		// For paths returned to Lua, which must open again through bzfile.
		bool TryNarrowPath(const std::filesystem::path& path, std::string& narrow)
		{
			bool lossy = false;
			narrow = ToAnsi(path.native(), &lossy);
			return !lossy;
		}

		std::filesystem::path NormalizePath(const std::filesystem::path& path)
		{
			std::error_code error;
			auto absolute = std::filesystem::absolute(path, error);
			if (error)
			{
				absolute = path;
				error.clear();
			}

			auto canonical = std::filesystem::weakly_canonical(absolute, error);
			if (!error)
			{
				return canonical;
			}

			return absolute.lexically_normal();
		}

		// The sandbox root must not be derived from the process working
		// directory. current_path() is process-global mutable state: the engine,
		// a common file dialog opened without OFN_NOCHANGEDIR, or any loaded DLL
		// can move it, and every allowed-path decision would move with it. Pin
		// the root once to the directory holding the game executable -- the same
		// directory winmm.dll is loaded from -- and fall back to the working
		// directory only if that cannot be resolved.
		std::filesystem::path ResolveGameRootPathOnce()
		{
			std::wstring executablePath(MAX_PATH, L'\0');
			for (;;)
			{
				const DWORD length = GetModuleFileNameW(
					nullptr,
					executablePath.data(),
					static_cast<DWORD>(executablePath.size()));
				if (length == 0)
				{
					break;
				}

				if (length < executablePath.size())
				{
					executablePath.resize(length);
					std::filesystem::path parent =
						std::filesystem::path(executablePath).parent_path();
					if (!parent.empty())
					{
						return NormalizePath(parent);
					}
					break;
				}

				if (executablePath.size() > 32768)
				{
					break;
				}
				executablePath.resize(executablePath.size() * 2);
			}

			return NormalizePath(std::filesystem::current_path());
		}

		std::filesystem::path GetWorkingDirectoryPath()
		{
			static const std::filesystem::path root = ResolveGameRootPathOnce();
			return root;
		}

		std::filesystem::path GetLogsDirectoryPath()
		{
			const auto gameRoot = GetWorkingDirectoryPath();
			const auto logs = gameRoot / L"logs";
			std::error_code error;
			std::filesystem::create_directories(logs, error);
			return error ? gameRoot : logs;
		}

		std::filesystem::path FindSteamAppsDirectory(const std::filesystem::path& start)
		{
			auto current = NormalizePath(start);
			for (;;)
			{
				if (ToLower(current.filename().wstring()) == L"steamapps")
				{
					return current;
				}

				std::error_code error;
				auto nestedSteamApps = current / "steamapps";
				if (std::filesystem::is_directory(nestedSteamApps, error))
				{
					return NormalizePath(nestedSteamApps);
				}

				auto parent = current.parent_path();
				if (parent == current)
				{
					break;
				}

				current = parent;
			}

			return {};
		}

		std::filesystem::path ResolveWorkshopDirectoryPathOnce()
		{
			std::filesystem::path bzrRoot = GetWorkingDirectoryPath();
			std::filesystem::path steamapps = FindSteamAppsDirectory(bzrRoot);
			if (steamapps.empty())
			{
				// Not a Steam install (GOG, or a copied game folder). There is
				// no Workshop directory to find, and guessing one two levels up
				// -- C:\Program Files (x86)\GOG Galaxy\workshop\... for GOG --
				// only turned an unrelated directory into an allowed write root.
				return {};
			}
			return NormalizePath(steamapps / "workshop" / "content" / "301650");
		}

		std::filesystem::path GetWorkshopDirectoryPath()
		{
			// Cached for the same reason as the game root, and because resolving
			// it walks the directory tree looking for "steamapps" on every call.
			static const std::filesystem::path workshop = ResolveWorkshopDirectoryPathOnce();
			return workshop;
		}

		bool IsPathInsideRoot(const std::filesystem::path& candidate, const std::filesystem::path& root)
		{
			if (root.empty())
			{
				return false;
			}

			auto normalizedCandidate = NormalizePath(candidate);
			auto normalizedRoot = NormalizePath(root);

			auto candidateIt = normalizedCandidate.begin();
			auto rootIt = normalizedRoot.begin();
			for (; rootIt != normalizedRoot.end(); ++rootIt, ++candidateIt)
			{
				if (candidateIt == normalizedCandidate.end())
				{
					return false;
				}

				if (ToLower(rootIt->wstring()) != ToLower(candidateIt->wstring()))
				{
					return false;
				}
			}

			return true;
		}

		// True when the candidate IS a sandbox root, or contains one. Such a path
		// is technically "inside" the allowed area (a root is inside itself), but
		// a recursive delete aimed at it would take the entire game installation
		// with it, so it is never a valid target for a mutating operation.
		bool IsSandboxRootOrAncestor(const std::filesystem::path& candidate)
		{
			const std::filesystem::path roots[] = {
				GetWorkingDirectoryPath(),
				GetWorkshopDirectoryPath()
			};

			for (const auto& root : roots)
			{
				if (root.empty())
				{
					continue;
				}

				// IsPathInsideRoot(root, candidate) asks whether the root lies
				// beneath the candidate, i.e. whether the candidate is an
				// ancestor of (or equal to) that root.
				if (IsPathInsideRoot(root, candidate))
				{
					return true;
				}
			}

			return false;
		}

		// Non-throwing replacement for the old CheckPathAllowed, which called
		// luaL_error directly. The game's Lua core is built as C, so LUAI_THROW
		// is longjmp (luaconf.h): raising from here unwound past live
		// std::filesystem::path and std::string locals without running their
		// destructors -- a leak on every rejection, and formally undefined
		// behaviour. Rejections are now ordinary Lua returns instead.
		bool TryResolveAllowedPath(
			const char* requestedPath,
			std::filesystem::path& outResolved,
			std::string& outError)
		{
			outResolved.clear();
			outError.clear();

			if (requestedPath == nullptr)
			{
				outError = "bzfile Error: path argument was nil";
				return false;
			}

			auto normalizedPath = NormalizePath(std::filesystem::path(requestedPath));
			const auto workingRoot = GetWorkingDirectoryPath();
			const auto workshopRoot = GetWorkshopDirectoryPath();

			if (IsPathInsideRoot(normalizedPath, workingRoot)
				|| (!workshopRoot.empty() && IsPathInsideRoot(normalizedPath, workshopRoot)))
			{
				outResolved = std::move(normalizedPath);
				return true;
			}

			outError = "bzfile Error: refusing to access path outside allowed roots. Path: \""
				+ DisplayPath(normalizedPath) + "\"";
			return false;
		}

		// Rejection results. Neither raises, so no destructor is skipped.
		int PushPathRejectionNil(lua_State* L, const std::string& error)
		{
			lua_pushnil(L);
			lua_pushstring(L, error.c_str());
			return 2;
		}

		int PushPathRejectionFalse(lua_State* L, const std::string& error)
		{
			lua_pushboolean(L, 0);
			lua_pushstring(L, error.c_str());
			return 2;
		}

		std::wstring QuoteCommandLineArgument(const std::wstring& value)
		{
			if (value.empty())
			{
				return L"\"\"";
			}

			if (value.find_first_of(L" \t\n\v\"") == std::wstring::npos)
			{
				return value;
			}

			std::wstring quoted;
			quoted.push_back(L'"');

			size_t backslashCount = 0;
			for (wchar_t ch : value)
			{
				if (ch == L'\\')
				{
					++backslashCount;
					continue;
				}

				if (ch == L'"')
				{
					quoted.append(backslashCount * 2 + 1, L'\\');
					quoted.push_back(ch);
					backslashCount = 0;
					continue;
				}

				quoted.append(backslashCount, L'\\');
				backslashCount = 0;
				quoted.push_back(ch);
			}

			quoted.append(backslashCount * 2, L'\\');
			quoted.push_back(L'"');
			return quoted;
		}

		std::filesystem::path GetCurrentModulePath()
		{
			HMODULE moduleHandle = nullptr;
			if (!GetModuleHandleExW(
				GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
				reinterpret_cast<LPCWSTR>(&GetCurrentModulePath),
				&moduleHandle))
			{
				return {};
			}

			std::wstring modulePath(MAX_PATH, L'\0');
			for (;;)
			{
				DWORD length = GetModuleFileNameW(
					moduleHandle,
					modulePath.data(),
					static_cast<DWORD>(modulePath.size()));
				if (length == 0)
				{
					return {};
				}

				if (length < modulePath.size())
				{
					modulePath.resize(length);
					return std::filesystem::path(modulePath);
				}

				modulePath.resize(modulePath.size() * 2);
			}
		}

		// The one write-protection check (see .jules/sentinel.md): every binding
		// that creates, overwrites, copies onto or deletes a path calls it. The
		// rule itself lives in bzfile_write_policy.h. It cannot be switched off
		// from Lua; SetAllowWinmmOverwrite used to disable it process-wide.
		bool IsWriteProtected(const std::filesystem::path& path)
		{
			return bzfile::policy::IsProtectedLeafName(path.filename().native());
		}

		bool SamePath(const std::filesystem::path& left, const std::filesystem::path& right)
		{
			return ToLower(NormalizePath(left).wstring()) == ToLower(NormalizePath(right).wstring());
		}

		std::string NarrowSystemError(DWORD errorCode)
		{
			std::error_code error(static_cast<int>(errorCode), std::system_category());
			return error.message();
		}

		bool LaunchHiddenProcess(
			const std::wstring& executable,
			const std::vector<std::wstring>& arguments,
			std::string& errorMessage)
		{
			std::wstring commandLine = QuoteCommandLineArgument(executable);
			for (const auto& argument : arguments)
			{
				commandLine.push_back(L' ');
				commandLine += QuoteCommandLineArgument(argument);
			}

			std::vector<wchar_t> mutableCommand(commandLine.begin(), commandLine.end());
			mutableCommand.push_back(L'\0');

			STARTUPINFOW startupInfo = {};
			startupInfo.cb = sizeof(startupInfo);
			startupInfo.dwFlags = STARTF_USESHOWWINDOW;
			startupInfo.wShowWindow = SW_HIDE;

			PROCESS_INFORMATION processInfo = {};
			BOOL created = CreateProcessW(
				executable.c_str(),
				mutableCommand.data(),
				nullptr,
				nullptr,
				FALSE,
				// GOG Galaxy can place the game and its children in a job object.
				// Without breakaway, the replacement helper is terminated with the
				// game before it can promote the pending DLL.
				CREATE_NO_WINDOW | DETACHED_PROCESS | CREATE_BREAKAWAY_FROM_JOB,
				nullptr,
				nullptr,
				&startupInfo,
				&processInfo);
			if (!created)
			{
				errorMessage = NarrowSystemError(GetLastError());
				return false;
			}

			CloseHandle(processInfo.hThread);
			CloseHandle(processInfo.hProcess);
			return true;
		}

		// Exception barrier around every registered function. The game's Lua
		// core is C, so a C++ exception leaving a binding (bad_alloc on a large
		// read, filesystem_error, system_error from a path conversion) has no
		// handler above it and takes the game down. A caught exception becomes
		// the usual nil + message result instead. The message is copied into
		// a local buffer inside the handler so no Lua call runs there.
		template <lua_CFunction Binding>
		int Guarded(lua_State* L)
		{
			char message[512] = "bzfile Error: unexpected internal error";
			try
			{
				return Binding(L);
			}
			catch (const std::bad_alloc&)
			{
				strcpy_s(message, "bzfile Error: out of memory");
			}
			catch (const std::exception& exception)
			{
				_snprintf_s(message, _TRUNCATE, "bzfile Error: %s", exception.what());
			}
			catch (...)
			{
			}

			lua_pushnil(L);
			lua_pushstring(L, message);
			return 2;
		}
	}

#ifdef _DEBUG
	void DebugPrint(lua_State* L, const char* message)
	{
		lua_getglobal(L, "print");
		lua_pushstring(L, message);
		lua_call(L, 1, 0);
	}
#endif

	static int Open(lua_State* L)
	{
		// Every Lua argument is read before any destructible object exists, so a
		// luaL_check*/luaL_opt* type error cannot unwind past a live destructor.
		const char* requestedPath = luaL_checkstring(L, 1);
		const char* requestedMode = luaL_optstring(L, 2, "r");
		const char* requestedOptions = luaL_optstring(L, 3, "app");

		std::filesystem::path filePath;
		std::string pathError;
		if (!TryResolveAllowedPath(requestedPath, filePath, pathError))
		{
			return PushPathRejectionNil(L, pathError);
		}

		std::string modeStr = requestedMode;
		std::string options = requestedOptions;

		std::ios_base::openmode openMode{};
		bool isWrite = false;

		if (modeStr.find('r') != std::string::npos)
		{
			openMode |= std::ios::in;
		}
		if (modeStr.find('w') != std::string::npos)
		{
			openMode |= std::ios::out;
			isWrite = true;
		}
		if (modeStr.find('b') != std::string::npos)
		{
			openMode |= std::ios::binary;
		}

		// These report through the same nil+message channel the "could not open
		// file" path below already uses, rather than raising: a raise here would
		// longjmp past filePath/modeStr/options.
		if (isWrite)
		{
			if (IsWriteProtected(filePath))
			{
				return PushPathRejectionNil(
					L, "bzfile Error: file is write-protected: \"" + DisplayPath(filePath) + "\"");
			}

			if (options == "app")
			{
				openMode |= std::ios::app;
			}
			else if (options == "trunc")
			{
				openMode |= std::ios::trunc;
			}
			else
			{
				return PushPathRejectionNil(
					L, "bzfile Error: invalid open option \"" + options + "\"");
			}
		}
		else if (openMode == 0)
		{
			return PushPathRejectionNil(
				L, "bzfile Error: invalid open mode \"" + modeStr + "\"");
		}

		void* buffer = lua_newuserdata(L, sizeof(std::fstream));
		std::fstream* fs = new (buffer) std::fstream(filePath, openMode);

		luaL_getmetatable(L, "FileMetatable");
		lua_setmetatable(L, -2);

		if (!fs->is_open())
		{
			lua_pushnil(L);
			lua_pushfstring(L, "bzfile Error: could not open file \"%s\"", DisplayPath(filePath).c_str());
			return 2;
		}

		return 1;
	}

	static int Cleanup(lua_State* L)
	{
		std::fstream* handle = (std::fstream*)lua_touserdata(L, 1);
		if (handle)
		{
			handle->~basic_fstream();
		}
		return 0;
	}

	static int Write(lua_State* L)
	{
		std::fstream* handle = (std::fstream*)luaL_checkudata(L, 1, "FileMetatable");
		if (!handle->is_open()) return luaL_error(L, "bzfile Error: file is not open");

		size_t len;
		const char* content = luaL_checklstring(L, 2, &len);
		handle->write(content, len);

		lua_pushvalue(L, 1);
		return 1;
	}

	static int Writeln(lua_State* L)
	{
		std::fstream* handle = (std::fstream*)luaL_checkudata(L, 1, "FileMetatable");
		if (!handle->is_open()) return luaL_error(L, "bzfile Error: file is not open");

		size_t len;
		const char* content = luaL_checklstring(L, 2, &len);
		handle->write(content, len);
		handle->put('\n');

		lua_pushvalue(L, 1);
		return 1;
	}

	static int Read(lua_State* L)
	{
		std::fstream* handle = (std::fstream*)luaL_checkudata(L, 1, "FileMetatable");
		if (!handle->is_open()) return luaL_error(L, "bzfile Error: file is not open");

		const lua_Integer count = luaL_optinteger(L, 2, 1);
		luaL_argcheck(L, count <= static_cast<lua_Integer>(kMaxReadBytes), 2,
			"count exceeds the 64 MiB read limit");

		if (handle->eof())
		{
			lua_pushnil(L);
			return 1;
		}

		if (count <= 1)
		{
			char c;
			if (handle->get(c))
			{
				lua_pushlstring(L, &c, 1);
			}
			else
			{
				lua_pushnil(L);
			}
			return 1;
		}

		// Read in chunks so memory follows what the file actually holds, not
		// the count the script asked for.
		std::string content;
		std::vector<char> chunk(static_cast<size_t>((std::min)(count, static_cast<lua_Integer>(64 * 1024))));
		size_t remaining = static_cast<size_t>(count);
		while (remaining > 0)
		{
			const size_t wanted = (std::min)(remaining, chunk.size());
			handle->read(chunk.data(), static_cast<std::streamsize>(wanted));
			const auto bytesRead = handle->gcount();
			if (bytesRead <= 0)
			{
				break;
			}

			content.append(chunk.data(), static_cast<size_t>(bytesRead));
			remaining -= static_cast<size_t>(bytesRead);
			if (static_cast<size_t>(bytesRead) < wanted)
			{
				break;
			}
		}

		if (content.empty())
		{
			lua_pushnil(L);
		}
		else
		{
			lua_pushlstring(L, content.data(), content.size());
		}
		return 1;
	}

	static int Readln(lua_State* L)
	{
		std::fstream* handle = (std::fstream*)luaL_checkudata(L, 1, "FileMetatable");
		if (!handle->is_open()) return luaL_error(L, "bzfile Error: file is not open");

		if (handle->eof())
		{
			lua_pushnil(L);
			return 1;
		}

		std::string line;
		if (std::getline(*handle, line))
		{
			lua_pushstring(L, line.c_str());
		}
		else
		{
			lua_pushnil(L);
		}
		return 1;
	}

	static int Dump(lua_State* L)
	{
		std::fstream* handle = (std::fstream*)luaL_checkudata(L, 1, "FileMetatable");
		if (!handle->is_open()) return luaL_error(L, "bzfile Error: file is not open");

		handle->clear();
		handle->seekg(0, std::ios::end);
		auto size = handle->tellg();
		handle->seekg(0, std::ios::beg);

		if (size < 0)
		{
			lua_pushstring(L, "");
			return 1;
		}

		if (static_cast<unsigned long long>(size) > kMaxReadBytes)
		{
			lua_pushnil(L);
			lua_pushstring(L, "bzfile Error: file exceeds the 64 MiB Dump limit");
			return 2;
		}

		// tellg() counts raw bytes, but a text-mode handle (the default, "r")
		// turns CRLF into LF and stops at 0x1A, so fewer bytes arrive than
		// were reserved. Keep only what was read: the unused tail used to be
		// returned as NUL padding, one byte per line. A handle opened for
		// writing only reads nothing and returns "".
		std::string content;
		content.resize(static_cast<size_t>(size));
		handle->read(content.data(), static_cast<std::streamsize>(size));
		const auto bytesRead = handle->gcount();
		content.resize(bytesRead > 0 ? static_cast<size_t>(bytesRead) : 0);

		lua_pushlstring(L, content.data(), content.size());
		return 1;
	}

	static int Flush(lua_State* L)
	{
		std::fstream* handle = (std::fstream*)luaL_checkudata(L, 1, "FileMetatable");
		if (!handle->is_open()) return luaL_error(L, "bzfile Error: file is not open");
		handle->flush();

		lua_pushvalue(L, 1);
		return 1;
	}

	// This will make the file handle nil in lua
	static int Close(lua_State* L)
	{
		std::fstream* handle = (std::fstream*)luaL_checkudata(L, 1, "FileMetatable");
		if (handle->is_open())
		{
			handle->close();
		}
		return 0;
	}

	static int GetWorkingDirectory(lua_State* L)
	{
		std::string directory;
		if (!TryNarrowPath(GetWorkingDirectoryPath(), directory))
		{
			lua_pushnil(L);
			lua_pushstring(L, "bzfile Error: the game directory cannot be represented in the system code page");
			return 2;
		}

		lua_pushlstring(L, directory.data(), directory.size());
		return 1;
	}

	// "" when the game is not a Steam install (no Workshop directory exists).
	static int GetWorkshopDirectory(lua_State* L)
	{
		std::string directory;
		if (!TryNarrowPath(GetWorkshopDirectoryPath(), directory))
		{
			lua_pushnil(L);
			lua_pushstring(L, "bzfile Error: the Workshop directory cannot be represented in the system code page");
			return 2;
		}

		lua_pushlstring(L, directory.data(), directory.size());
		return 1;
	}

	static int MakeDirectory(lua_State* L)
	{
		const char* requestedPath = luaL_checkstring(L, 1);

		std::filesystem::path directory;
		std::string pathError;
		if (!TryResolveAllowedPath(requestedPath, directory, pathError))
		{
			return PushPathRejectionFalse(L, pathError);
		}

		// A directory under a protected name would block the real file from
		// ever being written there (a "winmm.dll" folder in the game root).
		if (IsWriteProtected(directory))
		{
			return PushPathRejectionFalse(
				L, "bzfile Error: path is write-protected: \"" + DisplayPath(directory) + "\"");
		}

		std::error_code error;
		std::filesystem::create_directories(directory, error);
		if (error)
		{
			return PushPathRejectionFalse(
				L, "bzfile Error: MakeDirectory failed: " + error.message());
		}

		lua_pushboolean(L, 1);
		return 1;
	}

	static int Exists(lua_State* L)
	{
		const char* requestedPath = luaL_checkstring(L, 1);

		std::filesystem::path filePath;
		std::string pathError;
		if (!TryResolveAllowedPath(requestedPath, filePath, pathError))
		{
			return PushPathRejectionFalse(L, pathError);
		}

		std::error_code error;
		bool exists = std::filesystem::exists(filePath, error);

		lua_pushboolean(L, !error && exists);
		return 1;
	}

	static int CopyFile(lua_State* L)
	{
		// Both strings are fetched up front: the old code called
		// luaL_checkstring(L, 2) while sourcePath was already constructed, so an
		// argument-type error there leaked it.
		const char* requestedSource = luaL_checkstring(L, 1);
		const char* requestedDestination = luaL_checkstring(L, 2);

		std::filesystem::path sourcePath;
		std::filesystem::path destinationPath;
		std::string pathError;
		if (!TryResolveAllowedPath(requestedSource, sourcePath, pathError)
			|| !TryResolveAllowedPath(requestedDestination, destinationPath, pathError))
		{
			return PushPathRejectionFalse(L, pathError);
		}

		if (IsWriteProtected(destinationPath))
		{
			return PushPathRejectionFalse(
				L, "bzfile Error: destination is write-protected: \"" + DisplayPath(destinationPath) + "\"");
		}

		const bool overwriteExisting = lua_toboolean(L, 3) != 0;

		if (SamePath(sourcePath, destinationPath))
		{
			return PushPathRejectionFalse(L, "bzfile Error: source and destination are the same file");
		}

		std::error_code error;
		if (!std::filesystem::is_regular_file(sourcePath, error))
		{
			return PushPathRejectionFalse(
				L, "bzfile Error: source is not a readable file: \"" + DisplayPath(sourcePath) + "\"");
		}

		// Copy beside the destination first and rename it into place, so the
		// destination is untouched until a complete copy exists. The old code
		// deleted the destination before copying: a missing or unreadable
		// source (or CopyFile(p, p, true)) destroyed it. The rename is atomic
		// on NTFS and a POSIX rename under Wine.
		std::filesystem::path temporaryPath = destinationPath;
		temporaryPath += L".bzfile-copy";

		if (!CopyFileW(sourcePath.c_str(), temporaryPath.c_str(), FALSE))
		{
			const DWORD copyError = GetLastError();
			DeleteFileW(temporaryPath.c_str());
			return PushPathRejectionFalse(L, "bzfile Error: copy failed: " + NarrowSystemError(copyError));
		}

		DWORD moveFlags = MOVEFILE_WRITE_THROUGH;
		if (overwriteExisting)
		{
			moveFlags |= MOVEFILE_REPLACE_EXISTING;

			// A read-only destination refuses the replace; clear only that bit.
			const DWORD attributes = GetFileAttributesW(destinationPath.c_str());
			if (attributes != INVALID_FILE_ATTRIBUTES && (attributes & FILE_ATTRIBUTE_READONLY) != 0)
			{
				SetFileAttributesW(destinationPath.c_str(), attributes & ~FILE_ATTRIBUTE_READONLY);
			}
		}

		if (!MoveFileExW(temporaryPath.c_str(), destinationPath.c_str(), moveFlags))
		{
			const DWORD moveError = GetLastError();
			DeleteFileW(temporaryPath.c_str());
			if (!overwriteExisting && (moveError == ERROR_ALREADY_EXISTS || moveError == ERROR_FILE_EXISTS))
			{
				return PushPathRejectionFalse(L, "bzfile Error: destination already exists");
			}
			return PushPathRejectionFalse(L, "bzfile Error: copy failed: " + NarrowSystemError(moveError));
		}

		lua_pushboolean(L, 1);
		return 1;
	}

	// Retired. It let a script pick any destination under the game or
	// Workshop roots and have the helper overwrite it once the game exited,
	// with no hash, backup or lock: the arbitrary-destination shape AGENTS.md
	// rules out. Nothing called it. OpenShim updates go through
	// StageOpenShimUpdate / StageOpenShimSuiteUpdate. The export stays so a
	// probing script gets a clear failure instead of calling nil.
	static int ReplaceFileOnExit(lua_State* L)
	{
		lua_pushboolean(L, 0);
		lua_pushstring(L,
			"bzfile Error: ReplaceFileOnExit has been removed; "
			"use StageOpenShimUpdate or StageOpenShimSuiteUpdate");
		return 2;
	}

	namespace
	{
		struct CryptProvider
		{
			HCRYPTPROV handle = 0;
			~CryptProvider() { if (handle) CryptReleaseContext(handle, 0); }
		};

	struct CryptHash
	{
		HCRYPTHASH handle = 0;
		~CryptHash() { if (handle) CryptDestroyHash(handle); }
	};

	bool ComputeSha256(const std::filesystem::path& filePath, std::string& result, std::string& errorMessage)
	{
		std::ifstream input(filePath, std::ios::binary);
		if (!input.is_open())
		{
			errorMessage = "could not open file";
			return false;
		}

		CryptProvider provider;
		if (!CryptAcquireContext(&provider.handle, nullptr, nullptr, PROV_RSA_AES, CRYPT_VERIFYCONTEXT))
		{
			errorMessage = "CryptAcquireContext failed";
			return false;
		}

		CryptHash hash;
		if (!CryptCreateHash(provider.handle, CALG_SHA_256, 0, 0, &hash.handle))
		{
			errorMessage = "CryptCreateHash failed";
			return false;
		}

		std::vector<char> buffer(64 * 1024);
		while (input.good())
		{
			input.read(buffer.data(), static_cast<std::streamsize>(buffer.size()));
			const auto bytesRead = input.gcount();
			if (bytesRead <= 0)
			{
				break;
			}

			if (!CryptHashData(hash.handle, reinterpret_cast<const BYTE*>(buffer.data()), static_cast<DWORD>(bytesRead), 0))
			{
				errorMessage = "CryptHashData failed";
				return false;
			}
		}

		DWORD hashLength = 0;
		DWORD hashLengthSize = sizeof(hashLength);
		if (!CryptGetHashParam(hash.handle, HP_HASHSIZE, reinterpret_cast<BYTE*>(&hashLength), &hashLengthSize, 0))
		{
			errorMessage = "CryptGetHashParam(size) failed";
			return false;
		}

		std::vector<BYTE> hashBytes(hashLength);
		if (!CryptGetHashParam(hash.handle, HP_HASHVAL, hashBytes.data(), &hashLength, 0))
		{
			errorMessage = "CryptGetHashParam(value) failed";
			return false;
		}

		std::ostringstream hex;
		hex << std::hex << std::setfill('0');
		for (BYTE value : hashBytes)
		{
			hex << std::setw(2) << static_cast<unsigned int>(value);
		}

		result = hex.str();
		return true;
	}

	bool IsSha256(const std::string& value)
	{
		if (value.size() != 64)
		{
			return false;
		}

		for (const unsigned char valueCharacter : value)
		{
			if (!std::isxdigit(valueCharacter))
			{
				return false;
			}
		}

		return true;
	}

	bool IsX86PortableExecutable(const std::filesystem::path& filePath, std::string& errorMessage)
	{
		std::ifstream input(filePath, std::ios::binary);
		if (!input.is_open())
		{
			errorMessage = "could not open PE file";
			return false;
		}

		IMAGE_DOS_HEADER dosHeader = {};
		input.read(reinterpret_cast<char*>(&dosHeader), sizeof(dosHeader));
		if (!input || dosHeader.e_magic != IMAGE_DOS_SIGNATURE || dosHeader.e_lfanew <= 0)
		{
			errorMessage = "invalid DOS header";
			return false;
		}

		input.seekg(dosHeader.e_lfanew, std::ios::beg);
		DWORD signature = 0;
		IMAGE_FILE_HEADER fileHeader = {};
		input.read(reinterpret_cast<char*>(&signature), sizeof(signature));
		input.read(reinterpret_cast<char*>(&fileHeader), sizeof(fileHeader));
		if (!input || signature != IMAGE_NT_SIGNATURE)
		{
			errorMessage = "invalid PE signature";
			return false;
		}

		if (fileHeader.Machine != IMAGE_FILE_MACHINE_I386)
		{
			errorMessage = "OpenShim payload is not an x86 PE image";
			return false;
		}

		if ((fileHeader.Characteristics & IMAGE_FILE_DLL) == 0)
		{
			errorMessage = "OpenShim payload is not a DLL";
			return false;
		}

		return true;
	}

	}

	static int GetFileHash(lua_State* L)
	{
		const char* requestedPath = luaL_checkstring(L, 1);
		const char* algorithm = luaL_optstring(L, 2, "sha256");

		if (_stricmp(algorithm, "sha256") != 0)
		{
			return luaL_error(L, "bzfile Error: unsupported hash algorithm \"%s\"", algorithm);
		}

		std::filesystem::path filePath;
		std::string pathError;
		if (!TryResolveAllowedPath(requestedPath, filePath, pathError))
		{
			return PushPathRejectionNil(L, pathError);
		}

		std::string hashValue;
		std::string errorMessage;
		if (!ComputeSha256(filePath, hashValue, errorMessage))
		{
			lua_pushnil(L);
			lua_pushfstring(L, "bzfile Error: %s: \"%s\"", errorMessage.c_str(), DisplayPath(filePath).c_str());
			return 2;
		}

		lua_pushstring(L, hashValue.c_str());
		return 1;
	}

	static int GetFileVersion(lua_State* L)
	{
		const char* requestedPath = luaL_checkstring(L, 1);

		std::filesystem::path filePath;
		std::string pathError;
		if (!TryResolveAllowedPath(requestedPath, filePath, pathError))
		{
			return PushPathRejectionNil(L, pathError);
		}

		DWORD ignored = 0;
		const DWORD versionInfoSize = GetFileVersionInfoSizeW(filePath.c_str(), &ignored);
		if (versionInfoSize == 0)
		{
			lua_pushnil(L);
			lua_pushstring(L, "bzfile Error: file has no readable version information");
			return 2;
		}

		std::vector<BYTE> versionInfo(versionInfoSize);
		if (!GetFileVersionInfoW(filePath.c_str(), 0, versionInfoSize, versionInfo.data()))
		{
			lua_pushnil(L);
			lua_pushstring(L, "bzfile Error: GetFileVersionInfo failed");
			return 2;
		}

		VS_FIXEDFILEINFO* fixedInfo = nullptr;
		UINT fixedInfoSize = 0;
		if (!VerQueryValueW(versionInfo.data(), L"\\", reinterpret_cast<void**>(&fixedInfo), &fixedInfoSize)
			|| fixedInfo == nullptr
			|| fixedInfoSize < sizeof(VS_FIXEDFILEINFO)
			|| fixedInfo->dwSignature != VS_FFI_SIGNATURE)
		{
			lua_pushnil(L);
			lua_pushstring(L, "bzfile Error: invalid fixed file version information");
			return 2;
		}

		std::ostringstream version;
		version
			<< HIWORD(fixedInfo->dwFileVersionMS) << '.'
			<< LOWORD(fixedInfo->dwFileVersionMS) << '.'
			<< HIWORD(fixedInfo->dwFileVersionLS) << '.'
			<< LOWORD(fixedInfo->dwFileVersionLS);
		lua_pushstring(L, version.str().c_str());
		return 1;
	}

	static int StageOpenShimUpdate(lua_State* L)
	{
		const char* requestedSource = luaL_checkstring(L, 1);
		const char* requestedHash = luaL_checkstring(L, 2);

		std::filesystem::path sourcePath;
		std::string pathError;
		if (!TryResolveAllowedPath(requestedSource, sourcePath, pathError))
		{
			return PushPathRejectionFalse(L, pathError);
		}

		std::string expectedHash = requestedHash;
		for (char& hashCharacter : expectedHash)
		{
			hashCharacter = static_cast<char>(std::tolower(static_cast<unsigned char>(hashCharacter)));
		}

		if (!IsSha256(expectedHash))
		{
			lua_pushboolean(L, 0);
			lua_pushstring(L, "invalid expected SHA-256");
			return 2;
		}

		const std::filesystem::path modulePath = GetCurrentModulePath();
		if (modulePath.empty())
		{
			lua_pushboolean(L, 0);
			lua_pushstring(L, "could not resolve bzfile module path");
			return 2;
		}

		const std::filesystem::path moduleDirectory = NormalizePath(modulePath.parent_path());
		if (ToLower(moduleDirectory.filename().wstring()) != L"3686673790")
		{
			lua_pushboolean(L, 0);
			lua_pushstring(L, "OpenShim staging is restricted to Workshop item 3686673790");
			return 2;
		}

		if (!SamePath(sourcePath.parent_path(), moduleDirectory)
			|| ToLower(sourcePath.filename().wstring()) != L"winmm.dll")
		{
			lua_pushboolean(L, 0);
			lua_pushstring(L, "OpenShim source must be winmm.dll beside the loaded bzfile.dll");
			return 2;
		}

		std::string sourceHash;
		std::string validationError;
		if (!ComputeSha256(sourcePath, sourceHash, validationError) || sourceHash != expectedHash)
		{
			lua_pushboolean(L, 0);
			lua_pushfstring(L, "OpenShim source hash validation failed: %s", validationError.empty() ? "hash mismatch" : validationError.c_str());
			return 2;
		}

		if (!IsX86PortableExecutable(sourcePath, validationError))
		{
			lua_pushboolean(L, 0);
			lua_pushfstring(L, "OpenShim PE validation failed: %s", validationError.c_str());
			return 2;
		}

		const std::filesystem::path destinationPath = GetWorkingDirectoryPath() / L"winmm.dll";
		const std::wstring hashPrefix(expectedHash.begin(), expectedHash.begin() + 12);
		const std::filesystem::path stagedPath = moduleDirectory / (L"winmm.dll.pending." + hashPrefix);
		const std::filesystem::path helperPath = moduleDirectory / L"bzfile_replace_helper.exe";
		const std::filesystem::path logPath = GetLogsDirectoryPath() / L"winmm_replace.log";
		const std::filesystem::path backupPath = destinationPath.parent_path() / L"winmm.dll.previous";
		const std::filesystem::path statusPath = destinationPath.parent_path() / L"winmm_update.status";

		std::error_code error;
		if (!std::filesystem::exists(helperPath, error))
		{
			lua_pushboolean(L, 0);
			lua_pushstring(L, "bzfile_replace_helper.exe is missing beside bzfile.dll");
			return 2;
		}

		error.clear();
		std::filesystem::copy_file(sourcePath, stagedPath, std::filesystem::copy_options::overwrite_existing, error);
		if (error)
		{
			lua_pushboolean(L, 0);
			lua_pushfstring(L, "could not stage OpenShim payload: %s", error.message().c_str());
			return 2;
		}

		std::vector<std::wstring> arguments = {
			std::to_wstring(GetCurrentProcessId()),
			stagedPath.wstring(),
			destinationPath.wstring(),
			logPath.wstring(),
			std::wstring(expectedHash.begin(), expectedHash.end()),
			backupPath.wstring(),
			statusPath.wstring()
		};

		{
			std::ofstream status(statusPath, std::ios::trunc);
			if (status.is_open())
			{
				status << "state=staged\nexpected_sha256=" << expectedHash << "\n";
			}
		}

		std::string launchError;
		if (!LaunchHiddenProcess(helperPath.wstring(), arguments, launchError))
		{
			std::filesystem::remove(stagedPath, error);
			lua_pushboolean(L, 0);
			lua_pushfstring(L, "could not launch OpenShim update helper: %s", launchError.c_str());
			return 2;
		}

		lua_pushboolean(L, 1);
		lua_pushstring(L, "staged");
		lua_pushstring(L, DisplayPath(logPath).c_str());
		return 3;
	}

	static int StageOpenShimSuiteUpdate(lua_State* L)
	{
		struct Payload
		{
			std::filesystem::path source;
			std::string expectedHash;
			std::wstring expectedName;
			std::filesystem::path destination;
			std::filesystem::path staged;
			std::filesystem::path backup;
		};

		// Every Lua argument is read before any destructible object exists: a
		// luaL_checkstring type error longjmps and would skip the destructors
		// of the module paths below. For the same reason all three staged
		// paths are resolved before the payload vector is built.
		const char* requestedStaged[3] = {
			luaL_checkstring(L, 1),
			luaL_checkstring(L, 3),
			luaL_checkstring(L, 5)
		};
		const char* requestedHashes[3] = {
			luaL_checkstring(L, 2),
			luaL_checkstring(L, 4),
			luaL_checkstring(L, 6)
		};

		const std::filesystem::path modulePath = GetCurrentModulePath();
		if (modulePath.empty())
		{
			lua_pushboolean(L, 0);
			lua_pushstring(L, "could not resolve bzfile module path");
			return 2;
		}

		const std::filesystem::path moduleDirectory = NormalizePath(modulePath.parent_path());
		if (ToLower(moduleDirectory.filename().wstring()) != L"3686673790")
		{
			lua_pushboolean(L, 0);
			lua_pushstring(L, "OpenShim suite staging is restricted to Workshop item 3686673790");
			return 2;
		}

		std::filesystem::path stagedPaths[3];
		std::string pathError;
		for (int stagedIndex = 0; stagedIndex < 3; ++stagedIndex)
		{
			if (!TryResolveAllowedPath(requestedStaged[stagedIndex], stagedPaths[stagedIndex], pathError))
			{
				return PushPathRejectionFalse(L, pathError);
			}
		}

		const std::filesystem::path gameRoot = GetWorkingDirectoryPath();
		std::vector<Payload> payloads = {
			{
				stagedPaths[0],
				requestedHashes[0],
				L"winmm.dll",
				gameRoot / L"winmm.dll",
				{},
				gameRoot / L"winmm.dll.previous"
			},
			{
				stagedPaths[1],
				requestedHashes[1],
				L"openshim_net.ini.payload",
				gameRoot / L"net.ini",
				{},
				gameRoot / L"net.ini.previous"
			},
			{
				stagedPaths[2],
				requestedHashes[2],
				L"openshim_patches.json.payload",
				gameRoot / L"scripts" / L"patches.json",
				{},
				gameRoot / L"scripts" / L"patches.json.previous"
			}
		};

		for (size_t index = 0; index < payloads.size(); ++index)
		{
			auto& payload = payloads[index];
			for (char& hashCharacter : payload.expectedHash)
			{
				hashCharacter = static_cast<char>(std::tolower(static_cast<unsigned char>(hashCharacter)));
			}

			if (!IsSha256(payload.expectedHash))
			{
				lua_pushboolean(L, 0);
				lua_pushfstring(L, "invalid expected SHA-256 for suite payload %d", static_cast<int>(index + 1));
				return 2;
			}

			if (!SamePath(payload.source.parent_path(), moduleDirectory)
				|| ToLower(payload.source.filename().wstring()) != payload.expectedName)
			{
				lua_pushboolean(L, 0);
				lua_pushfstring(L, "suite payload %d must be %s beside the loaded bzfile.dll",
					static_cast<int>(index + 1),
					ToAnsi(payload.expectedName, nullptr).c_str());
				return 2;
			}

			std::string sourceHash;
			std::string validationError;
			if (!ComputeSha256(payload.source, sourceHash, validationError) || sourceHash != payload.expectedHash)
			{
				lua_pushboolean(L, 0);
				lua_pushfstring(L, "suite payload %d hash validation failed: %s",
					static_cast<int>(index + 1),
					validationError.empty() ? "hash mismatch" : validationError.c_str());
				return 2;
			}

			if (index == 0 && !IsX86PortableExecutable(payload.source, validationError))
			{
				lua_pushboolean(L, 0);
				lua_pushfstring(L, "OpenShim PE validation failed: %s", validationError.c_str());
				return 2;
			}

			const std::wstring hashPrefix(payload.expectedHash.begin(), payload.expectedHash.begin() + 12);
			payload.staged = moduleDirectory /
				(L"openshim_suite_" + std::to_wstring(index + 1) + L".pending." + hashPrefix);
		}

		const std::filesystem::path helperPath = moduleDirectory / L"bzfile_replace_helper.exe";
		const std::filesystem::path logPath = GetLogsDirectoryPath() / L"openshim_update.log";
		const std::filesystem::path statusPath = gameRoot / L"openshim_update.status";
		std::error_code error;
		if (!std::filesystem::exists(helperPath, error))
		{
			lua_pushboolean(L, 0);
			lua_pushstring(L, "bzfile_replace_helper.exe is missing beside bzfile.dll");
			return 2;
		}

		for (size_t index = 0; index < payloads.size(); ++index)
		{
			error.clear();
			std::filesystem::copy_file(
				payloads[index].source,
				payloads[index].staged,
				std::filesystem::copy_options::overwrite_existing,
				error);
			if (error)
			{
				for (size_t cleanupIndex = 0; cleanupIndex <= index; ++cleanupIndex)
				{
					std::error_code cleanupError;
					std::filesystem::remove(payloads[cleanupIndex].staged, cleanupError);
				}
				lua_pushboolean(L, 0);
				lua_pushfstring(L, "could not stage OpenShim suite payload %d: %s",
					static_cast<int>(index + 1), error.message().c_str());
				return 2;
			}
		}

		std::vector<std::wstring> arguments = {
			L"--suite",
			std::to_wstring(GetCurrentProcessId()),
			logPath.wstring(),
			statusPath.wstring()
		};
		for (const auto& payload : payloads)
		{
			arguments.push_back(payload.staged.wstring());
			arguments.push_back(payload.destination.wstring());
			arguments.emplace_back(payload.expectedHash.begin(), payload.expectedHash.end());
			arguments.push_back(payload.backup.wstring());
		}

		{
			std::ofstream status(statusPath, std::ios::trunc);
			if (status.is_open())
			{
				status << "state=staged\nexpected_sha256=" << payloads[0].expectedHash
					<< "\npayload_count=" << payloads.size() << "\n";
			}
		}

		std::string launchError;
		if (!LaunchHiddenProcess(helperPath.wstring(), arguments, launchError))
		{
			for (const auto& payload : payloads)
			{
				std::filesystem::remove(payload.staged, error);
			}
			lua_pushboolean(L, 0);
			lua_pushfstring(L, "could not launch OpenShim suite update helper: %s", launchError.c_str());
			return 2;
		}

		lua_pushboolean(L, 1);
		lua_pushstring(L, "staged");
		lua_pushstring(L, DisplayPath(logPath).c_str());
		return 3;
	}

	static int Delete(lua_State* L)
	{
		const char* requestedPath = luaL_checkstring(L, 1);

		std::filesystem::path path;
		std::string pathError;
		if (!TryResolveAllowedPath(requestedPath, path, pathError))
		{
			return PushPathRejectionFalse(L, pathError);
		}

		// This is remove_all -- a recursive delete. The allowed-root test permits
		// a sandbox root itself (a root is inside itself), and IsWriteProtected
		// only matches leaf file names, so before these two guards existed
		// Delete(GetWorkingDirectory()) would recursively erase the entire game
		// installation. Refuse roots and their ancestors outright, then refuse
		// any directory that still has a protected file somewhere beneath it.
		if (IsSandboxRootOrAncestor(path))
		{
			return PushPathRejectionFalse(
				L,
				"bzfile Error: refusing to delete a game/workshop root or a directory containing one: \""
					+ DisplayPath(path) + "\"");
		}

		if (IsWriteProtected(path))
		{
			return PushPathRejectionFalse(
				L, "bzfile Error: path is write-protected: \"" + DisplayPath(path) + "\"");
		}

		std::error_code error;
		if (std::filesystem::is_directory(path, error) && !error)
		{
			std::error_code scanError;
			std::filesystem::recursive_directory_iterator scan(
				path, std::filesystem::directory_options::skip_permission_denied, scanError);
			const std::filesystem::recursive_directory_iterator scanEnd;
			for (; !scanError && scan != scanEnd; scan.increment(scanError))
			{
				if (IsWriteProtected(scan->path()))
				{
					return PushPathRejectionFalse(
						L,
						"bzfile Error: refusing to recursively delete \"" + DisplayPath(path)
							+ "\" because it contains the write-protected file \""
							+ DisplayPath(scan->path().filename()) + "\"");
				}
			}

			// The scan is what keeps protected files out of the recursive
			// delete, so a scan that could not finish refuses. It used to stop
			// at the first error and let remove_all go ahead.
			if (scanError)
			{
				return PushPathRejectionFalse(
					L,
					"bzfile Error: could not check \"" + DisplayPath(path)
						+ "\" for write-protected files; nothing was deleted: " + scanError.message());
			}
		}
		error.clear();

		bool deleted = std::filesystem::remove_all(path, error) > 0;
		lua_pushboolean(L, !error && deleted);
		if (error)
		{
			lua_pushstring(L, error.message().c_str());
			return 2;
		}
		return 1;
	}

	static int ListDirectory(lua_State* L)
	{
		const char* requestedPath = luaL_checkstring(L, 1);

		std::filesystem::path path;
		std::string pathError;
		if (!TryResolveAllowedPath(requestedPath, path, pathError))
		{
			return PushPathRejectionNil(L, pathError);
		}

		std::error_code error;
		if (!std::filesystem::is_directory(path, error))
		{
			lua_pushnil(L);
			lua_pushfstring(L, "bzfile Error: not a directory or does not exist: \"%s\"", DisplayPath(path).c_str());
			return 2;
		}

		// The error_code increment: the range-for form throws
		// filesystem_error when advancing fails.
		lua_newtable(L);
		int index = 1;
		std::filesystem::directory_iterator entry(path, error);
		const std::filesystem::directory_iterator entryEnd;
		for (; !error && entry != entryEnd; entry.increment(error))
		{
			lua_pushstring(L, DisplayPath(entry->path().filename()).c_str());
			lua_rawseti(L, -2, index++);
		}

		if (error)
		{
			lua_pop(L, 1); // remove table
			lua_pushnil(L);
			lua_pushstring(L, error.message().c_str());
			return 2;
		}

		return 1;
	}

	// Write protection can no longer be switched off from Lua. The old flag
	// disabled it process-wide, for bzfile.dll itself and for Delete's scan,
	// and nothing used it. Both exports stay so a probing script gets a clear
	// answer instead of calling nil.
	static int SetAllowWinmmOverwrite(lua_State* L)
	{
		lua_pushboolean(L, 0);
		lua_pushstring(L, "bzfile Error: write protection cannot be disabled");
		return 2;
	}

	static int GetAllowWinmmOverwrite(lua_State* L)
	{
		lua_pushboolean(L, 0);
		return 1;
	}
}

static int lua_Init(lua_State* L)
{
	// File method table
	lua_newtable(L);
	int fileMethodTable = lua_gettop(L);
	lua_pushvalue(L, fileMethodTable); // the next function will pop this but we still want the table on the stack
	lua_setglobal(L, "_bzfile_impl_file_table");

	lua_pushcfunction(L, &File::Guarded<&File::Write>);
	lua_setfield(L, -2, "Write");

	lua_pushcfunction(L, &File::Guarded<&File::Writeln>);
	lua_setfield(L, -2, "Writeln");

	lua_pushcfunction(L, &File::Guarded<&File::Read>);
	lua_setfield(L, -2, "Read");

	lua_pushcfunction(L, &File::Guarded<&File::Readln>);
	lua_setfield(L, -2, "Readln");

	lua_pushcfunction(L, &File::Guarded<&File::Dump>);
	lua_setfield(L, -2, "Dump");

	lua_pushcfunction(L, &File::Guarded<&File::Flush>);
	lua_setfield(L, -2, "Flush");

	lua_pushcfunction(L, &File::Guarded<&File::Close>);
	lua_setfield(L, -2, "Close");

	// File Metatables

	luaL_newmetatable(L, "FileMetatable");
	lua_pushstring(L, "__gc");
	lua_pushcfunction(L, &File::Guarded<&File::Cleanup>);
	lua_settable(L, -3);

	lua_pushstring(L, "__index");
	lua_pushvalue(L, fileMethodTable);
	lua_settable(L, -3);

	return 0;
}

extern "C" int __declspec(dllexport) luaopen_bzfile(lua_State* L)
{
	// Every binding runs behind File::Guarded, the C++ exception barrier.
	static constexpr luaL_Reg EXPORT[] = {
		{ "Open", &File::Guarded<&File::Open> },
		{ "GetWorkingDirectory", &File::Guarded<&File::GetWorkingDirectory> },
		{ "GetWorkshopDirectory", &File::Guarded<&File::GetWorkshopDirectory> },
		{ "MakeDirectory", &File::Guarded<&File::MakeDirectory> },
		{ "Exists", &File::Guarded<&File::Exists> },
		{ "CopyFile", &File::Guarded<&File::CopyFile> },
		{ "ReplaceFileOnExit", &File::Guarded<&File::ReplaceFileOnExit> },
		{ "GetFileHash", &File::Guarded<&File::GetFileHash> },
		{ "GetFileVersion", &File::Guarded<&File::GetFileVersion> },
		{ "StageOpenShimUpdate", &File::Guarded<&File::StageOpenShimUpdate> },
		{ "StageOpenShimSuiteUpdate", &File::Guarded<&File::StageOpenShimSuiteUpdate> },
		{ "Delete", &File::Guarded<&File::Delete> },
		{ "ListDirectory", &File::Guarded<&File::ListDirectory> },
		{ "SetAllowWinmmOverwrite", &File::Guarded<&File::SetAllowWinmmOverwrite> },
		{ "GetAllowWinmmOverwrite", &File::Guarded<&File::GetAllowWinmmOverwrite> },
		{0, 0}
	};

	lua_Init(L);

	luaL_register(L, "bzfile", EXPORT);
	return 0;
}
