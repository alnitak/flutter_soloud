# The common native decoder tests (test/common/native_decoder_test.cpp) on
# Windows, built against the Media Foundation decoders
# (src/native_decoder/os_decoder_windows.cpp,
# src/audiobuffer/aac_stream_decoder_windows.cpp).
#
#   powershell -ExecutionPolicy Bypass -File test\windows\native_decoder_test.ps1 [-Asan]
#
# Platform: Windows x64/arm64. Needs: Visual Studio or Build Tools with the C++
# workload (found with vswhere, or cl.exe on PATH); prints SKIPPED without it.
# Reads example\assets\audio\sample-AAC.aac (skips that check if missing).
# No audio device.
#   -Asan  build with /fsanitize=address.
# The Media Foundation decode paths themselves are exercised by
# test\windows\os_decoder_windows_test.ps1.
# Verification: NOT YET VERIFIED ON WINDOWS (written on macOS; the sources
# were cross-compiled with mingw-w64 there, never run).

param([switch]$Asan)
$ErrorActionPreference = 'Stop'
. (Join-Path $PSScriptRoot '..\support\native_build.ps1')
$RepoRoot = Get-RepoRoot
Set-Location $RepoRoot

if (-not (Initialize-Msvc)) { Skip 'MSVC (cl.exe) not found; install Visual Studio Build Tools with the C++ workload' }

$exe = Join-Path ([IO.Path]::GetTempPath()) ($(if ($Asan) { 'native_decoder_test_asan.exe' } else { 'native_decoder_test.exe' }))
Invoke-NativeBuild -Out $exe -Asan:$Asan `
    -Includes @('src', 'src\soloud\include', 'src\native_decoder', 'src\audiobuffer', 'src\soloud\src\audiosource\wav') `
    -Sources @(
        'test\common\native_decoder_test.cpp',
        'src\audiobuffer\aac_stream_decoder.cpp',
        'src\audiobuffer\aac_stream_decoder_windows.cpp',
        'src\native_decoder\os_decoder_windows.cpp',
        'src\native_stream\m4a_gapless.cpp',
        'src\audiobuffer\mp3_stream_decoder.cpp',
        'src\audiobuffer\m4a_metadata.cpp') `
    -Libs @('mfplat.lib', 'mfreadwrite.lib', 'mfuuid.lib', 'shlwapi.lib', 'ole32.lib', 'uuid.lib')

# The source opens example/assets/audio/... relative to the repo root.
& $exe
exit $LASTEXITCODE
