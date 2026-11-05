#pragma once

/**
 * @file phatcont.h
 * @brief "Phat" controller (CPhatCont): one-pole feedback that thickens a mono signal.
 */

#include "sigdev.h"

/**
 * @brief "Phat" controller: a one-pole feedback (leaky integrator) that fattens the
 *        sound and keeps ringing for a while after the input became silent.
 *
 * out = gain * s, with s = s * coef + in.
 */
class CPhatCont : public CSignalDevice {
public:
    /**
     * @brief Starts with the effect off (level 0) and in the idle state.
     */
    CPhatCont();

    /**
     * @brief Does nothing.
     */
    virtual ~CPhatCont();

    /**
     * @brief Sets the amount of feedback.
     *
     * coef = sqrt(level), capped at 0.99; gain = 1 - 0.9 * coef^2. A level of 0 switches the
     * effect off and clears the state. Also recalculates the release time.
     *
     * @param level  0..1.
     */
    void setPhatLevel(float level);

    /**
     * @brief Recalculates m_releaseTime, the number of samples for the feedback to decay to 1/32768
     *        (about -90 dB) plus two; restarts the silence counter if the release got shorter.
     */
    void calcReleaseTime();

    /**
     * @brief Pulls a mono block from the input device and applies the feedback.
     *
     * While the input is silent the decaying tail is rendered for m_releaseTime samples; after
     * that the state is cleared and the buffer left silent. Does nothing when the level is 0.
     *
     * @param size                Number of samples requested.
     * @param[in,out] pBuf        Sample buffer.
     * @param[in,out] pProcessed  Silence flag; set to 1 when there is no input device, cleared when
     *                            a tail is rendered into a silent buffer.
     * @return Number of samples produced by the input device (size if there is none).
     */
    virtual int processSignal(int size, float* pBuf, int* pProcessed);

    float m_coef;       // +0x14  feedback coefficient
    float m_gain;       // +0x18  output gain compensation
    float m_state;      // +0x1c  last output
    int m_releaseTime;  // +0x20  samples until the tail is below -90dB
    int m_silentCount;  // +0x24  samples since the input became silent
};
