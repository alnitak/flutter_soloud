cmake_minimum_required(VERSION 3.10)
set(PROJECT_NAME "flutter_soloud")
project(${PROJECT_NAME} LANGUAGES CXX C OBJCXX)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

set(PLUGIN_NAME "${PROJECT_NAME}_plugin")

set(SRC_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../src")

# Include common plugin sources and SoLoud configuration
include(${SRC_DIR}/CMakeLists.txt)

# Build a STATIC library (will be linked into the app)
add_library(${PLUGIN_NAME} STATIC
  ${PLUGIN_SOURCES}
)

set_target_properties(${PLUGIN_NAME} PROPERTIES
  CXX_VISIBILITY_PRESET hidden)

target_compile_definitions(${PLUGIN_NAME} PRIVATE FLUTTER_PLUGIN_IMPL)
target_compile_definitions(${PLUGIN_NAME} PRIVATE SIGNALSMITH_USE_PFFFT)

# Xiph library include paths (for compilation only; linking is handled by the podspec)
if(NOT DEFINED ENV{NO_OPUS_OGG_LIBS})
    message("NO_OPUS_OGG_LIBS has not been set. Including Opus/Ogg headers.")
    include_directories(${CMAKE_CURRENT_SOURCE_DIR}/flutter_soloud/include)
else()
    message("NO_OPUS_OGG_LIBS has been set. Not including Opus/Ogg headers.")
    add_definitions(-DNO_OPUS_OGG_LIBS)
endif()

# Force release optimization flags regardless of build configuration
# For native debug purpose, use -g -O0 but remember to restore -O2 -DNDEBUG for release
target_compile_options("${PLUGIN_NAME}" PRIVATE -Wall -Wno-error -Wno-vla -O2 -DNDEBUG)

# On Apple platforms, miniaudio.h includes AVFoundation Objective-C headers,
# so soloud_miniaudio.cpp must be compiled as Objective-C++.
set(MINIAUDIO_SOURCE "${SRC_DIR}/soloud/src/backend/miniaudio/soloud_miniaudio.cpp")
set_source_files_properties(${MINIAUDIO_SOURCE} PROPERTIES LANGUAGE OBJCXX)

# Apple framework dependencies needed at compile time for includes
find_library(AUDIOTOOLBOX_FRAMEWORK AudioToolbox)
find_library(AVFAUDIO_FRAMEWORK AVFAudio)
if(AUDIOTOOLBOX_FRAMEWORK)
  target_link_libraries(${PLUGIN_NAME} PRIVATE ${AUDIOTOOLBOX_FRAMEWORK})
endif()
