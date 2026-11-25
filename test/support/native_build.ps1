# Helpers for test/windows/*_test.ps1: find MSVC, build a test executable
# from repo sources, and report SKIPPED for missing prerequisites. Dot-source
# it, don't run it:
#
#   . (Join-Path $PSScriptRoot '..\support\native_build.ps1')
#   $RepoRoot = Get-RepoRoot
#   if (-not (Initialize-Msvc)) { Skip 'MSVC (cl.exe) not found' }
#   Invoke-NativeBuild -Out $exe -Sources @('test\common\x_test.cpp', 'src\core\*.cpp') `
#       -Defines @('WITH_NULL') -Includes @('src') -Libs @('ole32.lib') [-Asan] [-Dll] `
#       [-ForceIncludes @('C:\path\defines.h')]
#
# Initialize-Msvc uses cl.exe from PATH (a Developer PowerShell), else finds
# the newest Visual Studio / Build Tools with the C++ workload through
# vswhere and imports vcvarsall.bat's environment for the host architecture
# (x64 or arm64). Sources are compiled one by one into uniquely named objects
# (several sources share a base name), C files as C, the rest as C++17, with
# the plugin's MSVC flags (hook/build.dart): /O2 /Zc:inline /EHsc, NOMINMAX,
# _CRT_SECURE_NO_WARNINGS, and the DLL runtime (/MD).
#
# Platform: Windows, Windows PowerShell 5.1 or PowerShell 7.
# Verification: NOT YET VERIFIED ON WINDOWS (written on macOS, no Windows host).

Set-StrictMode -Version 2.0

$script:NativeBuildRepoRoot = (Resolve-Path (Join-Path $PSScriptRoot '..\..')).Path

function Get-RepoRoot {
    return $script:NativeBuildRepoRoot
}

function Skip([string]$Reason) {
    Write-Output "SKIPPED: $Reason"
    exit 0
}

function Initialize-Msvc {
    if (Get-Command cl.exe -ErrorAction SilentlyContinue) { return $true }
    $programFilesX86 = [Environment]::GetFolderPath('ProgramFilesX86')
    $vswhere = Join-Path $programFilesX86 'Microsoft Visual Studio\Installer\vswhere.exe'
    if (-not (Test-Path $vswhere)) { return $false }
    $arch = 'x64'
    $component = 'Microsoft.VisualStudio.Component.VC.Tools.x86.x64'
    if ($env:PROCESSOR_ARCHITECTURE -eq 'ARM64') {
        $arch = 'arm64'
        $component = 'Microsoft.VisualStudio.Component.VC.Tools.ARM64'
    }
    $vs = & $vswhere -latest -products '*' -requires $component -property installationPath | Select-Object -First 1
    if (-not $vs) { return $false }
    $vcvars = Join-Path $vs 'VC\Auxiliary\Build\vcvarsall.bat'
    if (-not (Test-Path $vcvars)) { return $false }
    # "call" first, so cmd's /c quote stripping can't eat the path's quotes.
    $dump = & cmd.exe /c "call `"$vcvars`" $arch >nul 2>&1 && set"
    foreach ($line in $dump) {
        if ($line -match '^([^=]+)=(.*)$') {
            Set-Item -Path "env:$($Matches[1])" -Value $Matches[2]
        }
    }
    return [bool](Get-Command cl.exe -ErrorAction SilentlyContinue)
}

# Expands wildcards in repo-relative source paths, in a stable order.
function Expand-Sources([string[]]$Sources) {
    $result = New-Object System.Collections.Generic.List[string]
    foreach ($s in $Sources) {
        $full = if ([IO.Path]::IsPathRooted($s)) { $s } else { Join-Path $script:NativeBuildRepoRoot $s }
        if ($full -match '[\*\?]') {
            $matched = @(Get-ChildItem -Path $full -File | Sort-Object FullName | ForEach-Object { $_.FullName })
            if ($matched.Count -eq 0) { throw "no source matches $s" }
            foreach ($m in $matched) { $result.Add($m) }
        } else {
            if (-not (Test-Path $full)) { throw "source not found: $s" }
            $result.Add((Resolve-Path $full).Path)
        }
    }
    return ,$result.ToArray()
}

function Invoke-NativeBuild {
    param(
        [Parameter(Mandatory = $true)][string]$Out,
        [Parameter(Mandatory = $true)][string[]]$Sources,
        [string[]]$Defines = @(),
        [string[]]$Includes = @(),
        [string[]]$Libs = @(),
        [string[]]$CxxFlags = @(),
        [string[]]$ForceIncludes = @(),
        [string]$Runtime = 'MD',
        [switch]$Dll,
        [switch]$Asan
    )
    $objDir = Join-Path ([IO.Path]::GetTempPath()) ('soloud_obj_' + [Guid]::NewGuid().ToString('N'))
    New-Item -ItemType Directory -Path $objDir | Out-Null
    try {
        # Default /MD: the DLL runtime, as CMake uses.
        $flags = @('/nologo', '/c', "/$Runtime", '/EHsc', '/utf-8', '/W3', '/Zc:inline', '/DNOMINMAX', '/D_CRT_SECURE_NO_WARNINGS')
        if ($Asan) { $flags += @('/fsanitize=address', '/Zi', '/Od') } else { $flags += @('/O2') }
        foreach ($d in $Defines) { $flags += "/D$d" }
        # String-valued defines are easier to pass in a header than through
        # PowerShell's quoting of native arguments.
        foreach ($h in $ForceIncludes) { $flags += "/FI$h" }
        foreach ($i in $Includes) {
            $dir = if ([IO.Path]::IsPathRooted($i)) { $i } else { Join-Path $script:NativeBuildRepoRoot $i }
            $flags += "/I$dir"
        }
        $objects = New-Object System.Collections.Generic.List[string]
        $index = 0
        foreach ($src in (Expand-Sources $Sources)) {
            $obj = Join-Path $objDir ('{0:D3}_{1}.obj' -f $index, [IO.Path]::GetFileNameWithoutExtension($src))
            $index++
            $isC = [IO.Path]::GetExtension($src) -eq '.c'
            $langFlags = if ($isC) { @('/TC') } else { @('/TP', '/std:c++17') + $CxxFlags }
            & cl.exe @flags @langFlags "/Fo$obj" $src | Where-Object { $_ -notmatch '^\S+\.(c|cpp)$' }
            if ($LASTEXITCODE -ne 0) { throw "compile failed: $src" }
            $objects.Add($obj)
        }
        $linkFlags = @('/nologo', "/Fe$Out")
        if ($Dll) { $linkFlags += '/LD' }
        if ($Asan) { $linkFlags += @('/fsanitize=address', '/Zi') }
        & cl.exe @linkFlags $objects.ToArray() /link @Libs
        if ($LASTEXITCODE -ne 0) { throw "link failed: $Out" }
    } finally {
        Remove-Item -Recurse -Force $objDir -ErrorAction SilentlyContinue
    }
}

# The whole plugin (bindings.cpp down to the miniaudio backend) for Windows,
# for tests that drive the Dart entry points (initEngine, loadFile, play, ...).
# Pass to Invoke-NativeBuild with -Defines (Get-PluginDefines) and
# -Libs (Get-PluginLibs). Mirrors test/docker/windows_native.sh's plugin_lib.
function Get-PluginSources {
    return @(
        'src\pffft\pffft.c',
        'src\soloud_common.cpp', 'src\bindings.cpp', 'src\player.cpp', 'src\analyzer.cpp',
        'src\synth\*.cpp', 'src\waveform\waveform.cpp', 'src\waveform\miniaudio_libvorbis.cpp',
        'src\audiobuffer\*.cpp', 'src\native_stream\*.cpp',
        'src\native_decoder\native_audio_decoder.cpp', 'src\native_decoder\os_decoder_windows.cpp',
        'src\filters\*.cpp', 'src\mixeroutput\*.cpp',
        'src\soloud\src\core\*.cpp', 'src\soloud\src\filter\*.cpp',
        'src\soloud\src\audiosource\ay\*.cpp', 'src\soloud\src\audiosource\monotone\*.cpp',
        'src\soloud\src\audiosource\noise\*.cpp', 'src\soloud\src\audiosource\sfxr\*.cpp',
        'src\soloud\src\audiosource\speech\*.cpp', 'src\soloud\src\audiosource\tedsid\*.cpp',
        'src\soloud\src\audiosource\vic\*.cpp', 'src\soloud\src\audiosource\vizsn\*.cpp',
        'src\soloud\src\audiosource\wav\*.cpp',
        'src\soloud\src\backend\miniaudio\soloud_miniaudio.cpp')
}
function Get-PluginDefines { return @('WITH_MINIAUDIO', 'NO_XIPH_LIBS') }
function Get-PluginIncludes {
    return @('src\soloud\include', 'src\soloud\src', 'src\pffft', 'src', 'src\native_decoder', 'src\audiobuffer')
}
function Get-PluginLibs {
    return @('mfplat.lib', 'mfreadwrite.lib', 'mfuuid.lib', 'shlwapi.lib', 'ole32.lib', 'oleaut32.lib',
        'uuid.lib', 'propsys.lib', 'user32.lib', 'advapi32.lib', 'winmm.lib')
}
