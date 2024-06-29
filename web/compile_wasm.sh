#!/bin/bash

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

# -DMA_ENABLE_AUDIO_WORKLETS -sAUDIO_WORKLET=1 -sWASM_WORKERS=1 -sASYNCIFY
# https://github.com/mackron/miniaudio/issues/597#issuecomment-1445060662
# https://github.com/mackron/miniaudio/commit/810cdc238077ce86197e6c8cf70b06c5eea3e26f

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
../../src/bindings_capture.cpp \
../../src/capture.cpp \
../../src/synth/basic_wave.cpp \
../../src/filters/filters.cpp \
-O1 -D WITH_MINIAUDIO \
-I ~/.emscripten_cache/sysroot/include \
-s "EXPORTED_RUNTIME_METHODS=['ccall','cwrap']" \
-s "EXPORTED_FUNCTIONS=['_free', '_malloc']" \
-s EXPORT_ALL=1 -s NO_EXIT_RUNTIME=1 \
-s ASSERTIONS=1 -s SAFE_HEAP=1 \
-s TOTAL_MEMORY=512MB \
--shell-file ../html_template.html \
-s DEFAULT_TO_CXX \
-s STACK_SIZE=1048576 -s ALLOW_MEMORY_GROWTH \
-o ../../web/libflutter_soloud_plugin.js

#emcc -o main.html ../../src/main.c --shell-file ../html_template.html \
#-I ~/.emscripten_cache/sysroot/include \
#-s EXPORT_ALL=1 -s NO_EXIT_RUNTIME=1 -s "EXPORTED_RUNTIME_METHODS=['ccall']"
