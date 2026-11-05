/**
 * @file test_stepvox.cpp
 * @brief Unit tests for techpop/stepvox.h (CStepVox): step timing, play state and the random
 *        four-note chords sent to the instrument every other step.
 */
#include "test.h"

#include <vector>
#include "inst.h"
#include "stepvox.h"

namespace {

/**
 * @brief Instrument that records noteOn / noteOff calls.
 */
class RecInst : public CInst {
public:
    std::vector<int> ons;    ///< notes of noteOn, in order
    std::vector<int> vels;   ///< velocities of noteOn
    std::vector<int> offs;   ///< notes of noteOff, in order

    virtual void noteOn(int nn, int vel) { ons.push_back(nn); vels.push_back(vel); }
    virtual void noteOff(int nn) { offs.push_back(nn); }
};

}  // namespace

/** @brief A new sequencer is stopped at time 0. */
TEST(stepvox, constructor_defaults)
{
    CStepVox s;
    CHECK_EQ(s.m_curTime, 0);
    CHECK_EQ(s.isPlaying(), 0);
    CHECK_EQ(s.getNextWaitTime(), STEPVOX_CYCLE);
}

/** @brief startPlay()/stopPlay() toggle the play state, also through the CSequencer interface. */
TEST(stepvox, start_and_stop)
{
    CStepVox s;
    CSequencer* seq = &s;
    s.startPlay();
    CHECK_EQ(s.m_bPlaying, 1);
    CHECK_EQ(seq->isPlaying(), 1);
    s.stopPlay();
    CHECK_EQ(seq->isPlaying(), 0);
}

/** @brief incCurTime() advances within the step and wraps to 0 at the step end, dropping the overshoot. */
TEST(stepvox, time_advance_and_wrap)
{
    CStepVox s;
    CSequencer* seq = &s;
    seq->incCurTime(1000);
    CHECK_EQ(s.m_curTime, 1000);
    CHECK_EQ(seq->getNextWaitTime(), STEPVOX_CYCLE - 1000);
    seq->incCurTime(STEPVOX_CYCLE - 1001);
    CHECK_EQ(seq->getNextWaitTime(), 1);
    seq->incCurTime(1);
    CHECK_EQ(s.m_curTime, 0);
    CHECK_EQ(seq->getNextWaitTime(), STEPVOX_CYCLE);
    seq->incCurTime(STEPVOX_CYCLE + 500);   // overshoot is discarded
    CHECK_EQ(s.m_curTime, 0);
    s.incCurTime(77);
    s.resetCurTime();
    CHECK_EQ(s.m_curTime, 0);
}

/**
 * @brief processNote(): every other call releases the previous chord and plays a new dominant
 *        seventh chord (root 30..53, +4, +7, +10) at velocity 127.
 *
 * The step counter and previous notes are function statics, so the whole sequence is checked in
 * this one test (the first call is the first one in the process).
 */
TEST(stepvox, process_note_plays_chords)
{
    RecInst inst;
    CStepVox s;
    s.setInst(&inst);
    CHECK(s.m_pInst == &inst);
    CSequencer* seq = &s;

    seq->processNote();   // call 0: offs of the (zero) previous notes, a chord
    REQUIRE((int)inst.offs.size() == 4);
    for (int i = 0; i < 4; i++) CHECK_EQ(inst.offs[i], 0);
    REQUIRE((int)inst.ons.size() == 4);

    std::vector<int> prev(inst.ons.begin(), inst.ons.end());
    for (int step = 1; step < 12; step++) {
        size_t onBefore = inst.ons.size(), offBefore = inst.offs.size();
        seq->processNote();
        if (step & 1) {
            CHECK_EQ(inst.ons.size(), onBefore);       // odd calls do nothing
            CHECK_EQ(inst.offs.size(), offBefore);
        } else {
            REQUIRE(inst.offs.size() == offBefore + 4);
            REQUIRE(inst.ons.size() == onBefore + 4);
            for (int i = 0; i < 4; i++) CHECK_EQ(inst.offs[offBefore + i], prev[i]);
            prev.assign(inst.ons.begin() + onBefore, inst.ons.end());
        }
    }

    for (size_t c = 0; c + 3 < inst.ons.size(); c += 4) {
        int root = inst.ons[c];
        CHECK(root >= 30 && root <= 53);
        CHECK_EQ(inst.ons[c + 1], root + 4);
        CHECK_EQ(inst.ons[c + 2], root + 7);
        CHECK_EQ(inst.ons[c + 3], root + 10);
    }
    for (size_t i = 0; i < inst.vels.size(); i++) CHECK_EQ(inst.vels[i], 127);
    CHECK_EQ((int)inst.ons.size(), 24);
}
