# Common CMake configuration for the flutter_soloud plugin.
# This file is included by each platform's CMakeLists.txt to avoid duplication.
#
# Before including this file, the platform CMakeLists.txt must have set:
#   - PROJECT_NAME  (usually "flutter_soloud")
#   - PLUGIN_NAME   (usually "${PROJECT_NAME}_plugin")
#   - SRC_DIR       (path to the src/ directory, typically "${CMAKE_CURRENT_SOURCE_DIR}/../src")
#
# After including this file, the following variables are available:
#   - PLUGIN_SOURCES  (all common source files + SoLoud TARGET_SOURCES)
#   - TARGET_SOURCES  (SoLoud engine sources, from src.cmake)
#   - LINK_LIBRARIES  (SoLoud link libraries, from src.cmake)

# --- SoLoud configuration ---
message("**************** SOLOUD CONFIGURE.CMAKE")
include (${SRC_DIR}/Configure.cmake)

message("**************** SOLOUD SRC.CMAKE START")
if (UNIX AND NOT WIN32 AND NOT APPLE)
	if (CMAKE_SIZEOF_VOID_P MATCHES "8")
		set (LIB_POSTFIX "64" CACHE STRING "suffix for 32/64 dir placement")
		mark_as_advanced (LIB_POSTFIX)
	endif ()
endif ()
if (MSVC)
	add_definitions (-D_CRT_SECURE_NO_WARNINGS)
endif()
if (NOT DEFINED LIB_POSTFIX)
  set (LIB_POSTFIX "")
endif ()

include_directories(${SRC_DIR}/soloud/include)
include_directories(${SRC_DIR}/soloud/src)
include_directories(${SRC_DIR}/pffft)
include_directories(${SRC_DIR})
include (${SRC_DIR}/src.cmake)
message("**************** SOLOUD SRC.CMAKE END  ${TARGET_NAME}")

# --- Plugin source files (common across all platforms) ---
list(APPEND PLUGIN_SOURCES
  "${SRC_DIR}/common.cpp"
  "${SRC_DIR}/bindings.cpp"
  "${SRC_DIR}/player.cpp"
  "${SRC_DIR}/analyzer.cpp"
  "${SRC_DIR}/pffft/pffft.c"
  "${SRC_DIR}/synth/basic_wave.cpp"
  "${SRC_DIR}/waveform/waveform.cpp"
  "${SRC_DIR}/waveform/miniaudio_libvorbis.cpp"
  "${SRC_DIR}/audiobuffer/audiobuffer.cpp"
  "${SRC_DIR}/audiobuffer/stream_decoder.cpp"
  "${SRC_DIR}/audiobuffer/opus_stream_decoder.cpp"
  "${SRC_DIR}/audiobuffer/vorbis_stream_decoder.cpp"
  "${SRC_DIR}/audiobuffer/mp3_stream_decoder.cpp"
  "${SRC_DIR}/audiobuffer/flac_stream_decoder.cpp"
  "${SRC_DIR}/filters/filters.cpp"
  "${SRC_DIR}/filters/pitch_shift_filter.cpp"
  "${SRC_DIR}/filters/limiter.cpp"
  "${SRC_DIR}/filters/compressor.cpp"
  "${SRC_DIR}/filters/parametric_eq_filter.cpp"
  ${TARGET_SOURCES}
)
