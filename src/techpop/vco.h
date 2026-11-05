#pragma once

/**
 * @file vco.h
 * @brief CVco: the two oscillators of a voice (OSC1 + OSC2 + sub oscillator).
 *
 * Band limited 2048 point wavetables, 16.16 fixed point phase.
 * The pitch envelope of OSC2 (or FM amount) is the CEnvelopedObject part.
 * Also declares the parameter block and the specialized render loops of vcoMain.cpp.
 */

#include "envelope.h"
#include "lfo.h"

class CVcf;


// oscillator wave shapes (m_shape, m_shape2)
#define VCO_SINE        0
#define VCO_SAW         1
#define VCO_PULSE       2
#define VCO_TRIANGLE        3
#define VCO_NOISE       4
#define VCO_SQUARE      5

#define VCO_TABLE_SIZE      2048
#define VCO_TABLE_MASK      (VCO_TABLE_SIZE - 1)
#define VCO_PHASE_MASK      0x7ffffff   // 2048 << 16

/**
 * @brief Per-block parameters of the oscillator inner loop (vcoMain.cpp).  sizeof == 0xd8
 *
 * Filled by CVco::createWaveMain() and passed to one of the calcXXXXXX() render variants.
 * The pointer members (pPhase1, pPhase2, pSSPhase, pFmAmount, pFmSmoothCount, pNoise) point
 * back into the CVco so the loop can store its final state.
 */
typedef struct tagCalcSigParams {
    int size;                   // +0x00  samples to render
    float* pBuf;                // +0x08  output
    float* pTable1;             // +0x10  OSC1 wavetable
    unsigned int dPhase1;       // +0x18  OSC1 phase increment (16.16)
    unsigned int phase1;        // +0x1c
    float gain1;                // +0x20
    int pwOffset;               // +0x24  pulse width as a table index offset
    int fmAmount;               // +0x28  current FM depth
    int fmDelta;                // +0x2c  fmTarget - fmStart
    int fmTarget;               // +0x30
    int fmSmoothLen;            // +0x34
    int fmSmoothCount;          // +0x38
    int fmStart;                // +0x3c
    int* pFmAmount;             // +0x40  written back at the end
    int* pFmSmoothCount;        // +0x48
    float* pTable2;             // +0x50  OSC2 wavetable
    unsigned int dPhase2;       // +0x58
    unsigned int phase2;        // +0x5c
    float gain2;                // +0x60
    unsigned int* pNoise;       // +0x68  M-sequence state
    float* pSubTable;           // +0x70
    float subGain;              // +0x78
    int subOct;                 // +0x7c  0: same octave, 1: -1 oct
    unsigned int ssPhase[8];    // +0x80  super saw (multi detune) phases
    unsigned int ssDPhase[8];   // +0xa0
    unsigned int* pPhase1;      // +0xc0  written back at the end
    unsigned int* pPhase2;      // +0xc8
    unsigned int* pSSPhase;     // +0xd0
} CalcSigParams;

/**
 * @brief Declares the six render variants for one sync/FM/ring/detune combination (vcoMain.cpp).
 *
 * Name letters: S: osc sync, F: FM, R: ring mod, D: multi detune ('_' = off),
 * then OSC1 P(ulse)/S(imple table) and OSC2 P(ulse)/N(oise)/S(imple table).
 * Each variant renders p->size samples into p->pBuf (overwriting it) and writes the
 * final phases (and FM state) back through the pointers in @p p.
 */
#define DECLARE_CALC_FUNCS(S,F,R,D) \
    void calc##S##F##R##D##PP(tagCalcSigParams* p); \
    void calc##S##F##R##D##PN(tagCalcSigParams* p); \
    void calc##S##F##R##D##PS(tagCalcSigParams* p); \
    void calc##S##F##R##D##SP(tagCalcSigParams* p); \
    void calc##S##F##R##D##SN(tagCalcSigParams* p); \
    void calc##S##F##R##D##SS(tagCalcSigParams* p);
DECLARE_CALC_FUNCS(_,_,_,_)
DECLARE_CALC_FUNCS(_,_,_,D)
DECLARE_CALC_FUNCS(_,F,_,_)
DECLARE_CALC_FUNCS(_,F,_,D)
DECLARE_CALC_FUNCS(_,_,R,_)
DECLARE_CALC_FUNCS(_,_,R,D)
DECLARE_CALC_FUNCS(S,_,_,_)
DECLARE_CALC_FUNCS(S,_,_,D)
DECLARE_CALC_FUNCS(S,F,_,_)
DECLARE_CALC_FUNCS(S,F,_,D)
DECLARE_CALC_FUNCS(S,_,R,_)
DECLARE_CALC_FUNCS(S,_,R,D)

/**
 * @brief Raised cosine 1 -> 0 over 256 steps (index 0..255); used to smooth FM amount changes.
 */
extern double g_smoothCosTable[];

// ---------------------------------------------------------------------------
/**
 * @brief The oscillator section of one voice: OSC1, OSC2, sub oscillator, super saw detune,
 *        sync, ring modulation, FM and portamento.
 *
 * The CEnvelopedObject envelope is the oscillator modulation envelope (OSC2 pitch, FM amount
 * or pulse width, see setOscModEnvDest()). CVco is a generator (no input device); its output
 * feeds the voice's CVcf. Pitch modulation values are in 1/128 octave units.
 * @note setVCF() must be called before init(), which uses the filter pointer.
 */
class CVco : public CEnvelopedObject {
public:
    /**
     * @brief Builds the shared wavetables and pitch tables on first construction; members are set by init().
     */
    CVco();

    /**
     * @brief Destroys the oscillator (the shared tables are freed at program exit).
     */
    virtual ~CVco();

    /**
     * @brief Resets all oscillator settings to their defaults (OSC1 saw, OSC2 noise, mix 0.5, no FM,
     *        no portamento, modulation envelope max 640 with zero gain and times).
     *
     * Also recomputes the mix gains (calling the filter's setGainScale()) and the sampling rate
     * dependent values.
     */
    void init();

    /**
     * @brief Recomputes the OSC1/OSC2 mix gains from m_mix and the output gain normalization.
     *
     * m_gainScale = 1 / ((sub gain + 1) * OSC1 mix + OSC2 mix); the unnormalized sum is passed to
     * the filter with CVcf::setGainScale().
     */
    void calcCoeff();

    /**
     * @brief Rescales the envelope and portamento lengths and refreshes the rate dependent values.
     *
     * Updates m_invFs, m_fsPerMs and the FM smoothing time (100 ms) and recomputes the mix gains
     * (calcCoeff()).
     */
    virtual void onChangeSamplingFreq();

    /**
     * @brief Sets the portamento time.
     *
     * A glide in progress is rescaled proportionally to the new length.
     *
     * @param sec   Glide time in seconds (0 = off).
     */
    void setPortamentTime(float sec);

    /**
     * @brief Sets the FM depth dial; the new depth is computed and smoothed on the next block.
     *
     * @param val   FM amount, 0..127.
     */
    void setFMAmount(int val);

    /**
     * @brief Sets the per-voice detune (used for unison) and recomputes both oscillator frequencies.
     *
     * @param cent  Detune in cents.
     */
    void setDetune(float cent);

    /**
     * @brief Recomputes m_freq1 (Hz) from the note, key shift, common tune and detune.
     *
     * Also refreshes the super saw frequencies when the multi detune is on.
     * @note The note + key shift is clamped to 0..128; 128 reads one entry past the end of the
     *       128-entry note frequency table.
     */
    void calcOsc1Freq();

    /**
     * @brief Recomputes m_freq2 (Hz).
     *
     * With key tracking: m_freq1 * 2^(m_osc2Pitch + OSC2 tune / 1200). Without: the fixed base
     * frequency (A4 / 2) shifted by the OSC2 pitch and OSC2 tune + common tune + detune.
     */
    void calcOsc2Freq();

    /**
     * @brief Computes the 8 super saw frequencies: m_freq1 detuned by +-1, 3, 5 and 7 times m_ssDetune cents.
     */
    void calcSuperSawFreq();

    /**
     * @brief Sets the OSC1 multi detune (super saw) amount.
     *
     * A value above 0 turns the 8 extra detuned OSC1 voices on; turning it on or off recomputes the
     * gain normalization.
     *
     * @param cent  Detune step in cents (0..20 from the panel; 0 = off).
     */
    void setSSDetune(float cent);

    /**
     * @brief Recomputes the output gain normalization (same computation as the gain part of calcCoeff()).
     *
     * Sets m_gainScale and passes the unnormalized gain sum to the filter (CVcf::setGainScale()).
     */
    void calcMaxGain();

    /**
     * @brief Applies a pitch modulation to a frequency.
     *
     * @param freq  Base frequency in Hz.
     * @param mod   Pitch offset in 1/128 octave units, clamped to +-1280 (+-10 octaves).
     * @return The modulated frequency in Hz, limited to 22000 Hz.
     */
    float calcFreqMod(float freq, int mod);

    /**
     * @brief Sets the OSC1 waveform.
     *
     * @param shape VCO_SINE, VCO_SAW, VCO_PULSE, VCO_TRIANGLE or VCO_SQUARE (OSC1 has no noise
     *              table; VCO_NOISE is not supported here).
     */
    void setShape(int shape);

    /**
     * @brief Sets the sub oscillator level and recomputes the gain normalization.
     *
     * @param gain  Level relative to OSC1 (0..4 from the panel).
     */
    void setSubOscGain(float gain);

    /**
     * @brief Sets the sub oscillator waveform.
     *
     * @param wf    0: sine, 1: triangle, 2: saw, 3: square.
     */
    void setSubWaveForm(int wf);

    /**
     * @brief Sets the sub oscillator octave.
     *
     * @param oct   0: same octave as OSC1, 1: one octave down.
     */
    void setSubOct(int oct);

    /**
     * @brief Sets the filter that receives the gain normalization (CVcf::setGainScale()).
     *
     * @param pVcf  The voice's filter; not owned.
     */
    void setVCF(CVcf* pVcf);

    /**
     * @brief Sets the OSC2 waveform.
     *
     * @param shape VCO_SINE, VCO_SAW, VCO_PULSE, VCO_TRIANGLE, VCO_NOISE or VCO_SQUARE.
     */
    void setShape2(int shape);

    /**
     * @brief Sets the OSC2 pitch offset.
     *
     * While the modulation envelope is off the value is taken immediately (the frequency is
     * recomputed at the next note on); otherwise the pitch glides to it over 50 ms. In both cases
     * the next noteOn() jumps to the new value.
     *
     * @param pitch Offset in octaves (-5..+5 from the panel).
     */
    void setOsc2Pitch(float pitch);

    /**
     * @brief Sets the OSC2 fine tune and recomputes its frequency.
     *
     * @param cent  Fine tune in cents.
     */
    void setOsc2Tune(int cent);

    /**
     * @brief Turns OSC2 key tracking on or off and recomputes its frequency.
     *
     * @param bTrack    Nonzero: OSC2 follows the note (and pitch bend); 0: fixed base frequency.
     */
    void setOsc2KbdTrack(int bTrack);

    /**
     * @brief Sets the OSC1/OSC2 balance and recomputes the mix gains.
     *
     * @param mix   0..1 (0: OSC1 only, 1: OSC2 only).
     */
    void setMix(float mix);

    /**
     * @brief Sets the tune shared by both oscillators; frequencies are recomputed only if it changed.
     *
     * @param cent  Tune in cents.
     */
    void setCommonTune(int cent);

    /**
     * @brief Enables an LFO for this oscillator if its destination is an oscillator parameter.
     *
     * LFO_DEST_OSC2, LFO_DEST_OSC12, LFO_DEST_PW and LFO_DEST_FM enable the LFO input; any other
     * destination (or bEnable == 0) disables it and stores destination 0.
     *
     * @param no        LFO index, 0 or 1.
     * @param bEnable   Nonzero if the LFO is switched on.
     * @param dest      LFO destination (LFO_DEST_*).
     */
    void changeLfoSetting(int no, int bEnable, int dest);

    /**
     * @brief Turns oscillator sync on or off (OSC2 restarts when OSC1 wraps).
     *
     * @param bSync   Nonzero: sync on; 0: off.
     */
    void setSync(int bSync);

    /**
     * @brief Turns ring modulation on or off (OSC2 multiplied by OSC1).
     *
     * @param bRing   Nonzero: ring modulation on; 0: off.
     */
    void setRing(int bRing);

    /**
     * @brief Sets the base pulse width used by VCO_PULSE oscillators.
     *
     * @param pw    0..1 from the panel; scaled to a table offset of up to half a cycle.
     */
    void setPW(float pw);

    /**
     * @brief Sets the key shift and recomputes both oscillator frequencies.
     *
     * @param shift Transpose in semitones.
     */
    void setKeyShift(int shift);

    /**
     * @brief Sets the modulation envelope depth.
     *
     * @param val   0..127; 64 = 0, below 64 negative down to -1, above 64 positive up to +1.
     */
    void setOsc2PitchAmount(int val);

    /**
     * @brief Sets the modulation envelope attack time.
     *
     * @param sec   Attack time in seconds.
     */
    void setOsc2PitchAttack(float sec);

    /**
     * @brief Sets the modulation envelope decay time.
     *
     * @param sec   Decay time in seconds.
     */
    void setOsc2PitchDecay(float sec);

    /**
     * @brief Turns the modulation envelope on or off.
     *
     * @param bEnable   Nonzero: on; 0: off.
     */
    void setOsc2PitchEnvEnable(int bEnable);

    /**
     * @brief Selects what the modulation envelope controls and sets its range and decay curve.
     *
     * @param dest  0: OSC2 pitch (max 640 = 5 octaves, exponential decay),
     *              1: FM amount (max 512, linear decay), 2: pulse width (max 128, exponential decay).
     */
    void setOscModEnvDest(int dest);

    /**
     * @brief Resets the phase settings to free running and requests new random phases.
     *
     * @param no    Voice index (unused).
     */
    void setVoiceNo(int no);

    /**
     * @brief Same as setVoiceNo(): free running phase, new random phases at the next note on.
     *
     * @param no    Voice index (unused).
     */
    void initPhaseParam(int no);

    /**
     * @brief Stores this voice's unison position for the phase offsets applied by startPhase().
     *
     * @param num       Number of unison voices.
     * @param no        Index of this voice within the unison group.
     * @param spread    Phase spread between the unison voices, 0..1.
     */
    void setUnisonInfo(int num, int no, float spread);

    /**
     * @brief Sets the oscillator phase mode from the phase knob.
     *
     * @param val   0..127; 0 = free running (random phases), 1..127 = restart at note on with an
     *              OSC2 phase offset of (val - 1) / 126 of half a cycle.
     */
    void setPhaseShift(int val);

    /**
     * @brief Sets the phase mode directly.
     *
     * @param mode  0: free running (new random phases are requested), 1: restart at note on.
     */
    void setPhaseMode(int mode);

    /**
     * @brief Requests new random phases at the next note on (free running mode).
     */
    void initFreePhase();

    /**
     * @brief Reads pseudo random phase coefficients from the shared random table.
     *
     * @param amount    Scale applied to the table values (0..1 each).
     * @param start     First index into the random table (wraps at RND_TABLE_SIZE).
     * @param num       Number of coefficients.
     * @param[out] pCoef    Receives num values.
     */
    void calcMultiDetunePhaseCoef(float amount, int start, int num, float* pCoef);

    /**
     * @brief Sets the oscillator phases at note on.
     *
     * Restart mode: OSC1 starts at 0, OSC2 at the phase shift, the super saw voices at fixed
     * pseudo random offsets; unison voices add their own offsets scaled by the unison spread.
     * Free running mode: random phases only when requested (m_bInitPhase), otherwise the phases
     * continue. Clears m_bInitPhase.
     */
    void startPhase();

    /**
     * @brief Sets the note and recomputes both oscillator frequencies.
     *
     * @param nn            MIDI note number.
     * @param bPortament    Nonzero: glide from the previous pitch (including a glide in progress);
     *                      0: cancel any glide.
     */
    void setNote(int nn, int bPortament);

    /**
     * @brief Starts the modulation envelope, applies a pending OSC2 pitch change, restarts FM smoothing
     *        and sets the phases (startPhase()).
     */
    void noteOn();

    /**
     * @brief Makes the next FM amount change jump to its target without smoothing.
     */
    void resetFMSmooth();

    /**
     * @brief Renders the oscillator output for one block.
     *
     * Combines pitch bend (m_pitchEnvLevel), LFOs and the modulation envelope into OSC1 pitch,
     * OSC2 pitch, pulse width and FM modulations, applies the portamento curve in steps and
     * renders through createWaveMain().
     *
     * @param level         Modulation envelope level.
     * @param[out] pBuf     Receives size samples (overwritten).
     * @param size          Number of samples.
     * @param lfo1          LFO 1 value.
     * @param lfo2          LFO 2 value.
     * @return Always 0 (the buffer was written).
     */
    virtual int processEnvelopedSignal(int level, float* pBuf, int size, int lfo1, int lfo2);

    /**
     * @brief Renders samples with constant modulation values through the matching calcXXXXXX() loop.
     *
     * The parameter names in this declaration differ from their use; by position they are the
     * OSC1 pitch mod, OSC2 pitch mod, pulse width mod and FM mod.
     *
     * @param fmMod     OSC1 pitch modulation (1/128 octave).
     * @param pitchMod  OSC2 pitch modulation (1/128 octave).
     * @param pwMod     Pulse width modulation (+-128 spans the full range).
     * @param fmEnv     FM modulation (+-512 = full).
     * @param[out] pBuf Receives the samples (overwritten).
     * @param size      Maximum number of samples.
     * @return Samples rendered; less than size while the FM amount is being smoothed.
     */
    int createWaveMain(int fmMod, int pitchMod, int pwMod, int fmEnv, float* pBuf, int size);

    /**
     * @brief Rebuilds the note frequency table (s1g->m_freqTable) if the master tune changed.
     *
     * @param masterTune    Master tune in cents (A4 = 440 Hz * 2^(masterTune / 1200)).
     */
    static void makeFreqTable(int masterTune);

public:
    int m_pitchEnvLevel;        // +0x8c
    float m_freq1;              // +0x90  OSC1 frequency
    unsigned int m_phase1;      // +0x94
    int m_shape;                // +0x98  OSC1 wave
    int m_fmAmountVal;          // +0x9c  FM dial 0..127
    int m_fmAmount;             // +0xa0  current FM depth
    int m_fmTarget;             // +0xa4
    int m_fmStart;              // +0xa8
    int m_fmSmoothCount;        // +0xac
    int m_fmPrevTarget;         // +0xb0
    int m_bFMReset;             // +0xb4
    int m_fmSmoothLen;          // +0xb8
    int m_fmTargetTime;         // +0xbc
    float m_ssFreq[8];          // +0xc0  super saw voices
    unsigned int m_ssPhase[8];  // +0xe0
    float m_ssDetune;           // +0x100
    float m_freq2;              // +0x104 OSC2 frequency
    unsigned int m_phase2;      // +0x108
    int m_shape2;               // +0x10c OSC2 wave
    float m_osc2Pitch;          // +0x110 current (octaves)
    int m_bSync;                // +0x114
    float m_osc2PitchTarget;    // +0x118
    float m_osc2PitchStart;     // +0x11c
    int m_osc2PitchPos;         // +0x120
    int m_bOsc2PitchChanged;    // +0x124
    float m_osc2Tune;           // +0x128 cent
    int m_bOsc2KbdTrack;        // +0x12c
    unsigned int m_noise;       // +0x130 M-sequence state
    float m_osc1Mix;            // +0x134 1 - mix
    float m_osc2Mix;            // +0x138 mix
    float m_subGain;            // +0x13c
    int m_subWaveForm;          // +0x140
    int m_subOct;               // +0x144
    float m_gainScale;          // +0x148
    float m_osc1MixHalf;        // +0x14c
    float m_osc2MixHalf;        // +0x150
    double m_invFs;             // +0x158
    int m_fsPerMs;              // +0x160
    float m_mix;                // +0x164
    int m_bRing;                // +0x168
    float m_pw;                 // +0x16c
    float m_commonTune;         // +0x170 cent
    int m_keyShift;             // +0x174
    float m_detune;             // +0x178 cent
    int m_lfoDest[2];           // +0x17c
    int m_oscModEnvDest;        // +0x184
    CVcf* m_pVcf;               // +0x188
    float m_portamentTime;      // +0x190
    int m_portLen;              // +0x194
    int m_portPos;              // +0x198
    int m_portCur;              // +0x19c
    int m_portDiff;             // +0x1a0
    int m_portStep;             // +0x1a4
    int m_portRes;              // +0x1a8
    int m_note;                 // +0x1ac
    int m_portNextRes;          // +0x1b0
    int m_phaseMode;            // +0x1b4
    float m_phaseShift;         // +0x1b8
    int m_bInitPhase;           // +0x1bc
    int m_unisonNum;            // +0x1c0
    int m_unisonNo;             // +0x1c4
    float m_unisonPhaseSpread;  // +0x1c8
};
