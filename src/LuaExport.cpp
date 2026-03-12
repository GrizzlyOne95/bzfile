#include <lua.hpp>

#include <Windows.h>

#include <filesystem>
#include <fstream>
#include <cwctype>
#include <system_error>
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
		{0, 0}
	};

	lua_Init(L);

	luaL_register(L, "bzfile", EXPORT);
	return 0;
}
