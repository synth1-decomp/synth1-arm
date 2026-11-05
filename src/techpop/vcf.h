#pragma once

/**
 * @file vcf.h
 * @brief CVcf, the per-voice filter with its own envelope, and CBQF, a small biquad filter.
 *
 * Filter types: 12dB state variable (LPF/HPF/BPF), 24dB ladder lowpass and diode ladder lowpass.
 */

#include "envelope.h"

// filter types (m_type)
#define VCF_LPF12       0
#define VCF_LPF24       1
#define VCF_HPF12       2
#define VCF_BPF12       3
#define VCF_LPFDIODE        4

// cutoff resolution: cutoff values are 0..VCF_FREQ_MAX-1, an index into
// s1g->m_vcfFreqTable[]
#define VCF_FREQ_MAX        1024

/**
 * @brief Builds the cutoff table s1g->m_vcfFreqTable (15 Hz .. 16 kHz, exponential) once; later calls do nothing.
 */
void initVcf();

/**
 * @brief Biquad filter (RBJ cookbook)  sizeof == 0x30
 *
 * @note Not used by the synth.
 */
class CBQF {
public:
    /**
     * @brief Computes the biquad coefficients.
     *
     * The parameter names in this declaration differ from their use: the first parameter is the
     * filter type and the last one the sampling rate.
     *
     * @param fs    Filter type: 0 LPF, 1 HPF, 2 BPF (constant skirt gain), 3 BPF (constant peak gain),
     *              6 peaking EQ, 7 low shelf. Other values leave the coefficients undefined.
     * @param freq  Center/cutoff frequency in Hz.
     * @param q     Q factor.
     * @param gain  Gain in dB (peaking EQ and low shelf).
     * @param type  Sampling rate in Hz.
     */
    void setParam(int fs, float freq, float q, float gain, int type);

    /**
     * @brief Filters a buffer in place; the output is limited to at most 2.0 (positive side only).
     *
     * @param size          Number of samples.
     * @param[in,out] pBuf  Samples to filter.
     * @return size.
     */
    int process(int size, float* pBuf);

    float m_b[3];       // +0x00
    float m_a[3];       // +0x0c  (m_a[0] unused)
    float m_x[3];       // +0x18
    float m_y[3];       // +0x24
};

/**
 * @brief Filter with its own envelope.  sizeof == 0x1c8
 *
 * Takes the oscillator output of the voice as input. The envelope level, keyboard tracking,
 * unison detune offset and LFOs move the cutoff (an index 0..VCF_FREQ_MAX-1 into
 * s1g->m_vcfFreqTable); changes glide over 50 ms. After filtering, postProcessSignal() applies
 * a peak limiter and the optional distortion.
 */
class CVcf : public CEnvelopedObject {
public:
    /**
     * @brief Builds the cutoff table if needed and sets the defaults (24dB lowpass, cutoff 512,
     *        resonance 0.4, velocity sensitive).
     */
    CVcf();
    virtual ~CVcf(){}

    /**
     * @brief Resets the envelope, all filter states, the coefficient cache and the limiter.
     */
    void resetFilterBuf();

    /**
     * @brief Rescales the envelope, invalidates the coefficient cache and recomputes the limiter and
     *        coefficient smoothing constants.
     */
    virtual void onChangeSamplingFreq();

    /**
     * @brief Sets the resonance.
     *
     * @param reso  0..1 (above 0.9 the filter outputs are clipped to +-20).
     */
    void setResonance(float reso);

    /**
     * @brief Sets the cutoff; the current cutoff glides to the new value.
     *
     * @param freq  0..1, mapped to the cutoff index 0..1023.
     */
    void setFreq(float freq);

    /**
     * @brief Sets the envelope gain from the envelope amount.
     *
     * The amount is scaled by the velocity (when velocity sensitive) and limited so the envelope
     * keeps the cutoff (plus keyboard tracking and unison offsets) within 0..VCF_FREQ_MAX.
     */
    void calcSetAmount();

    /**
     * @brief Sets the filter type and invalidates the coefficient cache.
     *
     * @param type  VCF_LPF12, VCF_LPF24, VCF_HPF12, VCF_BPF12 or VCF_LPFDIODE.
     */
    void setType(int type);

    /**
     * @brief Sets the envelope amount; the current cutoff glides to the new value.
     *
     * @param amount    About -1..+1 (fraction of the cutoff range).
     */
    void setAmount(float amount);

    /**
     * @brief Sets the envelope sustain level and restarts the cutoff glide from the current cutoff.
     *
     * @param level   Sustain level, 0..1.
     * @see CEnvelopedObject::setSustain()
     */
    void setSustain(float level);

    /**
     * @brief Enables LFO @p no for the filter if it is switched on and its destination is LFO_DEST_FILTER.
     *
     * @param no        LFO index, 0 or 1.
     * @param bEnable   Nonzero if the LFO is switched on.
     * @param dest      LFO destination (LFO_DEST_*).
     */
    void changeLfoSetting(int no, int bEnable, int dest);

    /**
     * @brief Turns velocity scaling of the envelope amount on or off.
     *
     * @param bEnable   Nonzero: on; 0: off.
     */
    void setVelocitySensEnable(int bEnable);

    /**
     * @brief Sets the distortion applied after the filter.
     *
     * @param amount    0 = off; larger values saturate more (the drive is amount * 100).
     */
    void setDistortion(float amount);

    /**
     * @brief Stores the oscillator mix gain sum passed by CVco in m_gainScale (not otherwise used by
     *        the filter).
     *
     * @param scale   Unnormalized OSC1/OSC2/sub gain sum.
     */
    void setGainScale(float scale);

    /**
     * @brief Filters one block in place.
     *
     * The target cutoff is the cutoff + envelope level + keyboard tracking + unison offset, moved
     * by the LFOs towards the top (positive) or bottom (negative) of the range and clamped to
     * 0..VCF_FREQ_MAX-1. A cutoff change glides over 50 ms in small steps.
     *
     * @param level         Envelope level (cutoff index offset).
     * @param[in,out] pBuf  Samples to filter.
     * @param size          Number of samples.
     * @param lfo1          LFO 1 value (+-1024 = full range).
     * @param lfo2          LFO 2 value.
     * @return Always 0 (the buffer was written).
     */
    virtual int processEnvelopedSignal(int level, float* pBuf, int size, int lfo1, int lfo2);

    /**
     * @brief 12dB state variable filter, 2x oversampled; outputs the lowpass, highpass or bandpass
     *        output according to m_type.
     *
     * @param pIn       Input samples.
     * @param freq      Cutoff index 0..VCF_FREQ_MAX-1.
     * @param reso      Resonance 0..1.
     * @param[out] pOut Output samples (may be the same buffer as pIn).
     * @param size      Number of samples.
     */
    void lowpass2(float* pIn, int freq, float reso, float* pOut, unsigned int size);

    /**
     * @brief 24dB ladder lowpass (Moog type) with smoothed coefficients.
     *
     * @param pIn       Input samples.
     * @param freq      Cutoff index 0..VCF_FREQ_MAX-1.
     * @param reso      Resonance 0..1.
     * @param[out] pOut Output samples (may be the same buffer as pIn).
     * @param size      Number of samples.
     */
    void lowpass3(float* pIn, int freq, float reso, float* pOut, unsigned int size);

    /**
     * @brief Diode ladder lowpass (4 poles, zero-delay feedback, saturating input) with a one-pole
     *        highpass in the feedback path; 2x oversampled up to 48 kHz.
     *
     * @param pIn       Input samples.
     * @param freq      Cutoff index 0..VCF_FREQ_MAX-1 (the cutoff is 1.5x the table value, at most 0.45 fs).
     * @param reso      Resonance 0..1.
     * @param[out] pOut Output samples (may be the same buffer as pIn).
     * @param size      Number of samples.
     */
    void lowpassDiodeLadder(float* pIn, int freq, float reso, float* pOut, unsigned int size);

    /**
     * @brief Applies the peak limiter and, if set, the soft saturation distortion in place.
     *
     * @param[in,out] pBuf  Samples.
     * @param size          Number of samples.
     * @param pProcessed    Unused.
     */
    virtual void postProcessSignal(float* pBuf, int size, int* pProcessed);

    /**
     * @brief Peak limiter at 1.1: samples above the level push an envelope (5 ms release) that
     *        reduces the gain.
     *
     * Returns quickly when the limiter is idle and no sample exceeds the level.
     *
     * @param[in,out] pBuf  Samples.
     * @param size          Number of samples.
     */
    void processLimitter(float* pBuf, int size);

    /**
     * @brief Starts the envelope for a note: updates keyboard tracking, the velocity (curved to
     *        0..116; velocity 127 maps to 116) and the envelope amount, and jumps to the target cutoff.
     *
     * @param note      MIDI note number.
     * @param velocity  Note velocity, 0..127.
     */
    void noteOn(int note, int velocity);

    /**
     * @brief Computes the keyboard tracking cutoff offset (m_kbdTrackOfs) for a note.
     *
     * @param note  MIDI note number; note 48 has no offset.
     */
    void calcKbdTrack(int note);

    /**
     * @brief Starts the envelope release.
     */
    void noteOff();

    /**
     * @brief Sets the cutoff offset that follows this voice's unison detune.
     *
     * @param cent  Detune in cents, converted to cutoff index steps.
     */
    void setUnisonDetunedMod(float cent);

    /**
     * @brief Sets the keyboard tracking amount; the current cutoff glides to the new value.
     *
     * @param amount    0..1 (0 = off).
     */
    void setKbdTrack(float amount);

public:
    int m_freq;             // +0x8c  cutoff 0..1023
    float m_resonance;      // +0x90
    int m_curFreq;          // +0x94  current (interpolated) cutoff
    int m_interpPos;        // +0x98  cutoff interpolation position
    int m_startFreq;        // +0x9c  cutoff at the start of the interpolation
    int m_type;             // +0xa0
    float m_kbdTrack;       // +0xa4
    int m_kbdTrackOfs;      // +0xa8
    int m_note;             // +0xac
    float m_amount;         // +0xb0  envelope amount
    int m_unisonOfs;        // +0xb4
    int m_bVelocitySens;    // +0xb8
    int m_velocity;         // +0xbc
    float m_distortion;     // +0xc0
    float m_gainScale;      // +0xc4
    int m_lastFreq;         // +0xc8  coefficient cache key
    float m_lastReso;       // +0xcc
    float m_limitAttack;    // +0xd0
    float m_limitGain;      // +0xd4
    float m_limitEnv;       // +0xd8
    float m_limitRelease;   // +0xdc
    // 12dB state variable filter
    float m_low;            // +0xe0
    float m_high;           // +0xe4
    float m_band;           // +0xe8
    float m_f;              // +0xec
    float m_q;              // +0xf0
    float m_reso2;          // +0xf4
    // 24dB filter
    float m_y[4];               // +0xf8
    float m_x[4];               // +0x108
    int _reserved118;           // +0x118
    float m_k;                  // +0x11c
    float m_p;                  // +0x120
    float m_r;                  // +0x124
    float m_curK;               // +0x128
    float m_curP;               // +0x12c
    float m_curR;               // +0x130
    float m_coefSmooth;         // +0x134
    double _reserved138[3][4];  // +0x138  (only cleared)
    double _reserved198;        // +0x198  (only cleared)
    // diode ladder
    double m_z[5];          // +0x1a0
};
