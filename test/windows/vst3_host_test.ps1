# Build and run the standalone native VST3 host tests on Windows:
# test\common\vst3_host_test.cpp (the tests are in
# test\support\vst3_host_checks.h) with src\vst3\vst3_host.cpp and the Windows
# editor (src\vst3\vst3_editor_windows.cpp), against the mock plug-in and
# editor helper in test\support\, laid out as Windows .vst3 bundles
# (Contents\<arch>-win\<name>.vst3).
#
#   powershell -ExecutionPolicy Bypass -File test\windows\vst3_host_test.ps1 [-Asan] [-Test <name>]
#   tests: params instrument editor remote-editor exit
#
# Platform: Windows x64/arm64. Needs: Visual Studio or Build Tools with the C++
# workload, and the prebuilt VST3 hosting SDK the build hook downloads when
# VST3 is enabled (the `enable_vst3` user define):
# .dart_tool\flutter_soloud\vst3\prebuild\{include,windows\<x64|arm64>\vst3_hosting.lib},
# or $env:VST3_PREBUILD_DIR. Prints SKIPPED without either. No audio device,
# no assets.
#   -Asan  build with /fsanitize=address.
# Verification: NOT YET VERIFIED ON WINDOWS (written on macOS, where the same
# test source passes against the macOS host; the test, mock and helper
# sources were syntax-checked with mingw-w64).

param([switch]$Asan, [string]$Test = '')
$ErrorActionPreference = 'Stop'
. (Join-Path $PSScriptRoot '..\support\native_build.ps1')
$RepoRoot = Get-RepoRoot
Set-Location $RepoRoot

$arch = 'x64'; $archDir = 'x86_64-win'
if ($env:PROCESSOR_ARCHITECTURE -eq 'ARM64') { $arch = 'arm64'; $archDir = 'arm64-win' }

$prebuild = $env:VST3_PREBUILD_DIR
if (-not $prebuild) { $prebuild = Join-Path $RepoRoot '.dart_tool\flutter_soloud\vst3\prebuild' }
$sdkInclude = Join-Path $prebuild 'include'
$sdkLib = Join-Path $prebuild "windows\$arch\vst3_hosting.lib"
if (-not (Test-Path (Join-Path $sdkInclude 'pluginterfaces')) -or -not (Test-Path $sdkLib)) {
    Skip "VST3 hosting SDK not found ($sdkLib). Build the example app once with enable_vst3, or set VST3_PREBUILD_DIR."
}
if (-not (Initialize-Msvc)) { Skip 'MSVC (cl.exe) not found; install Visual Studio Build Tools with the C++ workload' }

$work = Join-Path ([IO.Path]::GetTempPath()) ($(if ($Asan) { 'vst3_host_test_win_asan' } else { 'vst3_host_test_win' }))
if (Test-Path $work) { Remove-Item -Recurse -Force $work }
New-Item -ItemType Directory -Path $work | Out-Null

# The mock plug-in, as a Windows .vst3 bundle (a DLL named .vst3).
$bundleBin = Join-Path $work "MockVst3.vst3\Contents\$archDir"
New-Item -ItemType Directory -Force -Path $bundleBin | Out-Null
Invoke-NativeBuild -Out (Join-Path $bundleBin 'MockVst3.vst3') -Dll -Asan:$Asan -Runtime 'MT' `
    -Includes @('test\support', $sdkInclude) `
    -Sources @('test\support\vst3_mock_plugin.cpp') `
    -Libs @($sdkLib, 'ole32.lib')

# The same plug-in in a bundle that embeds a Flutter app (the marker the
# Windows editor looks for), whose editor the host opens in the helper.
$remoteBin = Join-Path $work "MockVst3Remote.vst3\Contents\$archDir"
New-Item -ItemType Directory -Force -Path (Join-Path $remoteBin 'data\flutter_assets') | Out-Null
Copy-Item (Join-Path $bundleBin 'MockVst3.vst3') (Join-Path $remoteBin 'MockVst3Remote.vst3')

$helper = Join-Path $work 'vst3_mock_editor_helper.exe'
Invoke-NativeBuild -Out $helper `
    -Defines @('SOLOUD_ENABLE_VST3') -Includes @('src') `
    -Sources @('test\support\vst3_mock_editor_helper.cpp')

# SOLOUD_VST3_HELPER_PATH is a string literal: forward slashes, so nothing in
# it reads as an escape.
$defines = Join-Path $work 'vst3_test_defines.h'
"#define SOLOUD_VST3_HELPER_PATH `"$($helper -replace '\\', '/')`"" | Set-Content -Encoding ASCII $defines

$exe = Join-Path $work 'vst3_host_test.exe'
Invoke-NativeBuild -Out $exe -Asan:$Asan -Runtime 'MT' `
    -Defines @('SOLOUD_ENABLE_VST3') -ForceIncludes @($defines) `
    -Includes @('src', 'test\support', $sdkInclude) `
    -Sources @(
        'test\common\vst3_host_test.cpp',
        'src\vst3\vst3_host.cpp',
        'src\vst3\vst3_editor_windows.cpp') `
    -Libs @($sdkLib, 'ole32.lib', 'user32.lib', 'gdi32.lib', 'shell32.lib')

$testArgs = @($work)
if ($Test) { $testArgs += $Test }
& $exe @testArgs
exit $LASTEXITCODE
