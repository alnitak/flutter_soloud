// The scalar build of src/simd/simd_pcm.h for test/common/simd_pcm_test.cpp.
// Compiled with -DDISABLE_SIMD; the kernels' namespaces are renamed so this
// TU's inline functions cannot be merged with the SIMD TU's at link time.
#ifndef DISABLE_SIMD
#error "build this file with -DDISABLE_SIMD"
#endif

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <memory>

#define SoLoud SoLoudScalarBuild
#define soloud_simd soloud_simd_scalar_build
#include "simd/simd_pcm.h"

#define SIMD_PCM_VARIANT simd_pcm_scalar
#define SIMD_PCM_DEFINE_WRAPPERS
#include "simd_pcm_variant.inc"
