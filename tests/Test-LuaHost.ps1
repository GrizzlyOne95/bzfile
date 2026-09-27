param(
    [string]$HostPath = "tests\lua_host\bin\bzfile_lua_host.exe",
    [string]$DllPath = "Release\bzfile.dll",
    [string]$ScriptPath = "tests\lua_host\bzfile_api_tests.lua"
)

# Runs the bzfile Lua API tests in real Lua: bzfile_lua_host.exe loads
# bzfile.dll the way the game does. bzfile pins its sandbox root to the host
# executable's directory, so everything is copied into a scratch "game" folder.

$ErrorActionPreference = "Stop"
Set-StrictMode -Version Latest

foreach ($path in @($HostPath, $DllPath, $ScriptPath)) {
    if (-not (Test-Path -LiteralPath $path -PathType Leaf)) {
        throw "Missing input: $path"
    }
}

$scratch = Join-Path ([System.IO.Path]::GetTempPath()) ("bzfile-lua-host-" + [guid]::NewGuid().ToString("N"))
New-Item -ItemType Directory -Path $scratch | Out-Null
try {
    Copy-Item -LiteralPath $HostPath -Destination (Join-Path $scratch "bzfile_lua_host.exe")
    Copy-Item -LiteralPath $DllPath -Destination (Join-Path $scratch "bzfile.dll")
    $script = Join-Path $scratch "bzfile_api_tests.lua"
    Copy-Item -LiteralPath $ScriptPath -Destination $script

    Push-Location $scratch
    try {
        & (Join-Path $scratch "bzfile_lua_host.exe") $script
        $exitCode = $LASTEXITCODE
    }
    finally {
        Pop-Location
    }

    if ($exitCode -ne 0) {
        throw "bzfile Lua API tests failed (exit $exitCode)."
    }
    Write-Host "bzfile Lua API tests passed."

    # bzfile must refuse to load when the game's Lua dummynode is not where
    # its Lua core expects it.
    Push-Location $scratch
    try {
        & (Join-Path $scratch "bzfile_lua_host.exe") --expect-unsupported-build
        $exitCode = $LASTEXITCODE
    }
    finally {
        Pop-Location
    }
    if ($exitCode -ne 0) {
        throw "bzfile loaded on a game build whose Lua dummynode does not match (exit $exitCode)."
    }
    Write-Host "bzfile refuses an unsupported game build."
}
finally {
    Remove-Item -LiteralPath $scratch -Recurse -Force -ErrorAction SilentlyContinue
}
