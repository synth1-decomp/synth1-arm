/**
 * @file test_arp.cpp
 * @brief Unit tests for techpop/arp.h (CArpeggiator): step length from tempo / step table /
 *        sampling rate, gate time and wait times, the up / down / up-down / random patterns over
 *        the octave range, key order independence, enable / disable and the documented key 0 quirk.
 */
#include "test.h"

#include <stdlib.h>
#include <vector>
#include "inst.h"
#include "voicectl.h"
#include "arp.h"

namespace {

/**
 * @brief Instrument that records the arpeggiator's note on / off calls.
 */
class RecInst : public CInst {
public:
    std::vector<int> ons;    ///< notes of noteOnFromArp, in order
    std::vector<int> vels;   ///< velocities of noteOnFromArp
    std::vector<int> offs;   ///< notes of noteOffFromArp, in order
    int sounding;            ///< notes currently on (on - off)

    RecInst() : sounding(0) {}
    virtual void noteOnFromArp(int nn, int vel) { ons.push_back(nn); vels.push_back(vel); sounding++; }
    virtual void noteOffFromArp(int nn) { offs.push_back(nn); sounding--; }
};

/**
 * @brief An arpeggiator connected to a recording instrument and a keyboard state.
 */
struct ArpRig {
    RecInst inst;
    CKeyboardInfo kbd;
    CArpeggiator arp;

    ArpRig()
    {
        arp.setInst(&inst);
        arp.setKbdInfo(&kbd);
    }

    /**
     * @brief Starts the pattern with the given type and octave range.
     *
     * @param type      ARP_xxx.
     * @param octRange  Extra octaves.
     */
    void start(int type, int octRange)
    {
        arp.setOctRange(octRange);
        arp.setEnable(1);
        arp.setType(type);
        arp.timingReset(100);
    }

    /**
     * @brief Runs the arpeggiator like CSynthesizer1::processSignalW() until n notes were sent.
     *
     * @param n  Number of note ons to wait for.
     * @return Number of samples rendered.
     */
    long runNotes(int n)
    {
        long samples = 0;
        int guard = 0;
        size_t target = inst.ons.size() + n;
        while (inst.ons.size() < target && guard++ < 100000) {
            int len = arp.getNextWaitTime();
            if (len == 0) len = arp.processArpeggiator();
            arp.incCurTime(len);
            samples += len;
        }
        return samples;
    }
};

/**
 * @brief Checks that the recorded note ons match an expected sequence.
 *
 * @param got     Recorded notes.
 * @param expect  Expected notes.
 * @param n       Number of expected notes.
 * @return True if the first n recorded notes match.
 */
bool sameSeq(const std::vector<int>& got, const int* expect, int n)
{
    if ((int)got.size() < n) return false;
    for (int i = 0; i < n; i++) {
        if (got[i] != expect[i]) {
            printf("    note %d: got %d, expected %d\n", i, got[i], expect[i]);
            return false;
        }
    }
    return true;
}

}  // namespace

/** @brief Defaults: 44100 Hz, 120 BPM, 1/4 steps (22050 samples), one octave, gate 0.5, up/down. */
TEST(arp, constructor_defaults)
{
    CArpeggiator a;
    CHECK_EQ(a.m_samplingFreq, 44100);
    CHECK_EQ(a.isPlaying(), 0);
    CHECK_EQ(a.m_bEnable, 0);
    CHECK_CLOSE(a.m_tempo, 120.0, 0);
    CHECK_EQ(a.m_cycleTime, 22050);
    CHECK_EQ(a.m_beat, 24);
    CHECK_EQ(a.m_curTime, 0);
    CHECK_EQ(a.m_octRange, 1);
    CHECK_CLOSE(a.m_gateTime, 0.5, 0);
    CHECK_EQ(a.m_curNote, -1);
    CHECK_EQ(a.m_playingNote, -1);
    CHECK_EQ(a.m_velocity, 127);
    CHECK_EQ(a.m_type, ARP_UPDOWN);
}

/** @brief The step length is fs * beat * 60 / (tempo * 24) samples. */
TEST(arp, step_length_from_tempo_beat_and_rate)
{
    CArpeggiator a;
    a.setTempo(60.0f);
    CHECK_EQ(a.m_cycleTime, 44100);
    a.setTempo(240.0f);
    CHECK_EQ(a.m_cycleTime, 11025);
    a.setTempo(120.0f);
    a.setBeat(3);            // 48 = 1/2
    CHECK_EQ(a.m_beat, 48);
    CHECK_EQ(a.m_cycleTime, 44100);
    a.setBeat(0);            // 96 = 1/1
    CHECK_EQ(a.m_cycleTime, 88200);
    a.setBeat(11);           // 12 = 1/8
    CHECK_EQ(a.m_cycleTime, 11025);
    a.setBeat(18);           // 1: the shortest step
    CHECK_EQ(a.m_beat, 1);
    CHECK_EQ(a.m_cycleTime, 918);
    a.setBeat(7);
    a.setSamplingFreq(48000);
    CHECK_EQ(a.m_samplingFreq, 48000);
    CHECK_CLOSE(a.m_tempo, 120.0, 0);
    CHECK_EQ(a.m_cycleTime, 24000);
    a.setTempo(133.0f);
    CHECK_EQ(a.m_cycleTime, (int)((float)(48000 * 24 * 60) / (133.0f * 24.0f)));
}

/** @brief setBeat() clamps its index to 0..18; setTempo() clamps the tempo to at least 1 BPM. */
TEST(arp, clamping)
{
    CArpeggiator a;
    a.setBeat(-5);
    CHECK_EQ(a.m_beat, 96);
    a.setBeat(99);
    CHECK_EQ(a.m_beat, 1);
    a.setTempo(0.25f);
    CHECK_CLOSE(a.m_tempo, 1.0, 0);
    a.setTempo(-10.0f);
    CHECK_CLOSE(a.m_tempo, 1.0, 0);
}

/** @brief Changing tempo or step length rescales the position within the step. */
TEST(arp, position_rescaled_on_change)
{
    CArpeggiator a;
    a.incCurTime(11025);     // half of the 22050 step
    a.setTempo(60.0f);
    CHECK_EQ(a.m_cycleTime, 44100);
    CHECK_EQ(a.m_curTime, 22050);
    a.setBeat(3);            // twice as long
    CHECK_EQ(a.m_curTime, 44100);
    CHECK_EQ(a.m_cycleTime, 88200);
    a.m_tempo = 120.0f;
    a.recalcCycleTime();
    CHECK_EQ(a.m_cycleTime, 44100);
    CHECK_EQ(a.m_curTime, 22050);
}

/** @brief setGateTime() maps 0..127 to 0..1. */
TEST(arp, gate_time_mapping)
{
    CArpeggiator a;
    a.setGateTime(0);
    CHECK_CLOSE(a.m_gateTime, 0.0, 0);
    a.setGateTime(127);
    CHECK_CLOSE(a.m_gateTime, 1.0, 1e-7);
    a.setGateTime(64);
    CHECK_CLOSE(a.m_gateTime, 64 / 127.0, 1e-6);
}

/** @brief getNextWaitTime(): 0 at the step start and the gate end, else time to the next event. */
TEST(arp, next_wait_time)
{
    CArpeggiator a;          // 22050 per step, gate 0.5 -> 11025
    CHECK_EQ(a.getNextWaitTime(), 0);
    a.incCurTime(100);
    CHECK_EQ(a.getNextWaitTime(), 10925);
    a.incCurTime(10925);
    CHECK_EQ(a.getNextWaitTime(), 0);
    a.incCurTime(3975);
    CHECK_EQ(a.getNextWaitTime(), 22050 - 15000);
    a.incCurTime(7050);      // wraps to the next step
    CHECK_EQ(a.m_curTime, 0);
    a.incCurTime(22050 * 3 + 5);
    CHECK_EQ(a.m_curTime, 5);
}

/** @brief setEnable()/setType() start and stop playing; stopping releases the sounding note. */
TEST(arp, enable_and_disable)
{
    ArpRig r;
    r.kbd.onKey(60, 100);
    r.arp.setEnable(1);
    CHECK_EQ(r.arp.isPlaying(), 1);
    CHECK_EQ(r.arp.m_curTime, 0);
    CHECK_EQ(r.arp.m_curOct, 0);
    r.runNotes(1);                   // stops right after the note on, inside the gate
    CHECK_EQ(r.inst.sounding, 1);
    int playing = r.arp.m_playingNote;
    CHECK(playing != -1);
    r.arp.setEnable(0);
    CHECK_EQ(r.arp.isPlaying(), 0);
    CHECK_EQ(r.inst.sounding, 0);
    CHECK_EQ(r.inst.offs.back(), playing);
    CHECK_EQ(r.arp.m_playingNote, -1);
    // values other than 1 disable
    r.arp.setEnable(2);
    CHECK_EQ(r.arp.isPlaying(), 0);
    // setType restarts while enabled
    r.arp.setEnable(1);
    r.arp.m_curTime = 123;
    r.arp.setType(ARP_DOWN);
    CHECK_EQ(r.arp.m_curTime, 0);
    CHECK_EQ(r.arp.m_bDown, 1);
    r.arp.setType(ARP_UP);
    CHECK_EQ(r.arp.m_bDown, 0);
}

/** @brief Each step plays one note for the gate time, then releases it until the next step. */
TEST(arp, step_timing_and_gate)
{
    ArpRig r;
    r.kbd.onKey(60, 100);
    r.start(ARP_UP, 0);
    // step start: note on, returns the gate length
    CHECK_EQ(r.arp.getNextWaitTime(), 0);
    CHECK_EQ(r.arp.processArpeggiator(), 11025);
    CHECK_EQ((int)r.inst.ons.size(), 1);
    CHECK_EQ(r.inst.ons[0], 60);
    CHECK_EQ(r.inst.vels[0], 100);
    r.arp.incCurTime(11025);
    // gate end: note off, returns the rest of the step
    CHECK_EQ(r.arp.getNextWaitTime(), 0);
    CHECK_EQ(r.arp.processArpeggiator(), 11025);
    CHECK_EQ((int)r.inst.offs.size(), 1);
    CHECK_EQ(r.inst.sounding, 0);
    r.arp.incCurTime(11025);
    CHECK_EQ(r.arp.m_curTime, 0);

    // one note per step: 8 notes take 8 steps minus the final rest
    long samples = r.runNotes(8);
    CHECK_EQ(samples, 22050L * 7 + 11025);

    // gate 1.0: the note lasts the whole step
    r.arp.setGateTime(127);
    r.arp.m_curTime = 0;
    CHECK_EQ(r.arp.processArpeggiator(), 22050);
    // gate 0: no note at all
    r.arp.setGateTime(0);
    size_t before = r.inst.ons.size();
    r.arp.m_curTime = 0;
    CHECK_EQ(r.arp.processArpeggiator(), 22050);
    CHECK_EQ(r.inst.ons.size(), before);
    CHECK_EQ(r.inst.sounding, 0);
}

/** @brief Up, one octave range: the held keys ascending, then an octave higher, then repeat. */
TEST(arp, up_pattern)
{
    ArpRig r;
    r.kbd.onKey(67, 100);    // pressed out of order: the pattern follows pitch, not press order
    r.kbd.onKey(60, 100);
    r.kbd.onKey(64, 100);
    r.start(ARP_UP, 1);
    r.runNotes(14);
    static const int expect[] = {60, 64, 67, 72, 76, 79, 60, 64, 67, 72, 76, 79, 60, 64};
    CHECK(sameSeq(r.inst.ons, expect, 14));
    for (size_t i = 0; i < r.inst.vels.size(); i++) CHECK_EQ(r.inst.vels[i], 100);
}

/** @brief Up without octave range just cycles the held keys. */
TEST(arp, up_pattern_no_octave)
{
    ArpRig r;
    r.kbd.onKey(48, 100);
    r.kbd.onKey(50, 100);
    r.start(ARP_UP, 0);
    r.runNotes(6);
    static const int expect[] = {48, 50, 48, 50, 48, 50};
    CHECK(sameSeq(r.inst.ons, expect, 6));
}

/** @brief Up, three octaves: covers four octaves. */
TEST(arp, up_pattern_three_octaves)
{
    ArpRig r;
    r.kbd.onKey(40, 100);
    r.start(ARP_UP, 3);
    r.runNotes(9);
    static const int expect[] = {40, 52, 64, 76, 40, 52, 64, 76, 40};
    CHECK(sameSeq(r.inst.ons, expect, 9));
}

/** @brief Down, one octave: descends in the base octave first, then the upper octave (as coded). */
TEST(arp, down_pattern)
{
    ArpRig r;
    r.kbd.onKey(60, 100);
    r.kbd.onKey(64, 100);
    r.kbd.onKey(67, 100);
    r.start(ARP_DOWN, 1);
    r.runNotes(12);
    static const int expect[] = {67, 64, 60, 79, 76, 72, 67, 64, 60, 79, 76, 72};
    CHECK(sameSeq(r.inst.ons, expect, 12));
}

/** @brief Down without octave range cycles the held keys from the top. */
TEST(arp, down_pattern_no_octave)
{
    ArpRig r;
    r.kbd.onKey(60, 100);
    r.kbd.onKey(64, 100);
    r.kbd.onKey(67, 100);
    r.start(ARP_DOWN, 0);
    r.runNotes(7);
    static const int expect[] = {67, 64, 60, 67, 64, 60, 67};
    CHECK(sameSeq(r.inst.ons, expect, 7));
}

/** @brief Up/down: ascends through the octaves, turns without repeating the top or bottom note. */
TEST(arp, updown_pattern)
{
    ArpRig r;
    r.kbd.onKey(60, 100);
    r.kbd.onKey(64, 100);
    r.kbd.onKey(67, 100);
    r.start(ARP_UPDOWN, 0);
    r.runNotes(9);
    static const int expect0[] = {60, 64, 67, 64, 60, 64, 67, 64, 60};
    CHECK(sameSeq(r.inst.ons, expect0, 9));

    ArpRig r1;
    r1.kbd.onKey(60, 100);
    r1.kbd.onKey(64, 100);
    r1.kbd.onKey(67, 100);
    r1.start(ARP_UPDOWN, 1);
    r1.runNotes(14);
    static const int expect1[] = {60, 64, 67, 72, 76, 79, 76, 72, 67, 64, 60, 64, 67, 72};
    CHECK(sameSeq(r1.inst.ons, expect1, 14));
}

/** @brief Up/down with a single key alternates between the octaves. */
TEST(arp, updown_single_key)
{
    ArpRig r;
    r.kbd.onKey(60, 100);
    r.start(ARP_UPDOWN, 1);
    r.runNotes(8);
    for (size_t i = 0; i < r.inst.ons.size(); i++) {
        CHECK(r.inst.ons[i] == 60 || r.inst.ons[i] == 72);
    }
    int count72 = 0;
    for (size_t i = 0; i < r.inst.ons.size(); i++) if (r.inst.ons[i] == 72) count72++;
    CHECK(count72 >= 2);
    CHECK_EQ(r.inst.ons[0], 60);
    CHECK_EQ(r.inst.ons[1], 72);
}

/** @brief Random: notes are held keys in the octave range, and the sequence depends only on the seed. */
TEST(arp, random_pattern)
{
    ArpRig r;
    r.kbd.onKey(60, 100);
    r.kbd.onKey(63, 100);
    r.kbd.onKey(66, 100);
    r.start(ARP_RANDOM, 2);
    srand(31337);
    r.runNotes(60);
    int seen[128] = {0};
    for (size_t i = 0; i < r.inst.ons.size(); i++) {
        int n = r.inst.ons[i];
        int base = (n - 60) % 12;
        int oct = (n - 60) / 12;
        CHECK(n >= 60 && (base == 0 || base == 3 || base == 6) && oct >= 0 && oct <= 2);
        if (n >= 0 && n < 128) seen[n] = 1;
    }
    int distinct = 0;
    for (int i = 0; i < 128; i++) distinct += seen[i];
    CHECK(distinct >= 6);   // of the 9 possible notes

    ArpRig r2;
    r2.kbd.onKey(60, 100);
    r2.kbd.onKey(63, 100);
    r2.kbd.onKey(66, 100);
    r2.start(ARP_RANDOM, 2);
    srand(31337);
    r2.runNotes(60);
    CHECK(r.inst.ons == r2.inst.ons);
}

/** @brief getRandomArpNote() returns -1 without keys; the only key when one is held. */
TEST(arp, random_note_helper)
{
    ArpRig r;
    CHECK_EQ(r.arp.getRandomArpNote(), -1);
    r.kbd.onKey(10, 100);
    srand(1);
    for (int i = 0; i < 10; i++) CHECK_EQ(r.arp.getRandomArpNote(), 10);
}

/** @brief Up/down helpers: next held key above/below m_curNote, or the lowest/highest when -1. */
TEST(arp, note_helpers)
{
    ArpRig r;
    r.kbd.onKey(20, 100);
    r.kbd.onKey(30, 100);
    r.arp.m_curNote = -1;
    CHECK_EQ(r.arp.GetUpArpeggioNote(), 20);
    CHECK_EQ(r.arp.GetDownArpeggioNote(), 30);
    r.arp.m_curNote = 20;
    CHECK_EQ(r.arp.GetUpArpeggioNote(), 30);
    CHECK_EQ(r.arp.GetDownArpeggioNote(), -1);
    r.arp.m_curNote = 30;
    CHECK_EQ(r.arp.GetUpArpeggioNote(), -1);
    CHECK_EQ(r.arp.GetDownArpeggioNote(), 20);
    r.arp.m_curNote = 127;
    CHECK_EQ(r.arp.GetUpArpeggioNote(), -1);
}

/** @brief Documented quirk: key 0 is never returned by GetDownArpeggioNote(). */
TEST(arp, key_zero_not_found_downwards)
{
    ArpRig r;
    r.kbd.onKey(0, 100);
    r.kbd.onKey(12, 100);
    r.arp.m_curNote = 12;
    CHECK_EQ(r.arp.GetDownArpeggioNote(), -1);
    r.arp.m_curNote = -1;
    CHECK_EQ(r.arp.GetUpArpeggioNote(), 0);
    // so the down pattern only ever plays 12
    r.start(ARP_DOWN, 0);
    r.runNotes(4);
    static const int expect[] = {12, 12, 12, 12};
    CHECK(sameSeq(r.inst.ons, expect, 4));
}

/** @brief Notes pushed above 127 by the octave range are skipped (silent step). */
TEST(arp, notes_above_127_are_skipped)
{
    ArpRig r;
    r.kbd.onKey(120, 100);
    r.start(ARP_UP, 1);
    r.arp.processArpeggiator();             // step 1: 120
    CHECK_EQ((int)r.inst.ons.size(), 1);
    CHECK_EQ(r.inst.ons[0], 120);
    CHECK_EQ(r.arp.m_playingNote, 120);
    r.arp.processArpeggiator();             // step 2 (m_curTime is still 0): 120 + 12 = 132, skipped
    CHECK_EQ((int)r.inst.ons.size(), 1);
    CHECK_EQ((int)r.inst.offs.size(), 1);
    CHECK_EQ(r.arp.m_curOct, 1);
    CHECK_EQ(r.arp.m_curNote, 120);
    CHECK_EQ(r.arp.m_playingNote, -1);
    r.arp.processArpeggiator();             // step 3: back to the base octave
    CHECK_EQ((int)r.inst.ons.size(), 2);
    CHECK_EQ(r.inst.ons[1], 120);
    CHECK_EQ(r.arp.m_curOct, 0);
}

/** @brief No held keys: steps pass without notes. */
TEST(arp, no_keys_no_notes)
{
    ArpRig r;
    // (ARP_UP: with no key held, ARP_UPDOWN would read the velocity of key m_curNote = -1)
    r.start(ARP_UP, 1);
    for (int i = 0; i < 10; i++) {
        int len = r.arp.getNextWaitTime();
        if (len == 0) len = r.arp.processArpeggiator();
        r.arp.incCurTime(len);
    }
    CHECK(r.inst.ons.empty());
    CHECK_EQ(r.arp.m_playingNote, -1);
}

/** @brief timingReset() restarts the pattern from the lowest key with the new velocity. */
TEST(arp, timing_reset_restarts_pattern)
{
    ArpRig r;
    r.kbd.onKey(60, 100);
    r.kbd.onKey(64, 100);
    r.kbd.onKey(67, 100);
    r.start(ARP_UP, 0);
    r.runNotes(2);                          // 60, 64
    r.arp.incCurTime(5000);
    r.arp.timingReset(33);
    CHECK_EQ(r.arp.m_curTime, 0);
    CHECK_EQ(r.arp.m_curNote, -1);
    CHECK_EQ(r.arp.m_curOct, 0);
    CHECK_EQ(r.arp.m_velocity, 33);
    r.runNotes(1);
    CHECK_EQ(r.inst.ons.back(), 60);
    CHECK_EQ(r.inst.vels.back(), 33);
    // with ARP_DOWN the reset goes downwards
    r.arp.setType(ARP_DOWN);
    r.arp.timingReset(10);
    CHECK_EQ(r.arp.m_bDown, 1);
}

/** @brief Key changes are picked up at the next step. */
TEST(arp, follows_key_changes)
{
    ArpRig r;
    r.kbd.onKey(60, 100);
    r.kbd.onKey(64, 100);
    r.start(ARP_UP, 0);
    r.runNotes(2);                          // 60, 64
    r.kbd.onKey(62, 100);
    r.kbd.offKey(64);
    r.runNotes(4);
    static const int expect[] = {60, 64, 60, 62, 60, 62};
    CHECK(sameSeq(r.inst.ons, expect, 6));
}
