# Run Dart tests in Chrome against the real WASM build in web\ (Windows
# version of test/support/web_tests.sh, which has the details).
#
#   powershell -ExecutionPolicy Bypass -File test\support\web_tests.ps1 [-Wasm] [-ForceSingleThreaded] [-FlutterArgs <args>] [paths...]
#
# Default path: test\common. Without -Wasm this tests the single-threaded
# WASM build, with -Wasm the multi-threaded one. -FlutterArgs passes more
# options to flutter test, e.g. -FlutterArgs '--name','voice'.
#
# Needs: Flutter and Google Chrome (or $env:CHROME_EXECUTABLE).
# Verification: NOT YET VERIFIED ON WINDOWS (written on macOS, no Windows host).

param(
    [switch]$Wasm,
    [switch]$ForceSingleThreaded,
    [string[]]$FlutterArgs = @(),
    [Parameter(ValueFromRemainingArguments = $true)]
    [string[]]$Paths = @()
)
$ErrorActionPreference = 'Stop'
$RepoRoot = (Resolve-Path (Join-Path $PSScriptRoot '..\..')).Path
Set-Location $RepoRoot

$paths = @($Paths)
if ($paths.Count -eq 0) { $paths = @('test\common') }

$created = New-Object System.Collections.Generic.List[string]
$wrapper = $null
$exitCode = 1
try {
    # The page of test\common\foo_test.dart is served at /common/foo_test.html
    # and flutter_soloud loads its scripts from assets/packages/flutter_soloud/web/
    # relative to the page. The test server only serves test\, so link that
    # path to web\ for the run. A directory junction needs no admin rights.
    foreach ($p in $paths) {
        $dir = if (Test-Path $p -PathType Leaf) { Split-Path $p -Parent } else { $p }
        $link = Join-Path $dir 'assets\packages\flutter_soloud\web'
        if (Test-Path $link) { continue }
        New-Item -ItemType Directory -Force -Path (Split-Path $link -Parent) | Out-Null
        New-Item -ItemType Junction -Path $link -Target (Join-Path $RepoRoot 'web') | Out-Null
        $created.Add($link)
    }

    # The browser build compiles every test file into one program, so files
    # that import dart:ffi, the FFI bindings, the build hook or bin\ (all
    # VM-only) are left out.
    $ffiImport = "^import '(dart:ffi|package:ffi/|package:flutter_soloud/src/bindings/[a-z_]*(_ffi|ffigen)[a-z_]*\.dart|\.\./\.\./(hook|bin)/)"
    $files = New-Object System.Collections.Generic.List[string]
    foreach ($p in $paths) {
        $candidates = if (Test-Path $p -PathType Container) {
            Get-ChildItem -Path $p -Recurse -Filter '*_test.dart' | Sort-Object FullName | ForEach-Object { $_.FullName }
        } else { @((Resolve-Path $p).Path) }
        foreach ($f in $candidates) {
            if (-not (Select-String -Path $f -Pattern $ffiImport -Quiet)) {
                # Relative to the repository (GetRelativePath is not in PowerShell 5.1).
                $files.Add($f.Substring($RepoRoot.TrimEnd('\').Length + 1) -replace '\\', '/')
            }
        }
    }

    # Without a user gesture Chrome keeps the AudioContext suspended. flutter
    # test takes no browser flags but runs $env:CHROME_EXECUTABLE, so point
    # it at a wrapper that adds the autoplay flag.
    $chrome = $env:CHROME_EXECUTABLE
    if (-not $chrome) {
        $chrome = @(
            "$env:ProgramFiles\Google\Chrome\Application\chrome.exe",
            "${env:ProgramFiles(x86)}\Google\Chrome\Application\chrome.exe",
            "$env:LOCALAPPDATA\Google\Chrome\Application\chrome.exe"
        ) | Where-Object { $_ -and (Test-Path $_) } | Select-Object -First 1
    }
    if ($chrome) {
        $wrapper = Join-Path ([IO.Path]::GetTempPath()) ('soloud_chrome_' + [Guid]::NewGuid().ToString('N') + '.cmd')
        Set-Content -Path $wrapper -Encoding ASCII -Value "@`"$chrome`" --autoplay-policy=no-user-gesture-required %*"
        $env:CHROME_EXECUTABLE = $wrapper
    } else {
        Write-Warning 'Chrome not found: the audio tests will fail without the autoplay flag.'
    }

    $flutterArgs = @('test', '--platform', 'chrome')
    if ($Wasm) { $flutterArgs += '--wasm' }
    if ($ForceSingleThreaded) { $flutterArgs += '--dart-define=SOLOUD_FORCE_ST=true' }
    $flutterArgs += $FlutterArgs
    $flutterArgs += $files
    & flutter @flutterArgs
    $exitCode = $LASTEXITCODE
}
finally {
    foreach ($link in $created) {
        # Deletes the junction itself, never the contents of web\.
        if (Test-Path $link) { [IO.Directory]::Delete($link) }
        # Remove the assets\packages\flutter_soloud folders if now empty.
        $parent = Split-Path $link -Parent
        for ($i = 0; $i -lt 3; $i++) {
            if ((Test-Path $parent) -and -not (Get-ChildItem -Force $parent)) { Remove-Item $parent }
            $parent = Split-Path $parent -Parent
        }
    }
    if ($wrapper) {
        Remove-Item -Force $wrapper -ErrorAction SilentlyContinue
        # flutter test stops the .cmd wrapper, which may leave its Chrome
        # running: close the instances started with the autoplay flag.
        Get-CimInstance Win32_Process -Filter "Name = 'chrome.exe'" -ErrorAction SilentlyContinue |
            Where-Object { $_.CommandLine -like '*--autoplay-policy=no-user-gesture-required*' } |
            ForEach-Object { Stop-Process -Id $_.ProcessId -Force -ErrorAction SilentlyContinue }
    }
}
exit $exitCode
