:: NOTE: run this script with the Developer Command Prompt for Visual Studio
:: This script will clone the OGG and OPUS repositories, build the libraries,
:: and copy the .lib and .dll files to the build/windows/libs directory
@echo off
setlocal

:: Set up directories
set BASE_DIR=%cd%
set REPO_OGG=https://github.com/xiph/ogg
set REPO_OPUS=https://github.com/xiph/opus
set REPO_VORBIS=https://github.com/xiph/vorbis
set REPO_FLAC=https://github.com/xiph/flac
set BUILD_DIR=%BASE_DIR%\\windows\\build
set LIBS_DIR=%BASE_DIR%\..\windows\libs
set INCLUDE_DIR=%BASE_DIR%\..\windows\include
set OGG_DIR=%BASE_DIR%\\ogg
set OPUS_DIR=%BASE_DIR%\\opus
set VORBIS_DIR=%BASE_DIR%\\vorbis
set FLAC_DIR=%BASE_DIR%\\flac

:: Check if the OGG repo exists, if not, clone it
if not exist "%OGG_DIR%" (
    echo Cloning OGG repository...
    git clone %REPO_OGG%
    :: reset to a known good commit
    cd ogg
    git reset --hard db5c7a4
    cd ..
) else (
    echo OGG repository already exists.
)

:: Check if the OPUS repo exists, if not, clone it
if not exist "%OPUS_DIR%" (
    echo Cloning OPUS repository...
    git clone %REPO_OPUS%
    :: reset to a known good commit
    cd opus
    git reset --hard c79a9bd
    cd ..
) else (
    echo OPUS repository already exists.
)

:: Check if the VORBIS repo exists, if not, clone it
if not exist "%VORBIS_DIR%" (
    echo Cloning VORBIS repository...
    git clone %REPO_VORBIS%
    :: reset to a known good commit
    cd vorbis
    git reset --hard 84c0236
    cd ..
) else (
    echo VORBIS repository already exists.
)

:: Check if the FLAC repo exists, if not, clone it
if not exist "%FLAC_DIR%" (
    echo Cloning FLAC repository...
    git clone %REPO_FLAC%
    :: reset to a known good commit
    cd flac
    git reset --hard 9547dbc
    cd ..
) else (
    echo FLAC repository already exists.
)


:: Create directories if they don't exist
if not exist "%BUILD_DIR%" mkdir "%BUILD_DIR%"
if not exist "%LIBS_DIR%" mkdir "%LIBS_DIR%"
if not exist "%INCLUDE_DIR%" mkdir "%INCLUDE_DIR%"

:: Create subdirectories for includes
if not exist "%INCLUDE_DIR%\\ogg" mkdir "%INCLUDE_DIR%\\ogg"
if not exist "%INCLUDE_DIR%\\opus" mkdir "%INCLUDE_DIR%\\opus"
if not exist "%INCLUDE_DIR%\\vorbis" mkdir "%INCLUDE_DIR%\\vorbis"
if not exist "%INCLUDE_DIR%\\FLAC" mkdir "%INCLUDE_DIR%\\FLAC"
if not exist "%INCLUDE_DIR%\\share" mkdir "%INCLUDE_DIR%\\share"

:: Step 1: Build OGG library
echo Building OGG library...
mkdir "%BUILD_DIR%\\ogg"
cd "%BUILD_DIR%\\ogg"
:: Build static libraries
cmake -G "Visual Studio 17 2022" -DCMAKE_BUILD_TYPE:STRING=Release -DBUILD_SHARED_LIBS=ON -DCMAKE_MSVC_RUNTIME_LIBRARY="MultiThreaded" "%OGG_DIR%"
cmake --build . --config Release
:: Step 2: Copy OGG .lib, .dll and include files
echo Copying OGG files...
copy /Y ".\\Release\\*.lib" "%LIBS_DIR%"
copy /Y ".\\Release\\*.dll" "%LIBS_DIR%"
xcopy /Y /S "%OGG_DIR%\\include\\ogg" "%INCLUDE_DIR%\\ogg\"

cd "%BASE_DIR%"

:: Step 3: Build OPUS library
echo Building OPUS library...
mkdir "%BUILD_DIR%\\opus"
cd "%BUILD_DIR%\\opus"
:: Build static libraries
cmake -G "Visual Studio 17 2022" -DCMAKE_BUILD_TYPE:STRING=Release -DBUILD_SHARED_LIBS=ON -DCMAKE_MSVC_RUNTIME_LIBRARY="MultiThreaded" "%OPUS_DIR%"
cmake --build . --config Release
:: Step 4: Copy OPUS .lib, .dll and include files
echo Copying OPUS files...
copy /Y ".\\Release\\*.lib" "%LIBS_DIR%"
copy /Y ".\\Release\\*.dll" "%LIBS_DIR%"
xcopy /Y /S "%OPUS_DIR%\\include" "%INCLUDE_DIR%\\opus\"

:: Step 5: Build VORBIS library
echo Building VORBIS library...
mkdir "%BUILD_DIR%\\vorbis"
cd "%BUILD_DIR%\\vorbis"
:: Build static libraries
cmake -G "Visual Studio 17 2022" ^
    -DCMAKE_BUILD_TYPE:STRING=Release ^
    -DBUILD_SHARED_LIBS=ON ^
    -DCMAKE_MSVC_RUNTIME_LIBRARY="MultiThreaded" ^
    -DOGG_ROOT="%BUILD_DIR%\\ogg" ^
    -DOGG_INCLUDE_DIR="%OGG_DIR%\\include" ^
    -DOGG_LIBRARY="%BUILD_DIR%\\ogg\\Release\\ogg.lib" ^
    -DOGG_LIBRARY_RELEASE="%BUILD_DIR%\\ogg\\Release\\ogg.lib" ^
    "%VORBIS_DIR%"
cmake --build . --config Release
:: Step 6: Copy VORBIS .lib, .dll and include files
echo Copying VORBIS files...
copy /Y ".\\lib\\Release\\*.lib" "%LIBS_DIR%"
copy /Y ".\\lib\\Release\\*.dll" "%LIBS_DIR%"
xcopy /Y /S "%VORBIS_DIR%\\include\\vorbis" "%INCLUDE_DIR%\\vorbis\"


:: Step 7: Build FLAC library
echo Building FLAC library...
mkdir "%BUILD_DIR%\\flac"
cd "%BUILD_DIR%\\flac"
:: Build static libraries
cmake -G "Visual Studio 17 2022" ^
    -DCMAKE_BUILD_TYPE:STRING=Release ^
    -DBUILD_SHARED_LIBS=ON ^
    -DCMAKE_MSVC_RUNTIME_LIBRARY="MultiThreaded" ^
    -DOGG_ROOT="%BUILD_DIR%\\ogg" ^
    -DOGG_INCLUDE_DIR="%OGG_DIR%\\include" ^
    -DOGG_LIBRARY="%BUILD_DIR%\\ogg\\Release\\ogg.lib" ^
    -DOGG_LIBRARY_RELEASE="%BUILD_DIR%\\ogg\\Release\\ogg.lib" ^
    -DBUILD_CXXLIBS=OFF ^
    -DBUILD_PROGRAMS=OFF ^
    -DBUILD_EXAMPLES=OFF ^
    -DBUILD_TESTING=OFF ^
    -DBUILD_DOCS=OFF ^
    -DINSTALL_MANPAGES=OFF ^
    "%FLAC_DIR%"
cmake --build . --config Release
:: Step 8: Copy FLAC .lib, .dll and include files
echo Copying FLAC files...
@REM copy /Y ".\\lib\\Release\\*.lib" "%LIBS_DIR%"
@REM copy /Y ".\\lib\\Release\\*.dll" "%LIBS_DIR%"

copy /Y ".\\objs\\Release\\FLAC.dll" "%LIBS_DIR%"
copy /Y ".\\src\\libFLAC\\Release\\FLAC.lib" "%LIBS_DIR%"
xcopy /Y /S "%FLAC_DIR%\\include\\FLAC" "%INCLUDE_DIR%\\FLAC\"
xcopy /Y /S "%FLAC_DIR%\\include\\share" "%INCLUDE_DIR%\\share\"


:: Remove vorbisenc files
echo Removing vorbisenc files...
del /F /Q "%LIBS_DIR%\\vorbisenc.*"
del /F /Q "%INCLUDE_DIR%\\vorbis\\vorbisenc.h"

cd "%BASE_DIR%"
echo Done!
pause