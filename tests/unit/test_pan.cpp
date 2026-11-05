/**
 * @file test_pan.cpp
 * @brief Unit tests for techpop/pan.h (CPan): the balance law, the LFO auto-pan gain curve, its
 *        smoothing, two summed LFOs, LFO enable via changeLFOSetting() and silent blocks.
 */
#include "test.h"

#include <math.h>
#include <string.h>

#include "pan.h"
#include "lfo.h"

namespace {

/**
 * @brief Gives the tests access to the protected CPan state (the smoothed gains start uninitialised).
 */
class PanProbe : public CPan {
public:
    void setCurGains(float l, float r) { m_curGainL = l; m_curGainR = r; }
    float curL() const { return m_curGainL; }
    float curR() const { return m_curGainR; }
    float coef() const { return m_smoothCoef; }
    int lfoEnabled(int no) const { return m_bLfoEnable[no]; }
};

/**
 * @brief Source producing a constant on both channels, or silence.
 */
class ConstSource : public CSignalDevice {
public:
    float l, r;
    int silent;
    ConstSource() : l(1.0f), r(1.0f), silent(0) {}
    virtual int processSignal(int size, float* pBuf, int* pProcessed) { *pProcessed = 1; return size; }
    virtual int processSignalW(int size, float* pBuf, int* pProcessed)
    {
        if (silent) { *pProcessed = 1; return size; }
        for (int i = 0; i < size; i++) {
            pBuf[i * 2] = l;
            pBuf[i * 2 + 1] = r;
        }
        *pProcessed = 0;
        return size;
    }
};

/**
 * @brief Makes an LFO with a constant output value (amount range lo == hi).
 */
void constLfo(CLFO& lfo, int v)
{
    lfo.setSamplingFreq(44100);
    lfo.setAmount(v, v);
}

/**
 * @brief Runs @p frames frames through the pan and returns the last frame.
 */
tagStereoData runPan(CPan& p, int frames)
{
    float buf[512];
    int processed = 0;
    for (int done = 0; done < frames; done += 256) p.processSignalW(256, buf, &processed);
    tagStereoData last = {buf[510], buf[511]};
    return last;
}

/**
 * @brief Expected LFO pan gain of the left channel for an LFO value (right: use -value).
 */
double panCurve(int lfoVal)
{
    double v = lfoVal / 64.0;
    if (v < 0) return pow(v + 1.0, 3.0);
    return 0.5 * pow(v - 1.0, 3.0) + 1.5;
}

}  // namespace

/** @brief Balance: centre leaves both channels, positive attenuates L linearly, negative attenuates R. */
TEST(pan, balance_law)
{
    float bal[5] = {0.0f, 0.5f, 1.0f, -0.25f, -1.0f};
    double gl[5] = {1.0, 0.5, 0.0, 1.0, 1.0};
    double gr[5] = {1.0, 1.0, 1.0, 0.75, 0.0};
    for (int k = 0; k < 5; k++) {
        PanProbe p;
        CLFO a, b;
        p.setLfo(&a, &b);
        ConstSource src;
        src.l = 0.8f;
        src.r = -0.4f;
        p.inputDeviceIs(&src);
        p.setBalance(bal[k]);
        float buf[512];
        int processed = 0;
        CHECK_EQ(p.processSignalW(256, buf, &processed), 256);
        CHECK_CLOSE(buf[0], 0.8 * gl[k], 1e-7);
        CHECK_CLOSE(buf[1], -0.4 * gr[k], 1e-7);
        CHECK_CLOSE(buf[510], 0.8 * gl[k], 1e-7);
        CHECK_CLOSE(buf[511], -0.4 * gr[k], 1e-7);
    }
}

/** @brief Silent input blocks are passed through untouched; mono processing does nothing. */
TEST(pan, silent_and_mono_untouched)
{
    PanProbe p;
    CLFO a, b;
    p.setLfo(&a, &b);
    ConstSource src;
    src.silent = 1;
    p.inputDeviceIs(&src);
    p.setBalance(0.5f);
    float buf[512];
    for (int i = 0; i < 512; i++) buf[i] = 3.0f;
    int processed = 0;
    p.processSignalW(256, buf, &processed);
    CHECK_EQ(processed, 1);
    CHECK(buf[0] == 3.0f && buf[511] == 3.0f);
    processed = 7;
    CHECK_EQ(p.processSignal(256, buf, &processed), 256);
    CHECK_EQ(processed, 7);
    CHECK(buf[5] == 3.0f);
}

/** @brief The smoothing coefficient is a one-pole with a 2 ms half-life. */
TEST(pan, smoothing_coefficient)
{
    PanProbe p;
    CHECK_CLOSE(p.coef(), 1.0 - pow(0.5, 1.0 / 88.2), 1e-7);
    p.setSamplingFreq(96000);
    CHECK_CLOSE(p.coef(), 1.0 - pow(0.5, 1.0 / 192.0), 1e-7);
}

/** @brief LFO pan curve: value 0 is unity, +64 gives L 1.5 / R 0, -64 gives L 0 / R 1.5, +-32 in between. */
TEST(pan, lfo_pan_curve)
{
    int vals[5] = {0, 64, -64, 32, -32};
    for (int k = 0; k < 5; k++) {
        PanProbe p;
        CLFO a, b;
        constLfo(a, vals[k]);
        p.setLfo(&a, &b);
        p.setLfoEnable(0, 1);
        p.setCurGains(1.0f, 1.0f);
        ConstSource src;
        p.inputDeviceIs(&src);
        tagStereoData last = runPan(p, 4096);
        CHECK_CLOSE(last.l, panCurve(vals[k]), 1e-4);
        CHECK_CLOSE(last.r, panCurve(-vals[k]), 1e-4);
    }
    CHECK_CLOSE(panCurve(32), 1.4375, 1e-12);
    CHECK_CLOSE(panCurve(-32), 0.125, 1e-12);
}

/** @brief The LFO gains approach their target with the 2 ms half-life (halfway after about 88 samples). */
TEST(pan, lfo_gain_smoothing)
{
    PanProbe p;
    CLFO a, b;
    constLfo(a, 64);
    p.setLfo(&a, &b);
    p.setLfoEnable(0, 1);
    p.setCurGains(1.0f, 1.0f);
    ConstSource src;
    p.inputDeviceIs(&src);
    float buf[2 * 88];
    int processed = 0;
    p.processSignalW(88, buf, &processed);
    // after 88 of 88.2 samples: just under halfway from 1 to 1.5 (L) and 1 to 0 (R)
    CHECK_CLOSE(p.curL(), 1.25, 0.002);
    CHECK_CLOSE(p.curR(), 0.5, 0.004);
    CHECK(buf[0] > 1.0f && buf[0] < 1.01f);
    CHECK(buf[2 * 87] == p.curL());
}

/** @brief Two LFOs targeting the pan are summed; the balance gains scale the LFO targets. */
TEST(pan, two_lfos_sum_and_balance)
{
    PanProbe p;
    CLFO a, b;
    constLfo(a, 16);
    constLfo(b, 16);
    p.setLfo(&a, &b);
    p.setLfoEnable(0, 1);
    p.setLfoEnable(1, 1);
    p.setCurGains(1.0f, 1.0f);
    p.setBalance(0.5f);
    ConstSource src;
    p.inputDeviceIs(&src);
    tagStereoData last = runPan(p, 4096);
    CHECK_CLOSE(last.l, panCurve(32) * 0.5, 1e-4);
    CHECK_CLOSE(last.r, panCurve(-32) * 1.0, 1e-4);

    // only the second LFO enabled
    PanProbe q;
    q.setLfo(&a, &b);
    q.setLfoEnable(1, 1);
    q.setCurGains(1.0f, 1.0f);
    q.inputDeviceIs(&src);
    last = runPan(q, 4096);
    CHECK_CLOSE(last.l, panCurve(16), 1e-4);
    CHECK_CLOSE(last.r, panCurve(-16), 1e-4);
}

/** @brief A moving (triangle) LFO keeps the pan gains inside 0..1.5 and actually moves them. */
TEST(pan, moving_lfo_bounded)
{
    PanProbe p;
    CLFO a, b;
    a.setSamplingFreq(44100);
    a.setFreqMain(50.0f);
    a.setFreq(50.0f);
    a.setType(LFO_TRIANGLE);
    a.setAmount(-64, 64);
    p.setLfo(&a, &b);
    p.setLfoEnable(0, 1);
    p.setCurGains(1.0f, 1.0f);
    ConstSource src;
    p.inputDeviceIs(&src);
    float buf[512];
    double minL = 9, maxL = -9, minR = 9, maxR = -9;
    for (int blk = 0; blk < 40; blk++) {
        int processed = 0;
        CHECK_EQ(p.processSignalW(256, buf, &processed), 256);
        a.incCurTime(256);
        for (int i = 0; i < 256; i++) {
            minL = buf[i * 2] < minL ? buf[i * 2] : minL;
            maxL = buf[i * 2] > maxL ? buf[i * 2] : maxL;
            minR = buf[i * 2 + 1] < minR ? buf[i * 2 + 1] : minR;
            maxR = buf[i * 2 + 1] > maxR ? buf[i * 2 + 1] : maxR;
        }
    }
    CHECK(minL >= 0.0 && maxL <= 1.5);
    CHECK(minR >= 0.0 && maxR <= 1.5);
    CHECK(maxL - minL > 0.5);
    CHECK(maxR - minR > 0.5);
}

/**
 * @brief With both LFOs enabled and the second one moving, the block is split where LFO2 changes;
 *        the gains follow the sum of both values.
 */
TEST(pan, two_lfos_second_moving)
{
    PanProbe p;
    CLFO a, b;
    constLfo(a, 0);
    b.setSamplingFreq(44100);
    b.setFreqMain(10.0f);
    b.setFreq(10.0f);
    b.setType(LFO_SQUARE);
    b.setAmount(-32, 32);
    p.setLfo(&a, &b);
    p.setLfoEnable(0, 1);
    p.setLfoEnable(1, 1);
    p.setCurGains(1.0f, 1.0f);
    ConstSource src;
    p.inputDeviceIs(&src);
    // square at 10 Hz: 2205 samples at +32, then 2205 at -32
    static float buf[2 * 4410];
    int processed = 0;
    CHECK_EQ(p.processSignalW(4410, buf, &processed), 4410);
    CHECK_CLOSE(buf[2 * 2200], panCurve(32), 1e-4);
    CHECK_CLOSE(buf[2 * 2200 + 1], panCurve(-32), 1e-4);
    CHECK_CLOSE(buf[2 * 4400], panCurve(-32), 1e-4);
    CHECK_CLOSE(buf[2 * 4400 + 1], panCurve(32), 1e-4);
    CHECK(buf[2 * 2210] < buf[2 * 2200]);   // the gains start moving at the LFO edge
    for (int i = 0; i < 2 * 4410; i++) CHECK(buf[i] >= 0.0f && buf[i] <= 1.5f);
}

/** @brief changeLFOSetting() enables the modulation only for an enabled LFO whose destination is the pan; setLfo() disables both. */
TEST(pan, change_lfo_setting)
{
    PanProbe p;
    CLFO a, b;
    p.setLfo(&a, &b);
    CHECK_EQ(p.lfoEnabled(0), 0);
    CHECK_EQ(p.lfoEnabled(1), 0);
    a.m_bEnable = 1;
    a.m_destination = LFO_DEST_PAN;
    p.changeLFOSetting(0);
    CHECK_EQ(p.lfoEnabled(0), 1);
    b.m_bEnable = 1;
    b.m_destination = LFO_DEST_FILTER;
    p.changeLFOSetting(1);
    CHECK_EQ(p.lfoEnabled(1), 0);
    a.m_bEnable = 0;
    p.changeLFOSetting(0);
    CHECK_EQ(p.lfoEnabled(0), 0);
    p.setLfoEnable(1, 1);
    p.setLfo(&a, &b);
    CHECK_EQ(p.lfoEnabled(1), 0);
}
