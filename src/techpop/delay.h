#pragma once

/**
 * @file delay.h
 * @brief CDelay: stereo, cross feedback and ping-pong delay effect.
 */

#include "sigdev.h"

#define DELAY_BUFSIZE       0x20000     // samples per channel (power of 2)
#define DELAY_BUFMASK       (DELAY_BUFSIZE - 1)
#define DELAY_OUTBUFSIZE    256         // stereo samples per block

#define DELAY_TYPE_STEREO   0
#define DELAY_TYPE_CROSS    1
#define DELAY_TYPE_PINGPONG 2

/**
 * @brief Stereo / cross / ping-pong delay.
 *
 * The dry signal passes through the processed buffer (scaled by m_dry); the wet signal is written to
 * m_pOut (scaled by m_wet) and the caller mixes it. Delay time changes glide smoothly while the tail
 * is still audible. Up to four insertion filters (e.g. the delay tone EQ) are applied to the delayed
 * signal before it is fed back.
 *
 * @note Owns m_pBuf and m_pOut (aligned allocations freed in the destructor).
 */
// sizeof == 0xb0
class CDelay : public CSignalDevice {
public:
    /**
     * @brief Allocates the delay lines and output buffer; stereo type, feedback 0.7, delay time 100 s (clamped to the line).
     */
    CDelay();

    /**
     * @brief Frees the delay lines and the output buffer.
     */
    virtual ~CDelay();

    /**
     * @brief Clears the delay lines, resets the write position and stops any delay time glide.
     *
     * @note In the stereo layout the four floats between the two halves of m_pBuf are not cleared.
     */
    void resetPointers();

    /**
     * @brief Sets the delay time and recomputes the delay lengths and the release time.
     *
     * @param t  Delay time in seconds.
     */
    void setDelayTime(float t);

    /**
     * @brief Computes the glide coefficient for delay time changes (half-way in 150 ms).
     */
    void calcCoef();

    /**
     * @brief Recomputes the delay lengths and the glide coefficient for the new sampling rate.
     */
    virtual void onChangeSamplingFreq();

    /**
     * @brief Sets the delay type and recomputes the release time.
     *
     * @param type  DELAY_TYPE_STEREO, DELAY_TYPE_CROSS or DELAY_TYPE_PINGPONG.
     */
    void setType(int type);

    /**
     * @brief Computes how long the tail stays audible after the input stops (m_releaseTime).
     *
     * Uses the number of repeats until the feedback has decayed below 1/32768, times the longer of
     * the two delays (doubled for ping-pong).
     */
    void calcReleaseTime();

    /**
     * @brief Computes the left and right delay lengths from m_delaySample and the time balance.
     *
     * The base length is rounded down to a multiple of 4 samples; the time balance (seconds) is added
     * to the left channel if positive or to the right if negative. Both lengths are clamped to
     * 0.1 ms .. m_bufSize - 256 samples. While the tail is audible the change glides; otherwise it
     * takes effect immediately.
     */
    void calcDelaySample();

    /**
     * @brief Sets the base delay length directly and recomputes the delays and the release time.
     *
     * @param n  Delay length in samples.
     */
    void setDelaySample(int n);

    /**
     * @brief Sets the left/right delay time offset and recomputes the delays.
     *
     * @param bal  Offset in seconds; positive delays the left channel, negative the right.
     */
    void setTimeBalance(float bal);

    /**
     * @brief Sets the feedback amount and recomputes the release time.
     *
     * @param fb  Feedback gain, clamped to 0..0.999.
     */
    void setFeedback(float fb);

    /**
     * @brief Sets the dry/wet balance.
     *
     * @param v  0..127 (64: dry and wet both 1.0); see calcDryWet().
     */
    void setDelayLevel(int v);

    /**
     * @brief Does nothing (kept for the interface).
     *
     * @param v  Ignored.
     */
    void setMixDelay(int v);

    /**
     * @brief Enables or disables the delay; enabling clears the delay lines.
     *
     * @param b  1 to enable, 0 to disable.
     */
    void setEnable(int b);

    /**
     * @brief Sets the insertion filters applied to the delayed signal (each may be NULL; not owned).
     *
     * They are run with processSignalWForced() and a NULL pProcessed pointer.
     *
     * @param p1  First filter, or NULL.
     * @param p2  Second filter, or NULL.
     * @param p3  Third filter, or NULL.
     * @param p4  Fourth filter, or NULL.
     */
    void setInsertionFilter(CSignalDevice* p1, CSignalDevice* p2, CSignalDevice* p3, CSignalDevice* p4);

    /**
     * @brief Mono processing is not supported: does nothing.
     *
     * @param size        Number of samples.
     * @param pBuf        Not used (left unchanged).
     * @param pProcessed  Not used.
     * @return size.
     */
    virtual int processSignal(int size, float* pBuf, int* pProcessed);

    /**
     * @brief Runs the ping-pong delay for one block.
     *
     * The input is summed to mono into the left line; the left output feeds the right line and the
     * right output is fed back into the left line with the feedback gain. The block may be cut
     * short so that no sample is read before it is written.
     *
     * @param size       Number of stereo samples requested.
     * @param pIn        Input (dry) samples.
     * @param[out] pOut  Delayed (wet) samples.
     * @return The number of stereo samples processed (<= size).
     */
    int doPingPongDelay(int size, tagStereoData* pIn, tagStereoData* pOut);

    /**
     * @brief Runs the stereo or cross feedback delay for one block.
     *
     * In stereo mode each channel feeds back into itself; in cross mode the input and feedback of
     * each channel go to the opposite line. The block may be cut short so that no sample is read
     * before it is written.
     *
     * @param size       Number of stereo samples requested.
     * @param pIn        Input (dry) samples.
     * @param[out] pOut  Delayed (wet) samples.
     * @return The number of stereo samples processed (<= size).
     */
    int doStereoDelay(int size, tagStereoData* pIn, tagStereoData* pOut);

    /**
     * @brief Pulls a stereo block from the input and produces the delayed signal in m_pOut.
     *
     * When the input is silent the delay keeps running until the tail has decayed
     * (m_releaseTime); after that, or when disabled, m_bOutput is cleared and the input is returned
     * unchanged. Otherwise pBuf is scaled by the dry level, m_pOut is filled with the wet signal and
     * m_bOutput is set.
     *
     * @param size                Number of stereo samples, at most DELAY_OUTBUFSIZE.
     * @param[in,out] pBuf        Interleaved stereo buffer.
     * @param[in,out] pProcessed  Silence flag of pBuf: set by the input device, cleared when pBuf is zero-filled.
     * @return The number of stereo samples produced by the input device (size if there is no input).
     */
    virtual int processSignalW(int size, float* pBuf, int* pProcessed);

    float* m_pOut;                  // +0x18  wet output (DELAY_OUTBUFSIZE stereo samples)
    int m_bOutput;                  // +0x20  m_pOut holds valid data
    float* m_pBuf;                  // +0x28  delay line (stereo: tagStereoData[DELAY_BUFSIZE]; ping-pong: L)
    float* m_pBufR;                 // +0x30  ping-pong: R line
    int m_bufSize;                  // +0x38
    unsigned int m_writePos;        // +0x3c
    int m_bEnable;                  // +0x40
    int m_type;                     // +0x44  DELAY_TYPE_xxx
    float m_feedback;               // +0x48
    float m_wet;                    // +0x4c
    float m_dry;                    // +0x50
    float m_timeBalance;            // +0x54  -1..1, L/R delay time offset (seconds)
    int m_delaySample;              // +0x58
    float m_delayTime;              // +0x5c
    int m_delayL;                   // +0x60  samples
    int m_delayR;                   // +0x64
    int m_releaseTime;              // +0x68  samples until the tail has decayed
    int m_silentCount;              // +0x6c
    int m_bSmoothing;               // +0x70  delay time is gliding to m_delayL/R
    double m_curDelayL;             // +0x78
    double m_curDelayR;             // +0x80
    float m_smoothCoef;             // +0x88
    CSignalDevice* m_pInsFilter[4]; // +0x90
};
