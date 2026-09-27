-- bzfile API tests, run by tests/Test-LuaHost.ps1 through bzfile_lua_host.exe.
-- The working directory and the sandbox root are the same scratch folder.

local root = assert(bzfile.GetWorkingDirectory())
local failures = 0

local function check(condition, name)
    if condition then
        print("OK: " .. name)
    else
        failures = failures + 1
        print("FAIL: " .. name)
    end
end

local function path(name)
    return root .. "\\" .. name
end

local function writeRaw(name, content)
    local handle = assert(bzfile.Open(path(name), "wb", "trunc"))
    handle:Write(content)
    handle:Close()
end

local function readRaw(name)
    local handle = bzfile.Open(path(name), "rb")
    if not handle then
        return nil
    end
    local content = handle:Dump()
    handle:Close()
    return content
end

local function refused(ok, message)
    return (ok == nil or ok == false) and type(message) == "string"
end

-- Write protection -------------------------------------------------------

for _, name in ipairs({
    "winmm.dll", "WINMM.DLL", "winmm.dll.", "winmm.dll::$DATA", "bzfile.dll",
    "bzloader.dll", "bzfile_replace_helper.exe", "new_native.dll",
    "net.ini", "patches.json", "winmm.dll.pending.0123456789ab", "net.ini.previous",
}) do
    check(refused(bzfile.Open(path(name), "w", "trunc")), "Open for write refuses " .. name)
end

check(refused(bzfile.MakeDirectory(path("winmm.dll"))), "MakeDirectory refuses a protected name")

writeRaw("plain.txt", "plain")
check(refused(bzfile.CopyFile(path("plain.txt"), path("copied.dll"), true)), "CopyFile refuses a protected destination")

check(bzfile.MakeDirectory(path("holder")) == true, "MakeDirectory creates a directory")
do
    -- Plant a protected file the way an installer would, outside bzfile.
    local planted = assert(io.open(path("holder\\inner.dll"), "wb"))
    planted:write("MZ")
    planted:close()
end
check(refused(bzfile.Delete(path("holder"))), "Delete refuses a directory holding a protected file")
check(bzfile.Exists(path("holder\\inner.dll")) == true, "the protected file survived")

check(refused(bzfile.SetAllowWinmmOverwrite(true)), "SetAllowWinmmOverwrite refuses")
check(bzfile.GetAllowWinmmOverwrite() == false, "GetAllowWinmmOverwrite reports false")
check(refused(bzfile.Open(path("winmm.dll"), "w", "trunc")), "protection holds after SetAllowWinmmOverwrite(true)")

check(refused(bzfile.ReplaceFileOnExit(path("plain.txt"), path("other.txt"))), "ReplaceFileOnExit is retired")

-- Files Campaign Reimagined writes must stay writable.
writeRaw("openshim.ini", "[Player]\n")
check(readRaw("openshim.ini") == "[Player]\n", "openshim.ini stays writable")
writeRaw("openshim_update.status", "state=complete\n")
check(bzfile.Delete(path("openshim_update.status")) == true, "status file stays deletable")

-- CopyFile ----------------------------------------------------------------

writeRaw("keep.txt", "keep me")
check(refused(bzfile.CopyFile(path("missing.txt"), path("keep.txt"), true)), "CopyFile with a missing source fails")
check(readRaw("keep.txt") == "keep me", "a missing source leaves the destination intact")

check(refused(bzfile.CopyFile(path("keep.txt"), path("keep.txt"), true)), "CopyFile onto itself fails")
check(readRaw("keep.txt") == "keep me", "self-copy leaves the file intact")

writeRaw("source.txt", "new content")
check(refused(bzfile.CopyFile(path("source.txt"), path("keep.txt"), false)), "CopyFile without overwrite refuses an existing file")
check(readRaw("keep.txt") == "keep me", "no-overwrite leaves the destination intact")

check(bzfile.CopyFile(path("source.txt"), path("keep.txt"), true) == true, "CopyFile with overwrite replaces")
check(readRaw("keep.txt") == "new content", "overwrite delivers the new content")
check(bzfile.CopyFile(path("source.txt"), path("fresh.txt")) == true, "CopyFile to a new file")
check(readRaw("fresh.txt") == "new content", "new file has the content")
check(bzfile.Exists(path("keep.txt.bzfile-copy")) == false, "no temporary file is left behind")

-- Dump and Read -------------------------------------------------------------

writeRaw("crlf.txt", "line1\r\nline2\r\nline3\r\n")
do
    local handle = assert(bzfile.Open(path("crlf.txt"), "r"))
    local text = handle:Dump()
    handle:Close()
    check(text == "line1\nline2\nline3\n", "text-mode Dump has no NUL padding (got " .. #text .. " bytes)")
end
check(readRaw("crlf.txt") == "line1\r\nline2\r\nline3\r\n", "binary Dump returns the raw bytes")
do
    local handle = assert(bzfile.Open(path("crlf.txt"), "w", "app"))
    check(handle:Dump() == "", "Dump on a write-only handle returns an empty string")
    handle:Close()
end

do
    local handle = assert(bzfile.Open(path("crlf.txt"), "rb"))
    local ok = pcall(handle.Read, handle, 1024 * 1024 * 1024)
    check(not ok, "Read beyond the 64 MiB limit raises an argument error")
    check(handle:Read(1000000) == "line1\r\nline2\r\nline3\r\n", "a large Read returns what the file holds")
    handle:Close()
end

-- Roots -------------------------------------------------------------------

check(bzfile.GetWorkshopDirectory() == "", "no Workshop root outside a Steam library")
check(refused(bzfile.Open(root .. "\\..\\outside.txt", "w", "trunc")), "paths outside the root are refused")
do
    local entries = assert(bzfile.ListDirectory(root))
    local found = false
    for _, name in ipairs(entries) do
        if name == "keep.txt" then
            found = true
        end
    end
    check(found, "ListDirectory lists the root")
end

if failures > 0 then
    error(failures .. " bzfile API check(s) failed")
end
print("All bzfile API checks passed.")
