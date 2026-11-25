// Presentation latency of an AAudio output stream from its own counters.
//
// The usual formula is
//
//   latency = (framesWritten - framePosition) / sampleRate - (now - timestamp)
//
// where framePosition and timestamp come from AAudioStream_getTimestamp(). It
// assumes both counters count frames of the stream. Some devices break that:
// on a Samsung SM-G991N (Android 15) the timestamp's framePosition of a
// 44.1 kHz stream advances ~0.912x as fast as framesWritten, so the formula
// grows by ~88 ms for every second of playback.
//
// This helper measures how fast framePosition advances relative to
// framesWritten on the same stream and converts framePosition to written
// frames before subtracting. The ratio is measured against framesWritten
// rather than the wall clock, so it is not disturbed when the device is
// stopped and restarted (both counters stand still while stopped). When the
// ratio is within kUnitRatioTolerance of 1 it is taken as exactly 1, so on
// devices whose counters agree the result is the usual formula.
//
// It has no platform dependencies so it can be unit tested on any host.

#ifndef SOLOUD_AAUDIO_TIMESTAMP_LATENCY_H
#define SOLOUD_AAUDIO_TIMESTAMP_LATENCY_H

#include <cmath>
#include <cstdint>
#include <mutex>

namespace SoLoud
{
    class AAudioTimestampLatency
    {
    public:
        // Ratios closer to 1 than this are taken as exactly 1. The measured
        // ratio is noisy by roughly (buffer level change) / (measured span);
        // 2% leaves room for bursts of ~20 ms over the 1 s minimum span.
        static constexpr double kUnitRatioTolerance = 0.02;
        // Ratios outside [kMinRatio, kMaxRatio] are treated as a bad reading.
        static constexpr double kMinRatio = 0.5;
        static constexpr double kMaxRatio = 2.0;

        // Returns the latency in seconds, or 0 while the ratio is not known
        // yet (less than one second of written frames since the first sample
        // of this stream) or when the reading is not usable.
        //
        // [stream] identifies the AAudio stream; a different value, or either
        // counter going backwards, restarts the measurement.
        double update(const void *stream,
                      int64_t framesWritten,
                      int64_t framePosition,
                      int64_t timestampNanos,
                      int64_t nowNanos,
                      double sampleRate)
        {
            if (sampleRate <= 0.0)
                return 0.0;

            const double stalenessSec = (double)(nowNanos - timestampNanos) / 1000000000.0;
            // framesWritten as it was when framePosition was presented.
            const double writtenAtTimestamp = (double)framesWritten - stalenessSec * sampleRate;

            double ratio;
            {
                std::lock_guard<std::mutex> lock(mMutex);
                if (stream != mStream ||
                    framesWritten < mAnchorFramesWritten ||
                    framePosition < mAnchorFramePosition)
                {
                    mStream = stream;
                    mAnchorFramesWritten = framesWritten;
                    mAnchorWrittenAtTimestamp = writtenAtTimestamp;
                    mAnchorFramePosition = framePosition;
                }

                const double writtenSpan = writtenAtTimestamp - mAnchorWrittenAtTimestamp;
                const double positionSpan = (double)(framePosition - mAnchorFramePosition);
                // Warming up: not enough frames yet to know the ratio.
                if (writtenSpan < sampleRate || positionSpan <= 0.0)
                    return 0.0;
                ratio = positionSpan / writtenSpan;
            }

            if (ratio < kMinRatio || ratio > kMaxRatio)
                return 0.0;
            if (std::fabs(ratio - 1.0) <= kUnitRatioTolerance)
                ratio = 1.0;

            const double latencySec =
                ((double)framesWritten - (double)framePosition / ratio) / sampleRate - stalenessSec;
            return latencySec > 0.0 ? latencySec : 0.0;
        }

    private:
        std::mutex mMutex;
        const void *mStream = nullptr;
        int64_t mAnchorFramesWritten = -1;
        double mAnchorWrittenAtTimestamp = 0.0;
        int64_t mAnchorFramePosition = 0;
    };
} // namespace SoLoud

#endif // SOLOUD_AAUDIO_TIMESTAMP_LATENCY_H
