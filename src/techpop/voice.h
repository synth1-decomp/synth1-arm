#pragma once

/**
 * @file voice.h
 * @brief CVoice: one synth voice (oscillators -> filter -> amplifier) and its status values.
 */

#include "sigdev.h"
#include "vco.h"
#include "vcf.h"
#include "vca.h"

class CLFO;

// voice status (getCurStatus)
#define VOICE_DISABLED      0
#define VOICE_NOTEON        1
#define VOICE_RELEASING     2
#define VOICE_IDLE      3

/**
 * @brief One synth voice: oscillators -> filter -> amplifier.
 *
 * sizeof == 0x498 (CSynthesizer1 has CVoice m_voice[32] at +0x450)
 * The voice owns its CVco, CVcf and CVca; the LFOs are shared and not owned.
 */
class CVoice : public CSignalDevice {
public:
    /**
     * @brief Chains oscillator -> filter -> amplifier and sets default sounds; the voice starts disabled.
     */
    CVoice();
    virtual ~CVoice(){}

    /**
     * @brief Connects the two global LFOs to the oscillator, filter and amplifier.
     *
     * @param pLfo1 LFO 1 (not owned).
     * @param pLfo2 LFO 2 (not owned).
     */
    void setLfo(CLFO* pLfo1, CLFO* pLfo2);

    /**
     * @brief Passes LFO @p no's on/off state and destination to the oscillator, filter and amplifier.
     *
     * @param no    LFO index, 0 or 1.
     */
    void changeLFOSetting(int no);

    /**
     * @brief Enables or disables the voice; a disabled voice produces no output.
     *
     * @param bEnable   Nonzero: enabled; 0: disabled.
     */
    void enable(int bEnable);

    /**
     * @brief Tells whether the voice is sounding.
     *
     * @return Nonzero while the amplifier envelope is not off (held or releasing).
     */
    int isBusy();

    /**
     * @brief Returns the voice state.
     *
     * @return VOICE_DISABLED, VOICE_NOTEON (key held), VOICE_RELEASING (amp envelope still running)
     *         or VOICE_IDLE.
     */
    int getCurStatus();

    /**
     * @brief Renders the voice (through the amplifier, which pulls the filter and oscillator).
     *
     * @param[in,out] pBuf      Sample buffer.
     * @param size              Number of samples.
     * @param[out] pProcessed   Set by the chain; a disabled voice sets it to 1 and leaves pBuf untouched.
     * @return Number of samples (size for a disabled voice).
     */
    virtual int processSignal(int size, float* pBuf, int* pProcessed);

    /**
     * @brief Stores this voice's place in a unison group (used at the next trigger()).
     *
     * @param pBase First voice of the group (unused).
     * @param num   Number of unison voices.
     * @param no    Index of this voice in the group.
     */
    void setUnisonInfo(CVoice* pBase, int num, int no);

    /**
     * @brief Sets the unison phase spread passed to the oscillator at trigger().
     *
     * @param val   0..127, stored as 0..1.
     */
    void setUnisonPhaseShift(int val);

    /**
     * @brief Sets the note and velocity for the next trigger() and retunes the oscillator.
     *
     * Portamento is used for the pitch change; with auto portamento (m_bAutoPortament == 1)
     * only when a note is still held (legato).
     *
     * @param nn        MIDI note number.
     * @param velocity  Velocity, 0..127.
     */
    void setNoteInfo(int nn, int velocity);

    /**
     * @brief Starts the note: marks it held and starts the amplifier, filter and oscillator (with the
     *        unison phase info).
     */
    void trigger();

    /**
     * @brief Releases the note: starts the amplifier and filter envelope release.
     *
     * @param velocity  Release velocity (unused).
     */
    void release(int velocity);

    /**
     * @brief Returns the voice pan value.
     *
     * @return m_pan.
     */
    virtual float getPanVal(){ return m_pan; }

    CVco m_vco;                 // +0x018
    CVcf m_vcf;                 // +0x1e8
    CVca m_vca;                 // +0x3b0
    CLFO* m_pLfo[2];            // +0x460
    int m_bEnable;              // +0x470
    int m_bAutoPortament;       // +0x474  portamento only while a note is held
    float m_pan;                // +0x478
    float m_unisonPhaseShift;   // +0x47c
    int m_unisonNum;            // +0x480
    int m_unisonNo;             // +0x484
    int m_voiceNo;              // +0x488
    int m_bNoteOn;              // +0x48c
    int m_noteNo;               // +0x490
    int m_velocity;             // +0x494
};
