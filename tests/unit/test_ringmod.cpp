/**
 * @file test_ringmod.cpp
 * @brief Unit tests for CRingMod (techpop/ringmod.cpp, declared in efxs.h): the oscillator
 *        frequency and amplitude, the dry/wet crossfade, stereo and the silence flag.
 */
#include "test.h"

#include <math.h>
#include <string.h>

#include "efxs.h"

namespace {

const double kPi = 3.14159265358979323846;

/**
 * @brief Gives the tests access to the protected CRingMod state.
 */
class RingProbe : public CRingMod {
public:
    float x() const { return m_x; }
    float y() const { return m_y; }
    float coef() const { return m_coef; }
    float freq() const { return m_freq; }
    float level() const { return m_level; }
    void setState(float x, float y) { m_x = x; m_y = y; }
};

}  // namespace

/** @brief Defaults: 1000 Hz, level 0.5, oscillator at phase 0 on the unit circle; coef = 2 sin(pi f / fs). */
TEST(ringmod, defaults_and_coefficient)
{
    RingProbe r;
    CHECK_CLOSE(r.freq(), 1000.0, 0);
    CHECK_CLOSE(r.level(), 0.5, 0);
    CHECK_CLOSE(r.x(), 0.0, 0);
    CHECK_CLOSE(r.y(), 1.0, 1e-7);
    CHECK_CLOSE(r.coef(), 2 * sin(kPi * 1000.0 / 44100), 1e-7);
    r.setSamplingFreq(22050);
    CHECK_CLOSE(r.coef(), 2 * sin(kPi * 1000.0 / 22050), 1e-7);
}

/** @brief Level 0 leaves the signal untouched (the oscillator still runs). */
TEST(ringmod, level_zero_is_dry)
{
    RingProbe r;
    r.setLevel(0.0f);
    float b[128], in[128];
    for (int i = 0; i < 128; i++) in[i] = b[i] = (float)sin(i * 0.1);
    int processed = 0;
    CHECK_EQ(r.processSignal(128, b, &processed), 128);
    CHECK(memcmp(b, in, sizeof b) == 0);
    CHECK(r.x() != 0.0f);
}

/** @brief Level 1 on a DC input outputs the carrier: amplitude 1 and the set frequency (zero crossings). */
TEST(ringmod, carrier_frequency_and_amplitude)
{
    RingProbe r;
    r.setFreq(441.0f);
    r.setLevel(1.0f);
    static float b[44100];
    for (int i = 0; i < 44100; i++) b[i] = 1.0f;
    int processed = 0;
    for (int off = 0; off < 44100; off += 441) r.processSignal(441, b + off, &processed);
    int crossings = 0;
    double peak = 0;
    for (int i = 1; i < 44100; i++) {
        if ((b[i - 1] < 0) != (b[i] < 0)) crossings++;
        peak = fabs(b[i]) > peak ? fabs(b[i]) : peak;
    }
    CHECK(crossings >= 880 && crossings <= 884);   // 441 cycles in 1 s
    CHECK_CLOSE(peak, 1.0, 0.01);
    // the first sample: x = -coef * y with y = 1 (sin(-w), the oscillator runs "backwards")
    RingProbe s;
    s.setFreq(441.0f);
    s.setLevel(1.0f);
    float one = 1.0f;
    s.processSignal(1, &one, &processed);
    CHECK_CLOSE(one, -2 * sin(kPi * 441.0 / 44100), 1e-7);
}

/** @brief Level crossfades: gain = osc * level + (1 - level), so it stays within 1 - 2 level .. 1. */
TEST(ringmod, level_crossfade_range)
{
    RingProbe r;
    r.setLevel(0.25f);
    r.setFreq(1000.0f);
    float b[4410];
    for (int i = 0; i < 4410; i++) b[i] = 1.0f;
    int processed = 0;
    r.processSignal(4410, b, &processed);
    double lo = 9, hi = -9;
    for (int i = 0; i < 4410; i++) {
        lo = b[i] < lo ? b[i] : lo;
        hi = b[i] > hi ? b[i] : hi;
    }
    CHECK_CLOSE(lo, 0.5, 0.01);
    CHECK_CLOSE(hi, 1.0, 0.01);
}

/** @brief Stereo applies the same gain to both channels; a silent block is untouched and does not advance the oscillator. */
TEST(ringmod, stereo_and_silence)
{
    RingProbe r, m;
    r.setLevel(0.8f);
    m.setLevel(0.8f);
    float st[2 * 300], mo[300];
    for (int i = 0; i < 300; i++) {
        mo[i] = (float)cos(i * 0.07);
        st[i * 2] = mo[i];
        st[i * 2 + 1] = -2.0f * mo[i];
    }
    int processed = 0;
    r.processSignalW(300, st, &processed);
    m.processSignal(300, mo, &processed);
    int ok = 1;
    for (int i = 0; i < 300; i++) {
        if (st[i * 2] != mo[i]) ok = 0;
        if (fabs(st[i * 2 + 1] + 2.0f * mo[i]) > 1e-6) ok = 0;
    }
    CHECK(ok);

    float x0 = r.x();
    float b[8] = {1, 1, 1, 1, 1, 1, 1, 1};
    processed = 1;
    r.processSignal(4, b, &processed);
    r.processSignalW(2, b, &processed);
    CHECK(r.x() == x0);
    CHECK(b[0] == 1.0f && b[7] == 1.0f);
}

/** @brief setFreq() puts the oscillator back on the unit circle, keeping the phase (sign of y). */
TEST(ringmod, setfreq_renormalizes)
{
    RingProbe r;
    r.setLevel(1.0f);
    float b[1000];
    for (int i = 0; i < 1000; i++) b[i] = 1.0f;
    int processed = 0;
    r.processSignal(1000, b, &processed);
    float x = r.x(), y = r.y();
    r.setFreq(2000.0f);
    CHECK_CLOSE(r.x(), x, 0);
    CHECK_CLOSE(r.x() * r.x() + r.y() * r.y(), 1.0, 1e-5);
    CHECK((r.y() >= 0) == (y >= 0));
    CHECK_CLOSE(r.coef(), 2 * sin(kPi * 2000.0 / 44100), 1e-7);
}

/** @brief setFreq() clamps an oscillator x outside -1..1 to the unit circle (y becomes 0 with its sign kept). */
TEST(ringmod, setfreq_clamps_x)
{
    RingProbe r;
    r.setState(1.5f, 0.25f);
    r.setFreq(500.0f);
    CHECK_FEQ(r.x(), 1.0f);
    CHECK_CLOSE(r.y(), 0.0, 1e-7);
    CHECK(r.y() >= 0.0f);
    r.setState(-1.25f, -0.5f);
    r.setFreq(500.0f);
    CHECK_FEQ(r.x(), -1.0f);
    CHECK_CLOSE(r.y(), 0.0, 1e-7);
    CHECK(r.y() <= 0.0f);
    // the oscillator keeps running from the clamped point
    r.setLevel(1.0f);
    float b[64];
    for (int i = 0; i < 64; i++) b[i] = 1.0f;
    int processed = 0;
    r.processSignal(64, b, &processed);
    for (int i = 0; i < 64; i++) CHECK(fabs(b[i]) <= 1.01f);
}
