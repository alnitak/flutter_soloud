# Output-device swap regression test (miniaudio_changeDevice) on Windows:
# builds test\common\change_device_test.cpp against the real miniaudio backend
# (WASAPI) and runs it. The same source runs on macOS and Linux
# (test\macos\change_device_test.sh, test\linux\change_device_test.sh).
#
#   powershell -ExecutionPolicy Bypass -File test\windows\change_device_test.ps1 [-Asan]
#
# Platform: Windows x64/arm64. Needs: Visual Studio or Build Tools with the C++
# workload, and an output device: without one (Windows Server or CI with no
# audio endpoint) the test prints SKIPPED and exits 0. No assets.
#   -Asan  build with /fsanitize=address.
# Verification: NOT YET VERIFIED ON WINDOWS (written on macOS; the source runs
# there through test/macos/change_device_test.sh).

param([switch]$Asan)
$ErrorActionPreference = 'Stop'
. (Join-Path $PSScriptRoot '..\support\native_build.ps1')
$RepoRoot = Get-RepoRoot
Set-Location $RepoRoot

if (-not (Initialize-Msvc)) { Skip 'MSVC (cl.exe) not found; install Visual Studio Build Tools with the C++ workload' }

$exe = Join-Path ([IO.Path]::GetTempPath()) ($(if ($Asan) { 'change_device_test_asan.exe' } else { 'change_device_test.exe' }))
Invoke-NativeBuild -Out $exe -Asan:$Asan `
    -Defines @('WITH_MINIAUDIO', 'NO_XIPH_LIBS') `
    -Includes @('src\soloud\include', 'src', 'src\pffft') `
    -Sources @(
        'test\common\change_device_test.cpp',
        'src\soloud_common.cpp',
        'src\analyzer.cpp',
        'src\soloud\src\core\*.cpp',
        'src\soloud\src\backend\miniaudio\soloud_miniaudio.cpp',
        'src\mixeroutput\*.cpp',
        'src\pffft\pffft.c') `
    -Libs @('ole32.lib', 'user32.lib', 'advapi32.lib')

& $exe
exit $LASTEXITCODE
