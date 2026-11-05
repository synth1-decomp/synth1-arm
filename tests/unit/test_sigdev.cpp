/**
 * @file test_sigdev.cpp
 * @brief Unit tests for techpop/sigdev.h (CSignalDevice base class, SSE array helpers,
 *        killDenormal) and techpop/ssechk.h (CSSEChecker).
 */
#include "test.h"

#include <float.h>
#include <string.h>

#include "common.h"
#include "ssechk.h"
#include "sigdev.h"

extern CSSEChecker g_sseChecker;

namespace {

/**
 * @brief Test device: fills the buffer with start, start + step, ... and counts the calls.
 */
class RampDevice : public CSignalDevice {
public:
    RampDevice() : start(1.f), step(1.f), silent(0), produce(-1), calls(0), freqChanges(0) {}

    virtual int processSignal(int size, float* pBuf, int* pProcessed)
    {
        calls++;
        int n = produce >= 0 && produce < size ? produce : size;
        if (silent) {
            *pProcessed = 1;
            return n;
        }
        for (int i = 0; i < n; i++) pBuf[i] = start + step * i;
        *pProcessed = 0;
        return n;
    }

    virtual void onChangeSamplingFreq() { freqChanges++; }

    float start;
    float step;
    int silent;
    int produce;
    int calls;
    int freqChanges;
};

/**
 * @brief Device that keeps the base onChangeSamplingFreq() and pulls its input through m_pInput.
 */
class GainDevice : public CSignalDevice {
public:
    GainDevice() : gain(2.f) {}

    virtual int processSignal(int size, float* pBuf, int* pProcessed)
    {
        int n = m_pInput->processSignal(size, pBuf, pProcessed);
        if (!*pProcessed) mulArray2SSE(pBuf, gain, n);
        return n;
    }

    float gain;
};

/**
 * @brief Restores CSignalDevice::m_bSSE when it goes out of scope.
 */
struct SSEGuard {
    int saved;
    SSEGuard() : saved(CSignalDevice::m_bSSE) {}
    ~SSEGuard() { CSignalDevice::m_bSSE = saved; }
};

float g_bufA[1024] __attribute__((aligned(16)));
float g_bufB[1024] __attribute__((aligned(16)));
float g_bufC[1024] __attribute__((aligned(16)));

float valA(int i) { return 0.5f * i - 3.25f; }
float valB(int i) { return 1.0f / (i + 1) + 0.125f * (i % 5); }

}  // namespace

/**
 * @brief CSSEChecker always reports SSE support, and so does the global checker.
 */
TEST(ssechk, always_reports_sse)
{
    CSSEChecker c;
    CHECK_EQ(c.m_bSSE, 1);
    CHECK_EQ(c.checkSSE(), 1);
    CHECK_EQ(g_sseChecker.m_bSSE, 1);
}

/**
 * @brief CheckSSEFunc() and checkSSEFunc() copy the global check result into m_bSSE, and a new
 *        device does too.
 */
TEST(sigdev, sse_flag_follows_global_check)
{
    SSEGuard guard;
    CSignalDevice::m_bSSE = 0;
    CheckSSEFunc();
    CHECK_EQ(CSignalDevice::m_bSSE, 1);
    CSignalDevice::m_bSSE = 0;
    RampDevice d;
    CHECK_EQ(CSignalDevice::m_bSSE, 1);
    CSignalDevice::m_bSSE = 0;
    d.checkSSEFunc();
    CHECK_EQ(CSignalDevice::m_bSSE, 1);
}

/**
 * @brief A new device has no input, runs at 44100 Hz and has a centered pan.
 */
TEST(sigdev, constructor_defaults)
{
    GainDevice d;
    CHECK(d.m_pInput == NULL);
    CHECK_EQ(d.m_samplingFreq, 44100);
    CHECK_EQ(d.getPanVal(), 0);
    d.onChangeSamplingFreq();  // base version: no effect
    CHECK_EQ(d.m_samplingFreq, 44100);
}

/**
 * @brief setSamplingFreq() stores the rate and calls onChangeSamplingFreq() once per call.
 */
TEST(sigdev, set_sampling_freq_notifies)
{
    RampDevice d;
    d.setSamplingFreq(48000);
    CHECK_EQ(d.m_samplingFreq, 48000);
    CHECK_EQ(d.freqChanges, 1);
    d.setSamplingFreq(48000);
    CHECK_EQ(d.freqChanges, 2);
    d.setSamplingFreq(96000);
    CHECK_EQ(d.m_samplingFreq, 96000);
    CHECK_EQ(d.freqChanges, 3);

    GainDevice g;  // base onChangeSamplingFreq()
    g.setSamplingFreq(22050);
    CHECK_EQ(g.m_samplingFreq, 22050);
}

/**
 * @brief The sampling rate is per device: setting it on one device does not propagate to its input.
 */
TEST(sigdev, sampling_freq_is_per_device)
{
    RampDevice src;
    GainDevice g;
    g.inputDeviceIs(&src);
    g.setSamplingFreq(32000);
    CHECK_EQ(src.m_samplingFreq, 44100);
    CHECK_EQ(src.freqChanges, 0);
}

/**
 * @brief inputDeviceIs() chains devices: the downstream device pulls and transforms its input;
 *        NULL disconnects.
 */
TEST(sigdev, input_chaining)
{
    RampDevice src;
    GainDevice g;
    g.inputDeviceIs(&src);
    CHECK(g.m_pInput == &src);
    float buf[8];
    int processed = -1;
    int n = g.processSignal(8, buf, &processed);
    CHECK_EQ(n, 8);
    CHECK_EQ(processed, 0);
    for (int i = 0; i < 8; i++) CHECK_CLOSE(buf[i], 2.f * (1.f + i), 0);

    GainDevice g2;  // two stages
    g2.gain = 0.5f;
    g2.inputDeviceIs(&g);
    n = g2.processSignal(8, buf, &processed);
    CHECK_EQ(n, 8);
    for (int i = 0; i < 8; i++) CHECK_CLOSE(buf[i], 1.f + i, 0);
    CHECK_EQ(src.calls, 2);

    g.inputDeviceIs(NULL);
    CHECK(g.m_pInput == NULL);
}

/**
 * @brief processSignalForced() defaults to processSignal().
 */
TEST(sigdev, process_forced_defaults_to_process)
{
    RampDevice d;
    float buf[4];
    int processed = -1;
    CHECK_EQ(d.processSignalForced(4, buf, &processed), 4);
    CHECK_EQ(d.calls, 1);
    CHECK_EQ(processed, 0);
    CHECK_CLOSE(buf[3], 4.f, 0);
}

/**
 * @brief processSignalW() expands the mono output in place to interleaved stereo.
 */
TEST(sigdev, process_w_expands_to_stereo)
{
    RampDevice d;
    float buf[16];
    for (int i = 0; i < 16; i++) buf[i] = -99.f;
    int processed = -1;
    CHECK_EQ(d.processSignalW(8, buf, &processed), 8);
    CHECK_EQ(processed, 0);
    for (int i = 0; i < 8; i++) {
        CHECK_CLOSE(buf[2 * i], 1.f + i, 0);
        CHECK_CLOSE(buf[2 * i + 1], 1.f + i, 0);
    }
}

/**
 * @brief processSignalW() leaves the buffer alone when the device reports silence.
 */
TEST(sigdev, process_w_silent_leaves_buffer)
{
    RampDevice d;
    d.silent = 1;
    float buf[8];
    for (int i = 0; i < 8; i++) buf[i] = -7.f;
    int processed = 0;
    CHECK_EQ(d.processSignalW(4, buf, &processed), 4);
    CHECK_EQ(processed, 1);
    for (int i = 0; i < 8; i++) CHECK_CLOSE(buf[i], -7.f, 0);
}

/**
 * @brief processSignalW() expands only the n samples actually produced.
 */
TEST(sigdev, process_w_expands_only_produced_samples)
{
    RampDevice d;
    d.produce = 3;
    float buf[16];
    for (int i = 0; i < 16; i++) buf[i] = -1.f;
    int processed = -1;
    CHECK_EQ(d.processSignalW(8, buf, &processed), 3);
    for (int i = 0; i < 3; i++) {
        CHECK_CLOSE(buf[2 * i], 1.f + i, 0);
        CHECK_CLOSE(buf[2 * i + 1], 1.f + i, 0);
    }
    for (int i = 6; i < 16; i++) CHECK_CLOSE(buf[i], -1.f, 0);
}

/**
 * @brief processSignalWForced() defaults to processSignalW().
 */
TEST(sigdev, process_w_forced_defaults_to_process_w)
{
    RampDevice d;
    d.start = 10.f;
    d.step = 0.f;
    float buf[8];
    int processed = -1;
    CHECK_EQ(d.processSignalWForced(4, buf, &processed), 4);
    CHECK_EQ(d.calls, 1);
    for (int i = 0; i < 8; i++) CHECK_CLOSE(buf[i], 10.f, 0);
}

/**
 * @brief mono2Stereo() duplicates each sample into L and R, in place.
 */
TEST(sigdev, mono2stereo)
{
    float buf[10] = {1, 2, 3, 4, 5, 0, 0, 0, 0, 0};
    CSignalDevice::mono2Stereo(buf, 5);
    float expect[10] = {1, 1, 2, 2, 3, 3, 4, 4, 5, 5};
    for (int i = 0; i < 10; i++) CHECK_CLOSE(buf[i], expect[i], 0);
    float one[2] = {7, -1};
    CSignalDevice::mono2Stereo(one, 1);
    CHECK_CLOSE(one[0], 7, 0);
    CHECK_CLOSE(one[1], 7, 0);
    CSignalDevice::mono2Stereo(one, 0);  // no-op
    CHECK_CLOSE(one[0], 7, 0);
}

/**
 * @brief mulArray2SSE() scales in place, for every alignment and length, with and without SSE.
 */
TEST(sigdev, mul_array2_all_alignments)
{
    SSEGuard guard;
    for (int sse = 0; sse <= 1; sse++) {
        CSignalDevice::m_bSSE = sse;
        for (int off = 0; off < 4; off++) {
            for (int size = 0; size <= 19; size++) {
                float* p = g_bufA + off;
                for (int i = 0; i < size + 2; i++) p[i] = valA(i);
                CSignalDevice::mulArray2SSE(p, -1.5f, size);
                int bad = 0;
                for (int i = 0; i < size; i++) if (p[i] != valA(i) * -1.5f) bad++;
                for (int i = size; i < size + 2; i++) if (p[i] != valA(i)) bad++;
                CHECK_EQ(bad, 0);
            }
        }
    }
}

/**
 * @brief addArraySSE() adds element-wise for all relative alignments, with and without SSE.
 */
TEST(sigdev, add_array_all_alignments)
{
    SSEGuard guard;
    for (int sse = 0; sse <= 1; sse++) {
        CSignalDevice::m_bSSE = sse;
        for (int od = 0; od < 4; od++) {
            for (int os = 0; os < 4; os++) {
                for (int size = 0; size <= 21; size += 3) {
                    float* d = g_bufA + od;
                    float* s = g_bufB + os;
                    for (int i = 0; i < size + 2; i++) { d[i] = valA(i); s[i] = valB(i); }
                    CSignalDevice::addArraySSE(d, s, size);
                    int bad = 0;
                    for (int i = 0; i < size; i++) if (d[i] != valA(i) + valB(i)) bad++;
                    for (int i = size; i < size + 2; i++) if (d[i] != valA(i)) bad++;
                    CHECK_EQ(bad, 0);
                }
            }
        }
    }
}

/**
 * @brief addArraySSE() with overlapping arrays (d a few floats after s) gives the sequential
 *        scalar result: the SSE path is skipped when d - s is in 1..15 bytes.
 */
TEST(sigdev, add_array_overlap_is_sequential)
{
    SSEGuard guard;
    CSignalDevice::m_bSSE = 1;
    for (int shift = 1; shift <= 3; shift++) {
        float ref[40];
        for (int i = 0; i < 40; i++) ref[i] = g_bufA[i] = valA(i);
        CSignalDevice::addArraySSE(g_bufA + shift, g_bufA, 32);
        for (int i = 0; i < 32; i++) ref[i + shift] += ref[i];
        int bad = 0;
        for (int i = 0; i < 40; i++) if (g_bufA[i] != ref[i]) bad++;
        CHECK_EQ(bad, 0);
    }
}

/**
 * @brief mulArraySSE() computes d = s * v for all relative alignments, and v == 0 clears d.
 */
TEST(sigdev, mul_array_all_alignments)
{
    SSEGuard guard;
    for (int sse = 0; sse <= 1; sse++) {
        CSignalDevice::m_bSSE = sse;
        for (int od = 0; od < 4; od++) {
            for (int os = 0; os < 4; os++) {
                for (int size = 1; size <= 22; size += 3) {
                    float* d = g_bufA + od;
                    float* s = g_bufB + os;
                    for (int i = 0; i < size + 2; i++) { d[i] = -5.f; s[i] = valB(i); }
                    CSignalDevice::mulArraySSE(d, s, 3.f, size);
                    int bad = 0;
                    for (int i = 0; i < size; i++) if (d[i] != valB(i) * 3.f) bad++;
                    for (int i = size; i < size + 2; i++) if (d[i] != -5.f) bad++;
                    CHECK_EQ(bad, 0);
                }
            }
        }
    }
    for (int i = 0; i < 12; i++) g_bufA[i] = 9.f;
    CSignalDevice::mulArraySSE(g_bufA, g_bufB, 0.f, 10);
    int bad = 0;
    for (int i = 0; i < 10; i++) if (g_bufA[i] != 0.f) bad++;
    CHECK_EQ(bad, 0);
    CHECK_CLOSE(g_bufA[10], 9.f, 0);
}

/**
 * @brief mulArraySSE() works in place (d == s).
 */
TEST(sigdev, mul_array_in_place)
{
    for (int i = 0; i < 33; i++) g_bufA[i] = valA(i);
    CSignalDevice::mulArraySSE(g_bufA + 1, g_bufA + 1, 0.25f, 32);
    int bad = 0;
    for (int i = 1; i < 33; i++) if (g_bufA[i] != valA(i) * 0.25f) bad++;
    CHECK_EQ(bad, 0);
}

/**
 * @brief addMulArraySSE() computes d = s1 + s2 * v for every combination of alignments
 *        (all branches of the SSE path), and v == 0 copies s1.
 */
TEST(sigdev, add_mul_array_all_alignments)
{
    SSEGuard guard;
    for (int sse = 0; sse <= 1; sse++) {
        CSignalDevice::m_bSSE = sse;
        for (int od = 0; od < 4; od++) {
            for (int o1 = 0; o1 < 4; o1++) {
                for (int o2 = 0; o2 < 4; o2++) {
                    for (int size = 1; size <= 23; size += 2) {
                        float* d = g_bufA + od;
                        float* s1 = g_bufB + o1;
                        float* s2 = g_bufC + o2;
                        for (int i = 0; i < size + 2; i++) {
                            d[i] = -5.f; s1[i] = valA(i); s2[i] = valB(i);
                        }
                        CSignalDevice::addMulArraySSE(d, s1, s2, 0.75f, size);
                        int bad = 0;
                        for (int i = 0; i < size; i++) {
                            if (fabsf(d[i] - (valA(i) + valB(i) * 0.75f)) > 1e-6f) bad++;
                        }
                        for (int i = size; i < size + 2; i++) if (d[i] != -5.f) bad++;
                        CHECK_EQ(bad, 0);
                    }
                }
            }
        }
    }
    for (int i = 0; i < 10; i++) { g_bufA[i] = 0.f; g_bufB[i] = valA(i); }
    CSignalDevice::addMulArraySSE(g_bufA, g_bufB, g_bufC, 0.f, 10);
    int bad = 0;
    for (int i = 0; i < 10; i++) if (g_bufA[i] != valA(i)) bad++;
    CHECK_EQ(bad, 0);
}

/**
 * @brief addMulArraySSE() accumulates in place when d aliases s1 (d = d + s2 * v).
 */
TEST(sigdev, add_mul_array_accumulates_in_place)
{
    for (int i = 0; i < 40; i++) { g_bufA[i] = valA(i); g_bufB[i] = valB(i); }
    CSignalDevice::addMulArraySSE(g_bufA + 2, g_bufA + 2, g_bufB + 1, 2.f, 37);
    int bad = 0;
    for (int i = 2; i < 39; i++) {
        if (fabsf(g_bufA[i] - (valA(i) + valB(i - 1) * 2.f)) > 1e-6f) bad++;
    }
    CHECK_EQ(bad, 0);
}

/**
 * @brief killDenormal() flushes denormals (of either sign) to zero and leaves normal values,
 *        zero, infinities alone.
 */
TEST(sigdev, kill_denormal)
{
    float v = FLT_MIN / 2.f;
    killDenormal(v);
    CHECK(v == 0.f);
    v = -FLT_MIN / 1024.f;
    killDenormal(v);
    CHECK(v == 0.f);
    union { float f; unsigned int i; } u;
    u.i = 1;  // smallest denormal
    v = u.f;
    killDenormal(v);
    CHECK(v == 0.f);
    u.i = 0x007fffff;  // largest denormal
    v = u.f;
    killDenormal(v);
    CHECK(v == 0.f);

    float keep[] = {FLT_MIN, -FLT_MIN, 1.f, -0.5f, 1e-30f, 0.f, -0.f, HUGE_VALF, FLT_MAX};
    for (unsigned k = 0; k < sizeof keep / sizeof keep[0]; k++) {
        float w = keep[k];
        killDenormal(w);
        CHECK(memcmp(&w, &keep[k], sizeof w) == 0);
    }
}
