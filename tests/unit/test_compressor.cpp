/**
 * @file test_compressor.cpp
 * @brief Unit tests for techpop/compressor.h: EnvelopeDetector, SimpleComp and the CCompressor
 *        device (threshold/make-up mapping, gain reduction above threshold, silence handling).
 */
#include "test.h"

#include <math.h>
#include <string.h>

#include "compressor.h"

namespace {

/**
 * @brief Gives the tests read access to the protected CCompressor state.
 */
class CompProbe : public CCompressor {
public:
    float makeup() const { return m_makeup; }
    float level() const { return m_level; }
    SimpleComp& comp(int ch) { return m_comp[ch]; }
    int type() const { return m_type; }
    float release() const { return m_release; }
    float ratio() const { return m_ratio; }
};

/**
 * @brief Gives the tests access to the envelope of a SimpleComp.
 */
class SimpleProbe : public SimpleComp {
public:
    float env() const { return m_env; }
    float threshLin() const { return m_thresh; }
};

}  // namespace

/** @brief The follower coefficient is exp(-1 / (ms * fs / 1000)) and a step reaches 1 - 1/e after one time constant. */
TEST(envdetector, coefficient_and_step_response)
{
    EnvelopeDetector e(10.0f, 44100.0f);
    CHECK_CLOSE(e.getTc(), 10.0, 1e-7);
    CHECK_CLOSE(e.getSampleRate(), 44100.0, 1e-3);
    float state = 0.0f;
    for (int i = 0; i < 441; i++) e.run(1.0f, state);
    CHECK_CLOSE(state, 1.0 - exp(-1.0), 1e-4);
    e.setTc(1.0f);
    state = 0.0f;
    e.run(1.0f, state);
    CHECK_CLOSE(state, 1.0 - exp(-1.0 / 44.1), 1e-6);
    e.setSampleRate(1000.0f);
    state = 0.0f;
    e.run(1.0f, state);
    CHECK_CLOSE(state, 1.0 - exp(-1.0), 1e-6);
}

/** @brief SimpleComp defaults: 0 dB threshold (linear 1), ratio 1, attack 10 ms, release 100 ms. */
TEST(simplecomp, defaults_and_setters)
{
    SimpleProbe c;
    CHECK_CLOSE(c.getThresh(), 0.0, 0);
    CHECK_CLOSE(c.threshLin(), 1.0, 1e-7);
    CHECK_CLOSE(c.getRatio(), 1.0, 0);
    CHECK_CLOSE(c.getAttack(), 10.0, 1e-6);
    CHECK_CLOSE(c.getRelease(), 100.0, 1e-6);
    c.setThresh(-20.0f);
    CHECK_CLOSE(c.threshLin(), 0.1, 1e-6);
    c.setRatio(4.0f);
    CHECK_CLOSE(c.getRatio(), 4.0, 0);
    c.setAttack(3.0f);
    c.setRelease(50.0f);
    CHECK_CLOSE(c.getAttack(), 3.0, 1e-6);
    CHECK_CLOSE(c.getRelease(), 50.0, 1e-6);
}

/** @brief Below the threshold the signal passes unchanged (no gain reduction). */
TEST(simplecomp, unity_below_threshold)
{
    SimpleProbe c;
    c.setThresh(-6.0f);
    for (int i = 0; i < 1000; i++) {
        float x = (float)sin(i * 0.1) * 0.4f;
        CHECK_CLOSE(c.process(x), x, 1e-6);
    }
    CHECK(c.env() == 0.0f);
}

/** @brief A constant input above threshold is reduced so the output settles at the threshold level. */
TEST(simplecomp, reduces_to_threshold_above)
{
    SimpleProbe c;
    c.setThresh(-20.0f);   // 0.1
    c.setAttack(1.0f);
    float y = 0;
    for (int i = 0; i < 4410; i++) y = c.process(0.8f);
    CHECK_CLOSE(c.env(), 0.7, 1e-4);
    CHECK_CLOSE(y, 0.1, 1e-4);
    // negative input: same reduction, sign kept
    for (int i = 0; i < 4410; i++) y = c.process(-0.8f);
    CHECK_CLOSE(y, -0.1, 1e-4);
    // the gain reduction builds up gradually (attack): first sample barely reduced
    c.initRuntime();
    CHECK(c.env() == 0.0f);
    float first = c.process(0.8f);
    CHECK(first > 0.5f);
    CHECK(first < 0.8f);
}

/** @brief The ratio is stored only; it does not change the output. */
TEST(simplecomp, ratio_has_no_effect)
{
    SimpleProbe a, b;
    a.setThresh(-12.0f);
    b.setThresh(-12.0f);
    b.setRatio(10.0f);
    b.setRelease(1.0f);
    int same = 1;
    for (int i = 0; i < 2000; i++) {
        float x = (float)sin(i * 0.02);
        if (a.process(x) != b.process(x)) same = 0;
    }
    CHECK(same);
}

/** @brief setDepth(v): threshold -20-40v dB, make-up 10^(2v); setLevel maps 0..127 to -20..+10 dB; setAttack to 0.1*2000^sqrt(v) ms. */
TEST(compressor, parameter_mappings)
{
    CompProbe c;
    CHECK_CLOSE(c.comp(0).getThresh(), -10.0, 0);
    CHECK_CLOSE(c.comp(1).getRatio(), -1.0, 0);
    CHECK_CLOSE(c.comp(0).getAttack(), 1.0, 1e-6);
    c.setDepth(0.0f);
    CHECK_CLOSE(c.comp(0).getThresh(), -20.0, 1e-6);
    CHECK_CLOSE(c.makeup(), 1.0, 1e-6);
    CHECK_CLOSE(c.comp(1).getRatio(), 0.0, 0);
    c.setDepth(0.5f);
    CHECK_CLOSE(c.comp(1).getThresh(), -40.0, 1e-5);
    CHECK_CLOSE(c.makeup(), 10.0, 1e-4);
    c.setDepth(1.0f);
    CHECK_CLOSE(c.makeup(), 100.0, 1e-3);
    c.setLevel(0);
    CHECK_CLOSE(c.level(), 0.1, 1e-6);
    c.setLevel(127);
    CHECK_CLOSE(c.level(), pow(10.0, 0.5), 1e-5);
    c.setAttack(0.0f);
    CHECK_CLOSE(c.comp(0).getAttack(), 0.1, 1e-6);
    CHECK_CLOSE(c.comp(1).getRelease(), 0.1, 1e-6);
    c.setAttack(1.0f);
    CHECK_CLOSE(c.comp(1).getAttack(), 200.0, 1e-3);
    c.setAttack(0.25f);
    CHECK_CLOSE(c.comp(0).getAttack(), 0.1 * sqrt(2000.0), 1e-4);
    c.setType(3);
    c.setRelease(0.4f);
    c.setRatio(0.6f);
    CHECK_EQ(c.type(), 3);
    CHECK(c.release() == 0.4f);
    CHECK(c.ratio() == 0.6f);
    CHECK_CLOSE(c.comp(0).getRelease(), 0.1 * sqrt(2000.0), 1e-4);  // setRelease() does not change it
}

/** @brief A loud input settles at threshold * make-up * level = 0.1 * level, whatever the depth. */
TEST(compressor, gain_reduction_above_threshold)
{
    float depths[3] = {0.0f, 0.3f, 1.0f};
    for (int k = 0; k < 3; k++) {
        CompProbe c;
        c.setDepth(depths[k]);
        c.setLevel(127);
        c.setAttack(0.0f);
        float buf[2048];
        for (int i = 0; i < 2048; i++) buf[i] = 0.9f;
        int processed = 0;
        c.processSignal(2048, buf, &processed);
        CHECK_CLOSE(buf[2047], 0.1 * c.level(), 1e-4);
    }
}

/** @brief A quiet input below threshold just gets the make-up and level gains. */
TEST(compressor, quiet_signal_gets_makeup)
{
    CompProbe c;
    c.setDepth(0.25f);   // thresh -30 dB (0.0316), make-up 10^0.5
    c.setLevel(85);
    float buf[64];
    for (int i = 0; i < 64; i++) buf[i] = 0.01f * (float)sin(i * 0.3);
    float in[64];
    memcpy(in, buf, sizeof in);
    int processed = 0;
    c.processSignal(64, buf, &processed);
    double g = c.makeup() * c.level();
    int ok = 1;
    for (int i = 0; i < 64; i++) if (fabs(buf[i] - in[i] * g) > 1e-6) ok = 0;
    CHECK(ok);
}

/** @brief Stereo: the channels are compressed independently. */
TEST(compressor, stereo_channels_independent)
{
    CompProbe c;
    c.setDepth(0.5f);
    c.setLevel(127);
    c.setAttack(0.0f);
    float buf[2 * 2048];
    for (int i = 0; i < 2048; i++) {
        buf[i * 2] = 0.9f;
        buf[i * 2 + 1] = 0.001f;
    }
    int processed = 0;
    CHECK_EQ(c.processSignalW(2048, buf, &processed), 2048);
    CHECK_CLOSE(buf[2 * 2047], 0.1 * c.level(), 1e-4);
    CHECK_CLOSE(buf[2 * 2047 + 1], 0.001 * c.makeup() * c.level(), 1e-6);
}

/** @brief A silent block is left untouched and resets the envelope (no residual gain reduction). */
TEST(compressor, silent_block_resets_envelope)
{
    CompProbe c;
    c.setDepth(0.5f);
    c.setLevel(127);
    c.setAttack(1.0f);
    float buf[512];
    for (int i = 0; i < 512; i++) buf[i] = 0.9f;
    int processed = 0;
    c.processSignal(512, buf, &processed);
    float mid[512];
    for (int i = 0; i < 512; i++) mid[i] = buf[i] = 7.0f;
    processed = 1;
    CHECK_EQ(c.processSignal(512, buf, &processed), 512);
    CHECK_EQ(processed, 1);
    CHECK(memcmp(mid, buf, sizeof buf) == 0);
    // fresh envelope: the next loud sample is treated like the first one of a new compressor
    CompProbe fresh;
    fresh.setDepth(0.5f);
    fresh.setLevel(127);
    fresh.setAttack(1.0f);
    float a = 0.9f, b = 0.9f;
    processed = 0;
    c.processSignal(1, &a, &processed);
    fresh.processSignal(1, &b, &processed);
    CHECK(a == b);
    // same for stereo
    processed = 1;
    float st[4] = {5, 6, 7, 8};
    c.processSignalW(2, st, &processed);
    CHECK(st[0] == 5 && st[3] == 8);
}

/** @brief A sampling rate change is passed on to the envelope followers. */
TEST(compressor, sampling_rate_change)
{
    CompProbe c;
    c.setSamplingFreq(96000);
    CHECK_CLOSE(c.comp(0).getRelease(), 1.0, 1e-6);
    c.setDepth(1.0f);
    c.setLevel(127);
    c.setAttack(0.0f);
    float buf[4096];
    for (int i = 0; i < 4096; i++) buf[i] = 0.5f;
    int processed = 0;
    c.processSignal(4096, buf, &processed);
    CHECK_CLOSE(buf[4095], 0.1 * c.level(), 1e-4);
}

/** @brief Characterization: first samples of a compressed step (attack 0.5 -> about 4.5 ms). */
TEST(compressor, characterization_step)
{
    CompProbe c;
    c.setDepth(0.5f);
    c.setLevel(64);
    c.setAttack(0.5f);
    float buf[6] = {0.5f, 0.5f, 0.5f, -0.5f, 0.2f, 0.0f};
    int processed = 0;
    c.processSignal(6, buf, &processed);
    // reference values from the current (release-matching) build
    const double expect[6] = {2.71073985, 2.5844152, 2.46946144, -2.36440349, 0.930528581, 0.0};
    for (int i = 0; i < 6; i++) CHECK_CLOSE(buf[i], expect[i], 1e-6);
}
