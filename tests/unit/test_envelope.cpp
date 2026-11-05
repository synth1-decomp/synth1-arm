/**
 * @file test_envelope.cpp
 * @brief Unit tests for techpop/envelope.h: the CEnvelopedObject ADSR (segment lengths, levels,
 *        gain/invert, sustain, release, the decay/release step size, the smoothing flag) and the
 *        block splitting and silence handling of processSignal()/processSignalSub().
 */
#include "test.h"

#include <stdlib.h>
#include <vector>

#include "envelope.h"
#include "lfo.h"

namespace {

/**
 * @brief One recorded processEnvelopedSignal() call.
 */
struct EnvCall {
    int level;
    int size;
    int lfo1;
    int lfo2;
};

/**
 * @brief Test envelope that records every block and writes the envelope level into it.
 *
 * Blocks whose |level| is below m_silentBelow are reported as silent (buffer untouched); if
 * m_silentPattern is not empty, call k is silent when m_silentPattern[k % size] is nonzero instead.
 */
class RecEnv : public CEnvelopedObject {
public:
    RecEnv() : m_silentBelow(0), m_releaseEnds(0), m_postCalls(0) {}

    virtual int processEnvelopedSignal(int level, float* pBuf, int size, int lfo1, int lfo2)
    {
        EnvCall c;
        c.level = level;
        c.size = size;
        c.lfo1 = lfo1;
        c.lfo2 = lfo2;
        m_calls.push_back(c);
        if (!m_silentPattern.empty()) {
            if (m_silentPattern[(m_calls.size() - 1) % m_silentPattern.size()]) return 1;
        } else if (abs(level) < m_silentBelow) {
            return 1;
        }
        for (int i = 0; i < size; i++) pBuf[i] = (float)level;
        return 0;
    }

    virtual void releaseEnd() { m_releaseEnds++; }

    virtual void postProcessSignal(float* pBuf, int size, int* pProcessed) { m_postCalls++; }

    std::vector<EnvCall> m_calls;
    std::vector<int> m_silentPattern;
    int m_silentBelow;
    int m_releaseEnds;
    int m_postCalls;
};

/**
 * @brief Input device that fills its block with a constant (or reports silence).
 */
class ConstSource : public CSignalDevice {
public:
    ConstSource() : m_value(1.0f), m_silent(0), m_calls(0) {}

    virtual int processSignal(int size, float* pBuf, int* pProcessed)
    {
        m_calls++;
        if (m_silent) {
            *pProcessed = 1;
            return size;
        }
        for (int i = 0; i < size; i++) pBuf[i] = m_value;
        *pProcessed = 0;
        return size;
    }

    float m_value;
    int m_silent;
    int m_calls;
};

/**
 * @brief Runs a generator envelope for @p total samples in blocks and returns the per-sample level
 *        (silent blocks count as 0).
 */
std::vector<float> runTrace(RecEnv& e, int total, int block)
{
    std::vector<float> out;
    std::vector<float> buf(block);
    int done = 0;
    while (done < total) {
        int n = total - done < block ? total - done : block;
        int processed = 0;
        e.processSignal(n, &buf[0], &processed);
        for (int i = 0; i < n; i++) out.push_back(processed ? 0.0f : buf[i]);
        done += n;
    }
    return out;
}

/**
 * @brief A generator envelope at 1 kHz (so seconds map to round sample counts).
 */
void setupGen(RecEnv& e)
{
    e.m_bNoInput = 1;
    e.setSamplingFreq(1000);
}

}  // namespace

/** @brief The constructor sets an idle envelope with m_max 100, gain 1, full sustain, 1-sample segments. */
TEST(envelope, constructor_defaults)
{
    RecEnv e;
    CHECK_EQ(e.m_max, 100);
    CHECK_EQ(e.m_peakLevel, 100);
    CHECK_EQ(e.m_sustainLevel, 100);
    CHECK_CLOSE(e.m_gain, 1.0, 0);
    CHECK_CLOSE(e.m_sustain, 1.0, 0);
    CHECK_EQ(e.m_attackLen, 1);
    CHECK_EQ(e.m_decayLen, 1);
    CHECK_EQ(e.m_releaseLen, 1);
    CHECK_EQ(e.m_envStatus, ENV_OFF);
    CHECK_EQ(e.getEnvStatus(), ENV_OFF);
    CHECK_EQ(e.m_curLevel, 0);
    CHECK_EQ(e.m_bEnvEnable, 1);
    CHECK_EQ(e.m_bInvert, 0);
    CHECK_EQ(e.m_bSmooth, 0);
    CHECK_EQ(e.m_bNoInput, 0);
    CHECK_EQ(e.m_bProcessIdle, 0);
    CHECK_EQ(e.m_bPostProcess, 0);
    CHECK_EQ(e.m_decayType, ENV_CURVE_EXP);
    CHECK_EQ(e.m_releaseType, ENV_CURVE_EXP);
    CHECK_EQ(e.m_bLfoEnable[0], 0);
    CHECK_EQ(e.m_bLfoEnable[1], 0);
    CHECK(e.m_pLfo[0] == 0);
    CHECK(e.m_pLfo[1] == 0);
    CHECK_EQ(e.m_samplingFreq, 44100);
    CHECK_EQ(e.m_prevSamplingFreq, 44100);
}

/** @brief Segment setters convert seconds to whole samples at the current rate. */
TEST(envelope, segment_lengths_in_samples)
{
    RecEnv e;
    e.setAttack(0.5f);
    e.setDecay(0.25f);
    e.setRelease(1.0f);
    CHECK_EQ(e.m_attackLen, 22050);
    CHECK_EQ(e.m_decayLen, 11025);
    CHECK_EQ(e.m_releaseLen, 44100);
    e.setAttack(0.0f);
    CHECK_EQ(e.m_attackLen, 0);
    setupGen(e);
    e.setAttack(0.1f);
    e.setDecay(0.2f);
    e.setRelease(0.3f);
    CHECK_EQ(e.m_attackLen, 100);
    CHECK_EQ(e.m_decayLen, 200);
    CHECK_EQ(e.m_releaseLen, 300);
}

/** @brief setSamplingFreq() rescales all segment lengths and the position within the running one. */
TEST(envelope, sampling_rate_change_rescales_segments)
{
    RecEnv e;
    e.m_bNoInput = 1;
    e.setAttack(0.5f);
    e.setDecay(0.25f);
    e.setRelease(0.125f);
    e.envelopeOn();
    float buf[1000];
    int processed;
    e.processSignalSub(1000, buf, 0, 0, &processed);
    CHECK_EQ(e.m_envStatus, ENV_ATTACK);
    CHECK_EQ(e.m_curPos, 1000);
    e.setSamplingFreq(88200);
    CHECK_EQ(e.m_prevSamplingFreq, 88200);
    CHECK_EQ(e.m_attackLen, 44100);
    CHECK_EQ(e.m_decayLen, 22050);
    CHECK_EQ(e.m_releaseLen, 11024);   // 5512 * 2 (0.125 s truncated at 44.1 kHz)
    CHECK_EQ(e.m_curPos, 2000);
    e.setSamplingFreq(44100);
    CHECK_EQ(e.m_attackLen, 22050);
    CHECK_EQ(e.m_curPos, 1000);
}

/** @brief Changing the attack time while attacking keeps the relative position in the segment. */
TEST(envelope, set_attack_while_running_keeps_relative_position)
{
    RecEnv e;
    setupGen(e);
    e.setAttack(0.1f);
    e.envelopeOn();
    float buf[50];
    int processed;
    e.processSignalSub(50, buf, 0, 0, &processed);
    CHECK_EQ(e.m_curPos, 50);
    e.setAttack(0.2f);
    CHECK_EQ(e.m_attackLen, 200);
    CHECK_EQ(e.m_curPos, 100);
    // decay/release setters leave the attack position alone
    e.setDecay(0.5f);
    e.setRelease(0.5f);
    CHECK_EQ(e.m_curPos, 100);
}

/** @brief setGain() scales the peak and sustain levels; a negative gain sets the invert flag. */
TEST(envelope, gain_scales_levels_and_negative_inverts)
{
    RecEnv e;
    e.setSustain(0.5f);
    CHECK_EQ(e.m_sustainLevel, 50);
    e.setGain(0.5f);
    CHECK_EQ(e.m_peakLevel, 50);
    CHECK_EQ(e.m_sustainLevel, 25);
    CHECK_EQ(e.m_bInvert, 0);
    e.setGain(-2.0f);
    CHECK_CLOSE(e.m_gain, 2.0, 0);
    CHECK_EQ(e.m_bInvert, 1);
    CHECK_EQ(e.m_peakLevel, 200);
    CHECK_EQ(e.m_sustainLevel, 100);
    e.setGain(0.0f);
    CHECK_EQ(e.m_peakLevel, 0);
    CHECK_EQ(e.m_curLevel, 0);
}

/** @brief setGain() rescales the current level proportionally, or snaps it to sustain while sustaining. */
TEST(envelope, gain_rescales_current_level)
{
    RecEnv e;
    e.m_envStatus = ENV_ATTACK;
    e.m_curLevel = 40;
    e.setGain(2.0f);
    CHECK_EQ(e.m_curLevel, 80);
    e.m_envStatus = ENV_SUSTAIN;
    e.setSustain(0.25f);
    CHECK_EQ(e.m_curLevel, 50);    // 100 * 2 * 0.25
    e.setGain(1.0f);
    CHECK_EQ(e.m_curLevel, 25);
}

/** @brief setMax() rescales the peak, sustain and current levels. */
TEST(envelope, set_max_rescales_levels)
{
    RecEnv e;
    e.setSustain(0.5f);
    e.m_curLevel = 30;
    e.setMax(1000);
    CHECK_EQ(e.m_max, 1000);
    CHECK_EQ(e.m_peakLevel, 1000);
    CHECK_EQ(e.m_sustainLevel, 500);
    CHECK_EQ(e.m_curLevel, 300);
    e.setGain(0.5f);
    e.setMax(2000);
    CHECK_EQ(e.m_peakLevel, 1000);
    CHECK_EQ(e.m_sustainLevel, 500);
}

/** @brief setSustain() only changes the current level while sustaining. */
TEST(envelope, sustain_applies_immediately_only_when_sustaining)
{
    RecEnv e;
    e.m_envStatus = ENV_DECAY;
    e.m_curLevel = 77;
    e.setSustain(0.3f);
    CHECK_CLOSE(e.m_sustain, 0.3, 1e-7);
    CHECK_EQ(e.m_sustainLevel, 30);
    CHECK_EQ(e.m_curLevel, 77);
    e.m_envStatus = ENV_SUSTAIN;
    e.setSustain(0.6f);
    CHECK_EQ(e.m_curLevel, 60);
}

/** @brief envelopeOn() starts the attack, retriggering from the current level; envelopeReset() stops it. */
TEST(envelope, note_on_retriggers_from_current_level)
{
    RecEnv e;
    setupGen(e);
    e.setAttack(0.1f);
    e.envelopeOn();
    CHECK_EQ(e.m_envStatus, ENV_ATTACK);
    CHECK_EQ(e.m_curPos, 0);
    e.m_envStatus = ENV_RELEASE;
    e.m_curLevel = 40;
    e.envelopeOn();
    CHECK_EQ(e.m_envStatus, ENV_ATTACK);
    CHECK_EQ(e.m_curPos, 40);      // 40 * 100 / 100
    e.envelopeReset();
    CHECK_EQ(e.m_envStatus, ENV_OFF);
    CHECK_EQ(e.m_curLevel, 0);
    CHECK_EQ(e.m_curPos, 0);
}

/** @brief envelopeOff() starts the release from the current level and is ignored when off or releasing. */
TEST(envelope, note_off_starts_release)
{
    RecEnv e;
    setupGen(e);
    e.setRelease(0.3f);
    e.envelopeOff();
    CHECK_EQ(e.m_envStatus, ENV_OFF);
    e.m_envStatus = ENV_SUSTAIN;
    e.m_curLevel = 60;
    e.m_curPos = 12;
    e.envelopeOff();
    CHECK_EQ(e.m_envStatus, ENV_RELEASE);
    CHECK_EQ(e.m_releaseLevel, 60);
    CHECK_EQ(e.m_curPos, 0);
    CHECK(e.m_resolution >= 1);
    e.m_curLevel = 10;
    e.m_curPos = 5;
    e.envelopeOff();               // already releasing: unchanged
    CHECK_EQ(e.m_releaseLevel, 60);
    CHECK_EQ(e.m_curPos, 5);
}

/** @brief Full ADSR with linear decay: linear attack to the peak, linear decay to sustain, then sustain. */
TEST(envelope, adsr_linear_levels)
{
    RecEnv e;
    setupGen(e);
    e.setAttack(0.1f);
    e.setDecay(0.2f);
    e.setSustain(0.5f);
    e.setRelease(0.3f);
    e.setDecayType(ENV_CURVE_LINEAR);
    e.envelopeOn();
    std::vector<float> t = runTrace(e, 400, 64);
    // attack: one step per level unit, level = i + 1
    for (int i = 0; i < 100; i++) CHECK_CLOSE(t[i], i + 1, 0);
    // decay: steps of len / range = 4 samples, one level unit each
    CHECK_CLOSE(t[100], 99, 0);
    CHECK_CLOSE(t[103], 99, 0);
    CHECK_CLOSE(t[104], 98, 0);
    CHECK_CLOSE(t[199], 75, 0);
    CHECK_CLOSE(t[299], 50, 0);
    for (int i = 101; i < 300; i++) CHECK(t[i] <= t[i - 1]);
    for (int i = 300; i < 400; i++) CHECK_CLOSE(t[i], 50, 0);
    CHECK_EQ(e.getEnvStatus(), ENV_SUSTAIN);
    CHECK_EQ(e.m_curLevel, 50);
    CHECK_EQ(e.m_releaseEnds, 0);
}

/** @brief The release falls monotonically to zero within the release time, then the envelope is off. */
TEST(envelope, release_reaches_zero_and_calls_release_end)
{
    RecEnv e;
    setupGen(e);
    e.setAttack(0.01f);
    e.setDecay(0.01f);
    e.setSustain(0.5f);
    e.setRelease(0.3f);
    e.envelopeOn();
    runTrace(e, 100, 64);
    REQUIRE(e.getEnvStatus() == ENV_SUSTAIN);
    e.envelopeOff();
    CHECK_EQ(e.m_releaseLevel, 50);
    std::vector<float> t = runTrace(e, 300, 32);
    CHECK(t[0] <= 50);
    CHECK(t[0] > 25);
    for (int i = 1; i < 300; i++) CHECK(t[i] <= t[i - 1]);
    CHECK_EQ(e.getEnvStatus(), ENV_OFF);
    CHECK_EQ(e.m_curLevel, 0);
    CHECK_EQ(e.m_releaseEnds, 1);
    // once off (and not processing idle) the device reports silence without calling the derived class
    size_t calls = e.m_calls.size();
    float buf[16];
    int processed = 0;
    CHECK_EQ(e.processSignal(16, buf, &processed), 16);
    CHECK_EQ(processed, 1);
    CHECK_EQ((int)e.m_calls.size(), (int)calls);
}

/** @brief The exponential decay drops fast at the start and reaches sustain no later than the decay time. */
TEST(envelope, exponential_decay_shape)
{
    RecEnv e;
    setupGen(e);
    e.setAttack(0.001f);
    e.setDecay(0.2f);
    e.setSustain(0.2f);
    e.setDecayType(ENV_CURVE_EXP);
    e.envelopeOn();
    std::vector<float> t = runTrace(e, 300, 64);
    CHECK_CLOSE(t[0], 100, 0);
    for (int i = 2; i < 300; i++) CHECK(t[i] <= t[i - 1]);
    for (int i = 1; i < 300; i++) CHECK(t[i] >= 20);
    // halfway through the decay the curve is already near the bottom (exp((x-1)*8)*x)
    CHECK(t[1 + 100] < 20 + 5);
    // 10% into the decay it is still well above the linear midpoint
    CHECK(t[1 + 5] > 60);
    CHECK_CLOSE(t[201], 20, 0);
    CHECK_EQ(e.getEnvStatus(), ENV_SUSTAIN);
}

/**
 * @brief The release curve type is m_releaseType (always exponential, no setter) while the step size
 *        follows m_decayType, so a "linear" envelope still releases exponentially.
 */
TEST(envelope, release_curve_ignores_decay_type)
{
    RecEnv e;
    setupGen(e);
    e.setAttack(0.001f);
    e.setDecay(0.001f);
    e.setSustain(1.0f);
    e.setRelease(0.2f);
    e.setDecayType(ENV_CURVE_LINEAR);
    e.envelopeOn();
    runTrace(e, 10, 10);
    REQUIRE(e.getEnvStatus() == ENV_SUSTAIN);
    e.envelopeOff();
    CHECK_EQ(e.m_resolution, 2);   // linear step size: 200 / 100
    std::vector<float> t = runTrace(e, 200, 50);
    // a linear release would be at 50 halfway; the exponential one is almost at 0
    CHECK(t[100] < 5);
    CHECK(t[0] > 90);
}

/** @brief A sustain of 1.0 skips the decay: the envelope goes straight from the peak to sustain. */
TEST(envelope, full_sustain_skips_decay)
{
    RecEnv e;
    setupGen(e);
    e.setAttack(0.01f);
    e.setDecay(0.5f);
    e.setSustain(1.0f);
    e.envelopeOn();
    std::vector<float> t = runTrace(e, 20, 20);
    CHECK_CLOSE(t[9], 100, 0);
    CHECK_CLOSE(t[10], 100, 0);
    CHECK_EQ(e.getEnvStatus(), ENV_SUSTAIN);
}

/** @brief A zero attack time jumps directly to the peak. */
TEST(envelope, zero_attack_jumps_to_peak)
{
    RecEnv e;
    setupGen(e);
    e.setAttack(0.0f);
    e.setDecay(0.1f);
    e.setSustain(0.5f);
    e.envelopeOn();
    std::vector<float> t = runTrace(e, 4, 4);
    CHECK(t[0] >= 90);
    CHECK(e.getEnvStatus() == ENV_DECAY);
}

/** @brief With a negative gain the levels handed to the derived class are negated. */
TEST(envelope, inverted_envelope_passes_negative_levels)
{
    RecEnv e;
    setupGen(e);
    e.setGain(-1.0f);
    e.setAttack(0.01f);
    e.setDecay(0.01f);
    e.setSustain(0.5f);
    e.envelopeOn();
    std::vector<float> t = runTrace(e, 40, 40);
    CHECK_CLOSE(t[0], -10, 0);
    CHECK_CLOSE(t[9], -100, 0);
    CHECK_CLOSE(t[39], -50, 0);
    for (int i = 0; i < 40; i++) CHECK(t[i] < 0);
    CHECK_EQ(e.m_curLevel, 50);    // internal level stays positive
}

/** @brief When the envelope is disabled every block gets level 0 and the state does not advance. */
TEST(envelope, disabled_envelope_passes_zero)
{
    RecEnv e;
    setupGen(e);
    e.setAttack(0.01f);
    e.setEnvelopEnable(0);
    CHECK_EQ(e.m_bEnvEnable, 0);
    e.envelopeOn();
    float buf[32];
    int processed = 0;
    e.processSignal(32, buf, &processed);
    REQUIRE(e.m_calls.size() == 1);
    CHECK_EQ(e.m_calls[0].level, 0);
    CHECK_EQ(e.m_calls[0].size, 32);
    CHECK_EQ(e.m_envStatus, ENV_ATTACK);
    CHECK_EQ(e.m_curPos, 0);
    CHECK_EQ(processed, 0);
}

/** @brief calcDRResolution(): linear curves use len / range steps and recompute only at the end. */
TEST(envelope, dr_resolution_linear)
{
    RecEnv e;
    e.m_decayType = ENV_CURVE_LINEAR;
    e.m_envStatus = ENV_DECAY;
    e.m_decayLen = 1000;
    e.m_peakLevel = 100;
    e.m_sustainLevel = 90;
    e.m_nextResPos = 0;
    e.calcDRResolution();
    CHECK_EQ(e.m_resolution, 100);
    CHECK_EQ(e.m_nextResPos, 1000);
    e.m_bSmooth = 1;
    e.calcDRResolution();
    CHECK_EQ(e.m_resolution, 1000);
    // range larger than the length: step size at least 1
    e.m_bSmooth = 0;
    e.m_sustainLevel = 0;
    e.m_decayLen = 50;
    e.calcDRResolution();
    CHECK_EQ(e.m_resolution, 1);
    // release uses the release length and the release level
    e.m_envStatus = ENV_RELEASE;
    e.m_releaseLen = 600;
    e.m_releaseLevel = 30;
    e.calcDRResolution();
    CHECK_EQ(e.m_resolution, 20);
    CHECK_EQ(e.m_nextResPos, 600);
}

/** @brief calcDRResolution(): exponential curves refine the step at the start and coarsen it later. */
TEST(envelope, dr_resolution_exponential_stages)
{
    RecEnv e;
    e.m_decayType = ENV_CURVE_EXP;
    e.m_envStatus = ENV_DECAY;
    e.m_decayLen = 1800;
    e.m_peakLevel = 10;
    e.m_sustainLevel = 0;          // base step 180
    static const int expRes[6] = {20, 30, 45, 180, 360, 900};
    static const int expNext[6] = {90, 180, 450, 600, 900, 1800};
    e.m_nextResPos = 0;
    for (int i = 0; i < 6; i++) {
        e.calcDRResolution();
        CHECK_EQ(e.m_resolution, expRes[i]);
        CHECK_EQ(e.m_nextResPos, expNext[i]);
    }
    // with smoothing the step is half the stage length
    e.m_bSmooth = 1;
    e.m_nextResPos = 0;
    e.calcDRResolution();
    CHECK_EQ(e.m_nextResPos, 90);
    CHECK_EQ(e.m_resolution, 45);
    e.m_nextResPos = 1000;
    e.calcDRResolution();
    CHECK_EQ(e.m_nextResPos, 1800);
    CHECK_EQ(e.m_resolution, 900);
    // never below one sample
    e.m_bSmooth = 0;
    e.m_decayLen = 5;
    e.m_nextResPos = 0;
    e.calcDRResolution();
    CHECK_EQ(e.m_resolution, 1);
}

/** @brief With m_bSmooth the attack and a linear decay are each handed over as one block. */
TEST(envelope, smooth_flag_uses_whole_segment_blocks)
{
    RecEnv e;
    setupGen(e);
    e.m_bSmooth = 1;
    e.setAttack(0.1f);
    e.setDecay(0.2f);
    e.setSustain(0.5f);
    e.setDecayType(ENV_CURVE_LINEAR);
    e.envelopeOn();
    float buf[400];
    int processed = 0;
    e.processSignalSub(400, buf, 0, 0, &processed);
    REQUIRE(e.m_calls.size() == 3);
    CHECK_EQ(e.m_calls[0].size, 100);
    CHECK_EQ(e.m_calls[0].level, 100);
    CHECK_EQ(e.m_calls[1].size, 200);
    CHECK_EQ(e.m_calls[1].level, 50);
    CHECK_EQ(e.m_calls[2].size, 100);
    CHECK_EQ(e.m_calls[2].level, 50);
    CHECK_EQ(e.getEnvStatus(), ENV_SUSTAIN);
}

/** @brief A smoothed exponential release may finish early once 90% of the release time has passed. */
TEST(envelope, smooth_exponential_release_ends_early)
{
    for (int smooth = 0; smooth < 2; smooth++) {
        for (int stop = 85; stop <= 95; stop += 10) {
            RecEnv e;
            setupGen(e);
            e.setMax(1000000);
            e.m_bSmooth = smooth;
            e.setAttack(0.0f);
            e.setDecay(0.0f);
            e.setSustain(1.0f);
            e.setRelease(0.1f);
            e.envelopeOn();
            float buf[100];
            int processed = 0;
            e.processSignalSub(1, buf, 0, 0, &processed);
            REQUIRE(e.getEnvStatus() == ENV_SUSTAIN);
            e.envelopeOff();
            e.processSignalSub(stop, buf, 0, 0, &processed);
            int expected = (smooth && stop >= 90) ? ENV_OFF : ENV_RELEASE;
            CHECK_EQ(e.getEnvStatus(), expected);
            CHECK_EQ(e.m_releaseEnds, expected == ENV_OFF ? 1 : 0);
        }
    }
}

/** @brief processSignal() without an input device (and not a generator) reports silence. */
TEST(envelope, process_without_input_is_silent)
{
    RecEnv e;
    e.envelopeOn();
    float buf[8];
    int processed = 0;
    CHECK_EQ(e.processSignal(8, buf, &processed), 8);
    CHECK_EQ(processed, 1);
    CHECK(e.m_calls.empty());
}

/** @brief An idle envelope is silent unless m_bProcessIdle is set, which processes with level 0. */
TEST(envelope, idle_processing_flag)
{
    RecEnv e;
    e.m_bNoInput = 1;
    float buf[8];
    int processed = 0;
    e.processSignal(8, buf, &processed);
    CHECK_EQ(processed, 1);
    CHECK(e.m_calls.empty());
    e.m_bProcessIdle = 1;
    for (int i = 0; i < 8; i++) buf[i] = 5.0f;
    e.processSignal(8, buf, &processed);
    REQUIRE(e.m_calls.size() == 1);
    CHECK_EQ(e.m_calls[0].level, 0);
    CHECK_EQ(e.m_calls[0].size, 8);
    CHECK_EQ(processed, 0);
    CHECK_CLOSE(buf[0], 0, 0);
}

/** @brief processSignal() pulls the input first and passes its silence straight through. */
TEST(envelope, process_pulls_input_and_propagates_silence)
{
    RecEnv e;
    ConstSource src;
    e.inputDeviceIs(&src);
    e.envelopeOn();
    src.m_silent = 1;
    float buf[16];
    int processed = 0;
    CHECK_EQ(e.processSignal(16, buf, &processed), 16);
    CHECK_EQ(src.m_calls, 1);
    CHECK_EQ(processed, 1);
    CHECK(e.m_calls.empty());
    CHECK_EQ(e.m_envStatus, ENV_ATTACK);
    src.m_silent = 0;
    CHECK_EQ(e.processSignal(16, buf, &processed), 16);
    CHECK_EQ(src.m_calls, 2);
    CHECK_EQ(processed, 0);
    CHECK(!e.m_calls.empty());
}

/** @brief A silent start followed by written samples has its silent part zero-filled. */
TEST(envelope, silent_head_is_zero_filled)
{
    RecEnv e;
    setupGen(e);
    e.m_silentBelow = 50;
    e.setAttack(0.1f);
    e.envelopeOn();
    float buf[100];
    for (int i = 0; i < 100; i++) buf[i] = 7.0f;
    int processed = 1;
    e.processSignal(100, buf, &processed);
    CHECK_EQ(processed, 0);
    for (int i = 0; i < 49; i++) CHECK_CLOSE(buf[i], 0, 0);
    for (int i = 49; i < 100; i++) CHECK_CLOSE(buf[i], i + 1, 0);
}

/** @brief A silent tail after written samples (release finished mid-block) is zero-filled. */
TEST(envelope, silent_tail_is_zero_filled)
{
    RecEnv e;
    setupGen(e);
    e.m_silentBelow = 1;
    e.setAttack(0.01f);
    e.setDecay(0.01f);
    e.setSustain(1.0f);
    e.setRelease(0.05f);
    e.setDecayType(ENV_CURVE_LINEAR);
    e.envelopeOn();
    runTrace(e, 30, 30);
    e.envelopeOff();
    float buf[100];
    for (int i = 0; i < 100; i++) buf[i] = 7.0f;
    int processed = 1;
    e.processSignal(100, buf, &processed);
    CHECK_EQ(processed, 0);
    CHECK_EQ(e.getEnvStatus(), ENV_OFF);
    CHECK(buf[0] > 0);
    for (int i = 50; i < 100; i++) CHECK_CLOSE(buf[i], 0, 0);
    for (int i = 0; i < 100; i++) CHECK(buf[i] != 7.0f);
}

/** @brief When every block is silent the buffer is left untouched and the silence flag is set. */
TEST(envelope, all_silent_leaves_buffer_untouched)
{
    RecEnv e;
    setupGen(e);
    e.m_silentBelow = 1000;
    e.setAttack(0.1f);
    e.envelopeOn();
    float buf[64];
    for (int i = 0; i < 64; i++) buf[i] = 7.0f;
    int processed = 0;
    e.processSignal(64, buf, &processed);
    CHECK_EQ(processed, 1);
    CHECK(e.m_calls.size() > 1);
    for (int i = 0; i < 64; i++) CHECK_CLOSE(buf[i], 7.0, 0);
}

/** @brief postProcessSignal() runs once per processSignal() only when m_bPostProcess is set. */
TEST(envelope, post_process_flag)
{
    RecEnv e;
    setupGen(e);
    e.m_bProcessIdle = 1;
    float buf[16];
    int processed;
    e.processSignal(16, buf, &processed);
    CHECK_EQ(e.m_postCalls, 0);
    e.m_bPostProcess = 1;
    e.processSignal(16, buf, &processed);
    e.processSignal(16, buf, &processed);
    CHECK_EQ(e.m_postCalls, 2);
}

/** @brief Enabled LFOs pass their values; a constant LFO does not split the block. */
TEST(envelope, lfo_values_are_passed)
{
    RecEnv e;
    setupGen(e);
    e.m_bProcessIdle = 1;
    CLFO l1, l2;
    l1.setSamplingFreq(44100);
    l2.setSamplingFreq(44100);
    l1.setAmount(300, 300);
    l2.setAmount(-200, -200);
    e.setLfo(0, &l1);
    e.setLfo(1, &l2);
    CHECK(e.m_pLfo[0] == &l1);
    CHECK(e.m_pLfo[1] == &l2);
    float buf[64];
    int processed;
    e.processSignal(64, buf, &processed);
    REQUIRE(e.m_calls.size() == 1);
    CHECK_EQ(e.m_calls[0].lfo1, 0);    // not enabled yet
    CHECK_EQ(e.m_calls[0].lfo2, 0);
    e.enableLfo(0, 1);
    e.enableLfo(1, 1);
    e.m_calls.clear();
    e.processSignal(64, buf, &processed);
    REQUIRE(e.m_calls.size() == 1);
    CHECK_EQ(e.m_calls[0].lfo1, 300);
    CHECK_EQ(e.m_calls[0].lfo2, -200);
    CHECK_EQ(e.m_calls[0].size, 64);
    e.enableLfo(1, 0);
    e.m_calls.clear();
    e.processSignal(64, buf, &processed);
    REQUIRE(e.m_calls.size() == 1);
    CHECK_EQ(e.m_calls[0].lfo2, 0);
}

/** @brief A moving LFO splits the buffer into blocks of constant value that cover it exactly. */
TEST(envelope, moving_lfo_splits_blocks)
{
    RecEnv e;
    setupGen(e);
    e.m_bProcessIdle = 1;
    CLFO l;
    l.setSamplingFreq(44100);
    l.setFreqMain(10.0f);          // 4410-sample cycle
    l.setAmount(0, 1000);
    e.setLfo(0, &l);
    e.enableLfo(0, 1);
    std::vector<float> buf(1024);
    int processed;
    e.processSignal(1024, &buf[0], &processed);
    REQUIRE(e.m_calls.size() > 1);
    int total = 0;
    for (size_t i = 0; i < e.m_calls.size(); i++) {
        total += e.m_calls[i].size;
        CHECK(e.m_calls[i].lfo1 >= 0 && e.m_calls[i].lfo1 <= 1000);
        if (i > 0) CHECK(e.m_calls[i].lfo1 < e.m_calls[i - 1].lfo1);   // saw falls
    }
    CHECK_EQ(total, 1024);
}

/** @brief A sampling rate change rescales the position inside a running decay or release segment. */
TEST(envelope, sampling_rate_change_in_decay_and_release)
{
    RecEnv e;
    setupGen(e);
    e.setAttack(0.01f);
    e.setDecay(0.1f);
    e.setSustain(0.5f);
    e.setRelease(0.1f);
    e.setDecayType(ENV_CURVE_LINEAR);
    e.envelopeOn();
    float buf[64];
    int processed;
    e.processSignalSub(30, buf, 0, 0, &processed);
    REQUIRE(e.m_envStatus == ENV_DECAY);
    CHECK_EQ(e.m_curPos, 20);
    e.setSamplingFreq(2000);
    CHECK_EQ(e.m_decayLen, 200);
    CHECK_EQ(e.m_curPos, 40);
    e.envelopeOff();
    e.processSignalSub(50, buf, 0, 0, &processed);
    REQUIRE(e.m_envStatus == ENV_RELEASE);
    CHECK_EQ(e.m_releaseLen, 200);
    CHECK_EQ(e.m_curPos, 50);
    e.setSamplingFreq(500);
    CHECK_EQ(e.m_releaseLen, 50);
    CHECK_EQ(e.m_curPos, 12);   // 50 * 50 / 200
}

/** @brief Decay/release setters rescale the position while their segment runs; a zero-length attack restarts at 0. */
TEST(envelope, segment_setters_rescale_running_position)
{
    RecEnv e;
    setupGen(e);
    e.setAttack(0.01f);
    e.setDecay(0.1f);
    e.setSustain(0.5f);
    e.setRelease(0.1f);
    e.setDecayType(ENV_CURVE_LINEAR);
    e.envelopeOn();
    float buf[64];
    int processed;
    e.processSignalSub(30, buf, 0, 0, &processed);
    REQUIRE(e.m_envStatus == ENV_DECAY);
    e.setDecay(0.05f);
    CHECK_EQ(e.m_decayLen, 50);
    CHECK_EQ(e.m_curPos, 10);
    e.setAttack(0.5f);              // not attacking: position unchanged
    CHECK_EQ(e.m_curPos, 10);
    e.envelopeOff();
    e.processSignalSub(20, buf, 0, 0, &processed);
    REQUIRE(e.m_envStatus == ENV_RELEASE);
    e.setRelease(0.2f);
    CHECK_EQ(e.m_releaseLen, 200);
    CHECK_EQ(e.m_curPos, 40);

    // from a zero-length attack the position restarts at 0 (no division by the old length)
    RecEnv z;
    setupGen(z);
    z.setAttack(0.0f);
    z.envelopeOn();
    z.m_curPos = 5;
    z.setAttack(0.1f);
    CHECK_EQ(z.m_attackLen, 100);
    CHECK_EQ(z.m_curPos, 0);
    z.setAttack(0.0f);
    z.m_curPos = 5;
    z.setSamplingFreq(2000);
    CHECK_EQ(z.m_attackLen, 0);
    CHECK_EQ(z.m_curPos, 0);
    // the same for zero-length decay and release segments
    z.setDecay(0.0f);
    z.setRelease(0.0f);
    z.m_envStatus = ENV_DECAY;
    z.m_curPos = 5;
    z.setDecay(0.1f);
    CHECK_EQ(z.m_decayLen, 200);
    CHECK_EQ(z.m_curPos, 0);
    z.setDecay(0.0f);
    z.m_curPos = 5;
    z.setSamplingFreq(1000);
    CHECK_EQ(z.m_curPos, 0);
    z.m_envStatus = ENV_RELEASE;
    z.m_curPos = 5;
    z.setRelease(0.1f);
    CHECK_EQ(z.m_releaseLen, 100);
    CHECK_EQ(z.m_curPos, 0);
    z.setRelease(0.0f);
    z.m_curPos = 5;
    z.setSamplingFreq(2000);
    CHECK_EQ(z.m_curPos, 0);
}

/** @brief Attack: a silent block after written ones is zero-filled; the block as a whole is not silent. */
TEST(envelope, attack_silent_block_after_output_is_zeroed)
{
    RecEnv e;
    setupGen(e);
    e.setAttack(0.1f);              // 100 samples, 1-sample steps
    e.envelopeOn();
    int pat[2] = {0, 1};
    e.m_silentPattern.assign(pat, pat + 2);
    float buf[20];
    for (int i = 0; i < 20; i++) buf[i] = 7.0f;
    int processed = 1;
    e.processSignal(20, buf, &processed);
    CHECK_EQ(processed, 0);
    REQUIRE(e.m_calls.size() == 20);
    for (int k = 0; k < 20; k++) CHECK_CLOSE(buf[k], k % 2 ? 0.0 : k + 1.0, 0);
}

/** @brief Decay: silent blocks before the first written one are zero-filled afterwards, later silent ones directly. */
TEST(envelope, decay_silent_head_and_gaps_are_zeroed)
{
    RecEnv e;
    setupGen(e);
    e.setAttack(0.0f);
    e.setDecay(0.1f);
    e.setSustain(0.0f);
    e.setDecayType(ENV_CURVE_LINEAR);
    e.envelopeOn();
    int pat[4] = {1, 1, 1, 0};
    e.m_silentPattern.assign(pat, pat + 4);
    float buf[8];
    for (int i = 0; i < 8; i++) buf[i] = 7.0f;
    int processed = 1;
    e.processSignal(8, buf, &processed);
    CHECK_EQ(processed, 0);
    REQUIRE(e.m_calls.size() == 8);
    const float expect[8] = {0, 0, 0, 96, 0, 0, 0, 92};
    for (int k = 0; k < 8; k++) CHECK_CLOSE(buf[k], expect[k], 0);
}

/** @brief Sustain remainder written after a silent decay in the same block: the decay part is zero-filled. */
TEST(envelope, sustain_after_silent_decay_zeroes_head)
{
    RecEnv e;
    setupGen(e);
    e.setAttack(0.0f);
    e.setDecay(0.004f);
    e.setSustain(0.5f);
    e.setDecayType(ENV_CURVE_LINEAR);
    e.envelopeOn();
    int pat[5] = {1, 1, 1, 1, 0};
    e.m_silentPattern.assign(pat, pat + 5);
    float buf[10];
    for (int i = 0; i < 10; i++) buf[i] = 7.0f;
    int processed = 1;
    e.processSignal(10, buf, &processed);
    CHECK_EQ(processed, 0);
    CHECK_EQ(e.getEnvStatus(), ENV_SUSTAIN);
    REQUIRE(e.m_calls.size() == 5);
    CHECK_EQ(e.m_calls[4].size, 6);
    for (int k = 0; k < 4; k++) CHECK_CLOSE(buf[k], 0, 0);
    for (int k = 4; k < 10; k++) CHECK_CLOSE(buf[k], 50, 0);
}

/** @brief With LFO2 splitting the block, a silent piece after a written one is zero-filled. */
TEST(envelope, lfo2_split_silent_piece_is_zeroed)
{
    RecEnv e;
    setupGen(e);
    e.m_envStatus = ENV_SUSTAIN;    // full sustain: level 100
    CLFO l;
    l.setSamplingFreq(44100);
    l.setFreqMain(10.0f);
    l.setAmount(0, 1000);
    e.setLfo(1, &l);
    e.enableLfo(1, 1);
    int pat[2] = {0, 1};
    e.m_silentPattern.assign(pat, pat + 2);
    std::vector<float> buf(1024, 7.0f);
    int processed = 1;
    e.processSignal(1024, &buf[0], &processed);
    CHECK_EQ(processed, 0);
    REQUIRE(e.m_calls.size() > 2);
    int pos = 0, ok = 1;
    for (size_t k = 0; k < e.m_calls.size(); k++) {
        for (int i = 0; i < e.m_calls[k].size; i++) {
            if (buf[pos + i] != (k % 2 ? 0.0f : 100.0f)) ok = 0;
        }
        pos += e.m_calls[k].size;
    }
    CHECK(ok);
    CHECK_EQ(pos, 1024);
}

/**
 * @brief A negative peak level (negative m_max) is reached at the first attack step (level clamp)
 *        and, the level being "at the peak", the attack ends with the block (characterization).
 */
TEST(envelope, negative_peak_attack_clamps_to_peak)
{
    RecEnv e;
    setupGen(e);
    e.setMax(-100);
    CHECK_EQ(e.m_peakLevel, -100);
    e.setAttack(0.01f);
    e.envelopeOn();
    float buf[5];
    int processed;
    e.processSignal(5, buf, &processed);
    REQUIRE(e.m_calls.size() == 5);
    for (int k = 0; k < 5; k++) CHECK_EQ(e.m_calls[k].level, -100);
    CHECK_EQ(e.m_envStatus, ENV_SUSTAIN);
    CHECK_EQ(e.m_curLevel, -100);
}

/**
 * @brief The linear decay level is clamped at the sustain level: with levels beyond float
 *        precision (m_max 2^24) the rounded step would undershoot it by one (characterization).
 */
TEST(envelope, linear_decay_clamps_rounding_undershoot)
{
    RecEnv e;
    setupGen(e);
    e.setMax(16777216);
    e.setSustain(2e-7f);
    CHECK_EQ(e.m_peakLevel, 16777216);
    CHECK_EQ(e.m_sustainLevel, 3);
    e.setAttack(0.0f);
    e.setDecay(0.003f);
    REQUIRE(e.m_decayLen == 3);
    e.setDecayType(ENV_CURVE_LINEAR);
    // the unclamped last step: 16777216 - (long)(3 * (16777213.f / 3.f)) = 2, below the sustain level
    float rate = (float)16777213 / 3.0f;
    CHECK_EQ(16777216 - (long)(3 * rate), 2);
    e.envelopeOn();
    float buf[3];
    int processed;
    e.processSignal(3, buf, &processed);
    REQUIRE(e.m_calls.size() == 3);
    CHECK(e.m_calls[1].level > 3);
    CHECK_EQ(e.m_calls[2].level, 3);
    CHECK_EQ(e.getEnvStatus(), ENV_SUSTAIN);
}
