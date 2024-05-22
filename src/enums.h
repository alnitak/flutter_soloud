#pragma once

#ifndef ENUMS_H
#define ENUMS_H

/// Possible player errors.
///
/// WARNING: Keep these in sync with `lib/src/enums.dart`.
typedef enum PlayerErrors
{
    /// No error
    noError = 0,
    /// Some parameter is invalid
    invalidParameter = 1,
    /// File not found 
    fileNotFound = 2,
    /// File found, but could not be loaded
    fileLoadFailed = 3,
    /// The sound file has already been loaded
    fileAlreadyLoaded = 4,
    /// DLL not found, or wrong DLL
    dllNotFound = 5,
    /// Out of memory
    outOfMemory = 6,
    /// Feature not implemented
    notImplemented = 7,
    /// Other error
    unknownError = 8,
    /// Player not initialized
    nullPointer = 9,
    /// Audio sound hash is not found
    soundHashNotFound = 10,
    /// Player not initialized
    backendNotInited = 11,
    /// Filter not found
    filterNotFound = 12,
    /// Asking for wave and FFT is not enabled
    visualizationNotEnabled = 13,
    /// The maximum number of filters has been reached (default is 8).
    maxNumberOfFiltersReached = 14,
    /// The filter has already been added.
    filterAlreadyAdded = 15,
    /// Player already inited.
    playerAlreadyInited = 16,
    /// Audio handle is not found
    soundHandleNotFound = 17,
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