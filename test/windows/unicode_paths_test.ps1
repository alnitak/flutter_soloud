# Non-ASCII, space-containing and long (> MAX_PATH) paths through the
# plugin's loadFile() (WAV, MP3, FLAC through SoLoud's DiskFile; M4A through
# Media Foundation and the native stream), memory and disk mode, plain and
# \\?\-prefixed long paths, and loadMem() of an M4A while %TMP% is a
# non-ASCII directory (decodeMemory's temporary file). Builds
# test\windows\unicode_paths_test.cpp against the whole plugin.
#
#   powershell -ExecutionPolicy Bypass -File test\windows\unicode_paths_test.ps1 [-Asan] [-Strict]
#
# Platform: Windows 10/11 x64/arm64. Needs: Visual Studio or Build Tools with
# the C++ workload (SKIPPED without it), an audio output device (the engine
# must start; SKIPPED otherwise) and the fixtures in example\assets\audio.
#   -Asan    build with /fsanitize=address.
#   -Strict  known src/ bugs fail the run.
# A plain (unprefixed) long path is reported, not required: it works only in
# a long-path-aware process with LongPathsEnabled set.
# Verification: NOT VERIFIED ON WINDOWS. Cross-compiled with mingw-w64 and run
# under Wine (test/docker/windows_native.sh), whose path handling is more
# lenient than Windows' (no MAX_PATH limit).

param([switch]$Asan, [switch]$Strict)
$ErrorActionPreference = 'Stop'
. (Join-Path $PSScriptRoot '..\support\native_build.ps1')
$RepoRoot = Get-RepoRoot
Set-Location $RepoRoot

if (-not (Initialize-Msvc)) { Skip 'MSVC (cl.exe) not found; install Visual Studio Build Tools with the C++ workload' }

$exe = Join-Path ([IO.Path]::GetTempPath()) ($(if ($Asan) { 'unicode_paths_test_asan.exe' } else { 'unicode_paths_test.exe' }))
Invoke-NativeBuild -Out $exe -Asan:$Asan `
    -Defines (Get-PluginDefines) -Includes (Get-PluginIncludes) `
    -Sources (@('test\windows\unicode_paths_test.cpp') + (Get-PluginSources)) `
    -Libs (Get-PluginLibs)

$work = Join-Path ([IO.Path]::GetTempPath()) ('unicode_paths_test_' + [Guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory -Path $work | Out-Null
try {
    $testArgs = @((Join-Path $RepoRoot 'example\assets\audio'), $work)
    if ($Strict) { $testArgs += '--strict' }
    & $exe @testArgs
    $status = $LASTEXITCODE
} finally {
    # The long directory needs the \\?\ form to be removed.
    cmd /c "rd /s /q `"\\?\$work`"" 2>$null
}
exit $status
