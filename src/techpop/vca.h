#pragma once

/**
 * @file vca.h
 * @brief CVca: the per-voice amplifier with its own envelope.
 */

#include "envelope.h"

/**
 * @brief Amplifier with its own envelope.  sizeof == 0xb0
 *
 * Last stage of a voice; its input is the voice's CVcf. Applies the amp envelope, amp gain,
 * velocity gain and amp LFOs with per-block gain ramps.
 */
class CVca : public CEnvelopedObject {
public:
    /**
     * @brief Sets the defaults: envelope max 2048, amp gain 0.5, no velocity sensitivity.
     */
    CVca();
    virtual ~CVca(){}

    /**
     * @brief Sets the amp gain; the change is applied as a ramp on the next block.
     *
     * @param gain  Linear gain (0..1 from the panel).
     */
    void setGain(float gain);

    /**
     * @brief Enables LFO @p no for the amplifier if it is switched on and its destination is LFO_DEST_AMP.
     *
     * @param no        LFO index, 0 or 1.
     * @param bEnable   Nonzero if the LFO is switched on.
     * @param dest      LFO destination (LFO_DEST_*).
     */
    void changeLfoSetting(int no, int bEnable, int dest);

    /**
     * @brief Multiplies one block by the envelope gain in place.
     *
     * Gain = level / 2048 * amp gain * velocity gain, reduced by each LFO as
     * (2048 - lfo) / 2048. When anything changed since the last block the gain ramps linearly
     * from the previous value over the block; otherwise the constant gain is applied.
     *
     * @param level         Envelope level, 0..2048.
     * @param[in,out] pBuf  Samples.
     * @param size          Number of samples.
     * @param lfo1          LFO 1 value.
     * @param lfo2          LFO 2 value.
     * @return 1 if the gain is constant zero and the buffer was left untouched (silent), else 0.
     */
    virtual int processEnvelopedSignal(int level, float* pBuf, int size, int lfo1, int lfo2);

    /**
     * @brief Called when the release has finished: clears the gain and resets the input filter
     *        (CVcf::resetFilterBuf()) and the oscillator's FM smoothing and envelope.
     *
     * @note Assumes the input chain is CVca <- CVcf <- CVco, as set up by CVoice.
     */
    virtual void releaseEnd();

    /**
     * @brief Sets the velocity sensitivity used at the next note on.
     *
     * @param sens  0..1; at 1 velocity 0 attenuates by 30 dB.
     */
    void setVelocitySens(float sens);

    /**
     * @brief Computes the velocity gain and starts the envelope.
     *
     * @param velocity  Note velocity, 0..127.
     */
    void noteOn(int velocity);

    /**
     * @brief Starts the envelope release.
     */
    void noteOff();

public:
    float m_curGain;        // +0x8c  gain reached at the end of the last block
    float m_targetGain;     // +0x90
    int m_lastLevel;        // +0x94  last envelope level
    int m_lastLfo1;         // +0x98
    int m_lastLfo2;         // +0x9c
    float m_gain;           // +0xa0  amp gain
    float m_velGain;        // +0xa4  velocity gain
    float m_velocitySens;   // +0xa8
    int m_bGainChanged;     // +0xac
};
