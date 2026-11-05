/**
 * @file test_delay.cpp
 * @brief Unit tests for techpop/delay.h (CDelay): delay length computation (rounding, time
 *        balance, clamping), dry/wet levels, echo position and feedback for the stereo, cross and
 *        ping-pong types, insertion filters, delay time glide, enable/reset and the silence tail.
 */
#include "test.h"

#include <math.h>
#include <string.h>
#include <vector>

#include "delay.h"

namespace {

/**
 * @brief Stereo test signal source: plays interleaved samples, then zeros (or reports silence).
 */
class Source : public CSignalDevice {
public:
    std::vector<float> data;    ///< interleaved L/R
    int pos;                    ///< next frame
    int silentAfterEnd;         ///< report silence (*pProcessed = 1) after the data

    Source() : pos(0), silentAfterEnd(0) {}

    int frames() const { return (int)data.size() / 2; }

    virtual int processSignal(int size, float* pBuf, int* pProcessed)
    {
        *pProcessed = 1;
        return size;
    }

    virtual int processSignalW(int size, float* pBuf, int* pProcessed)
    {
        if (silentAfterEnd && pos >= frames()) {
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
 * @brief Insertion filter that halves the signal (counts its calls).
 */
class Halver : public CSignalDevice {
public:
    int calls;
    Halver() : calls(0) {}
    virtual int processSignal(int size, float* pBuf, int* pProcessed) { return size; }
    virtual int processSignalWForced(int size, float* pBuf, int* pProcessed)
    {
        calls++;
        for (int i = 0; i < size * 2; i++) pBuf[i] *= 0.5f;
        return size;
    }
};

/**
 * @brief A delay wired to a source, with helpers to run blocks and collect dry and wet output.
 */
struct Rig {
    Source src;
    CDelay dl;
    std::vector<float> dry;
    std::vector<float> wet;
    std::vector<int> outFlag;

    /**
     * @brief Sets up a 1000-sample delay without time balance, no feedback, dry and wet at 1.
     */
    Rig()
    {
        dl.inputDeviceIs(&src);
        dl.setTimeBalance(0.0f);
        dl.setDelaySample(1000);
        dl.setFeedback(0.0f);
        dl.setDelayLevel(64);
    }

    /**
     * @brief Runs @p frames frames in blocks of 256; silent results are stored as zeros.
     *
     * @return The silence flag of the last block.
     */
    int run(int frames)
    {
        float buf[512];
        int processed = 0;
        for (int done = 0; done < frames; done += 256) {
            processed = 0;
            dl.processSignalW(256, buf, &processed);
            for (int i = 0; i < 512; i++) {
                dry.push_back(processed ? 0.0f : buf[i]);
                wet.push_back(dl.m_bOutput ? dl.m_pOut[i] : 0.0f);
            }
            outFlag.push_back(dl.m_bOutput);
        }
        return processed;
    }
};

/**
 * @brief Fills the source with a single impulse at frame 0.
 */
void impulse(Source& s, int frames, float l, float r)
{
    s.data.assign(frames * 2, 0.0f);
    s.data[0] = l;
    s.data[1] = r;
}

/**
 * @brief Sum of |wet| over all frames except the listed ones (both channels).
 */
double wetElsewhere(const std::vector<float>& wet, const int* skip, int nSkip)
{
    double sum = 0;
    for (size_t f = 0; f < wet.size() / 2; f++) {
        int s = 0;
        for (int k = 0; k < nSkip; k++) if ((int)f == skip[k]) s = 1;
        if (!s) sum += fabs(wet[f * 2]) + fabs(wet[f * 2 + 1]);
    }
    return sum;
}

}  // namespace

/** @brief Constructor defaults: stereo type, enabled, feedback 0.7, no insertion filters, write position 0. */
TEST(delay, defaults)
{
    CDelay d;
    CHECK_EQ(d.m_type, DELAY_TYPE_STEREO);
    CHECK_EQ(d.m_bEnable, 1);
    CHECK_CLOSE(d.m_feedback, 0.7, 1e-7);
    CHECK_EQ(d.m_bufSize, DELAY_BUFSIZE);
    CHECK_EQ(d.m_writePos, 0u);
    CHECK_EQ(d.m_bSmoothing, 0);
    CHECK_EQ(d.m_delaySample, 4410000);   // 100 s, clamped to the line below
    for (int i = 0; i < 4; i++) CHECK(d.m_pInsFilter[i] == NULL);
    CHECK(d.m_pBufR == d.m_pBuf + DELAY_BUFSIZE + 4);
}

/** @brief setDelayLevel() follows calcDryWet(): 64 = both 1, 0 = dry only, 127 = wet only, linear in between. */
TEST(delay, dry_wet_levels)
{
    CDelay d;
    d.setDelayLevel(64);
    CHECK(d.m_dry == 1.0f && d.m_wet == 1.0f);
    d.setDelayLevel(0);
    CHECK(d.m_dry == 1.0f && d.m_wet == 0.0f);
    d.setDelayLevel(127);
    CHECK(d.m_dry == 0.0f && d.m_wet == 1.0f);
    d.setDelayLevel(32);
    CHECK_CLOSE(d.m_wet, 31 / 63.0, 1e-7);
    CHECK(d.m_dry == 1.0f);
    d.setDelayLevel(96);
    CHECK_CLOSE(d.m_dry, 31 / 63.0, 1e-7);
    CHECK(d.m_wet == 1.0f);
    d.setMixDelay(10);   // no-op
    CHECK_CLOSE(d.m_dry, 31 / 63.0, 1e-7);
}

/** @brief Delay lengths: rounded down to 4 samples, time balance on L (positive) or R (negative), clamped. */
TEST(delay, delay_length_computation)
{
    CDelay d;
    d.setTimeBalance(0.0f);
    d.setDelaySample(1003);
    CHECK_EQ(d.m_delayL, 1000);
    CHECK_EQ(d.m_delayR, 1000);
    d.setTimeBalance(0.01f);
    CHECK_EQ(d.m_delayL, 1441);
    CHECK_EQ(d.m_delayR, 1000);
    d.setTimeBalance(-0.01f);
    CHECK_EQ(d.m_delayL, 1000);
    CHECK_EQ(d.m_delayR, 1441);
    d.setTimeBalance(0.0f);
    d.setDelaySample(0);
    CHECK_EQ(d.m_delayL, 4);            // 0.1 ms at 44.1 kHz
    d.setDelaySample(200000);
    CHECK_EQ(d.m_delayR, DELAY_BUFSIZE - 256);
    d.setDelayTime(0.5f);
    CHECK_EQ(d.m_delaySample, 22050);
    CHECK_EQ(d.m_delayL, 22048);
    // not running: the change takes effect immediately
    CHECK_EQ(d.m_bSmoothing, 0);
    CHECK_CLOSE(d.m_curDelayL, 22048.0, 0);
    d.setSamplingFreq(48000);
    CHECK_EQ(d.m_delaySample, 24000);
    CHECK_CLOSE(d.m_smoothCoef, 1.0 - pow(0.5, 1.0 / (48000 * 0.15)), 1e-7);
}

/** @brief Feedback is clamped to 0..0.999; the release time is (repeats to -90 dB + 2) * longest delay, doubled for ping-pong. */
TEST(delay, feedback_and_release_time)
{
    CDelay d;
    d.setTimeBalance(0.0f);
    d.setDelaySample(1000);
    d.setFeedback(1.5f);
    CHECK_CLOSE(d.m_feedback, 0.999, 1e-7);
    d.setFeedback(-1.0f);
    CHECK(d.m_feedback == 0.0f);
    CHECK_EQ(d.m_releaseTime, 2000);
    d.setFeedback(0.5f);
    CHECK_EQ(d.m_releaseTime, 17000);
    d.setType(DELAY_TYPE_PINGPONG);
    CHECK_EQ(d.m_releaseTime, 34000);
    d.setType(DELAY_TYPE_CROSS);
    d.setTimeBalance(0.01f);
    CHECK_EQ(d.m_releaseTime, 17 * 1441);
}

/** @brief Stereo: an impulse comes out of the wet buffer exactly at the delay time, per channel; the dry signal is untouched. */
TEST(delay, echo_at_delay_offset)
{
    Rig r;
    impulse(r.src, 2048, 1.0f, 0.5f);
    r.run(2048);
    CHECK(r.dry[0] == 1.0f && r.dry[1] == 0.5f);
    CHECK(r.wet[1000 * 2] == 1.0f);
    CHECK(r.wet[1000 * 2 + 1] == 0.5f);
    int skip[1] = {1000};
    CHECK(wetElsewhere(r.wet, skip, 1) == 0.0);
    CHECK_EQ(r.outFlag[0], 1);
}

/** @brief Feedback repeats the echo every delay time, scaled by the feedback each time. */
TEST(delay, feedback_repeats)
{
    Rig r;
    r.dl.setFeedback(0.5f);
    impulse(r.src, 3328, 1.0f, -1.0f);
    r.run(3328);
    CHECK_CLOSE(r.wet[1000 * 2], 1.0, 0);
    CHECK_CLOSE(r.wet[2000 * 2], 0.5, 0);
    CHECK_CLOSE(r.wet[3000 * 2 + 1], -0.25, 0);
    int skip[3] = {1000, 2000, 3000};
    CHECK(wetElsewhere(r.wet, skip, 3) == 0.0);
}

/** @brief Time balance delays the left channel by the extra offset. */
TEST(delay, time_balance_offsets_left)
{
    Rig r;
    r.dl.setTimeBalance(0.01f);
    impulse(r.src, 2048, 1.0f, 1.0f);
    r.run(2048);
    CHECK(r.wet[1000 * 2 + 1] == 1.0f);
    CHECK(r.wet[1441 * 2] == 1.0f);
    CHECK(r.wet[1000 * 2] == 0.0f);
}

/** @brief Cross type: the input goes to the opposite channel and the feedback crosses back. */
TEST(delay, cross_feedback)
{
    Rig r;
    r.dl.setType(DELAY_TYPE_CROSS);
    r.dl.setFeedback(0.5f);
    impulse(r.src, 2304, 1.0f, 0.0f);
    r.run(2304);
    CHECK_CLOSE(r.wet[1000 * 2], 0.0, 0);
    CHECK_CLOSE(r.wet[1000 * 2 + 1], 1.0, 0);
    CHECK_CLOSE(r.wet[2000 * 2], 0.5, 0);
    CHECK_CLOSE(r.wet[2000 * 2 + 1], 0.0, 0);
}

/** @brief Ping-pong: the mono sum appears on L after delayL, then on R after delayR, then back on L scaled by the feedback. */
TEST(delay, ping_pong)
{
    Rig r;
    r.dl.setType(DELAY_TYPE_PINGPONG);
    r.dl.setFeedback(0.5f);
    impulse(r.src, 3328, 1.0f, 0.0f);
    r.run(3328);
    CHECK_CLOSE(r.wet[1000 * 2], 0.5, 0);       // (L + R) / 2
    CHECK_CLOSE(r.wet[1000 * 2 + 1], 0.0, 0);
    CHECK_CLOSE(r.wet[2000 * 2 + 1], 0.5, 0);
    CHECK_CLOSE(r.wet[2000 * 2], 0.0, 0);
    CHECK_CLOSE(r.wet[3000 * 2], 0.25, 0);
    int skip[3] = {1000, 2000, 3000};
    CHECK(wetElsewhere(r.wet, skip, 3) == 0.0);
}

/** @brief The dry level scales the buffer and the wet level scales the delayed output. */
TEST(delay, dry_wet_scaling_applied)
{
    Rig r;
    r.dl.setDelayLevel(127);   // dry 0, wet 1
    impulse(r.src, 1280, 1.0f, 1.0f);
    r.run(1280);
    CHECK(r.dry[0] == 0.0f);
    CHECK(r.wet[2000] == 1.0f);

    Rig s;
    s.dl.setDelayLevel(32);    // dry 1, wet 31/63
    impulse(s.src, 1280, 1.0f, 1.0f);
    s.run(1280);
    CHECK(s.dry[0] == 1.0f);
    CHECK_CLOSE(s.wet[2000], 31 / 63.0, 1e-7);
}

/** @brief Insertion filters process the delayed signal before it is output and fed back. */
TEST(delay, insertion_filter_in_loop)
{
    Rig r;
    Halver h;
    r.dl.setInsertionFilter(NULL, &h, NULL, NULL);
    r.dl.setFeedback(0.5f);
    impulse(r.src, 2304, 1.0f, 1.0f);
    r.run(2304);
    CHECK(h.calls > 0);
    CHECK_CLOSE(r.wet[1000 * 2], 0.5, 0);
    CHECK_CLOSE(r.wet[2000 * 2], 0.125, 0);   // fed back 0.5 * 0.5, filtered again
}

/** @brief Without an input device the block is flagged silent. */
TEST(delay, no_input_is_silent)
{
    CDelay d;
    float b[8];
    int processed = 0;
    CHECK_EQ(d.processSignalW(4, b, &processed), 4);
    CHECK_EQ(processed, 1);
    processed = 0;
    CHECK_EQ(d.processSignal(4, b, &processed), 4);
    CHECK_EQ(processed, 0);
}

/** @brief Disabled: no wet output, input untouched; re-enabling clears the lines and rewinds the write position. */
TEST(delay, enable_and_reset)
{
    Rig r;
    impulse(r.src, 768, 1.0f, 1.0f);
    r.run(256);
    CHECK_EQ(r.dl.m_writePos, 256u);
    r.dl.setEnable(0);
    r.run(256);
    CHECK_EQ(r.outFlag[1], 0);
    CHECK_EQ(r.dl.m_writePos, 256u);
    r.dl.setEnable(1);
    CHECK_EQ(r.dl.m_writePos, 0u);
    r.run(1024);
    // the impulse written before the reset never comes back
    double sum = 0;
    for (size_t i = 0; i < r.wet.size(); i++) sum += fabs(r.wet[i]);
    CHECK(sum == 0.0);

    Rig s;
    impulse(s.src, 512, 1.0f, 1.0f);
    s.run(256);
    s.dl.resetPointers();
    s.run(1024);
    sum = 0;
    for (size_t i = 0; i < s.wet.size(); i++) sum += fabs(s.wet[i]);
    CHECK(sum == 0.0);
}

/** @brief Silent input: idle when fresh; after signal the tail is rendered for m_releaseTime frames (with the echo), then the output stops. */
TEST(delay, silence_tail)
{
    Rig fresh;
    fresh.src.silentAfterEnd = 1;
    CHECK_EQ(fresh.run(256), 1);
    CHECK_EQ(fresh.outFlag[0], 0);

    Rig r;
    r.src.silentAfterEnd = 1;
    impulse(r.src, 256, 1.0f, 1.0f);
    r.run(256);
    // release 2000: entry counts 0..1792 render (8 blocks), 2048 stops
    int flags = 0;
    for (int blk = 1; blk <= 10; blk++) {
        int p = r.run(256);
        if (blk <= 8) {
            CHECK_EQ(p, 0);
            CHECK_EQ(r.outFlag[blk], 1);
        } else {
            CHECK_EQ(p, 1);
            CHECK_EQ(r.outFlag[blk], 0);
        }
        flags++;
    }
    CHECK_EQ(flags, 10);
    CHECK(r.wet[1000 * 2] == 1.0f);   // echo rendered during the tail
    CHECK(r.dry[300 * 2] == 0.0f);    // silent blocks are zero-filled
}

/** @brief Changing the delay while running glides the read position smoothly to the new length. */
TEST(delay, delay_time_glide)
{
    Rig r;
    r.src.data.assign(2 * 256, 0.1f);
    r.run(256);
    r.dl.setDelaySample(2000);
    CHECK_EQ(r.dl.m_bSmoothing, 1);
    CHECK_CLOSE(r.dl.m_curDelayL, 1000.0, 0);
    r.run(256);
    double c1 = r.dl.m_curDelayL;
    CHECK(c1 > 1000.0 && c1 < 1100.0);
    // half-way after 150 ms
    r.run(6400);
    double c2 = r.dl.m_curDelayL;
    CHECK(c2 > c1);
    CHECK(c2 > 1400.0 && c2 < 1600.0);
    r.run(100000);
    CHECK_EQ(r.dl.m_bSmoothing, 0);
    CHECK_CLOSE(r.dl.m_curDelayL, 2000.0, 0);
    CHECK_CLOSE(r.dl.m_curDelayR, 2000.0, 0);
}

/**
 * @brief Ping-pong delay time change: the read position glides (one-pole) towards the new length,
 *        an impulse written at the change is echoed between the old and the new delay, and the
 *        insertion filters run on the gliding output.
 */
TEST(delay, ping_pong_delay_time_glide)
{
    Rig r;
    Halver h;
    r.dl.setType(DELAY_TYPE_PINGPONG);
    r.dl.setInsertionFilter(&h, NULL, NULL, NULL);
    r.src.data.assign(2 * 4096, 0.0f);
    r.src.data[256 * 2] = 1.0f;     // impulse on the left at frame 256
    r.run(256);
    r.dl.setDelaySample(2000);
    CHECK_EQ(r.dl.m_bSmoothing, 1);
    CHECK_CLOSE(r.dl.m_curDelayL, 1000.0, 0);
    r.run(256);
    double c1 = r.dl.m_curDelayL;
    CHECK(c1 > 1000.0 && c1 < 1100.0);
    CHECK_CLOSE(c1, r.dl.m_curDelayR, 0);
    r.run(3584);
    CHECK(h.calls > 0);
    double c2 = r.dl.m_curDelayL;
    CHECK(c2 > c1 && c2 < 2000.0);
    // the echo ((L + R) / 2 = 0.5, halved by the insertion filter) lands after 1000 + glide
    int peakAt = -1;
    double peak = 0, sum = 0;
    int bad = 0;
    for (size_t f = 0; f < r.wet.size() / 2; f++) {
        double l = r.wet[f * 2], rr = r.wet[f * 2 + 1];
        if (!(l == l) || !(rr == rr) || fabs(l) > 1.0 || fabs(rr) > 1.0) bad++;
        if (fabs(l) > peak) { peak = fabs(l); peakAt = (int)f; }
        sum += fabs(l);
    }
    CHECK_EQ(bad, 0);
    CHECK(peakAt > 256 + 1050 && peakAt < 256 + 1250);
    // interpolated over neighbouring samples; the slower moving read position stretches it by
    // about 1 / (1 - d(delay)/dt) = 1.1
    CHECK(sum > 0.25 && sum < 0.31);
    r.run(100000);
    CHECK_EQ(r.dl.m_bSmoothing, 0);
    CHECK_CLOSE(r.dl.m_curDelayL, 2000.0, 0);
}

/**
 * @brief Gliding down to a delay shorter than the block cuts the blocks to the current delay
 *        (stereo and ping-pong): every frame is still rendered, the output stays finite and
 *        bounded, and the glide ends at the new length.
 */
TEST(delay, glide_below_block_length)
{
    for (int k = 0; k < 4; k++) {
        int type = k & 1, ofs = (k >> 1) ? 44 : 0;
        Rig r;
        if (type) r.dl.setType(DELAY_TYPE_PINGPONG);
        r.dl.setFeedback(0.5f);
        // equal sides: the left check cuts the block; left 44 samples longer: the right one does
        r.dl.setTimeBalance(ofs ? 0.001f : 0.0f);
        r.src.data.assign(2 * (1024 + 400 * 256), 0.1f);
        r.run(1024);
        r.dl.setDelaySample(8);
        CHECK_EQ(r.dl.m_delayL, 8 + ofs);
        CHECK_EQ(r.dl.m_delayR, 8);
        CHECK_EQ(r.dl.m_bSmoothing, 1);
        double prev = r.dl.m_curDelayL;
        int mono = 1;
        for (int b = 0; b < 400; b++) {     // 1 - 2^(-t / 0.15 s): within 0.1 sample after ~2 s
            r.run(256);
            if (r.dl.m_curDelayL > prev) mono = 0;
            prev = r.dl.m_curDelayL;
        }
        CHECK(mono);
        CHECK_EQ(r.dl.m_bSmoothing, 0);
        CHECK_CLOSE(r.dl.m_curDelayL, 8.0 + ofs, 0);
        CHECK_CLOSE(r.dl.m_curDelayR, 8.0, 0);
        CHECK_EQ((int)r.wet.size(), 2 * (1024 + 400 * 256));
        int bad = 0;
        for (size_t i = 0; i < r.wet.size(); i++) {
            if (!(r.wet[i] == r.wet[i]) || fabs(r.wet[i]) > 0.21) bad++;
        }
        CHECK_EQ(bad, 0);
        // steady state with the short delay: constant input, constant echo
        size_t n = r.wet.size();
        CHECK_CLOSE(r.wet[n - 2], r.wet[n - 4], 1e-6);
        CHECK(fabs(r.wet[n - 2]) > 0.05);
    }
}

/** @brief Fixed delays shorter than the block (right side longer): echoes land exactly at 8 and 52 frames. */
TEST(delay, short_fixed_delays_right_longer)
{
    Rig r;
    r.dl.setTimeBalance(-0.001f);
    r.dl.setDelaySample(8);
    CHECK_EQ(r.dl.m_delayL, 8);
    CHECK_EQ(r.dl.m_delayR, 52);
    CHECK_EQ(r.dl.m_bSmoothing, 0);
    impulse(r.src, 256, 1.0f, 1.0f);
    r.run(256);
    CHECK_CLOSE(r.wet[8 * 2], 1.0, 0);
    CHECK_CLOSE(r.wet[52 * 2 + 1], 1.0, 0);
    int skip[2] = {8, 52};
    CHECK_CLOSE(wetElsewhere(r.wet, skip, 2), 0.0, 0);
    CHECK_CLOSE(r.wet[52 * 2], 0.0, 0);
    CHECK_CLOSE(r.wet[8 * 2 + 1], 0.0, 0);
}

/** @brief Ping-pong with fixed delays shorter than the block (right side longer): the left echo at 8 frames crosses to the right 52 frames later. */
TEST(delay, short_fixed_delays_ping_pong)
{
    Rig r;
    r.dl.setType(DELAY_TYPE_PINGPONG);
    r.dl.setTimeBalance(-0.001f);
    r.dl.setDelaySample(8);
    CHECK_EQ(r.dl.m_delayL, 8);
    CHECK_EQ(r.dl.m_delayR, 52);
    impulse(r.src, 256, 1.0f, 1.0f);
    r.run(256);
    CHECK_CLOSE(r.wet[8 * 2], 1.0, 0);          // (L + R) / 2
    CHECK_CLOSE(r.wet[60 * 2 + 1], 1.0, 0);     // the left output fed to the right line
    int skip[2] = {8, 60};
    CHECK_CLOSE(wetElsewhere(r.wet, skip, 2), 0.0, 0);
}
