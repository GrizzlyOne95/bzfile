#include <lua.hpp>

#include <Windows.h>
#include <wincrypt.h>

#include <filesystem>
#include <fstream>
#include <cwctype>
#include <string>
#include <system_error>
#include <sstream>
#include <iomanip>
#include <vector>

namespace File
{
	namespace
	{
		bool g_AllowWinmmOverwrite = false;

		std::wstring ToLower(std::wstring value)
		{
			for (auto& ch : value)
			{
				ch = static_cast<wchar_t>(towlower(ch));
			}
			return value;
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

		std::filesystem::path GetWorkingDirectoryPath()
		{
			return NormalizePath(std::filesystem::current_path());
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

		std::filesystem::path GetWorkshopDirectoryPath()
		{
			std::filesystem::path bzrRoot = GetWorkingDirectoryPath();
			std::filesystem::path steamapps = FindSteamAppsDirectory(bzrRoot);
			if (steamapps.empty())
			{
				steamapps = bzrRoot.parent_path().parent_path();
			}
			return NormalizePath(steamapps / "workshop" / "content" / "301650");
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

		std::filesystem::path CheckPathAllowed(lua_State* L, const std::filesystem::path& requestedPath)
		{
			auto normalizedPath = NormalizePath(requestedPath);
			auto workingRoot = GetWorkingDirectoryPath();
			auto workshopRoot = GetWorkshopDirectoryPath();

			if (IsPathInsideRoot(normalizedPath, workingRoot)
				|| (!workshopRoot.empty() && IsPathInsideRoot(normalizedPath, workshopRoot)))
			{
				return normalizedPath;
			}

			luaL_error(
				L,
				"bzfile Error: refusing to access path outside allowed roots. Path: \"%s\"",
				normalizedPath.string().c_str());

			return {};
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

		bool IsWriteProtected(const std::filesystem::path& path)
		{
			if (g_AllowWinmmOverwrite)
			{
				return false;
			}

			auto fileName = ToLower(path.filename().wstring());
			if (fileName == L"winmm.dll" || fileName == L"bzfile.dll")
			{
				return true;
			}

			return false;
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
				CREATE_NO_WINDOW | DETACHED_PROCESS,
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
		std::filesystem::path filePath = CheckPathAllowed(L, luaL_checkstring(L, 1));
		std::string modeStr = luaL_optstring(L, 2, "r");
		std::string options = luaL_optstring(L, 3, "app");

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

		if (isWrite)
		{
			if (IsWriteProtected(filePath))
			{
				return luaL_error(L, "bzfile Error: file is write-protected: \"%s\"", filePath.string().c_str());
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
				return luaL_error(L, "bzfile Error: invalid open option \"%s\"", options.c_str());
			}
		}
		else if (openMode == 0)
		{
			return luaL_error(L, "bzfile Error: invalid open mode \"%s\"", modeStr.c_str());
		}

		void* buffer = lua_newuserdata(L, sizeof(std::fstream));
		std::fstream* fs = new (buffer) std::fstream(filePath, openMode);

		luaL_getmetatable(L, "FileMetatable");
		lua_setmetatable(L, -2);

		if (!fs->is_open())
		{
			lua_pushnil(L);
			lua_pushfstring(L, "bzfile Error: could not open file \"%s\"", filePath.string().c_str());
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

		int count = luaL_optint(L, 2, 1);

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
		else
		{
			std::vector<char> buffer(count);
			handle->read(buffer.data(), count);

			auto bytesRead = handle->gcount();

			if (bytesRead > 0)
			{
				lua_pushlstring(L, buffer.data(), (size_t)bytesRead);
			}
			else
			{
				lua_pushnil(L);
			}
			return 1;
		}
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

		std::string content;
		content.resize(static_cast<size_t>(size));
		handle->read(content.data(), size);

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
		lua_pushstring(L, GetWorkingDirectoryPath().string().c_str());
		return 1;
	}

	static int GetWorkshopDirectory(lua_State* L)
	{
		lua_pushstring(L, GetWorkshopDirectoryPath().string().c_str());
		return 1;
	}

	static int MakeDirectory(lua_State* L)
	{
		std::filesystem::path directory = CheckPathAllowed(L, luaL_checkstring(L, 1));
		std::error_code error;
		std::filesystem::create_directories(directory, error);
		if (error)
		{
			return luaL_error(L, "bzfile Error: MakeDirectory failed: %s", error.message().c_str());
		}
		return 0;
	}

	static int Exists(lua_State* L)
	{
		std::filesystem::path filePath = CheckPathAllowed(L, luaL_checkstring(L, 1));
		std::error_code error;
		bool exists = std::filesystem::exists(filePath, error);

		lua_pushboolean(L, !error && exists);
		return 1;
	}

	static int CopyFile(lua_State* L)
	{
		std::filesystem::path sourcePath = CheckPathAllowed(L, luaL_checkstring(L, 1));
		std::filesystem::path destinationPath = CheckPathAllowed(L, luaL_checkstring(L, 2));

		if (IsWriteProtected(destinationPath))
		{
			lua_pushboolean(L, 0);
			lua_pushfstring(L, "bzfile Error: destination is write-protected: \"%s\"", destinationPath.string().c_str());
			return 2;
		}

		bool overwriteExisting = lua_toboolean(L, 3) != 0;

		std::error_code error;
		auto copyOptions = overwriteExisting
			? std::filesystem::copy_options::overwrite_existing
			: std::filesystem::copy_options::none;

		if (overwriteExisting && std::filesystem::exists(destinationPath, error))
		{
			error.clear();

			// Best-effort force replace for existing shims: clear common blocking
			// attributes and remove the old file before copying the new one in.
			auto destinationWide = destinationPath.wstring();
			SetFileAttributesW(destinationWide.c_str(), FILE_ATTRIBUTE_NORMAL);

			std::error_code removeError;
			if (std::filesystem::remove(destinationPath, removeError))
			{
				copyOptions = std::filesystem::copy_options::none;
			}
			else if (removeError)
			{
				error = removeError;
			}
		}

		bool copied = std::filesystem::copy_file(sourcePath, destinationPath, copyOptions, error);
		lua_pushboolean(L, copied);
		if (!copied)
			{
			auto errorMessage = error ? error.message() : "copy failed";
			lua_pushstring(L, errorMessage.c_str());
			return 2;
		}

		return 1;
	}

	static int ReplaceFileOnExit(lua_State* L)
	{
		std::filesystem::path sourcePath = CheckPathAllowed(L, luaL_checkstring(L, 1));
		std::filesystem::path destinationPath = CheckPathAllowed(L, luaL_checkstring(L, 2));

		if (IsWriteProtected(destinationPath))
		{
			lua_pushboolean(L, 0);
			lua_pushfstring(L, "bzfile Error: destination is write-protected: \"%s\"", destinationPath.string().c_str());
			return 2;
		}

		auto stagedPath = destinationPath;
		stagedPath += ".pending";

		std::error_code error;
		std::filesystem::copy_file(
			sourcePath,
			stagedPath,
			std::filesystem::copy_options::overwrite_existing,
			error);
		if (error)
		{
			lua_pushboolean(L, 0);
			lua_pushstring(L, error.message().c_str());
			return 2;
		}

		const DWORD currentProcessId = GetCurrentProcessId();
		auto modulePath = GetCurrentModulePath();
		if (modulePath.empty())
		{
			std::filesystem::remove(stagedPath, error);
			lua_pushboolean(L, 0);
			lua_pushstring(L, "could not resolve bzfile module path");
			return 2;
		}

		auto helperPath = modulePath.parent_path() / L"bzfile_replace_helper.exe";

		error.clear();
		if (!std::filesystem::exists(helperPath, error))
		{
			std::filesystem::remove(stagedPath, error);
			auto message = "helper executable not found: " + helperPath.string();
			lua_pushboolean(L, 0);
			lua_pushstring(L, message.c_str());
			return 2;
		}

		auto logStem = destinationPath.stem().wstring();
		if (logStem.empty())
		{
			logStem = destinationPath.filename().wstring();
		}
		if (logStem.empty())
		{
			logStem = L"bzfile";
		}

		auto logPath = destinationPath.parent_path() / (logStem + L"_replace.log");
		std::vector<std::wstring> arguments = {
			std::to_wstring(currentProcessId),
			stagedPath.wstring(),
			destinationPath.wstring(),
			logPath.wstring()
		};

		std::string launchError;
		if (!LaunchHiddenProcess(helperPath.wstring(), arguments, launchError))
		{
			std::filesystem::remove(stagedPath, error);
			lua_pushboolean(L, 0);
			lua_pushstring(L, launchError.c_str());
			return 2;
		}

		lua_pushboolean(L, 1);
		return 1;
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
	}

	static int GetFileHash(lua_State* L)
	{
		std::filesystem::path filePath = CheckPathAllowed(L, luaL_checkstring(L, 1));
		const char* algorithm = luaL_optstring(L, 2, "sha256");

		ALG_ID algorithmId = 0;
		if (_stricmp(algorithm, "sha256") == 0)
		{
			algorithmId = CALG_SHA_256;
		}
		else
		{
			luaL_error(L, "bzfile Error: unsupported hash algorithm \"%s\"", algorithm);
			return 0;
		}

		std::ifstream input(filePath, std::ios::binary);
		if (!input.is_open())
		{
			lua_pushnil(L);
			lua_pushfstring(L, "bzfile Error: could not open \"%s\"", filePath.string().c_str());
			return 2;
		}

		CryptProvider provider;
		if (!CryptAcquireContext(&provider.handle, nullptr, nullptr, PROV_RSA_AES, CRYPT_VERIFYCONTEXT))
		{
			lua_pushnil(L);
			lua_pushstring(L, "bzfile Error: CryptAcquireContext failed");
			return 2;
		}

		CryptHash hash;
		if (!CryptCreateHash(provider.handle, algorithmId, 0, 0, &hash.handle))
		{
			lua_pushnil(L);
			lua_pushstring(L, "bzfile Error: CryptCreateHash failed");
			return 2;
		}

		std::vector<char> buffer(64 * 1024);
		while (input.good())
		{
			input.read(buffer.data(), static_cast<std::streamsize>(buffer.size()));
			auto bytesRead = input.gcount();
			if (bytesRead <= 0)
			{
				break;
			}

			if (!CryptHashData(hash.handle, reinterpret_cast<const BYTE*>(buffer.data()), static_cast<DWORD>(bytesRead), 0))
			{
				lua_pushnil(L);
				lua_pushstring(L, "bzfile Error: CryptHashData failed");
				return 2;
			}
		}

		DWORD hashLength = 0;
		DWORD hashLengthSize = sizeof(hashLength);
		if (!CryptGetHashParam(hash.handle, HP_HASHSIZE, reinterpret_cast<BYTE*>(&hashLength), &hashLengthSize, 0))
		{
			lua_pushnil(L);
			lua_pushstring(L, "bzfile Error: CryptGetHashParam(size) failed");
			return 2;
		}

		std::vector<BYTE> hashBytes(hashLength);
		if (!CryptGetHashParam(hash.handle, HP_HASHVAL, hashBytes.data(), &hashLength, 0))
		{
			lua_pushnil(L);
			lua_pushstring(L, "bzfile Error: CryptGetHashParam(value) failed");
			return 2;
		}

		std::ostringstream hex;
		hex << std::hex << std::setfill('0');
		for (BYTE value : hashBytes)
		{
			hex << std::setw(2) << static_cast<unsigned int>(value);
		}

		lua_pushstring(L, hex.str().c_str());
		return 1;
	}

	static int Delete(lua_State* L)
	{
		std::filesystem::path path = CheckPathAllowed(L, luaL_checkstring(L, 1));

		if (IsWriteProtected(path))
		{
			lua_pushboolean(L, 0);
			lua_pushfstring(L, "bzfile Error: path is write-protected: \"%s\"", path.string().c_str());
			return 2;
		}

		std::error_code error;
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
		std::filesystem::path path = CheckPathAllowed(L, luaL_checkstring(L, 1));
		std::error_code error;
		if (!std::filesystem::is_directory(path, error))
		{
			lua_pushnil(L);
			lua_pushfstring(L, "bzfile Error: not a directory or does not exist: \"%s\"", path.string().c_str());
			return 2;
		}

		lua_newtable(L);
		int index = 1;
		for (const auto& entry : std::filesystem::directory_iterator(path, error))
		{
			lua_pushstring(L, entry.path().filename().string().c_str());
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

	static int SetAllowWinmmOverwrite(lua_State* L)
	{
		g_AllowWinmmOverwrite = lua_toboolean(L, 1) != 0;
		return 0;
	}

	static int GetAllowWinmmOverwrite(lua_State* L)
	{
		lua_pushboolean(L, g_AllowWinmmOverwrite);
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

	lua_pushcfunction(L, &File::Write);
	lua_setfield(L, -2, "Write");

	lua_pushcfunction(L, &File::Writeln);
	lua_setfield(L, -2, "Writeln");

	lua_pushcfunction(L, &File::Read);
	lua_setfield(L, -2, "Read");

	lua_pushcfunction(L, &File::Readln);
	lua_setfield(L, -2, "Readln");

	lua_pushcfunction(L, &File::Dump);
	lua_setfield(L, -2, "Dump");

	lua_pushcfunction(L, &File::Flush);
	lua_setfield(L, -2, "Flush");

	lua_pushcfunction(L, &File::Close);
	lua_setfield(L, -2, "Close");

	// File Metatables

	luaL_newmetatable(L, "FileMetatable");
	lua_pushstring(L, "__gc");
	lua_pushcfunction(L, &File::Cleanup);
	lua_settable(L, -3);

	lua_pushstring(L, "__index");
	lua_pushvalue(L, fileMethodTable);
	lua_settable(L, -3);

	return 0;
}

extern "C" int __declspec(dllexport) luaopen_bzfile(lua_State* L)
{
	static constexpr luaL_Reg EXPORT[] = {
		{ "Open", &File::Open },
		{ "GetWorkingDirectory", &File::GetWorkingDirectory },
		{ "GetWorkshopDirectory", &File::GetWorkshopDirectory },
		{ "MakeDirectory", &File::MakeDirectory },
		{ "Exists", &File::Exists },
		{ "CopyFile", &File::CopyFile },
		{ "ReplaceFileOnExit", &File::ReplaceFileOnExit },
		{ "GetFileHash", &File::GetFileHash },
		{ "Delete", &File::Delete },
		{ "ListDirectory", &File::ListDirectory },
		{ "SetAllowWinmmOverwrite", &File::SetAllowWinmmOverwrite },
		{ "GetAllowWinmmOverwrite", &File::GetAllowWinmmOverwrite },
		{0, 0}
	};

	lua_Init(L);

	luaL_register(L, "bzfile", EXPORT);
	return 0;
}
