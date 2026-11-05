/**
 * @file test_lfo.cpp
 * @brief Unit tests for techpop/lfo.h (CLFO): speed and tempo sync mapping, frequency glide,
 *        amount ranges per destination, the waveforms returned by getSameValAndTime(), the
 *        random types and the cycle bookkeeping.
 */
#include "test.h"
#include "sound_fixture.h"
#include "engine.h"

#include <stdlib.h>
#include "lfo.h"
#include "vco.h"
#include "synth1.h"

namespace {

/**
 * @brief Makes an LFO at 44100 Hz whose running frequency equals the target (no glide pending).
 *
 * @param lfo   LFO to set up.
 * @param freq  Frequency in Hz.
 */
void setupLfo(CLFO& lfo, float freq)
{
    lfo.setSamplingFreq(44100);
    lfo.setFreq(freq);
    lfo.setFreqMain(freq);
    lfo.m_curTime = 0;
}

/**
 * @brief Lets the frequency glide finish by advancing the LFO in 64-sample steps.
 *
 * @param lfo  LFO to advance.
 * @return Number of samples advanced.
 */
int finishGlide(CLFO& lfo)
{
    int n = 0;
    while (lfo.m_freqMain != lfo.m_freq && n < 44100 * 60) {
        lfo.incCurTime(64);
        n += 64;
    }
    return n;
}

/**
 * @brief Creates the shared oscillator tables (the sine LFO reads the VCO sine table).
 */
void initTables()
{
    t1::initEngine();
    static CVco* vco = 0;
    if (!vco) vco = new CVco();
}

/**
 * @brief A synthesizer for the tempo sync tests, with an isolated sound library.
 */
struct IsolatedSynth {
    soundtest::IsolatedLibrary lib;
    CSynthesizer1* synth;

    /**
     * @brief Creates the synthesizer after initEngine(), inside the isolated library.
     */
    IsolatedSynth() : synth(0) { t1::initEngine(); synth = new CSynthesizer1(); }

    /**
     * @brief Deletes the synthesizer before the library state is restored.
     */
    ~IsolatedSynth() { delete synth; }
};

}  // namespace

/** @brief The constructor sets a disabled saw LFO at 1 Hz / 44100 Hz with a zero amount range. */
TEST(lfo, constructor_defaults)
{
    CLFO lfo;
    CHECK_EQ(lfo.m_bEnable, 0);
    CHECK_EQ(lfo.m_type, LFO_SAW);
    CHECK_EQ(lfo.m_bTempoSync, 0);
    CHECK_EQ(lfo.m_bKeySync, 0);
    CHECK_EQ(lfo.m_speedDial, 0);
    CHECK_EQ(lfo.m_amountDial, 0);
    CHECK_CLOSE(lfo.m_freq, 1.0, 0);
    CHECK_CLOSE(lfo.m_freqMain, 1.0, 0);
    CHECK_EQ(lfo.m_amountLo, 0);
    CHECK_EQ(lfo.m_amountHi, 0);
    CHECK_EQ(lfo.m_curTime, 0);
    CHECK_EQ(lfo.m_cycleTime, 44100);
    CHECK_EQ(lfo.m_samplingFreq, 44100);
    CHECK_EQ(lfo.getNextCycleRestTime(), 44100);
}

/** @brief setSamplingFreq() recomputes the cycle length for the current frequency. */
TEST(lfo, sampling_freq_sets_cycle_length)
{
    CLFO lfo;
    lfo.setSamplingFreq(48000);
    CHECK_EQ(lfo.m_samplingFreq, 48000);
    CHECK_EQ(lfo.m_cycleTime, 48000);
    CHECK_EQ(lfo.m_minStep, 64);
    lfo.setFreq(4.0f);
    lfo.setFreqMain(4.0f);
    CHECK_EQ(lfo.m_cycleTime, 12000);
    lfo.setSamplingFreq(96000);
    CHECK_EQ(lfo.m_cycleTime, 24000);
}

/** @brief setFreqMain() rescales the cycle position so the phase is kept. */
TEST(lfo, set_freq_main_keeps_phase)
{
    CLFO lfo;
    setupLfo(lfo, 1.0f);
    lfo.m_curTime = 22050;
    lfo.setFreqMain(2.0f);
    CHECK_EQ(lfo.m_cycleTime, 22050);
    CHECK_EQ(lfo.m_curTime, 11025);
    lfo.setFreqMain(0.5f);
    CHECK_EQ(lfo.m_cycleTime, 88200);
    CHECK_EQ(lfo.m_curTime, 44100);
}

/** @brief setFreq() sets the minimum block length: 32 samples above 10 Hz, 64 otherwise. */
TEST(lfo, min_step_depends_on_freq)
{
    CLFO lfo;
    lfo.setSamplingFreq(44100);
    lfo.setFreq(10.0f);
    CHECK_EQ(lfo.m_minStep, 64);
    lfo.setFreq(10.5f);
    CHECK_EQ(lfo.m_minStep, 32);
    lfo.setFreq(0.1f);
    CHECK_EQ(lfo.m_minStep, 64);
}

/** @brief The running frequency glides to the target at 1 Hz per millisecond, both directions. */
TEST(lfo, freq_glides_one_hz_per_ms)
{
    CLFO lfo;
    setupLfo(lfo, 1.0f);
    lfo.setFreq(21.0f);    // setFreq itself advances the glide by one sample
    CHECK_CLOSE(lfo.m_freqMain, 1.0 + 1.0 / 44.1, 1e-4);
    CHECK_CLOSE(lfo.m_freq, 21.0, 0);
    lfo.processSmoothFreq(441);   // 10 ms -> +10 Hz
    CHECK_CLOSE(lfo.m_freqMain, 11.0 + 1.0 / 44.1, 1e-3);
    lfo.processSmoothFreq(44100); // never overshoots
    CHECK_CLOSE(lfo.m_freqMain, 21.0, 0);
    CHECK_EQ(lfo.m_cycleTime, (int)(44100 / 21.0f));

    lfo.setFreq(5.0f);
    lfo.processSmoothFreq(441);   // -10 Hz
    CHECK(lfo.m_freqMain < 11.0f + 0.01f && lfo.m_freqMain > 10.9f);
    lfo.processSmoothFreq(44100);
    CHECK_CLOSE(lfo.m_freqMain, 5.0, 0);

    // incCurTime() also drives the glide
    lfo.setFreq(6.0f);
    lfo.incCurTime(441);
    CHECK_CLOSE(lfo.m_freqMain, 6.0, 0);
}

/** @brief Without tempo sync the speed dial maps to 1500^(dial/127) / 12 Hz. */
TEST(lfo, speed_dial_maps_to_frequency)
{
    CLFO lfo;
    lfo.setSamplingFreq(44100);
    lfo.setSpeedDial(0);
    CHECK_EQ(lfo.m_speedDial, 0);
    CHECK_CLOSE(lfo.m_freq, 1.0 / 12.0, 1e-6);
    lfo.setSpeedDial(127);
    CHECK_CLOSE(lfo.m_freq, 125.0, 1e-4);
    CHECK_EQ(lfo.m_minStep, 32);
    lfo.setSpeedDial(64);
    CHECK_CLOSE(lfo.m_freq, pow(1500.0, 64 / 127.0) / 12.0, 1e-4);
    // monotonic
    float prev = 0.0f;
    for (int d = 0; d < 128; d++) {
        lfo.setSpeedDial(d);
        CHECK(lfo.m_freq > prev);
        prev = lfo.m_freq;
    }
}

/** @brief Once the glide is done the cycle length is fs / freq samples (the LFO period). */
TEST(lfo, period_matches_speed_after_glide)
{
    CLFO lfo;
    lfo.setSamplingFreq(44100);
    lfo.setSpeedDial(64);
    finishGlide(lfo);
    CHECK_CLOSE(lfo.m_freqMain, lfo.m_freq, 0);
    CHECK_EQ(lfo.m_cycleTime, (int)(44100 / lfo.m_freq));
    CHECK_EQ(lfo.m_cycleTime, 13276);
    lfo.setSpeedDial(127);
    finishGlide(lfo);
    CHECK_EQ(lfo.m_cycleTime, 352);
    lfo.setSpeedDial(0);
    finishGlide(lfo);
    CHECK_EQ(lfo.m_cycleTime, 529200);
}

/** @brief With tempo sync the dial picks a note length from the table and the host tempo. */
TEST(lfo, tempo_sync_uses_note_length_table)
{
    IsolatedSynth isolated;
    CSynthesizer1* synth = isolated.synth;
    REQUIRE(synth != 0);
    CHECK_CLOSE(synth->getBaseTempo(), 120.0, 0);
    CLFO lfo;
    lfo.setSynth1(synth);
    lfo.setSamplingFreq(44100);
    lfo.setSpeedDial(0);
    lfo.setTempoSync(1);
    CHECK_EQ(lfo.m_bTempoSync, 1);
    // the table is in 1/96 bars (1/24 beats): dial 0 = 1536 = 16 bars = 32 s at 120 BPM
    CHECK_CLOSE(lfo.m_freq, 1.0 / 32.0, 1e-7);
    // dial 60: index 14 = 48 = half a bar: 1 Hz at 120 BPM
    lfo.setSpeedDial(60);
    CHECK_CLOSE(lfo.m_freq, 1.0, 1e-6);
    // dial 127: index 29 = 1 = 1/32 triplet: 48 Hz at 120 BPM
    lfo.setSpeedDial(127);
    CHECK_CLOSE(lfo.m_freq, 48.0, 1e-4);
    finishGlide(lfo);
    CHECK_EQ(lfo.m_cycleTime, (int)(44100 / 48.0f));

    // the frequency is proportional to the tempo
    float saved = synth->m_baseTempo;
    synth->m_baseTempo = 60.0f;
    lfo.setSpeedDial(60);
    CHECK_CLOSE(lfo.m_freq, 0.5, 1e-6);
    synth->m_baseTempo = saved;
    lfo.recalcSpeed();
    CHECK_CLOSE(lfo.m_freq, 1.0, 1e-6);

    // back to free running
    lfo.setTempoSync(0);
    CHECK_CLOSE(lfo.m_freq, pow(1500.0, 60 / 127.0) / 12.0, 1e-4);
}

/** @brief setKeySync() and setEnable() only store their flag. */
TEST(lfo, flags_are_stored)
{
    CLFO lfo;
    lfo.setSamplingFreq(44100);
    lfo.setKeySync(1);
    CHECK_EQ(lfo.m_bKeySync, 1);
    lfo.setEnable(1);
    CHECK_EQ(lfo.m_bEnable, 1);
    lfo.setEnable(0);
    CHECK_EQ(lfo.m_bEnable, 0);
}

/** @brief recalcAmount() range per destination at full amount. */
TEST(lfo, amount_range_per_destination_full)
{
    CLFO lfo;
    lfo.setSamplingFreq(44100);
    lfo.setDestination(LFO_DEST_OSC2);
    lfo.setAmountDial(127);
    CHECK_EQ(lfo.m_amountLo, -640);
    CHECK_EQ(lfo.m_amountHi, 640);
    lfo.setDestination(LFO_DEST_OSC12);
    CHECK_EQ(lfo.m_amountLo, -640);
    CHECK_EQ(lfo.m_amountHi, 640);
    lfo.setDestination(LFO_DEST_PW);
    CHECK_EQ(lfo.m_amountLo, -128);
    CHECK_EQ(lfo.m_amountHi, 128);
    lfo.setDestination(LFO_DEST_FILTER);
    CHECK_EQ(lfo.m_amountLo, 0);
    CHECK_EQ(lfo.m_amountHi, 1024);
    lfo.setDestination(LFO_DEST_AMP);   // inverted: 2048 at the bottom of the wave
    CHECK_EQ(lfo.m_amountLo, 2048);
    CHECK_EQ(lfo.m_amountHi, 0);
    lfo.setDestination(LFO_DEST_FM);
    CHECK_EQ(lfo.m_amountLo, 0);
    CHECK_EQ(lfo.m_amountHi, 512);
    lfo.setDestination(LFO_DEST_PAN);
    CHECK_EQ(lfo.m_amountLo, -64);
    CHECK_EQ(lfo.m_amountHi, 64);
    lfo.setDestination(0);              // no destination
    CHECK_EQ(lfo.m_amountLo, 0);
    CHECK_EQ(lfo.m_amountHi, 0);
}

/** @brief Amount dial curves: exponential for pitch, linear otherwise; zero dial gives zero range. */
TEST(lfo, amount_dial_curves)
{
    CLFO lfo;
    lfo.setSamplingFreq(44100);
    lfo.setDestination(LFO_DEST_OSC2);
    lfo.setAmountDial(0);
    CHECK_EQ(lfo.m_amountLo, 0);
    CHECK_EQ(lfo.m_amountHi, 0);
    lfo.setAmountDial(32);
    CHECK_EQ(lfo.m_amountLo, -55);
    CHECK_EQ(lfo.m_amountHi, 55);
    lfo.setAmountDial(64);
    CHECK_EQ(lfo.m_amountLo, -155);
    CHECK_EQ(lfo.m_amountHi, 155);
    lfo.setAmountDial(100);
    CHECK_EQ(lfo.m_amountHi, 364);

    lfo.setDestination(LFO_DEST_FILTER);
    lfo.setAmountDial(64);
    CHECK_EQ(lfo.m_amountHi, 516);
    lfo.setDestination(LFO_DEST_AMP);
    CHECK_EQ(lfo.m_amountLo, 1032);
    lfo.setDestination(LFO_DEST_PAN);
    CHECK_EQ(lfo.m_amountLo, -32);
    CHECK_EQ(lfo.m_amountHi, 32);
    lfo.setDestination(LFO_DEST_PW);
    CHECK_EQ(lfo.m_amountHi, 64);

    // pitch curve is below the linear one in the middle (exponential)
    lfo.setDestination(LFO_DEST_OSC12);
    for (int d = 1; d < 127; d += 7) {
        lfo.setAmountDial(d);
        CHECK(lfo.m_amountHi <= (int)(d / 127.0 * 640.0));
        CHECK_EQ(lfo.m_amountLo, -lfo.m_amountHi);
    }
}

/** @brief setAmount() sets the range directly. */
TEST(lfo, set_amount_direct)
{
    CLFO lfo;
    lfo.setAmount(-3, 7);
    CHECK_EQ(lfo.m_amountLo, -3);
    CHECK_EQ(lfo.m_amountHi, 7);
}

/** @brief A zero amount range returns the constant value with time -1. */
TEST(lfo, zero_range_is_constant)
{
    CLFO lfo;
    setupLfo(lfo, 1.0f);
    lfo.setAmount(5, 5);
    int t = 0;
    CHECK_EQ(lfo.getSameValAndTime(&t, 0), 5);
    CHECK_EQ(t, -1);
    CHECK_EQ(lfo.getSameValAndTime(&t, 12345), 5);
    CHECK_EQ(t, -1);
}

/** @brief Saw: falls from hi to lo over a cycle; the returned time is exactly the run of equal values. */
TEST(lfo, saw_shape_and_runs)
{
    CLFO lfo;
    setupLfo(lfo, 1.0f);
    lfo.setType(LFO_SAW);
    lfo.setAmount(0, 100);
    int t = 0;
    CHECK_EQ(lfo.getSameValAndTimeMain(&t, 0), 100);
    int last = lfo.getSameValAndTimeMain(&t, 44099);
    CHECK_EQ(last, 0);
    CHECK_EQ(t, 64);   // clamped to the minimum block length

    int pos = 0, prev = 101, blocks = 0;
    while (pos < 44100) {
        int n = 0;
        int v = lfo.getSameValAndTimeMain(&n, pos);
        CHECK(v >= 0 && v <= 100);
        CHECK(v < prev);           // strictly falling block by block
        CHECK(n >= 64);
        if (pos + n - 1 < 44100) {
            int dummy;
            CHECK_EQ(lfo.getSameValAndTimeMain(&dummy, pos + n - 1), v);
        }
        if (pos + n < 44100) {
            int dummy;
            CHECK_EQ(lfo.getSameValAndTimeMain(&dummy, pos + n), v - 1);
        }
        prev = v;
        pos += n;
        blocks++;
    }
    CHECK_EQ(blocks, 101);
}

/** @brief Saw with a range larger than the cycle steps every sample. */
TEST(lfo, saw_short_cycle)
{
    CLFO lfo;
    lfo.setSamplingFreq(44100);
    lfo.m_cycleTime = 100;
    lfo.m_curTime = 0;
    lfo.setAmount(0, 1000);
    lfo.setType(LFO_SAW);
    int t = 0;
    CHECK_EQ(lfo.getSameValAndTimeMain(&t, 0), 1000);
    CHECK_EQ(t, 64);  // next = 1, clamped to the minimum block length
    CHECK_EQ(lfo.getSameValAndTimeMain(&t, 99), 0);
    CHECK_EQ(lfo.getSameValAndTimeMain(&t, 33), 1000 - (1000 * 33) / 99);
}

/** @brief An inverted range (hi < lo) mirrors the wave: the saw then rises from hi to lo. */
TEST(lfo, inverted_range_mirrors)
{
    CLFO lfo;
    setupLfo(lfo, 1.0f);
    lfo.setType(LFO_SAW);
    lfo.setAmount(100, 0);
    int t = 0;
    CHECK_EQ(lfo.getSameValAndTimeMain(&t, 0), 0);
    CHECK_EQ(lfo.getSameValAndTimeMain(&t, 44099), 100);
    CLFO ref;
    setupLfo(ref, 1.0f);
    ref.setAmount(0, 100);
    for (int pos = 0; pos < 44100; pos += 997) {
        int a, b;
        CHECK_EQ(lfo.getSameValAndTimeMain(&a, pos), 100 - ref.getSameValAndTimeMain(&b, pos));
        CHECK_EQ(a, b);
    }
}

/** @brief Triangle: starts at lo, reaches hi at half the cycle and comes back; blocks are constant. */
TEST(lfo, triangle_shape)
{
    CLFO lfo;
    setupLfo(lfo, 1.0f);
    lfo.setType(LFO_TRIANGLE);
    lfo.setAmount(-50, 50);
    int t = 0;
    CHECK_EQ(lfo.getSameValAndTimeMain(&t, 0), -50);
    CHECK_EQ(lfo.getSameValAndTimeMain(&t, 22050), 50);
    CHECK_EQ(lfo.getSameValAndTimeMain(&t, 11025), 0);
    CHECK_EQ(lfo.getSameValAndTimeMain(&t, 33075), 0);
    int pos = 0, maxv = -1000, minv = 1000;
    while (pos < 44100) {
        int n = 0;
        int v = lfo.getSameValAndTimeMain(&n, pos);
        CHECK(n >= 64);
        CHECK(v >= -50 && v <= 50);
        if (pos + n - 1 < 44100) {
            int dummy;
            CHECK_EQ(lfo.getSameValAndTimeMain(&dummy, pos + n - 1), v);
        }
        if (pos < 22050) CHECK(pos + n <= 22050);   // blocks never straddle the peak
        if (v > maxv) maxv = v;
        if (v < minv) minv = v;
        pos += n;
    }
    CHECK_EQ(maxv, 50);
    CHECK_EQ(minv, -50);
}

/** @brief Triangle with a range larger than the cycle: both halves step every sample (minimum block). */
TEST(lfo, triangle_short_cycle)
{
    CLFO lfo;
    lfo.setSamplingFreq(44100);
    lfo.m_cycleTime = 100;
    lfo.m_curTime = 0;
    lfo.setAmount(0, 1000);
    lfo.setType(LFO_TRIANGLE);
    int t = 0;
    // rising half: val = range * time / half, next = time + 1 (clamped to the 64-sample minimum)
    CHECK_EQ(lfo.getSameValAndTimeMain(&t, 0), 0);
    CHECK_EQ(t, 64);
    CHECK_EQ(lfo.getSameValAndTimeMain(&t, 25), 500);
    CHECK_EQ(t, 64);
    CHECK_EQ(lfo.getSameValAndTimeMain(&t, 49), 980);
    // falling half: val = max - (t - half) * range / len, next = (t - half) + 1 + half
    CHECK_EQ(lfo.getSameValAndTimeMain(&t, 50), 1000);
    CHECK_EQ(t, 64);
    CHECK_EQ(lfo.getSameValAndTimeMain(&t, 75), 500);
    CHECK_EQ(lfo.getSameValAndTimeMain(&t, 99), 1000 - 49 * 1000 / 50);
    // a shorter minimum block shows the per-sample step
    lfo.m_minStep = 1;
    CHECK_EQ(lfo.getSameValAndTimeMain(&t, 10), 200);
    CHECK_EQ(t, 1);
    CHECK_EQ(lfo.getSameValAndTimeMain(&t, 60), 800);
    CHECK_EQ(t, 1);
}

/** @brief Square: hi for the first half cycle, lo for the second; time runs to the edge. */
TEST(lfo, square_shape)
{
    CLFO lfo;
    setupLfo(lfo, 1.0f);
    lfo.setType(LFO_SQUARE);
    lfo.setAmount(-10, 10);
    int t = 0;
    CHECK_EQ(lfo.getSameValAndTimeMain(&t, 0), 10);
    CHECK_EQ(t, 22050);
    CHECK_EQ(lfo.getSameValAndTimeMain(&t, 1000), 10);
    CHECK_EQ(t, 21050);
    CHECK_EQ(lfo.getSameValAndTimeMain(&t, 22050), -10);
    CHECK_EQ(t, 22050);
    CHECK_EQ(lfo.getSameValAndTimeMain(&t, 22040), 10);
    CHECK_EQ(t, 64);   // 10 samples to the edge, clamped to m_minStep
    CHECK_EQ(lfo.getSameValAndTimeMain(&t, 44000), -10);
    CHECK_EQ(t, 100);
}

/** @brief Sine: mid value at 0, hi at a quarter, lo at three quarters; blocks of m_minStep. */
TEST(lfo, sine_shape)
{
    initTables();
    CLFO lfo;
    setupLfo(lfo, 1.0f);
    lfo.setType(LFO_SINE);
    lfo.setAmount(-100, 100);
    int t = 0;
    CHECK_EQ(lfo.getSameValAndTimeMain(&t, 0), 0);
    CHECK_EQ(t, 64);
    CHECK_EQ(lfo.getSameValAndTimeMain(&t, 11025), 100);
    CHECK_EQ(t, 64);   // the time to the next m_minStep boundary is always raised to m_minStep
    CHECK_EQ(lfo.getSameValAndTimeMain(&t, 22050), 0);
    CHECK_EQ(lfo.getSameValAndTimeMain(&t, 33075), -100);
    for (int pos = 0; pos < 44100; pos += 113) {
        int v = lfo.getSameValAndTimeMain(&t, pos);
        double expect = 100.0 * sin(2 * M_PI * (double)pos / 44100.0);
        CHECK(fabs(v - expect) <= 1.5);
    }
    // offset range: centered on (lo + hi) / 2
    lfo.setAmount(0, 1024);
    CHECK_EQ(lfo.getSameValAndTimeMain(&t, 0), 512);
    CHECK_EQ(lfo.getSameValAndTimeMain(&t, 11025), 1024);
    CHECK_EQ(lfo.getSameValAndTimeMain(&t, 33075), 0);
}

/** @brief getSameValAndTime() reads at the current position plus offset, wrapping at the cycle. */
TEST(lfo, offset_wraps_around_cycle)
{
    CLFO lfo;
    setupLfo(lfo, 1.0f);
    lfo.setType(LFO_SQUARE);
    lfo.setAmount(0, 1);
    lfo.m_curTime = 40000;
    int t = 0;
    CHECK_EQ(lfo.getSameValAndTime(&t, 0), 0);
    CHECK_EQ(t, 4100);
    CHECK_EQ(lfo.getSameValAndTime(&t, 4100), 1);    // wrapped to 0
    CHECK_EQ(t, 22050);
    CHECK_EQ(lfo.getSameValAndTime(&t, 4100 + 22050), 0);
}

/** @brief incCurTime() advances and wraps the cycle position; getNextCycleRestTime() is the remainder. */
TEST(lfo, inc_cur_time_wraps)
{
    CLFO lfo;
    setupLfo(lfo, 1.0f);
    lfo.incCurTime(1000);
    CHECK_EQ(lfo.m_curTime, 1000);
    CHECK_EQ(lfo.getNextCycleRestTime(), 43100);
    lfo.incCurTime(43100);
    CHECK_EQ(lfo.m_curTime, 0);
    lfo.incCurTime(44100 * 2 + 7);
    CHECK_EQ(lfo.m_curTime, 7);
    lfo.reset();
    CHECK_EQ(lfo.m_curTime, 0);
    CHECK_EQ(lfo.m_resetCount, 0);
}

/** @brief Sample & hold: a constant random level in range for the whole cycle, a new one at each wrap. */
TEST(lfo, sample_and_hold)
{
    CLFO lfo;
    setupLfo(lfo, 1.0f);
    lfo.setAmount(0, 1000);
    srand(12345);
    lfo.setType(LFO_SAMPLEHOLD);
    CHECK_EQ(lfo.m_prevRandLevel, 500);   // starts from the middle
    int level = lfo.m_randLevel;
    CHECK(level >= 0 && level < 1000);
    int t = 0;
    CHECK_EQ(lfo.getSameValAndTime(&t, 0), level);
    CHECK_EQ(t, 44100);
    CHECK_EQ(lfo.getSameValAndTime(&t, 30000), level);
    CHECK_EQ(t, 14100);

    int distinct = 0;
    for (int i = 0; i < 20; i++) {
        int before = lfo.m_randLevel;
        lfo.incCurTime(44000);
        CHECK_EQ(lfo.m_randLevel, before);    // no wrap yet
        lfo.incCurTime(200);                  // wraps
        CHECK_EQ(lfo.m_prevRandLevel, before);
        CHECK(lfo.m_randLevel >= 0 && lfo.m_randLevel < 1000);
        if (lfo.m_randLevel != before) distinct++;
        lfo.m_curTime = 0;
    }
    CHECK(distinct >= 15);

    // inverted range also stays within [hi, lo)
    lfo.setAmount(1000, 0);
    for (int i = 0; i < 50; i++) {
        lfo.makeRandomLevel(0);
        CHECK(lfo.m_randLevel >= 0 && lfo.m_randLevel < 1000);
    }
    // zero range: the level is the amount
    lfo.setAmount(7, 7);
    lfo.makeRandomLevel(3);
    CHECK_EQ(lfo.m_randLevel, 7);
    CHECK_EQ(lfo.m_prevRandLevel, 3);
}

/** @brief The random levels depend only on the rand() seed. */
TEST(lfo, random_is_deterministic_for_seed)
{
    CLFO a, b;
    setupLfo(a, 1.0f);
    setupLfo(b, 1.0f);
    a.setAmount(-64, 64);
    b.setAmount(-64, 64);
    int seqA[16], seqB[16];
    srand(777);
    a.setType(LFO_SAMPLEHOLD);
    for (int i = 0; i < 16; i++) {
        a.incCurTime(44100);
        seqA[i] = a.m_randLevel;
    }
    srand(777);
    b.setType(LFO_SAMPLEHOLD);
    for (int i = 0; i < 16; i++) {
        b.incCurTime(44100);
        seqB[i] = b.m_randLevel;
    }
    for (int i = 0; i < 16; i++) CHECK_EQ(seqA[i], seqB[i]);
}

/** @brief reset() picks a new random level for the random types, starting from the old one. */
TEST(lfo, reset_renews_random_level)
{
    CLFO lfo;
    setupLfo(lfo, 1.0f);
    lfo.setAmount(0, 100000);
    srand(99);
    lfo.setType(LFO_SMOOTHRANDOM);
    int old = lfo.m_randLevel;
    lfo.m_curTime = 500;
    lfo.reset();
    CHECK_EQ(lfo.m_curTime, 0);
    CHECK_EQ(lfo.m_prevRandLevel, old);

    // non-random types leave the levels alone
    lfo.setType(LFO_SAW);
    int r = lfo.m_randLevel, p = lfo.m_prevRandLevel;
    lfo.reset();
    CHECK_EQ(lfo.m_randLevel, r);
    CHECK_EQ(lfo.m_prevRandLevel, p);
}

/** @brief Smooth random glides linearly from the previous level to the new one over a cycle. */
TEST(lfo, smooth_random_glides)
{
    CLFO lfo;
    setupLfo(lfo, 1.0f);
    lfo.setAmount(0, 1000);
    lfo.m_type = LFO_SMOOTHRANDOM;
    int t = 0;

    // rising
    lfo.m_prevRandLevel = 100;
    lfo.m_randLevel = 900;
    CHECK_EQ(lfo.getSameValAndTimeMain(&t, 0), 100);
    CHECK(t >= 64);
    int end = lfo.getSameValAndTimeMain(&t, 44099);
    CHECK(end >= 895 && end <= 900);
    int mid = lfo.getSameValAndTimeMain(&t, 22050);
    CHECK(mid >= 495 && mid <= 505);
    int prev = -1;
    for (int pos = 0; pos < 44100; pos += 441) {
        int v = lfo.getSameValAndTimeMain(&t, pos);
        CHECK(v >= prev);
        prev = v;
    }

    // falling
    lfo.m_prevRandLevel = 800;
    lfo.m_randLevel = 200;
    CHECK_EQ(lfo.getSameValAndTimeMain(&t, 0), 800);
    end = lfo.getSameValAndTimeMain(&t, 44099);
    CHECK(end >= 200 && end <= 205);
    prev = 100000;
    for (int pos = 0; pos < 44100; pos += 441) {
        int v = lfo.getSameValAndTimeMain(&t, pos);
        CHECK(v <= prev);
        prev = v;
    }

    // equal levels: constant until the end of the cycle
    lfo.m_prevRandLevel = 300;
    lfo.m_randLevel = 300;
    CHECK_EQ(lfo.getSameValAndTimeMain(&t, 1000), 300);
    CHECK_EQ(t, 43100);
}

/** @brief Smooth random: at a cycle wrap the new glide starts where the last one ended. */
TEST(lfo, smooth_random_is_continuous_across_cycles)
{
    CLFO lfo;
    setupLfo(lfo, 1.0f);
    lfo.setAmount(-500, 500);
    srand(4242);
    lfo.setType(LFO_SMOOTHRANDOM);
    for (int i = 0; i < 10; i++) {
        int target = lfo.m_randLevel;
        lfo.incCurTime(44100);
        CHECK_EQ(lfo.m_prevRandLevel, target);
        int t;
        CHECK_EQ(lfo.getSameValAndTime(&t, 0), target);
    }
}
