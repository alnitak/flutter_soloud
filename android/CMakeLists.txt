cmake_minimum_required(VERSION 3.10)
set(PROJECT_NAME "flutter_soloud")
project(${PROJECT_NAME} LANGUAGES CXX C)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# ─── Always compile in Release mode for maximum performance ───
set(CMAKE_BUILD_TYPE Release CACHE STRING "" FORCE)
set(CMAKE_C_FLAGS_RELEASE "-DNDEBUG" CACHE STRING "" FORCE)
set(CMAKE_CXX_FLAGS_RELEASE "-DNDEBUG" CACHE STRING "" FORCE)

set(PLUGIN_NAME "${PROJECT_NAME}_plugin")

set(SRC_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../src")

# Include common plugin sources and SoLoud configuration
include(${SRC_DIR}/CMakeLists.txt)

add_library(${PLUGIN_NAME} SHARED
  ${PLUGIN_SOURCES}
)
#apply_standard_settings(${PLUGIN_NAME})
set_target_properties(${PLUGIN_NAME} PROPERTIES
  CXX_VISIBILITY_PRESET hidden)


find_library( # Sets the name of the path variable.
		log-lib
		# Specifies the name of the NDK library that
		# you want CMake to locate.
		log)

  
target_compile_definitions(${PLUGIN_NAME} PRIVATE FLUTTER_PLUGIN_IMPL)
target_compile_definitions(${PLUGIN_NAME} PRIVATE SIGNALSMITH_USE_PFFFT)
target_include_directories(${PLUGIN_NAME} INTERFACE "${CMAKE_CURRENT_SOURCE_DIR}/include")
target_link_libraries(${PLUGIN_NAME} PRIVATE ${log-lib} android)


if(NOT DEFINED NO_OPUS_OGG_LIBS AND NOT DEFINED ENV{NO_OPUS_OGG_LIBS})
    message("NO_OPUS_OGG_LIBS has not been set. Linking Opus and Ogg libraries!")

    # Add FLAC library
    set(FLAC_LIBRARY ${CMAKE_CURRENT_SOURCE_DIR}/libs/${ANDROID_ABI}/libFLAC.so)
    target_link_libraries(${PLUGIN_NAME} PRIVATE ${FLAC_LIBRARY})

    # Add opus library
    set(OPUS_LIBRARY ${CMAKE_CURRENT_SOURCE_DIR}/libs/${ANDROID_ABI}/libopus.so)
    target_link_libraries(${PLUGIN_NAME} PRIVATE ${OPUS_LIBRARY})
    set(INSTALL_BUNDLE_LIB_DIR "${CMAKE_INSTALL_PREFIX}/lib")

    # Add ogg library
    set(OGG_LIBRARY ${CMAKE_CURRENT_SOURCE_DIR}/libs/${ANDROID_ABI}/libogg.so)
    target_link_libraries(${PLUGIN_NAME} PRIVATE ${OGG_LIBRARY})
    set(INSTALL_BUNDLE_LIB_DIR "${CMAKE_INSTALL_PREFIX}/lib")

    # Add vorbis library
    set(VORBIS_LIBRARY ${CMAKE_CURRENT_SOURCE_DIR}/libs/${ANDROID_ABI}/libvorbis.so)
    target_link_libraries(${PLUGIN_NAME} PRIVATE ${VORBIS_LIBRARY})
    set(VORBISFILE_LIBRARY ${CMAKE_CURRENT_SOURCE_DIR}/libs/${ANDROID_ABI}/libvorbisfile.so)
    target_link_libraries(${PLUGIN_NAME} PRIVATE ${VORBISFILE_LIBRARY})

    include_directories(${CMAKE_CURRENT_SOURCE_DIR}/include)
else()
    message("NO_OPUS_OGG_LIBS has been set. Not linking Opus and Ogg libraries!")
    add_definitions(-DNO_OPUS_OGG_LIBS)
endif()

# Support Android 15 16k page size
target_link_options("${PLUGIN_NAME}" PRIVATE "-Wl,-z,max-page-size=16384")

# ─── Optimization flags ───
# Base flags: -O3 for aggressive optimization, -ffast-math for relaxed FP
# (safe for audio DSP where strict IEEE 754 compliance is not required).
target_compile_options("${PLUGIN_NAME}" PRIVATE
  -Wall -Wno-error -fPIC
  -O3
  -ffast-math
  -funroll-loops
  -fomit-frame-pointer
  -ffunction-sections
  -fdata-sections
  -DNDEBUG
)

# ─── NEON / SIMD: per-ABI intrinsics ───
# arm64-v8a: NEON is mandatory — no extra flags needed.
# armeabi-v7a: explicitly enable NEON. Safe since minSdk 21 (Lollipop)
#   guarantees ARMv7 + NEON on all real-world devices.
# x86 / x86_64: enable SSE2/SSE3 for emulator and Chromebook builds.
if (ANDROID_ABI STREQUAL "armeabi-v7a")
    target_compile_options("${PLUGIN_NAME}" PRIVATE -mfpu=neon -mfloat-abi=softfp)
elseif (ANDROID_ABI STREQUAL "x86")
    target_compile_options("${PLUGIN_NAME}" PRIVATE -msse2 -msse3)
elseif (ANDROID_ABI STREQUAL "x86_64")
    target_compile_options("${PLUGIN_NAME}" PRIVATE -msse2 -msse3)
endif()

# ─── Link-Time Optimization ───
# Enables cross-TU inlining — significant win for many small cpp files.
include(CheckIPOSupported)
check_ipo_supported(RESULT IPO_SUPPORTED OUTPUT IPO_OUTPUT)
if (IPO_SUPPORTED)
    set_target_properties("${PLUGIN_NAME}" PROPERTIES INTERPROCEDURAL_OPTIMIZATION TRUE)
else()
    message(WARNING "LTO not supported: ${IPO_OUTPUT}")
endif()

# Strip unused sections from the final .so
target_link_options("${PLUGIN_NAME}" PRIVATE "-Wl,--gc-sections")

# List of absolute paths to libraries that should be bundled with the plugin.
set(flutter_soloud_bundled_libraries
  $<TARGET_FILE:${PLUGIN_NAME}>
  PARENT_SCOPE
)
