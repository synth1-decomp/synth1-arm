/**
 * @file test_phaser.cpp
 * @brief Unit tests for techpop/phaser.h (CPhaser): sweep factor and range, bypass and depth 0,
 *        all-pass magnitude, the notch at depth 0.5, bounded feedback output, stereo symmetry and
 *        the silence tail.
 *
 * The sweep direction after construction depends on an uninitialised member (m_freqMul is read by
 * the first setRate()), so the tests first force a known direction with primeDown().
 */
#include "test.h"

#include <math.h>
#include <string.h>

#include "phaser.h"

namespace {

const double kPi = 3.14159265358979323846;

/**
 * @brief Gives the tests access to the protected CPhaser state.
 */
class PhaserProbe : public CPhaser {
public:
    float freq() const { return m_freq; }
    float freqMul() const { return m_freqMul; }
    float freqCoef() const { return m_freqCoef; }
    int stage() const { return m_nStage; }
    int silentLimit() const { return m_silentLimit; }
    float feedback() const { return m_feedback; }
    float depth() const { return m_depth; }

    /**
     * @brief Forces a downward sweep: a range below the current frequency makes the next block
     *        reverse the direction (processing zeros keeps the all-pass state at zero).
     */
    void primeDown()
    {
        float m = m_freq;
        setRange(m * 0.25f, m * 0.5f);
        float z[16];
        memset(z, 0, sizeof z);
        int processed = 0;
        processSignal(16, z, &processed);
    }
};

/**
 * @brief Steady-state peak of a sine through a phaser (mono path).
 */
double sineGain(CPhaser& p, double freq)
{
    static float buf[44100];
    for (int i = 0; i < 44100; i++) buf[i] = (float)sin(2 * kPi * freq * i / 44100);
    int processed = 0;
    for (int off = 0; off < 44100; off += 441) p.processSignal(441, buf + off, &processed);
    double peak = 0;
    for (int i = 22050; i < 44100; i++) peak = fabs(buf[i]) > peak ? fabs(buf[i]) : peak;
    return peak;
}

}  // namespace

/** @brief Defaults: 1 stage, feedback 0.7, depth 0.5, 25 ms silence limit, frequency coefficient 4 / fs. */
TEST(phaser, defaults)
{
    PhaserProbe p;
    CHECK_EQ(p.stage(), 1);
    CHECK_CLOSE(p.feedback(), 0.7, 1e-7);
    CHECK_CLOSE(p.depth(), 0.5, 0);
    CHECK_EQ(p.silentLimit(), 1102);
    CHECK_CLOSE(p.freqCoef(), 4.0 / 44100, 1e-10);
    CHECK_CLOSE(p.freq(), 200.0, 0);
    p.setSamplingFreq(48000);
    CHECK_EQ(p.silentLimit(), 1200);
    CHECK_CLOSE(p.freqCoef(), 4.0 / 48000, 1e-10);
}

/** @brief The per-sample sweep factor covers max/min in half an LFO period; setRate() keeps the direction. */
TEST(phaser, sweep_factor_and_direction)
{
    PhaserProbe p;
    p.primeDown();
    CHECK(p.freqMul() < 1.0f);
    p.setRange(100.0f, 6400.0f);   // ratio 64
    p.setRate(1.0f);               // half period 22050 samples
    CHECK(p.freqMul() < 1.0f);
    CHECK_CLOSE(log(1.0 / p.freqMul()), log(64.0) / 22050.0, 2e-7);   // float precision
    // reversal: below the range the next block turns the sweep upwards
    float z[64];
    memset(z, 0, sizeof z);
    int processed = 0;
    p.setRange(1000.0f, 2000.0f);  // m_freq is far below 1000
    p.processSignal(64, z, &processed);
    CHECK(p.freqMul() > 1.0f);
    p.setRate(2.0f);
    CHECK(p.freqMul() > 1.0f);
    CHECK_CLOSE(log(p.freqMul()), log(2.0) / 11025.0, 2e-7);
}

/** @brief Over several LFO periods the frequency sweeps the whole range and stays (nearly) inside it. */
TEST(phaser, sweep_stays_in_range)
{
    PhaserProbe p;
    p.primeDown();
    p.setRange(300.0f, 3000.0f);
    p.setRate(5.0f);
    float buf[64];
    double lo = 1e9, hi = 0;
    for (int blk = 0; blk < 44100 / 64 * 2; blk++) {
        memset(buf, 0, sizeof buf);
        int processed = 0;
        p.processSignal(64, buf, &processed);
        if (blk > 100) {
            lo = p.freq() < lo ? p.freq() : lo;
            hi = p.freq() > hi ? p.freq() : hi;
        }
    }
    // the direction is only checked once per 64-sample block
    double step = pow((double)p.freqMul() > 1 ? p.freqMul() : 1.0 / p.freqMul(), 64.0);
    CHECK(lo >= 300.0 / step * 0.999);
    CHECK(hi <= 3000.0 * step * 1.001);
    CHECK(lo < 300.0 * 1.05);
    CHECK(hi > 3000.0 / 1.05);
}

/** @brief Disabled or depth 0: the signal passes through unchanged. */
TEST(phaser, bypass_and_zero_depth)
{
    PhaserProbe p;
    p.setEnable(0);
    float b[64], in[64];
    for (int i = 0; i < 64; i++) in[i] = b[i] = (float)sin(i * 0.2);
    int processed = 0;
    CHECK_EQ(p.processSignal(64, b, &processed), 64);
    CHECK(memcmp(b, in, sizeof b) == 0);

    PhaserProbe q;
    q.primeDown();
    q.setDepth(0.0f);
    q.setFeedback(0.9f);
    q.setStage(6);
    q.processSignal(64, b, &processed);
    CHECK(memcmp(b, in, sizeof b) == 0);
}

/** @brief With depth 1 and no feedback the chain is an all-pass: a steady sine keeps its amplitude. */
TEST(phaser, allpass_magnitude)
{
    PhaserProbe p;
    p.setRange(1000.0f, 1000.0f);   // factor 1: frequency frozen (at 200 Hz)
    p.setDepth(1.0f);
    p.setFeedback(0.0f);
    p.setStage(4);
    CHECK_CLOSE(p.freqMul(), 1.0, 0);
    CHECK_CLOSE(sineGain(p, 300.0), 1.0, 0.01);
    CHECK_CLOSE(sineGain(p, 5000.0), 1.0, 0.01);
    CHECK_CLOSE(p.freq(), 200.0, 0);
}

/** @brief With depth 0.5 the dry/all-pass mix has a deep notch (about 255 Hz for the frozen 200 Hz sweep frequency) and passes low frequencies. */
TEST(phaser, notch_at_half_depth)
{
    double minG = 9, minF = 0;
    for (int k = 0; k <= 100; k++) {
        double f = 150.0 + 2.5 * k;
        PhaserProbe p;
        p.setRange(1000.0f, 1000.0f);   // frozen at 200 Hz: a = (1 - w) / (1 + w), w = 800 / fs
        p.setDepth(0.5f);
        p.setFeedback(0.0f);
        p.setStage(1);
        double g = sineGain(p, f);
        if (g < minG) {
            minG = g;
            minF = f;
        }
    }
    CHECK(minG < 0.05);
    // each first-order section (z^-1 - a) / (1 - a z^-1) is at -90 degrees where
    // cos(omega) = 2a / (1 + a^2)
    double w = 800.0 / 44100, a = (1 - w) / (1 + w);
    CHECK_CLOSE(minF, acos(2 * a / (1 + a * a)) * 44100 / (2 * kPi), 5.0);
    PhaserProbe q;
    q.setRange(1000.0f, 1000.0f);
    q.setDepth(0.5f);
    q.setFeedback(0.0f);
    CHECK(sineGain(q, 5000.0) > 0.95);
}

/** @brief A sweeping phaser with feedback changes the signal but stays bounded, mono and stereo. */
TEST(phaser, changes_signal_bounded)
{
    for (int stages = 1; stages <= 6; stages++) {
        PhaserProbe p;
        p.primeDown();
        p.setRange(200.0f, 8000.0f);
        p.setRate(2.0f);
        p.setStage(stages);
        p.setFeedback(0.9f);
        p.setDepth(0.5f);
        static float buf[2 * 22050];
        for (int i = 0; i < 22050; i++) {
            float v = (float)(0.5 * sin(2 * kPi * 440 * i / 44100) + 0.3 * sin(2 * kPi * 1500 * i / 44100));
            buf[i * 2] = v;
            buf[i * 2 + 1] = v;
        }
        double diff = 0, peak = 0;
        int sym = 1;
        for (int off = 0; off < 22050; off += 245) {
            float in[2 * 245];
            memcpy(in, buf + off * 2, sizeof in);
            int processed = 0;
            p.processSignalW(245, buf + off * 2, &processed);
            for (int i = 0; i < 2 * 245; i++) {
                diff += fabs(buf[off * 2 + i] - in[i]);
                peak = fabs(buf[off * 2 + i]) > peak ? fabs(buf[off * 2 + i]) : peak;
            }
        }
        for (int i = 0; i < 22050; i++) if (buf[i * 2] != buf[i * 2 + 1]) sym = 0;
        CHECK(sym);
        CHECK(diff > 100.0);
        CHECK(peak == peak);   // finite (not NaN)
        CHECK(peak < 4.0);     // observed 1.4 .. 2.1 for this input (peak 0.8)
    }
}

/** @brief Silent input: a 25 ms tail is rendered (zeros in, flag cleared), then the buffer is left alone. */
TEST(phaser, silence_tail)
{
    PhaserProbe p;
    p.primeDown();
    float b[256];
    for (int i = 0; i < 256; i++) b[i] = 0.5f;
    int processed = 0;
    p.processSignal(256, b, &processed);
    int rendered = 0;
    for (int blk = 0; blk < 8; blk++) {
        for (int i = 0; i < 256; i++) b[i] = 7.0f;
        processed = 1;
        p.processSignal(256, b, &processed);
        if (!processed) {
            rendered++;
            CHECK(b[0] != 7.0f);
        } else {
            CHECK(b[0] == 7.0f);
        }
    }
    CHECK_EQ(rendered, 5);   // entry counts 0..1024 <= 1102
}

/** @brief Silence in gives silence out: zeros through a fresh phaser stay exactly zero. */
TEST(phaser, zeros_stay_zero)
{
    PhaserProbe p;
    p.primeDown();
    p.setStage(6);
    p.setFeedback(0.95f);
    float b[2 * 512];
    memset(b, 0, sizeof b);
    int processed = 0;
    p.processSignalW(512, b, &processed);
    int zero = 1;
    for (int i = 0; i < 1024; i++) if (b[i] != 0.0f) zero = 0;
    CHECK(zero);
}

/** @brief Stereo silent input: the tail is rendered from a zero-filled stereo buffer (both channels), then left alone. */
TEST(phaser, silence_tail_stereo)
{
    PhaserProbe p;
    p.primeDown();
    float b[2 * 256];
    for (int i = 0; i < 2 * 256; i++) b[i] = 0.5f;
    int processed = 0;
    p.processSignalW(256, b, &processed);
    int rendered = 0;
    for (int blk = 0; blk < 8; blk++) {
        for (int i = 0; i < 2 * 256; i++) b[i] = 7.0f;
        processed = 1;
        CHECK_EQ(p.processSignalW(256, b, &processed), 256);
        if (!processed) {
            rendered++;
            int changed = 1;
            for (int i = 0; i < 2 * 256; i++) if (b[i] == 7.0f) changed = 0;
            CHECK(changed);
        } else {
            CHECK(b[0] == 7.0f && b[511] == 7.0f);
        }
    }
    CHECK_EQ(rendered, 5);   // entry counts 0..1024 <= 1102
}
