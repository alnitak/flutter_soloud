#include "capture.h"
#include "soloud.h"

#include <cstdarg>
#include <memory.h>

#define CAPTURE_BUFFER_SIZE 1024

float capturedBuffer[CAPTURE_BUFFER_SIZE];
void data_callback(ma_device *pDevice, void *pOutput, const void *pInput, ma_uint32 frameCount)
{
    // Process the captured audio data as needed
    // For example, you can copy it to a buffer for later use.
    float *captured = (float *)(pInput); // Assuming float format
    // Do something with the captured audio data...
    // platform_log("framecound: %d   data_callback %f\n", frameCount, captured[0]);

    memcpy(capturedBuffer, captured, sizeof(float) * CAPTURE_BUFFER_SIZE);
}

Capture::Capture() : mInited(false){};
Capture::~Capture()
{
    dispose();
}

void Capture::listDevices()
{
    platform_log("***************** LIST DEVICES START\n");
    if ((result = ma_context_init(NULL, 0, NULL, &context)) != MA_SUCCESS)
    {
        platform_log("Failed to initialize context %d\n", result);
    }

    if ((result = ma_context_get_devices(
             &context,
             &pPlaybackInfos,
             &playbackCount,
             &pCaptureInfos,
             &captureCount)) != MA_SUCCESS)
    {
        platform_log("Failed to get devices %d\n", result);
    }

    // Loop over each device info and do something with it. Here we just print
    // the name with their index. You may want
    // to give the user the opportunity to choose which device they'd prefer.
    for (ma_uint32 iDevice = 0; iDevice < playbackCount; iDevice += 1)
    {
        platform_log("######%s %d - %s\n",
                     pCaptureInfos[iDevice].isDefault ? "X" : "-",
                     iDevice,
                     pCaptureInfos[iDevice].name);
    }

    // ma_device_config config = ma_device_config_init(ma_device_type_playback);
    // config.playback.pDeviceID = &pPlaybackInfos[chosenPlaybackDeviceIndex].id;
    // config.playback.format = MY_FORMAT;
    // config.playback.channels = MY_CHANNEL_COUNT;
    // config.sampleRate = MY_SAMPLE_RATE;
    // config.dataCallback = data_callback;
    // config.pUserData = pMyCustomData;

    // ma_device device;
    // if (ma_device_init(&context, &config, &device) != MA_SUCCESS)
    // {
    //     // Error
    // }

    // ...

    //     ma_device_uninit(&device);
    // ma_context_uninit(&context);
    platform_log("***************** LIST DEVICES END\n");
}

CaptureErrors Capture::init()
{
    listDevices();

    deviceConfig = ma_device_config_init(ma_device_type_capture);
    deviceConfig.periodSizeInFrames = CAPTURE_BUFFER_SIZE;
    deviceConfig.capture.pDeviceID = &pPlaybackInfos[6].id;
    deviceConfig.capture.format = ma_format_f32;
    deviceConfig.capture.channels = 2;
    deviceConfig.sampleRate = 44100;
    deviceConfig.dataCallback = data_callback;
    deviceConfig.pUserData = nullptr;

    result = ma_device_init(NULL, &deviceConfig, &device);
    if (result != MA_SUCCESS)
    {
        platform_log("Failed to initialize capture device.\n");
        return capture_init_failed;
    }
    mInited = true;
    return capture_noError;
}

void Capture::dispose()
{
    // Clean up SoLoud
    mInited = false;

    ma_device_uninit(&device);
}

bool Capture::isInited()
{
    return mInited;
}

CaptureErrors Capture::startCapture()
{
    if (!mInited)
        return capture_not_inited;

    result = ma_device_start(&device);
    if (result != MA_SUCCESS)
    {
        ma_device_uninit(&device);
        platform_log("Failed to start device.\n");
        return failed_to_start_device;
    }
    return capture_noError;
}

CaptureErrors Capture::stopCapture()
{
    if (!mInited)
        return capture_not_inited;

    ma_device_uninit(&device);
    return capture_noError;
}

float waveData[256];
float *Capture::getWave()
{
    int n = CAPTURE_BUFFER_SIZE >> 8;
    for (int i = 0; i < 256; i++)
    {
        waveData[i] = (
            capturedBuffer[i * n] + 
            capturedBuffer[i * n + 1] + 
            capturedBuffer[i * n + 2] + 
            capturedBuffer[i * n + 3]
            ) / n;
    }
    return waveData;
}