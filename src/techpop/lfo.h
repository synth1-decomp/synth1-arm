#pragma once

/**
 * @file lfo.h
 * @brief CLFO: low frequency oscillator that reports piecewise-constant integer modulation values.
 */

class CSynthesizer1;

// LFO wave types (m_type)
#define LFO_SAW         0
#define LFO_TRIANGLE        1
#define LFO_SQUARE      2
#define LFO_SAMPLEHOLD      3
#define LFO_SMOOTHRANDOM    4
#define LFO_SINE        5

// LFO destinations (m_destination)
#define LFO_DEST_OSC2       1
#define LFO_DEST_OSC12      2
#define LFO_DEST_FILTER     3
#define LFO_DEST_AMP        4
#define LFO_DEST_PW     5
#define LFO_DEST_FM     6
#define LFO_DEST_PAN        7

/**
 * @brief Low frequency oscillator.
 *
 * It does not produce a signal itself: users ask for the current value and for how long it stays the
 * same (getSameValAndTime()) so they can process blocks with a constant value. Values are integers
 * between m_amountLo and m_amountHi, whose scale depends on the destination. The owner advances the
 * phase with incCurTime() once per block.
 */
// sizeof == 0x160 (CSynthesizer1 has CLFO m_lfo[2] with a 0x160 stride)
class CLFO {
public:
    /**
     * @brief Constructs a disabled saw LFO at 1 Hz, 44100 Hz sampling rate, zero amount.
     */
    CLFO();

    /**
     * @brief Destroys the LFO; nothing to release.
     */
    virtual ~CLFO(){}

    /**
     * @brief Sets the synthesizer used to read the tempo for tempo sync.
     *
     * @param pSynth  The synthesizer (not owned).
     */
    void setSynth1(CSynthesizer1* pSynth);

    /**
     * @brief Sets the sampling frequency and recomputes the cycle length.
     *
     * @param fs  Sampling frequency in Hz.
     */
    void setSamplingFreq(int fs);

    /**
     * @brief Sets the target frequency; the running frequency glides towards it (see processSmoothFreq()).
     *
     * Also sets the minimum block length m_minStep to 32 samples above 10 Hz, otherwise 64.
     *
     * @param freq  Frequency in Hz.
     */
    void setFreq(float freq);

    /**
     * @brief Sets the running frequency immediately, rescaling the cycle position to keep the phase.
     *
     * @param freq  Frequency in Hz.
     */
    void setFreqMain(float freq);

    /**
     * @brief Sets the enable flag (stored only; read by the owner).
     *
     * @param bEnable  Nonzero to enable.
     */
    void setEnable(int bEnable);

    /**
     * @brief Turns tempo sync on or off and recomputes the frequency.
     *
     * @param bSync  Nonzero to derive the frequency from the host tempo.
     */
    void setTempoSync(int bSync);

    /**
     * @brief Recomputes the target frequency from the speed dial.
     *
     * With tempo sync the dial selects a note length from a table (16 bars .. 1/32 triplet);
     * otherwise the frequency is 1500^(dial/127) / 12 Hz (about 0.08..125 Hz).
     */
    void recalcSpeed();

    /**
     * @brief Sets the key sync flag (stored only; the owner resets the LFO on note on).
     *
     * @param bSync  Nonzero to enable key sync.
     */
    void setKeySync(int bSync);

    /**
     * @brief Sets the modulation destination and recomputes the amount range.
     *
     * @param dest  LFO_DEST_xxx.
     */
    void setDestination(int dest);

    /**
     * @brief Computes m_amountLo / m_amountHi from the amount dial and the destination.
     *
     * Pitch destinations use an exponential dial curve and a bipolar range of +/-640; pulse width
     * +/-128; filter 0..1024; amp 2048..0 (inverted); FM 0..512; pan +/-64.
     */
    void recalcAmount();

    /**
     * @brief Sets the amount dial and recomputes the amount range.
     *
     * @param val  0..127.
     */
    void setAmountDial(int val);

    /**
     * @brief Sets the output range directly.
     *
     * @param lo  Value at the bottom of the wave.
     * @param hi  Value at the top of the wave (may be below lo to invert the wave).
     */
    void setAmount(int lo, int hi);

    /**
     * @brief Sets the speed dial and recomputes the frequency.
     *
     * @param val  0..127.
     */
    void setSpeedDial(int val);

    /**
     * @brief Sets the wave type; the random types pick a new random level.
     *
     * @param type  LFO_xxx wave type.
     */
    void setType(int type);

    /**
     * @brief Picks a new random level in the amount range (sample & hold / smooth random).
     *
     * @param prevLevel  Level to start from; becomes m_prevRandLevel (smooth random glides from it).
     */
    void makeRandomLevel(int prevLevel);

    /**
     * @brief Moves the running frequency towards the target by 1 Hz per millisecond of elapsed time.
     *
     * @param size  Elapsed time in samples.
     */
    void processSmoothFreq(int size);

    /**
     * @brief Restarts the cycle (key sync) and picks a new random level for the random types.
     */
    void reset();

    /**
     * @brief Returns the value at an offset from the current position, and how long it stays constant.
     *
     * @param[out] pTime  Number of samples the value stays the same (at least m_minStep), or -1 if
     *                    the output is constant (zero amount range).
     * @param offset      Offset from the current cycle position in samples.
     * @return The LFO value, between m_amountLo and m_amountHi.
     */
    int getSameValAndTime(int* pTime, int offset);

    /**
     * @brief Returns the value at a cycle position, and how long it stays constant.
     *
     * @param[out] pTime  Number of samples the value stays the same (at least m_minStep), or -1 if
     *                    the output is constant (zero amount range).
     * @param time        Position in the cycle in samples, 0..m_cycleTime-1.
     * @return The LFO value, between m_amountLo and m_amountHi.
     */
    int getSameValAndTimeMain(int* pTime, int time);

    /**
     * @brief Advances the cycle position; a new random level is picked at each cycle wrap.
     *
     * Also advances the frequency glide (processSmoothFreq()).
     *
     * @param size  Number of samples elapsed.
     */
    void incCurTime(int size);

    /**
     * @brief Gets the time left in the current cycle.
     *
     * @return Number of samples until the end of the cycle (m_cycleTime - m_curTime).
     */
    int getNextCycleRestTime();

    int m_bEnable;              // +0x08
    int m_type;                 // +0x0c
    int m_destination;          // +0x10
    int m_bTempoSync;           // +0x14
    int m_bKeySync;             // +0x18
    int m_speedDial;            // +0x1c  0..127
    int m_amountDial;           // +0x20  0..127
    CSynthesizer1* m_pSynth;    // +0x28
    int m_samplingFreq;         // +0x30
    int m_minStep;              // +0x34  shortest block length returned by getSameValAndTime
    float m_freqMain;           // +0x38  current (smoothed) frequency
    float m_freq;               // +0x3c  target frequency
    int m_amountLo;             // +0x40
    int m_amountHi;             // +0x44
    int m_curTime;              // +0x48  position in the cycle (samples)
    int m_cycleTime;            // +0x4c  cycle length (samples)
    int m_randLevel;            // +0x50
    int m_prevRandLevel;        // +0x54
    int m_resetCount;           // +0x58
    int m_reserved;             // +0x5c
    int m_work[64];             // +0x60  not referenced by lfo.cpp
};
