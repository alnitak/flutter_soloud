#pragma once

#ifndef ENUMS_H
#define ENUMS_H

/// Possible player errors
typedef enum PlayerErrors
{
    /// No error
    noError,
    /// Some parameter is invalid
    invalidParameter,
    /// File not found 
    fileNotFound,
    /// File found, but could not be loaded
    fileLoadFailed,
    /// The sound file has already been loaded
    fileAlreadyLoaded,
    /// DLL not found, or wrong DLL
    dllNotFound,
    /// Out of memory
    outOfMemory,
    /// Feature not implemented
    notImplemented,
    /// Other error
    unknownError,
    /// Player not initialized
    backendNotInited
} PlayerErrors_t;

/// Possible capture errors
typedef enum CaptureErrors
{
    /// No error
    capture_noError          = 0,
    /// 
    capture_init_failed      = 1,
    /// 
    capture_not_inited       = 2,
    /// 
    failed_to_start_device   = 3,
} CaptureErrors_t;

#endif // ENUMS_H