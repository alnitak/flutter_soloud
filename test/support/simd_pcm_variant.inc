// Included by test/common/simd_pcm_test.cpp twice: once in a TU built with
// the host SIMD backend, once (test/support/simd_pcm_scalar_ref.cpp) with
// -DDISABLE_SIMD. SIMD_PCM_VARIANT names the namespace of the non-inline
// wrappers, so the two builds of the inline kernels never meet at link time
// (the kernels' own namespaces are renamed by the includer).
#include <cstddef>
#include <cstdint>

namespace SIMD_PCM_VARIANT {
void deinterleaveStereoFloats(const float *s, float *l, float *r, size_t n);
void deinterleaveQuadFloats(const float *s, float *d0, float *d1, float *d2,
                            float *d3, size_t n);
void deinterleaveFloatsToPlanar(const float *s, float *d, size_t stride,
                                size_t n, uint32_t ch);
void interleaveStereoFloats(const float *l, const float *r, float *d,
                            size_t n);
void convertS8ToFloats(const int8_t *s, float *d, size_t n);
void convertInt16ToFloats(const int16_t *s, float *d, size_t n);
void deinterleaveStereoInt16ToFloats(const int16_t *s, float *l, float *r,
                                     size_t n);
void convertInt32ToFloats(const int32_t *s, float *d, size_t n, float inv);
void interleavePlanarInt32ToFloats(const int32_t *const *s, float *d,
                                   size_t n, uint32_t ch, float inv);
void convertFloatsToS8Clamped(const float *s, int8_t *d, size_t n);
void convertFloatsToInt16Clamped(const float *s, int16_t *d, size_t n);
void convertFloatsToInt32ScaledClamped(const float *s, int32_t *d, size_t n,
                                       float scale);
void convertFloatsToInt32Clamped(const float *s, int32_t *d, size_t n);
double sumOfSquaresF32(const float *s, size_t n);
float peakAbsF32(const float *s, size_t n);
void clipHardInPlaceF32(float *b, size_t n, float lo, float hi, float post);
void clipRoundoffInPlaceF32(float *b, size_t n, float post);
} // namespace SIMD_PCM_VARIANT

#ifdef SIMD_PCM_DEFINE_WRAPPERS
namespace SIMD_PCM_VARIANT {
void deinterleaveStereoFloats(const float *s, float *l, float *r, size_t n) {
  SoLoud::deinterleaveStereoFloats(s, l, r, n);
}
void deinterleaveQuadFloats(const float *s, float *d0, float *d1, float *d2,
                            float *d3, size_t n) {
  SoLoud::deinterleaveQuadFloats(s, d0, d1, d2, d3, n);
}
void deinterleaveFloatsToPlanar(const float *s, float *d, size_t stride,
                                size_t n, uint32_t ch) {
  SoLoud::deinterleaveFloatsToPlanar(s, d, stride, n, ch);
}
void interleaveStereoFloats(const float *l, const float *r, float *d,
                            size_t n) {
  SoLoud::interleaveStereoFloats(l, r, d, n);
}
void convertS8ToFloats(const int8_t *s, float *d, size_t n) {
  SoLoud::convertS8ToFloats(s, d, n);
}
void convertInt16ToFloats(const int16_t *s, float *d, size_t n) {
  SoLoud::convertInt16ToFloats(s, d, n);
}
void deinterleaveStereoInt16ToFloats(const int16_t *s, float *l, float *r,
                                     size_t n) {
  SoLoud::deinterleaveStereoInt16ToFloats(s, l, r, n);
}
void convertInt32ToFloats(const int32_t *s, float *d, size_t n, float inv) {
  SoLoud::convertInt32ToFloats(s, d, n, inv);
}
void interleavePlanarInt32ToFloats(const int32_t *const *s, float *d,
                                   size_t n, uint32_t ch, float inv) {
  SoLoud::interleavePlanarInt32ToFloats(s, d, n, ch, inv);
}
void convertFloatsToS8Clamped(const float *s, int8_t *d, size_t n) {
  SoLoud::convertFloatsToS8Clamped(s, d, n);
}
void convertFloatsToInt16Clamped(const float *s, int16_t *d, size_t n) {
  SoLoud::convertFloatsToInt16Clamped(s, d, n);
}
void convertFloatsToInt32ScaledClamped(const float *s, int32_t *d, size_t n,
                                       float scale) {
  SoLoud::convertFloatsToInt32ScaledClamped(s, d, n, scale);
}
void convertFloatsToInt32Clamped(const float *s, int32_t *d, size_t n) {
  SoLoud::convertFloatsToInt32Clamped(s, d, n);
}
double sumOfSquaresF32(const float *s, size_t n) {
  return SoLoud::sumOfSquaresF32(s, n);
}
float peakAbsF32(const float *s, size_t n) { return SoLoud::peakAbsF32(s, n); }
void clipHardInPlaceF32(float *b, size_t n, float lo, float hi, float post) {
  SoLoud::clipHardInPlaceF32(b, n, lo, hi, post);
}
void clipRoundoffInPlaceF32(float *b, size_t n, float post) {
  SoLoud::clipRoundoffInPlaceF32(b, n, post);
}
} // namespace SIMD_PCM_VARIANT
#endif
