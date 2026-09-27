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

function Start-Helper([string[]]$Arguments) {
    $startInfo = [System.Diagnostics.ProcessStartInfo]::new()
    $startInfo.FileName = $script:helper
    $startInfo.UseShellExecute = $false
    foreach ($argument in $Arguments) {
        $startInfo.ArgumentList.Add([string]$argument)
    }
    $process = [System.Diagnostics.Process]::Start($startInfo)
    if ($null -eq $process) {
        throw "Could not launch replacement helper."
    }
    return $process
}

# The helper waits for the game's process id before promoting. Tests stand in
# with a short-lived process that has already exited, or one that is still
# running when the helper starts.
function Get-ExitedProcessId {
    $process = Start-Process -FilePath "cmd.exe" -ArgumentList "/c", "exit" -WindowStyle Hidden -PassThru -Wait
    return [string]$process.Id
}

function Start-Sleeper([int]$Seconds) {
    return Start-Process -FilePath "cmd.exe" -ArgumentList "/c", "ping -n $($Seconds + 1) 127.0.0.1 >nul" -WindowStyle Hidden -PassThru
}

function Invoke-Helper([string[]]$Arguments) {
    # The helper is linked as a Windows GUI subsystem executable, so invoking it
    # directly from PowerShell does not reliably set $LASTEXITCODE or wait for
    # completion. ProcessStartInfo gives us deterministic waiting and preserves
    # each argument boundary without manual command-line quoting.
    $startInfo = [System.Diagnostics.ProcessStartInfo]::new()
    $startInfo.FileName = $script:helper
    $startInfo.UseShellExecute = $false
    foreach ($argument in $Arguments) {
        $startInfo.ArgumentList.Add([string]$argument)
    }

    $process = [System.Diagnostics.Process]::Start($startInfo)
    if ($null -eq $process) {
        throw "Could not launch replacement helper."
    }
    $process.WaitForExit()
    return $process.ExitCode
}

$helper = [System.IO.Path]::GetFullPath($HelperPath)
if (-not (Test-Path -LiteralPath $helper)) {
    throw "Replacement helper was not built: $helper"
}

$root = Join-Path ([System.IO.Path]::GetTempPath()) ("bzfile-helper-test-" + [guid]::NewGuid().ToString("N"))
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

    $exitCode = Invoke-Helper @((Get-ExitedProcessId), $staged, $destination, $log, $expected, $backup, $status)
    Assert-True ($exitCode -eq 0) "single-file replacement returned exit code $exitCode"
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

    $exitCode = Invoke-Helper @((Get-ExitedProcessId), $badStaged, $badDestination, $badLog, ("0" * 64), $badBackup, $badStatus)
    Assert-True ($exitCode -eq 1) "hash mismatch did not fail"
    Assert-True ((Get-Content -LiteralPath $badDestination -Raw) -eq "keep me") "hash mismatch modified destination"
    Assert-True ((Get-Content -LiteralPath $badStatus -Raw) -match "state=failed") "hash mismatch status did not report failure"

    # Missing staged file must fail closed.
    $missing = Join-Path $root "missing"
    New-Item -ItemType Directory -Path $missing -Force | Out-Null
    $missingStatus = Join-Path $missing "status.txt"
    $missingLog = Join-Path $missing "replace.log"
    $exitCode = Invoke-Helper @((Get-ExitedProcessId), (Join-Path $missing "does-not-exist.bin"), (Join-Path $missing "destination.bin"), $missingLog, ("0" * 64), (Join-Path $missing "backup.bin"), $missingStatus)
    Assert-True ($exitCode -eq 1) "missing staged file did not fail"
    Assert-True ((Get-Content -LiteralPath $missingStatus -Raw) -match "state=failed") "missing staged status did not report failure"

    # Three-file suite success: the campaign updater depends on all three files
    # being promoted as one verified transaction with backups of old payloads.
    $suite = Join-Path $root "suite"
    New-Item -ItemType Directory -Path $suite -Force | Out-Null
    $suiteLog = Join-Path $suite "suite.log"
    $suiteStatus = Join-Path $suite "suite.status"
    $suiteArgs = @("--suite", (Get-ExitedProcessId), $suiteLog, $suiteStatus)
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

    $exitCode = Invoke-Helper $suiteArgs
    Assert-True ($exitCode -eq 0) "suite replacement returned exit code $exitCode"
    Assert-True ((Get-Content -LiteralPath $suiteStatus -Raw) -match "state=complete") "suite status did not reach complete"
    for ($i = 1; $i -le 3; $i++) {
        Assert-True ((Get-Sha (Join-Path $suite "destination$i.bin")) -eq $expectedHashes[$i - 1]) "suite destination $i hash is wrong"
        Assert-True ((Get-Content -LiteralPath (Join-Path $suite "backup$i.bin") -Raw) -eq "old-$i") "suite backup $i is wrong"
    }

    # Waits for a live game, and a second helper steps aside without touching
    # the first one's status or staged file.
    $wait = Join-Path $root "wait"
    New-Item -ItemType Directory -Path $wait -Force | Out-Null
    $waitStaged = Join-Path $wait "staged.bin"
    $waitDestination = Join-Path $wait "destination.bin"
    $waitStatus = Join-Path $wait "status.txt"
    $waitLog = Join-Path $wait "replace.log"
    Write-AsciiFile $waitStaged "waited payload"
    Write-AsciiFile $waitDestination "old payload"
    $waitHash = Get-Sha $waitStaged

    $game = Start-Sleeper 4
    $first = Start-Helper @([string]$game.Id, $waitStaged, $waitDestination, $waitLog, $waitHash, (Join-Path $wait "destination.bak"), $waitStatus)
    Start-Sleep -Milliseconds 1500
    Assert-True (-not $first.HasExited) "helper did not wait for the running game"
    Assert-True ((Get-Content -LiteralPath $waitStatus -Raw) -match "state=waiting_for_exit") "waiting helper status is wrong"
    Assert-True ((Get-Content -LiteralPath $waitDestination -Raw) -eq "old payload") "destination changed while the game was running"

    $otherStaged = Join-Path $wait "other.bin"
    Write-AsciiFile $otherStaged "other payload"
    $exitCode = Invoke-Helper @([string]$game.Id, $otherStaged, $waitDestination, $waitLog, (Get-Sha $otherStaged), (Join-Path $wait "other.bak"), $waitStatus)
    Assert-True ($exitCode -eq 3) "second helper returned $exitCode instead of stepping aside"
    Assert-True ((Get-Content -LiteralPath $waitStatus -Raw) -match "expected_sha256=$waitHash") "second helper overwrote the first helper's status"

    $first.WaitForExit()
    Assert-True ($first.ExitCode -eq 0) "waiting helper returned exit code $($first.ExitCode)"
    Assert-True ((Get-Sha $waitDestination) -eq $waitHash) "waiting helper did not install its payload"
    Assert-True ((Get-Content -LiteralPath $waitStatus -Raw) -match "state=complete") "waiting helper status did not reach complete"

    # Suite rollback: payload 2's destination is locked, so payload 1 is
    # promoted and rolled back, payloads 2 and 3 are never touched, and the
    # rollback reports success. Staged files are cleaned up. (Takes ~30 s:
    # the helper retries a locked destination before giving up.)
    $rollback = Join-Path $root "rollback"
    New-Item -ItemType Directory -Path $rollback -Force | Out-Null
    $rollbackLog = Join-Path $rollback "suite.log"
    $rollbackStatus = Join-Path $rollback "suite.status"
    $rollbackArgs = @("--suite", (Get-ExitedProcessId), $rollbackLog, $rollbackStatus)
    for ($i = 1; $i -le 3; $i++) {
        $rollbackStaged = Join-Path $rollback ("staged$i.bin")
        Write-AsciiFile $rollbackStaged ("new-$i")
        Write-AsciiFile (Join-Path $rollback ("destination$i.bin")) ("old-$i")
        $rollbackArgs += @($rollbackStaged, (Join-Path $rollback ("destination$i.bin")), (Get-Sha $rollbackStaged), (Join-Path $rollback ("backup$i.bin")))
    }

    # Readable (so the backup can be taken) but not replaceable.
    $lock = [System.IO.File]::Open((Join-Path $rollback "destination2.bin"), "Open", "Read", "Read")
    try {
        $exitCode = Invoke-Helper $rollbackArgs
    }
    finally {
        $lock.Dispose()
    }
    Assert-True ($exitCode -eq 1) "suite with a locked destination returned exit code $exitCode"
    for ($i = 1; $i -le 3; $i++) {
        Assert-True ((Get-Content -LiteralPath (Join-Path $rollback "destination$i.bin") -Raw) -eq "old-$i") "rollback left destination $i changed"
        Assert-True (-not (Test-Path -LiteralPath (Join-Path $rollback "staged$i.bin"))) "rollback left staged file $i behind"
    }
    Assert-True ((Get-Content -LiteralPath $rollbackStatus -Raw) -match "previous files restored") "a complete rollback was reported as incomplete"

    # Early exits still leave a terminal status.
    $early = Join-Path $root "early"
    New-Item -ItemType Directory -Path $early -Force | Out-Null
    $earlyStatus = Join-Path $early "suite.status"
    $exitCode = Invoke-Helper @("--suite", (Get-ExitedProcessId), (Join-Path $early "suite.log"), $earlyStatus, "only-one-extra-argument")
    Assert-True ($exitCode -eq 2) "wrong suite argument count returned exit code $exitCode"
    Assert-True ((Get-Content -LiteralPath $earlyStatus -Raw) -match "state=failed") "wrong argument count left no failed status"

    $zeroStaged = Join-Path $early "staged.bin"
    $zeroStatus = Join-Path $early "zero.status"
    Write-AsciiFile $zeroStaged "payload"
    $exitCode = Invoke-Helper @("0", $zeroStaged, (Join-Path $early "destination.bin"), (Join-Path $early "zero.log"), (Get-Sha $zeroStaged), (Join-Path $early "destination.bak"), $zeroStatus)
    Assert-True ($exitCode -eq 1) "process id 0 was accepted"
    Assert-True (-not (Test-Path -LiteralPath (Join-Path $early "destination.bin"))) "process id 0 installed without waiting"

    Write-Host "bzfile replacement helper integration tests passed."
}
finally {
    Remove-Item -LiteralPath $root -Recurse -Force -ErrorAction SilentlyContinue
}
