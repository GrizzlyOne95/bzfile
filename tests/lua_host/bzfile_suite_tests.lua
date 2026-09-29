local root = bzfile.GetWorkingDirectory()
local mod = root .. "\\mods\\3686673790\\"
local names = {"winmm.dll", "openshim_net.ini.payload", "openshim_patches.json.payload", "bzloader.dll", "openshim.dll"}
local args = {}
for _, name in ipairs(names) do
    args[#args + 1] = mod .. name
    args[#args + 1] = assert(bzfile.GetFileHash(mod .. name))
end
args[11] = assert(bzfile.GetFileHash(mod .. "bzfile_replace_helper.exe"))

-- Each rejection must happen before staging or starting the helper.
local function Refused(values, expected)
    local ok, reason = bzfile.StageOpenShimSuiteUpdateV3(unpack(values))
    assert(not ok and tostring(reason):find(expected, 1, true), tostring(reason))
    assert(not bzfile.IsOpenShimUpdateActive())
end
local bad = {}
for i, value in ipairs(args) do bad[i] = value end
bad[11] = string.rep("0", 64)
Refused(bad, "helper")
bad[11] = args[11]
bad[7] = root .. "\\outside.dll"
Refused(bad, "beside the loaded")
bad[7] = args[7]
bad[10] = string.rep("0", 64)
Refused(bad, "hash validation failed")
Refused({args[1]}, "argument count")

local ok, reason = bzfile.StageOpenShimSuiteUpdateV3(unpack(args))
assert(ok, reason)
print("V3 suite staged through real Lua; helper installs after host exit")
