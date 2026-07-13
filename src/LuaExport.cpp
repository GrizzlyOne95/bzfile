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

		auto logPath = GetLogsDirectoryPath() / (logStem + L"_replace.log");
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

	bool SamePath(const std::filesystem::path& left, const std::filesystem::path& right)
	{
		return ToLower(NormalizePath(left).wstring()) == ToLower(NormalizePath(right).wstring());
	}
	}

	static int GetFileHash(lua_State* L)
	{
		std::filesystem::path filePath = CheckPathAllowed(L, luaL_checkstring(L, 1));
		const char* algorithm = luaL_optstring(L, 2, "sha256");

		if (_stricmp(algorithm, "sha256") != 0)
		{
			luaL_error(L, "bzfile Error: unsupported hash algorithm \"%s\"", algorithm);
			return 0;
		}

		std::string hashValue;
		std::string errorMessage;
		if (!ComputeSha256(filePath, hashValue, errorMessage))
		{
			lua_pushnil(L);
			lua_pushfstring(L, "bzfile Error: %s: \"%s\"", errorMessage.c_str(), filePath.string().c_str());
			return 2;
		}

		lua_pushstring(L, hashValue.c_str());
		return 1;
	}

	static int GetFileVersion(lua_State* L)
	{
		const std::filesystem::path filePath = CheckPathAllowed(L, luaL_checkstring(L, 1));
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
		const std::filesystem::path sourcePath = CheckPathAllowed(L, luaL_checkstring(L, 1));
		std::string expectedHash = luaL_checkstring(L, 2);
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
		lua_pushstring(L, logPath.string().c_str());
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

		const std::filesystem::path gameRoot = GetWorkingDirectoryPath();
		std::vector<Payload> payloads = {
			{
				CheckPathAllowed(L, luaL_checkstring(L, 1)),
				luaL_checkstring(L, 2),
				L"winmm.dll",
				gameRoot / L"winmm.dll",
				{},
				gameRoot / L"winmm.dll.previous"
			},
			{
				CheckPathAllowed(L, luaL_checkstring(L, 3)),
				luaL_checkstring(L, 4),
				L"openshim_net.ini.payload",
				gameRoot / L"net.ini",
				{},
				gameRoot / L"net.ini.previous"
			},
			{
				CheckPathAllowed(L, luaL_checkstring(L, 5)),
				luaL_checkstring(L, 6),
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
					payload.source.filename().string().c_str());
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
		lua_pushstring(L, logPath.string().c_str());
		return 3;
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
		{ "GetFileVersion", &File::GetFileVersion },
		{ "StageOpenShimUpdate", &File::StageOpenShimUpdate },
		{ "StageOpenShimSuiteUpdate", &File::StageOpenShimSuiteUpdate },
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
