// A failing allocator for test/common/out_of_memory_test.cpp.
//
// Replaces every global operator new / new[] / delete / delete[] (throwing,
// nothrow, sized and aligned forms). Include it in exactly one translation
// unit of the test binary.
//
//   oom::arm(n)     on the calling thread, let n allocations through and make
//                   the next one fail (throwing forms throw std::bad_alloc,
//                   nothrow forms return nullptr). Only one allocation fails;
//                   later ones succeed, so the code's recovery path runs with
//                   a working heap.
//   oom::disarm()   stop counting. Returns true when the failure was injected.
//   oom::liveTracked()
//                   allocations made by an armed thread that have not been
//                   freed yet, from any thread (the leak check).
//   oom::clearTracked()
//   oom::mallocInUse()
//                   bytes the C heap has in use (-1 under sanitizers), to
//                   catch leaks of malloc'd memory that operator new misses.
//
// Only operator new is intercepted. malloc / calloc / realloc (dr_libs,
// miniaudio, libc) are not: on macOS a binary cannot interpose malloc on
// itself, so allocation failures inside those C libraries are not covered.
#pragma once

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <new>

#if defined(__APPLE__)
#include <malloc/malloc.h>
#elif defined(__GLIBC__)
#include <malloc.h>
#endif

namespace oom {

namespace detail {
constexpr size_t kSlots = 1u << 18;
inline std::atomic<uintptr_t> gSlots[kSlots];
inline std::atomic<long> gTracked{0};
inline std::atomic<long> gInjected{0};
// -1: disarmed. >= 0: allocations still allowed before the failing one.
inline thread_local long tCountdown = -1;
inline thread_local bool tTracking = false;
inline thread_local bool tFailed = false;

inline size_t slotOf(uintptr_t p) {
  return static_cast<size_t>((p >> 4) * 0x9E3779B97F4A7C15ull >> 46) &
         (kSlots - 1);
}
// Open addressing: 0 = empty, 1 = removed (a lookup goes on past it, an
// insertion may reuse it).
inline void track(void *ptr) {
  const uintptr_t p = reinterpret_cast<uintptr_t>(ptr);
  for (size_t i = slotOf(p), n = 0; n < kSlots; ++n, i = (i + 1) & (kSlots - 1)) {
    uintptr_t cur = gSlots[i].load(std::memory_order_relaxed);
    while (cur <= 1) {
      if (gSlots[i].compare_exchange_weak(cur, p)) {
        ++gTracked;
        return;
      }
    }
  }
}
inline void untrack(void *ptr) {
  if (ptr == nullptr || gTracked.load(std::memory_order_relaxed) == 0) return;
  const uintptr_t p = reinterpret_cast<uintptr_t>(ptr);
  for (size_t i = slotOf(p), n = 0; n < kSlots; ++n, i = (i + 1) & (kSlots - 1)) {
    uintptr_t cur = gSlots[i].load(std::memory_order_relaxed);
    if (cur == 0) return;
    if (cur == p && gSlots[i].compare_exchange_strong(cur, 1)) {
      --gTracked;
      return;
    }
  }
}

// Returns false when this allocation must fail.
inline bool admit() {
  if (tCountdown < 0) return true;
  if (tCountdown == 0) {
    tCountdown = -1;
    tFailed = true;
    ++gInjected;
    return false;
  }
  --tCountdown;
  return true;
}

inline void *allocate(size_t size, size_t align, bool nothrow) {
  if (!admit()) {
    if (nothrow) return nullptr;
    throw std::bad_alloc();
  }
  if (size == 0) size = 1;
  void *p = nullptr;
  if (align > alignof(std::max_align_t)) {
    if (posix_memalign(&p, align, (size + align - 1) / align * align) != 0)
      p = nullptr;
  } else {
    p = std::malloc(size);
  }
  if (p == nullptr) {
    if (nothrow) return nullptr;
    throw std::bad_alloc();
  }
  if (tTracking) track(p);
  return p;
}
inline void release(void *p) {
  if (p == nullptr) return;
  untrack(p);
  std::free(p);
}
} // namespace detail

/// Let [n] allocations through on this thread, fail the next one, and track
/// every allocation made meanwhile.
inline void arm(long n) {
  detail::tFailed = false;
  detail::tCountdown = n;
  detail::tTracking = true;
}
/// Track allocations of this thread without failing any.
inline void trackOnly() {
  detail::tFailed = false;
  detail::tCountdown = -1;
  detail::tTracking = true;
}
/// Stop failing and tracking. True when the failure was injected.
inline bool disarm() {
  detail::tCountdown = -1;
  detail::tTracking = false;
  return detail::tFailed;
}
inline long liveTracked() { return detail::gTracked.load(); }
inline void clearTracked() {
  for (auto &s : detail::gSlots) s.store(0, std::memory_order_relaxed);
  detail::gTracked.store(0);
}

/// Bytes the C heap has in use, malloc included, or -1 where that cannot be
/// read (sanitizer builds replace the heap). Lets a test see a leak of
/// memory the C libraries malloc'd, which operator new does not track.
inline long long mallocInUse() {
#if defined(__has_feature)
#if __has_feature(address_sanitizer) || __has_feature(thread_sanitizer)
  return -1;
#endif
#endif
#if defined(__SANITIZE_ADDRESS__) || defined(__SANITIZE_THREAD__)
  return -1;
#elif defined(__APPLE__)
  malloc_statistics_t st;
  malloc_zone_statistics(nullptr, &st);
  return static_cast<long long>(st.size_in_use);
#elif defined(__GLIBC__) && (__GLIBC__ > 2 || __GLIBC_MINOR__ >= 33)
  return static_cast<long long>(mallinfo2().uordblks);
#else
  return -1;
#endif
}

} // namespace oom

// ---- replacements ----------------------------------------------------------
void *operator new(size_t n) { return oom::detail::allocate(n, 0, false); }
void *operator new[](size_t n) { return oom::detail::allocate(n, 0, false); }
void *operator new(size_t n, const std::nothrow_t &) noexcept {
  return oom::detail::allocate(n, 0, true);
}
void *operator new[](size_t n, const std::nothrow_t &) noexcept {
  return oom::detail::allocate(n, 0, true);
}
void *operator new(size_t n, std::align_val_t a) {
  return oom::detail::allocate(n, static_cast<size_t>(a), false);
}
void *operator new[](size_t n, std::align_val_t a) {
  return oom::detail::allocate(n, static_cast<size_t>(a), false);
}
void *operator new(size_t n, std::align_val_t a,
                   const std::nothrow_t &) noexcept {
  return oom::detail::allocate(n, static_cast<size_t>(a), true);
}
void *operator new[](size_t n, std::align_val_t a,
                     const std::nothrow_t &) noexcept {
  return oom::detail::allocate(n, static_cast<size_t>(a), true);
}
void operator delete(void *p) noexcept { oom::detail::release(p); }
void operator delete[](void *p) noexcept { oom::detail::release(p); }
void operator delete(void *p, size_t) noexcept { oom::detail::release(p); }
void operator delete[](void *p, size_t) noexcept { oom::detail::release(p); }
void operator delete(void *p, const std::nothrow_t &) noexcept {
  oom::detail::release(p);
}
void operator delete[](void *p, const std::nothrow_t &) noexcept {
  oom::detail::release(p);
}
void operator delete(void *p, std::align_val_t) noexcept {
  oom::detail::release(p);
}
void operator delete[](void *p, std::align_val_t) noexcept {
  oom::detail::release(p);
}
void operator delete(void *p, size_t, std::align_val_t) noexcept {
  oom::detail::release(p);
}
void operator delete[](void *p, size_t, std::align_val_t) noexcept {
  oom::detail::release(p);
}
void operator delete(void *p, std::align_val_t,
                     const std::nothrow_t &) noexcept {
  oom::detail::release(p);
}
void operator delete[](void *p, std::align_val_t,
                       const std::nothrow_t &) noexcept {
  oom::detail::release(p);
}
