# The in-process Windows VST3 editor window (src\vst3\vst3_editor_windows.cpp):
# open/close/reopen, plug-in initiated resize, the close button (WM_CLOSE),
# teardown while open, the editor opened from DPI-unaware, system-aware and
# per-monitor-v2 threads (window DPI and setContentScaleFactor reported), and
# a view without HWND support. Builds test\windows\vst3_editor_windows_test.cpp
# with a fake IPlugView.
#
#   powershell -ExecutionPolicy Bypass -File test\windows\vst3_editor_windows_test.ps1 [-Asan]
#
# Platform: Windows 10/11 x64/arm64, in an interactive desktop session (it
# opens windows for a few hundred milliseconds). Needs: Visual Studio or
# Build Tools with the C++ workload and the prebuilt VST3 hosting SDK
# (.dart_tool\flutter_soloud\vst3\prebuild\{include,windows\<x64|arm64>\vst3_hosting.lib},
# or $env:VST3_PREBUILD_DIR, see vst3_host_test.ps1). Prints SKIPPED without
# them. No audio device, no assets.
#   -Asan  build with /fsanitize=address.
# Verification: NOT VERIFIED ON WINDOWS. Cross-compiled with mingw-w64 and run
# under Wine + Xvfb (test/docker/windows_native.sh, which links
# test\windows\vst3_sdk_mingw_stubs.cpp instead of the MSVC-only SDK library).

param([switch]$Asan)
$ErrorActionPreference = 'Stop'
. (Join-Path $PSScriptRoot '..\support\native_build.ps1')
$RepoRoot = Get-RepoRoot
Set-Location $RepoRoot

$arch = 'x64'
if ($env:PROCESSOR_ARCHITECTURE -eq 'ARM64') { $arch = 'arm64' }
$prebuild = $env:VST3_PREBUILD_DIR
if (-not $prebuild) { $prebuild = Join-Path $RepoRoot '.dart_tool\flutter_soloud\vst3\prebuild' }
$sdkInclude = Join-Path $prebuild 'include'
$sdkLib = Join-Path $prebuild "windows\$arch\vst3_hosting.lib"
if (-not (Test-Path (Join-Path $sdkInclude 'pluginterfaces')) -or -not (Test-Path $sdkLib)) {
    Skip "VST3 hosting SDK not found ($sdkLib). Build the example app once with enable_vst3, or set VST3_PREBUILD_DIR."
}
if (-not (Initialize-Msvc)) { Skip 'MSVC (cl.exe) not found; install Visual Studio Build Tools with the C++ workload' }

$exe = Join-Path ([IO.Path]::GetTempPath()) ($(if ($Asan) { 'vst3_editor_windows_test_asan.exe' } else { 'vst3_editor_windows_test.exe' }))
Invoke-NativeBuild -Out $exe -Asan:$Asan `
    -Defines @('SOLOUD_ENABLE_VST3') `
    -Includes @('src', $sdkInclude) `
    -Sources @('test\windows\vst3_editor_windows_test.cpp', 'src\vst3\vst3_editor_windows.cpp') `
    -Libs @($sdkLib, 'ole32.lib', 'user32.lib', 'gdi32.lib', 'shell32.lib')

& $exe
exit $LASTEXITCODE
