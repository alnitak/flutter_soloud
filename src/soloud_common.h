#pragma once

#ifndef SOLOUD_COMMON_H
#define SOLOUD_COMMON_H

#ifdef __ANDROID__
#define _IS_ANDROID_
#elif __linux__
#define _IS_LINUX_
#elif _WIN32 | WIN32 | __WIN32 | _WIN64
#define _IS_WIN_
#elif __APPLE__
#include <TargetConditionals.h>
#define _IS_MACOS_
/// these targets doesn't work!
#ifdef TARGET_OS_IPHONE
// iOS
#elif TARGET_IPHONE_SIMULATOR
// iOS Simulator
#elif TARGET_OS_MAC
#define _IS_MACOS_
#else
// Unsupported platform
#endif
#else
#define _WASM_
#endif

#ifdef _IS_ANDROID_
#include <android/log.h>
#define FFI_PLUGIN_EXPORT                                                      \
  __attribute__((visibility("default"))) __attribute__((used))
#endif

#ifdef _IS_WIN_
#include <windows.h>
#define FFI_PLUGIN_EXPORT __declspec(dllexport)
#endif

#if defined _IS_LINUX_ || defined _WASM_
#include <stdarg.h>
#include <stdio.h>
#define FFI_PLUGIN_EXPORT                                                      \
  __attribute__((visibility("default"))) __attribute__((used))
#endif

#ifdef _IS_MACOS_
#include <stdarg.h>
#include <stdio.h>
#define FFI_PLUGIN_EXPORT                                                      \
  __attribute__((visibility("default"))) __attribute__((used))
#endif

void soloud_platform_log(const char *fmt, ...);

/// Debug logging for hot paths. Compiles to a no-op unless SOLOUD_DEBUG_LOGGING
/// is defined at build time, so release builds do not pay for formatting or I/O.
#ifdef SOLOUD_DEBUG_LOGGING
#define SOLOUD_DEBUG_LOG(...) soloud_platform_log(__VA_ARGS__)
#else
#define SOLOUD_DEBUG_LOG(...) ((void)0)
#endif

#ifndef MIN
#define MIN(a, b) (((a) < (b)) ? (a) : (b))
#endif

#endif // SOLOUD_COMMON_H
