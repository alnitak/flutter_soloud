cmake_minimum_required(VERSION 3.16)
project(flutter_soloud_native_bench LANGUAGES C CXX)

if(APPLE)
  enable_language(OBJCXX)
endif()

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_C_STANDARD 11)

option(ENABLE_SANITIZERS "Build with AddressSanitizer and UndefinedBehaviorSanitizer" OFF)

get_filename_component(REPO_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/../.." ABSOLUTE)

set(SOLOUD_BACKEND_NULL TRUE)
set(SOLOUD_BACKEND_MINIAUDIO FALSE)
set(USE_OPENMPT FALSE)
include("${REPO_ROOT}/src/src.cmake")

set(BENCH_SOURCES
  "${CMAKE_CURRENT_SOURCE_DIR}/native_bench_test.cpp"
  ${TARGET_SOURCES}
  "${REPO_ROOT}/src/pffft/pffft.c"
  "${REPO_ROOT}/src/soloud_common.cpp"
  "${REPO_ROOT}/src/analyzer.cpp"
  "${REPO_ROOT}/src/filters/amplitude_modulator_filter.cpp"
  "${REPO_ROOT}/src/filters/compressor.cpp"
  "${REPO_ROOT}/src/filters/custom_filter.cpp"
  "${REPO_ROOT}/src/filters/limiter.cpp"
  "${REPO_ROOT}/src/filters/parametric_eq_filter.cpp"
  "${REPO_ROOT}/src/filters/pitch_shift_filter.cpp"
  "${REPO_ROOT}/src/synth/basic_wave.cpp"
  "${REPO_ROOT}/src/audiobuffer/circular_float_buffer.cpp"
  "${REPO_ROOT}/src/audiobuffer/mp3_stream_decoder.cpp"
  "${REPO_ROOT}/src/audiobuffer/m4a_metadata.cpp"
  "${REPO_ROOT}/src/audiobuffer/aac_stream_decoder.cpp"
  "${REPO_ROOT}/src/native_decoder/native_audio_decoder.cpp"
  "${REPO_ROOT}/src/mixeroutput/mixer_output.cpp"
  "${REPO_ROOT}/src/mixeroutput/mixer_output_encoder.cpp"
  "${REPO_ROOT}/src/mixeroutput/pcm_converter.cpp"
  "${REPO_ROOT}/src/mixeroutput/pcm_queue.cpp"
  "${REPO_ROOT}/src/mixeroutput/wav_output_encoder.cpp"
  "${REPO_ROOT}/src/mixeroutput/opus_output_encoder.cpp"
  "${REPO_ROOT}/src/mixeroutput/vorbis_output_encoder.cpp"
  "${REPO_ROOT}/src/mixeroutput/flac_output_encoder.cpp"
)

if(APPLE)
  list(APPEND BENCH_SOURCES
    "${REPO_ROOT}/src/native_decoder/os_decoder_apple.mm"
    "${REPO_ROOT}/src/audiobuffer/aac_stream_decoder_apple.mm"
  )
elseif(ANDROID)
  list(APPEND BENCH_SOURCES
    "${REPO_ROOT}/src/native_decoder/os_decoder_android.cpp"
    "${REPO_ROOT}/src/audiobuffer/aac_stream_decoder_android.cpp"
  )
elseif(WIN32)
  list(APPEND BENCH_SOURCES
    "${REPO_ROOT}/src/native_decoder/os_decoder_windows.cpp"
    "${REPO_ROOT}/src/audiobuffer/aac_stream_decoder_windows.cpp"
  )
elseif(UNIX)
  list(APPEND BENCH_SOURCES
    "${REPO_ROOT}/src/native_decoder/os_decoder_linux.cpp"
    "${REPO_ROOT}/src/audiobuffer/aac_stream_decoder_linux.cpp"
  )
endif()

add_executable(native_bench_test ${BENCH_SOURCES})

target_include_directories(native_bench_test PRIVATE
  "${REPO_ROOT}/src"
  "${REPO_ROOT}/src/soloud/include"
  "${REPO_ROOT}/src/soloud/src/audiosource/wav"
  "${REPO_ROOT}/src/pffft"
  "${REPO_ROOT}/src/filters"
  "${REPO_ROOT}/src/synth"
  "${REPO_ROOT}/src/audiobuffer"
  "${REPO_ROOT}/src/native_decoder"
  "${REPO_ROOT}/src/mixeroutput"
)

target_compile_definitions(native_bench_test PRIVATE
  WITH_NULL
  NO_XIPH_LIBS
  _USE_MATH_DEFINES
  NOMINMAX
)

if(MSVC)
  target_compile_options(native_bench_test PRIVATE /O2 /Oi /Ot /fp:precise /EHsc)
  if(ENABLE_SANITIZERS)
    target_compile_options(native_bench_test PRIVATE /fsanitize=address)
  endif()
else()
  target_compile_options(native_bench_test PRIVATE
    $<$<CONFIG:Release>:-O3>
    -fno-math-errno
    -fno-trapping-math
  )
  if(CMAKE_SYSTEM_PROCESSOR MATCHES "^(x86_64|AMD64|amd64)$")
    target_compile_options(native_bench_test PRIVATE -msse2)
  elseif(ANDROID AND ANDROID_ABI STREQUAL "armeabi-v7a")
    target_compile_options(native_bench_test PRIVATE -mfpu=neon)
  endif()
  if(ENABLE_SANITIZERS)
    target_compile_options(native_bench_test PRIVATE -O1 -g -fno-omit-frame-pointer -fsanitize=address,undefined)
    target_link_options(native_bench_test PRIVATE -fsanitize=address,undefined)
  endif()
endif()

find_package(Threads REQUIRED)
target_link_libraries(native_bench_test PRIVATE Threads::Threads ${LINK_LIBRARIES})

if(APPLE)
  target_link_libraries(native_bench_test PRIVATE
    "-framework AudioToolbox"
    "-framework CoreFoundation"
    "-framework Foundation"
  )
elseif(ANDROID)
  target_link_libraries(native_bench_test PRIVATE
    mediandk
    android
    log
    ${CMAKE_DL_LIBS}
    m
  )
elseif(WIN32)
  target_link_libraries(native_bench_test PRIVATE
    ole32
    mfplat
    mfreadwrite
    mfuuid
    propsys
  )
elseif(UNIX)
  target_link_libraries(native_bench_test PRIVATE ${CMAKE_DL_LIBS} m)
endif()
