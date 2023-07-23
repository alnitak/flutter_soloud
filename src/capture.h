#ifndef CAPTURE_H
#define CAPTURE_H

#include "enums.h"
#ifndef COMMON_H
#include "common.h"
#endif

// #ifndef miniaudio_c
// #define MINIAUDIO_IMPLEMENTATION
#include "soloud/src/backend/miniaudio/miniaudio.h"
// #endif

class Capture {
public:
    Capture();
    ~Capture();

    void listDevices();

    CaptureErrors init();

    /// @brief Must be called when there is no more need of the capture or when closing the app
    /// @return 
    void dispose();

    bool isInited();

    CaptureErrors startCapture();
    CaptureErrors stopCapture();

    float *getWave();

private:
    ma_context context;
    ma_device_info *pPlaybackInfos;
    ma_uint32 playbackCount;
    ma_device_info *pCaptureInfos;
    ma_uint32 captureCount;
    ma_result result;
    // ma_encoder_config encoderConfig;
    // ma_encoder encoder;
    ma_device_config deviceConfig;
    ma_device device;

    /// true when the capture is initialized
    bool mInited;
};




#endif // CAPTURE_H