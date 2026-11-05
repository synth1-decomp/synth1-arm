/**
 * @file test_eq.cpp
 * @brief Unit tests for techpop/eq.h (CEq): peaking boost/cut at the centre frequency and its Q,
 *        first-order low/high pass modes and their level, bypass conditions, mono mode, the
 *        forced (insertion filter) entry points and the silence tail.
 */
#include "test.h"

#include <math.h>
#include <string.h>
#include <vector>

#include "eq.h"

namespace {

const double kPi = 3.14159265358979323846;

/**
 * @brief Gives the tests access to the protected CEq state.
 */
class EqProbe : public CEq {
public:
    int mode() const { return m_mode; }
    float freq() const { return m_freq; }
    float level() const { return m_level; }
    float b0() const { return m_b0; }
    float a1() const { return m_a1; }
    int silentLimit() const { return m_silentLimit; }
    void setEnableFlag(int b) { m_bEnable = b; }
};

/**
 * @brief Signal source producing a sine (same on both channels) or silence.
 */
class SineSource : public CSignalDevice {
public:
    double freq;
    double amp;
    long pos;
    int silent;

    SineSource() : freq(1000.0), amp(1.0), pos(0), silent(0) {}

    float next() { return (float)(amp * sin(2.0 * kPi * freq * (pos++) / 44100.0)); }

    virtual int processSignal(int size, float* pBuf, int* pProcessed)
    {
        if (silent) { *pProcessed = 1; return size; }
        for (int i = 0; i < size; i++) pBuf[i] = next();
        *pProcessed = 0;
        return size;
    }

    virtual int processSignalW(int size, float* pBuf, int* pProcessed)
    {
        if (silent) { *pProcessed = 1; return size; }
        for (int i = 0; i < size; i++) pBuf[i * 2] = pBuf[i * 2 + 1] = next();
        *pProcessed = 0;
        return size;
    }
};

/**
 * @brief Measures the steady-state gain of an EQ for a sine of the given frequency.
 *
 * @param eq      Configured EQ (its input is replaced).
 * @param freq    Test frequency in Hz.
 * @param stereo  Use processSignalW() (both channels checked) instead of processSignal().
 * @return Peak output over the second half of a 1 s run.
 */
double sineGain(CEq& eq, double freq, int stereo)
{
    SineSource src;
    src.freq = freq;
    eq.inputDeviceIs(&src);
    float buf[512];
    double peak = 0;
    for (int blk = 0; blk < 172; blk++) {
        int processed = 0;
        if (stereo) eq.processSignalW(256, buf, &processed);
        else eq.processSignal(256, buf, &processed);
        if (blk >= 86) {
            int n = stereo ? 512 : 256;
            for (int i = 0; i < n; i++) peak = fabs(buf[i]) > peak ? fabs(buf[i]) : peak;
        }
    }
    eq.inputDeviceIs(NULL);
    return peak;
}

/**
 * @brief Steady-state output of an EQ for a constant (DC) input.
 */
double dcGain(CEq& eq)
{
    float buf[4096];
    double y = 0;
    for (int blk = 0; blk < 20; blk++) {
        for (int i = 0; i < 4096; i++) buf[i] = 1.0f;
        eq.processSignalForced(4096, buf, NULL);
        y = buf[4095];
    }
    return y;
}

}  // namespace

/** @brief Defaults: enabled stereo peaking band at 100 Hz, Q 3, 0 dB; 200 ms silence limit. */
TEST(eq, defaults)
{
    EqProbe e;
    CHECK_EQ(e.mode(), EQ_MODE_PEAKING);
    CHECK_CLOSE(e.freq(), 100.0, 0);
    CHECK_CLOSE(e.level(), 1.0, 0);
    CHECK_EQ(e.silentLimit(), 8820);
    e.setSamplingFreq(48000);
    CHECK_EQ(e.silentLimit(), 9600);
}

/** @brief Peaking boost and cut: the gain at the centre frequency is the requested gain in dB. */
TEST(eq, peaking_gain_at_centre)
{
    float gains[4] = {12.0f, 6.0f, -6.0f, -15.0f};
    for (int k = 0; k < 4; k++) {
        EqProbe e;
        e.setFreq(1000.0f);
        e.setQ(2.0f);
        e.setGain(gains[k]);
        CHECK_CLOSE(sineGain(e, 1000.0, 1), pow(10.0, gains[k] / 20.0), 0.01 * pow(10.0, gains[k] / 20.0));
    }
    EqProbe m;
    m.setFreq(3000.0f);
    m.setQ(1.0f);
    m.setGain(9.0f);
    CHECK_CLOSE(sineGain(m, 3000.0, 0), pow(10.0, 9.0 / 20.0), 0.03);
}

/** @brief Far from the centre a peaking band is close to unity, and a higher Q gives a narrower bell. */
TEST(eq, peaking_q_and_skirts)
{
    EqProbe e;
    e.setFreq(1000.0f);
    e.setQ(4.0f);
    e.setGain(12.0f);
    double far = sineGain(e, 60.0, 1);
    CHECK(far > 0.9 && far < 1.15);
    double nearNarrow = sineGain(e, 1400.0, 1);
    EqProbe w;
    w.setFreq(1000.0f);
    w.setQ(0.5f);
    w.setGain(12.0f);
    double nearWide = sineGain(w, 1400.0, 1);
    CHECK(nearWide > nearNarrow);
    CHECK(nearNarrow > 1.0);
    CHECK(nearWide < pow(10.0, 12.0 / 20.0));
}

/** @brief 0 dB peaking is bypassed: the buffer comes back bit-identical. */
TEST(eq, zero_gain_is_bypass)
{
    EqProbe e;
    e.setFreq(1000.0f);
    SineSource src;
    e.inputDeviceIs(&src);
    float buf[512];
    int processed = 0;
    e.processSignalW(256, buf, &processed);
    SineSource ref;
    int same = 1;
    for (int i = 0; i < 256; i++) {
        float v = ref.next();
        if (buf[i * 2] != v || buf[i * 2 + 1] != v) same = 0;
    }
    CHECK(same);
}

/** @brief Low pass: first-order bilinear coefficients, unity DC gain, -3 dB near the cutoff, highs attenuated. */
TEST(eq, lowpass_mode)
{
    EqProbe e;
    e.setMode(EQ_MODE_LOWPASS);
    e.setFreq(1000.0f);
    double k = 88200.0, wd = 2 * kPi * 1000.0;
    CHECK_CLOSE(e.b0(), wd / (k + wd), 1e-7);
    CHECK_CLOSE(e.a1(), (k - wd) / (k + wd), 1e-7);
    CHECK_CLOSE(dcGain(e), 1.0, 1e-4);
    EqProbe f;
    f.setMode(EQ_MODE_LOWPASS);
    f.setFreq(1000.0f);
    CHECK_CLOSE(sineGain(f, 1000.0, 1), sqrt(0.5), 0.03);
    CHECK(sineGain(f, 15000.0, 1) < 0.1);
}

/** @brief High pass: blocks DC, passes highs; the gain setting is an output level in the pass modes. */
TEST(eq, highpass_mode_and_level)
{
    EqProbe e;
    e.setMode(EQ_MODE_HIGHPASS);
    e.setFreq(200.0f);
    CHECK(fabs(dcGain(e)) < 1e-4);
    CHECK_CLOSE(sineGain(e, 10000.0, 1), 1.0, 0.01);
    e.setGain(-6.0f);
    CHECK_CLOSE(e.level(), pow(10.0, -6.0 / 20.0), 1e-6);
    CHECK_CLOSE(sineGain(e, 10000.0, 0), pow(10.0, -6.0 / 20.0), 0.01);
}

/** @brief Pass modes are bypassed at their extremes: low pass at or above Nyquist, high pass at or below 1 Hz (freq clamped to 1). */
TEST(eq, pass_mode_bypass)
{
    EqProbe lp;
    lp.setMode(EQ_MODE_LOWPASS);
    lp.setFreq(22050.0f);
    lp.setGain(-20.0f);   // would scale if the filter ran
    SineSource src;
    lp.inputDeviceIs(&src);
    float buf[512];
    int processed = 0;
    lp.processSignalW(256, buf, &processed);
    SineSource ref;
    int same = 1;
    for (int i = 0; i < 256; i++) {
        float v = ref.next();
        if (buf[i * 2] != v || buf[i * 2 + 1] != v) same = 0;
    }
    CHECK(same);
    double peak;

    EqProbe hp;
    hp.setMode(EQ_MODE_HIGHPASS);
    hp.setFreq(0.25f);
    CHECK_CLOSE(hp.freq(), 1.0, 0);
    hp.setGain(-20.0f);
    SineSource s2;
    hp.inputDeviceIs(&s2);
    hp.processSignal(256, buf, &processed);
    peak = 0;
    for (int i = 0; i < 256; i++) peak = fabs(buf[i]) > peak ? fabs(buf[i]) : peak;
    CHECK(peak > 0.99);
}

/** @brief A disabled band leaves the signal alone, also through the forced entry points. */
TEST(eq, disabled_is_bypass)
{
    EqProbe e;
    e.setFreq(1000.0f);
    e.setGain(12.0f);
    e.setEnableFlag(0);
    float b[4] = {0.5f, -0.5f, 0.25f, 1.0f};
    e.processSignalWForced(2, b, NULL);
    e.processSignalForced(4, b, NULL);
    CHECK(b[0] == 0.5f && b[1] == -0.5f && b[2] == 0.25f && b[3] == 1.0f);
    CHECK_CLOSE(sineGain(e, 1000.0, 1), 1.0, 1e-4);   // sampled sine peak
}

/** @brief Mono mode filters one channel and duplicates it, giving the same gain on both channels. */
TEST(eq, mono_mode_duplicates)
{
    EqProbe e;
    e.setStereoMode(0);
    e.setFreq(2000.0f);
    e.setGain(-12.0f);
    SineSource src;
    src.freq = 2000.0;
    e.inputDeviceIs(&src);
    float buf[512];
    int ok = 1;
    double peak = 0;
    for (int blk = 0; blk < 100; blk++) {
        int processed = 0;
        e.processSignalW(256, buf, &processed);
        for (int i = 0; i < 256; i++) {
            if (buf[i * 2] != buf[i * 2 + 1]) ok = 0;
            if (blk > 50) peak = fabs(buf[i * 2]) > peak ? fabs(buf[i * 2]) : peak;
        }
    }
    CHECK(ok);
    CHECK_CLOSE(peak, pow(10.0, -12.0 / 20.0), 0.01);
}

/**
 * @brief The remaining bypass paths: stereo high pass at 1 Hz, mono low pass at Nyquist and a
 *        disabled band in mono leave the input untouched; the stereo forced path applies the
 *        pass-mode output level.
 */
TEST(eq, bypass_paths_stereo_and_mono)
{
    float buf[512];
    int processed = 0;

    EqProbe hp;
    hp.setMode(EQ_MODE_HIGHPASS);
    hp.setFreq(0.5f);
    hp.setGain(-20.0f);
    SineSource s1;
    hp.inputDeviceIs(&s1);
    CHECK_EQ(hp.processSignalW(256, buf, &processed), 256);
    SineSource r1;
    int same = 1;
    for (int i = 0; i < 256; i++) {
        float v = r1.next();
        if (buf[i * 2] != v || buf[i * 2 + 1] != v) same = 0;
    }
    CHECK(same);

    EqProbe lp;
    lp.setMode(EQ_MODE_LOWPASS);
    lp.setFreq(22050.0f);
    lp.setGain(-20.0f);
    SineSource s2;
    lp.inputDeviceIs(&s2);
    CHECK_EQ(lp.processSignal(256, buf, &processed), 256);
    SineSource r2;
    same = 1;
    for (int i = 0; i < 256; i++) if (buf[i] != r2.next()) same = 0;
    CHECK(same);

    EqProbe off;
    off.setFreq(1000.0f);
    off.setGain(12.0f);
    off.setEnableFlag(0);
    SineSource s3;
    off.inputDeviceIs(&s3);
    CHECK_EQ(off.processSignal(256, buf, &processed), 256);
    SineSource r3;
    same = 1;
    for (int i = 0; i < 256; i++) if (buf[i] != r3.next()) same = 0;
    CHECK(same);

    // stereo forced low pass with an output level: both channels scaled
    EqProbe lv;
    lv.setMode(EQ_MODE_LOWPASS);
    lv.setFreq(5000.0f);
    lv.setGain(-6.0f);
    CHECK_CLOSE(sineGain(lv, 100.0, 1), pow(10.0, -6.0 / 20.0), 0.01);
}

/** @brief Silent input: the filter runs on 1e-6 for 200 ms (flag cleared), then leaves the buffer alone. */
TEST(eq, silence_tail)
{
    EqProbe e;
    e.setFreq(1000.0f);
    e.setGain(6.0f);
    SineSource src;
    e.inputDeviceIs(&src);
    float buf[512];
    int processed = 0;
    e.processSignalW(256, buf, &processed);
    src.silent = 1;
    // limit 8820: entry counts 0, 256, ..., 8704 render (35 blocks), 8960 stops
    int rendered = 0;
    for (int blk = 0; blk < 40; blk++) {
        for (int i = 0; i < 512; i++) buf[i] = 5.0f;
        processed = 0;
        e.processSignalW(256, buf, &processed);
        if (!processed) {
            rendered++;
            CHECK(buf[0] != 5.0f);
        } else {
            CHECK(buf[0] == 5.0f);
        }
    }
    CHECK_EQ(rendered, 35);
    // from rest, the silent input is rendered as a DC of 1e-6 through the filter
    EqProbe f;
    f.setFreq(1000.0f);
    f.setGain(6.0f);
    f.inputDeviceIs(&src);
    for (int blk = 0; blk < 34; blk++) f.processSignal(256, buf, &processed);
    CHECK_EQ(processed, 0);
    CHECK_CLOSE(buf[255], 1e-6, 1e-9);   // the peaking band has unity gain at DC
}

/** @brief Characterization: peaking coefficients and the first output samples of an impulse. */
TEST(eq, characterization_peaking_impulse)
{
    EqProbe e;
    e.setFreq(2000.0f);
    e.setQ(1.5f);
    e.setGain(10.0f);
    float b[8] = {1, 0, 0, 0, 0, 0, 0, 0};
    e.processSignalForced(8, b, NULL);
    // reference values from the current (release-matching) build
    const double expect[8] = {1.21143007, 0.321940184, 0.22705853, 0.131914765,
                              0.0435427576, -0.0327379555, -0.0934163332, -0.136734843};
    for (int i = 0; i < 8; i++) CHECK_CLOSE(b[i], expect[i], 1e-6);
}
