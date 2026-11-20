# The engine on WASAPI when the output device changes under it: default
# device switched while playing (needs 2+ active render endpoints), default
# device held in exclusive mode by another client, no render device at all,
# and (opt-in, administrator) the default endpoint disabled and re-enabled
# while playing. Builds test\windows\wasapi_device_change_test.cpp against the
# whole plugin (Get-PluginSources in test\support\native_build.ps1).
#
#   powershell -ExecutionPolicy Bypass -File test\windows\wasapi_device_change_test.ps1 [-Asan] [-Strict] [-AllowDeviceDisable]
#
# Platform: Windows 10/11 x64/arm64. Needs: Visual Studio or Build Tools with
# the C++ workload (SKIPPED without it) and an audio output device for most
# cases; cases whose prerequisite is missing print "not run", and SKIPPED
# when none could run. No assets.
#   -Asan                build with /fsanitize=address.
#   -Strict              known src/ bugs (KNOWN BUG M...) fail the run.
#   -AllowDeviceDisable  also run the removal case: disables the default
#                        render endpoint with pnputil (administrator only;
#                        the endpoint is re-enabled afterwards, also on
#                        failure). Changes the machine's audio setup for a
#                        few seconds.
# Changes the default output device for a few seconds when two endpoints are
# present, and restores it.
# Verification: NOT VERIFIED ON WINDOWS. Cross-compiled with mingw-w64 and run
# under Wine (test/docker/windows_native.sh), where only the no-device case
# can run (Wine has no IPolicyConfig and no exclusive mode).

param([switch]$Asan, [switch]$Strict, [switch]$AllowDeviceDisable)
$ErrorActionPreference = 'Stop'
. (Join-Path $PSScriptRoot '..\support\native_build.ps1')
$RepoRoot = Get-RepoRoot
Set-Location $RepoRoot

if (-not (Initialize-Msvc)) { Skip 'MSVC (cl.exe) not found; install Visual Studio Build Tools with the C++ workload' }

$exe = Join-Path ([IO.Path]::GetTempPath()) ($(if ($Asan) { 'wasapi_device_change_test_asan.exe' } else { 'wasapi_device_change_test.exe' }))
Invoke-NativeBuild -Out $exe -Asan:$Asan `
    -Defines (Get-PluginDefines) -Includes (Get-PluginIncludes) `
    -Sources (@('test\windows\wasapi_device_change_test.cpp') + (Get-PluginSources)) `
    -Libs (Get-PluginLibs)

$testArgs = @()
if ($Strict) { $testArgs += '--strict' }
& $exe @testArgs
$status = $LASTEXITCODE

if ($AllowDeviceDisable) {
    $isAdmin = ([Security.Principal.WindowsPrincipal][Security.Principal.WindowsIdentity]::GetCurrent()).IsInRole(
        [Security.Principal.WindowsBuiltInRole]::Administrator)
    if (-not $isAdmin) {
        Write-Output 'removal case not run: -AllowDeviceDisable needs an elevated shell'
    } else {
        $signals = Join-Path ([IO.Path]::GetTempPath()) ('wasapi_removal_' + [Guid]::NewGuid().ToString('N'))
        New-Item -ItemType Directory -Path $signals | Out-Null
        $proc = Start-Process -FilePath $exe -ArgumentList (@('--removal', $signals) + $testArgs) -NoNewWindow -PassThru
        $instance = $null
        try {
            $endpointFile = Join-Path $signals 'endpoint.txt'
            for ($i = 0; $i -lt 600 -and -not (Test-Path $endpointFile); $i++) { Start-Sleep -Milliseconds 100 }
            $endpoint = (Get-Content $endpointFile -ErrorAction Stop | Select-Object -First 1).Trim()
            # Render endpoints are software devices: SWD\MMDEVAPI\<endpoint id>.
            $instance = 'SWD\MMDEVAPI\' + $endpoint
            & pnputil /disable-device "$instance" | Out-Host
            New-Item -ItemType File -Path (Join-Path $signals 'removed') | Out-Null
            $checked = Join-Path $signals 'checked'
            for ($i = 0; $i -lt 600 -and -not (Test-Path $checked); $i++) { Start-Sleep -Milliseconds 100 }
        } finally {
            if ($instance) { & pnputil /enable-device "$instance" | Out-Host }
            New-Item -ItemType File -Force -Path (Join-Path $signals 'restored') | Out-Null
        }
        $proc.WaitForExit()
        if ($proc.ExitCode -ne 0) { $status = $proc.ExitCode }
        Remove-Item -Recurse -Force $signals -ErrorAction SilentlyContinue
    }
}
exit $status
