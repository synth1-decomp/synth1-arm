#pragma once

/**
 * @file phaser.h
 * @brief Multi-stage all-pass phaser (CPhaser) with an exponential frequency sweep.
 */

#include "sigdev.h"


#define PHASER_MAX_STAGE    6

/**
 * @brief State of one all-pass stage (two first-order sections).
 */
typedef struct tagAllPassState {
    float z1;
    float z2;
} AllPassState;

/**
 * @brief Phaser: a chain of all-pass stages whose break frequency sweeps between a minimum and
 *        a maximum, mixed with the dry signal.
 *
 * The frequency is multiplied by a constant factor every sample, so it sweeps exponentially from
 * minimum to maximum in half an LFO period and back (the direction is checked once per block).
 * The output of the chain is fed back into its input. Processes the buffer in place.
 */
class CPhaser : public CSignalDevice {
public:
    /**
     * @brief Clears the state and sets 1 stage, 200..10000 Hz, 0.1 Hz, feedback 0.7 and depth 0.5.
     */
    CPhaser();

    /**
     * @brief Does nothing.
     */
    virtual ~CPhaser();

    /**
     * @brief Sets the number of all-pass stages.
     *
     * @param n  1..PHASER_MAX_STAGE.
     */
    void setStage(int n);

    /**
     * @brief Sets the sweep range and recalculates the per-sample sweep factor.
     *
     * @param minFreq  Lower end in Hz.
     * @param maxFreq  Upper end in Hz.
     */
    void setRange(float minFreq, float maxFreq);

    /**
     * @brief Sets the sweep rate and recalculates the per-sample sweep factor, keeping the current
     *        sweep direction.
     *
     * @param rate  Sweep (LFO) frequency in Hz.
     */
    void setRate(float rate);

    /**
     * @brief Recalculates the sampling-rate dependent values (calcFsParams() and the sweep factor).
     */
    virtual void onChangeSamplingFreq();

    /**
     * @brief Enables or bypasses the phaser.
     *
     * @param b  Non-zero to enable.
     */
    void setEnable(int b);

    /**
     * @brief Sets the feedback gain.
     *
     * @param v  Feedback gain, about -0.99..0.99.
     */
    void setFeedback(float v);

    /**
     * @brief Sets the wet/dry mix.
     *
     * @param v  0 = dry, 1 = all-pass output only.
     */
    void setDepth(float v);

    /**
     * @brief Recalculates the silence tail limit (25 ms) and the frequency coefficient (4 / fs).
     */
    void calcFsParams(){
        m_silentLimit = (int)(m_samplingFreq * 0.025f);
        m_freqCoef = 4.0f / m_samplingFreq;
    }

    /**
     * @brief Processes a mono buffer in place.
     *
     * @param size                Number of samples.
     * @param[in,out] pBuf        Sample buffer.
     * @param[in,out] pProcessed  Silence flag; cleared when the tail is rendered into a silent buffer.
     * @return size.
     * @see processSignalMain()
     */
    virtual int processSignal(int size, float* pBuf, int* pProcessed);

    /**
     * @brief Common entry for mono and stereo processing.
     *
     * A silent input is replaced by zeros and processed for another 25 ms (the tail), after which
     * the buffer is left alone. Reverses the sweep direction if the frequency has left the range.
     *
     * @param size                Number of samples (mono) or stereo frames.
     * @param[in,out] pBuf        Sample buffer.
     * @param[in,out] pProcessed  Silence flag; cleared when the tail is rendered into a silent buffer.
     * @param bStereo             Non-zero for an interleaved stereo buffer.
     * @return size.
     */
    int processSignalMain(int size, float* pBuf, int* pProcessed, int bStereo);

    /**
     * @brief Processes an interleaved stereo buffer in place.
     *
     * @param size                Number of stereo frames.
     * @param[in,out] pBuf        Interleaved stereo buffer.
     * @param[in,out] pProcessed  Silence flag; cleared when the tail is rendered into a silent buffer.
     * @return size.
     * @see processSignalMain()
     */
    virtual int processSignalW(int size, float* pBuf, int* pProcessed);

    /**
     * @brief Runs the all-pass chain over a mono buffer (left channel state).
     *
     * @param size           Number of samples.
     * @param[in,out] pBuf   Sample buffer.
     */
    void processMono(int size, float* pBuf);

    /**
     * @brief Runs the all-pass chains over a stereo buffer; both channels share the sweep.
     *
     * @param size           Number of stereo frames.
     * @param[in,out] pBuf   Stereo buffer.
     */
    void processStereo(int size, tagStereoData* pBuf);

protected:
    int m_bEnable;                          // +0x14
    int m_nStage;                           // +0x18
    float m_minFreq;                        // +0x1c
    float m_maxFreq;                        // +0x20
    float m_rate;                           // +0x24
    float m_feedback;                       // +0x28
    float m_depth;                          // +0x2c
    int m_silentCount;                      // +0x30
    int m_silentLimit;                      // +0x34
    AllPassState m_zL[PHASER_MAX_STAGE];    // +0x38
    AllPassState m_zR[PHASER_MAX_STAGE];    // +0x68
    float m_freqCoef;                       // +0x98
    float m_freq;                           // +0x9c
    float m_freqMul;                        // +0xa0
    float m_fbL;                            // +0xa4
    float m_fbR;                            // +0xa8
};
