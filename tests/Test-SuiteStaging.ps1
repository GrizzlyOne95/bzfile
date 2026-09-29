[CmdletBinding()]
param(
    [string]$ShimRoot = "",
    [string]$HostPath = "tests\lua_host\bin\bzfile_lua_host.exe",
    [string]$DllPath = "Release\bzfile.dll",
    [string]$HelperPath = "Release\bzfile_replace_helper.exe"
)
$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest
$repo = Split-Path -Parent $PSScriptRoot
$usesRealShim = $ShimRoot -ne ''
if ($usesRealShim) { $ShimRoot = (Resolve-Path -LiteralPath $ShimRoot).Path }
$scratch = Join-Path ([IO.Path]::GetTempPath()) ('bzfile-suite-' + [guid]::NewGuid().ToString('N'))
$mod = Join-Path $scratch 'mods\3686673790'
New-Item -ItemType Directory -Path $mod -Force | Out-Null
$text = ''
try {
    Copy-Item -LiteralPath $HostPath -Destination (Join-Path $scratch 'bzfile_lua_host.exe')
    Copy-Item -LiteralPath $DllPath -Destination (Join-Path $mod 'bzfile.dll')
    Copy-Item -LiteralPath $HelperPath -Destination (Join-Path $mod 'bzfile_replace_helper.exe')
    $plan = @(
        @{Source='bin\Release\winmm.dll'; Bundle='winmm.dll'; Target='winmm.dll'},
        @{Source='net.ini'; Bundle='openshim_net.ini.payload'; Target='net.ini'},
        @{Source='scripts\patches.json'; Bundle='openshim_patches.json.payload'; Target='scripts\patches.json'},
        @{Source='bin\Release\bzloader.dll'; Bundle='bzloader.dll'; Target='bzloader.dll'},
        @{Source='bin\Release\plugins\openshim.dll'; Bundle='openshim.dll'; Target='plugins\openshim.dll'}
    )
    if (-not $usesRealShim) {
        # CI has no sibling build. Real x86 PE fixtures still exercise the
        # constrained bindings, helper trust and five-file transaction.
        $ShimRoot = Join-Path $scratch 'fixture-input'
        foreach ($entry in $plan) {
            $source = Join-Path $ShimRoot $entry.Source
            New-Item -ItemType Directory -Path (Split-Path $source -Parent) -Force | Out-Null
            if ($entry.Source.EndsWith('.dll')) { Copy-Item -LiteralPath $DllPath -Destination $source }
            else { [IO.File]::WriteAllText($source, 'fixture config') }
        }
    }
    foreach ($entry in $plan) {
        Copy-Item -LiteralPath (Join-Path $ShimRoot $entry.Source) -Destination (Join-Path $mod $entry.Bundle)
    }
    foreach ($initialState in @('fresh', 'existing')) {
        if ($initialState -eq 'existing') {
            foreach ($entry in $plan) { [IO.File]::WriteAllText((Join-Path $scratch $entry.Target), 'old payload') }
        }
        & (Join-Path $scratch 'bzfile_lua_host.exe') (Join-Path $repo 'tests\lua_host\bzfile_suite_tests.lua') (Join-Path $mod 'bzfile.dll')
        if ($LASTEXITCODE -ne 0) { throw 'Real Lua suite staging failed.' }
        $status = Join-Path $scratch 'openshim_update.status'
        $deadline = [DateTime]::UtcNow.AddSeconds(45)
        do {
            Start-Sleep -Milliseconds 100
            $text = if (Test-Path -LiteralPath $status) { Get-Content -LiteralPath $status -Raw } else { '' }
            if ($text -match 'state=failed') { throw $text }
        } until ($text -match 'state=complete' -or [DateTime]::UtcNow -gt $deadline)
        if ($text -notmatch 'state=complete') { throw 'Suite helper did not finish.' }
        foreach ($entry in $plan) {
            $target = Join-Path $scratch $entry.Target
            if ((Get-FileHash -LiteralPath $target).Hash -ne (Get-FileHash -LiteralPath (Join-Path $mod $entry.Bundle)).Hash) {
                throw "Installed $($entry.Target) does not match its source."
            }
            if ($initialState -eq 'existing' -and (Get-Content -LiteralPath "$target.previous" -Raw) -ne 'old payload') {
                throw "Backup missing for $($entry.Target)."
            }
        }
        Write-Host "PASS: V3 $initialState install, five exact payloads and constrained API rejections"
    }
} finally {
    # This is a generated fixture, never a game installation. Preserve it on a
    # failure while a helper could still be using it.
    if ($text -match 'state=complete') { Remove-Item -LiteralPath $scratch -Recurse -Force }
    else { Write-Warning "Fixture retained for investigation: $scratch" }
}
