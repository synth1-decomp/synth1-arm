#pragma once

/**
 * @file chorus.h
 * @brief Chorus / flanger effect (CChorus) with mono, stereo and ensemble modes.
 */

#include "sigdev.h"

#define CHORUS_BUFSIZE  0x2000      // stereo samples (power of 2)
#define CHORUS_BUFMASK  (CHORUS_BUFSIZE - 1)

// chorus kinds (setKind)
#define CHORUS_KIND_MONO        1   // both channels modulated by the same LFO phase
#define CHORUS_KIND_STEREO      2   // L by sin, R by cos
#define CHORUS_KIND_ENSEMBLE    4   // two taps per channel, cross mixed

/**
 * @brief Chorus / flanger: a stereo delay line read by LFO-modulated taps.
 *
 * The input, scaled by the level, is written into the delay line together with the delay effect's
 * output (CDelay::m_pOut / m_bOutput, see setDelaySigPointer()). The modulated taps are added to the
 * buffer, so the dry signal passes through unchanged. The LFO is a "magic circle" sine/cosine
 * oscillator. sizeof == 0xb0.
 */
class CChorus : public CSignalDevice {
public:
    /**
     * @brief Allocates and clears the delay line and sets the defaults.
     *
     * Defaults: delay 0.02 s, depth 0.5, rate 1 Hz, no feedback, level 0, stereo mode, enabled.
     */
    CChorus();

    /**
     * @brief Sets the centre delay time.
     *
     * Updates m_delaySample, the modulation depth in samples, the write position relative to the read
     * position, the release time and the depth coefficient.
     *
     * @param t  Delay in seconds.
     */
    void setDelayTime(float t);

    /**
     * @brief Sets the modulation depth as a fraction of the delay time.
     *
     * Updates m_depthSample (= delay samples * depth), m_step and the depth coefficient.
     *
     * @param depth  0..1.
     */
    void setDepth(float depth);

    /**
     * @brief Sets the LFO rate.
     *
     * Updates the LFO period in samples, m_step, the oscillator coefficient and the depth coefficient.
     *
     * @param rate  LFO frequency in Hz (must be non-zero).
     */
    void setRate(float rate);

    /**
     * @brief Sets the feedback gain and recalculates the release time.
     *
     * @param fb  Feedback gain; negative values invert the fed-back signal (about -0.99..0.97).
     */
    void setFeedback(float fb);

    /**
     * @brief Sets the gain applied to the input before it is written into the delay line.
     *
     * @param level  Gain, 0..1.
     */
    void setLevel(float level);

    /**
     * @brief Frees the delay line.
     */
    virtual ~CChorus();

    /**
     * @brief Recomputes the sample-based values from the stored delay time and rate.
     */
    virtual void onChangeSamplingFreq();

    /**
     * @brief Enables or bypasses the effect.
     *
     * The delay line is cleared when the effect is switched on from the off state.
     *
     * @param b  Non-zero to enable.
     */
    void setEnable(int b);

    /**
     * @brief Selects the chorus mode.
     *
     * @param kind  CHORUS_KIND_MONO, CHORUS_KIND_STEREO or CHORUS_KIND_ENSEMBLE; any other value
     *              selects CHORUS_KIND_STEREO.
     */
    void setKind(int kind);

    /**
     * @brief Recalculates the tail length used to stop processing after the input falls silent.
     *
     * m_releaseTime is the number of feedback passes needed to decay to 1/32768 (about -90 dB),
     * plus two, times the delay in samples. If the effect was already idle (m_silentCount past the
     * old release time) it stays idle, otherwise the silence counter restarts at 0.
     */
    void calcReleaseTime();

    /**
     * @brief Recalculates m_depthCoef, which scales the depth down at high rates.
     *
     * m_depthCoef = min(1, period / (2 * pi * depthSample)), so the tap never moves faster than one
     * sample per sample.
     */
    void calcDepthCoef();

    /**
     * @brief Connects the delay effect's output, which is mixed into the delay line (or into the
     *        output buffer while the chorus is disabled).
     *
     * @param pSig     Interleaved stereo output buffer of the delay effect.
     * @param pbValid  Flag that is non-zero while pSig holds a signal.
     */
    void setDelaySigPointer(float* pSig, int* pbValid);

    /**
     * @brief Mono processing; not supported. Does nothing and marks the buffer as silent.
     *
     * @param size             Number of samples.
     * @param pBuf             Unused.
     * @param[out] pProcessed  Set to 1.
     * @return size.
     */
    virtual int processSignal(int size, float* pBuf, int* pProcessed);

    /**
     * @brief Pulls a block from the input device and adds the chorus signal to it.
     *
     * When disabled, only the delay effect's output is mixed into the buffer. When the input and the
     * delay output are both silent, processing continues for m_releaseTime samples and then stops.
     * A silent input is replaced by 1e-9 before processing (denormal guard).
     *
     * @param size                 Number of stereo frames requested.
     * @param[in,out] pBuf         Interleaved stereo buffer.
     * @param[in,out] pProcessed   Silence flag: non-zero means the buffer holds no signal. Set by the
     *                             input device and cleared when the chorus writes output.
     * @return Number of frames produced by the input device.
     */
    virtual int processSignalW(int size, float* pBuf, int* pProcessed);

    int _pad_0014[3];           // +0x14  unused
    tagStereoData* m_pBuf;      // +0x20  delay line, CHORUS_BUFSIZE + 1 samples
    int m_silentCount;          // +0x28
    int m_releaseTime;          // +0x2c
    int m_bEnable;              // +0x30
    int m_kind;                 // +0x34  CHORUS_KIND_xxx
    unsigned int m_writePos;    // +0x38
    unsigned int m_readPos;     // +0x3c
    float m_inLevel[2];         // +0x40  (1.0, 1.0)
    unsigned int m_delaySample; // +0x48
    int m_depthSample;          // +0x4c
    float m_depthCoef;          // +0x50
    float m_depth;              // +0x54
    int m_step;                 // +0x58
    float m_feedback;           // +0x5c
    int m_bFeedback;            // +0x60
    float m_level;              // +0x64
    float* m_pDelaySig;         // +0x68
    int* m_pbDelaySigValid;     // +0x70
    int m_period;               // +0x78  LFO period in samples
    float m_oscCoef;            // +0x7c  2*sin(pi*rate/fs)
    float m_oscSin;             // +0x80  "magic circle" oscillator
    float m_oscCos;             // +0x84
    int _pad_0088[8];           // +0x88  unused
    float m_delayTime;          // +0xa8
    float m_rate;               // +0xac
};
