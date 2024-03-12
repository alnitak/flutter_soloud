#pragma once

#ifndef ENUMS_H
#define ENUMS_H

/// Possible player errors. 
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
    nullPointer,
    /// audio sound hash has not found
    soundHashNotFound,
    /// Player not initialized
    backendNotInited,
    /// Filter not found
    filterNotFound,
    /// asking for wave and FFT is not enabled
    visualizationNotEnabled,
} PlayerErrors_t;

/// Possible capture errors
typedef enum CaptureErrors
{
    /// No error
    capture_noError,
    /// 
    capture_init_failed,
    /// 
    capture_not_inited,
    /// 
    failed_to_start_device,
} CaptureErrors_t;


#endif // ENUMS_H