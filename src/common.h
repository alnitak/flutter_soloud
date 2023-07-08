#pragma once

#ifndef COMMON_H
#define COMMON_H

#ifdef __ANDROID__
    #define _IS_ANDROID_
#elif __linux__
    #define _IS_LINUX_
#elif _WIN32 | WIN32 | __WIN32 | _WIN64
    #define _IS_WIN_
#elif  __APPLE__
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
#endif


#ifdef _IS_ANDROID_
#include <android/log.h>
#define FFI_PLUGIN_EXPORT __attribute__((visibility("default"))) __attribute__((used))
#endif

#ifdef _IS_WIN_
#include <windows.h>
#define FFI_PLUGIN_EXPORT __declspec(dllexport)
#endif

#ifdef _IS_LINUX_
#include <stdarg.h>
#include <stdio.h>
#define FFI_PLUGIN_EXPORT __attribute__((visibility("default"))) __attribute__((used))
#endif

#ifdef _IS_MACOS_
#include <stdarg.h>
#include <stdio.h>
#define FFI_PLUGIN_EXPORT __attribute__((visibility("default"))) __attribute__((used))
#endif


void platform_log(const char *fmt, ...);


#endif // COMMON_H
