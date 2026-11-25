// Standalone tests for SoLoud::AAudioTimestampLatency, the helper behind
// getHardwareOutputLatency() on Android (AAudio).
//
// AAudioStream_getTimestamp()'s framePosition does not advance at the stream
// rate on every device. On a Samsung SM-G991N (Android 15) the framePosition
// of a 44.1 kHz stream advanced ~0.912x as fast as framesWritten, so
// (framesWritten - framePosition) / sampleRate grew by ~88 ms per second of
// playback. The helper measures the ratio between the two counters and
// converts framePosition before subtracting.
//
// Build and run from the flutter_soloud repository root with:
//
//   ./test/common/aaudio_timestamp_latency_test.sh

#include "../../src/soloud/src/backend/miniaudio/aaudio_timestamp_latency.h"

#include <cmath>
#include <cstdint>
#include <cstdio>
#include <vector>

namespace {

int gFailures = 0;
int gAssertions = 0;

#define EXPECT(condition, format, ...)                                            \
    do {                                                                          \
        ++gAssertions;                                                            \
        if (!(condition)) {                                                       \
            ++gFailures;                                                          \
            std::printf("FAIL %s:%d: " format "\n", __FILE__, __LINE__, ##__VA_ARGS__); \
        }                                                                         \
    } while (0)

struct Sample {
    int64_t framesWritten;
    int64_t framePosition;
    int64_t timestampNanos;
    int64_t nowNanos;
};

// The formula used before the helper.
double plainLatency(const Sample &s, double sampleRate)
{
    return (double)(s.framesWritten - s.framePosition) / sampleRate -
           (double)(s.nowNanos - s.timestampNanos) / 1e9;
}

// The plain formula as the helper returns it, negative values as 0.
double plainResult(const Sample &s, double sampleRate)
{
    const double plain = plainLatency(s, sampleRate);
    return plain > 0.0 ? plain : 0.0;
}

// Raw values read about every 0.4 s from a 44.1 kHz SoLoud stream on a
// Samsung SM-G991N (Android 15, SDK 35, device speaker). Over the run
// framesWritten advances 44,100 frames/s and framePosition ~40,190 frames/s.
const std::vector<Sample> kSmG991n = {
        {73870, 65037, 669654170564742LL, 669654199536395LL},
        {322358, 285898, 669659673460891LL, 669659833342430LL},
        {340336, 302134, 669660078165891LL, 669660240450660LL},
        {358670, 318456, 669660483790429LL, 669660657217929LL},
        {376737, 334588, 669660886199775LL, 669661065667237LL},
        {394804, 350987, 669661292586044LL, 669661475428890LL},
        {412960, 367217, 669661698105121LL, 669661888378505LL},
        {431116, 387859, 669662201099390LL, 669662299290543LL},
        {448916, 404000, 669662603992966LL, 669662704071274LL},
        {467161, 420227, 669663008389081LL, 669663117389927LL},
        {484961, 436532, 669663413629773LL, 669663520243619LL},
        {502761, 452765, 669663817320850LL, 669663923291619LL},
        {520739, 468904, 669664219627696LL, 669664332311119LL},
        {538628, 485138, 669664623340926LL, 669664739361619LL},
        {556784, 501278, 669665025966003LL, 669665148283311LL},
        {575029, 517505, 669665430475580LL, 669665563224426LL},
        {592918, 533637, 669665832458695LL, 669665968250887LL},
        {610718, 549949, 669666237802002LL, 669666372379310LL},
        {628963, 566175, 669666641233886LL, 669666786176656LL},
        {647030, 582412, 669667046432078LL, 669667195347155LL},
        {665097, 602739, 669667550909540LL, 669667604341270LL},
        {683342, 618970, 669667956254655LL, 669668018372155LL},
        {725083, 655603, 669668866921539LL, 669668965130231LL},
        {743239, 671745, 669669269997346LL, 669669376387884LL},
        {761128, 687973, 669669674284423LL, 669669783383192LL},
        {779284, 704188, 669670077385499LL, 669670195279038LL},
        {797262, 720502, 669670483059153LL, 669670601324114LL},
        {815062, 736643, 669670886005922LL, 669671009338268LL},
        {833307, 752780, 669671287754845LL, 669671420386075LL},
        {851552, 769015, 669671691800037LL, 669671833297690LL},
        {869530, 785240, 669672095265344LL, 669672241484305LL},
        {887419, 801477, 669672500275575LL, 669672647197536LL},
        {905575, 817774, 669672905369420LL, 669673057330920LL},
        {923642, 838498, 669673411077882LL, 669673467290497LL},
        {941709, 854727, 669673816191958LL, 669673878424112LL},
        {959598, 870943, 669674219032881LL, 669674283286496LL},
        {977576, 887259, 669674625748535LL, 669674691321342LL},
        {995465, 903484, 669675029004611LL, 669675097344303LL},
        {1013443, 919707, 669675434412688LL, 669675505255380LL},
        {1031510, 935929, 669675836562841LL, 669675914238880LL},
        {1049310, 952157, 669676241284072LL, 669676318337880LL},
        {1067555, 968374, 669676645008110LL, 669676730571956LL},
        {1085711, 984514, 669677047873994LL, 669677142240610LL},
        {1103778, 1000829, 669677453563263LL, 669677553437686LL},
};

void testSmG991nLog()
{
    const double sampleRate = 44100.0;
    SoLoud::AAudioTimestampLatency latency;
    const int stream = 0;

    double first = -1.0, minValue = 1e9, maxValue = -1e9;
    for (size_t i = 0; i < kSmG991n.size(); ++i)
    {
        const Sample &s = kSmG991n[i];
        const double value = latency.update(&stream, s.framesWritten, s.framePosition,
                                            s.timestampNanos, s.nowNanos, sampleRate);
        if (i == 0)
        {
            // The first sample comes 1.5 s into the stream, too late for the
            // plain formula to be trusted during warm-up.
            EXPECT(value == 0.0, "first sample must be warm-up (0), got %f", value);
            continue;
        }
        if (first < 0.0)
            first = value;
        if (value < minValue) minValue = value;
        if (value > maxValue) maxValue = value;
    }
    // The plain formula drifts from 171 ms to 2.2 s over this run.
    EXPECT(plainLatency(kSmG991n.back(), sampleRate) > 2.0,
           "plain formula should show the drift, got %f",
           plainLatency(kSmG991n.back(), sampleRate));
    EXPECT(minValue > 0.010 && maxValue < 0.060,
           "latency must stay in a plausible range, got [%f, %f]", minValue, maxValue);
    EXPECT(maxValue - minValue < 0.030,
           "latency must not drift, spread %f s", maxValue - minValue);
}

// A simulated stream. Running time excludes stops; the wall clock does not.
struct SimStream {
    double sampleRate = 48000.0;
    double positionRatio = 1.0;    // framePosition advance per presented frame
    double latencySec = 0.030;     // what the helper should report
    int64_t burstFrames = 192;     // framesWritten advances in bursts
    double runningSec = 0.0;
    double stoppedSec = 0.0;

    Sample read(double timestampAgeSec) const
    {
        const double timestampRunningSec = runningSec - timestampAgeSec;
        const int64_t written = (int64_t)((runningSec + latencySec) * sampleRate) /
                                burstFrames * burstFrames;
        Sample s;
        s.framesWritten = written;
        s.framePosition = (int64_t)(timestampRunningSec * sampleRate * positionRatio);
        s.nowNanos = 1000000000LL + (int64_t)((runningSec + stoppedSec) * 1e9);
        s.timestampNanos = s.nowNanos - (int64_t)(timestampAgeSec * 1e9);
        return s;
    }
};

double ageAt(int i)
{
    // Timestamps are 5..165 ms old, as on the device above.
    return 0.005 + 0.010 * (i % 17);
}

// On a device whose counters agree the result is the plain formula, from the
// first sample (no warm-up) and for the whole run, also when the first call
// comes late and framesWritten moves in large bursts.
void checkConformingDevice(SimStream sim, double firstSec, double durationSec, const char *label)
{
    SoLoud::AAudioTimestampLatency latency;
    const int stream = 0;
    int compared = 0;
    for (int i = 0; i * 0.1 <= durationSec; ++i)
    {
        sim.runningSec = firstSec + 0.1 * i;
        const Sample s = sim.read(ageAt(i));
        const double value = latency.update(&stream, s.framesWritten, s.framePosition,
                                            s.timestampNanos, s.nowNanos, sim.sampleRate);
        const double plain = plainResult(s, sim.sampleRate);
        if (firstSec > SoLoud::AAudioTimestampLatency::kPlainWarmUpMaxSec && i * 0.1 < 1.25)
        {
            // Late first call: 0 for the first second of measured frames
            // (how much wall time that is depends on the timestamps' age).
            EXPECT(value == 0.0 || (i * 0.1 > 0.95 && value == plain),
                   "%s t=%.1f: late first call must warm up (0), got %f",
                   label, sim.runningSec, value);
            continue;
        }
        EXPECT(value == plain, "%s t=%.1f: %f != plain %f", label, sim.runningSec, value, plain);
        ++compared;
    }
    EXPECT(compared > durationSec * 10 - 20, "%s: compared only %d samples", label, compared);
}

void testConformingDeviceMatchesPlainFormula()
{
    SimStream sim;
    checkConformingDevice(sim, 0.2, 120.0, "conforming");

    // 2048-frame callbacks at 44.1 kHz (46 ms), as with bufferSize: 2048.
    SimStream bursts;
    bursts.sampleRate = 44100.0;
    bursts.burstFrames = 2048;
    bursts.latencySec = 0.080;
    checkConformingDevice(bursts, 0.2, 120.0, "conforming, 2048-frame bursts");

    checkConformingDevice(sim, 600.0, 60.0, "conforming, first call at 10 min");
}

void testWarmUp()
{
    // Early in the stream the ratio is not known yet, but the plain formula
    // has had no time to drift: it is returned as is.
    SimStream sim;
    sim.positionRatio = 0.912;
    SoLoud::AAudioTimestampLatency latency;
    const int stream = 0;
    for (int i = 0; i < 9; ++i)
    {
        sim.runningSec = 0.2 + 0.1 * i; // less than 1 s after the first sample
        const Sample s = sim.read(0.02);
        const double value = latency.update(&stream, s.framesWritten, s.framePosition,
                                            s.timestampNanos, s.nowNanos, sim.sampleRate);
        EXPECT(value == plainResult(s, sim.sampleRate) && value > 0.0,
               "t=%.1f: expected the plain formula during warm-up, got %f",
               sim.runningSec, value);
    }

    // A first call long after the stream started: the plain formula has
    // drifted (~26 s here), so the warm-up returns 0.
    SimStream late;
    late.sampleRate = 44100.0;
    late.positionRatio = 0.9117;
    SoLoud::AAudioTimestampLatency lateLatency;
    for (int i = 0; i < 9; ++i)
    {
        late.runningSec = 300.0 + 0.1 * i;
        const Sample s = late.read(0.02);
        const double value = lateLatency.update(&stream, s.framesWritten, s.framePosition,
                                                s.timestampNanos, s.nowNanos, late.sampleRate);
        EXPECT(value == 0.0, "late t=%.1f: expected warm-up 0, got %f", late.runningSec, value);
    }
}

double runAndCheck(SoLoud::AAudioTimestampLatency &latency, const void *stream, SimStream &sim,
                   double fromSec, double toSec, const char *label)
{
    double last = 0.0;
    int i = 0;
    for (double t = fromSec; t <= toSec + 1e-9; t += 0.1, ++i)
    {
        sim.runningSec = t;
        const Sample s = sim.read(ageAt(i));
        last = latency.update(stream, s.framesWritten, s.framePosition,
                              s.timestampNanos, s.nowNanos, sim.sampleRate);
        EXPECT(std::fabs(last - sim.latencySec) < 0.008,
               "%s t=%.1f: got %f, want ~%f", label, t, last, sim.latencySec);
    }
    return last;
}

void testSlowPositionDevice()
{
    SimStream sim;
    sim.sampleRate = 44100.0;
    sim.positionRatio = 0.9117;
    SoLoud::AAudioTimestampLatency latency;
    const int stream = 0;
    sim.runningSec = 0.2;
    Sample s = sim.read(0.03);
    latency.update(&stream, s.framesWritten, s.framePosition, s.timestampNanos, s.nowNanos,
                   sim.sampleRate);
    runAndCheck(latency, &stream, sim, 1.5, 120.0, "slow position");
    EXPECT(plainLatency(sim.read(0.03), sim.sampleRate) > 10.0,
           "the plain formula should have drifted by now");
}

void testSlightlySlowPositionDevice()
{
    // A ratio of 0.99 is within the unit tolerance while the measured span is
    // short, so the plain formula is returned (drifting 10 ms/s). The
    // tolerance shrinks as the span grows: from ~6 s on the value is
    // corrected and stays right.
    SimStream sim;
    sim.positionRatio = 0.99;
    SoLoud::AAudioTimestampLatency latency;
    const int stream = 0;
    for (int i = 0; i < 60; ++i)
    {
        sim.runningSec = 0.2 + 0.1 * i;
        const Sample s = sim.read(ageAt(i));
        const double value = latency.update(&stream, s.framesWritten, s.framePosition,
                                            s.timestampNanos, s.nowNanos, sim.sampleRate);
        EXPECT(value < sim.latencySec + 0.065, "t=%.1f: %f drifted too far", sim.runningSec, value);
    }
    runAndCheck(latency, &stream, sim, 6.2, 120.0, "ratio 0.99");
    EXPECT(plainLatency(sim.read(0.03), sim.sampleRate) > 1.0,
           "the plain formula should have drifted by now");
}

void testLateFirstCallStaysBounded()
{
    // The ratio error is multiplied by all the frames played so far. When the
    // first call comes long after the stream started, the value is 0 until
    // the measured span covers the frames played before it, and then right.
    // It is never far off.
    const double firstCalls[] = {30.0, 300.0, 3600.0};
    for (double firstSec : firstCalls)
    {
        SimStream sim;
        sim.sampleRate = 44100.0;
        sim.positionRatio = 0.9117;
        SoLoud::AAudioTimestampLatency latency;
        const int stream = 0;
        int zeros = 0, valid = 0;
        double firstValidSec = -1.0;
        const double stepSec = firstSec / 200.0;
        for (int i = 0; i <= 600; ++i)
        {
            sim.runningSec = firstSec + stepSec * i;
            const Sample s = sim.read(ageAt(i));
            const double value = latency.update(&stream, s.framesWritten, s.framePosition,
                                                s.timestampNanos, s.nowNanos, sim.sampleRate);
            if (value == 0.0)
            {
                ++zeros;
                EXPECT(valid == 0, "first call at %.0f s: 0 again at t=%.1f after valid values",
                       firstSec, sim.runningSec);
                continue;
            }
            if (firstValidSec < 0.0)
                firstValidSec = sim.runningSec;
            ++valid;
            EXPECT(std::fabs(value - sim.latencySec) < 0.008,
                   "first call at %.0f s, t=%.1f: got %f, want ~%f",
                   firstSec, sim.runningSec, value, sim.latencySec);
        }
        EXPECT(firstValidSec > 0.0 && firstValidSec <= 2.0 * firstSec + 2.0 * stepSec,
               "first call at %.0f s: first valid value at %.1f s", firstSec, firstValidSec);
        EXPECT(valid > 350, "first call at %.0f s: only %d valid values (%d zeros)",
               firstSec, valid, zeros);
    }
}

void testStopAndRestartKeepsTheRatio()
{
    // Idle timeout stops the device: both counters stand still while the
    // wall clock keeps going. The result must stay right after the restart,
    // with no new warm-up.
    SimStream sim;
    sim.sampleRate = 44100.0;
    sim.positionRatio = 0.9117;
    SoLoud::AAudioTimestampLatency latency;
    const int stream = 0;
    sim.runningSec = 0.2;
    Sample s = sim.read(0.03);
    latency.update(&stream, s.framesWritten, s.framePosition, s.timestampNanos, s.nowNanos,
                   sim.sampleRate);
    runAndCheck(latency, &stream, sim, 1.5, 5.0, "before stop");
    sim.stoppedSec += 30.0;
    runAndCheck(latency, &stream, sim, 5.1, 10.0, "after restart");
    sim.stoppedSec += 600.0;
    runAndCheck(latency, &stream, sim, 10.1, 20.0, "after long stop");
}

void testNewStreamRestartsMeasurement()
{
    SimStream sim;
    sim.positionRatio = 0.9117;
    SoLoud::AAudioTimestampLatency latency;
    const int streamA = 0, streamB = 0;
    sim.runningSec = 0.2;
    Sample s = sim.read(0.03);
    latency.update(&streamA, s.framesWritten, s.framePosition, s.timestampNanos, s.nowNanos,
                   sim.sampleRate);
    runAndCheck(latency, &streamA, sim, 1.5, 3.0, "stream A");

    // A rerouted device opens a new stream whose counters start again. It
    // runs at the stream rate.
    SimStream next;
    next.runningSec = 0.2;
    s = next.read(0.03);
    double value = latency.update(&streamB, s.framesWritten, s.framePosition, s.timestampNanos,
                                  s.nowNanos, next.sampleRate);
    EXPECT(value == plainResult(s, next.sampleRate),
           "new stream must warm up again (plain formula), got %f", value);
    runAndCheck(latency, &streamB, next, 1.5, 3.0, "stream B");

    // Same stream pointer, but the counters went back (a stream reopened at
    // the same address): start over too.
    SimStream reopened;
    reopened.runningSec = 0.2;
    s = reopened.read(0.03);
    value = latency.update(&streamB, s.framesWritten, s.framePosition, s.timestampNanos,
                           s.nowNanos, reopened.sampleRate);
    EXPECT(value == plainResult(s, reopened.sampleRate),
           "counters going back must restart warm-up (plain formula), got %f", value);
    runAndCheck(latency, &streamB, reopened, 1.5, 3.0, "reopened");
}

void testLastKnownAfterTimestampFails()
{
    // After a stop/start on the legacy (AudioTrack) path the stream keeps
    // playing but AAudioStream_getTimestamp() fails (SM-N975F, Android 12).
    // lastKnown() returns the last value of the same stream.
    SimStream sim;
    SoLoud::AAudioTimestampLatency latency;
    const int stream = 0, other = 0;
    sim.runningSec = 0.2;
    Sample s = sim.read(0.03);
    EXPECT(latency.lastKnown(&stream, s.framesWritten) == 0.0,
           "no value before the first update");
    const double last = runAndCheck(latency, &stream, sim, 0.2, 15.0, "before stop");
    EXPECT(last > 0.0, "expected a value before the stop, got %f", last);

    // Stopped for 3 s, then playing again with no timestamp.
    sim.stoppedSec += 3.0;
    for (double t = 15.1; t <= 25.0; t += 0.1)
    {
        sim.runningSec = t;
        s = sim.read(0.03);
        const double value = latency.lastKnown(&stream, s.framesWritten);
        EXPECT(value == last, "t=%.1f: lastKnown %f, want %f", t, value, last);
    }

    // Another stream, or the same one reopened (framesWritten went back).
    EXPECT(latency.lastKnown(&other, s.framesWritten) == 0.0, "other stream must have no value");
    EXPECT(latency.lastKnown(&stream, 1000) == 0.0, "a reopened stream must have no value");

    // Timestamps come back: update() takes over, and a new stream forgets
    // the old value.
    sim.runningSec = 25.1;
    s = sim.read(0.03);
    EXPECT(std::fabs(latency.update(&stream, s.framesWritten, s.framePosition, s.timestampNanos,
                                    s.nowNanos, sim.sampleRate) - sim.latencySec) < 0.008,
           "timestamps back: expected ~%f", sim.latencySec);
    SimStream next;
    next.sampleRate = 44100.0;
    next.positionRatio = 0.9117;
    next.runningSec = 300.0; // late first call: warm-up returns 0
    s = next.read(0.03);
    EXPECT(latency.update(&other, s.framesWritten, s.framePosition, s.timestampNanos, s.nowNanos,
                          next.sampleRate) == 0.0,
           "late first call on the new stream must warm up (0)");
    EXPECT(latency.lastKnown(&other, s.framesWritten) == 0.0,
           "the new stream must not inherit the old value");
}

void testImplausibleRatioReturnsZero()
{
    SimStream sim;
    sim.positionRatio = 0.3;
    SoLoud::AAudioTimestampLatency latency;
    const int stream = 0;
    for (int i = 0; i < 40; ++i)
    {
        sim.runningSec = 0.2 + 0.1 * i;
        const Sample s = sim.read(0.02);
        if (i < 10)
        {
            // Warm-up: the ratio is not known yet.
            latency.update(&stream, s.framesWritten, s.framePosition, s.timestampNanos,
                           s.nowNanos, sim.sampleRate);
            continue;
        }
        const double value = latency.update(&stream, s.framesWritten, s.framePosition,
                                            s.timestampNanos, s.nowNanos, sim.sampleRate);
        EXPECT(value == 0.0, "t=%.1f: expected 0 for ratio 0.3, got %f", sim.runningSec, value);
    }
}

} // namespace

int main()
{
    testSmG991nLog();
    testConformingDeviceMatchesPlainFormula();
    testWarmUp();
    testSlowPositionDevice();
    testSlightlySlowPositionDevice();
    testLateFirstCallStaysBounded();
    testStopAndRestartKeepsTheRatio();
    testNewStreamRestartsMeasurement();
    testImplausibleRatioReturnsZero();
    testLastKnownAfterTimestampFails();

    if (gFailures > 0)
    {
        std::printf("aaudio_timestamp_latency_test: %d of %d assertions failed\n",
                    gFailures, gAssertions);
        return 1;
    }
    std::printf("aaudio_timestamp_latency_test: all %d assertions passed\n", gAssertions);
    return 0;
}
