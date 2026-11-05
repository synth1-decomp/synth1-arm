#pragma once

/**
 * @file compressor.h
 * @brief Compressor effect (CCompressor) and its building blocks: a one-pole envelope follower
 *        and a simple feed-forward compressor.
 */

#include "sigdev.h"

// dB -> natural log factor: exp(dB * DB2EXP) == 10^(dB/20)
#define DB2EXP  0.11512925465f

/**
 * @brief One-pole envelope follower (smoothing filter) with a time constant in ms.
 *
 * sizeof == 0x18.
 */
class EnvelopeDetector {
public:
    /**
     * @brief Leaves the members uninitialised; set the sampling rate and time constant before use.
     */
    EnvelopeDetector(){}

    /**
     * @brief Creates a follower with the given time constant.
     *
     * @param ms          Time constant in ms.
     * @param sampleRate  Sampling rate in Hz.
     */
    EnvelopeDetector(float ms, float sampleRate);

    /**
     * @brief Does nothing.
     */
    virtual ~EnvelopeDetector(){}

    /**
     * @brief Sets the time constant and recalculates the coefficient.
     *
     * @param ms  Time constant in ms.
     */
    virtual void setTc(float ms);

    /**
     * @brief Gets the time constant.
     *
     * @return The time constant in ms.
     */
    virtual float getTc(){ return m_ms; }

    /**
     * @brief Sets the sampling rate and recalculates the coefficient.
     *
     * @param sampleRate  Sampling rate in Hz.
     */
    virtual void setSampleRate(float sampleRate);

    /**
     * @brief Gets the sampling rate.
     *
     * @return The sampling rate in Hz.
     */
    virtual float getSampleRate(){ return m_sampleRate; }

    /**
     * @brief Runs one sample through the filter: state = in + coef * (state - in).
     *
     * @param in              Input sample.
     * @param[in,out] state   Filter state (the envelope).
     */
    inline void run(float in, float& state){ state = in + m_coef * (state - in); }

protected:
    float m_sampleRate;     // +0x08
    float m_ms;             // +0x0c
    float m_coef;           // +0x10

    /**
     * @brief Recalculates m_coef = exp(-1 / (ms * 0.001 * sampleRate)).
     */
    void setCoef();
};

/**
 * @brief Simple feed-forward compressor for one channel.
 *
 * The amount by which |in| exceeds the threshold is smoothed by the attack follower; the gain is
 * thresh / (thresh + envelope). The ratio and the release follower are stored but not used by
 * process(). sizeof == 0x48.
 */
class SimpleComp {
public:
    /**
     * @brief Sets 44100 Hz, attack 10 ms, release 100 ms, threshold 0 dB and ratio 1.
     */
    SimpleComp();

    /**
     * @brief Does nothing.
     */
    virtual ~SimpleComp(){}

    /**
     * @brief Sets the threshold (also stored as a linear amplitude).
     *
     * @param dB  Threshold in dB.
     */
    virtual void setThresh(float dB);

    /**
     * @brief Sets the ratio (stored only; process() does not use it).
     *
     * @param ratio  Compression ratio.
     */
    virtual void setRatio(float ratio);

    /**
     * @brief Sets the attack time constant.
     *
     * @param ms  Time constant in ms.
     */
    virtual void setAttack(float ms);

    /**
     * @brief Sets the release time constant (stored only; process() does not use it).
     *
     * @param ms  Time constant in ms.
     */
    virtual void setRelease(float ms);

    /**
     * @brief Gets the threshold.
     *
     * @return The threshold in dB.
     */
    virtual float getThresh(){ return m_threshdB; }

    /**
     * @brief Gets the ratio.
     *
     * @return The ratio.
     */
    virtual float getRatio(){ return m_ratio; }

    /**
     * @brief Gets the attack time constant.
     *
     * @return The attack time constant in ms.
     */
    virtual float getAttack(){ return m_att.getTc(); }

    /**
     * @brief Gets the release time constant.
     *
     * @return The release time constant in ms.
     */
    virtual float getRelease(){ return m_rel.getTc(); }

    /**
     * @brief Sets the sampling rate of both envelope followers.
     *
     * @param sampleRate  Sampling rate in Hz.
     */
    virtual void setSampleRate(float sampleRate);

    /**
     * @brief Resets the envelope to 0.
     */
    virtual void initRuntime();

    /**
     * @brief Compresses one sample.
     *
     * @param in  Input sample.
     * @return in * thresh / (thresh + envelope), where the envelope follows the amount above the
     *         threshold.
     */
    float process(float in);

protected:
    float m_env;            // +0x08
    float m_thresh;         // +0x0c  linear
    float m_threshdB;       // +0x10
    float m_ratio;          // +0x14
    EnvelopeDetector m_att; // +0x18
    EnvelopeDetector m_rel; // +0x30
};

/**
 * @brief Compressor effect (stereo: one SimpleComp per channel) followed by make-up gain and an
 *        output level.
 *
 * Processes the buffer in place; it does not pull from an input device. sizeof == 0xc0.
 */
class CCompressor : public CSignalDevice {
public:
    /**
     * @brief Sets threshold -10 dB, ratio -1, attack and release 1 ms on both channels.
     */
    CCompressor();

    /**
     * @brief Does nothing.
     */
    virtual ~CCompressor();

    /**
     * @brief Passes the new sampling rate to both channel compressors.
     */
    virtual void onChangeSamplingFreq();

    /**
     * @brief Stores the type (not used by the processing).
     *
     * @param type  Compressor type.
     */
    void setType(int type);

    /**
     * @brief Sets the attack (and release) time of both channels.
     *
     * @param v  0..1, mapped to 0.1 * 2000^sqrt(v) ms (0.1..200 ms).
     */
    void setAttack(float v);

    /**
     * @brief Stores the release value (not used; setAttack() sets the release time).
     *
     * @param v  Release value, 0..1.
     */
    void setRelease(float v);

    /**
     * @brief Stores the ratio value (not used by the processing).
     *
     * @param v  Ratio value.
     */
    void setRatio(float v);

    /**
     * @brief Sets the compression depth.
     *
     * The threshold becomes -20 - 40 * v dB and the make-up gain 10^((-20 - thresh) / 20), which
     * compensates for the lowered threshold.
     *
     * @param v  0..1.
     */
    void setDepth(float v);

    /**
     * @brief Sets the output level.
     *
     * @param v  0..127, mapped to -20..+10 dB.
     */
    void setLevel(int v);

    /**
     * @brief Compresses a mono buffer in place using the left channel compressor.
     *
     * @param size           Number of samples.
     * @param[in,out] pBuf   Sample buffer.
     * @param pProcessed     Silence flag; if non-zero the buffer is left alone and the envelope reset.
     * @return size.
     */
    virtual int processSignal(int size, float* pBuf, int* pProcessed);

    /**
     * @brief Compresses an interleaved stereo buffer in place, one compressor per channel.
     *
     * @param size           Number of stereo frames.
     * @param[in,out] pBuf   Interleaved stereo buffer.
     * @param pProcessed     Silence flag; if non-zero the buffer is left alone and both envelopes reset.
     * @return size.
     */
    virtual int processSignalW(int size, float* pBuf, int* pProcessed);

protected:
    SimpleComp m_comp[2];   // +0x18
    float m_makeup;         // +0xa8
    float m_level;          // +0xac
    int m_type;             // +0xb0
    float m_attack;         // +0xb4
    float m_release;        // +0xb8
    float m_ratio;          // +0xbc
};
