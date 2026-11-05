/**
 * @file test_efxs.cpp
 * @brief Unit tests for CEffector (techpop/efxs.h): the effect type switch, enable/bypass, and
 *        the distribution of control 1, control 2 and the level to each effect (checked by
 *        comparing the effector's output with a standalone effect configured by hand).
 */
#include "test.h"

#include <math.h>
#include <string.h>

#include "common.h"
#include "efxs.h"

namespace {

const double kPi = 3.14159265358979323846;
const int kFrames = 2048;

/**
 * @brief Gives the tests access to the protected CEffector members.
 */
class EffProbe : public CEffector {
public:
    CSignalDevice* effect() const { return m_pEffect; }
    CSignalDevice* dist() { return &m_dist; }
    CSignalDevice* comp() { return &m_comp; }
    CSignalDevice* ring() { return &m_ringMod; }
    CSignalDevice* deci() { return &m_decimator; }
    CSignalDevice* phaser() { return &m_phaser; }
    int cont1() const { return m_cont1; }
    int cont2() const { return m_cont2; }
    int levelVal() const { return m_level; }
    int stereoFlag() const { return m_bStereo; }

    /**
     * @brief Forces a downward phaser sweep (the initial direction is uninitialised):
     *        a range below the current 200 Hz makes the next block reverse the direction.
     */
    void primePhaser()
    {
        m_phaser.setRange(50.0f, 100.0f);
        float z[16];
        memset(z, 0, sizeof z);
        int processed = 0;
        m_phaser.processSignal(16, z, &processed);
    }
};

/**
 * @brief Stereo source: a two-tone test signal, or silence.
 */
class Source : public CSignalDevice {
public:
    long pos;
    int silent;
    Source() : pos(0), silent(0) {}

    static float sample(long n, int ch)
    {
        return (float)(0.6 * sin(2 * kPi * 220.0 * n / 44100) + (ch ? 0.3 : 0.2) * sin(2 * kPi * 1700.0 * n / 44100));
    }

    virtual int processSignal(int size, float* pBuf, int* pProcessed)
    {
        if (silent) { *pProcessed = 1; return size; }
        for (int i = 0; i < size; i++, pos++) pBuf[i] = sample(pos, 0);
        *pProcessed = 0;
        return size;
    }

    virtual int processSignalW(int size, float* pBuf, int* pProcessed)
    {
        if (silent) { *pProcessed = 1; return size; }
        for (int i = 0; i < size; i++, pos++) {
            pBuf[i * 2] = sample(pos, 0);
            pBuf[i * 2 + 1] = sample(pos, 1);
        }
        *pProcessed = 0;
        return size;
    }
};

/**
 * @brief Runs kFrames frames through the effector (stereo) and through a reference effect
 *        processing the same input in place; returns the largest difference.
 */
double compareStereo(CEffector& eff, CSignalDevice& ref)
{
    Source src;
    eff.inputDeviceIs(&src);
    double maxDiff = 0;
    float a[512], b[512];
    for (int done = 0; done < kFrames; done += 256) {
        int pa = 0, pb = 0;
        eff.processSignalW(256, a, &pa);
        for (int i = 0; i < 256; i++) {
            b[i * 2] = Source::sample(done + i, 0);
            b[i * 2 + 1] = Source::sample(done + i, 1);
        }
        ref.processSignalW(256, b, &pb);
        for (int i = 0; i < 512; i++) {
            double d = fabs(a[i] - b[i]);
            maxDiff = d > maxDiff ? d : maxDiff;
        }
    }
    return maxDiff;
}

/**
 * @brief Mono counterpart of compareStereo().
 */
double compareMono(CEffector& eff, CSignalDevice& ref)
{
    Source src;
    eff.inputDeviceIs(&src);
    double maxDiff = 0;
    float a[256], b[256];
    for (int done = 0; done < kFrames; done += 256) {
        int pa = 0, pb = 0;
        eff.processSignal(256, a, &pa);
        for (int i = 0; i < 256; i++) b[i] = Source::sample(done + i, 0);
        ref.processSignal(256, b, &pb);
        for (int i = 0; i < 256; i++) {
            double d = fabs(a[i] - b[i]);
            maxDiff = d > maxDiff ? d : maxDiff;
        }
    }
    return maxDiff;
}

/**
 * @brief Returns how much the effector changes the source signal (sum of |out - in|, stereo).
 */
double changeAmount(CEffector& eff)
{
    Source src;
    eff.inputDeviceIs(&src);
    double sum = 0;
    float a[512];
    for (int done = 0; done < kFrames; done += 256) {
        int p = 0;
        eff.processSignalW(256, a, &p);
        for (int i = 0; i < 256; i++) {
            sum += fabs(a[i * 2] - Source::sample(done + i, 0));
            sum += fabs(a[i * 2 + 1] - Source::sample(done + i, 1));
        }
    }
    return sum;
}

/**
 * @brief Sets type, both controls and the level on an effector.
 */
void configure(CEffector& e, int type, int c1, int c2, int lv)
{
    e.setType(type);
    e.setCont1(c1);
    e.setCont2(c2);
    e.setLevel(lv);
}

}  // namespace

/** @brief Constructor: enabled, distortion 1, controls and level at 0. */
TEST(efxs, defaults)
{
    EffProbe e;
    CHECK_EQ(e.getEnable(), 1);
    CHECK_EQ(e.getType(), EFX_DIST1);
    CHECK(e.effect() == e.dist());
    CHECK_EQ(e.cont1(), 0);
    CHECK_EQ(e.cont2(), 0);
    CHECK_EQ(e.levelVal(), 0);
    e.setStereoMode(1);
    CHECK_EQ(e.stereoFlag(), 1);
}

/** @brief The type switch selects the matching effect; unknown types keep the effect but store the type. */
TEST(efxs, type_switch)
{
    EffProbe e;
    struct { int type; CSignalDevice* dev; } map[10] = {
        {EFX_DIST1, e.dist()}, {EFX_DIST2, e.dist()}, {EFX_DIST3, e.dist()},
        {EFX_DECIMATOR, e.deci()}, {EFX_RINGMOD, e.ring()}, {EFX_COMPRESSOR, e.comp()},
        {EFX_PHASER1, e.phaser()}, {EFX_PHASER2, e.phaser()}, {EFX_PHASER3, e.phaser()},
        {EFX_PHASER4, e.phaser()},
    };
    for (int k = 0; k < 10; k++) {
        e.setType(map[k].type);
        CHECK_EQ(e.getType(), map[k].type);
        CHECK(e.effect() == map[k].dev);
    }
    e.setType(EFX_RINGMOD);
    e.setType(42);
    CHECK_EQ(e.getType(), 42);
    CHECK(e.effect() == e.ring());
    e.setType(-1);
    CHECK(e.effect() == e.ring());
}

/** @brief Disabled, the effector passes the input through unchanged (mono and stereo). */
TEST(efxs, disabled_is_passthrough)
{
    EffProbe e;
    configure(e, EFX_RINGMOD, 100, 0, 127);
    e.setEnable(0);
    CHECK_EQ(e.getEnable(), 0);
    CHECK(changeAmount(e) == 0.0);
    Source src;
    e.inputDeviceIs(&src);
    float m[256];
    int p = 0;
    CHECK_EQ(e.processSignal(256, m, &p), 256);
    CHECK(m[100] == Source::sample(100, 0));
}

/** @brief Each distortion type equals a standalone CDistortion with drive = cont1, tone = cont2, level. */
TEST(efxs, distortion_routing)
{
    int types[3] = {EFX_DIST1, EFX_DIST2, EFX_DIST3};
    for (int t = 0; t < 3; t++) {
        EffProbe e;
        configure(e, types[t], 90, 70, 100);
        CDistortion ref;
        ref.setDrive(90);
        ref.setType(t);
        ref.setDrive(90);
        ref.setSpeakerTone(70);
        ref.setLevel(100);
        CHECK(compareStereo(e, ref) < 1e-6);
        EffProbe em;
        configure(em, types[t], 90, 70, 100);
        CDistortion rm;
        rm.setDrive(90);
        rm.setType(t);
        rm.setDrive(90);
        rm.setSpeakerTone(70);
        rm.setLevel(100);
        CHECK(compareMono(em, rm) < 1e-6);
    }
}

/** @brief Compressor: depth = cont1/127, attack = cont2/127, level = level. */
TEST(efxs, compressor_routing)
{
    EffProbe e;
    configure(e, EFX_COMPRESSOR, 64, 30, 90);
    CCompressor ref;
    ref.setDepth(64 / 127.0f);
    ref.setAttack(30 / 127.0f);
    ref.setLevel(90);
    CHECK(compareStereo(e, ref) < 1e-6);
    CHECK(changeAmount(e) > 1.0);
}

/** @brief Ring modulator: frequency 8000^(cont1/127) Hz, wet level/127. */
TEST(efxs, ringmod_routing)
{
    EffProbe e;
    configure(e, EFX_RINGMOD, 100, 0, 80);
    CRingMod ref;
    ref.setFreq((float)fpow(8000.0, 100 / 127.0f));
    ref.setLevel(80 / 127.0f);
    CHECK(compareStereo(e, ref) < 1e-6);
}

/** @brief Decimator: hold length cont1, bit reduction cont2/8, wet level/127. */
TEST(efxs, decimator_routing)
{
    EffProbe e;
    configure(e, EFX_DECIMATOR, 5, 60, 127);
    CDecimator ref;
    ref.setSample(5);
    ref.setBit(60 / 8);
    ref.setLevel(1.0f);
    CHECK(compareStereo(e, ref) < 1e-7);
    EffProbe m;
    configure(m, EFX_DECIMATOR, 5, 60, 127);
    CDecimator rm;
    rm.setSample(5);
    rm.setBit(7);
    rm.setLevel(1.0f);
    CHECK(compareMono(m, rm) < 1e-7);
}

/** @brief Phaser: range from cont1, rate from cont2, feedback (level-64)/64*0.99, stages 1/2/4/6 by type. */
TEST(efxs, phaser_routing)
{
    int types[4] = {EFX_PHASER1, EFX_PHASER2, EFX_PHASER3, EFX_PHASER4};
    int stages[4] = {1, 2, 4, 6};
    double prevChange = -1;
    for (int k = 0; k < 4; k++) {
        EffProbe e;
        e.primePhaser();
        configure(e, types[k], 80, 50, 100);

        CPhaser ref;
        ref.setRate(0.01f);                 // as left by CEffector's constructor (cont2 = 0)
        ref.setRange(1000.0f, 1000.0f);     // cont1 = 0
        ref.setRange(50.0f, 100.0f);        // same priming as primePhaser()
        float z[16];
        memset(z, 0, sizeof z);
        int processed = 0;
        ref.processSignal(16, z, &processed);
        float minFreq = pow(10.0, (127 - 80) / 127.0f) * 100.0;
        float maxFreq = pow(10.0, 80 / 127.0f) * 1000.0;
        ref.setRange(minFreq, maxFreq);
        ref.setRate(fpow(1000.0, 50 / 127.0f) * 0.02);
        ref.setFeedback((100 - 64) / 64.0f * 0.99f);
        ref.setStage(stages[k]);
        CHECK(compareStereo(e, ref) < 1e-5);

        EffProbe c;
        c.primePhaser();
        configure(c, types[k], 80, 50, 100);
        double ch = changeAmount(c);
        CHECK(ch > 1.0);
        CHECK(ch != prevChange);   // the stage count changes the result
        prevChange = ch;
    }
}

/** @brief Silent input passes through the active effect's silence handling (compressor: untouched, flag kept). */
TEST(efxs, silent_input)
{
    EffProbe e;
    configure(e, EFX_COMPRESSOR, 64, 64, 64);
    Source src;
    src.silent = 1;
    e.inputDeviceIs(&src);
    float b[512];
    for (int i = 0; i < 512; i++) b[i] = 4.0f;
    int p = 0;
    CHECK_EQ(e.processSignalW(256, b, &p), 256);
    CHECK_EQ(p, 1);
    CHECK(b[0] == 4.0f && b[511] == 4.0f);
}

/** @brief A sampling rate change is passed on to every effect. */
TEST(efxs, sampling_rate_propagates)
{
    EffProbe e;
    e.setSamplingFreq(48000);
    CHECK_EQ(e.dist()->m_samplingFreq, 48000);
    CHECK_EQ(e.comp()->m_samplingFreq, 48000);
    CHECK_EQ(e.ring()->m_samplingFreq, 48000);
    CHECK_EQ(e.deci()->m_samplingFreq, 48000);
    CHECK_EQ(e.phaser()->m_samplingFreq, 48000);
}
