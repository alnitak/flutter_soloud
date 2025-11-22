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
  "${SRC_DIR}/waveform/miniaudio_libvorbis.cpp"
  "${SRC_DIR}/audiobuffer/audiobuffer.cpp"
  "${SRC_DIR}/audiobuffer/stream_decoder.cpp"
  "${SRC_DIR}/audiobuffer/flac_stream_decoder.cpp"
  "${SRC_DIR}/audiobuffer/opus_stream_decoder.cpp"
  "${SRC_DIR}/audiobuffer/vorbis_stream_decoder.cpp"
  "${SRC_DIR}/audiobuffer/mp3_stream_decoder.cpp"
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

if(NOT DEFINED ENV{NO_OPUS_OGG_LIBS})
    message("NO_OPUS_OGG_LIBS has not been set. Linking Opus and Ogg libraries!")

    if(MSVC)
      # Enable static linking of the MSVC runtime
      set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
      # For the plugin
      set(CMAKE_CXX_FLAGS_RELEASE "${CMAKE_CXX_FLAGS_RELEASE} /MT")
      set(CMAKE_CXX_FLAGS_DEBUG "${CMAKE_CXX_FLAGS_DEBUG} /MTd")
    endif()

    # Add opus library
    set(OPUS_LIBRARY ${CMAKE_CURRENT_SOURCE_DIR}/libs/opus.lib)
    # Link the library to your target
    target_link_libraries(${PLUGIN_NAME} PRIVATE ${OPUS_LIBRARY})

    # Add ogg library
    set(OGG_LIBRARY ${CMAKE_CURRENT_SOURCE_DIR}/libs/ogg.lib)
    # Link the library to your target
    target_link_libraries(${PLUGIN_NAME} PRIVATE ${OGG_LIBRARY})

    # Add vorbis library
    set(VORBIS_LIBRARY ${CMAKE_CURRENT_SOURCE_DIR}/libs/vorbis.lib)
    set(VORBISFILE_LIBRARY ${CMAKE_CURRENT_SOURCE_DIR}/libs/vorbisfile.lib)
    # Link the library to your target
    target_link_libraries(${PLUGIN_NAME} PRIVATE ${VORBIS_LIBRARY})
    target_link_libraries(${PLUGIN_NAME} PRIVATE ${VORBISFILE_LIBRARY})

    # Add FLAC library
    set(FLAC_LIBRARY ${CMAKE_CURRENT_SOURCE_DIR}/libs/FLAC.lib)
    # Link the library to your target
    target_link_libraries(${PLUGIN_NAME} PRIVATE ${FLAC_LIBRARY})

    # Set the ogg, opus, vorbis, vorbisfile and flac include directory
    include_directories(${CMAKE_CURRENT_SOURCE_DIR}/include)

    # Copy DLLs to output directory for both Debug and Release configurations
    add_custom_command(TARGET ${PLUGIN_NAME} POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${CMAKE_CURRENT_SOURCE_DIR}/libs/opus.dll"
        "${CMAKE_BINARY_DIR}/runner/$<CONFIG>/opus.dll"
    )

    add_custom_command(TARGET ${PLUGIN_NAME} POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${CMAKE_CURRENT_SOURCE_DIR}/libs/ogg.dll"
        "${CMAKE_BINARY_DIR}/runner/$<CONFIG>/ogg.dll"
    )

    add_custom_command(TARGET ${PLUGIN_NAME} POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${CMAKE_CURRENT_SOURCE_DIR}/libs/vorbis.dll"
        "${CMAKE_BINARY_DIR}/runner/$<CONFIG>/vorbis.dll"
    )

    add_custom_command(TARGET ${PLUGIN_NAME} POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${CMAKE_CURRENT_SOURCE_DIR}/libs/vorbisfile.dll"
        "${CMAKE_BINARY_DIR}/runner/$<CONFIG>/vorbisfile.dll"
    )

    add_custom_command(TARGET ${PLUGIN_NAME} POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${CMAKE_CURRENT_SOURCE_DIR}/libs/FLAC.dll"
        "${CMAKE_BINARY_DIR}/runner/$<CONFIG>/FLAC.dll"
    )
else()
    message("NO_OPUS_OGG_LIBS has been set. Not linking Opus and Ogg libraries!")
    add_definitions(-DNO_OPUS_OGG_LIBS)
endif()

if(MSVC)
  # Force optimize flags for all configurations
  string(REPLACE "/O2" "/Ox" CMAKE_CXX_FLAGS_RELEASE "${CMAKE_CXX_FLAGS_RELEASE}")
  string(REPLACE "/Ob1" "/Ob2" CMAKE_CXX_FLAGS_RELEASE "${CMAKE_CXX_FLAGS_RELEASE}")
  string(REPLACE "/Od" "/Ox" CMAKE_CXX_FLAGS_DEBUG "${CMAKE_CXX_FLAGS_DEBUG}")
  string(REPLACE "/Od" "/Ox" CMAKE_CXX_FLAGS_RELWITHDEBINFO "${CMAKE_CXX_FLAGS_RELWITHDEBINFO}")
  
  # Remove RTC1 from debug flags
  string(REPLACE "/RTC1" "" CMAKE_CXX_FLAGS_DEBUG "${CMAKE_CXX_FLAGS_DEBUG}")
  string(REPLACE "/RTC1" "" CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS}")
  
  # Use static runtime for all configurations
  foreach(CONFIG DEBUG RELEASE RELWITHDEBINFO MINSIZEREL)
    string(REPLACE "/MD" "/MT" CMAKE_CXX_FLAGS_${CONFIG} "${CMAKE_CXX_FLAGS_${CONFIG}}")
    string(REPLACE "/MDd" "/MT" CMAKE_CXX_FLAGS_${CONFIG} "${CMAKE_CXX_FLAGS_${CONFIG}}")
  endforeach()

  # Advanced optimization flags
  set(OPTIMIZATION_FLAGS
    /GL     # Whole program optimization
    /Gy     # Function-level linking
    /Oi     # Generate intrinsic functions
    /Ot     # Favor fast code
    /Ox     # Full optimization
    /fp:fast # Fast floating-point model
    /arch:SSE2  # Use SSE2 instructions
    /Qpar   # Auto-parallelize loops
    /O2     # Maximize speed
    /RTC-   # Disable runtime checks explicitly
  )

  # Add debug info generation for release builds
  set(OPTIMIZATION_FLAGS "${OPTIMIZATION_FLAGS} /Zi")
  set(CMAKE_EXE_LINKER_FLAGS_RELEASE "${CMAKE_EXE_LINKER_FLAGS_RELEASE} /DEBUG /OPT:REF /OPT:ICF")
  set(CMAKE_SHARED_LINKER_FLAGS_RELEASE "${CMAKE_SHARED_LINKER_FLAGS_RELEASE} /DEBUG /OPT:REF /OPT:ICF")
  
  # Apply optimization flags to all configurations
  foreach(FLAG ${OPTIMIZATION_FLAGS})
    target_compile_options(${PLUGIN_NAME} PRIVATE ${FLAG})
  endforeach()

  # Linker optimization flags
  set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} /LTCG /OPT:REF /OPT:ICF")
  set(CMAKE_SHARED_LINKER_FLAGS "${CMAKE_SHARED_LINKER_FLAGS} /LTCG /OPT:REF /OPT:ICF")

  # Replace existing compile options with optimized ones
  target_compile_options(${PLUGIN_NAME} PRIVATE 
    /W4     # Warning level 4 
    /WX-    # Disable warnings as errors
    ${OPTIMIZATION_FLAGS}
  )

  # Create a custom target for copying PDB files
  # This target will only be part of the default build for configurations other than Profile.
  add_custom_target(copy_pdbs $<IF:$<NOT:$<CONFIG:Profile>>,ALL,>>
    # copy to "build" dir
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
      "$<TARGET_FILE_DIR:${PLUGIN_NAME}>/${PLUGIN_NAME}.pdb"
      "${CMAKE_BINARY_DIR}/../../${PLUGIN_NAME}.pdb"
    DEPENDS ${PLUGIN_NAME}
    COMMENT "Copying PDB files to build directory"
    VERBATIM
  )

endif()

# List of absolute paths to libraries that should be bundled with the plugin.
set(flutter_soloud_bundled_libraries
  $<TARGET_FILE:${PLUGIN_NAME}>
  PARENT_SCOPE
)