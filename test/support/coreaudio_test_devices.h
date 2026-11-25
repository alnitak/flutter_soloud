// CoreAudio helpers for the macOS device tests (test/macos/
// coreaudio_device_change_test.mm, sample_rate_change_test.mm): find devices,
// create a process-private aggregate device, switch the system default
// output, change a device's nominal sample rate -- and put everything back.
//
// Every system-wide change goes through SystemAudioRestorer, which records
// the original value first and restores it from its destructor, from atexit()
// and from the handlers of SIGINT, SIGTERM, SIGHUP, SIGABRT, SIGSEGV, SIGBUS
// and SIGILL, so a failing or crashing test leaves the Mac as it found it
// (only SIGKILL cannot be caught). Aggregate devices are created private
// (kAudioAggregateDeviceIsPrivateKey), so they vanish with the process anyway.

#ifndef SOLOUD_TEST_COREAUDIO_TEST_DEVICES_H
#define SOLOUD_TEST_COREAUDIO_TEST_DEVICES_H

#include <CoreAudio/CoreAudio.h>
#include <CoreFoundation/CoreFoundation.h>

#include <csignal>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <unistd.h>
#include <vector>

namespace catest {

inline AudioObjectPropertyAddress addr(AudioObjectPropertySelector sel,
                                       AudioObjectPropertyScope scope = kAudioObjectPropertyScopeGlobal) {
    return {sel, scope, kAudioObjectPropertyElementMain};
}

inline AudioObjectID defaultOutput() {
    AudioObjectID id = kAudioObjectUnknown;
    UInt32 size = sizeof(id);
    auto a = addr(kAudioHardwarePropertyDefaultOutputDevice);
    AudioObjectGetPropertyData(kAudioObjectSystemObject, &a, 0, nullptr, &size, &id);
    return id;
}

inline bool setDefaultOutputRaw(AudioObjectID id) {
    auto a = addr(kAudioHardwarePropertyDefaultOutputDevice);
    return AudioObjectSetPropertyData(kAudioObjectSystemObject, &a, 0, nullptr, sizeof(id), &id) == noErr;
}

inline std::string cfToString(CFStringRef s) {
    if (!s) return {};
    char buf[512] = {0};
    CFStringGetCString(s, buf, sizeof(buf), kCFStringEncodingUTF8);
    return buf;
}

inline std::string deviceString(AudioObjectID id, AudioObjectPropertySelector sel) {
    CFStringRef s = nullptr;
    UInt32 size = sizeof(s);
    auto a = addr(sel);
    if (AudioObjectGetPropertyData(id, &a, 0, nullptr, &size, &s) != noErr || !s) return {};
    std::string out = cfToString(s);
    CFRelease(s);
    return out;
}

inline std::string deviceName(AudioObjectID id) { return deviceString(id, kAudioObjectPropertyName); }
inline std::string deviceUID(AudioObjectID id) { return deviceString(id, kAudioDevicePropertyDeviceUID); }

inline std::vector<AudioObjectID> allDevices() {
    auto a = addr(kAudioHardwarePropertyDevices);
    UInt32 size = 0;
    if (AudioObjectGetPropertyDataSize(kAudioObjectSystemObject, &a, 0, nullptr, &size) != noErr) return {};
    std::vector<AudioObjectID> ids(size / sizeof(AudioObjectID));
    AudioObjectGetPropertyData(kAudioObjectSystemObject, &a, 0, nullptr, &size, ids.data());
    return ids;
}

inline bool hasOutput(AudioObjectID id) {
    auto a = addr(kAudioDevicePropertyStreams, kAudioObjectPropertyScopeOutput);
    UInt32 size = 0;
    return AudioObjectGetPropertyDataSize(id, &a, 0, nullptr, &size) == noErr && size > 0;
}

inline bool hasInput(AudioObjectID id) {
    auto a = addr(kAudioDevicePropertyStreams, kAudioObjectPropertyScopeInput);
    UInt32 size = 0;
    return AudioObjectGetPropertyDataSize(id, &a, 0, nullptr, &size) == noErr && size > 0;
}

/// The first output device whose name contains [part].
inline AudioObjectID findOutputByName(const std::string &part) {
    for (AudioObjectID id : allDevices())
        if (hasOutput(id) && deviceName(id).find(part) != std::string::npos) return id;
    return kAudioObjectUnknown;
}

inline AudioObjectID findByUID(const std::string &uid) {
    for (AudioObjectID id : allDevices())
        if (deviceUID(id) == uid) return id;
    return kAudioObjectUnknown;
}

inline double nominalRate(AudioObjectID id) {
    Float64 r = 0;
    UInt32 size = sizeof(r);
    auto a = addr(kAudioDevicePropertyNominalSampleRate);
    AudioObjectGetPropertyData(id, &a, 0, nullptr, &size, &r);
    return r;
}

inline bool setNominalRateRaw(AudioObjectID id, double rate) {
    Float64 r = rate;
    auto a = addr(kAudioDevicePropertyNominalSampleRate);
    if (AudioObjectSetPropertyData(id, &a, 0, nullptr, sizeof(r), &r) != noErr) return false;
    for (int i = 0; i < 200 && nominalRate(id) != rate; ++i) usleep(10000);
    return nominalRate(id) == rate;
}

/// Whether [id] lists [rate] among its available nominal rates.
inline bool supportsRate(AudioObjectID id, double rate) {
    auto a = addr(kAudioDevicePropertyAvailableNominalSampleRates);
    UInt32 size = 0;
    if (AudioObjectGetPropertyDataSize(id, &a, 0, nullptr, &size) != noErr) return false;
    std::vector<AudioValueRange> ranges(size / sizeof(AudioValueRange));
    AudioObjectGetPropertyData(id, &a, 0, nullptr, &size, ranges.data());
    for (const auto &r : ranges)
        if (rate >= r.mMinimum && rate <= r.mMaximum) return true;
    return false;
}

/// Records and restores the system-wide audio state a test changes.
class SystemAudioRestorer {
public:
    static SystemAudioRestorer &instance() {
        static SystemAudioRestorer r;
        return r;
    }

    /// Call before changing the default output.
    void saveDefaultOutput() {
        if (mSavedDefault != kAudioObjectUnknown) return;
        mSavedDefault = defaultOutput();
        mSavedDefaultUID = deviceUID(mSavedDefault);
        std::printf("  (saved default output: %s)\n", deviceName(mSavedDefault).c_str());
    }

    /// Call before changing [id]'s nominal rate.
    void saveRate(AudioObjectID id) {
        for (const auto &s : mRates)
            if (s.id == id) return;
        if (mRateCount < kMaxRates) mRates[mRateCount++] = {id, nominalRate(id)};
        std::printf("  (saved %s rate: %.0f Hz)\n", deviceName(id).c_str(), nominalRate(id));
    }

    void trackAggregate(AudioObjectID id) {
        if (mAggCount < kMaxAgg) mAggregates[mAggCount++] = id;
    }
    void untrackAggregate(AudioObjectID id) {
        for (int i = 0; i < mAggCount; ++i)
            if (mAggregates[i] == id) mAggregates[i] = kAudioObjectUnknown;
    }

    /// Puts everything back. Safe to call more than once.
    void restore() {
        for (int i = 0; i < mAggCount; ++i)
            if (mAggregates[i] != kAudioObjectUnknown) {
                AudioHardwareDestroyAggregateDevice(mAggregates[i]);
                mAggregates[i] = kAudioObjectUnknown;
            }
        for (int i = 0; i < mRateCount; ++i)
            if (mRates[i].id != kAudioObjectUnknown && nominalRate(mRates[i].id) != mRates[i].rate)
                setNominalRateRaw(mRates[i].id, mRates[i].rate);
        if (mSavedDefault != kAudioObjectUnknown && defaultOutput() != mSavedDefault) {
            // The object ID can change if the device was re-created; the UID
            // does not.
            AudioObjectID target = findByUID(mSavedDefaultUID);
            setDefaultOutputRaw(target != kAudioObjectUnknown ? target : mSavedDefault);
        }
    }

    bool defaultRestored() const { return mSavedDefault == kAudioObjectUnknown || defaultOutput() == mSavedDefault; }

    ~SystemAudioRestorer() { restore(); }

private:
    SystemAudioRestorer() {
        std::atexit([] { instance().restore(); });
        for (int sig : {SIGINT, SIGTERM, SIGHUP, SIGABRT, SIGSEGV, SIGBUS, SIGILL}) std::signal(sig, onSignal);
    }
    static void onSignal(int sig) {
        instance().restore();
        std::signal(sig, SIG_DFL);
        std::raise(sig);
    }

    static constexpr int kMaxRates = 8, kMaxAgg = 8;
    struct SavedRate {
        AudioObjectID id = kAudioObjectUnknown;
        double rate = 0;
    };
    AudioObjectID mSavedDefault = kAudioObjectUnknown;
    std::string mSavedDefaultUID;
    SavedRate mRates[kMaxRates];
    int mRateCount = 0;
    AudioObjectID mAggregates[kMaxAgg] = {};
    int mAggCount = 0;
};

inline bool setDefaultOutput(AudioObjectID id) {
    SystemAudioRestorer::instance().saveDefaultOutput();
    return setDefaultOutputRaw(id);
}

inline bool setNominalRate(AudioObjectID id, double rate) {
    SystemAudioRestorer::instance().saveRate(id);
    return setNominalRateRaw(id, rate);
}

/// A process-private aggregate device over [subDeviceUID]; returns its ID.
inline AudioObjectID createPrivateAggregate(const std::string &name, const std::string &uid,
                                            const std::string &subDeviceUID) {
    CFMutableDictionaryRef desc = CFDictionaryCreateMutable(nullptr, 0, &kCFTypeDictionaryKeyCallBacks,
                                                            &kCFTypeDictionaryValueCallBacks);
    CFStringRef cfName = CFStringCreateWithCString(nullptr, name.c_str(), kCFStringEncodingUTF8);
    CFStringRef cfUid = CFStringCreateWithCString(nullptr, uid.c_str(), kCFStringEncodingUTF8);
    CFStringRef cfSub = CFStringCreateWithCString(nullptr, subDeviceUID.c_str(), kCFStringEncodingUTF8);
    int one = 1;
    CFNumberRef cfOne = CFNumberCreate(nullptr, kCFNumberIntType, &one);
    CFDictionarySetValue(desc, CFSTR(kAudioAggregateDeviceNameKey), cfName);
    CFDictionarySetValue(desc, CFSTR(kAudioAggregateDeviceUIDKey), cfUid);
    CFDictionarySetValue(desc, CFSTR(kAudioAggregateDeviceIsPrivateKey), cfOne);
    CFMutableDictionaryRef sub = CFDictionaryCreateMutable(nullptr, 0, &kCFTypeDictionaryKeyCallBacks,
                                                           &kCFTypeDictionaryValueCallBacks);
    CFDictionarySetValue(sub, CFSTR(kAudioSubDeviceUIDKey), cfSub);
    CFMutableArrayRef subs = CFArrayCreateMutable(nullptr, 0, &kCFTypeArrayCallBacks);
    CFArrayAppendValue(subs, sub);
    CFDictionarySetValue(desc, CFSTR(kAudioAggregateDeviceSubDeviceListKey), subs);
    CFDictionarySetValue(desc, CFSTR(kAudioAggregateDeviceMasterSubDeviceKey), cfSub);

    AudioObjectID agg = kAudioObjectUnknown;
    const OSStatus st = AudioHardwareCreateAggregateDevice(desc, &agg);
    CFRelease(subs);
    CFRelease(sub);
    CFRelease(cfOne);
    CFRelease(cfSub);
    CFRelease(cfUid);
    CFRelease(cfName);
    CFRelease(desc);
    if (st != noErr) {
        std::printf("  AudioHardwareCreateAggregateDevice failed: %d\n", static_cast<int>(st));
        return kAudioObjectUnknown;
    }
    SystemAudioRestorer::instance().trackAggregate(agg);
    // Give the HAL a moment to publish it.
    for (int i = 0; i < 100 && !hasOutput(agg); ++i) usleep(10000);
    return agg;
}

inline bool destroyAggregate(AudioObjectID agg) {
    SystemAudioRestorer::instance().untrackAggregate(agg);
    return AudioHardwareDestroyAggregateDevice(agg) == noErr;
}

} // namespace catest

#endif // SOLOUD_TEST_COREAUDIO_TEST_DEVICES_H
