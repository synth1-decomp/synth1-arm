#pragma once

/**
 * @file pan.h
 * @brief Stereo balance and LFO auto-pan device (CPan).
 */

#include "sigdev.h"

class CLFO;

/**
 * @brief Stereo balance with optional LFO auto-pan (two LFOs can target the pan).
 *
 * The balance attenuates one channel linearly. When an LFO targets the pan, the sum of the LFO
 * values is turned into per-channel gains (0..1.5) that are smoothed with a 2 ms half-life.
 */
class CPan : public CSignalDevice {
public:
    /**
     * @brief Sets a centred balance and calculates the smoothing coefficient.
     */
    CPan();

    /**
     * @brief Recalculates the gain smoothing coefficient (one-pole, 2 ms half-life).
     */
    void calcCoef();

    /**
     * @brief Recalculates the smoothing coefficient for the new sampling rate.
     */
    virtual void onChangeSamplingFreq();

    /**
     * @brief Sets the two LFOs that may modulate the pan and disables both.
     *
     * @param pLfo1  First LFO.
     * @param pLfo2  Second LFO.
     */
    void setLfo(CLFO* pLfo1, CLFO* pLfo2);

    /**
     * @brief Enables or disables pan modulation by one LFO.
     *
     * @param no       LFO index, 0 or 1.
     * @param bEnable  Non-zero to enable.
     */
    void setLfoEnable(int no, int bEnable);

    /**
     * @brief Sets the balance.
     *
     * @param balance  -1 = left only .. 0 = centre .. 1 = right only.
     */
    void setBalance(float balance);

    /**
     * @brief Re-reads an LFO's settings: modulation is enabled when the LFO is on and its
     *        destination is LFO_DEST_PAN.
     *
     * @param no  LFO index, 0 or 1.
     */
    void changeLFOSetting(int no);

    /**
     * @brief Mono processing; not supported. Does nothing.
     *
     * @param size        Number of samples.
     * @param pBuf        Unused.
     * @param pProcessed  Unused (not written).
     * @return size.
     */
    virtual int processSignal(int size, float* pBuf, int* pProcessed);

    /**
     * @brief Applies the LFO pan gains to a run of frames during which the LFO value is constant.
     *
     * With v = lfoVal / 64, the left target gain is (v + 1)^3 for v < 0 and 0.5 * (v - 1)^3 + 1.5
     * otherwise (1 at the centre, 0 at -1, 1.5 at +1); the right gain uses -v. The targets are
     * multiplied by the balance gains and approached sample by sample with the smoothing coefficient.
     *
     * @param[in,out] pBuf     Stereo frames to scale.
     * @param size             Number of frames.
     * @param gainL            Left balance gain.
     * @param gainR            Right balance gain.
     * @param lfoVal           Sum of the active LFO values.
     * @param[in,out] pCurL    Smoothed left gain.
     * @param[in,out] pCurR    Smoothed right gain.
     * @return size.
     */
    int calcLfoPanMain(tagStereoData* pBuf, int size, float gainL, float gainR, int lfoVal, float* pCurL, float* pCurR);

    /**
     * @brief Pulls a stereo block from the input device and applies balance and auto-pan.
     *
     * Silent blocks are passed through untouched. With LFO modulation the block is split into runs
     * of constant LFO values (CLFO::getSameValAndTime()).
     *
     * @param size                Number of stereo frames requested.
     * @param[in,out] pBuf        Interleaved stereo buffer.
     * @param[in,out] pProcessed  Silence flag set by the input device.
     * @return The number of frames from the input device, or size when no LFO is active.
     */
    virtual int processSignalW(int size, float* pBuf, int* pProcessed);

protected:
    float m_balance;        // +0x14  -1..1
    CLFO* m_pLfo[2];        // +0x18
    int m_bLfoEnable[2];    // +0x28
    float m_curGainL;       // +0x30  smoothed LFO gain
    float m_curGainR;       // +0x34
    float m_smoothCoef;     // +0x38
};
