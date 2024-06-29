cmake_minimum_required(VERSION 3.10)
set(PROJECT_NAME "flutter_soloud")
project(${PROJECT_NAME} LANGUAGES CXX)
set(CMAKE_CXX_STANDARD 14)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

set(PLUGIN_NAME "${PROJECT_NAME}_plugin")

if (EMSCRIPTEN)
    set(CMAKE_AR "emcc")
    set(CMAKE_STATIC_LIBRARY_SUFFIX ".js")
    set(CMAKE_C_CREATE_STATIC_LIBRARY "<CMAKE_AR> -o <TARGET> <LINK_FLAGS> <OBJECTS>")
    set(CMAKE_CXX_CREATE_STATIC_LIBRARY "<CMAKE_AR> -o <TARGET> <LINK_FLAGS> <OBJECTS>")
endif()

set(CMAKE_C_FLAGS "-s STANDALONE_WASM" )


## Add SoLoud custom cmake files
message("**************** SOLOUD CONFIGURE.CMAKE")
include (Configure.cmake)
message("**************** SOLOUD SRC.CMAKE 1")
include_directories(../src/soloud/include )
include_directories(../src/soloud/src)
include (src.cmake)
message("**************** SOLOUD SRC.CMAKE 2  ${TARGET_NAME}")


list(APPEND PLUGIN_SOURCES
  "../src/common.cpp"
  "../src/bindings.cpp"
  "../src/player.cpp"
  "../src/analyzer.cpp"
  "../src/bindings_capture.cpp"
  "../src/capture.cpp"
  "../src/synth/basic_wave.cpp"
  "../src/filters/filters.cpp"

  # add SoLoud sources. These definitions are in src.cmake
  ${TARGET_SOURCES}
)
add_library(${PLUGIN_NAME} STATIC
  ${PLUGIN_SOURCES}
)

  
target_compile_definitions(${PLUGIN_NAME} PRIVATE FLUTTER_PLUGIN_IMPL)
target_include_directories(${PLUGIN_NAME} INTERFACE "${CMAKE_CURRENT_SOURCE_DIR}/src")
target_link_libraries(${PLUGIN_NAME} PRIVATE flutter)


target_compile_options(${PLUGIN_NAME} PRIVATE -Wall -Wno-error -O3 -DNDEBUG -s MAIN_MODULE=1)

target_compile_definitions (${PLUGIN_NAME} PRIVATE __WASM__)

