#pragma once

/**
 * @file envelope.h
 * @brief CEnvelopedObject: signal device driven by an ADSR envelope, base of the VCO, VCF and VCA.
 *
 * Envelope levels are integers in 0..m_max scaled by the gain; segment lengths are in samples.
 */

#include "common.h"
#include "sigdev.h"

class CLFO;

// envelope status (m_envStatus)
#define ENV_OFF         0
#define ENV_ATTACK      1
#define ENV_DECAY       2
#define ENV_SUSTAIN     3
#define ENV_RELEASE     4

// decay/release curve type
#define ENV_CURVE_EXP       0
#define ENV_CURVE_LINEAR    1


/**
 * @brief A signal device driven by an ADSR envelope (base of CVca, CVcf, CVco).
 *
 * processSignal() pulls the input (unless m_bNoInput), splits the buffer into blocks where the
 * envelope level and the two optional LFO values are constant, and hands each block to the derived
 * class through processEnvelopedSignal(). The attack is linear; decay and release are linear or
 * exponential (ENV_CURVE_xxx) and are evaluated with a step size from calcDRResolution().
 */
// sizeof == 0x8c (derived classes start their fields at +0x8c)
class CEnvelopedObject : public CSignalDevice {
public:
    /**
     * @brief Constructs an idle envelope with m_max 100, gain 1, full sustain and 1-sample segments.
     */
    CEnvelopedObject();

    /**
     * @brief Destroys the object; nothing to release.
     */
    virtual ~CEnvelopedObject(){}

    /**
     * @brief Sets the full-scale envelope level and rescales the peak, sustain and current levels.
     *
     * @param max  Envelope level at gain 1.0.
     */
    void setMax(int max);

    /**
     * @brief Sets the decay curve type.
     *
     * calcDRResolution() reads this value for both the decay and the release step size.
     *
     * @param type  ENV_CURVE_EXP or ENV_CURVE_LINEAR.
     */
    void setDecayType(int type);

    /**
     * @brief Sets the envelope depth; a negative gain inverts the envelope (m_bInvert).
     *
     * Rescales the peak and sustain levels and the current level (set to the sustain level while
     * sustaining).
     *
     * @param gain  Envelope amount; its absolute value scales m_max.
     */
    void setGain(float gain);

    /**
     * @brief Rescales the attack, decay and release lengths (and the position in the running segment) to the new rate.
     */
    virtual void onChangeSamplingFreq();

    /**
     * @brief Sets the attack time, keeping the relative position if the attack is running.
     *
     * @param sec  Attack time in seconds.
     */
    void setAttack(float sec);

    /**
     * @brief Sets the decay time, keeping the relative position if the decay is running.
     *
     * @param sec  Decay time in seconds.
     */
    void setDecay(float sec);

    /**
     * @brief Sets the release time, keeping the relative position if the release is running.
     *
     * @param sec  Release time in seconds.
     */
    void setRelease(float sec);

    /**
     * @brief Sets the sustain level; takes effect immediately while sustaining.
     *
     * @param level  Sustain level, 0..1 of the peak.
     */
    void setSustain(float level);

    /**
     * @brief Enables or disables the envelope; when disabled, blocks are processed with level 0.
     *
     * @param bEnable  Nonzero to enable.
     */
    void setEnvelopEnable(int bEnable);

    /**
     * @brief Gets the envelope status.
     *
     * @return The current segment, ENV_OFF .. ENV_RELEASE.
     */
    int getEnvStatus();

    /**
     * @brief Starts (or restarts) the attack from the current level.
     */
    void envelopeOn();

    /**
     * @brief Starts the release from the current level; does nothing if already off or releasing.
     */
    void envelopeOff();

    /**
     * @brief Computes the step size (m_resolution) of the running decay or release segment.
     *
     * Linear curves use one step per level unit. Exponential curves use finer steps at the start of
     * the segment, where the curve is steep, and coarser ones later; m_nextResPos is set to the
     * position where the step size must be recomputed. When m_bSmooth is set, longer steps are used
     * because the derived class interpolates within a block.
     */
    void calcDRResolution();

    /**
     * @brief Stops the envelope immediately (status ENV_OFF, level 0).
     */
    void envelopeReset();

    /**
     * @brief Sets the LFO feeding one of the two modulation inputs.
     *
     * @param no    LFO slot, 0 or 1.
     * @param pLfo  The LFO (not owned).
     */
    void setLfo(int no, CLFO* pLfo);

    /**
     * @brief Enables or disables one of the two LFO inputs.
     *
     * @param no       LFO slot, 0 or 1.
     * @param bEnable  Nonzero to pass the LFO value to processEnvelopedSignal().
     */
    void enableLfo(int no, int bEnable);

    /**
     * @brief Produces a block of output.
     *
     * Pulls the input device first (unless the object is a generator), then splits the buffer where
     * the values of the enabled LFOs change and runs processSignalSub() on each piece. Calls
     * postProcessSignal() at the end when m_bPostProcess is set.
     *
     * @param size                Number of samples.
     * @param[in,out] pBuf        Buffer to fill or process.
     * @param[out] pProcessed     Set to 1 if the whole buffer is silent (not written), else 0.
     * @return The number of samples (size, or the count returned by the input device if it was silent).
     */
    virtual int processSignal(int size, float* pBuf, int* pProcessed);

    /**
     * @brief Advances the envelope over a block with constant LFO values.
     *
     * Walks through the attack, decay/release and sustain segments, calling processEnvelopedSignal()
     * for each sub-block with a constant envelope level, zero-filling sub-blocks that report silence
     * when other parts of the buffer are written. Updates the envelope status and calls releaseEnd()
     * when the release finishes.
     *
     * @param size              Number of samples.
     * @param[in,out] pBuf      Buffer to process.
     * @param lfo1              Value of LFO 1 (0 if disabled).
     * @param lfo2              Value of LFO 2 (0 if disabled).
     * @param[out] pProcessed   Set to 1 if the whole block is silent, else 0.
     * @return size.
     */
    int processSignalSub(int size, float* pBuf, int lfo1, int lfo2, int* pProcessed);

    /**
     * @brief Processes one block with a constant envelope level (implemented by the derived class).
     *
     * @param level         Current envelope value (0..m_max, negated when the gain is negative).
     * @param[in,out] pBuf  Samples of the block.
     * @param size          Number of samples.
     * @param lfo1          Value of LFO 1.
     * @param lfo2          Value of LFO 2.
     * @return 1 when the buffer was left untouched (silent), 0 when it was written.
     */
    virtual int processEnvelopedSignal(int level, float* pBuf, int size, int lfo1, int lfo2) = 0;

    /**
     * @brief Called when the release segment has finished; does nothing by default.
     */
    virtual void releaseEnd();

    /**
     * @brief Optional processing of the whole buffer after the envelope pass (when m_bPostProcess is set).
     *
     * @param[in,out] pBuf        Buffer of processSignal().
     * @param size                Number of samples.
     * @param[in,out] pProcessed  Silence flag of the buffer.
     */
    virtual void postProcessSignal(float* pBuf, int size, int* pProcessed){}

public:
    int m_bProcessIdle;     // +0x14  keep processing while the envelope is off
    int m_bNoInput;         // +0x18  generator (no input device)
    int m_bEnvEnable;       // +0x1c
    int m_bInvert;          // +0x20  negative gain
    int m_max;              // +0x24
    int m_decayType;        // +0x28
    int m_releaseType;      // +0x2c
    int m_bSmooth;          // +0x30  derived class interpolates by itself
    int m_envStatus;        // +0x34
    int m_bPostProcess;     // +0x38
    int m_peakLevel;        // +0x3c  m_max * m_gain
    int m_sustainLevel;     // +0x40
    float m_gain;           // +0x44
    int m_attackLen;        // +0x48  in samples
    int m_decayLen;         // +0x4c
    float m_sustain;        // +0x50
    int m_releaseLen;       // +0x54
    int m_curLevel;         // +0x58
    int m_curPos;           // +0x5c
    int m_releaseLevel;     // +0x60  level at note off
    int m_nextResPos;       // +0x64
    int m_resolution;       // +0x68
    CLFO* m_pLfo[2];        // +0x70
    int m_bLfoEnable[2];    // +0x80
    int m_prevSamplingFreq; // +0x88
};
