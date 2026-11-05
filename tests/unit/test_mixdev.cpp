/**
 * @file test_mixdev.cpp
 * @brief Unit tests for techpop/mixdev.h: CMixDevice mono summing (processSignal), panned stereo
 *        mixing (processSignalW) and the silence flags.
 */
#include "test.h"

#include <string.h>

#include "common.h"
#include "mixdev.h"

namespace {

/**
 * @brief Test source: constant value or ramp, optional silence, optional short blocks, pan.
 */
class Source : public CSignalDevice {
public:
    explicit Source(float v = 1.f)
        : value(v), step(0.f), silent(0), chunk(0), pan(0.f), calls(0), produced(0) {}

    virtual int processSignal(int size, float* pBuf, int* pProcessed)
    {
        calls++;
        int n = (chunk > 0 && chunk < size) ? chunk : size;
        if (silent) {
            *pProcessed = 1;
        } else {
            for (int i = 0; i < n; i++) pBuf[i] = value + step * (produced + i);
            *pProcessed = 0;
        }
        produced += n;
        return n;
    }

    virtual float getPanVal() { return pan; }

    float value;
    float step;
    int silent;
    int chunk;
    float pan;
    int calls;
    int produced;
};

struct SSEGuard {
    int saved;
    SSEGuard() : saved(CSignalDevice::m_bSSE) {}
    ~SSEGuard() { CSignalDevice::m_bSSE = saved; }
};

float g_out[1024] __attribute__((aligned(16)));

void fillOut(float v)
{
    for (int i = 0; i < 1024; i++) g_out[i] = v;
}

}  // namespace

/**
 * @brief A new mixer has an aligned work buffer, no inputs and zero channel/voice counts.
 */
TEST(mixdev, constructor_defaults)
{
    CMixDevice m;
    REQUIRE(m.m_pWork != NULL);
    CHECK_EQ((long)m.m_pWork & 15, 0);
    for (int i = 0; i < MIXDEV_MAX_INPUT; i++) CHECK(m.m_pInputs[i] == NULL);
    CHECK_EQ(m.m_channelNum, 0);
    CHECK_EQ(m.m_enableVoiceNum, 0);
    CHECK_EQ(m.m_samplingFreq, 44100);
}

/**
 * @brief The setters store the channel count, voice count and input pointers.
 */
TEST(mixdev, setters)
{
    CMixDevice m;
    Source a;
    m.setChannelNum(5);
    m.setEnableVoiceNum(3);
    m.inputMixDeviceIs(31, &a);
    CHECK_EQ(m.m_channelNum, 5);
    CHECK_EQ(m.m_enableVoiceNum, 3);
    CHECK(m.m_pInputs[31] == &a);
    m.inputMixDeviceIs(31, NULL);
    CHECK(m.m_pInputs[31] == NULL);
}

/**
 * @brief processSignal() with no connected input reports silence and returns size unchanged.
 */
TEST(mixdev, process_no_inputs_is_silent)
{
    CMixDevice m;
    m.setChannelNum(4);
    fillOut(-3.f);
    int processed = 0;
    CHECK_EQ(m.processSignal(300, g_out, &processed), 300);
    CHECK_EQ(processed, 1);
    CHECK_CLOSE(g_out[0], -3.f, 0);
}

/**
 * @brief processSignal() sums all connected inputs, skipping empty slots, with and without SSE
 *        and for aligned and unaligned output buffers.
 */
TEST(mixdev, process_sums_inputs)
{
    SSEGuard guard;
    for (int sse = 0; sse <= 1; sse++) {
        for (int off = 0; off < 4; off++) {
            CSignalDevice::m_bSSE = sse;
            CMixDevice m;
            Source a(1.f), b(10.f), c(100.f);
            a.step = 0.5f;
            c.step = -1.f;
            m.setChannelNum(5);
            m.inputMixDeviceIs(1, &a);
            m.inputMixDeviceIs(2, &b);
            m.inputMixDeviceIs(4, &c);
            m.inputMixDeviceIs(5, &b);  // beyond m_channelNum: ignored
            fillOut(-1.f);
            float* out = g_out + off;
            int processed = -1;
            CHECK_EQ(m.processSignal(64, out, &processed), 64);
            CHECK_EQ(processed, 0);
            int bad = 0;
            for (int i = 0; i < 64; i++) {
                float expect = (1.f + 0.5f * i) + 10.f + (100.f - i);
                if (fabsf(out[i] - expect) > 1e-4f) bad++;
            }
            CHECK_EQ(bad, 0);
            CHECK_CLOSE(out[64], -1.f, 0);
            CHECK_EQ(b.calls, 1);
        }
    }
}

/**
 * @brief processSignal() renders at most 256 samples per call.
 */
TEST(mixdev, process_caps_block_at_256)
{
    CMixDevice m;
    Source a(2.f), b(3.f);
    m.setChannelNum(2);
    m.inputMixDeviceIs(0, &a);
    m.inputMixDeviceIs(1, &b);
    fillOut(-1.f);
    int processed = -1;
    CHECK_EQ(m.processSignal(1000, g_out, &processed), 256);
    CHECK_EQ(processed, 0);
    CHECK_EQ(a.produced, 256);
    CHECK_EQ(b.produced, 256);
    CHECK_CLOSE(g_out[255], 5.f, 0);
    CHECK_CLOSE(g_out[256], -1.f, 0);
}

/**
 * @brief Inputs that return short blocks are called repeatedly until the block is full.
 */
TEST(mixdev, process_repeats_short_inputs)
{
    SSEGuard guard;
    for (int sse = 0; sse <= 1; sse++) {
        CSignalDevice::m_bSSE = sse;
        CMixDevice m;
        Source a(0.f), b(0.f);
        a.step = 1.f;
        a.chunk = 16;
        b.step = 2.f;
        b.chunk = 8;
        m.setChannelNum(2);
        m.inputMixDeviceIs(0, &a);
        m.inputMixDeviceIs(1, &b);
        int processed = -1;
        CHECK_EQ(m.processSignal(64, g_out, &processed), 64);
        CHECK_EQ(a.calls, 4);
        CHECK_EQ(b.calls, 8);
        CHECK_EQ(processed, 0);
        int bad = 0;
        for (int i = 0; i < 64; i++) if (g_out[i] != 3.f * i) bad++;
        CHECK_EQ(bad, 0);
    }
}

/**
 * @brief Silent secondary inputs add nothing; the result is just the first input.
 */
TEST(mixdev, process_silent_secondary_adds_nothing)
{
    CMixDevice m;
    Source a(4.f), b(9.f);
    b.silent = 1;
    m.setChannelNum(2);
    m.inputMixDeviceIs(0, &a);
    m.inputMixDeviceIs(1, &b);
    int processed = -1;
    CHECK_EQ(m.processSignal(32, g_out, &processed), 32);
    CHECK_EQ(processed, 0);
    for (int i = 0; i < 32; i++) CHECK_CLOSE(g_out[i], 4.f, 0);
}

/**
 * @brief A silent leading input does not count as "first": the next non-silent input renders
 *        directly into the output, so the block is not flagged silent and holds that input's signal.
 *
 * @note mixdev.h says the flag is set when "the first input was silent"; the code actually keeps
 *       treating inputs as the first one until one of them produces signal, so the flag is only set
 *       when every connected input was silent.
 */
TEST(mixdev, process_silent_first_input_is_skipped)
{
    CMixDevice m;
    Source a(4.f), b(9.f), c(1.f);
    a.silent = 1;
    m.setChannelNum(3);
    m.inputMixDeviceIs(0, &a);
    m.inputMixDeviceIs(1, &b);
    m.inputMixDeviceIs(2, &c);
    fillOut(-100.f);
    int processed = -1;
    CHECK_EQ(m.processSignal(32, g_out, &processed), 32);
    CHECK_EQ(processed, 0);
    CHECK_EQ(b.calls, 1);
    for (int i = 0; i < 32; i++) CHECK_CLOSE(g_out[i], 10.f, 0);
}

/**
 * @brief When every connected input is silent the block is flagged silent.
 */
TEST(mixdev, process_all_inputs_silent_flags_silence)
{
    CMixDevice m;
    Source a, b;
    a.silent = b.silent = 1;
    m.setChannelNum(2);
    m.inputMixDeviceIs(0, &a);
    m.inputMixDeviceIs(1, &b);
    int processed = 0;
    CHECK_EQ(m.processSignal(32, g_out, &processed), 32);
    CHECK_EQ(processed, 1);
    CHECK_EQ(a.calls, 1);
    CHECK_EQ(b.calls, 1);
}

/**
 * @brief A silent first input that returns short blocks gets its part of the buffer zeroed and
 *        the block counts as not silent, so the other inputs' sum is valid.
 */
TEST(mixdev, process_silent_short_first_input_is_zeroed)
{
    CMixDevice m;
    Source a(4.f), b(9.f);
    a.silent = 1;
    a.chunk = 8;
    m.setChannelNum(2);
    m.inputMixDeviceIs(0, &a);
    m.inputMixDeviceIs(1, &b);
    fillOut(-100.f);
    int processed = -1;
    CHECK_EQ(m.processSignal(32, g_out, &processed), 32);
    CHECK_EQ(processed, 0);
    for (int i = 0; i < 32; i++) CHECK_CLOSE(g_out[i], 9.f, 0);
}

/**
 * @brief processSignalW() with no enabled voices writes nothing and reports silence.
 */
TEST(mixdev, process_w_no_voices_is_silent)
{
    CMixDevice m;
    fillOut(-2.f);
    int processed = 0;
    CHECK_EQ(m.processSignalW(16, g_out, &processed), 16);
    CHECK_EQ(processed, 1);
    CHECK_CLOSE(g_out[0], -2.f, 0);
}

/**
 * @brief processSignalW() with all voices silent leaves the output untouched and reports silence.
 */
TEST(mixdev, process_w_all_silent_leaves_buffer)
{
    CMixDevice m;
    Source a, b;
    a.silent = b.silent = 1;
    m.inputMixDeviceIs(0, &a);
    m.inputMixDeviceIs(1, &b);
    m.setEnableVoiceNum(2);
    fillOut(-2.f);
    int processed = 0;
    CHECK_EQ(m.processSignalW(16, g_out, &processed), 16);
    CHECK_EQ(processed, 1);
    CHECK_EQ(a.calls, 1);
    CHECK_EQ(b.calls, 1);
    for (int i = 0; i < 32; i++) CHECK_CLOSE(g_out[i], -2.f, 0);
}

/**
 * @brief Centered voices go equally to both channels and are summed; the output is cleared first.
 */
TEST(mixdev, process_w_center_pan_sums_to_both)
{
    CMixDevice m;
    Source a(1.f), b(0.f), c(5.f);
    b.silent = 1;
    a.step = 1.f;
    m.inputMixDeviceIs(0, &b);  // a silent voice first: buffer is cleared only for the first real one
    m.inputMixDeviceIs(1, &a);
    m.inputMixDeviceIs(2, &c);
    m.setEnableVoiceNum(3);
    fillOut(-50.f);
    int processed = -1;
    CHECK_EQ(m.processSignalW(20, g_out, &processed), 20);
    CHECK_EQ(processed, 0);
    for (int i = 0; i < 20; i++) {
        CHECK_CLOSE(g_out[2 * i], 1.f + i + 5.f, 0);
        CHECK_CLOSE(g_out[2 * i + 1], 1.f + i + 5.f, 0);
    }
    CHECK_CLOSE(g_out[40], -50.f, 0);
}

/**
 * @brief Pan attenuates the opposite channel linearly: pan > 0 scales left by 1 - pan, pan < 0
 *        scales right by 1 + pan; hard pan keeps a 1e-6 floor.
 */
TEST(mixdev, process_w_pan_law)
{
    const float pans[] = {0.25f, -0.5f, 1.f, -1.f, 0.75f};
    for (unsigned k = 0; k < sizeof pans / sizeof pans[0]; k++) {
        CMixDevice m;
        Source a(2.f);
        a.pan = pans[k];
        m.inputMixDeviceIs(0, &a);
        m.setEnableVoiceNum(1);
        fillOut(-1.f);
        int processed = -1;
        m.processSignalW(8, g_out, &processed);
        CHECK_EQ(processed, 0);
        float p = pans[k];
        float lv = p > 0 ? (1.f - p > 0.000001f ? 1.f - p : 0.000001f) : 1.f;
        float rv = p < 0 ? (1.f + p > 0.000001f ? 1.f + p : 0.000001f) : 1.f;
        for (int i = 0; i < 8; i++) {
            CHECK_CLOSE(g_out[2 * i], 2.f * lv, 1e-7);
            CHECK_CLOSE(g_out[2 * i + 1], 2.f * rv, 1e-7);
        }
    }
}

/**
 * @brief Two voices panned hard left and hard right end up (almost) only in their own channels.
 */
TEST(mixdev, process_w_hard_pans_separate)
{
    CMixDevice m;
    Source l(3.f), r(7.f);
    l.pan = -1.f;
    r.pan = 1.f;
    m.inputMixDeviceIs(0, &l);
    m.inputMixDeviceIs(1, &r);
    m.setEnableVoiceNum(2);
    int processed = -1;
    m.processSignalW(4, g_out, &processed);
    CHECK_EQ(processed, 0);
    for (int i = 0; i < 4; i++) {
        CHECK_CLOSE(g_out[2 * i], 3.f, 1e-5);
        CHECK_CLOSE(g_out[2 * i + 1], 7.f, 1e-5);
    }
}

/**
 * @brief processSignalW() only uses the first m_enableVoiceNum inputs (m_channelNum is ignored).
 */
TEST(mixdev, process_w_uses_enabled_voice_count)
{
    CMixDevice m;
    Source a(1.f), b(100.f);
    m.inputMixDeviceIs(0, &a);
    m.inputMixDeviceIs(1, &b);
    m.setChannelNum(0);
    m.setEnableVoiceNum(1);
    int processed = -1;
    m.processSignalW(4, g_out, &processed);
    CHECK_EQ(b.calls, 0);
    CHECK_CLOSE(g_out[0], 1.f, 0);
    CHECK_CLOSE(g_out[7], 1.f, 0);
}

/**
 * @brief CMixDevice can be used as the input of another device through the base-class interface
 *        and as a nested input of another mixer.
 */
TEST(mixdev, nested_mixers)
{
    CMixDevice inner, outer;
    Source a(1.f), b(2.f), c(4.f);
    inner.setChannelNum(2);
    inner.inputMixDeviceIs(0, &a);
    inner.inputMixDeviceIs(1, &b);
    outer.setChannelNum(2);
    outer.inputMixDeviceIs(0, &inner);
    outer.inputMixDeviceIs(1, &c);
    int processed = -1;
    CSignalDevice* dev = &outer;
    CHECK_EQ(dev->processSignal(128, g_out, &processed), 128);
    CHECK_EQ(processed, 0);
    int bad = 0;
    for (int i = 0; i < 128; i++) if (g_out[i] != 7.f) bad++;
    CHECK_EQ(bad, 0);
}
