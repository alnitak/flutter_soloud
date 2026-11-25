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
// stopped and restarted (both counters stand still while stopped).
//
// The measured ratio is noisy by about (counter noise) / (measured span), so:
//
// - A ratio closer to 1 than that noise is taken as exactly 1, and the result
//   is the usual formula. The tolerance shrinks as the span grows, so a device
//   whose ratio is only slightly off is corrected too, once the span is long
//   enough to tell.
// - The ratio error is multiplied by framePosition, all the frames played so
//   far. A corrected value is only returned once the measured span is at
//   least as long as the stream had played when the measurement started, so
//   the error stays within about twice the counter noise even when the first
//   call comes long after the stream started.
// - While the ratio is not known yet (the first second), the usual formula is
//   returned if the measurement started early in the stream, when it has had
//   no time to drift, and 0 otherwise.
//
// The last value returned for a stream is kept: when the device is stopped
// and started again on the legacy (AudioTrack) path, the stream keeps
// playing but AAudioStream_getTimestamp() fails with
// AAUDIO_ERROR_INVALID_STATE (seen on a Samsung SM-N975F, Android 12).
// lastKnown() returns that value while the stream is the same, since the
// route, and so its latency, has not changed.
//
// It has no platform dependencies so it can be unit tested on any host.

#ifndef SOLOUD_AAUDIO_TIMESTAMP_LATENCY_H
#define SOLOUD_AAUDIO_TIMESTAMP_LATENCY_H

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <mutex>

namespace SoLoud
{
    class AAudioTimestampLatency
    {
    public:
        // How far framesWritten, taken back to the timestamp's time, can be
        // from the frames actually written at that time, in seconds: bursts of
        // the data callback (2048 frames at 44.1 kHz are 46 ms) and changes of
        // the buffer level.
        static constexpr double kCounterNoiseSec = 0.05;
        // Ratios closer to 1 than kCounterNoiseSec / (measured span), and never
        // less than this, are taken as exactly 1. The floor covers the drift
        // between the audio clock and the monotonic clock (tens of ppm).
        static constexpr double kMinUnitRatioTolerance = 0.0001;
        // Ratios outside [kMinRatio, kMaxRatio] are treated as a bad reading.
        static constexpr double kMinRatio = 0.5;
        static constexpr double kMaxRatio = 2.0;
        // During warm-up the usual formula is returned only if the
        // measurement started within this many seconds of the stream's start.
        static constexpr double kPlainWarmUpMaxSec = 1.0;

        // Returns the latency in seconds, or 0 when it is not known (see
        // above) or the reading is not usable.
        //
        // [stream] identifies the AAudio stream; a different value, or either
        // counter going backwards, restarts the measurement and forgets the
        // last known value.
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
            const double plainSec =
                ((double)framesWritten - (double)framePosition) / sampleRate - stalenessSec;

            std::lock_guard<std::mutex> lock(mMutex);
            if (stream != mStream ||
                framesWritten < mAnchorFramesWritten ||
                framePosition < mAnchorFramePosition)
            {
                mStream = stream;
                mAnchorFramesWritten = framesWritten;
                mAnchorWrittenAtTimestamp = writtenAtTimestamp;
                mAnchorFramePosition = framePosition;
                mLastLatencySec = 0.0;
            }
            mLastFramesWritten = framesWritten;

            const double latencySec = compute(framesWritten, framePosition, stalenessSec,
                                              writtenAtTimestamp, plainSec, sampleRate);
            if (latencySec > 0.0)
                mLastLatencySec = latencySec;
            return latencySec;
        }

        // The last value update() returned for [stream], for when
        // AAudioStream_getTimestamp() fails. 0 if there is none, if [stream]
        // is not the one update() last saw, or if [framesWritten] went
        // backwards (the stream was reopened).
        double lastKnown(const void *stream, int64_t framesWritten)
        {
            std::lock_guard<std::mutex> lock(mMutex);
            if (stream != mStream || framesWritten < mLastFramesWritten)
                return 0.0;
            return mLastLatencySec;
        }

    private:
        // The latency from one reading; called with mMutex held.
        double compute(int64_t framesWritten,
                       int64_t framePosition,
                       double stalenessSec,
                       double writtenAtTimestamp,
                       double plainSec,
                       double sampleRate) const
        {
            const double writtenSpan = writtenAtTimestamp - mAnchorWrittenAtTimestamp;
            const double positionSpan = (double)(framePosition - mAnchorFramePosition);
            const double anchorPosition = (double)mAnchorFramePosition;

            // Warming up: not enough frames yet to know the ratio.
            if (writtenSpan < sampleRate || positionSpan <= 0.0)
            {
                if (anchorPosition <= kPlainWarmUpMaxSec * sampleRate)
                    return positiveOrZero(plainSec);
                return 0.0;
            }

            const double ratio = positionSpan / writtenSpan;
            if (ratio < kMinRatio || ratio > kMaxRatio)
                return 0.0;

            const double tolerance =
                std::max(kMinUnitRatioTolerance, kCounterNoiseSec * sampleRate / writtenSpan);
            if (std::fabs(ratio - 1.0) <= tolerance)
                return positiveOrZero(plainSec);

            // The ratio error is multiplied by framePosition: wait until the
            // span covers the frames played before the measurement started.
            if (positionSpan < anchorPosition)
                return 0.0;

            return positiveOrZero(
                ((double)framesWritten - (double)framePosition / ratio) / sampleRate - stalenessSec);
        }

        static double positiveOrZero(double value)
        {
            return value > 0.0 ? value : 0.0;
        }

        std::mutex mMutex;
        const void *mStream = nullptr;
        int64_t mAnchorFramesWritten = -1;
        double mAnchorWrittenAtTimestamp = 0.0;
        int64_t mAnchorFramePosition = 0;
        int64_t mLastFramesWritten = -1;
        double mLastLatencySec = 0.0;
    };
} // namespace SoLoud

#endif // SOLOUD_AAUDIO_TIMESTAMP_LATENCY_H
