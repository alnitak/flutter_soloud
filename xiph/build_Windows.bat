:: NOTE: run this script with the Developer Command Prompt for Visual Studio
:: This script will clone the OGG and OPUS repositories, build the libraries,
:: and copy the .lib and .dll files to the build/windows/libs directory
@echo off
setlocal

:: Set up directories
set REPO_OGG=https://github.com/xiph/ogg
set REPO_OPUS=https://github.com/xiph/opus
set BUILD_DIR=%cd%\build
set LIBS_DIR=%BUILD_DIR%\windows\libs
set OGG_DIR=ogg
set OPUS_DIR=opus


:: Check if the OGG repo exists, if not, clone it
if not exist "%OGG_DIR%" (
    echo Cloning OGG repository...
    git clone %REPO_OGG%
) else (
    echo OGG repository already exists.
)

:: Check if the OPUS repo exists, if not, clone it
if not exist "%OPUS_DIR%" (
    echo Cloning OPUS repository...
    git clone %REPO_OPUS%
) else (
    echo OPUS repository already exists.
)

:: Create the build directory if it doesn't exist
if not exist "%BUILD_DIR%\windows" (
    mkdir "%BUILD_DIR%\windows"
)

:: Create the libs directory if it doesn't exist
if not exist "%LIBS_DIR%" (
    mkdir "%LIBS_DIR%"
)

:: Step 1: Build OGG library
echo Building OGG library...
cd %OGG_DIR%
mkdir build
cd build
:: Ensure shared libraries are built (DLL)
cmake -G "Visual Studio 17 2022" -DCMAKE_BUILD_TYPE:STRING=Release -DBUILD_SHARED_LIBS=ON ..
msbuild ogg.sln
cmake --build . --config Release

:: Step 2: Copy OGG .lib and .dll to libs folder
echo Copying OGG .lib and .dll files...
copy /Y ".\Release\*.lib" "%LIBS_DIR%"
copy /Y ".\Release\*.dll" "%LIBS_DIR%"

cd ..\..

:: Step 3: Build OPUS library
echo Building OPUS library...
cd %OPUS_DIR%
mkdir build
cd build
:: Ensure shared libraries are built (DLL)
cmake -G "Visual Studio 17 2022" -DCMAKE_BUILD_TYPE:STRING=Release -DBUILD_SHARED_LIBS=ON ..
msbuild opus.sln
cmake --build . --config Release

:: Step 4: Copy OPUS .lib and .dll to libs folder
echo Copying OPUS .lib and .dll files...
copy /Y ".\Release\*.lib" "%LIBS_DIR%"
copy /Y ".\Release\*.dll" "%LIBS_DIR%"

cd ..\..
echo Done!
pause
