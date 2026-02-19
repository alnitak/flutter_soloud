cmake_minimum_required(VERSION 3.10)
set(PROJECT_NAME "flutter_soloud")
project(${PROJECT_NAME} LANGUAGES CXX C)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

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

target_compile_options("${PLUGIN_NAME}" PRIVATE -Wall -Wno-error -fPIC -O2) #  -ldl -lpthread -lm

# List of absolute paths to libraries that should be bundled with the plugin.
set(flutter_soloud_bundled_libraries
  $<TARGET_FILE:${PLUGIN_NAME}>
  PARENT_SCOPE
)
