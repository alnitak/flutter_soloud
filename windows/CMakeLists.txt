cmake_minimum_required(VERSION 3.10)
set(PROJECT_NAME "flutter_soloud")
project(${PROJECT_NAME} LANGUAGES CXX)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

set(PLUGIN_NAME "${PROJECT_NAME}_plugin")

set(SRC_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../src")

## Add SoLoud custom cmake files
message("**************** SOLOUD CONFIGURE.CMAKE")
include (Configure.cmake)
message("**************** SOLOUD SRC.CMAKE 1")
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
include_directories(${SRC_DIR})
include (src.cmake)
message("**************** SOLOUD SRC.CMAKE 2  ${TARGET_NAME}")




list(APPEND PLUGIN_SOURCES
  ${TARGET_SOURCES}
  "${SRC_DIR}/common.cpp"
  "${SRC_DIR}/bindings.cpp"
  "${SRC_DIR}/player.cpp"
  "${SRC_DIR}/analyzer.cpp"
  "${SRC_DIR}/synth/basic_wave.cpp"
  "${SRC_DIR}/waveform/waveform.cpp"
  "${SRC_DIR}/audiobuffer/audiobuffer.cpp"
  "${SRC_DIR}/filters/filters.cpp"
  "${SRC_DIR}/filters/pitch_shift_filter.cpp"
  "${SRC_DIR}/filters/smbPitchShift.cpp"
  "${SRC_DIR}/filters/limiter.cpp"
  "${SRC_DIR}/filters/compressor.cpp"
)

add_library(${PLUGIN_NAME} SHARED
  ${PLUGIN_SOURCES}
)
apply_standard_settings(${PLUGIN_NAME})
set_target_properties(${PLUGIN_NAME} PROPERTIES
  CXX_VISIBILITY_PRESET hidden)


  
target_compile_definitions(${PLUGIN_NAME} PRIVATE FLUTTER_PLUGIN_IMPL)
target_include_directories(${PLUGIN_NAME} INTERFACE "${CMAKE_CURRENT_SOURCE_DIR}/include")
# target_link_libraries(${PLUGIN_NAME} PRIVATE flutter)

if(DEFINED ENV{NO_OPUS_OGG_LIBS})
    message("NO_OPUS_OGG_LIBS environment variable has been set to: $ENV{NO_OPUS_OGG_LIBS}")
endif()

if(NOT DEFINED ENV{NO_OPUS_OGG_LIBS} OR NOT ("$ENV{NO_OPUS_OGG_LIBS}" STREQUAL "1"))
    message("Linking Opus and Ogg libraries statically!")

    if(MSVC)
      # Enable static linking of the MSVC runtime
      set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
      # For the plugin
      set(CMAKE_CXX_FLAGS_RELEASE "${CMAKE_CXX_FLAGS_RELEASE} /MT")
      set(CMAKE_CXX_FLAGS_DEBUG "${CMAKE_CXX_FLAGS_DEBUG} /MTd")
    endif()

    # Add opus and ogg static libraries
    set(OPUS_LIBRARY ${CMAKE_CURRENT_SOURCE_DIR}/libs/opus.lib)
    set(OGG_LIBRARY ${CMAKE_CURRENT_SOURCE_DIR}/libs/ogg.lib)
    
    # Add the static library dependencies
    target_link_libraries(${PLUGIN_NAME} PRIVATE 
        ${OPUS_LIBRARY}
        ${OGG_LIBRARY}
    )

    # Set the include directory
    target_include_directories(${PLUGIN_NAME} PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include)

    # Add compile definition
    target_compile_definitions(${PLUGIN_NAME} PRIVATE LIBOPUS_OGG_AVAILABLE)

    # Remove DLL copy commands since we're using static libraries
endif()

target_compile_options(${PLUGIN_NAME} PRIVATE 
    /W4     # Warning level 4
    /WX-    # Disable warnings as errors
    /arch:SSE2  # Enable SSE2 instructions
    /arch:SSE3  # Enable SSE3 instructions
)

# List of absolute paths to libraries that should be bundled with the plugin.
set(flutter_soloud_bundled_libraries
  $<TARGET_FILE:${PLUGIN_NAME}>
  PARENT_SCOPE
)