/**
 * @file test_chorus.cpp
 * @brief Unit tests for techpop/chorus.h (CChorus): parameter derived values, the echo at the
 *        delay time, feedback, the three tap layouts, modulation bounds, the delay effect input,
 *        bypass and the silence tail.
 */
#include "test.h"

#include <math.h>
#include <string.h>
#include <vector>

#include "chorus.h"

namespace {

const double kPi = 3.14159265358979323846;

/**
 * @brief Stereo test signal source: plays interleaved samples, then zeros (or reports silence).
 */
class Source : public CSignalDevice {
public:
    std::vector<float> data;    ///< interleaved L/R
    int pos;                    ///< next frame
    int silentAfterEnd;         ///< report silence (*pProcessed = 1) after the data
    int forceSilent;            ///< report silence for every block

    Source() : pos(0), silentAfterEnd(0), forceSilent(0) {}

    int frames() const { return (int)data.size() / 2; }

    virtual int processSignal(int size, float* pBuf, int* pProcessed)
    {
        for (int i = 0; i < size; i++, pos++) pBuf[i] = pos < frames() ? data[pos * 2] : 0.0f;
        *pProcessed = 0;
        return size;
    }

    virtual int processSignalW(int size, float* pBuf, int* pProcessed)
    {
        if (forceSilent || (silentAfterEnd && pos >= frames())) {
            pos += size;
            *pProcessed = 1;
            return size;
        }
        for (int i = 0; i < size; i++, pos++) {
            pBuf[i * 2] = pos < frames() ? data[pos * 2] : 0.0f;
            pBuf[i * 2 + 1] = pos < frames() ? data[pos * 2 + 1] : 0.0f;
        }
        *pProcessed = 0;
        return size;
    }
};

/**
 * @brief Chorus wired to a source and an (initially silent) delay-effect output buffer.
 */
struct Rig {
    Source src;
    CChorus ch;
    float delaySig[512];
    int delayValid;

    Rig() : delayValid(0)
    {
        memset(delaySig, 0, sizeof delaySig);
        ch.inputDeviceIs(&src);
        ch.setDelaySigPointer(delaySig, &delayValid);
    }

    /**
     * @brief Runs @p frames frames in blocks of 256 and appends the output to @p out.
     */
    void run(int frames, std::vector<float>& out)
    {
        float buf[512];
        for (int done = 0; done < frames; done += 256) {
            int processed = 0;
            ch.processSignalW(256, buf, &processed);
            for (int i = 0; i < 512; i++) out.push_back(processed ? 0.0f : buf[i]);
        }
    }
};

/**
 * @brief Fills the source with a single impulse at frame 0 (amplitudes per channel).
 */
void impulse(Source& s, int frames, float l, float r)
{
    s.data.assign(frames * 2, 0.0f);
    s.data[0] = l;
    s.data[1] = r;
}

}  // namespace

/** @brief The constructor defaults and the derived sample counts / coefficients. */
TEST(chorus, defaults_and_derived_values)
{
    CChorus c;
    CHECK_EQ(c.m_delaySample, 882u);       // 0.02 s
    CHECK_EQ(c.m_depthSample, 441);        // depth 0.5
    CHECK_EQ(c.m_period, 44100);           // 1 Hz
    CHECK_CLOSE(c.m_oscCoef, 2 * sin(kPi / 44100), 1e-9);
    CHECK_CLOSE(c.m_depthCoef, 1.0, 0);
    CHECK_EQ(c.m_bFeedback, 0);
    CHECK_EQ(c.m_releaseTime, 2 * 882);    // no feedback: 2 passes
    CHECK_EQ(c.m_kind, CHORUS_KIND_STEREO);
    CHECK_EQ(c.m_bEnable, 1);
    CHECK_CLOSE(c.m_level, 0.0, 0);
    CHECK_EQ(c.m_writePos, (c.m_readPos + 882u) & CHORUS_BUFMASK);
    // m_step: period / depth = 100 < 300 -> 6
    CHECK_EQ(c.m_step, 6);
}

/** @brief Feedback sets the release time to (passes to -90 dB + 2) * delay samples. */
TEST(chorus, feedback_release_time)
{
    CChorus c;
    c.setFeedback(0.5f);
    CHECK_EQ(c.m_bFeedback, 1);
    CHECK_EQ(c.m_releaseTime, (15 + 2) * 882);
    c.setFeedback(-0.5f);
    CHECK_EQ(c.m_releaseTime, (15 + 2) * 882);
    c.setDelayTime(0.01f);
    CHECK_EQ(c.m_delaySample, 441u);
    CHECK_EQ(c.m_releaseTime, 17 * 441);
}

/** @brief At high rates the depth is scaled so the tap moves at most one sample per sample. */
TEST(chorus, depth_coef_limits_tap_speed)
{
    CChorus c;
    c.setDepth(1.0f);
    c.setRate(20.0f);
    CHECK_EQ(c.m_period, 2205);
    CHECK_CLOSE(c.m_depthCoef, 2205.0 / (882 * 2 * kPi), 1e-6);
    c.setRate(0.5f);
    CHECK_CLOSE(c.m_depthCoef, 1.0, 0);
    // period / depth = 88200 / 882 = 100 -> step 6; with a tiny depth the step grows
    c.setDepth(0.01f);
    CHECK_EQ(c.m_depthSample, 8);
    CHECK_EQ(c.m_step, 88200 / 8 / 50);
}

/** @brief setKind() accepts mono, stereo and ensemble; anything else selects stereo. */
TEST(chorus, kind_selection)
{
    CChorus c;
    c.setKind(CHORUS_KIND_MONO);
    CHECK_EQ(c.m_kind, CHORUS_KIND_MONO);
    c.setKind(CHORUS_KIND_ENSEMBLE);
    CHECK_EQ(c.m_kind, CHORUS_KIND_ENSEMBLE);
    c.setKind(3);
    CHECK_EQ(c.m_kind, CHORUS_KIND_STEREO);
    c.setKind(0);
    CHECK_EQ(c.m_kind, CHORUS_KIND_STEREO);
}

/** @brief A sampling rate change recomputes the delay and LFO period from the stored times. */
TEST(chorus, sampling_rate_change)
{
    CChorus c;
    c.setRate(2.0f);
    c.setSamplingFreq(48000);
    CHECK_EQ(c.m_delaySample, 960u);
    CHECK_EQ(c.m_period, 24000);
    CHECK_CLOSE(c.m_oscCoef, 2 * sin(2.0 * kPi / 48000), 1e-9);
}

/** @brief Mono processing is not supported: it only flags the buffer as silent. */
TEST(chorus, mono_marks_silent)
{
    CChorus c;
    float b[4] = {1, 2, 3, 4};
    int processed = 0;
    CHECK_EQ(c.processSignal(4, b, &processed), 4);
    CHECK_EQ(processed, 1);
    CHECK(b[0] == 1 && b[3] == 4);
}

/** @brief With level 0 nothing enters the line, so the dry signal passes through unchanged. */
TEST(chorus, level_zero_is_dry)
{
    Rig r;
    r.src.data.resize(1024 * 2);
    for (int i = 0; i < 1024; i++) {
        r.src.data[i * 2] = (float)sin(i * 0.05);
        r.src.data[i * 2 + 1] = (float)cos(i * 0.03);
    }
    std::vector<float> out;
    r.run(1024, out);
    int same = 1;
    for (int i = 0; i < 2048; i++) if (out[i] != r.src.data[i]) same = 0;
    CHECK(same);
}

/** @brief Depth 0, level 1: an impulse comes back exactly at the delay time (882 frames), per channel in stereo mode. */
TEST(chorus, echo_at_delay_time)
{
    Rig r;
    r.ch.setDepth(0.0f);
    r.ch.setLevel(1.0f);
    impulse(r.src, 2048, 1.0f, 0.0f);
    std::vector<float> out;
    r.run(2048, out);
    CHECK_CLOSE(out[0], 1.0, 0);
    CHECK_CLOSE(out[882 * 2], 1.0, 1e-6);
    CHECK_CLOSE(out[882 * 2 + 1], 0.0, 1e-6);   // stereo: no leak to R
    double other = 0;
    for (int i = 1; i < 2048; i++) {
        if (i == 882) continue;
        other += fabs(out[i * 2]) + fabs(out[i * 2 + 1]);
    }
    CHECK(other < 1e-6);
}

/** @brief Feedback repeats the echo every delay time, scaled by the feedback gain each pass. */
TEST(chorus, feedback_repeats)
{
    Rig r;
    r.ch.setDepth(0.0f);
    r.ch.setLevel(1.0f);
    r.ch.setFeedback(0.5f);
    impulse(r.src, 3072, 1.0f, 1.0f);
    std::vector<float> out;
    r.run(3072, out);
    CHECK_CLOSE(out[882 * 2], 1.0, 1e-6);
    CHECK_CLOSE(out[1764 * 2], 0.5, 1e-6);
    CHECK_CLOSE(out[2646 * 2 + 1], 0.25, 1e-6);
}

/** @brief Ensemble mode cross-mixes the taps: an impulse on L only comes back on both channels. */
TEST(chorus, ensemble_cross_mix)
{
    Rig r;
    r.ch.setDepth(0.0f);
    r.ch.setLevel(1.0f);
    r.ch.setKind(CHORUS_KIND_ENSEMBLE);
    impulse(r.src, 1024, 1.0f, 0.0f);
    std::vector<float> out;
    r.run(1024, out);
    CHECK_CLOSE(out[882 * 2], 1.0, 1e-5);       // 0.9 + 0.1
    CHECK_CLOSE(out[882 * 2 + 1], 1.0, 1e-5);   // 0.1 + 0.9

    Rig s;
    s.ch.setDepth(0.0f);
    s.ch.setLevel(1.0f);
    s.ch.setKind(CHORUS_KIND_MONO);
    impulse(s.src, 1024, 0.0f, 1.0f);
    std::vector<float> o2;
    s.run(1024, o2);
    CHECK_CLOSE(o2[882 * 2], 0.0, 1e-6);         // mono kind: same LFO phase, no cross mix
    CHECK_CLOSE(o2[882 * 2 + 1], 1.0, 1e-6);
}

/** @brief With modulation the echo lands within delay +/- depth samples and the output stays bounded. */
TEST(chorus, modulated_echo_window_and_bounds)
{
    for (int kind = 1; kind <= 4; kind *= 2) {
        Rig r;
        r.ch.setKind(kind);
        r.ch.setLevel(1.0f);
        r.ch.setRate(3.0f);
        r.ch.setDepth(0.5f);     // 441 samples
        impulse(r.src, 2048, 1.0f, 1.0f);
        std::vector<float> out;
        r.run(2048, out);
        double inside = 0, outside = 0;
        for (int i = 1; i < 2048; i++) {
            double e = fabs(out[i * 2]) + fabs(out[i * 2 + 1]);
            if (i >= 882 - 443 && i <= 882 + 443) inside += e;
            else outside += e;
        }
        CHECK(inside > 0.5);
        CHECK(outside < 1e-6);
    }

    Rig r;
    r.ch.setLevel(1.0f);
    r.ch.setFeedback(0.5f);
    r.ch.setRate(2.0f);
    r.ch.setDepth(0.8f);
    r.src.data.resize(8192 * 2);
    for (int i = 0; i < 8192; i++) r.src.data[i * 2] = r.src.data[i * 2 + 1] = (float)sin(2 * kPi * 440.0 * i / 44100);
    std::vector<float> out;
    r.run(8192, out);
    double peak = 0, diff = 0;
    for (int i = 0; i < 8192 * 2; i++) {
        peak = fabs(out[i]) > peak ? fabs(out[i]) : peak;
        diff += fabs(out[i] - r.src.data[i]);
    }
    CHECK(peak < 3.01);   // dry 1 + tap <= 1 / (1 - 0.5)
    CHECK(diff > 100.0);  // the signal is clearly changed
}

/** @brief The delay effect's output is written into the line (delayed by the chorus), not added to the dry output. */
TEST(chorus, delay_signal_goes_into_line)
{
    Rig r;
    r.ch.setDepth(0.0f);
    r.ch.setLevel(0.0f);
    r.src.data.assign(1024 * 2, 0.0f);
    std::vector<float> out;
    float buf[512];
    for (int blk = 0; blk < 4; blk++) {
        memset(r.delaySig, 0, sizeof r.delaySig);
        r.delayValid = 1;
        if (blk == 0) r.delaySig[0] = 0.5f;
        int processed = 0;
        r.ch.processSignalW(256, buf, &processed);
        CHECK_EQ(processed, 0);
        for (int i = 0; i < 512; i++) out.push_back(buf[i]);
    }
    CHECK_CLOSE(out[0], 0.0, 0);
    CHECK_CLOSE(out[882 * 2], 0.5, 1e-6);
}

/** @brief Disabled: the delay output is mixed straight into the buffer (or copied into a silent one). */
TEST(chorus, disabled_mixes_delay_output)
{
    Rig r;
    r.ch.setEnable(0);
    r.ch.setLevel(1.0f);
    r.src.data.assign(256 * 2, 0.25f);
    for (int i = 0; i < 512; i++) r.delaySig[i] = 0.5f;
    r.delayValid = 1;
    float buf[512];
    int processed = 0;
    r.ch.processSignalW(256, buf, &processed);
    CHECK_EQ(processed, 0);
    CHECK_CLOSE(buf[0], 0.75, 0);
    CHECK_CLOSE(buf[511], 0.75, 0);
    // silent input: the delay output is copied
    r.src.forceSilent = 1;
    for (int i = 0; i < 512; i++) buf[i] = 9.0f;
    r.ch.processSignalW(256, buf, &processed);
    CHECK_EQ(processed, 0);
    CHECK_CLOSE(buf[3], 0.5, 0);
    // both silent: stays silent
    r.delayValid = 0;
    r.ch.processSignalW(256, buf, &processed);
    CHECK_EQ(processed, 1);
}

/** @brief Re-enabling clears the delay line, so an old echo does not come back. */
TEST(chorus, reenable_clears_line)
{
    Rig r;
    r.ch.setDepth(0.0f);
    r.ch.setLevel(1.0f);
    impulse(r.src, 2048, 1.0f, 1.0f);
    std::vector<float> out;
    r.run(256, out);
    r.ch.setEnable(0);
    r.ch.setEnable(1);
    r.run(1024, out);
    CHECK_CLOSE(out[882 * 2], 0.0, 0);
}

/** @brief Silent input: idle when fresh; after signal the tail is rendered for m_releaseTime frames, then idle. */
TEST(chorus, silence_tail)
{
    Rig r;
    r.ch.setDepth(0.0f);
    r.ch.setLevel(1.0f);
    r.src.forceSilent = 1;
    float buf[512];
    for (int i = 0; i < 512; i++) buf[i] = 9.0f;
    int processed = 0;
    r.ch.processSignalW(256, buf, &processed);
    CHECK_EQ(processed, 1);
    CHECK(buf[0] == 9.0f);

    r.src.forceSilent = 0;
    impulse(r.src, 256, 1.0f, 1.0f);
    r.src.pos = 0;
    r.src.silentAfterEnd = 1;
    r.ch.processSignalW(256, buf, &processed);
    CHECK_EQ(processed, 0);
    // release 1764: entry counts 0..1536 render (7 blocks), 1792 is idle
    int rendered = 0;
    float echo = 0;
    for (int blk = 1; blk <= 8; blk++) {
        r.ch.processSignalW(256, buf, &processed);
        if (!processed) {
            rendered++;
            if (blk == 3) echo = buf[(882 - 768) * 2];
        }
    }
    CHECK_EQ(rendered, 7);
    CHECK_CLOSE(echo, 1.0, 1e-6);   // the echo comes out during the tail (on top of the 1e-9 guard)
}
