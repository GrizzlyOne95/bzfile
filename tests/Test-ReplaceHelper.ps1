param(
    [string]$HelperPath = "Release\bzfile_replace_helper.exe"
)

$ErrorActionPreference = "Stop"
Set-StrictMode -Version Latest

function Assert-True([bool]$Condition, [string]$Message) {
    if (-not $Condition) {
        throw $Message
    }
}

function Write-AsciiFile([string]$Path, [string]$Text) {
    [System.IO.File]::WriteAllText($Path, $Text, [System.Text.Encoding]::ASCII)
}

function Get-Sha([string]$Path) {
    return (Get-FileHash -LiteralPath $Path -Algorithm SHA256).Hash.ToLowerInvariant()
}

$helper = [System.IO.Path]::GetFullPath($HelperPath)
if (-not (Test-Path -LiteralPath $helper)) {
    throw "Replacement helper was not built: $helper"
}

$root = Join-Path $env:RUNNER_TEMP ("bzfile-helper-test-" + [guid]::NewGuid().ToString("N"))
New-Item -ItemType Directory -Path $root -Force | Out-Null

try {
    # Hardened single-file success: verify promotion, backup, status and staged removal.
    $single = Join-Path $root "single"
    New-Item -ItemType Directory -Path $single -Force | Out-Null
    $staged = Join-Path $single "staged.bin"
    $destination = Join-Path $single "destination.bin"
    $backup = Join-Path $single "destination.bak"
    $status = Join-Path $single "status.txt"
    $log = Join-Path $single "replace.log"
    Write-AsciiFile $staged "new payload"
    Write-AsciiFile $destination "old payload"
    $expected = Get-Sha $staged

    & $helper "0" $staged $destination $log $expected $backup $status
    Assert-True ($LASTEXITCODE -eq 0) "single-file replacement returned exit code $LASTEXITCODE"
    Assert-True ((Get-Sha $destination) -eq $expected) "single-file destination hash is wrong"
    Assert-True ((Get-Content -LiteralPath $backup -Raw) -eq "old payload") "single-file backup was not preserved"
    Assert-True (-not (Test-Path -LiteralPath $staged)) "single-file staged payload still exists after promotion"
    Assert-True ((Get-Content -LiteralPath $status -Raw) -match "state=complete") "single-file status did not reach complete"

    # Hash mismatch must fail before touching the existing destination.
    $mismatch = Join-Path $root "mismatch"
    New-Item -ItemType Directory -Path $mismatch -Force | Out-Null
    $badStaged = Join-Path $mismatch "staged.bin"
    $badDestination = Join-Path $mismatch "destination.bin"
    $badBackup = Join-Path $mismatch "destination.bak"
    $badStatus = Join-Path $mismatch "status.txt"
    $badLog = Join-Path $mismatch "replace.log"
    Write-AsciiFile $badStaged "tampered payload"
    Write-AsciiFile $badDestination "keep me"

    & $helper "0" $badStaged $badDestination $badLog ("0" * 64) $badBackup $badStatus
    Assert-True ($LASTEXITCODE -eq 1) "hash mismatch did not fail"
    Assert-True ((Get-Content -LiteralPath $badDestination -Raw) -eq "keep me") "hash mismatch modified destination"
    Assert-True ((Get-Content -LiteralPath $badStatus -Raw) -match "state=failed") "hash mismatch status did not report failure"

    # Missing staged file must fail closed.
    $missing = Join-Path $root "missing"
    New-Item -ItemType Directory -Path $missing -Force | Out-Null
    $missingStatus = Join-Path $missing "status.txt"
    $missingLog = Join-Path $missing "replace.log"
    & $helper "0" (Join-Path $missing "does-not-exist.bin") (Join-Path $missing "destination.bin") $missingLog ("0" * 64) (Join-Path $missing "backup.bin") $missingStatus
    Assert-True ($LASTEXITCODE -eq 1) "missing staged file did not fail"
    Assert-True ((Get-Content -LiteralPath $missingStatus -Raw) -match "state=failed") "missing staged status did not report failure"

    # Three-file suite success: the campaign updater depends on all three files
    # being promoted as one verified transaction with backups of old payloads.
    $suite = Join-Path $root "suite"
    New-Item -ItemType Directory -Path $suite -Force | Out-Null
    $suiteLog = Join-Path $suite "suite.log"
    $suiteStatus = Join-Path $suite "suite.status"
    $suiteArgs = @("--suite", "0", $suiteLog, $suiteStatus)
    $expectedHashes = @()
    for ($i = 1; $i -le 3; $i++) {
        $suiteStaged = Join-Path $suite ("staged$i.bin")
        $suiteDestination = Join-Path $suite ("destination$i.bin")
        $suiteBackup = Join-Path $suite ("backup$i.bin")
        Write-AsciiFile $suiteStaged ("new-$i")
        Write-AsciiFile $suiteDestination ("old-$i")
        $suiteHash = Get-Sha $suiteStaged
        $expectedHashes += $suiteHash
        $suiteArgs += @($suiteStaged, $suiteDestination, $suiteHash, $suiteBackup)
    }

    & $helper @suiteArgs
    Assert-True ($LASTEXITCODE -eq 0) "suite replacement returned exit code $LASTEXITCODE"
    Assert-True ((Get-Content -LiteralPath $suiteStatus -Raw) -match "state=complete") "suite status did not reach complete"
    for ($i = 1; $i -le 3; $i++) {
        Assert-True ((Get-Sha (Join-Path $suite "destination$i.bin")) -eq $expectedHashes[$i - 1]) "suite destination $i hash is wrong"
        Assert-True ((Get-Content -LiteralPath (Join-Path $suite "backup$i.bin") -Raw) -eq "old-$i") "suite backup $i is wrong"
    }

    Write-Host "bzfile replacement helper integration tests passed."
}
finally {
    Remove-Item -LiteralPath $root -Recurse -Force -ErrorAction SilentlyContinue
}
