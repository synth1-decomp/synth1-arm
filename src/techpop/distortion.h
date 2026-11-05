#pragma once

/**
 * @file distortion.h
 * @brief Distortion effect: biquad filter cascade, static wave shaper and a three-stage tube amp
 *        simulator, combined in CDistortion.
 *
 * CDistortion is one of the effects selectable in the effect unit (efxs.h). It shapes the signal with
 * either the amp simulator (tube) or the wave shaper (soft clip / fold back) and then runs it through
 * a speaker simulation filter.
 */

#include "sigdev.h"

#define BIQUAD_MAX_STAGE    4

// biquad filter types (RBJ cookbook)
#define BQ_LPF          0
#define BQ_HPF          1
#define BQ_BPF          2   // constant skirt gain
#define BQ_BPF2         3   // constant 0 dB peak gain
#define BQ_NOTCH        4   // not implemented
#define BQ_APF          5   // not implemented
#define BQ_PEAKING      6
#define BQ_LOWSHELF     7

/**
 * @brief Cascade of up to BIQUAD_MAX_STAGE (4) biquad stages, mono or stereo.
 *
 * Coefficients follow the RBJ audio EQ cookbook and are stored normalized by a0. Each stage keeps
 * separate history for the left (index 0) and right (index 1) channel.
 */
// sizeof == 0x124
class CBiquadFilter {
public:
    /**
     * @brief Clears the filter history (x and y delay elements) of all stages and both channels.
     */
    void clearOldSig();

    /**
     * @brief Sets the number of active stages; 0 makes process() a pass-through.
     *
     * @param n  Number of active stages, 0..BIQUAD_MAX_STAGE.
     */
    void setPoles(int n);

    /**
     * @brief Computes the coefficients of one stage.
     *
     * Uses the RBJ cookbook formulas. BQ_NOTCH and BQ_APF are not implemented: for those types the
     * coefficients are left uninitialized.
     *
     * @param stage  Stage index, 0..BIQUAD_MAX_STAGE-1.
     * @param type   Filter type, BQ_xxx.
     * @param freq   Cutoff / center frequency in Hz.
     * @param q      Quality factor.
     * @param gain   Gain in dB (BQ_PEAKING and BQ_LOWSHELF only).
     * @param fs     Sampling frequency in Hz.
     */
    void setParam(int stage, int type, float freq, float q, float gain, int fs);

    /**
     * @brief Filters a mono buffer in place through all active stages (left-channel history).
     *
     * @param size           Number of samples.
     * @param[in,out] pBuf   Samples to filter.
     * @return size.
     */
    int process(int size, float* pBuf);

    /**
     * @brief Filters an interleaved stereo buffer in place through all active stages.
     *
     * With no active stages the samples are left unchanged.
     *
     * @param size           Number of stereo sample pairs.
     * @param[in,out] pBuf   Samples to filter.
     * @return size.
     */
    int processLR(int size, tagStereoData* pBuf);

    int m_nStage;                           // +0x00
    float m_b0[BIQUAD_MAX_STAGE];           // +0x04
    float m_b1[BIQUAD_MAX_STAGE];           // +0x14
    float m_b2[BIQUAD_MAX_STAGE];           // +0x24
    float m_a0[BIQUAD_MAX_STAGE];           // +0x34  (unused, coefs are normalized)
    float m_a1[BIQUAD_MAX_STAGE];           // +0x44
    float m_a2[BIQUAD_MAX_STAGE];           // +0x54
    float m_x0[2][BIQUAD_MAX_STAGE];        // +0x64
    float m_x1[2][BIQUAD_MAX_STAGE];        // +0x84
    float m_x2[2][BIQUAD_MAX_STAGE];        // +0xa4
    float m_y0[2][BIQUAD_MAX_STAGE];        // +0xc4
    float m_y1[2][BIQUAD_MAX_STAGE];        // +0xe4
    float m_y2[2][BIQUAD_MAX_STAGE];        // +0x104
};

/**
 * @brief Static (memoryless) wave shaper: soft clip or fold back, with output gain compensation.
 *
 * Type 0 (tube) only selects the gain compensation curve; the tube shaping itself is done by
 * CGtAmpSim1, which uses the drive and gain stored here.
 */
// sizeof == 0x10
class CWaveShaper {
public:
    /**
     * @brief Constructs the shaper with a fold back threshold of 0.7.
     */
    CWaveShaper(){ m_threshold = 0.7f; }

    /**
     * @brief Sets the input drive and recomputes the output gain compensation for the current type.
     *
     * @param drive  Linear input gain.
     */
    void setDrive(float drive);

    /**
     * @brief Sets the shaping type and recomputes the gain compensation.
     *
     * @param type  0: tube, 1: soft clip, 2: fold back.
     */
    void setType(int type);

    /**
     * @brief Shapes a mono buffer in place.
     *
     * Type 1 applies the soft clipper; any other type applies fold back.
     *
     * @param[in,out] pBuf  Samples to process.
     * @param size          Number of samples.
     * @param level         Output level, multiplied with the gain compensation.
     */
    void process(float* pBuf, int size, float level);

    /**
     * @brief Shapes an interleaved stereo buffer in place (same rules as process()).
     *
     * @param[in,out] pBuf  Samples to process.
     * @param size          Number of stereo sample pairs.
     * @param level         Output level, multiplied with the gain compensation.
     */
    void processLR(tagStereoData* pBuf, int size, float level);

    int m_type;             // +0x00  0:tube 1:soft clip 2:fold back
    float m_drive;          // +0x04
    float m_gain;           // +0x08  output gain compensation
    float m_threshold;      // +0x0c  fold back level
};

/**
 * @brief One-pole low pass filter used inside CGtAmpSim1 (m_coef must be set by the owner).
 */
class CGtLPF {
public:
    /**
     * @brief Constructs the filter with a cleared state.
     */
    CGtLPF(){ m_z = 0.0f; }

    /**
     * @brief Filters one sample: z += (in - z) * m_coef.
     *
     * @param in  Input sample.
     * @return The filtered sample (the new state).
     */
    float process(float in){ m_z += (in - m_z) * m_coef; return m_z; }
    float m_coef;
    float m_z;
};

/**
 * @brief One-pole high pass filter used inside CGtAmpSim1 (m_coef must be set by the owner).
 */
class CGtHPF {
public:
    /**
     * @brief Constructs the filter with a cleared state.
     */
    CGtHPF(){ m_y = 0.0f; m_x = 0.0f; }

    /**
     * @brief Filters one sample: y = (in - x_prev + y_prev) * m_coef.
     *
     * @param in  Input sample.
     * @return The filtered sample.
     */
    float process(float in){ m_y = (in - m_x + m_y) * m_coef; m_x = in; return m_y; }
    float m_coef;
    float m_y;
    float m_x;
};

/**
 * @brief Guitar amp simulator: three cascaded tube stages, mono or stereo.
 *
 * Each stage adds a low passed bias feedback to its input, applies the tube transfer curve and then
 * high/low pass filters the result. Drive and output gain compensation come from the embedded
 * CWaveShaper (type 0).
 */
// sizeof == 200
class CGtAmpSim1 {
public:
    /**
     * @brief Constructs the simulator at 44100 Hz with drive 1.0 and lets the bias filters settle.
     */
    CGtAmpSim1();

    /**
     * @brief Sets the sampling frequency and recomputes the filter coefficients.
     *
     * @param fs  Sampling frequency in Hz.
     */
    void setSamplingFreq(int fs);

    /**
     * @brief Sets the input drive (also passed to the embedded wave shaper for gain compensation).
     *
     * @param drive  Linear input gain.
     */
    void setDrive(float drive);

    /**
     * @brief Computes the coefficients of the bias, high pass and low pass filters for m_samplingFreq.
     */
    void calcCoef();

    /**
     * @brief Runs one sample of one channel through the three tube stages.
     *
     * @param in     Input sample.
     * @param ch     Channel, 0 (left) or 1 (right).
     * @param drive  Input gain.
     * @param level  Output gain.
     * @return The output sample.
     */
    float processSingle(float in, int ch, float drive, float level);

    /**
     * @brief Processes a mono buffer in place (left-channel state).
     *
     * @param[in,out] pBuf  Samples to process.
     * @param size          Number of samples.
     * @param level         Output level, multiplied with the shaper's gain compensation.
     */
    void process(float* pBuf, int size, float level);

    /**
     * @brief Processes an interleaved stereo buffer in place.
     *
     * @param[in,out] pBuf  Samples to process.
     * @param size          Number of stereo sample pairs.
     * @param level         Output level, multiplied with the shaper's gain compensation.
     */
    void processLR(tagStereoData* pBuf, int size, float level);

    /**
     * @brief Runs one stereo sample pair through the three tube stages.
     *
     * @param[in,out] pBuf  The sample pair, replaced by the output.
     * @param drive         Input gain.
     * @param level         Output gain.
     */
    void processSingleLR(tagStereoData* pBuf, float drive, float level);

    int m_samplingFreq;     // +0x00
    float m_drive;          // +0x04
    CWaveShaper m_shaper;   // +0x08
    CGtLPF m_bias1[2];      // +0x18
    CGtHPF m_hpf1[2];       // +0x28
    CGtLPF m_lpf1[2];       // +0x40
    CGtLPF m_bias2[2];      // +0x50
    CGtHPF m_hpf2[2];       // +0x60
    CGtLPF m_lpf2[2];       // +0x78
    CGtLPF m_bias3[2];      // +0x88
    CGtHPF m_hpf3[2];       // +0x98
    float m_out1[2];        // +0xb0
    float m_out2[2];        // +0xb8
    float m_out3[2];        // +0xc0
};

/**
 * @brief Distortion effect device: amp simulator or wave shaper followed by a speaker simulation.
 *
 * Types: 0 tube amp (CGtAmpSim1), 1 soft clip, 2 fold back (CWaveShaper). The speaker simulation is a
 * two-stage biquad (low pass with a tone-dependent cutoff and a fixed 100 Hz high pass). The device
 * processes the buffer it is given in place; it does not pull from an input device.
 *
 * @note Owns the CGtAmpSim1 instance (allocated with new, deleted in the destructor).
 */
// sizeof == 0x170
class CDistortion : public CSignalDevice {
public:
    /**
     * @brief Constructs the effect with type 0, level 1.0, speaker tone 64 and allocates the amp simulator.
     *
     * @throws std::bad_alloc  If the amp simulator cannot be allocated.
     */
    CDistortion();

    /**
     * @brief Deletes the amp simulator.
     */
    virtual ~CDistortion();

    /**
     * @brief Updates the silence timeout (25 ms), the speaker filter and the amp simulator for the new rate.
     */
    virtual void onChangeSamplingFreq();

    /**
     * @brief Sets the speaker tone and recomputes the speaker filter.
     *
     * @param v  0..127; the low pass cutoff goes exponentially from 400 Hz to 16 kHz.
     */
    void setSpeakerTone(int v);

    /**
     * @brief Recomputes the speaker filter: low pass at 400 * 40^(tone/127) Hz (Q 3), high pass at 100 Hz (Q 2.3).
     */
    void calcCoefsForSpeakerTone();

    /**
     * @brief Sets the drive of the amp simulator and the wave shaper.
     *
     * @param v  0..127, mapped to -10..+60 dB of input gain.
     */
    void setDrive(int v);

    /**
     * @brief Sets the output level.
     *
     * @param v  0..127, mapped linearly to (v + 2) / 129.
     */
    void setLevel(int v);

    /**
     * @brief Selects the distortion type.
     *
     * @param type  Taken modulo 3: 0 tube amp, 1 soft clip, 2 fold back.
     */
    void setType(int type);

    /**
     * @brief Processes a mono buffer in place (calls processSignalMain() with bStereo 0).
     *
     * @param size                Number of samples.
     * @param[in,out] pBuf        Buffer processed in place.
     * @param[in,out] pProcessed  Silence flag of pBuf; cleared when output is written.
     * @return size.
     * @see processSignalMain()
     */
    virtual int processSignal(int size, float* pBuf, int* pProcessed);

    /**
     * @brief Processes an interleaved stereo buffer in place (calls processSignalMain() with bStereo 1).
     *
     * @param size                Number of stereo sample pairs.
     * @param[in,out] pBuf        Interleaved stereo buffer processed in place.
     * @param[in,out] pProcessed  Silence flag of pBuf; cleared when output is written.
     * @return size.
     * @see processSignalMain()
     */
    virtual int processSignalW(int size, float* pBuf, int* pProcessed);

    /**
     * @brief Common body of processSignal() and processSignalW().
     *
     * When the input is flagged as silent, the buffer is zeroed and processed anyway so that the
     * filter tails can decay; once the input has been silent for more than 25 ms the call returns
     * without touching the buffer.
     *
     * @param size                Number of samples (mono) or sample pairs (stereo).
     * @param[in,out] pBuf        Buffer processed in place.
     * @param[in,out] pProcessed  Nonzero on entry if pBuf holds no signal; cleared when output is written.
     * @param bStereo             Nonzero for interleaved stereo data.
     * @return size.
     */
    int processSignalMain(int size, float* pBuf, int* pProcessed, int bStereo);

    /**
     * @brief Applies the shaper (or amp simulator) and the speaker filter to a mono buffer.
     *
     * @param size           Number of samples.
     * @param[in,out] pBuf   Samples to process.
     */
    void processMono(int size, float* pBuf);

    /**
     * @brief Applies the shaper (or amp simulator) and the speaker filter to a stereo buffer.
     *
     * @param size           Number of stereo sample pairs.
     * @param[in,out] pBuf   Samples to process.
     */
    void processStereo(int size, tagStereoData* pBuf);

protected:
    float m_drive;              // +0x14
    int m_type;                 // +0x18
    float m_threshold;          // +0x1c
    int m_reserved;             // +0x20
    float m_level;              // +0x24
    int m_silentCount;          // +0x28
    int m_silentLimit;          // +0x2c
    CBiquadFilter m_speaker;    // +0x30  speaker simulation (LPF + HPF)
    int m_speakerTone;          // +0x154
    CGtAmpSim1* m_pAmp;         // +0x158
    CWaveShaper m_shaper;       // +0x160
};
