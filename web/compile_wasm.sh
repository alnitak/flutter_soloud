#!/bin/bash
set -euo pipefail

rm -f libflutter_soloud_plugin.*
rm -r build
mkdir build
cd build


#https://emscripten.org/docs/tools_reference/emcc.html
#-g3 #keep debug info, including JS whitespace, function names
#-sSTACK_SIZE=1048576 -sALLOW_MEMORY_GROWTH

# disable the asynchronous startup/loading behaviour
# -s BINARYEN_ASYNC_COMPILATION=0
# https://github.com/emscripten-core/emscripten/issues/5352#issuecomment-312384604

# https://emscripten.org/docs/tools_reference/settings_reference.html

# -s ASSERTIONS=1 
# -s TOTAL_MEMORY=512MB \
# -s DEFAULT_TO_CXX \
# -s STACK_SIZE=1048576 
# -msimd128 for sse3 https://emscripten.org/docs/porting/simd.html

## Compiling with "-O2" or "-O3" doesn't work

em++ \
-I ../../src -I ../../src/filters -I ../../src/synth -I ../../src/soloud/include \
-I ../../src/soloud/src -I ../../src/soloud/include \
../../src/soloud/src/core/*.c* \
../../src/soloud/src/filter/*.c* \
../../src/soloud/src/backend/miniaudio/*.c* \
../../src/soloud/src/audiosource/ay/*.c* \
../../src/soloud/src/audiosource/speech/*.c* \
../../src/soloud/src/audiosource/wav/*.c* \
../../src/common.cpp \
../../src/bindings.cpp \
../../src/player.cpp \
../../src/analyzer.cpp \
../../src/synth/basic_wave.cpp \
../../src/filters/*.cpp \
-O3 -D WITH_MINIAUDIO \
-msimd128 -msse3 \
-I ~/.emscripten_cache/sysroot/include \
-s "EXPORTED_RUNTIME_METHODS=['ccall','cwrap']" \
-s "EXPORTED_FUNCTIONS=['_free', '_malloc']" \
-s EXPORT_ALL=1 -s NO_EXIT_RUNTIME=1 \
-s SAFE_HEAP=1 \
-s ALLOW_MEMORY_GROWTH \
-o ../../web/libflutter_soloud_plugin.js
