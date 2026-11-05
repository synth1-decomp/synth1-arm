#pragma once

/**
 * @file stepvox.h
 * CStepVox: a small test sequencer driven by CSeqSyncAudioInterface.
 */

#include "ssai.h"

class CInst;

#define STEPVOX_CYCLE   44100   // samples per step

/**
 * @brief Step voice: a tiny test sequencer that plays random chords.
 *
 * Implements CSequencer. Time advances in samples; one step lasts STEPVOX_CYCLE samples.
 * Every other step it releases the previous chord and plays a new random four-note chord
 * on the attached instrument.
 */
class CStepVox : public CSequencer {
public:
    /**
     * @brief Creates a stopped sequencer at time 0.
     *
     * The instrument must be set with setInst() before playback.
     */
    CStepVox();

    /**
     * @brief Sets the instrument that receives the note on/off events.
     *
     * @param pInst  Instrument to play (not owned).
     */
    void setInst(CInst* pInst);

    /**
     * @brief Advances the position within the current step.
     *
     * Wraps m_curTime to 0 once it reaches STEPVOX_CYCLE (the overshoot is discarded).
     *
     * @param time  Number of samples rendered since the last call.
     */
    virtual void incCurTime(int time);

    /**
     * @brief Computes the number of samples until the next step starts.
     *
     * @return STEPVOX_CYCLE minus the position within the step; 1..STEPVOX_CYCLE.
     */
    virtual int getNextWaitTime();

    /**
     * @brief Plays one step.
     *
     * On even-numbered calls it sends note off for the four previous notes and note on
     * (velocity 127) for a new chord whose root is a random note 30..53, with notes at
     * root + 0, 4, 7 and 10. The random generator is seeded from the clock on the first call.
     *
     * @note The step counter, previous notes and an unused sweep value are function-level
     *       statics, so they are shared by all instances.
     */
    virtual void processNote();

    /**
     * @brief Resets the position within the step to 0.
     */
    void resetCurTime();

    /**
     * @brief Tells whether the sequencer is playing.
     *
     * @return m_bPlaying: 1 while playing, 0 when stopped.
     */
    virtual int isPlaying();

    /**
     * @brief Starts playback (sets m_bPlaying to 1).
     */
    void startPlay();

    /**
     * @brief Stops playback (sets m_bPlaying to 0).
     *
     * @note Notes still sounding are not released.
     */
    void stopPlay();

    unsigned int m_curTime; // +0x08
    CInst* m_pInst;         // +0x10
    int m_bPlaying;         // +0x18
};
