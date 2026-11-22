# Run all native tests that apply to Windows: test\common through the CMake
# suite (test\CMakeLists.txt + ctest, the common tests' only Windows runner,
# since their per-test scripts are bash), then every test\windows\*_test.ps1.
# Prints a PASS/FAIL/SKIPPED summary like test/support/run_native_tests.sh.
#
#   powershell -ExecutionPolicy Bypass -File test\windows\all.ps1 [-Asan] [-SkipCMake] [-Filter <regex>] [-Config Release]
#
# Platform: Windows x64/arm64. Needs: CMake >= 3.16 and Visual Studio or Build
# Tools with the C++ workload (the Visual Studio generator finds it by
# itself). Without test\CMakeLists.txt the common part is reported SKIPPED;
# without MSVC each script reports SKIPPED. Audio-device tests skip without a
# device.
#   -Asan    forwarded to each *_test.ps1, and -DSOLOUD_TEST_SANITIZE=address to CMake.
#   -SkipCMake  run only the test\windows scripts (CI builds the CMake suite itself).
#   -Filter  only run test\windows scripts (and ctest tests, via -R) whose
#            name matches.
#   -Config  CMake configuration (default Release).
# Logs go to $env:NATIVE_TEST_LOG_DIR (default: a temp dir).
# Verification: NOT YET VERIFIED ON WINDOWS (written on macOS, no Windows host).

param(
    [switch]$Asan,
    [switch]$SkipCMake,
    [string]$Filter = '',
    [string]$Config = 'Release'
)
# Not 'Stop': Windows PowerShell 5.1 turns a redirected native command's
# stderr into errors, and every result here is judged by its exit code.
$ErrorActionPreference = 'Continue'
$RepoRoot = (Resolve-Path (Join-Path $PSScriptRoot '..\..')).Path
Set-Location $RepoRoot

$logDir = $env:NATIVE_TEST_LOG_DIR
if (-not $logDir) { $logDir = Join-Path ([IO.Path]::GetTempPath()) ('soloud_native_tests_' + [Guid]::NewGuid().ToString('N')) }
New-Item -ItemType Directory -Force -Path $logDir | Out-Null

$passed = New-Object System.Collections.Generic.List[string]
$failed = New-Object System.Collections.Generic.List[string]
$skipped = New-Object System.Collections.Generic.List[string]

function Report([string]$Name, [int]$Status, [string]$Log, [double]$Seconds) {
    $line = '{0,-45} ' -f $Name
    $text = if (Test-Path $Log) { Get-Content -Raw $Log } else { '' }
    if ($Status -eq 0 -and $text -match '(?m)^SKIPPED') {
        $script:skipped.Add($Name); Write-Output ($line + ('SKIPPED ({0:N0}s)' -f $Seconds))
    } elseif ($Status -eq 0) {
        $script:passed.Add($Name); Write-Output ($line + ('PASS ({0:N0}s)' -f $Seconds))
    } else {
        $script:failed.Add($Name); Write-Output ($line + ('FAIL ({0:N0}s), log: {1}' -f $Seconds, $Log))
        # The failure lines first: they are often above the last 20 lines.
        $fails = @(Get-Content $Log | Select-String -Pattern 'FAIL|error C\d|warning C\d|ERROR: AddressSanitizer' |
            Select-Object -First 60)
        if ($fails.Count -gt 0) {
            Write-Output '    -- lines with FAIL / compiler errors / sanitizer reports --'
            $fails | ForEach-Object { Write-Output "    $($_.Line)" }
            Write-Output '    -- last 20 lines --'
        }
        Get-Content $Log -Tail 20 | ForEach-Object { Write-Output "    $_" }
    }
}

# 1. test\common through CMake / ctest.
$commonLog = Join-Path $logDir 'common_ctest.log'
$start = Get-Date
if ($SkipCMake) {
    'SKIPPED: -SkipCMake' | Set-Content $commonLog
    Report 'test\common (ctest)' 0 $commonLog 0
} elseif (-not (Test-Path (Join-Path $RepoRoot 'test\CMakeLists.txt'))) {
    'SKIPPED: test\CMakeLists.txt does not exist yet (the CMake suite for test\common)' | Set-Content $commonLog
    Report 'test\common (ctest)' 0 $commonLog 0
} elseif (-not (Get-Command cmake -ErrorAction SilentlyContinue)) {
    'SKIPPED: cmake not found' | Set-Content $commonLog
    Report 'test\common (ctest)' 0 $commonLog 0
} else {
    $build = Join-Path ([IO.Path]::GetTempPath()) ('flutter_soloud_tests_' + $(if ($Asan) { 'asan' } else { $Config.ToLower() }))
    $status = 0
    try {
        $cmakeArgs = @('-S', (Join-Path $RepoRoot 'test'), '-B', $build)
        if ($Asan) { $cmakeArgs += '-DSOLOUD_TEST_SANITIZE=address' }
        & cmake @cmakeArgs *>> $commonLog
        if ($LASTEXITCODE -ne 0) { throw 'cmake configure failed' }
        & cmake --build $build --config $Config --parallel *>> $commonLog
        if ($LASTEXITCODE -ne 0) { throw 'cmake build failed' }
        $ctestArgs = @('--test-dir', $build, '-C', $Config, '--output-on-failure')
        if ($Filter) { $ctestArgs += @('-R', $Filter) }
        & ctest @ctestArgs *>> $commonLog
        $status = $LASTEXITCODE
    } catch {
        Add-Content $commonLog "ERROR: $_"
        $status = 1
    }
    Report 'test\common (ctest)' $status $commonLog ((Get-Date) - $start).TotalSeconds
}

# 2. Windows-only scripts, each in its own PowerShell so a script's exit or
# environment changes (vcvars) can't affect the next.
$shell = (Get-Process -Id $PID).Path
foreach ($script in Get-ChildItem -Path $PSScriptRoot -Filter '*_test.ps1' | Sort-Object Name) {
    $name = 'test\windows\' + $script.BaseName
    if ($Filter -and $script.BaseName -notmatch $Filter) { continue }
    $log = Join-Path $logDir ('windows_' + $script.BaseName + '.log')
    $start = Get-Date
    $scriptArgs = @('-NoProfile', '-ExecutionPolicy', 'Bypass', '-File', $script.FullName)
    if ($Asan) { $scriptArgs += '-Asan' }
    & $shell @scriptArgs *> $log
    Report $name $LASTEXITCODE $log ((Get-Date) - $start).TotalSeconds
}

Write-Output ''
Write-Output ('NATIVE_SUMMARY: passed={0} failed={1} skipped={2} (logs: {3})' -f $passed.Count, $failed.Count, $skipped.Count, $logDir)
if ($failed.Count -gt 0) { exit 1 }
exit 0
