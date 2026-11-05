#pragma once

/**
 * @file arp.h
 * @brief Arpeggiator (CArpeggiator) that steps through the held keys of the keyboard state.
 */

class CInst;
class CKeyboardInfo;

// arpeggiator types (m_type)
#define ARP_UPDOWN  1
#define ARP_UP      2
#define ARP_DOWN    3
#define ARP_RANDOM  4

/**
 * @brief Arpeggiator: plays the held keys of a CKeyboardInfo one after another
 *        through CInst::noteOnFromArp / noteOffFromArp.
 *
 * Timing is sample based: the host loop asks getNextWaitTime() how long it may render, calls
 * processArpeggiator() when that is 0, and advances the position with incCurTime().
 * sizeof == 0x68 (CSynthesizer1 +0x9f08).
 */
class CArpeggiator {
public:
    /**
     * @brief Sets the defaults: 44100 Hz, 120 BPM, 1/4 steps, one octave, gate 0.5, up/down, disabled.
     */
    CArpeggiator();

    /**
     * @brief Does nothing.
     */
    virtual ~CArpeggiator(){}

    /**
     * @brief Sets the sampling rate and recalculates the step length.
     *
     * @param fs  Sampling rate in Hz.
     */
    void setSamplingFreq(int fs);

    /**
     * @brief Sets the tempo and recalculates the step length.
     *
     * Does nothing if the tempo is unchanged. The position within the current step is rescaled to the
     * new step length.
     *
     * @param tempo  Beats per minute; values below 1 are clamped to 1.
     */
    void setTempo(float tempo);

    /**
     * @brief Sets the instrument that receives the arpeggiated note on/off events.
     *
     * @param pInst  Instrument (not owned).
     */
    void setInst(CInst* pInst);

    /**
     * @brief Sets the keyboard state whose held keys are arpeggiated.
     *
     * @param pKbdInfo  Keyboard state (not owned).
     */
    void setKbdInfo(CKeyboardInfo* pKbdInfo);

    /**
     * @brief Tells whether the arpeggiator is playing.
     *
     * @return Non-zero while the arpeggiator is switched on (m_bPlaying).
     */
    int isPlaying();

    /**
     * @brief Switches the arpeggiator on or off (see setType()).
     *
     * @param bEnable  1 to enable; any other value disables.
     */
    void setEnable(int bEnable);

    /**
     * @brief Sets the pattern and restarts or stops the arpeggiator according to the enable state.
     *
     * @param type  ARP_UPDOWN, ARP_UP, ARP_DOWN or ARP_RANDOM.
     */
    void setType(int type);

    /**
     * @brief Starts playing from the beginning of a step in the lowest octave.
     *
     * The direction starts downwards for ARP_DOWN, upwards otherwise.
     */
    void arpeggiatorOn();

    /**
     * @brief Stops playing and releases the note that is sounding, if any.
     */
    void arpeggiatorOff();

    /**
     * @brief Sets the number of extra octaves the pattern covers.
     *
     * @param range  Extra octaves, 0..3.
     */
    void setOctRange(int range);

    /**
     * @brief Sets the step length and recalculates the step length in samples.
     *
     * @param beat  Index into the step length table, 0 (1/1) .. 18 (1/64); clamped to that range.
     */
    void setBeat(int beat);

    /**
     * @brief Recalculates m_cycleTime from the tempo and step length, rescaling the position in the step.
     */
    void recalcCycleTime();

    /**
     * @brief Sets the gate time (note length as a fraction of the step).
     *
     * @param val  0..127, mapped to 0..1.
     */
    void setGateTime(int val);

    /**
     * @brief Restarts the pattern from the first step (called when the first key is pressed or all
     *        keys are released).
     *
     * @param velocity  Velocity used for the following arpeggiated notes.
     */
    void timingReset(int velocity);

    /**
     * @brief Computes the number of samples until processArpeggiator() must be called next.
     *
     * @return 0 at the start of a step or at the end of the gate time, otherwise the samples left
     *         until the gate end or the next step.
     */
    int getNextWaitTime();

    /**
     * @brief Advances the position in the step, wrapping at the step length.
     *
     * @param size  Number of samples rendered.
     */
    void incCurTime(int size);

    /**
     * @brief Handles a step start or gate end.
     *
     * Releases the sounding note. At the start of a step it picks the next note according to the
     * pattern, octave range and direction and sends a note on. With a gate time of 0 no note is
     * played.
     *
     * @return Number of samples until the next call (gate end or next step).
     */
    int processArpeggiator();

    /**
     * @brief Finds the next held key above m_curNote (or the lowest held key if m_curNote is -1).
     *
     * @return Key number 0..127, or -1 if there is none.
     */
    int GetUpArpeggioNote();

    /**
     * @brief Finds the next held key below m_curNote (or the highest held key if m_curNote is -1).
     *
     * @return Key number, or -1 if there is none.
     * @note  Key 0 is never returned (it is treated as "not found").
     */
    int GetDownArpeggioNote();

    /**
     * @brief Picks one of the held keys at random (rand()).
     *
     * @return Key number 0..127, or -1 if no key is held.
     */
    int getRandomArpNote();

    CInst* m_pInst;                 // +0x08
    int m_samplingFreq;             // +0x10
    int m_bPlaying;                 // +0x14
    float m_tempo;                  // +0x18  BPM
    int m_cycleTime;                // +0x1c  samples per step
    int m_octRange;                 // +0x20  0..3
    float m_gateTime;               // +0x24  0..1
    int m_beat;                     // +0x28  step length in 1/24 beats
    int m_curTime;                  // +0x2c  position in the step
    int m_bNewStep;                 // +0x30
    CKeyboardInfo* m_pKbdInfo;      // +0x38
    int m_bDown;                    // +0x40  current direction
    int m_curOct;                   // +0x44
    int m_curNote;                  // +0x48  key (without octave), -1 = none
    int m_playingNote;              // +0x4c  -1 = none
    int m_velocity;                 // +0x50
    int m_type;                     // +0x54
    int m_bEnable;                  // +0x58
    char _pad_005c[0x68 - 0x5c];    // +0x5c
};
