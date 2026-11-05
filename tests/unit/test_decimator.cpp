/**
 * @file test_decimator.cpp
 * @brief Unit tests for CDecimator (techpop/decimator.cpp, declared in efxs.h): bit reduction
 *        (16.16 fixed point, low bits cleared), sample-and-hold rate reduction, the dry/wet mix,
 *        stereo and the silence flag.
 */
#include "test.h"

#include <math.h>
#include <string.h>

#include "efxs.h"

namespace {

/**
 * @brief Makes a decimator with all parameters set.
 */
void initDec(CDecimator& d, int sample, int bit, float level)
{
    d.setSample(sample);
    d.setBit(bit);
    d.setLevel(level);
}

}  // namespace

/** @brief Bit reduction: the value is scaled by 65536, rounded, and the low bit+1 bits are cleared (rounding towards -inf). */
TEST(decimator, bit_reduction_values)
{
    CDecimator d;
    initDec(d, 0, 0, 1.0f);
    float b[4] = {0.3f, -0.3f, 0.5f, 1.0f / 65536};
    int processed = 0;
    CHECK_EQ(d.processSignal(4, b, &processed), 4);
    CHECK_CLOSE(b[0], 19660 / 65536.0, 1e-9);    // round(19660.8) = 19661, & ~1
    CHECK_CLOSE(b[1], -19662 / 65536.0, 1e-9);   // -19661 & ~1
    CHECK_CLOSE(b[2], 0.5, 0);
    CHECK_CLOSE(b[3], 0.0, 0);

    CDecimator e;
    initDec(e, 0, 15, 1.0f);   // keep only the integer part (16 bits cleared)
    float c[5] = {0.3f, 0.99f, 1.6f, -0.3f, -1.0f};
    e.processSignal(5, c, &processed);
    CHECK_CLOSE(c[0], 0.0, 0);
    CHECK_CLOSE(c[1], 0.0, 0);
    CHECK_CLOSE(c[2], 1.0, 0);
    CHECK_CLOSE(c[3], -1.0, 0);
    CHECK_CLOSE(c[4], -1.0, 0);
}

/** @brief More dropped bits give a coarser staircase: the number of distinct output levels on a ramp shrinks. */
TEST(decimator, bit_depth_staircase)
{
    int prevLevels = 1 << 30;
    for (int bit = 7; bit <= 15; bit += 4) {
        CDecimator d;
        initDec(d, 0, bit, 1.0f);
        float b[1000];
        for (int i = 0; i < 1000; i++) b[i] = i / 1000.0f;
        int processed = 0;
        d.processSignal(1000, b, &processed);
        int levels = 1;
        double maxErr = 0;
        for (int i = 1; i < 1000; i++) if (b[i] != b[i - 1]) levels++;
        for (int i = 0; i < 1000; i++) {
            double err = i / 1000.0 - b[i];
            maxErr = err > maxErr ? err : maxErr;
            CHECK(err >= -0.5 / 65536);   // the output never exceeds the (rounded) input
        }
        CHECK(levels < prevLevels);
        CHECK(maxErr < (double)(1 << (bit + 1)) / 65536 + 1.0 / 65536);
        CHECK_EQ(levels, bit == 15 ? 1 : (1 << (15 - bit)));
        prevLevels = levels;
    }
}

/** @brief Rate reduction: each captured sample is held for sample + 1 samples, also across blocks. */
TEST(decimator, sample_hold)
{
    CDecimator d;
    initDec(d, 3, 0, 1.0f);
    float b[16];
    for (int i = 0; i < 16; i++) b[i] = (float)(i + 1) / 64.0f;
    int processed = 0;
    d.processSignal(6, b, &processed);
    d.processSignal(10, b + 6, &processed);
    for (int i = 0; i < 16; i++) {
        int src = i / 4 * 4;
        CHECK_CLOSE(b[i], (src + 1) / 64.0, 1e-9);
    }
}

/** @brief The level mixes the held/quantised signal with the dry one: out = in (1 - level) + held * level. */
TEST(decimator, dry_wet_mix)
{
    CDecimator d;
    initDec(d, 1, 15, 0.25f);
    float b[4] = {0.6f, 0.2f, 1.4f, 0.8f};
    int processed = 0;
    d.processSignal(4, b, &processed);
    CHECK_CLOSE(b[0], 0.6 * 0.75 + 0.0 * 0.25, 1e-7);
    CHECK_CLOSE(b[1], 0.2 * 0.75 + 0.0 * 0.25, 1e-7);
    CHECK_CLOSE(b[2], 1.4 * 0.75 + 1.0 * 0.25, 1e-6);
    CHECK_CLOSE(b[3], 0.8 * 0.75 + 1.0 * 0.25, 1e-6);

    CDecimator z;
    initDec(z, 5, 9, 0.0f);
    float c[3] = {0.123f, -0.456f, 0.789f};
    z.processSignal(3, c, &processed);
    CHECK(c[0] == 0.123f && c[1] == -0.456f && c[2] == 0.789f);
}

/** @brief Silence in gives silence out, and a block flagged silent is not touched (the counter does not advance). */
TEST(decimator, silence)
{
    CDecimator d;
    initDec(d, 2, 4, 1.0f);
    float z[64];
    memset(z, 0, sizeof z);
    int processed = 0;
    d.processSignal(64, z, &processed);
    int zero = 1;
    for (int i = 0; i < 64; i++) if (z[i] != 0.0f) zero = 0;
    CHECK(zero);

    CDecimator e;
    initDec(e, 1, 0, 1.0f);
    float a[2] = {0.5f, 0.25f};
    e.processSignal(1, a, &processed);   // count 0: captures 0.5
    processed = 1;
    float s[4] = {7, 7, 7, 7};
    e.processSignal(2, s, &processed);
    e.processSignalW(2, s, &processed);
    CHECK(s[0] == 7 && s[3] == 7);
    processed = 0;
    e.processSignal(1, a + 1, &processed);   // count 1: still holding 0.5
    CHECK_CLOSE(a[1], 0.5, 0);
}

/** @brief Stereo: both channels are captured at the same time and processed like the mono path. */
TEST(decimator, stereo_matches_mono)
{
    CDecimator st, ml, mr;
    initDec(st, 2, 6, 0.7f);
    initDec(ml, 2, 6, 0.7f);
    initDec(mr, 2, 6, 0.7f);
    float s[2 * 100], l[100], r[100];
    for (int i = 0; i < 100; i++) {
        l[i] = s[i * 2] = (float)sin(i * 0.11) * 0.9f;
        r[i] = s[i * 2 + 1] = (float)cos(i * 0.05) * 0.4f;
    }
    int processed = 0;
    st.processSignalW(100, s, &processed);
    ml.processSignal(100, l, &processed);
    mr.processSignal(100, r, &processed);
    int ok = 1;
    for (int i = 0; i < 100; i++) {
        if (fabs(s[i * 2] - l[i]) > 1e-7 || fabs(s[i * 2 + 1] - r[i]) > 1e-7) ok = 0;
    }
    CHECK(ok);
}
