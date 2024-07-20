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
    /// The capture device has failed to initialize.
    capture_init_failed,
    /// The capture device has not yet been initialized.
    capture_not_inited,
    /// Failed to start the device.
    failed_to_start_device,
} CaptureErrors_t;

typedef enum PlayerStateEvents
{
    event_started = 0,
    event_stopped,
    event_rerouted,
    event_interruption_began,
    event_interruption_ended,
    event_unlocked,
} PlayerEvents_t;

typedef enum SoundType
{
    // using Soloud::wav
    TYPE_WAV,
    // using Soloud::wavStream
    TYPE_WAVSTREAM,
    // this sound is a waveform
    TYPE_SYNTH
} SoundType_t;

typedef enum FilterType
{
    BiquadResonantFilter,
    EqFilter,
    EchoFilter,
    LofiFilter,
    FlangerFilter,
    BassboostFilter,
    WaveShaperFilter,
    RobotizeFilter,
    FreeverbFilter,
    PitchShiftFilter
} FilterType_t;

#endif // ENUMS_H