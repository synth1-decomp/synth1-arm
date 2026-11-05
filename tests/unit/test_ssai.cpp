/**
 * @file test_ssai.cpp
 * @brief Unit tests for techpop/ssai.h: CSeqSyncAudioInterface (master rendering, output layouts,
 *        sequencer-synchronized chunking) and the keep_bits_from_16()/foldback() sample helpers.
 */
#include "test.h"

#include <string.h>

#include "common.h"
#include "sigdev.h"
#include "ssai.h"

namespace {

/**
 * @brief Stereo test device: frame k (counted over all calls) is L = k + 1, R = -(k + 1) / 2.
 *
 * Optionally returns at most maxFrames per call, or reports silence.
 */
class StereoSource : public CSignalDevice {
public:
    StereoSource() : frames(0), calls(0), maxFrames(0), silent(0), maxRequest(0) {}

    virtual int processSignal(int size, float* pBuf, int* pProcessed)
    {
        *pProcessed = 1;
        return size;
    }

    virtual int processSignalW(int size, float* pBuf, int* pProcessed)
    {
        calls++;
        if (size > maxRequest) maxRequest = size;
        int n = (maxFrames > 0 && maxFrames < size) ? maxFrames : size;
        if (silent) {
            for (int i = 0; i < 2 * n; i++) pBuf[i] = 12345.f;  // must not be used
            *pProcessed = 1;
        } else {
            for (int i = 0; i < n; i++) {
                pBuf[2 * i] = L(frames + i);
                pBuf[2 * i + 1] = R(frames + i);
            }
            *pProcessed = 0;
        }
        frames += n;
        return n;
    }

    static float L(int k) { return (float)(k + 1); }
    static float R(int k) { return -(float)(k + 1) / 2.f; }

    int frames;
    int calls;
    int maxFrames;
    int silent;
    int maxRequest;
};

/**
 * @brief Sequencer stub with an event every period samples; records the call sequence.
 */
class StubSeq : public CSequencer {
public:
    explicit StubSeq(int p) : period(p), time(0), playing(1), notes(0), noteTimesNum(0) {}

    virtual int isPlaying() { return playing; }
    virtual void incCurTime(int t) { time += t; }
    virtual int getNextWaitTime() { return period - (time % period); }
    virtual void processNote()
    {
        if (noteTimesNum < 64) noteTimes[noteTimesNum++] = time;
        notes++;
    }

    int period;
    int time;
    int playing;
    int notes;
    int noteTimes[64];
    int noteTimesNum;
};

float g_l[2048];
float g_r[2048];

void fill(float* p, int n, float v)
{
    for (int i = 0; i < n; i++) p[i] = v;
}

}  // namespace

/**
 * @brief A new interface has neither master device nor sequencer; the setters store them.
 */
TEST(ssai, constructor_and_setters)
{
    CSeqSyncAudioInterface ai;
    CHECK(ai.m_pMasterOut == NULL);
    CHECK(ai.m_pSequencer == NULL);
    StereoSource s;
    StubSeq q(10);
    ai.setMasterOutDevice(&s);
    ai.setSequencer(&q);
    CHECK(ai.m_pMasterOut == &s);
    CHECK(ai.m_pSequencer == &q);
    ai.setSequencer(NULL);
    CHECK(ai.m_pSequencer == NULL);
}

/**
 * @brief Without master device and sequencer, getSoundData() writes nothing and returns
 *        size * sizeof(float).
 */
TEST(ssai, no_device_writes_nothing)
{
    CSeqSyncAudioInterface ai;
    fill(g_l, 64, 3.f);
    float* bufs[2] = {g_l, g_r};
    CHECK_EQ(ai.getSoundData(1, bufs, 0, 32), 32 * (int)sizeof(float));
    CHECK_CLOSE(g_l[0], 3.f, 0);
    CHECK_CLOSE(g_l[63], 3.f, 0);
}

/**
 * @brief Two output buffers receive the left and right channels separately.
 */
TEST(ssai, two_channel_output)
{
    CSeqSyncAudioInterface ai;
    StereoSource s;
    ai.setMasterOutDevice(&s);
    fill(g_l, 2048, -9.f);
    fill(g_r, 2048, -9.f);
    float* bufs[2] = {g_l, g_r};
    CHECK_EQ(ai.getSoundData(2, bufs, 0, 100), 400);
    int bad = 0;
    for (int i = 0; i < 100; i++) {
        if (g_l[i] != StereoSource::L(i) || g_r[i] != StereoSource::R(i)) bad++;
    }
    CHECK_EQ(bad, 0);
    CHECK_CLOSE(g_l[100], -9.f, 0);
    CHECK_CLOSE(g_r[100], -9.f, 0);
    CHECK(bufs[0] == g_l);  // pointers are not advanced
}

/**
 * @brief One buffer with bMix != 1 receives interleaved stereo.
 */
TEST(ssai, one_buffer_interleaved)
{
    CSeqSyncAudioInterface ai;
    StereoSource s;
    ai.setMasterOutDevice(&s);
    fill(g_l, 2048, -9.f);
    float* bufs[1] = {g_l};
    ai.getSoundData(1, bufs, 0, 50);
    int bad = 0;
    for (int i = 0; i < 50; i++) {
        if (g_l[2 * i] != StereoSource::L(i) || g_l[2 * i + 1] != StereoSource::R(i)) bad++;
    }
    CHECK_EQ(bad, 0);
    CHECK_CLOSE(g_l[100], -9.f, 0);
}

/**
 * @brief One buffer with bMix == 1 receives the mono sum L + R.
 */
TEST(ssai, one_buffer_mixed)
{
    CSeqSyncAudioInterface ai;
    StereoSource s;
    ai.setMasterOutDevice(&s);
    fill(g_l, 2048, -9.f);
    float* bufs[1] = {g_l};
    ai.getSoundData(1, bufs, 1, 50);
    int bad = 0;
    for (int i = 0; i < 50; i++) {
        if (g_l[i] != StereoSource::L(i) + StereoSource::R(i)) bad++;
    }
    CHECK_EQ(bad, 0);
    CHECK_CLOSE(g_l[50], -9.f, 0);
}

/**
 * @brief Silent blocks are written as zeros in every output layout.
 */
TEST(ssai, silence_is_written_as_zeros)
{
    StereoSource s;
    s.silent = 1;
    CSeqSyncAudioInterface ai;
    ai.setMasterOutDevice(&s);
    float* bufs[2] = {g_l, g_r};

    fill(g_l, 2048, 5.f);
    fill(g_r, 2048, 5.f);
    ai.getSoundData(2, bufs, 0, 40);
    int bad = 0;
    for (int i = 0; i < 40; i++) if (g_l[i] != 0.f || g_r[i] != 0.f) bad++;
    CHECK_EQ(bad, 0);
    CHECK_CLOSE(g_l[40], 5.f, 0);

    fill(g_l, 2048, 5.f);
    ai.getSoundData(1, bufs, 0, 40);
    bad = 0;
    for (int i = 0; i < 80; i++) if (g_l[i] != 0.f) bad++;
    CHECK_EQ(bad, 0);
    CHECK_CLOSE(g_l[80], 5.f, 0);

    fill(g_l, 2048, 5.f);
    ai.getSoundData(1, bufs, 1, 40);
    bad = 0;
    for (int i = 0; i < 40; i++) if (g_l[i] != 0.f) bad++;
    CHECK_EQ(bad, 0);
    CHECK_CLOSE(g_l[40], 5.f, 0);
}

/**
 * @brief outputMaster() pulls at most 256 frames per processSignalW() call and keeps writing
 *        consecutively across blocks.
 */
TEST(ssai, output_master_blocks_of_256)
{
    CSeqSyncAudioInterface ai;
    StereoSource s;
    fill(g_l, 2048, -9.f);
    fill(g_r, 2048, -9.f);
    float* bufs[2] = {g_l, g_r};
    ai.outputMaster(&s, bufs, 2, 0, 600);
    CHECK_EQ(s.calls, 3);
    CHECK_EQ(s.maxRequest, 256);
    CHECK_EQ(s.frames, 600);
    int bad = 0;
    for (int i = 0; i < 600; i++) {
        if (g_l[i] != StereoSource::L(i) || g_r[i] != StereoSource::R(i)) bad++;
    }
    CHECK_EQ(bad, 0);
    CHECK_CLOSE(g_l[600], -9.f, 0);
}

/**
 * @brief A device returning fewer frames than requested is called again until the size is met.
 */
TEST(ssai, output_master_handles_short_returns)
{
    CSeqSyncAudioInterface ai;
    StereoSource s;
    s.maxFrames = 7;
    fill(g_l, 2048, -9.f);
    float* bufs[1] = {g_l};
    ai.outputMaster(&s, bufs, 1, 0, 30);
    CHECK_EQ(s.calls, 5);
    CHECK_EQ(s.frames, 30);
    int bad = 0;
    for (int i = 0; i < 30; i++) {
        if (g_l[2 * i] != StereoSource::L(i) || g_l[2 * i + 1] != StereoSource::R(i)) bad++;
    }
    CHECK_EQ(bad, 0);
}

/**
 * @brief A stopped sequencer is ignored: the whole buffer is rendered in one go.
 */
TEST(ssai, stopped_sequencer_is_ignored)
{
    CSeqSyncAudioInterface ai;
    StereoSource s;
    StubSeq q(10);
    q.playing = 0;
    ai.setMasterOutDevice(&s);
    ai.setSequencer(&q);
    float* bufs[2] = {g_l, g_r};
    ai.getSoundData(2, bufs, 0, 64);
    CHECK_EQ(s.calls, 1);
    CHECK_EQ(q.time, 0);
    CHECK_EQ(q.notes, 0);
}

/**
 * @brief A running sequencer splits the block at its event times: processNote() is called exactly
 *        when the sequencer time reaches each event, and the time advances by the full size.
 */
TEST(ssai, sequencer_events_land_on_exact_samples)
{
    CSeqSyncAudioInterface ai;
    StereoSource s;
    StubSeq q(30);
    ai.setMasterOutDevice(&s);
    ai.setSequencer(&q);
    float* bufs[2] = {g_l, g_r};
    CHECK_EQ(ai.getSoundData(2, bufs, 0, 100), 400);
    CHECK_EQ(q.time, 100);
    CHECK_EQ(s.frames, 100);
    CHECK_EQ(q.notes, 3);
    CHECK_EQ(q.noteTimes[0], 30);
    CHECK_EQ(q.noteTimes[1], 60);
    CHECK_EQ(q.noteTimes[2], 90);
    CHECK_EQ(s.calls, 4);  // chunks of 30, 30, 30, 10

    // next call continues with the pending event at 120
    ai.getSoundData(2, bufs, 0, 20);
    CHECK_EQ(q.time, 120);
    CHECK_EQ(q.notes, 4);
    CHECK_EQ(q.noteTimes[3], 120);
}

/**
 * @brief An event due exactly at the end of the block is processed in that block.
 */
TEST(ssai, sequencer_event_at_block_end)
{
    CSeqSyncAudioInterface ai;
    StereoSource s;
    StubSeq q(64);
    ai.setMasterOutDevice(&s);
    ai.setSequencer(&q);
    float* bufs[2] = {g_l, g_r};
    ai.getSoundData(2, bufs, 0, 64);
    CHECK_EQ(q.notes, 1);
    CHECK_EQ(q.noteTimes[0], 64);
    CHECK_EQ(s.calls, 1);
}

/**
 * @brief With a running sequencer, each chunk is written from the start of the output buffers
 *        (outputMaster() does not advance the caller's pointers), so the buffer start ends up
 *        holding the last chunk. Documents the current behaviour.
 */
TEST(ssai, sequencer_chunks_overwrite_buffer_start)
{
    CSeqSyncAudioInterface ai;
    StereoSource s;
    StubSeq q(30);
    ai.setMasterOutDevice(&s);
    ai.setSequencer(&q);
    fill(g_l, 2048, -9.f);
    fill(g_r, 2048, -9.f);
    float* bufs[2] = {g_l, g_r};
    ai.getSoundData(2, bufs, 0, 100);
    // last chunk: frames 90..99 at positions 0..9
    for (int i = 0; i < 10; i++) CHECK_CLOSE(g_l[i], StereoSource::L(90 + i), 0);
    // positions 10..29 still hold the third chunk (frames 70..89)
    for (int i = 10; i < 30; i++) CHECK_CLOSE(g_l[i], StereoSource::L(60 + i), 0);
    // beyond the largest chunk nothing was written
    CHECK_CLOSE(g_l[30], -9.f, 0);
    CHECK_CLOSE(g_r[99], -9.f, 0);
}

/**
 * @brief keep_bits_from_16() keeps the top keepBits bits of the sample.
 */
TEST(ssai, keep_bits_from_16)
{
    CHECK_EQ(keep_bits_from_16(0x1234, 16), 0x1234);
    CHECK_EQ(keep_bits_from_16(0x7FFF, 8), 0x7F00);
    CHECK_EQ(keep_bits_from_16(0x7FFF, 4), 0x7000);
    CHECK_EQ(keep_bits_from_16(-1, 4), -4096);
    CHECK_EQ(keep_bits_from_16(-1, 1), -32768);
    CHECK_EQ(keep_bits_from_16(0x1234, 1), 0);
    CHECK_EQ(keep_bits_from_16(-32768, 1), -32768);
    CHECK_EQ(keep_bits_from_16(0x00FF, 8), 0);
    for (int b = 1; b <= 16; b++) {
        short v = keep_bits_from_16((short)0x5A5A, b);
        CHECK_EQ(v & ((1 << (16 - b)) - 1), 0);  // low bits cleared
    }
}

/**
 * @brief foldback() leaves values within the threshold unchanged and reflects values beyond it
 *        back into range (1.5 -> 0.5, 2.5 -> -0.5 at threshold 1).
 */
TEST(ssai, foldback_values)
{
    CHECK_CLOSE(foldback(0.3f, 1.f), 0.3f, 0);
    CHECK_CLOSE(foldback(-1.f, 1.f), -1.f, 0);
    CHECK_CLOSE(foldback(1.f, 1.f), 1.f, 0);
    CHECK_CLOSE(foldback(1.5f, 1.f), 0.5f, 1e-6);
    CHECK_CLOSE(foldback(2.5f, 1.f), -0.5f, 1e-6);
    CHECK_CLOSE(foldback(-1.5f, 1.f), -0.5f, 1e-6);
    CHECK_CLOSE(foldback(3.f, 1.f), -1.f, 1e-6);
    CHECK_CLOSE(foldback(5.f, 1.f), 1.f, 1e-6);
    CHECK_CLOSE(foldback(0.75f, 0.5f), 0.25f, 1e-6);
}

/**
 * @brief foldback() output always lies within +-threshold, is continuous and odd-symmetric.
 */
TEST(ssai, foldback_properties)
{
    const float ths[] = {0.25f, 1.f, 3.f};
    for (int t = 0; t < 3; t++) {
        float th = ths[t];
        float prev = foldback(-10.f * th, th);
        int outOfRange = 0, jumps = 0, asym = 0;
        for (int i = -1000; i <= 1000; i++) {
            float x = i * th / 100.f;
            float y = foldback(x, th);
            if (fabsf(y) > th * 1.00001f) outOfRange++;
            if (fabsf(y - prev) > th / 100.f * 1.01f + 1e-5f) jumps++;
            if (fabsf(y + foldback(-x, th)) > 1e-5f * th) asym++;
            prev = y;
        }
        CHECK_EQ(outOfRange, 0);
        CHECK_EQ(jumps, 0);
        CHECK_EQ(asym, 0);
    }
}
