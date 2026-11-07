// Cross-platform SIMD, DSP accuracy, memory alignment, leak-regression,
// and throughput microbenchmark suite for flutter_soloud.
//
// Supported platforms:
//   - macOS (ARM64 NEON, x86_64 SSE2/AVX2)
//   - Linux (x86_64 SSE2/AVX2, ARM64 NEON)
//   - Windows (x86_64 SSE2/AVX2, ARM64 / ARM64EC NEON)
//
// Build & run:
//   macOS / Linux: ./test/native_bench/run_native_bench.sh [--all|--sanitize]
//   Windows:       test\native_bench\run_native_bench.bat [--sanitize]

#include "simd/soloud_simd.h"
#include "simd/simd_pcm.h"

#include "enums.h"
#include "analyzer.h"
#include "pffft/pffft.h"

#include "soloud/include/soloud.h"
#include "soloud/include/soloud_bassboostfilter.h"
#include "soloud/include/soloud_biquadresonantfilter.h"
#include "soloud/include/soloud_dcremovalfilter.h"
#include "soloud/include/soloud_echofilter.h"
#include "soloud/include/soloud_eqfilter.h"
#include "soloud/include/soloud_flangerfilter.h"
#include "soloud/include/soloud_freeverbfilter.h"
#include "soloud/include/soloud_lofifilter.h"
#include "soloud/include/soloud_robotizefilter.h"
#include "soloud/include/soloud_wav.h"
#include "soloud/include/soloud_waveshaperfilter.h"
#include "soloud/include/soloud_wavstream.h"
#include "soloud/src/audiosource/wav/mb_ogg.h"

#include "filters/amplitude_modulator_filter.h"
#include "filters/compressor.h"
#include "filters/limiter.h"
#include "filters/parametric_eq_filter.h"
#include "filters/pitch_shift_filter.h"
#include "filters/signalsmith-linear/fft.h"

#include "synth/basic_wave.h"

#include "audiobuffer/buffer.h"
#include "audiobuffer/circular_float_buffer.h"
#include "audiobuffer/realtime_ring_buffer.h"

#include "mixeroutput/mixer_output.h"
#include "mixeroutput/pcm_converter.h"
#include "mixeroutput/wav_output_encoder.h"

#include "native_decoder/native_audio_decoder.h"

#include <algorithm>
#include <chrono>
#include <climits>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <limits>
#include <memory>
#include <string>
#include <vector>

namespace {

int gAssertions = 0;
int gFailures = 0;

#define CHECK_TRUE(cond, ...)                                               \
  do {                                                                      \
    ++gAssertions;                                                          \
    if (!(cond)) {                                                          \
      ++gFailures;                                                          \
      std::fprintf(stderr, "  FAIL [%s:%d]: ", __FILE__, __LINE__);         \
      std::fprintf(stderr, __VA_ARGS__);                                    \
      std::fprintf(stderr, "\n");                                           \
    }                                                                       \
  } while (0)

#define CHECK_NEAR(actual, expected, tol, ...)                              \
  do {                                                                      \
    const double _a = static_cast<double>(actual);                          \
    const double _e = static_cast<double>(expected);                        \
    const double _d = std::fabs(_a - _e);                                   \
    CHECK_TRUE(_d <= static_cast<double>(tol),                              \
               "%s (got %.8g, expected %.8g, diff %.8g > %.8g)",            \
               __VA_ARGS__, _a, _e, _d, static_cast<double>(tol));          \
  } while (0)

const char *activeSimdBackend() {
#if defined(SOLOUD_SIMD_NEON_AARCH64)
  return "ARM64 NEON (AArch64)";
#elif defined(SOLOUD_SIMD_NEON)
  return "ARMv7 NEON";
#elif defined(SOLOUD_SIMD_SSE2)
#if defined(__AVX2__)
  return "x86_64 SSE2 + AVX2/FMA";
#else
  return "x86_64 SSE2";
#endif
#elif defined(SOLOUD_SIMD_WASM)
  return "WebAssembly SIMD128";
#else
  return "Scalar Fallback";
#endif
}

template <typename T>
inline void doNotOptimize(const T &val) {
#if defined(_MSC_VER)
  volatile const T *sink = &val;
  (void)sink;
#else
  asm volatile("" : : "g"(val) : "memory");
#endif
}

// ============================================================================
// 1. SIMD Primitives, Fast Math, Bitwise Finite Checks, & Denormal Flushing
// ============================================================================
void testSimdPrimitivesAndMath() {
  std::printf("[1/7] Testing soloud_simd primitives, fast_log2/exp2, FTZ/DAZ...\n");
  using namespace soloud_simd;

  const f32x4 a = f32x4::set(1.5f, -2.0f, 3.25f, -4.5f);
  const f32x4 b = f32x4::set(2.0f, 0.5f, -1.0f, 4.0f);
  float out[4] = {};

  add(a, b).storeu(out);
  CHECK_NEAR(out[0], 3.5f, 1e-6f, "f32x4 add[0]");
  CHECK_NEAR(out[1], -1.5f, 1e-6f, "f32x4 add[1]");
  CHECK_NEAR(out[2], 2.25f, 1e-6f, "f32x4 add[2]");
  CHECK_NEAR(out[3], -0.5f, 1e-6f, "f32x4 add[3]");

  fma(f32x4::splat(1.0f), a, b).storeu(out);
  CHECK_NEAR(out[0], 4.0f, 1e-6f, "f32x4 fma[0]");
  CHECK_NEAR(out[1], 0.0f, 1e-6f, "f32x4 fma[1]");
  CHECK_NEAR(out[2], -2.25f, 1e-6f, "f32x4 fma[2]");
  CHECK_NEAR(out[3], -17.0f, 1e-6f, "f32x4 fma[3]");

  fms(f32x4::splat(10.0f), a, b).storeu(out);
  CHECK_NEAR(out[0], 7.0f, 1e-6f, "f32x4 fms[0]");
  CHECK_NEAR(out[3], 28.0f, 1e-6f, "f32x4 fms[3]");

  CHECK_NEAR(reduce_add(a), -1.75f, 1e-6f, "f32x4 reduce_add");
  CHECK_NEAR(reduce_max(abs(a)), 4.5f, 1e-6f, "f32x4 reduce_max(abs)");

  const f32x4 sq = sqrt(f32x4::set(0.0f, 1.0f, 4.0f, 9.0f));
  sq.storeu(out);
  CHECK_NEAR(out[0], 0.0f, 1e-5f, "f32x4 sqrt(0)");
  CHECK_NEAR(out[1], 1.0f, 1e-5f, "f32x4 sqrt(1)");
  CHECK_NEAR(out[2], 2.0f, 1e-5f, "f32x4 sqrt(4)");
  CHECK_NEAR(out[3], 3.0f, 1e-5f, "f32x4 sqrt(9)");

  // Minimax fast_log2 & fast_exp2 accuracy across 8 decades
  double maxLog2Err = 0.0;
  double maxExp2RelErr = 0.0;
  for (int k = 0; k < 256; ++k) {
    const float x = std::pow(10.0f, -4.0f + 8.0f * (static_cast<float>(k) / 255.0f));
    const float vLog = fast_log2(x);
    float vecOut[4] = {};
    fast_log2(f32x4::splat(x)).storeu(vecOut);
    const double refLog = std::log2(static_cast<double>(x));
    maxLog2Err = std::max(maxLog2Err, std::fabs(static_cast<double>(vLog) - refLog));
    maxLog2Err = std::max(maxLog2Err, std::fabs(static_cast<double>(vecOut[0]) - refLog));

    const float eArg = -12.0f + 24.0f * (static_cast<float>(k) / 255.0f);
    const float vExp = fast_exp2(eArg);
    fast_exp2(f32x4::splat(eArg)).storeu(vecOut);
    const double refExp = std::exp2(static_cast<double>(eArg));
    maxExp2RelErr = std::max(maxExp2RelErr, std::fabs(static_cast<double>(vExp) - refExp) / refExp);
    maxExp2RelErr = std::max(maxExp2RelErr, std::fabs(static_cast<double>(vecOut[0]) - refExp) / refExp);
  }
  CHECK_TRUE(maxLog2Err < 2.5e-4, "fast_log2 max error %.6g >= 2.5e-4", maxLog2Err);
  CHECK_TRUE(maxExp2RelErr < 2.5e-4, "fast_exp2 max rel error %.6g >= 2.5e-4", maxExp2RelErr);

  // Bitwise IEEE-754 finite/NaN checks
  const float qnan = std::numeric_limits<float>::quiet_NaN();
  const float pinf = std::numeric_limits<float>::infinity();
  const float ninf = -std::numeric_limits<float>::infinity();
  const float subnormal = 1.0e-40f;
  CHECK_TRUE(isFiniteBits(0.0f) && isFiniteBits(-0.0f), "isFiniteBits(+-0)");
  CHECK_TRUE(isFiniteBits(1.0f) && isFiniteBits(-12345.0f), "isFiniteBits(normal)");
  CHECK_TRUE(isFiniteBits(subnormal), "isFiniteBits(subnormal)");
  CHECK_TRUE(!isFiniteBits(qnan) && isNanBits(qnan), "isFiniteBits/isNanBits(NaN)");
  CHECK_TRUE(!isFiniteBits(pinf) && !isNanBits(pinf), "isFiniteBits(+Inf)");
  CHECK_TRUE(!isFiniteBits(ninf) && !isNanBits(ninf), "isFiniteBits(-Inf)");
  CHECK_NEAR(sanitizeFinite(qnan, 0.0f), 0.0f, 0.0f, "sanitizeFinite(NaN)");

  // ScopedFlushDenormals RAII nest & restore test
  {
    ScopedFlushDenormals outer;
    {
      ScopedFlushDenormals inner;
      volatile float tiny = 1.0e-30f;
      for (int i = 0; i < 20; ++i) tiny *= 0.1f;
      CHECK_TRUE(isFiniteBits(tiny), "ScopedFlushDenormals keeps arithmetic finite");
    }
  }
}

// ============================================================================
// 2. SIMD PCM Conversions, Deinterleaving/Interleaving, & Clippers
// ============================================================================
void testSimdPcmAndClippers() {
  std::printf("[2/7] Testing simd_pcm conversions, strided deinterleave, clippers...\n");

  // Test deinterleaveFloatsToPlanar across channels {1,2,3,4,6,8}, odd frame
  // counts {0,1,3,4,7,16,31,67}, and specifically channelStride > frames!
  const uint32_t channelCounts[] = {1, 2, 3, 4, 6, 8};
  const size_t frameCounts[] = {0, 1, 3, 4, 7, 16, 31, 67};
  for (uint32_t ch : channelCounts) {
    for (size_t frames : frameCounts) {
      const size_t stride = frames + 19; // deliberately > frames (regression test!)
      std::vector<float> src(frames * ch);
      for (size_t f = 0; f < frames; ++f) {
        for (uint32_t c = 0; c < ch; ++c) {
          src[f * ch + c] = static_cast<float>(f * 100 + c + 1);
        }
      }
      std::vector<float> dst(stride * ch, -999.0f);
      SoLoud::deinterleaveFloatsToPlanar(src.data(), dst.data(), stride, frames, ch);

      bool ok = true;
      for (uint32_t c = 0; c < ch; ++c) {
        for (size_t f = 0; f < frames; ++f) {
          if (dst[c * stride + f] != static_cast<float>(f * 100 + c + 1)) ok = false;
        }
        // Guard region between [frames, stride) must remain untouched!
        for (size_t f = frames; f < stride; ++f) {
          if (dst[c * stride + f] != -999.0f) ok = false;
        }
      }
      CHECK_TRUE(ok, "deinterleaveFloatsToPlanar ch=%u frames=%zu stride=%zu", ch, frames, stride);
    }
  }

  // Stereo float interleave <-> deinterleave roundtrip across odd tail lengths
  for (size_t frames : {size_t(0), size_t(1), size_t(5), size_t(17), size_t(64)}) {
    std::vector<float> l(frames), r(frames), inter(frames * 2), lOut(frames), rOut(frames);
    for (size_t i = 0; i < frames; ++i) {
      l[i] = std::sin(static_cast<float>(i) * 0.1f);
      r[i] = std::cos(static_cast<float>(i) * 0.1f);
    }
    SoLoud::interleaveStereoFloats(l.data(), r.data(), inter.data(), frames);
    SoLoud::deinterleaveStereoFloats(inter.data(), lOut.data(), rOut.data(), frames);
    bool match = true;
    for (size_t i = 0; i < frames; ++i) {
      if (l[i] != lOut[i] || r[i] != rOut[i]) match = false;
    }
    CHECK_TRUE(match, "stereo interleave/deinterleave roundtrip frames=%zu", frames);
  }

  // S8, S16, S24LE, S32 <-> Float32 conversions + saturation & NaN edge cases
  for (size_t n : {size_t(1), size_t(7), size_t(16), size_t(33), size_t(129)}) {
    std::vector<float> fIn(n);
    for (size_t i = 0; i < n; ++i) {
      fIn[i] = -1.0f + 2.0f * (static_cast<float>(i) / static_cast<float>(std::max<size_t>(1, n - 1)));
    }

    std::vector<int8_t> s8(n);
    std::vector<float> fFromS8(n);
    SoLoud::convertFloatsToS8Clamped(fIn.data(), s8.data(), n);
    SoLoud::convertS8ToFloats(s8.data(), fFromS8.data(), n);
    double maxS8Err = 0.0;
    for (size_t i = 0; i < n; ++i) {
      const float expected = std::min(127.0f / 128.0f, fIn[i]);
      maxS8Err = std::max(maxS8Err, std::fabs(double(fFromS8[i]) - double(expected)));
    }
    CHECK_TRUE(maxS8Err <= (1.5 / 128.0), "S8<->F32 roundtrip n=%zu err=%g", n, maxS8Err);

    std::vector<int16_t> s16(n);
    std::vector<float> fFromS16(n);
    SoLoud::convertFloatsToInt16Clamped(fIn.data(), s16.data(), n);
    SoLoud::convertInt16ToFloats(s16.data(), fFromS16.data(), n);
    double maxS16Err = 0.0;
    for (size_t i = 0; i < n; ++i) {
      const float expected = std::min(32767.0f / 32768.0f, fIn[i]);
      maxS16Err = std::max(maxS16Err, std::fabs(double(fFromS16[i]) - double(expected)));
    }
    CHECK_TRUE(maxS16Err <= (1.5 / 32768.0), "S16<->F32 roundtrip n=%zu err=%g", n, maxS16Err);

    std::vector<int32_t> s32(n);
    std::vector<float> fFromS32(n);
    SoLoud::convertFloatsToInt32Clamped(fIn.data(), s32.data(), n);
    SoLoud::convertInt32ToFloats(s32.data(), fFromS32.data(), n);
    double maxS32Err = 0.0;
    for (size_t i = 0; i < n; ++i) {
      maxS32Err = std::max(maxS32Err, std::fabs(double(fFromS32[i]) - double(fIn[i])));
    }
    CHECK_TRUE(maxS32Err <= 1e-6, "S32<->F32 roundtrip n=%zu err=%g", n, maxS32Err);
  }

  // 24-bit planar int32 (libFLAC format) <-> interleaved float + PCM_F32LE_PLANAR
  {
    const int32_t vals[10] = {
        -8388608, -4194304, -1, 0, 1, 123456, 4194304, 8388607, -123456, 654321};
    const int32_t *planar24[2] = {vals, vals + 5};
    float inter24[10] = {};
    SoLoud::interleavePlanarInt32ToFloats(planar24, inter24, 5, 2,
                                          1.0f / 8388608.0f);
    int32_t back24[10] = {};
    SoLoud::convertFloatsToInt32ScaledClamped(inter24, back24, 10, 8388607.0f);
    for (int i = 0; i < 5; ++i) {
      CHECK_NEAR(inter24[i * 2], float(vals[i]) / 8388608.0f, 1e-6f,
                 "interleavePlanarInt32ToFloats L");
      CHECK_NEAR(inter24[i * 2 + 1], float(vals[5 + i]) / 8388608.0f, 1e-6f,
                 "interleavePlanarInt32ToFloats R");
    }

    // Buffer::addData unaligned (+1 byte) PCM_S16LE & PCM_S32LE UBSan check
    Buffer unalignedAddBuf;
    unalignedAddBuf.setSizeInBytes(64 * sizeof(float));
    uint8_t rawUnaligned16[1 + 6 * sizeof(int16_t)] = {};
    const int16_t ref16[6] = {-32768, -16384, 0, 8192, 16384, 32767};
    std::memcpy(rawUnaligned16 + 1, ref16, sizeof(ref16));
    bool allAdded = false;
    CHECK_TRUE(unalignedAddBuf.addData(PCM_S16LE, rawUnaligned16 + 1, 6,
                                       &allAdded) == 6 &&
                   allAdded,
               "Buffer::addData unaligned PCM_S16LE");
    const float *outUnaligned =
        reinterpret_cast<const float *>(unalignedAddBuf.buffer.data());
    for (int i = 0; i < 6; ++i) {
      CHECK_NEAR(outUnaligned[i], float(ref16[i]) / 32768.0f, 1e-6f,
                 "Buffer::addData unaligned PCM_S16LE sample");
    }
  }

  // Critical S32 saturation & NaN regression check (+1.0f must NEVER wrap to INT32_MIN!)
  // Largest IEEE-754 float32 strictly < 2^31 is 2147483520.0f (0x7FFFFF80).
  {
    constexpr int32_t kMaxFloat32S32 = 2147483520;
    const float edgeFloats[8] = {
        1.0f, 5.0f, -1.0f, -5.0f, 0.0f,
        std::numeric_limits<float>::quiet_NaN(),
        std::numeric_limits<float>::infinity(),
        -std::numeric_limits<float>::infinity()};
    int32_t s32Out[8] = {};
    SoLoud::convertFloatsToInt32Clamped(edgeFloats, s32Out, 8);
    CHECK_TRUE(s32Out[0] == kMaxFloat32S32, "+1.0f -> %d (got %d)", kMaxFloat32S32, s32Out[0]);
    CHECK_TRUE(s32Out[1] == kMaxFloat32S32, "+5.0f -> %d (got %d)", kMaxFloat32S32, s32Out[1]);
    CHECK_TRUE(s32Out[2] == INT32_MIN, "-1.0f -> INT32_MIN (got %d)", s32Out[2]);
    CHECK_TRUE(s32Out[3] == INT32_MIN, "-5.0f -> INT32_MIN (got %d)", s32Out[3]);
    CHECK_TRUE(s32Out[4] == 0, "0.0f -> 0");
    CHECK_TRUE(s32Out[5] == 0, "NaN -> 0 (got %d)", s32Out[5]);
    CHECK_TRUE(s32Out[6] == kMaxFloat32S32, "+Inf -> %d (got %d)", kMaxFloat32S32, s32Out[6]);
    CHECK_TRUE(s32Out[7] == INT32_MIN, "-Inf -> INT32_MIN (got %d)", s32Out[7]);

    // Also verify PcmConverter on deliberately misaligned output pointer (+1 byte)
    std::vector<uint8_t> unalignedBuf(1 + 8 * sizeof(int32_t), 0);
    convertPcm(edgeFloats, unalignedBuf.data() + 1, 8, MIXER_OUTPUT_PCM_S32LE);
    int32_t unalignedS32[8] = {};
    std::memcpy(unalignedS32, unalignedBuf.data() + 1, sizeof(unalignedS32));
    CHECK_TRUE(unalignedS32[0] == kMaxFloat32S32 &&
                   unalignedS32[2] == INT32_MIN && unalignedS32[5] == 0,
               "convertPcm unaligned S32 saturation & NaN");
  }

  // sumOfSquaresF32, peakAbsF32, clipHardInPlaceF32, clipRoundoffInPlaceF32
  for (size_t n : {size_t(3), size_t(16), size_t(257), size_t(5003)}) {
    std::vector<float> v(n);
    double refSumSq = 0.0;
    float refPeak = 0.0f;
    for (size_t i = 0; i < n; ++i) {
      v[i] = -2.0f + 4.0f * (static_cast<float>(i) / static_cast<float>(n));
      refSumSq += static_cast<double>(v[i]) * static_cast<double>(v[i]);
      refPeak = std::max(refPeak, std::fabs(v[i]));
    }
    CHECK_NEAR(SoLoud::sumOfSquaresF32(v.data(), n), refSumSq, refSumSq * 1e-6 + 1e-6,
               "sumOfSquaresF32");
    CHECK_NEAR(SoLoud::peakAbsF32(v.data(), n), refPeak, 1e-6f, "peakAbsF32");

    std::vector<float> roundSimd = v;
    SoLoud::clipRoundoffInPlaceF32(roundSimd.data(), n, 0.9f);
    double maxClipDiff = 0.0;
    for (size_t i = 0; i < n; ++i) {
      float c = v[i];
      if (c <= -1.65f) c = -0.9862875f;
      else if (c >= 1.65f) c = 0.9862875f;
      else c = 0.87f * c - 0.1f * c * c * c;
      c *= 0.9f;
      maxClipDiff = std::max(maxClipDiff, std::fabs(double(roundSimd[i]) - double(c)));
    }
    CHECK_TRUE(maxClipDiff < 1e-6, "clipRoundoffInPlaceF32 n=%zu diff=%g", n, maxClipDiff);
  }
}

// ============================================================================
// 3. Memory Alignment & Leak Regression Tests
// ============================================================================
void testAlignmentAndLeakRegressions() {
  std::printf("[3/7] Testing 64-byte alignment & memory leak regressions...\n");

  // 64-byte alignment of soloud_simd::alignedAllocFloats & SoLoud::AlignedFloatBuffer
  for (size_t count : {size_t(1), size_t(17), size_t(512), size_t(4096)}) {
    auto p = soloud_simd::makeAlignedFloats(count);
    CHECK_TRUE(p != nullptr, "makeAlignedFloats(%zu) non-null", count);
    CHECK_TRUE((reinterpret_cast<uintptr_t>(p.get()) & 63u) == 0u,
               "makeAlignedFloats(%zu) 64-byte aligned", count);
  }

  SoLoud::AlignedFloatBuffer abuf;
  CHECK_TRUE(abuf.init(1024) == SoLoud::SO_NO_ERROR, "AlignedFloatBuffer::init");
  CHECK_TRUE((reinterpret_cast<uintptr_t>(abuf.mData) & 63u) == 0u,
             "AlignedFloatBuffer::mData is 64-byte aligned");

  SoLoud::TinyAlignedFloatBuffer tbuf;
  CHECK_TRUE((reinterpret_cast<uintptr_t>(tbuf.mData) & 63u) == 0u,
             "TinyAlignedFloatBuffer::mData is 64-byte aligned");

  // Repeated Soloud engine init/deinit cycles + internal scratch 64B alignment
  for (int iter = 0; iter < 8; ++iter) {
    SoLoud::Soloud engine;
    const SoLoud::result r = engine.init(
        SoLoud::Soloud::CLIP_ROUNDOFF | SoLoud::Soloud::ENABLE_VISUALIZATION,
        SoLoud::Soloud::NULLDRIVER, 48000, 512, 2);
    CHECK_TRUE(r == SoLoud::SO_NO_ERROR, "Soloud::init iter %d", iter);
    CHECK_TRUE((reinterpret_cast<uintptr_t>(engine.mScratch.mData) & 63u) == 0u,
               "Soloud::mScratch 64-byte aligned");
    CHECK_TRUE((reinterpret_cast<uintptr_t>(engine.mOutputScratch.mData) & 63u) == 0u,
               "Soloud::mOutputScratch 64-byte aligned");
    engine.deinit();
  }

  // Regression: signalsmith::linear::Pow2FFT & Pow2RealFFT resize(256) -> resize(0)
  // Previously leaked `plan` (and `Temporary::buffer`) when resized to 0!
  for (int iter = 0; iter < 16; ++iter) {
    signalsmith::linear::Pow2FFT<float> cfft(256);
    cfft.resize(0);
    signalsmith::linear::Pow2RealFFT<float> rfft(512);
    rfft.resize(0);
  }

  // Regression: Wav::loadRawWave(0-length, aCopy=false, aTakeOwnership=true)
  // and Wav/WavStream::loadMem(invalid, aCopy=false, aTakeOwnership=true)
  for (int iter = 0; iter < 8; ++iter) {
    float *rawZero = new float[4]();
    SoLoud::Wav wav;
    wav.loadRawWave(rawZero, 0, 44100.0f, 2, false, true);

    unsigned char *badMem1 = static_cast<unsigned char *>(std::malloc(32));
    std::memset(badMem1, 0xFF, 32);
    SoLoud::Wav wavBad;
    CHECK_TRUE(wavBad.loadMem(badMem1, 32, false, true) != SoLoud::SO_NO_ERROR,
               "Wav::loadMem invalid returns error and frees owned buffer");

    unsigned char *badMem2 = static_cast<unsigned char *>(std::malloc(32));
    std::memset(badMem2, 0xFF, 32);
    SoLoud::WavStream streamBad;
    CHECK_TRUE(streamBad.loadMem(badMem2, 32, false, true) != SoLoud::SO_NO_ERROR,
               "WavStream::loadMem invalid returns error and frees owned buffer");
  }

#if !defined(NO_XIPH_LIBS)
  {
    SoLoud::MBOggDecoder oggDec;
  }
#endif
}

// ============================================================================
// 4. PFFFT Real/Complex Accuracy & ParametricEq / Compressor / Limiter / DSP
// ============================================================================
void testPffftAndCustomDsp() {
  std::printf("[4/7] Testing PFFFT_REAL, ParametricEq, Compressor, Limiter, Synth...\n");

  // PFFFT_REAL forward + inverse roundtrip accuracy
  {
    const int N = 1024;
    PFFFT_Setup *setup = pffft_new_setup(N, PFFFT_REAL);
    CHECK_TRUE(setup != nullptr, "pffft_new_setup(1024, PFFFT_REAL)");
    float *in = static_cast<float *>(pffft_aligned_malloc(N * sizeof(float)));
    float *spec = static_cast<float *>(pffft_aligned_malloc(N * sizeof(float)));
    float *out = static_cast<float *>(pffft_aligned_malloc(N * sizeof(float)));
    float *work = static_cast<float *>(pffft_aligned_malloc(N * sizeof(float)));
    for (int i = 0; i < N; ++i) {
      in[i] = 0.6f * std::sin(2.0f * float(M_PI) * 7.0f * float(i) / float(N)) +
              0.3f * std::cos(2.0f * float(M_PI) * 53.0f * float(i) / float(N));
    }
    pffft_transform_ordered(setup, in, spec, work, PFFFT_FORWARD);
    pffft_transform_ordered(setup, spec, out, work, PFFFT_BACKWARD);
    double maxErr = 0.0;
    const float invN = 1.0f / static_cast<float>(N);
    for (int i = 0; i < N; ++i) {
      maxErr = std::max(maxErr, std::fabs(double(out[i] * invN) - double(in[i])));
    }
    CHECK_TRUE(maxErr < 1e-5, "PFFFT_REAL 1024 roundtrip maxErr=%g", maxErr);
    pffft_aligned_free(work);
    pffft_aligned_free(out);
    pffft_aligned_free(spec);
    pffft_aligned_free(in);
    pffft_destroy_setup(setup);
  }

  // ParametricEq (PFFFT_REAL STFT): flat identity & 1 kHz band boost
  {
    SoLoud::Soloud engine;
    engine.init(SoLoud::Soloud::CLIP_ROUNDOFF, SoLoud::Soloud::NULLDRIVER, 48000, 512, 2);

    ParametricEq eqFlat(&engine, 3);
    std::unique_ptr<SoLoud::FilterInstance> flatInst(eqFlat.createInstance());
    const unsigned int totalFrames = 8192;
    std::vector<float> sig(totalFrames), outFlat(totalFrames);
    for (unsigned int i = 0; i < totalFrames; ++i) {
      sig[i] = 0.4f * std::sin(2.0f * float(M_PI) * 1000.0f * float(i) / 48000.0f);
    }
    outFlat = sig;
    for (unsigned int off = 0; off < totalFrames; off += 512) {
      flatInst->filterChannel(outFlat.data() + off, 512, 48000.0f, 0.0, 0, 1);
    }
    // After 2048-sample STFT latency warmup, RMS must match input closely
    const double rmsIn = std::sqrt(SoLoud::sumOfSquaresF32(sig.data() + 3072, 4096) / 4096.0);
    const double rmsFlat = std::sqrt(SoLoud::sumOfSquaresF32(outFlat.data() + 3072, 4096) / 4096.0);
    CHECK_NEAR(rmsFlat, rmsIn, rmsIn * 0.05, "ParametricEq flat unity gain RMS");

    // Boost middle band (1000 Hz) to 2.5x linear gain via FilterInstance
    ParametricEq eqBoost(&engine, 3);
    std::unique_ptr<SoLoud::FilterInstance> boostInst(eqBoost.createInstance());
    boostInst->setFilterParameter(ParametricEq::BAND_FREQ_OFFSET + 1, 1000.0f);
    boostInst->setFilterParameter(ParametricEq::BAND_GAIN_OFFSET + 1, 2.5f);
    std::vector<float> outBoost = sig;
    for (unsigned int off = 0; off < totalFrames; off += 512) {
      boostInst->filterChannel(outBoost.data() + off, 512, 48000.0f, 0.0, 0, 1);
    }
    const double rmsBoost = std::sqrt(SoLoud::sumOfSquaresF32(outBoost.data() + 3072, 4096) / 4096.0);
    CHECK_TRUE(rmsBoost > rmsIn * 1.8, "ParametricEq 1kHz boost increases RMS (%g vs %g)", rmsBoost, rmsIn);

    engine.deinit();
  }

  // Compressor & Limiter peak control + finite output on burst -> silence decay
  {
    Compressor comp(48000);
    comp.setParam(Compressor::THRESHOLD, -18.0f);
    comp.setParam(Compressor::RATIO, 8.0f);
    std::unique_ptr<SoLoud::FilterInstance> compInst(comp.createInstance());

    Limiter lim(48000);
    lim.setParam(Limiter::THRESHOLD, -6.0f);
    lim.setParam(Limiter::OUTPUT_CEILING, -3.0f);
    std::unique_ptr<SoLoud::FilterInstance> limInst(lim.createInstance());

    const float ceilingLin = std::pow(10.0f, -3.0f / 20.0f) + 1e-4f;
    std::vector<float> buf(512 * 2);
    for (int block = 0; block < 16; ++block) {
      const float amp = (block < 8) ? 0.95f : 0.0f; // burst followed by silence decay
      for (unsigned int i = 0; i < 512; ++i) {
        const float s = amp * std::sin(2.0f * float(M_PI) * 440.0f * float(block * 512 + i) / 48000.0f);
        buf[i] = s;
        buf[512 + i] = -s;
      }
      compInst->filter(buf.data(), 512, 512, 2, 48000.0f, block * (512.0 / 48000.0));
      limInst->filter(buf.data(), 512, 512, 2, 48000.0f, block * (512.0 / 48000.0));
      for (float s : buf) {
        CHECK_TRUE(soloud_simd::isFiniteBits(s) && std::fabs(s) <= ceilingLin,
                   "Compressor+Limiter finite & <= ceiling (got %g)", s);
      }
    }
  }

  // Built-in SoLoud filters: Biquad, Freeverb, Echo, WaveShaper, Bassboost, Eq, Lofi, Flanger
  {
    SoLoud::BiquadResonantFilter bq;
    bq.setParams(SoLoud::BiquadResonantFilter::LOWPASS, 1200.0f, 2.0f);
    SoLoud::FreeverbFilter fv;
    SoLoud::EchoFilter echo;
    echo.setParams(0.05f, 0.5f, 0.3f);
    SoLoud::WaveShaperFilter ws;
    ws.setParams(0.8f);
    SoLoud::BassboostFilter bb;
    bb.setParams(4.0f);

    SoLoud::Filter *filters[] = {&bq, &fv, &echo, &ws, &bb};
    for (SoLoud::Filter *f : filters) {
      std::unique_ptr<SoLoud::FilterInstance> inst(f->createInstance());
      std::vector<float> planar(512 * 2);
      for (int i = 0; i < 512; ++i) {
        planar[i] = 0.5f * std::sin(0.05f * float(i));
        planar[512 + i] = 0.5f * std::cos(0.05f * float(i));
      }
      inst->filter(planar.data(), 512, 512, 2, 48000.0f, 0.0);
      bool allFinite = true;
      for (float x : planar) allFinite &= soloud_simd::isFiniteBits(x);
      CHECK_TRUE(allFinite, "Built-in filter produces finite output");
    }
  }

  // Basicwave synth across all waveforms (including WAVE_FSQUARE & WAVE_FSAW)
  for (int wf = 0; wf <= SoLoud::Soloud::WAVE_FSAW; ++wf) {
    for (bool superWave : {false, true}) {
      Basicwave bw(static_cast<SoLoud::Soloud::WAVEFORM>(wf), superWave, 0.5, 1.0);
      bw.setSamplerate(48000.0);
      bw.setFreq(440.0);
      std::unique_ptr<SoLoud::AudioSourceInstance> inst(bw.createInstance());
      inst->init(bw, 0);
      std::vector<float> audio(512, 0.0f);
      inst->getAudio(audio.data(), 512, 512);
      bool finite = true;
      double energy = 0.0;
      for (float x : audio) {
        finite &= soloud_simd::isFiniteBits(x);
        energy += double(x) * double(x);
      }
      CHECK_TRUE(finite && energy > 1e-4, "Basicwave wf=%d super=%d finite & non-silent", wf, int(superWave));
    }
  }
}

// ============================================================================
// 5. Resamplers (Linear & Catmull-Rom) & Core Mixer Verification
// ============================================================================
void testResamplersAndMixer() {
  std::printf("[5/7] Testing Linear & Catmull-Rom resamplers and Soloud::mix...\n");

  const unsigned int srcFrames = 4800;
  std::vector<float> sineStereo(srcFrames * 2);
  for (unsigned int i = 0; i < srcFrames; ++i) {
    const float s = 0.5f * std::sin(2.0f * float(M_PI) * 440.0f * float(i) / 48000.0f);
    sineStereo[i] = s;
    sineStereo[srcFrames + i] = -s;
  }

  for (unsigned int resampler : {unsigned(SoLoud::Soloud::RESAMPLER_POINT),
                                 unsigned(SoLoud::Soloud::RESAMPLER_LINEAR),
                                 unsigned(SoLoud::Soloud::RESAMPLER_CATMULLROM)}) {
    for (float speed : {0.5f, 1.0f, 1.37f, 2.25f}) {
      SoLoud::Soloud engine;
      engine.init(SoLoud::Soloud::CLIP_ROUNDOFF, SoLoud::Soloud::NULLDRIVER, 48000, 512, 2);
      engine.setMainResampler(resampler);

      SoLoud::Wav wav;
      wav.loadRawWave(sineStereo.data(), srcFrames * 2, 48000.0f, 2, true, false);
      wav.setLooping(true);
      const SoLoud::handle h = engine.play(wav, 0.8f, -0.25f);
      engine.setRelativePlaySpeed(h, speed);
      engine.fadeVolume(h, 0.4f, 0.05);
      engine.fadePan(h, 0.35f, 0.05);

      std::vector<float> mixed(512 * 2, 0.0f);
      double totalEnergy = 0.0;
      bool allFinite = true;
      for (int b = 0; b < 8; ++b) {
        engine.mix(mixed.data(), 512);
        for (float s : mixed) {
          allFinite &= soloud_simd::isFiniteBits(s);
          totalEnergy += double(s) * double(s);
        }
      }
      CHECK_TRUE(allFinite && totalEnergy > 1.0,
                 "Mixer resampler=%u speed=%.2f finite & audible", resampler, speed);
      engine.deinit();
    }
  }
}

// ============================================================================
// 6. Streaming Ring Buffers, RELEASED Buffer Compaction, & MixerOutput Wrap
// ============================================================================
void testStreamingAndMixerOutput() {
  std::printf("[6/7] Testing Buffer compaction, CircularFloatBuffer, RealtimeRingBuffer, MixerOutput...\n");

  // Buffer RELEASED mode amortised compaction over 500 push/pop cycles
  {
    Buffer buf;
    buf.setSizeInBytes(4096 * sizeof(float));
    buf.setBufferType(BufferingType::RELEASED);
    std::vector<int16_t> chunk(256);
    for (int cycle = 0; cycle < 500; ++cycle) {
      for (int i = 0; i < 256; ++i) chunk[i] = static_cast<int16_t>((cycle * 256 + i) & 0x7FFF);
      bool allAdded = false;
      const size_t added = buf.addData(BufferType::PCM_S16LE, chunk.data(), 256, &allAdded);
      CHECK_TRUE(added == 256 && allAdded, "Buffer::addData cycle %d", cycle);
      const float *rd = reinterpret_cast<const float *>(buf.buffer.data() + buf.getReadOffset());
      CHECK_NEAR(rd[0], static_cast<float>(chunk[0]) / 32768.0f, 1e-6f, "Buffer RELEASED sample[0]");
      buf.removeData(256 * sizeof(float));
    }
    CHECK_TRUE(buf.getFloatsBufferSize() == 0, "Buffer empty after 500 cycles");
  }

  // CircularFloatBuffer wrap-around + strided readPlanar
  {
    SoLoud::CircularFloatBuffer cbuf(200); // 100 stereo frames capacity
    std::vector<float> in(80 * 2);
    for (int pass = 0; pass < 10; ++pass) {
      for (int f = 0; f < 80; ++f) {
        in[f * 2 + 0] = float(pass * 100 + f);
        in[f * 2 + 1] = -float(pass * 100 + f);
      }
      CHECK_TRUE(cbuf.write(in.data(), in.size()) == in.size(), "CircularFloatBuffer::write");
      std::vector<float> planar(128 * 2, -777.0f); // stride 128 > 80 frames!
      CHECK_TRUE(cbuf.readPlanar(planar.data(), 128, 80, 2) == 80, "CircularFloatBuffer::readPlanar");
      bool ok = true;
      for (int f = 0; f < 80; ++f) {
        if (planar[f] != float(pass * 100 + f) || planar[128 + f] != -float(pass * 100 + f)) ok = false;
      }
      for (int f = 80; f < 128; ++f) {
        if (planar[f] != -777.0f || planar[128 + f] != -777.0f) ok = false;
      }
      CHECK_TRUE(ok, "CircularFloatBuffer wrap pass %d", pass);
    }
  }

  // RealtimeRingBuffer wrap-around + rewind history check
  {
    SoLoud::RealtimeRingBuffer rrb;
    rrb.init(2, 512, 256);
    std::vector<int16_t> pcm16(300 * 2);
    for (int pass = 0; pass < 8; ++pass) {
      for (int f = 0; f < 300; ++f) {
        pcm16[f * 2 + 0] = static_cast<int16_t>(f * 10);
        pcm16[f * 2 + 1] = static_cast<int16_t>(-f * 10);
      }
      CHECK_TRUE(rrb.pushInterleavedInt16(pcm16.data(), 300) == 300, "RealtimeRingBuffer::pushInterleavedInt16");
      const uint64_t savedRead = rrb.getReadFrame();
      std::vector<float> out1(320 * 2, 0.0f), out2(320 * 2, 0.0f);
      CHECK_TRUE(rrb.popPlanar(out1.data(), 300, 320) == 300, "RealtimeRingBuffer::popPlanar");
      // Rollback to savedRead (within history window!) and verify bit-identical re-read
      rrb.setReadFrame(savedRead);
      CHECK_TRUE(rrb.popPlanar(out2.data(), 300, 320) == 300, "RealtimeRingBuffer::popPlanar after rollback");
      CHECK_TRUE(std::memcmp(out1.data(), out2.data(), out1.size() * sizeof(float)) == 0,
                 "RealtimeRingBuffer checkpoint rollback exactness");
    }
  }

  // MixerOutput regression: non-frame-aligned bufferSizeBytes (e.g. 1003 bytes
  // for stereo PCM_S16LE = 4 bytes/frame) must round to whole frames so wrap
  // never splits multi-byte samples or swaps stereo channels!
  {
    MixerOutput &mo = MixerOutput::instance();
    const PlayerErrors err = mo.start(MIXER_OUTPUT_PCM_S16LE, 48000, 2,
                                      /*bufferSizeBytes=*/1003,
                                      /*notificationThresholdBytes=*/256,
                                      /*chunkPCMFrames=*/-1);
    CHECK_TRUE(err == PlayerErrors::noError, "MixerOutput::start non-aligned size");
    CHECK_TRUE((mo.getBufferSize() % 4) == 0,
               "MixerOutput bufferSize (%zu) aligned to frame size (4)", mo.getBufferSize());

    std::vector<float> block(128 * 2);
    for (int i = 0; i < 128; ++i) {
      block[i * 2 + 0] = 0.5f;   // Left = +16384
      block[i * 2 + 1] = -0.25f; // Right = -8192
    }
    // Overrun & wrap multiple times
    for (int k = 0; k < 12; ++k) {
      mo.onAudioData(block.data(), 128);
    }
    CHECK_TRUE((mo.getReadOffset() % 4) == 0 && (mo.getAvailableBytes() % 4) == 0,
               "MixerOutput readOffset & availableBytes stay frame-aligned on overrun");
    const size_t rdOff = mo.getReadOffset();
    int16_t lr[2] = {};
    std::memcpy(lr, mo.getBufferPointer() + rdOff, sizeof(lr));
    CHECK_TRUE(lr[0] > 16000 && lr[1] < -8000,
               "MixerOutput stereo channels intact after wrap/overrun (L=%d, R=%d)", lr[0], lr[1]);
    mo.stop();
  }
}

// ============================================================================
// 7. Throughput Microbenchmarks (MSamples/s)
// ============================================================================
template <typename Fn>
double measureMSamplesPerSec(size_t samplesPerIter, int minIters, Fn &&fn) {
  // Warmup
  for (int i = 0; i < 4; ++i) fn();

  using Clock = std::chrono::steady_clock;
  int iters = minIters;
  double elapsedSec = 0.0;
  while (true) {
    const auto t0 = Clock::now();
    for (int i = 0; i < iters; ++i) {
      fn();
    }
    const auto t1 = Clock::now();
    elapsedSec = std::chrono::duration<double>(t1 - t0).count();
    if (elapsedSec >= 0.04 || iters >= (1 << 20)) break;
    iters *= 4;
  }
  const double totalSamples = static_cast<double>(samplesPerIter) * static_cast<double>(iters);
  return (totalSamples / 1.0e6) / elapsedSec;
}

void runThroughputBenchmarks() {
  std::printf("[7/7] Running throughput microbenchmarks (SIMD backend: %s)...\n",
              activeSimdBackend());
  std::printf("  %-44s %14s\n", "Kernel / Subsystem", "Throughput");
  std::printf("  %-44s %14s\n", "--------------------------------------------", "--------------");

  constexpr size_t kFrames = 4096;
  constexpr size_t kStereoSamples = kFrames * 2;
  std::vector<float> fSrc(kStereoSamples), fDstL(kFrames), fDstR(kFrames), fInter(kStereoSamples);
  std::vector<int16_t> s16Buf(kStereoSamples);
  std::vector<int32_t> s32Buf(kStereoSamples);
  for (size_t i = 0; i < kStereoSamples; ++i) {
    fSrc[i] = 0.8f * std::sin(static_cast<float>(i) * 0.013f);
    s16Buf[i] = static_cast<int16_t>(fSrc[i] * 32767.0f);
  }

  auto report = [](const char *name, double msps) {
    std::printf("  %-44s %10.1f MS/s\n", name, msps);
  };

  report("deinterleaveStereoFloats (4096 fr)",
         measureMSamplesPerSec(kStereoSamples, 512, [&] {
           SoLoud::deinterleaveStereoFloats(fSrc.data(), fDstL.data(), fDstR.data(), kFrames);
           doNotOptimize(fDstL[0]);
         }));

  report("interleaveStereoFloats (4096 fr)",
         measureMSamplesPerSec(kStereoSamples, 512, [&] {
           SoLoud::interleaveStereoFloats(fDstL.data(), fDstR.data(), fInter.data(), kFrames);
           doNotOptimize(fInter[0]);
         }));

  report("convertInt16ToFloats (8192 smp)",
         measureMSamplesPerSec(kStereoSamples, 512, [&] {
           SoLoud::convertInt16ToFloats(s16Buf.data(), fInter.data(), kStereoSamples);
           doNotOptimize(fInter[0]);
         }));

  report("convertFloatsToInt16Clamped (8192 smp)",
         measureMSamplesPerSec(kStereoSamples, 512, [&] {
           SoLoud::convertFloatsToInt16Clamped(fSrc.data(), s16Buf.data(), kStereoSamples);
           doNotOptimize(s16Buf[0]);
         }));

  report("convertFloatsToInt32Clamped (8192 smp)",
         measureMSamplesPerSec(kStereoSamples, 512, [&] {
           SoLoud::convertFloatsToInt32Clamped(fSrc.data(), s32Buf.data(), kStereoSamples);
           doNotOptimize(s32Buf[0]);
         }));

  report("clipRoundoffInPlaceF32 (8192 smp)",
         measureMSamplesPerSec(kStereoSamples, 512, [&] {
           std::memcpy(fInter.data(), fSrc.data(), kStereoSamples * sizeof(float));
           SoLoud::clipRoundoffInPlaceF32(fInter.data(), kStereoSamples, 1.0f);
           doNotOptimize(fInter[0]);
         }));

  report("sumOfSquaresF32 + peakAbsF32 (8192 smp)",
         measureMSamplesPerSec(kStereoSamples, 512, [&] {
           double sq = SoLoud::sumOfSquaresF32(fSrc.data(), kStereoSamples);
           float pk = SoLoud::peakAbsF32(fSrc.data(), kStereoSamples);
           doNotOptimize(sq);
           doNotOptimize(pk);
         }));

  // PFFFT_REAL vs PFFFT_COMPLEX 1024-pt roundtrip
  {
    PFFFT_Setup *realSetup = pffft_new_setup(1024, PFFFT_REAL);
    PFFFT_Setup *cplxSetup = pffft_new_setup(1024, PFFFT_COMPLEX);
    float *rIn = static_cast<float *>(pffft_aligned_malloc(2048 * sizeof(float)));
    float *rOut = static_cast<float *>(pffft_aligned_malloc(2048 * sizeof(float)));
    float *rWork = static_cast<float *>(pffft_aligned_malloc(2048 * sizeof(float)));
    std::memset(rIn, 0, 2048 * sizeof(float));
    for (int i = 0; i < 1024; ++i) rIn[i] = fSrc[i];

    report("PFFFT_REAL 1024 fwd+inv",
           measureMSamplesPerSec(1024, 256, [&] {
             pffft_transform_ordered(realSetup, rIn, rOut, rWork, PFFFT_FORWARD);
             pffft_transform_ordered(realSetup, rOut, rIn, rWork, PFFFT_BACKWARD);
             doNotOptimize(rIn[0]);
           }));

    report("PFFFT_COMPLEX 1024 fwd+inv",
           measureMSamplesPerSec(1024, 256, [&] {
             pffft_transform_ordered(cplxSetup, rIn, rOut, rWork, PFFFT_FORWARD);
             pffft_transform_ordered(cplxSetup, rOut, rIn, rWork, PFFFT_BACKWARD);
             doNotOptimize(rIn[0]);
           }));

    pffft_aligned_free(rWork);
    pffft_aligned_free(rOut);
    pffft_aligned_free(rIn);
    pffft_destroy_setup(cplxSetup);
    pffft_destroy_setup(realSetup);
  }

  // Mixer + Resamplers (8 simultaneous stereo voices @ 1.37x speed)
  for (auto [label, resampler] :
       {std::pair<const char *, unsigned>{"Soloud::mix 8 voices Linear (512 fr)",
                                          SoLoud::Soloud::RESAMPLER_LINEAR},
        std::pair<const char *, unsigned>{"Soloud::mix 8 voices Catmull-Rom (512 fr)",
                                          SoLoud::Soloud::RESAMPLER_CATMULLROM}}) {
    SoLoud::Soloud engine;
    engine.init(SoLoud::Soloud::CLIP_ROUNDOFF, SoLoud::Soloud::NULLDRIVER, 48000, 512, 2);
    engine.setMainResampler(resampler);
    SoLoud::Wav wav;
    wav.loadRawWave(fSrc.data(), kStereoSamples, 48000.0f, 2, true, false);
    wav.setLooping(true);
    for (int v = 0; v < 8; ++v) {
      const SoLoud::handle h = engine.play(wav, 0.2f, -0.7f + 0.2f * float(v));
      engine.setRelativePlaySpeed(h, 1.37f);
    }
    std::vector<float> mixOut(512 * 2);
    report(label, measureMSamplesPerSec(512 * 2, 128, [&] {
             engine.mix(mixOut.data(), 512);
             doNotOptimize(mixOut[0]);
           }));
    engine.deinit();
  }

  // DSP Filters (Stereo 512-frame blocks = 1024 samples)
  {
    SoLoud::Soloud engine;
    engine.init(SoLoud::Soloud::CLIP_ROUNDOFF, SoLoud::Soloud::NULLDRIVER, 48000, 512, 2);
    ParametricEq peq(&engine, 8);
    std::unique_ptr<SoLoud::FilterInstance> peqInst(peq.createInstance());
    peqInst->setFilterParameter(ParametricEq::BAND_GAIN_OFFSET + 2, 1.8f);

    Compressor comp(48000);
    std::unique_ptr<SoLoud::FilterInstance> compInst(comp.createInstance());

    Limiter lim(48000);
    std::unique_ptr<SoLoud::FilterInstance> limInst(lim.createInstance());

    SoLoud::BiquadResonantFilter bq;
    bq.setParams(SoLoud::BiquadResonantFilter::LOWPASS, 1500.0f, 2.0f);
    std::unique_ptr<SoLoud::FilterInstance> bqInst(bq.createInstance());

    SoLoud::FreeverbFilter fv;
    std::unique_ptr<SoLoud::FilterInstance> fvInst(fv.createInstance());

    std::vector<float> block(512 * 2);
    report("ParametricEq 8-band stereo (512 fr)",
           measureMSamplesPerSec(1024, 128, [&] {
             std::memcpy(block.data(), fSrc.data(), 1024 * sizeof(float));
             peqInst->filter(block.data(), 512, 512, 2, 48000.0f, 0.0);
             doNotOptimize(block[0]);
           }));

    report("Compressor stereo (512 fr)",
           measureMSamplesPerSec(1024, 256, [&] {
             std::memcpy(block.data(), fSrc.data(), 1024 * sizeof(float));
             compInst->filter(block.data(), 512, 512, 2, 48000.0f, 0.0);
             doNotOptimize(block[0]);
           }));

    report("Limiter look-ahead stereo (512 fr)",
           measureMSamplesPerSec(1024, 256, [&] {
             std::memcpy(block.data(), fSrc.data(), 1024 * sizeof(float));
             limInst->filter(block.data(), 512, 512, 2, 48000.0f, 0.0);
             doNotOptimize(block[0]);
           }));

    report("BiquadResonantFilter stereo (512 fr)",
           measureMSamplesPerSec(1024, 256, [&] {
             std::memcpy(block.data(), fSrc.data(), 1024 * sizeof(float));
             bqInst->filter(block.data(), 512, 512, 2, 48000.0f, 0.0);
             doNotOptimize(block[0]);
           }));

    report("FreeverbFilter stereo (512 fr)",
           measureMSamplesPerSec(1024, 128, [&] {
             std::memcpy(block.data(), fSrc.data(), 1024 * sizeof(float));
             fvInst->filter(block.data(), 512, 512, 2, 48000.0f, 0.0);
             doNotOptimize(block[0]);
           }));

    engine.deinit();
  }
}

} // namespace

int main(int argc, char **argv) {
  (void)argc;
  (void)argv;
  std::setvbuf(stdout, nullptr, _IONBF, 0);
  std::printf("=== flutter_soloud Native Optimization & SIMD Suite (%s) ===\n",
              activeSimdBackend());

  testSimdPrimitivesAndMath();
  testSimdPcmAndClippers();
  testAlignmentAndLeakRegressions();
  testPffftAndCustomDsp();
  testResamplersAndMixer();
  testStreamingAndMixerOutput();
  runThroughputBenchmarks();

  std::printf("\nSummary: %d assertions, %d failures\n", gAssertions, gFailures);
  return (gFailures == 0) ? 0 : 1;
}
