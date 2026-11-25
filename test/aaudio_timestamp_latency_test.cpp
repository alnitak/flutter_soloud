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
//   ./test/run_aaudio_timestamp_latency_test.sh

#include "../src/soloud/src/backend/miniaudio/aaudio_timestamp_latency.h"

#include <cmath>
#include <cstdint>
#include <cstdio>
#include <vector>

namespace {

int gFailures = 0;
int gAssertions = 0;

#define EXPECT(condition, format, ...) do {     ++gAssertions;     if (!(condition)) {         ++gFailures;         std::printf("FAIL %s:%d: " format "\n", __FILE__, __LINE__, ##__VA_ARGS__);     } } while (0)

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

void testConformingDeviceMatchesPlainFormula()
{
    SimStream sim;
    SoLoud::AAudioTimestampLatency latency;
    const int stream = 0;
    int compared = 0;
    for (int i = 0; i < 200; ++i)
    {
        sim.runningSec = 0.2 + 0.1 * i;
        const Sample s = sim.read(ageAt(i));
        const double value = latency.update(&stream, s.framesWritten, s.framePosition,
                                            s.timestampNanos, s.nowNanos, sim.sampleRate);
        if (sim.runningSec < 1.2 + 0.2)
            continue; // warm-up
        const double plain = plainLatency(s, sim.sampleRate);
        EXPECT(std::fabs(value - (plain > 0.0 ? plain : 0.0)) < 1e-12,
               "t=%.1f: %f != plain %f", sim.runningSec, value, plain);
        ++compared;
    }
    EXPECT(compared > 150, "compared only %d samples", compared);
}

void testWarmUpReturnsZero()
{
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
        EXPECT(value == 0.0, "t=%.1f: expected warm-up 0, got %f", sim.runningSec, value);
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
    EXPECT(value == 0.0, "new stream must warm up again, got %f", value);
    runAndCheck(latency, &streamB, next, 1.5, 3.0, "stream B");

    // Same stream pointer, but the counters went back (a stream reopened at
    // the same address): start over too.
    SimStream reopened;
    reopened.runningSec = 0.2;
    s = reopened.read(0.03);
    value = latency.update(&streamB, s.framesWritten, s.framePosition, s.timestampNanos,
                           s.nowNanos, reopened.sampleRate);
    EXPECT(value == 0.0, "counters going back must restart warm-up, got %f", value);
    runAndCheck(latency, &streamB, reopened, 1.5, 3.0, "reopened");
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
    testWarmUpReturnsZero();
    testSlowPositionDevice();
    testStopAndRestartKeepsTheRatio();
    testNewStreamRestartsMeasurement();
    testImplausibleRatioReturnsZero();

    if (gFailures > 0)
    {
        std::printf("aaudio_timestamp_latency_test: %d of %d assertions failed\n",
                    gFailures, gAssertions);
        return 1;
    }
    std::printf("aaudio_timestamp_latency_test: all %d assertions passed\n", gAssertions);
    return 0;
}
