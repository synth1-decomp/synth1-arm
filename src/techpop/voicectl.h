#pragma once

/**
 * @file voicectl.h
 * @brief Keyboard state (CKeyboardInfo) and voice allocation (CVoiceCtl).
 *
 * CKeyboardInfo records which keys are held and in which order; CVoiceCtl turns those key changes into
 * voice triggers and releases for the poly, mono and legato modes, including unison.
 */

#include <list>

class CVoice;
class CMixDevice;

/**
 * @brief Key on/off state with damper (sustain pedal) handling.
 *
 * Keeps the velocity of each key and the order in which the held keys were pressed. Newly pressed
 * keys are stored in m_keyOrder as note + 0x100 until CVoiceCtl::controlVoiceTrigger() has handled
 * them. While the damper is down, key releases are deferred until the pedal is released.
 *
 * @note All inline (arp.cpp uses it from here too).
 */
// sizeof == 0x618.
class CKeyboardInfo {
public:
    /**
     * @brief Constructs an empty keyboard state (no keys held, damper up).
     */
    CKeyboardInfo(){
        m_keyOnNum = 0;
        m_bDumper = 0;
        m_bChanged = 0;
        for (int i = 0; i < 128; i++) {
            m_bOffReserved[i] = 0;
            m_velocity[i] = 0;
        }
    }

    /**
     * @brief Destroys the keyboard state; nothing to release.
     */
    virtual ~CKeyboardInfo(){}

    /**
     * @brief Registers a key press; a key that is already held is released first.
     *
     * @param nn   MIDI note number, 0..127.
     * @param vel  Velocity, 1..127.
     */
    void onKey(int nn, int vel){
        if (m_velocity[nn]) offKeyImpl(nn);
        m_velocity[nn] = vel;
        m_bOffReserved[nn] = 0;
        m_keyOrder[m_keyOnNum] = nn + 0x100;
        m_keyOnNum++;
        m_bChanged = 1;
    }

    /**
     * @brief Registers a key release, or defers it while the damper is down.
     *
     * @param nn  MIDI note number, 0..127.
     */
    void offKey(int nn){
        if (m_bDumper) {
            m_bOffReserved[nn] = 1;
        } else {
            offKeyImpl(nn);
        }
    }

    /**
     * @brief Releases a key immediately: clears its velocity and removes it from the key order.
     *
     * @param nn  MIDI note number, 0..127.
     */
    void offKeyImpl(int nn){
        m_velocity[nn] = 0;
        m_bChanged = 1;
        for (int i = 0; i < m_keyOnNum; i++) {
            int key = m_keyOrder[i];
            if (key > 0xff) key -= 0x100;
            if (key == nn) {
                for (; i < m_keyOnNum - 1; i++) {
                    m_keyOrder[i] = m_keyOrder[i + 1];
                }
                m_keyOnNum--;
                break;
            }
        }
    }

    /**
     * @brief Sets the damper (sustain pedal) state; releasing it performs the deferred key releases.
     *
     * @param b  Nonzero for pedal down.
     */
    void setDumper(int b){
        m_bDumper = b;
        if (b == 0) {
            for (int i = 0; i < 128; i++) {
                if (m_bOffReserved[i]) {
                    m_bOffReserved[i] = 0;
                    offKeyImpl(i);
                }
            }
        }
    }

    /**
     * @brief Gets the number of held keys.
     *
     * @return Number of keys currently held (including keys awaiting handling).
     */
    int getKeyOnNum(){ return m_keyOnNum; }

    /**
     * @brief Gets the velocity of a key.
     *
     * @param nn  MIDI note number, 0..127.
     * @return The velocity, or 0 when the key is not held.
     */
    int getVelocity(int nn){ return m_velocity[nn]; }

    int m_keyOnNum;             // +0x008
    int m_velocity[128];        // +0x00c  0 = off
    int m_keyOrder[128];        // +0x20c  pressed keys in order (note + 0x100)
    int m_bDumper;              // +0x40c
    int m_bOffReserved[128];    // +0x410  released while the damper was down
    int m_bChanged;             // +0x610
};

#define VOICECTL_MAX_VOICE  32

// poly modes (m_polyMode)
#define POLYMODE_POLY       0
#define POLYMODE_MONO       1
#define POLYMODE_LEGATO     2

/**
 * @brief Voice allocation: assigns the keys of a CKeyboardInfo to the voices
 *        (poly / mono / legato, unison).
 *
 * In unison mode each key triggers m_unisonVoiceNum voices with spread detune and pan.
 *
 * @note The voices, the mix device and the keyboard info are owned by the caller (CSynthesizer1).
 */
// sizeof == 0x1d0 (CSynthesizer1 +0xaba0)
class CVoiceCtl {
public:
    /**
     * @brief Constructs the controller in poly mode without unison.
     */
    CVoiceCtl();

    /**
     * @brief Sets the number of voices; also the polyphony and the number of enabled voices.
     *
     * @param num  Number of voices, up to VOICECTL_MAX_VOICE.
     */
    void setVoiceNum(int num);

    /**
     * @brief Registers a voice and tells it its index.
     *
     * @param no      Voice index, 0..VOICECTL_MAX_VOICE-1.
     * @param pVoice  The voice (not owned); its m_voiceNo is set to no.
     */
    void setVoice(int no, CVoice* pVoice);

    /**
     * @brief Sets the poly mode and recomputes the enabled voices.
     *
     * @param mode  POLYMODE_POLY, POLYMODE_MONO or POLYMODE_LEGATO.
     */
    void setPolyMode(int mode);

    /**
     * @brief Enables the voices that the current mode can use and disables (releases) the rest.
     *
     * Poly mode uses m_polyNum voices; mono/legato use one voice, or the unison voice count, limited
     * by m_polyNum. Tells the mix device how many voices are enabled.
     */
    void calcEnableVoices();

    /**
     * @brief Sets the polyphony and recomputes the enabled voices.
     *
     * @param num  Number of voices, clamped to 1..m_voiceNum.
     */
    void setPolyNum(int num);

    /**
     * @brief Turns unison on or off and updates detune, pan and the enabled voices.
     *
     * @param mode  1 for unison on; any other value turns it off.
     */
    void setUnisonMode(int mode);

    /**
     * @brief Distributes the unison detune over the voices.
     *
     * The detune range (0..100 cents, exponential in m_unisonDetune) is spread evenly across the
     * unison voices; every odd voice is also shifted by m_unisonPitch. Without unison, all voices get
     * zero detune.
     */
    void calcUnisonDetuneParams();

    /**
     * @brief Spreads the unison voices evenly over -m_unisonPan..+m_unisonPan, or centers all voices without unison.
     */
    void calcUnisonPan();

    /**
     * @brief Sets the number of voices per key in unison mode.
     *
     * @param num  Voices per key, at least 1.
     */
    void setUnisonVoiceNum(int num);

    /**
     * @brief Sets the unison detune amount.
     *
     * @param val  0..127.
     */
    void setUnisonDetune(int val);

    /**
     * @brief Sets the pitch offset of the odd unison voices.
     *
     * @param val  Pitch value; the offset is (val - 24) semitones (stored in cents).
     */
    void setUnisonPitch(int val);

    /**
     * @brief Sets the unison pan spread.
     *
     * @param pan  Spread width; the voices are placed between -pan and +pan.
     */
    void setVoicePan(float pan);

    /**
     * @brief Sets the keyboard state to read key changes from.
     *
     * @param pKbdInfo  The keyboard state (not owned).
     */
    void setKeyboardInfo(CKeyboardInfo* pKbdInfo);

    /**
     * @brief Finds a free voice, searching round robin from the voice after m_lastVoice.
     *
     * @return The first enabled voice without a note on, or NULL if all are busy.
     */
    CVoice* getNextOffVoice();

    /**
     * @brief Processes the key changes recorded in the keyboard state.
     *
     * Does nothing unless the keyboard state changed; otherwise dispatches to controlVoicePoly() or
     * controlVoiceMono() and then marks all held keys as handled.
     *
     * @throws std::bad_alloc  From the active voice list.
     */
    void controlVoiceTrigger();

    /**
     * @brief Poly mode: releases voices whose key went off and triggers voices for newly pressed keys.
     *
     * Each new key gets one voice (or m_unisonVoiceNum in unison mode); when no voice is free, the
     * oldest active voice is stolen.
     *
     * @throws std::bad_alloc  From the active voice list.
     */
    void controlVoicePoly();

    /**
     * @brief Mono / legato mode: plays the most recently pressed held key.
     *
     * When all keys are released the voices are released. In mono mode the voices are retriggered on
     * every change; in legato mode only voices that are not sounding are triggered, the others just
     * change pitch.
     *
     * @throws std::bad_alloc  From the active voice list.
     */
    void controlVoiceMono();

    /**
     * @brief Gets the per-voice status array.
     *
     * @return Pointer to m_voiceStatus (VOICECTL_MAX_VOICE entries, VOICE_xxx), as updated by isVoicesStatusChanged().
     */
    int* getVoicesStatus();

    /**
     * @brief Refreshes the status of all VOICECTL_MAX_VOICE voices.
     *
     * @return 1 if any voice status changed since the last call, otherwise 0.
     */
    int isVoicesStatusChanged();

    CMixDevice* m_pMixDevice;               // +0x000
    CVoice* m_pVoice[VOICECTL_MAX_VOICE];   // +0x008
    int m_voiceStatus[VOICECTL_MAX_VOICE];  // +0x108
    int m_voiceNum;                         // +0x188
    int m_polyMode;                         // +0x18c
    int m_polyNum;                          // +0x190
    int m_bUnison;                          // +0x194
    int m_unisonVoiceNum;                   // +0x198
    int m_unisonDetune;                     // +0x19c  0..127
    int m_unisonPitch;                      // +0x1a0  cents
    float m_unisonPan;                      // +0x1a4
    CKeyboardInfo* m_pKbdInfo;              // +0x1a8
    int m_enableVoiceNum;                   // +0x1b0
    int m_lastVoice;                        // +0x1b4  last triggered voice (round robin)
    std::list<CVoice*> m_activeVoices;      // +0x1b8  voices in trigger order
    int m_monoVelocity;                     // +0x1c8
};
