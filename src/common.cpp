#include "common.h"

#ifdef _IS_ANDROID_
#include <android/log.h>
#endif

#ifdef _IS_WIN_
#include <algorithm>
#endif

#ifdef __EMSCRIPTEN__
#include <emscripten/emscripten.h>
#endif

void platform_log(const char *fmt, ...) {
  va_list args;
  va_start(args, fmt);
#ifdef _IS_ANDROID_
  __android_log_vprint(ANDROID_LOG_VERBOSE, "flutter_soloud NDK", fmt, args);
#elif defined(_IS_WIN_)
  char *buf = new char[4096];
  std::fill_n(buf, 4096, '\0');
  _vsprintf_p(buf, 4096, fmt, args);
  OutputDebugStringA(buf);
  delete[] buf;
#elif defined(__EMSCRIPTEN__)
  char buf[4096];
  vsnprintf(buf, sizeof(buf), fmt, args);
  emscripten_log(EM_LOG_CONSOLE, "%s", buf);
#else
  vprintf(fmt, args);
  fflush(stdout);
#endif
  va_end(args);
}
