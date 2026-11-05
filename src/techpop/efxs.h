#pragma once

/**
 * @file efxs.h
 * @brief "Effect" section: one of distortion / compressor / ring modulator /
 *        decimator / phaser, selected by type.
 *
 * Declares the ring modulator, the decimator and the CEffector switch that owns one instance of
 * every effect and routes the signal through the selected one.
 */

#include "sigdev.h"
#include "distortion.h"
#include "compressor.h"
#include "phaser.h"

/**
 * @brief Ring modulator: multiplies the signal by a sine oscillator ("magic circle" recursion).
 *
 * The signal is multiplied by osc * level + (1 - level), so level crossfades between the dry signal
 * and full ring modulation. Processes the buffer in place. sizeof == 0x28.
 */
class CRingMod : public CSignalDevice {
public:
    /**
     * @brief Starts the oscillator at phase 0 with 1000 Hz and level 0.5.
     */
    CRingMod();

    /**
     * @brief Does nothing.
     */
    virtual ~CRingMod();

    /**
     * @brief Sets the oscillator frequency.
     *
     * Also puts the oscillator state back on the unit circle (keeping its phase) so the amplitude
     * does not drift.
     *
     * @param freq  Frequency in Hz.
     */
    void setFreq(float freq);

    /**
     * @brief Sets the wet amount.
     *
     * @param level  0..1.
     */
    void setLevel(float level);

    /**
     * @brief Recalculates the oscillator coefficient for the new sampling rate.
     */
    virtual void onChangeSamplingFreq();

    /**
     * @brief Recalculates m_coef = 2 * sin(pi * freq / fs).
     */
    void calcCoefs();

    /**
     * @brief Ring-modulates a mono buffer in place.
     *
     * @param size           Number of samples.
     * @param[in,out] pBuf   Sample buffer.
     * @param pProcessed     Silence flag; if non-zero nothing is done (the oscillator is not advanced).
     * @return size.
     */
    virtual int processSignal(int size, float* pBuf, int* pProcessed);

    /**
     * @brief Ring-modulates an interleaved stereo buffer in place (same gain on both channels).
     *
     * @param size           Number of stereo frames.
     * @param[in,out] pBuf   Interleaved stereo buffer.
     * @param pProcessed     Silence flag; if non-zero nothing is done (the oscillator is not advanced).
     * @return size.
     */
    virtual int processSignalW(int size, float* pBuf, int* pProcessed);

protected:
    float m_level;          // +0x14  wet amount
    float m_freq;           // +0x18
    float m_coef;           // +0x1c  2*sin(pi*f/fs)
    float m_x;              // +0x20
    float m_y;              // +0x24
};

/**
 * @brief Sample rate / bit depth reducer.
 *
 * Every (m_sample + 1)-th sample is quantised (scaled by 65536, converted to an integer and the low
 * m_bit + 1 bits cleared) and held; the held value is mixed with the dry signal by the level.
 * Processes the buffer in place. sizeof == 0x30.
 */
class CDecimator : public CSignalDevice {
public:
    /**
     * @brief Resets the sample counter.
     */
    CDecimator();

    /**
     * @brief Does nothing.
     */
    virtual ~CDecimator();

    /**
     * @brief Does nothing (the effect does not depend on the sampling rate).
     */
    virtual void onChangeSamplingFreq();

    /**
     * @brief Sets the wet amount.
     *
     * @param level  0..1.
     */
    void setLevel(float level);

    /**
     * @brief Sets the hold length: each captured sample is held for n + 1 samples.
     *
     * @param n  0..127.
     */
    void setSample(int n);

    /**
     * @brief Sets the bit reduction: the low n + 1 bits of the 16.16 fixed-point value are cleared.
     *
     * @param n  0..15.
     */
    void setBit(int n);

    /**
     * @brief Decimates a mono buffer in place.
     *
     * @param size           Number of samples.
     * @param[in,out] pBuf   Sample buffer.
     * @param pProcessed     Silence flag; if non-zero nothing is done.
     * @return size.
     */
    virtual int processSignal(int size, float* pBuf, int* pProcessed);

    /**
     * @brief Decimates an interleaved stereo buffer in place (both channels sampled together).
     *
     * @param size           Number of stereo frames.
     * @param[in,out] pBuf   Interleaved stereo buffer.
     * @param pProcessed     Silence flag; if non-zero nothing is done.
     * @return size.
     */
    virtual int processSignalW(int size, float* pBuf, int* pProcessed);

protected:
    float m_level;          // +0x14
    float m_hold[2];        // +0x18  held sample (L,R)
    unsigned int m_count;   // +0x20
    int m_sample;           // +0x24  hold length - 1
    int m_bit;              // +0x28  number of dropped bits - 1
};

// effect types (CEffector::setType)
#define EFX_DIST1       0
#define EFX_DIST2       1
#define EFX_DIST3       2
#define EFX_DECIMATOR   3
#define EFX_RINGMOD     4
#define EFX_COMPRESSOR  5
#define EFX_PHASER1     6
#define EFX_PHASER2     7
#define EFX_PHASER3     8
#define EFX_PHASER4     9

/**
 * @brief Effect section: owns one instance of each effect and runs the input through the one
 *        selected by setType().
 *
 * The two controls and the level are passed to every effect, each of which interprets them in its
 * own way. sizeof == 0x370.
 */
class CEffector : public CSignalDevice {
public:
    /**
     * @brief Enables the section and selects distortion 1 with all controls at 0.
     */
    CEffector();

    /**
     * @brief Does nothing.
     */
    virtual ~CEffector();

    /**
     * @brief Selects the active effect.
     *
     * The distortion types set the distortion curve (0..2); the phaser types set 1, 2, 4 or 6 stages.
     * Unknown values keep the previous effect.
     *
     * @param type  EFX_xxx.
     */
    void setType(int type);

    /**
     * @brief Sets control 1 on every effect.
     *
     * Distortion drive, compressor depth, ring modulator frequency (8000^(v/127) Hz), decimator hold
     * length, and phaser sweep range (min 100 * 10^((127-v)/127) Hz, max 1000 * 10^(v/127) Hz).
     *
     * @param v  0..127.
     */
    void setCont1(int v);

    /**
     * @brief Sets control 2 on every effect.
     *
     * Distortion speaker tone, compressor attack, decimator bit reduction (v / 8), and phaser rate
     * (0.02 * 1000^(v/127) Hz, 0.01 Hz for 0).
     *
     * @param v  0..127.
     */
    void setCont2(int v);

    /**
     * @brief Sets the level on every effect.
     *
     * Distortion and compressor output level, ring modulator and decimator wet amount (v / 127), and
     * phaser feedback ((v - 64) / 64 * 0.99).
     *
     * @param v  0..127.
     */
    void setLevel(int v);

    /**
     * @brief Passes the new sampling rate to every effect.
     */
    virtual void onChangeSamplingFreq();

    /**
     * @brief Enables or bypasses the section.
     *
     * @param b  Non-zero to enable.
     */
    void setEnable(int b);

    /**
     * @brief Gets the enable flag.
     *
     * @return Non-zero if the section is enabled.
     */
    int getEnable();

    /**
     * @brief Gets the selected effect type.
     *
     * @return The effect type (EFX_xxx).
     */
    int getType();

    /**
     * @brief Sets the unison stereo flag (stored only).
     *
     * @param b  Non-zero for unison stereo.
     */
    void setStereoMode(int b){ m_bStereo = b; }

    /**
     * @brief Pulls a mono block from the input device and runs it through the active effect if enabled.
     *
     * @param size                Number of samples requested.
     * @param[in,out] pBuf        Sample buffer.
     * @param[in,out] pProcessed  Silence flag, as set by the input device and the effect.
     * @return Number of samples produced by the input device.
     */
    virtual int processSignal(int size, float* pBuf, int* pProcessed);

    /**
     * @brief Pulls a stereo block from the input device and runs it through the active effect if enabled.
     *
     * @param size                Number of stereo frames requested.
     * @param[in,out] pBuf        Interleaved stereo buffer.
     * @param[in,out] pProcessed  Silence flag, as set by the input device and the effect.
     * @return Number of frames produced by the input device.
     */
    virtual int processSignalW(int size, float* pBuf, int* pProcessed);

protected:
    int m_bEnable;              // +0x14
    int m_type;                 // +0x18
    int m_cont1;                // +0x1c
    int m_cont2;                // +0x20
    int m_level;                // +0x24
    int m_bStereo;              // +0x28  unison stereo (set by CSynthesizer1)
    int m_reserved;             // +0x2c
    CSignalDevice* m_pEffect;   // +0x30  current effect
    CDistortion m_dist;         // +0x38
    CCompressor m_comp;         // +0x1a8
    CRingMod m_ringMod;         // +0x268
    CDecimator m_decimator;     // +0x290
    CPhaser m_phaser;           // +0x2c0
};
