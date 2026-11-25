# The plugin's own C/C++ sources compiled with MSVC the way the build hook
# compiles them (hook/build.dart: /O2 /Zc:inline /fp:fast /EHsc, the same
# defines) plus /W3 /WX: any warning in a plugin file fails. Vendored code
# (src\soloud, src\pffft, miniaudio and the dr_* headers) is compiled with
# warnings off, since it is not ours to change. Then, unless -SkipCMake, the
# CMake suite (test\CMakeLists.txt) is configured, built and run with MSVC's
# AddressSanitizer (-DSOLOUD_TEST_SANITIZE=address); the Docker/Linux CI
# builds it only with mingw-w64 and clang/gcc.
#
#   powershell -ExecutionPolicy Bypass -File test\windows\msvc_build_test.ps1 [-SkipCMake] [-WarningLevel 3|4]
#
# Platform: Windows 10/11 x64/arm64. Needs: Visual Studio or Build Tools with
# the C++ workload (prints SKIPPED without it), CMake >= 3.16 for the second
# part (reported as not run without it). No audio device for the first part;
# the ctest part has the device-free suite only.
#   -SkipCMake     only the /WX compile of the plugin sources.
#   -WarningLevel  /W3 (default, what the build hook gets from cl's default)
#                  or /W4.
# Logs: $env:NATIVE_TEST_LOG_DIR, default a temp dir.
# Verification: NOT VERIFIED ON WINDOWS (written on macOS; MSVC cannot run in
# the Docker images, which use mingw-w64).

param([switch]$SkipCMake, [ValidateSet(3, 4)][int]$WarningLevel = 3)
$ErrorActionPreference = 'Continue'
. (Join-Path $PSScriptRoot '..\support\native_build.ps1')
$RepoRoot = Get-RepoRoot
Set-Location $RepoRoot

if (-not (Initialize-Msvc)) { Skip 'MSVC (cl.exe) not found; install Visual Studio Build Tools with the C++ workload' }

$logDir = $env:NATIVE_TEST_LOG_DIR
if (-not $logDir) { $logDir = Join-Path ([IO.Path]::GetTempPath()) ('msvc_build_test_' + [Guid]::NewGuid().ToString('N')) }
New-Item -ItemType Directory -Force -Path $logDir | Out-Null
$objDir = Join-Path $logDir 'obj'
New-Item -ItemType Directory -Force -Path $objDir | Out-Null

# 1. Plugin sources with warnings as errors.
$defines = @('FLUTTER_PLUGIN_IMPL', 'SIGNALSMITH_USE_PFFFT', 'WITH_MINIAUDIO', 'WITH_NULL',
    'MA_NO_RESOURCE_MANAGER', 'MA_NO_NODE_GRAPH', 'MA_NO_ENGINE', 'MA_NO_GENERATION', 'NDEBUG',
    'NOMINMAX', '_CRT_SECURE_NO_WARNINGS', 'NO_XIPH_LIBS')
$common = @('/nologo', '/c', '/MD', '/O2', '/Zc:inline', '/fp:fast', '/EHsc', '/utf-8', '/std:c++17')
foreach ($d in $defines) { $common += "/D$d" }
foreach ($i in (Get-PluginIncludes)) { $common += ('/I' + (Join-Path $RepoRoot $i)) }

$sources = Expand-Sources (Get-PluginSources)
$failedFiles = New-Object System.Collections.Generic.List[string]
$warned = 0
$index = 0
foreach ($src in $sources) {
    $rel = $src.Substring($RepoRoot.Length + 1)
    $vendored = $rel -like 'src\soloud\*' -or $rel -like 'src\pffft\*' -or $rel -like 'src\filters\signalsmith-*'
    $obj = Join-Path $objDir ('{0:D3}.obj' -f $index)
    $index++
    # A one-element array assigned from `if` collapses to a string, which @splat
    # would pass character by character (/ T P).
    $lang = @(if ([IO.Path]::GetExtension($src) -eq '.c') { '/TC' } else { '/TP' })
    $warn = @(if ($vendored) { '/W0' } else {
        # Vendored headers included from plugin files are not ours: /external
        # keeps their warnings out of /WX.
        @("/W$WarningLevel", '/WX', '/external:W0', ('/external:I' + (Join-Path $RepoRoot 'src\soloud')),
          ('/external:I' + (Join-Path $RepoRoot 'src\pffft')),
          ('/external:I' + (Join-Path $RepoRoot 'src\filters\signalsmith-stretch')),
          ('/external:I' + (Join-Path $RepoRoot 'src\filters\signalsmith-linear')))
    })
    $out = & cl.exe @common @lang @warn "/Fo$obj" $src 2>&1 | Where-Object { $_ -notmatch '^\S+\.(c|cpp)$' }
    if ($LASTEXITCODE -ne 0) {
        $failedFiles.Add($rel)
        Write-Output "FAIL  $rel"
        $out | Select-Object -First 30 | ForEach-Object { Write-Output "      $_" }
    } elseif ($out -match 'warning C') {
        $warned++
    }
}
Write-Output ('{0} sources compiled, {1} failed with /W{2} /WX' -f $sources.Count, $failedFiles.Count, $WarningLevel)
$status = if ($failedFiles.Count -gt 0) { 1 } else { 0 }

# 2. The CMake suite with MSVC AddressSanitizer.
if (-not $SkipCMake) {
    if (-not (Get-Command cmake -ErrorAction SilentlyContinue)) {
        Write-Output 'CMake suite with /fsanitize=address: not run (cmake not found)'
    } elseif (-not (Test-Path (Join-Path $RepoRoot 'test\CMakeLists.txt'))) {
        Write-Output 'CMake suite with /fsanitize=address: not run (no test\CMakeLists.txt)'
    } else {
        $build = Join-Path ([IO.Path]::GetTempPath()) 'flutter_soloud_tests_msvc_asan'
        $cmakeLog = Join-Path $logDir 'cmake_asan.log'
        & cmake -S (Join-Path $RepoRoot 'test') -B $build -DSOLOUD_TEST_SANITIZE=address *>> $cmakeLog
        if ($LASTEXITCODE -eq 0) { & cmake --build $build --config RelWithDebInfo --parallel *>> $cmakeLog }
        if ($LASTEXITCODE -eq 0) {
            & ctest --test-dir $build -C RelWithDebInfo --output-on-failure *>> $cmakeLog
        }
        if ($LASTEXITCODE -ne 0) {
            Write-Output "CMake suite with /fsanitize=address: FAILED, log: $cmakeLog"
            # The failing tests' own output (ctest --output-on-failure) is above the tail.
            Get-Content $cmakeLog | Select-String -Pattern 'FAIL|error C\d|ERROR: AddressSanitizer|SUMMARY: AddressSanitizer' |
                Select-Object -First 40 | ForEach-Object { Write-Output "    $($_.Line)" }
            Get-Content $cmakeLog -Tail 30 | ForEach-Object { Write-Output "    $_" }
            $status = 1
        } else {
            Write-Output 'CMake suite with /fsanitize=address: passed'
        }
    }
}
exit $status
