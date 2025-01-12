#!/bin/bash

# This script builds the Ogg and Opus libraries for WebAssembly
#
# emscripten must be installed and activated before running this script
# The script will git clone the libs, compile them and makes a fat library for iOS and MacOS

# Exit on any error
set -e

# Clone repositories if they don't exist
if [ ! -d "ogg" ]; then
    git clone https://github.com/xiph/ogg
fi

if [ ! -d "opus" ]; then
    git clone https://github.com/xiph/opus
fi

# Directories for source code and build output
LIBS=("ogg" "opus")
BASE_DIR="$PWD"
BUILD_DIR="$BASE_DIR/build"
OUTPUT_DIR="$BASE_DIR/../web"

# rm -rf $BUILD_DIR
# rm -rf $OUTPUT_DIR

# Create build and output directories
mkdir -p $BUILD_DIR
mkdir -p $OUTPUT_DIR

# Function to build a library for a specific architecture
build_lib() {
    local lib_name=$1
    local output_dir="$BUILD_DIR/$lib_name"

    echo "Building $lib_name..."

    cd "$lib_name"

    if [ "$lib_name" = "ogg" ]; then
        ./autogen.sh
        emcc -O3 -I./include \
            src/bitwise.c \
            src/framing.c \
            -s WASM=1 \
            -s EXPORTED_FUNCTIONS="['_malloc', '_free']" \
            -s EXPORTED_RUNTIME_METHODS="['ccall', 'cwrap']" \
            -s MODULARIZE=1 \
            -s EXPORT_NAME="'Module_$lib_name'" \
            -o "$OUTPUT_DIR/$lib_name.js"

    elif [ "$lib_name" = "opus" ]; then
        # Clean any previous build artifacts
        make clean || true
        
        # Configure opus with validation
        emconfigure ./configure --disable-extra-programs --disable-doc \
            --disable-asm --disable-rtcd --disable-intrinsics || exit 1

        # Core opus source files
        OPUS_SOURCES="
            src/opus.c
            src/opus_decoder.c
            src/opus_encoder.c
            src/opus_multistream.c
            src/opus_multistream_decoder.c
            src/opus_multistream_encoder.c
            src/repacketizer.c
            celt/bands.c
            celt/celt.c
            celt/celt_encoder.c
            celt/celt_decoder.c
            celt/cwrs.c
            celt/entcode.c
            celt/entdec.c
            celt/entenc.c
            celt/kiss_fft.c
            celt/laplace.c
            celt/mathops.c
            celt/mdct.c
            celt/modes.c
            celt/pitch.c
            celt/quant_bands.c
            celt/rate.c
            celt/vq.c
            silk/fixed/LTP_analysis_filter_FIX.c
            silk/fixed/LTP_scale_ctrl_FIX.c
            silk/fixed/corrMatrix_FIX.c
            silk/fixed/encode_frame_FIX.c
            silk/fixed/find_LPC_FIX.c
            silk/fixed/find_LTP_FIX.c
            silk/fixed/find_pitch_lags_FIX.c
            silk/fixed/find_pred_coefs_FIX.c
            silk/fixed/noise_shape_analysis_FIX.c
            silk/fixed/process_gains_FIX.c
            silk/fixed/regularize_correlations_FIX.c
            silk/fixed/residual_energy_FIX.c
            silk/fixed/warped_autocorrelation_FIX.c
            silk/A2NLSF.c
            silk/CNG.c
            silk/HP_variable_cutoff.c
            silk/NLSF2A.c
            silk/NLSF_decode.c
            silk/NLSF_encode.c
            silk/NSQ.c
            silk/NSQ_del_dec.c
            silk/PLC.c
            silk/VAD.c
            silk/VQ_WMat_EC.c
            silk/ana_filt_bank_1.c
            silk/biquad_alt.c
            silk/bwexpander.c
            silk/bwexpander_32.c
            silk/check_control_input.c
            silk/code_signs.c
            silk/control_audio_bandwidth.c
            silk/control_codec.c
            silk/control_SNR.c
            silk/debug.c
            silk/decoder_set_fs.c
            silk/decode_core.c
            silk/decode_frame.c
            silk/decode_indices.c
            silk/decode_parameters.c
            silk/decode_pitch.c
            silk/decode_pulses.c
            silk/dec_API.c
            silk/encode_indices.c
            silk/encode_pulses.c
            silk/enc_API.c
            silk/gain_quant.c
            silk/init_decoder.c
            silk/init_encoder.c
            silk/inner_prod_aligned.c
            silk/interpolate.c
            silk/lin2log.c
            silk/log2lin.c
            silk/LP_variable_cutoff.c
            silk/LPC_analysis_filter.c
            silk/LPC_inv_pred_gain.c
            silk/pitch_est_tables.c
            silk/process_NLSFs.c
            silk/quant_LTP_gains.c
            silk/resampler.c
            silk/resampler_down2.c
            silk/resampler_down2_3.c
            silk/resampler_private_AR2.c
            silk/resampler_private_down_FIR.c
            silk/resampler_private_IIR_FIR.c
            silk/resampler_private_up2_HQ.c
            silk/resampler_rom.c
            silk/shell_coder.c
            silk/sigm_Q15.c
            silk/sort.c
            silk/stereo_decode_pred.c
            silk/stereo_encode_pred.c
            silk/stereo_find_predictor.c
            silk/stereo_LR_to_MS.c
            silk/stereo_MS_to_LR.c
            silk/stereo_quant_pred.c
            silk/sum_sqr_shift.c
            silk/table_LSF_cos.c
            silk/tables_gain.c
            silk/tables_LTP.c
            silk/tables_NLSF_CB_NB_MB.c
            silk/tables_NLSF_CB_WB.c
            silk/tables_other.c
            silk/tables_pitch_lag.c
            silk/tables_pulses_per_block.c"

        # Compile Opus
        emcc  \
            -I. -I./include -I./silk -I./celt -I./silk/fixed -I./silk/float \
            -DHAVE_CONFIG_H \
            -DFIXED_POINT=1 \
            -DDISABLE_FLOAT_API \
            -DOPUS_BUILD=1 \
            -DUSE_ALLOCA=1 \
            $OPUS_SOURCES \
            -s WASM=1 \
            -s EXPORTED_FUNCTIONS="['_malloc', '_free', '_opus_decoder_create', '_opus_decoder_destroy', '_opus_decode']" \
            -s EXPORTED_RUNTIME_METHODS="['ccall', 'cwrap', 'setValue', 'getValue']" \
            -s EXPORT_ALL=1 -s NO_EXIT_RUNTIME=1 \
            -s MODULARIZE=1 \
            -s SAFE_HEAP=1 \
            -s EXPORT_NAME="'Module_$lib_name'" \
            -s ALLOW_MEMORY_GROWTH=1 \
            -s ASSERTIONS=1 \
            -s VERBOSE=1 \
            -o "$OUTPUT_DIR/$lib_name.js"
    fi

    cd ..
}

# Create output libraries
for lib in "${LIBS[@]}"; do
    echo "Creating libraries for $lib..."
    build_lib $lib
done

echo
echo
echo "Libraries created in $OUTPUT_DIR:"
ls -la $OUTPUT_DIR
