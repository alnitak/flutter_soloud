@echo off
REM Build and run the cross-platform SIMD, DSP, alignment, leak-regression,
REM and throughput microbenchmark suite on Windows (x86_64 or ARM64).
REM
REM Usage:
REM   test\native_bench\run_native_bench.bat
REM   test\native_bench\run_native_bench.bat --sanitize

setlocal enabledelayedexpansion

set "SCRIPT_DIR=%~dp0"
for %%I in ("%SCRIPT_DIR%..\..") do set "REPO_ROOT=%%~fI"

set "SANITIZE=OFF"
set "TAG=release"
if /I "%~1"=="--sanitize" (
  set "SANITIZE=ON"
  set "TAG=sanitize"
)

set "BUILD_DIR=%TEMP%\flutter_soloud_native_bench_%TAG%"

echo === Configuring native_bench_test (%TAG%) ===
cmake -S "%SCRIPT_DIR%" -B "%BUILD_DIR%" -DENABLE_SANITIZERS=%SANITIZE%
if errorlevel 1 exit /b 1

echo === Building native_bench_test (%TAG%) ===
cmake --build "%BUILD_DIR%" --config Release --parallel
if errorlevel 1 exit /b 1

set "EXE=%BUILD_DIR%\Release\native_bench_test.exe"
if not exist "%EXE%" set "EXE=%BUILD_DIR%\native_bench_test.exe"

echo === Running native_bench_test (%TAG%) ===
"%EXE%" "%REPO_ROOT%"
exit /b %ERRORLEVEL%

