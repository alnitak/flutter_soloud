@echo off
REM Build and run the cross-platform SIMD, DSP, alignment, leak-regression,
REM and throughput microbenchmark suite on Windows (x86_64 or ARM64).
REM
REM Usage:
REM   test\native_bench\run_native_bench.bat             REM Release run
REM   test\native_bench\run_native_bench.bat --sanitize  REM ASan run
REM   test\native_bench\run_native_bench.bat --all       REM Both Release & ASan

setlocal enabledelayedexpansion

REM Resolve paths without trailing backslashes so CMake arguments are not escaped.
for %%I in ("%~dp0.") do set "SCRIPT_DIR=%%~fI"
for %%I in ("%SCRIPT_DIR%\..\..") do set "REPO_ROOT=%%~fI"

set "MODE=%~1"
if "%MODE%"=="" set "MODE=--release"

if /I "%MODE%"=="--sanitize" (
  call :run_build "RelWithDebInfo" "ON" "sanitize"
  exit /b !ERRORLEVEL!
) else if /I "%MODE%"=="--all" (
  call :run_build "Release" "OFF" "release"
  if errorlevel 1 exit /b !ERRORLEVEL!
  call :run_build "RelWithDebInfo" "ON" "sanitize"
  exit /b !ERRORLEVEL!
) else (
  call :run_build "Release" "OFF" "release"
  exit /b !ERRORLEVEL!
)

:run_build
set "BUILD_TYPE=%~1"
set "SANITIZE=%~2"
set "TAG=%~3"
set "BUILD_DIR=%TEMP%\flutter_soloud_native_bench_%TAG%"

echo === Configuring native_bench_test (%TAG%) ===
cmake -S "%SCRIPT_DIR%" -B "%BUILD_DIR%" -DCMAKE_BUILD_TYPE="%BUILD_TYPE%" -DENABLE_SANITIZERS=%SANITIZE%
if errorlevel 1 exit /b 1

echo === Building native_bench_test (%TAG%) ===
cmake --build "%BUILD_DIR%" --config "%BUILD_TYPE%" --parallel
if errorlevel 1 exit /b 1

set "EXE=%BUILD_DIR%\%BUILD_TYPE%\native_bench_test.exe"
if not exist "!EXE!" set "EXE=%BUILD_DIR%\Release\native_bench_test.exe"
if not exist "!EXE!" set "EXE=%BUILD_DIR%\native_bench_test.exe"

if not exist "!EXE!" (
  echo Error: executable not found in "%BUILD_DIR%"
  exit /b 1
)

REM When AddressSanitizer is enabled, ensure the MSVC ASan runtime DLL is next to the executable.
if /I "%SANITIZE%"=="ON" (
  set "VSWHERE=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe"
  if exist "!VSWHERE!" (
    for /f "usebackq delims=" %%A in (`"!VSWHERE!" -latest -products * -find **/Hostx64/x64/clang_rt.asan_dynamic-x86_64.dll 2^>nul`) do (
      if exist "%%A" (
        for %%D in ("!EXE!") do copy /y "%%A" "%%~dpD" >nul 2>&1
      )
    )
  )
)

echo === Running native_bench_test (%TAG%) ===
"!EXE!" "%REPO_ROOT%"
exit /b %ERRORLEVEL%
