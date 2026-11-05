/**
 * @file test_vca.cpp
 * @brief Unit tests for techpop/vca.h: CVca gain ramps, envelope scaling, amp gain, velocity
 *        sensitivity, the amp LFO, the scalar/SSE paths and the end-of-release voice reset.
 */
#include "test.h"

#include <math.h>
#include <vector>

#include "engine.h"
#include "vca.h"
#include "vcf.h"
#include "vco.h"

namespace {

/**
 * @brief Input device producing a constant value.
 */
class OnesSource : public CSignalDevice {
public:
    OnesSource() : m_value(1.0f) {}

    virtual int processSignal(int size, float* pBuf, int* pProcessed)
    {
        for (int i = 0; i < size; i++) pBuf[i] = m_value;
        *pProcessed = 0;
        return size;
    }

    float m_value;
};

/**
 * @brief Restores CSignalDevice::m_bSSE when it goes out of scope.
 */
struct SSEGuard {
    int saved;
    SSEGuard() : saved(CSignalDevice::m_bSSE) {}
    ~SSEGuard() { CSignalDevice::m_bSSE = saved; }
};

/**
 * @brief Expected velocity gain: -30 dB * sens at velocity 0, 0 dB at 127.
 */
double velGain(double sens, int vel)
{
    return pow(10.0, sens * -30.0 * (127 - vel) / 127.0 / 20.0);
}

}  // namespace

/** @brief The constructor sets envelope max 2048, amp gain 0.5, smoothing and no velocity sensitivity. */
TEST(vca, constructor_defaults)
{
    CVca v;
    CHECK_EQ(v.m_max, 2048);
    CHECK_EQ(v.m_peakLevel, 2048);
    CHECK_EQ(v.m_sustainLevel, 2048);
    CHECK_CLOSE(v.CEnvelopedObject::m_gain, 1.0, 0);
    CHECK_CLOSE(v.m_gain, 0.5, 0);
    CHECK_CLOSE(v.m_velGain, 1.0, 0);
    CHECK_CLOSE(v.m_velocitySens, 0.0, 0);
    CHECK_CLOSE(v.m_curGain, 0.0, 0);
    CHECK_CLOSE(v.m_targetGain, 0.0, 0);
    CHECK_EQ(v.m_bSmooth, 1);
    CHECK_EQ(v.m_bProcessIdle, 0);
    CHECK_EQ(v.m_bGainChanged, 1);
    CHECK_EQ(v.m_bInvert, 0);
    CHECK_EQ(v.getEnvStatus(), ENV_OFF);
}

/** @brief setGain() stores the amp gain and flags a change only when the value differs. */
TEST(vca, set_gain_flags_changes)
{
    CVca v;
    v.m_bGainChanged = 0;
    v.setGain(0.5f);
    CHECK_EQ(v.m_bGainChanged, 0);
    v.setGain(0.8f);
    CHECK_CLOSE(v.m_gain, 0.8, 1e-7);
    CHECK_EQ(v.m_bGainChanged, 1);
    // the envelope depth is untouched
    CHECK_EQ(v.m_peakLevel, 2048);
}

/** @brief Only an enabled LFO with destination LFO_DEST_AMP (4) is routed to the amplifier. */
TEST(vca, lfo_routing)
{
    CVca v;
    v.changeLfoSetting(0, 1, 4);
    CHECK_EQ(v.m_bLfoEnable[0], 1);
    v.changeLfoSetting(0, 0, 4);
    CHECK_EQ(v.m_bLfoEnable[0], 0);
    v.changeLfoSetting(1, 1, 3);
    CHECK_EQ(v.m_bLfoEnable[1], 0);
    v.changeLfoSetting(1, 1, 4);
    CHECK_EQ(v.m_bLfoEnable[1], 1);
}

/** @brief noteOn() computes the velocity gain (up to -30 dB at velocity 0) and starts the attack. */
TEST(vca, velocity_gain)
{
    CVca v;
    v.noteOn(0);
    CHECK_CLOSE(v.m_velGain, 1.0, 1e-6);       // no sensitivity
    CHECK_EQ(v.getEnvStatus(), ENV_ATTACK);
    v.setVelocitySens(1.0f);
    CHECK_CLOSE(v.m_velocitySens, 1.0, 0);
    v.noteOn(127);
    CHECK_CLOSE(v.m_velGain, 1.0, 1e-6);
    v.noteOn(0);
    CHECK_CLOSE(v.m_velGain, 0.0316227766, 1e-6);
    v.noteOn(64);
    CHECK_CLOSE(v.m_velGain, velGain(1.0, 64), 1e-6);
    v.setVelocitySens(0.5f);
    v.noteOn(0);
    CHECK_CLOSE(v.m_velGain, 0.177827941, 1e-6);  // -15 dB
    // monotonic in velocity
    float prev = 0;
    for (int vel = 0; vel <= 127; vel++) {
        v.noteOn(vel);
        CHECK(v.m_velGain > prev);
        prev = v.m_velGain;
    }
}

/** @brief noteOff() starts the release. */
TEST(vca, note_off_releases)
{
    CVca v;
    v.noteOff();
    CHECK_EQ(v.getEnvStatus(), ENV_OFF);
    v.noteOn(100);
    v.noteOff();
    CHECK_EQ(v.getEnvStatus(), ENV_RELEASE);
}

/** @brief A gain change ramps linearly from the previous gain to level/2048 * gain * velGain over the block. */
TEST(vca, gain_ramps_over_block)
{
    SSEGuard g;
    for (int sse = 0; sse < 2; sse++) {
        CVca v;
        CSignalDevice::m_bSSE = sse;
        float buf[64];
        for (int i = 0; i < 64; i++) buf[i] = 1.0f;
        CHECK_EQ(v.processEnvelopedSignal(1024, buf, 64, 0, 0), 0);
        double target = 1024 * 0.5 / 2048;       // 0.25
        CHECK_CLOSE(v.m_targetGain, target, 1e-7);
        CHECK_CLOSE(v.m_curGain, target, 1e-6);
        for (int i = 0; i < 64; i++) CHECK_CLOSE(buf[i], target * (i + 1) / 64, 1e-6);
        CHECK_EQ(v.m_lastLevel, 1024);
        CHECK_EQ(v.m_bGainChanged, 0);
        // nothing changed: constant gain
        for (int i = 0; i < 64; i++) buf[i] = 2.0f;
        CHECK_EQ(v.processEnvelopedSignal(1024, buf, 64, 0, 0), 0);
        for (int i = 0; i < 64; i++) CHECK_CLOSE(buf[i], 0.5, 1e-6);
        // ramp down to a new level
        for (int i = 0; i < 64; i++) buf[i] = 1.0f;
        v.processEnvelopedSignal(512, buf, 64, 0, 0);
        CHECK_CLOSE(buf[63], 0.125, 1e-6);
        CHECK_CLOSE(buf[0], 0.25 - 0.125 / 64, 1e-6);
        for (int i = 1; i < 64; i++) CHECK(buf[i] < buf[i - 1]);
    }
}

/** @brief The SSE ramp gives the same result as the scalar one, also on unaligned buffers. */
TEST(vca, sse_matches_scalar)
{
    SSEGuard g;
    static const int sizes[5] = {7, 8, 13, 64, 257};
    for (int s = 0; s < 5; s++) {
        for (int ofs = 0; ofs < 4; ofs++) {
            int n = sizes[s];
            std::vector<float> a(n + 8), b(n + 8);
            for (int i = 0; i < n + 8; i++) a[i] = b[i] = (float)sin(i * 0.37) + 0.1f;
            CVca va, vb;
            va.m_curGain = vb.m_curGain = 0.3f;
            CSignalDevice::m_bSSE = 0;
            va.processEnvelopedSignal(1500, &a[ofs], n, 100, 0);
            CSignalDevice::m_bSSE = 1;
            vb.processEnvelopedSignal(1500, &b[ofs], n, 100, 0);
            for (int i = 0; i < n + 8; i++) CHECK_CLOSE(a[i], b[i], 1e-5);
            CHECK_CLOSE(va.m_curGain, vb.m_curGain, 1e-5);
        }
    }
}

/** @brief A constant zero gain leaves the buffer untouched and reports silence. */
TEST(vca, zero_gain_is_silent)
{
    CVca v;
    float buf[16];
    for (int i = 0; i < 16; i++) buf[i] = 3.0f;
    // first block at level 0 still "ramps" (from 0 to 0) and writes
    CHECK_EQ(v.processEnvelopedSignal(0, buf, 16, 0, 0), 0);
    for (int i = 0; i < 16; i++) CHECK_CLOSE(buf[i], 0, 0);
    for (int i = 0; i < 16; i++) buf[i] = 3.0f;
    CHECK_EQ(v.processEnvelopedSignal(0, buf, 16, 0, 0), 1);
    for (int i = 0; i < 16; i++) CHECK_CLOSE(buf[i], 3.0, 0);
}

/** @brief The amp LFOs reduce the gain by (2048 - lfo) / 2048 each, and a change of LFO value ramps. */
TEST(vca, lfo_reduces_gain)
{
    CVca v;
    v.setGain(1.0f);
    float buf[32];
    for (int i = 0; i < 32; i++) buf[i] = 1.0f;
    v.processEnvelopedSignal(2048, buf, 32, 1024, 0);
    CHECK_CLOSE(v.m_targetGain, 0.5, 1e-7);
    v.processEnvelopedSignal(2048, buf, 32, 1024, 1024);
    CHECK_CLOSE(v.m_targetGain, 0.25, 1e-7);
    v.processEnvelopedSignal(2048, buf, 32, 0, 2048);
    CHECK_CLOSE(v.m_targetGain, 0.0, 1e-7);
    CHECK_EQ(v.m_lastLfo1, 0);
    CHECK_EQ(v.m_lastLfo2, 2048);
}

/** @brief The velocity gain scales the amplitude reached by a note. */
TEST(vca, velocity_scales_output)
{
    CVca v;
    v.setVelocitySens(1.0f);
    v.noteOn(0);
    float buf[16];
    for (int i = 0; i < 16; i++) buf[i] = 1.0f;
    v.processEnvelopedSignal(2048, buf, 16, 0, 0);
    CHECK_CLOSE(buf[15], 0.5 * velGain(1.0, 0), 1e-6);
}

/** @brief A full note through processSignal(): attack ramp, sustain level * gain, then release to silence. */
TEST(vca, full_note_amplitude)
{
    CVca v;
    OnesSource src;
    v.inputDeviceIs(&src);
    v.setAttack(0.01f);
    v.setDecay(0.01f);
    v.setSustain(0.5f);
    v.setRelease(0.01f);
    float buf[256];
    int processed = 0;
    v.processSignal(256, buf, &processed);
    CHECK_EQ(processed, 1);        // envelope off: silent
    v.noteOn(127);
    std::vector<float> out;
    for (int b = 0; b < 8; b++) {
        v.processSignal(256, buf, &processed);
        CHECK_EQ(processed, 0);
        out.insert(out.end(), buf, buf + 256);
    }
    CHECK(out[0] >= 0 && out[0] < 0.01);
    // peak of the attack is the full amp gain
    float peak = 0;
    for (size_t i = 0; i < out.size(); i++) if (out[i] > peak) peak = out[i];
    CHECK(peak <= 0.5 + 1e-5);
    CHECK(peak > 0.4);
    // sustain at 0.5 * 0.5
    CHECK_CLOSE(out[out.size() - 1], 0.25, 1e-5);
    CHECK_EQ(v.getEnvStatus(), ENV_SUSTAIN);
    // a later amp gain change ramps to the new value
    v.setGain(1.0f);
    v.processSignal(256, buf, &processed);
    CHECK_CLOSE(buf[255], 0.5, 1e-5);
    CHECK(buf[0] < 0.26);
}

/**
 * @brief When the release ends, the VCA clears its gain and resets the VCF filter and the VCO's FM
 *        smoothing and envelope (input chain CVca <- CVcf <- CVco).
 */
TEST(vca, release_end_resets_voice_chain)
{
    t1::initEngine();
    CVco vco;
    CVcf vcf;
    CVca vca;
    vcf.inputDeviceIs(&vco);
    vca.inputDeviceIs(&vcf);
    vco.m_bFMReset = 0;
    vco.m_envStatus = ENV_SUSTAIN;
    vco.m_curLevel = 10;
    vcf.m_envStatus = ENV_SUSTAIN;
    vcf.m_curLevel = 100;
    vcf.m_low = 0.5f;
    vcf.m_y[3] = 0.5f;
    vcf.m_z[2] = 0.5;
    vcf.m_limitEnv = 0.2f;
    vca.setAttack(0.0f);
    vca.setDecay(0.0f);
    vca.setRelease(0.001f);
    vca.noteOn(127);
    float buf[256];
    for (int i = 0; i < 256; i++) buf[i] = 1.0f;
    int processed;
    vca.processSignalSub(64, buf, 0, 0, &processed);
    REQUIRE(vca.getEnvStatus() == ENV_SUSTAIN);
    CHECK(vca.m_curGain > 0);
    vca.noteOff();
    vca.processSignalSub(256, buf, 0, 0, &processed);
    CHECK_EQ(vca.getEnvStatus(), ENV_OFF);
    CHECK_CLOSE(vca.m_curGain, 0, 0);
    CHECK_EQ(vcf.getEnvStatus(), ENV_OFF);
    CHECK_CLOSE(vcf.m_low, 0, 0);
    CHECK_CLOSE(vcf.m_y[3], 0, 0);
    CHECK_CLOSE(vcf.m_z[2], 0, 0);
    CHECK_CLOSE(vcf.m_limitEnv, 0, 0);
    CHECK_EQ(vcf.m_lastFreq, -1);
    CHECK_EQ(vco.m_bFMReset, 1);
    CHECK_EQ(vco.getEnvStatus(), ENV_OFF);
    CHECK_EQ(vco.m_curLevel, 0);
}
