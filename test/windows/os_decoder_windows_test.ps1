# Media Foundation decoders on Windows: NativeAudioDecoder
# (src\native_decoder\os_decoder_windows.cpp) and the streaming
# AACDecoderWrapper (src\audiobuffer\aac_stream_decoder_windows.cpp) for
# M4A, ADTS AAC, AC-3 and E-AC-3. Builds and runs
# test\windows\os_decoder_windows_test.cpp: the shared checks in
# test\support\os_decoder_checks.h plus UTF-8 paths, STA threads, temp files
# and cross-thread COM teardown.
#
#   powershell -ExecutionPolicy Bypass -File test\windows\os_decoder_windows_test.ps1 [-Asan]
#
# Platform: Windows 10/11 x64/arm64 (Media Foundation; AC-3 needs the Dolby
# MFT, missing on N editions, where those checks only require a clean
# failure). Needs: Visual Studio or Build Tools with the C++ workload; prints
# SKIPPED without it. Fixtures from example\assets\audio. No audio device.
#   -Asan  build with /fsanitize=address.
# Known src/ bugs are reported as KNOWN BUG and do not fail the run; set
# OSDEC_STRICT=1 to count them.
# Verification: NOT YET VERIFIED ON WINDOWS (written on macOS; cross-compiled
# with mingw-w64 there, never run).

param([switch]$Asan)
$ErrorActionPreference = 'Stop'
. (Join-Path $PSScriptRoot '..\support\native_build.ps1')
$RepoRoot = Get-RepoRoot
Set-Location $RepoRoot

if (-not (Initialize-Msvc)) { Skip 'MSVC (cl.exe) not found; install Visual Studio Build Tools with the C++ workload' }

$exe = Join-Path ([IO.Path]::GetTempPath()) ($(if ($Asan) { 'os_decoder_windows_test_asan.exe' } else { 'os_decoder_windows_test.exe' }))
Invoke-NativeBuild -Out $exe -Asan:$Asan `
    -Includes @('src', 'src\native_decoder', 'src\audiobuffer') `
    -Sources @(
        'test\windows\os_decoder_windows_test.cpp',
        'src\audiobuffer\aac_stream_decoder.cpp',
        'src\audiobuffer\aac_stream_decoder_windows.cpp',
        'src\native_decoder\os_decoder_windows.cpp',
        'src\native_stream\m4a_gapless.cpp',
        'src\audiobuffer\m4a_metadata.cpp') `
    -Libs @('mfplat.lib', 'mfreadwrite.lib', 'mfuuid.lib', 'shlwapi.lib', 'ole32.lib', 'uuid.lib')

$work = Join-Path ([IO.Path]::GetTempPath()) ('os_decoder_windows_test_' + [Guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory -Path $work | Out-Null
try {
    & $exe (Join-Path $RepoRoot 'example\assets\audio') $work
    $status = $LASTEXITCODE
} finally {
    Remove-Item -Recurse -Force $work -ErrorAction SilentlyContinue
}
exit $status
