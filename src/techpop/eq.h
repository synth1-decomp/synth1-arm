#pragma once

/**
 * @file eq.h
 * @brief CEq: one equalizer band, either a peaking biquad or a first-order low/high pass.
 */

#include "sigdev.h"

#define EQ_MODE_PEAKING     0
#define EQ_MODE_LOWPASS     1
#define EQ_MODE_HIGHPASS    2

/**
 * @brief One band of EQ: peaking (biquad) or 1st-order low/high pass.
 *
 * Used for the main peaking EQ, the tone (LPF/HPF) control and the delay tone filter. The band is
 * bypassed when it would have no effect (0 dB peaking gain, low pass at or above Nyquist, high pass
 * at or below 1 Hz).
 */
class CEq : public CSignalDevice {
public:
    /**
     * @brief Constructs an enabled stereo peaking band at 100 Hz, Q 3, 0 dB.
     */
    CEq();

    /**
     * @brief Updates the silence timeout (200 ms) and recomputes the coefficients for the new rate.
     */
    virtual void onChangeSamplingFreq();

    /**
     * @brief Computes the filter coefficients for the current mode, frequency, gain and Q.
     *
     * Peaking: poles and zeros are placed from the analog prototype (damping 1 / (2Q), scaled by
     * the gain) and the result is normalized so that the gain at m_freq is the requested gain.
     * Low/high pass: bilinear transform of a first-order section.
     */
    void calcCoefficients();

    /**
     * @brief Sets the filter mode and recomputes the coefficients.
     *
     * @param mode  EQ_MODE_PEAKING, EQ_MODE_LOWPASS or EQ_MODE_HIGHPASS.
     */
    void setMode(int mode);

    /**
     * @brief Selects stereo or mono processing for processSignalW().
     *
     * @param bStereo  Nonzero to filter both channels; zero processes mono and duplicates it to stereo.
     */
    void setStereoMode(int bStereo);

    /**
     * @brief Sets the gain.
     *
     * Peaking mode: the boost/cut at the center frequency (coefficients are recomputed). Low/high
     * pass modes: an output level applied after the filter.
     *
     * @param gain  Gain in dB.
     */
    void setGain(float gain);

    /**
     * @brief Sets the center / cutoff frequency and recomputes the coefficients.
     *
     * @param freq  Frequency in Hz, clamped to at least 1 Hz.
     */
    void setFreq(float freq);

    /**
     * @brief Sets the quality factor of the peaking filter and recomputes the coefficients.
     *
     * @param q  Quality factor.
     */
    void setQ(float q);

    /**
     * @brief Pulls a stereo block from the input and filters it in place.
     *
     * In mono mode the base class implementation is used. When the input is silent, the filter keeps
     * running on a tiny constant (1e-6) for up to 200 ms so its tail decays.
     *
     * @param size                Number of stereo samples.
     * @param[in,out] pBuf        Interleaved stereo buffer.
     * @param[in,out] pProcessed  Silence flag of pBuf (set by the input); cleared when a silent buffer is refilled.
     * @return The number of samples produced by the input device.
     */
    virtual int processSignalW(int size, float* pBuf, int* pProcessed);

    /**
     * @brief Pulls a mono block from the input and filters it in place (same rules as processSignalW()).
     *
     * @param size                Number of samples.
     * @param[in,out] pBuf        Sample buffer.
     * @param[in,out] pProcessed  Silence flag of pBuf (set by the input); cleared when a silent buffer is refilled.
     * @return The number of samples produced by the input device.
     */
    virtual int processSignal(int size, float* pBuf, int* pProcessed);

    /**
     * @brief Filters a mono buffer in place without pulling the input (left-channel state).
     *
     * @param size            Number of samples.
     * @param[in,out] pBuf    Sample buffer.
     * @param pProcessed      Not used (may be NULL).
     * @return size.
     */
    virtual int processSignalForced(int size, float* pBuf, int* pProcessed);

    /**
     * @brief Filters an interleaved stereo buffer in place without pulling the input.
     *
     * @param size            Number of stereo samples.
     * @param[in,out] pBuf    Interleaved stereo buffer.
     * @param pProcessed      Not used (may be NULL).
     * @return size.
     */
    virtual int processSignalWForced(int size, float* pBuf, int* pProcessed);

protected:
    int m_mode;                 // +0x14  EQ_MODE_xxx
    int m_bEnable;              // +0x18
    int m_bStereo;              // +0x1c
    int m_silentCount;          // +0x20
    int m_silentLimit;          // +0x24
    float m_gain;               // +0x28  dB (peaking)
    float m_level;              // +0x2c  linear gain (low/high pass)
    float m_freq;               // +0x30  Hz
    float m_q;                  // +0x34
    float m_invFs;              // +0x38
    float m_b0;                 // +0x3c
    float m_b1;                 // +0x40
    float m_b2;                 // +0x44
    float m_nb1;                // +0x48  b1, b2 normalized by the gain at m_freq
    float m_nb2;                // +0x4c
    float m_a1;                 // +0x50
    float m_a2;                 // +0x54
    tagStereoData m_x1;         // +0x58
    tagStereoData m_x2;         // +0x60
    tagStereoData m_y1;         // +0x68
    tagStereoData m_y2;         // +0x70
    int _pad_0078[2];           // +0x78  unused (sizeof(CEq) == 0x80 from CSynthesizer1 layout)
    friend class CSynthesizer1; // writes m_bEnable of the delay tone EQ (+0x18)
};
