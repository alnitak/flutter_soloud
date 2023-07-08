#pragma once

#ifndef ENUMS_H
#define ENUMS_H

/// Possible player errors
typedef enum PlayerErrors
{
    /// No error
    noError          = 0,
    /// Some parameter is invalid
    invalidParameter = 1,
    /// File not found 
    fileNotFound     = 2,
    /// File found, but could not be loaded
    fileLoadFailed   = 3,
    /// DLL not found, or wrong DLL
    dllNotFound      = 4,
    /// Out of memory
    outOfMemory      = 5,
    /// Feature not implemented
    notImplemented   = 6,
    /// Other error
    unknownError     = 7,
    /// Player not initialized
    backendNotInited = 8
} PlayerErrors_t;

#endif // ENUMS_H