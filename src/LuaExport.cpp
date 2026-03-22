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

		std::filesystem::path CheckWritePathAllowed(lua_State* L, const std::filesystem::path& requestedPath)
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
				"bzfile Error: refusing to write outside allowed roots. Path: \"%s\"",
				normalizedPath.string().c_str());
			return normalizedPath;
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
		std::string fileName = luaL_checkstring(L, 1);
		std::string mode = luaL_optstring(L, 2, "r");
		std::string options = luaL_optstring(L, 3, "app");

		std::ios_base::openmode option{};
		if (mode == "r")
		{
			option = std::ios::in;
		}
		else if (mode == "w")
		{
			fileName = CheckWritePathAllowed(L, fileName).string();
			option = std::ios::out;
			if (options == "app")
			{
				option |= std::ios::app;
			}
			else if (options == "trunc")
			{
				option |= std::ios::trunc;
			}
			else
			{
				lua_pop(L, -1);
				luaL_error(L, "bzfile Error: invalid open option \"%s\"", options.c_str());
			}
		}
		else
		{
			lua_pop(L, -1);
			luaL_error(L, "bzfile Error: invalid open mode \"%s\"", mode.c_str());
		}

		void* buffer = lua_newuserdata(L, sizeof(std::fstream)); // allocate memory in lua
		new (buffer) std::fstream(fileName, option); // emplace fstream object in lua's memory

		luaL_getmetatable(L, "FileMetatable");
		lua_setmetatable(L, -2);

		return 1;
	}

	static int Cleanup(lua_State* L)
	{
		std::fstream* handle = (std::fstream*)lua_touserdata(L, 1);
		handle->~basic_fstream(); // need to call destructor manually since lua is managing the memory
		return 0;
	}

	static int Write(lua_State* L)
	{
		std::fstream* handle = (std::fstream*)lua_touserdata(L, 1);

		const char* content = luaL_checkstring(L, 2);
		*handle << content;

		// This is equivalent to return *this
		// allows for method chaining on the lua side
		lua_pushvalue(L, 1);
		return 1;
	}

	static int Writeln(lua_State* L)
	{
		std::fstream* handle = (std::fstream*)lua_touserdata(L, 1);

		const char* content = luaL_checkstring(L, 2);
		*handle << content << '\n';

		lua_pushvalue(L, 1);
		return 1;
	}

	static int Read(lua_State* L)
	{
		std::fstream* handle = (std::fstream*)lua_touserdata(L, 1);
		int count = luaL_optint(L, 2, 1);

#ifdef _DEBUG
		if (!lua_isuserdata(L, 1))
		{
			lua_pop(L, -1);
			luaL_error(L, "not userdata");
		}

		if (!handle->is_open())
		{
			lua_pop(L, -1);
			luaL_error(L, "not open");
		}
#endif

		if (handle->eof())
		{
			lua_pushnil(L);
			return 1;
		}

		if (count <= 1)
		{
			char c;
			handle->get(c);

			lua_pushlstring(L, &c, 1);
			return 1;
		}
		else
		{
			std::vector<char> buffer(count);
			handle->read(buffer.data(), count);

			auto bytesRead = handle->gcount();

			if (bytesRead <= 0)
			{
				lua_pop(L, -1);
				luaL_error(L, "zero bytes read");
			}

			lua_pushlstring(L, buffer.data(), (size_t)bytesRead);
			return 1;
		}
	}

	static int Readln(lua_State* L)
	{
		std::fstream* handle = (std::fstream*)lua_touserdata(L, 1);

#ifdef _DEBUG
		if (!lua_isuserdata(L, 1))
		{
			lua_pop(L, -1);
			luaL_error(L, "not userdata");
		}

		if (!handle->is_open())
		{
			lua_pop(L, -1);
			luaL_error(L, "not open");
		}
#endif

		if (handle->eof())
		{
			lua_pushnil(L);
			return 1;
		}

		std::string line;
		std::getline(*handle, line);

		lua_pushstring(L, line.c_str());
		return 1;
	}

	static int Dump(lua_State* L)
	{
		std::fstream* handle = (std::fstream*)lua_touserdata(L, 1);

#ifdef _DEBUG
		if (!lua_isuserdata(L, 1))
		{
			lua_pop(L, -1);
			luaL_error(L, "not userdata");
		}

		if (!handle->is_open())
		{
			lua_pop(L, -1);
			luaL_error(L, "not open");
		}
#endif

		handle->clear();
		handle->seekg(0, std::ios::beg);

		std::string content;
		std::string line;

		while (std::getline(*handle, line))
		{
			content += line + '\n';
		}

		lua_pushstring(L, content.c_str());
		return 1;
	}

	static int Flush(lua_State* L)
	{
		std::fstream* handle = (std::fstream*)lua_touserdata(L, 1);
		handle->flush();

		lua_pushvalue(L, 1);
		return 1;
	}

	// This will make the file handle nil in lua
	static int Close(lua_State* L)
	{
		std::fstream* handle = (std::fstream*)lua_touserdata(L, 1);
		handle->close();
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
		const char* directory = luaL_checkstring(L, 1);
		std::filesystem::create_directories(CheckWritePathAllowed(L, directory));
		return 0;
	}

	static int Exists(lua_State* L)
	{
		const char* filePath = luaL_checkstring(L, 1);
		std::error_code error;
		auto normalizedPath = NormalizePath(filePath);
		bool exists = std::filesystem::exists(normalizedPath, error);

		lua_pushboolean(L, !error && exists);
		return 1;
	}

		static int CopyFile(lua_State* L)
		{
			const char* sourcePath = luaL_checkstring(L, 1);
			const char* destinationPath = luaL_checkstring(L, 2);
			bool overwriteExisting = lua_toboolean(L, 3) != 0;

			auto normalizedSource = NormalizePath(sourcePath);
			auto normalizedDestination = CheckWritePathAllowed(L, destinationPath);

			std::error_code error;
			auto copyOptions = overwriteExisting
				? std::filesystem::copy_options::overwrite_existing
				: std::filesystem::copy_options::none;

			if (overwriteExisting && std::filesystem::exists(normalizedDestination, error))
			{
				error.clear();

				// Best-effort force replace for existing shims: clear common blocking
				// attributes and remove the old file before copying the new one in.
				auto destinationWide = normalizedDestination.wstring();
				SetFileAttributesW(destinationWide.c_str(), FILE_ATTRIBUTE_NORMAL);

				std::error_code removeError;
				if (std::filesystem::remove(normalizedDestination, removeError))
				{
					copyOptions = std::filesystem::copy_options::none;
				}
				else if (removeError)
				{
					error = removeError;
				}
			}

			bool copied = std::filesystem::copy_file(normalizedSource, normalizedDestination, copyOptions, error);
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
		const char* sourcePath = luaL_checkstring(L, 1);
		const char* destinationPath = luaL_checkstring(L, 2);

		auto normalizedSource = NormalizePath(sourcePath);
		auto normalizedDestination = CheckWritePathAllowed(L, destinationPath);
		auto stagedPath = normalizedDestination;
		stagedPath += ".pending";

		std::error_code error;
		std::filesystem::copy_file(
			normalizedSource,
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

		auto logStem = normalizedDestination.stem().wstring();
		if (logStem.empty())
		{
			logStem = normalizedDestination.filename().wstring();
		}
		if (logStem.empty())
		{
			logStem = L"bzfile";
		}

		auto logPath = normalizedDestination.parent_path() / (logStem + L"_replace.log");
		std::vector<std::wstring> arguments = {
			std::to_wstring(currentProcessId),
			stagedPath.wstring(),
			normalizedDestination.wstring(),
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

	static int GetFileHash(lua_State* L)
	{
		const char* filePath = luaL_checkstring(L, 1);
		const char* algorithm = luaL_optstring(L, 2, "sha256");
		auto normalizedPath = NormalizePath(filePath);

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

		std::ifstream input(normalizedPath, std::ios::binary);
		if (!input.is_open())
		{
			lua_pushnil(L);
			lua_pushfstring(L, "could not open \"%s\"", normalizedPath.string().c_str());
			return 2;
		}

		HCRYPTPROV provider = 0;
		HCRYPTHASH hashHandle = 0;
		if (!CryptAcquireContext(&provider, nullptr, nullptr, PROV_RSA_AES, CRYPT_VERIFYCONTEXT))
		{
			lua_pushnil(L);
			lua_pushstring(L, "CryptAcquireContext failed");
			return 2;
		}

		if (!CryptCreateHash(provider, algorithmId, 0, 0, &hashHandle))
		{
			CryptReleaseContext(provider, 0);
			lua_pushnil(L);
			lua_pushstring(L, "CryptCreateHash failed");
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

			if (!CryptHashData(hashHandle, reinterpret_cast<const BYTE*>(buffer.data()), static_cast<DWORD>(bytesRead), 0))
			{
				CryptDestroyHash(hashHandle);
				CryptReleaseContext(provider, 0);
				lua_pushnil(L);
				lua_pushstring(L, "CryptHashData failed");
				return 2;
			}
		}

		DWORD hashLength = 0;
		DWORD hashLengthSize = sizeof(hashLength);
		if (!CryptGetHashParam(hashHandle, HP_HASHSIZE, reinterpret_cast<BYTE*>(&hashLength), &hashLengthSize, 0))
		{
			CryptDestroyHash(hashHandle);
			CryptReleaseContext(provider, 0);
			lua_pushnil(L);
			lua_pushstring(L, "CryptGetHashParam(size) failed");
			return 2;
		}

		std::vector<BYTE> hashBytes(hashLength);
		if (!CryptGetHashParam(hashHandle, HP_HASHVAL, hashBytes.data(), &hashLength, 0))
		{
			CryptDestroyHash(hashHandle);
			CryptReleaseContext(provider, 0);
			lua_pushnil(L);
			lua_pushstring(L, "CryptGetHashParam(value) failed");
			return 2;
		}

		CryptDestroyHash(hashHandle);
		CryptReleaseContext(provider, 0);

		std::ostringstream hex;
		hex << std::hex << std::setfill('0');
		for (BYTE value : hashBytes)
		{
			hex << std::setw(2) << static_cast<unsigned int>(value);
		}

		lua_pushstring(L, hex.str().c_str());
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
		{0, 0}
	};

	lua_Init(L);

	luaL_register(L, "bzfile", EXPORT);
	return 0;
}
