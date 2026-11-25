# The streaming AAC decoder on Windows (AACDecoderWrapper ->
# src\audiobuffer\aac_stream_decoder_windows.cpp, Media Foundation AAC MFT):
# tiny and frame-aligned chunks, streams of 1-2 ADTS frames, end of data,
# create / feed / destroy on different threads (fix P9), parallel decoders,
# HE-AAC (SBR, embedded fixture test\windows\he_aac_fixture.h), junk between
# frames. Builds and runs test\windows\aac_stream_decoder_windows_test.cpp.
#
#   powershell -ExecutionPolicy Bypass -File test\windows\aac_stream_decoder_windows_test.ps1 [-Asan]
#
# Platform: Windows 10/11 x64/arm64 (Media Foundation; prints SKIPPED where
# the AAC MFT is missing, e.g. N editions without the Media Feature Pack).
# Needs: Visual Studio or Build Tools with the C++ workload; prints SKIPPED
# without it. Fixtures from example\assets\audio. No audio device.
#   -Asan  build with /fsanitize=address.
# Known src/ bugs are reported as KNOWN BUG and do not fail the run; set
# OSDEC_STRICT=1 to count them.
# Verification: NOT VERIFIED ON WINDOWS. Cross-compiled with mingw-w64 and run
# under Wine (test/docker/windows_native.sh); the test logic was also run
# against the Linux (FFmpeg) implementation of the same interface.

param([switch]$Asan)
$ErrorActionPreference = 'Stop'
. (Join-Path $PSScriptRoot '..\support\native_build.ps1')
$RepoRoot = Get-RepoRoot
Set-Location $RepoRoot

if (-not (Initialize-Msvc)) { Skip 'MSVC (cl.exe) not found; install Visual Studio Build Tools with the C++ workload' }

$exe = Join-Path ([IO.Path]::GetTempPath()) ($(if ($Asan) { 'aac_stream_decoder_windows_test_asan.exe' } else { 'aac_stream_decoder_windows_test.exe' }))
Invoke-NativeBuild -Out $exe -Asan:$Asan `
    -Includes @('src', 'src\native_decoder', 'src\audiobuffer') `
    -Sources @(
        'test\windows\aac_stream_decoder_windows_test.cpp',
        'src\audiobuffer\aac_stream_decoder.cpp',
        'src\audiobuffer\aac_stream_decoder_windows.cpp',
        'src\native_decoder\os_decoder_windows.cpp',
        'src\native_stream\m4a_gapless.cpp',
        'src\audiobuffer\m4a_metadata.cpp') `
    -Libs @('mfplat.lib', 'mfreadwrite.lib', 'mfuuid.lib', 'shlwapi.lib', 'ole32.lib', 'uuid.lib', 'propsys.lib')

& $exe (Join-Path $RepoRoot 'example\assets\audio')
exit $LASTEXITCODE
