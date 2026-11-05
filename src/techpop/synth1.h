#pragma once

/**
 * @file synth1.h
 * @brief The Synth1 engine class CSynthesizer1 and its supporting types.
 *
 * Declares CSynthesizer1 (implemented in synth1.cpp and synmidi.cpp), the patch class
 * CSoundData (implemented in sound.cpp), the MIDI controller assignment types, the
 * parameter table used for MIDI assignment, and S1G, the process-wide globals shared by
 * all plugin instances.
 */

#include "sigdev.h"
#include "inst.h"
#include "sound.h"
#include "voice.h"
#include "delay.h"
#include "eq.h"
#include "efxs.h"
#include "pan.h"
#include "mixdev.h"
#include "arp.h"
#include "lfo.h"
#include "chorus.h"
#include "voicectl.h"

/**
 * @brief Converts an effect balance value into dry and wet levels.
 *
 * At 64 both levels are 1.0. Below 64 the dry level stays 1.0 and the wet level falls
 * linearly to 0 at 0; above 64 the wet level stays 1.0 and the dry level falls linearly
 * to 0 at 127.
 *
 * @param v         Balance, 0..127.
 * @param[out] dry  Dry level, 0..1.
 * @param[out] wet  Wet level, 0..1.
 */
inline void calcDryWet(int v, float& dry, float& wet)
{
    float d, w;
    if (!(v < 65)) {
        d = (127 - v) / 63.0f;
        w = 1.0f;
    } else {
        d = 1.0f;
        float t = (v - 1) / 63.0f;
        w = (v != 0) ? t : 0;
    }
    dry = d;
    wet = w;
}

class CSoundData;
class CVoice;
class CLFO;
struct tagWriteProgramInfo;

// ---------------------------------------------------------------------------
#ifndef CSOUNDDATA_DEFINED
#define CSOUNDDATA_DEFINED
#define SOUNDDATA_PARAMS_MAX    100

/**
 * @brief One patch: name, colour, version, bank/program location and a list of
 *        parameter id/value pairs.
 *
 * sizeof == 0xb60 (CSoundLibrary embeds one at +0x808, CSynthesizer1 one at +0xc3a0).
 * The code lives in sound.cpp. sound.h holds the same definition; both are guarded by
 * CSOUNDDATA_DEFINED so that whichever header is included first defines the class.
 */
class CSoundData {
public:
    /**
     * @brief Constructs an empty patch.
     *
     * Calls init(): no parameters, name "not defined".
     *
     * @see init()
     */
    CSoundData();

    /**
     * @brief Destroys the patch.
     */
    virtual ~CSoundData(){}

    /**
     * @brief Empties the parameter list and sets the name to "not defined".
     *
     * All m_paramID slots become -1 and all values 0. The colour, location and version
     * are left unchanged.
     */
    void init();

    /**
     * @brief Returns the id and value stored in one slot of the parameter list.
     *
     * @param idx        Slot index, 0..getParamsNum()-1 (not range checked).
     * @param[out] pID   Parameter id (-1 for an empty slot).
     * @param[out] pVal  Parameter value.
     */
    void enumParam(int idx, int* pID, int* pVal);

    /**
     * @brief Sets the value of a parameter, appending it to the list if it is not there yet.
     *
     * Silently does nothing when all SOUNDDATA_PARAMS_MAX slots are used by other ids.
     *
     * @param id   Parameter id.
     * @param val  New value.
     */
    void setParamVal(int id, int val);

    /**
     * @brief Looks up the value of a parameter.
     *
     * @param id  Parameter id.
     * @return The stored value, or 0 if the patch does not contain the parameter.
     */
    int getParamVal(int id);

    /**
     * @brief Gets the number of parameters in the list.
     *
     * @return m_paramNum, the number of valid (id, value) entries.
     */
    int getParamsNum();

    char m_name[32];                        // +0x008  ("not defined")
    unsigned int m_color;                   // +0x028
    int m_paramID[SOUNDDATA_PARAMS_MAX];    // +0x02c  (-1 = empty)
    int m_paramVal[SOUNDDATA_PARAMS_MAX];   // +0x1bc
    int m_paramNum;                         // +0x34c
    int m_bankNo;                           // +0x350
    char m_path[0x800];                     // +0x354
    int m_progNo;                           // +0xb54
    int m_version;                          // +0xb58  (113 == 0x71 for V1.13)
};
#endif

// ---------------------------------------------------------------------------
// CKeyboardInfo is defined in inst.h.

/**
 * @brief One MIDI controller to parameter assignment slot ("midi ctrl src/sens/assign").
 *
 * The controller value, scaled by the sensitivity and the parameter's range, is added to
 * the patch value of the assigned parameter (see CSynthesizer1::setSoundParam2EngineMain()).
 */
struct MidiCtrlAssign {
    union {
        int src;    // +0x0  (status << 8) | cc, e.g. 0xb001 = CC1
        BYTE srcCC; // +0x0  low byte: the cc number
    };
    float sens;     // +0x4
    int paramID;    // +0x8  (-1 = none)
    float val;      // +0xc  current source value
};

// ---------------------------------------------------------------------------
/**
 * @brief The synth engine: 32 voices, the effect chain, arpeggiator, LFOs, the patch
 *        library and MIDI handling.
 *
 * Signal flow: voices -> mixer -> effector -> peaking EQ -> tone EQ -> pan -> delay ->
 * chorus. processSignalW() pulls the output from the chorus.
 *
 * sizeof == 0xCF50 (operator new in audiounit/Synth1.cpp). Primary base CSignalDevice
 * (+0x00), CInst at +0x18.
 *
 * @note GUI notifications are posted to m_pSynWin with PostMessageControl():
 *       0x720 (parameter changed: id, value), 0x721 (program loaded) and 0x723 (voice
 *       status changed).
 */
class CSynthesizer1 : public CSignalDevice, public CInst {
public:
    /**
     * @brief Builds the engine: wires the signal chain, sets up the voices, LFOs and
     *        arpeggiator, and loads the initial program.
     *
     * Calls initCCMap() first, reads "enableBankSelect" and "enableProgramChange" from the
     * ini file, sets the tempo to 120 BPM and the volume to 64, requests bank 0 /
     * program 0, builds m_initSound (createInitialSound()) and loads the program.
     */
    CSynthesizer1();

    /**
     * @brief Destroys the engine (does nothing).
     */
    virtual ~CSynthesizer1();

    // CSignalDevice
    /**
     * @brief Mono processing entry; does nothing.
     *
     * @param size              Number of samples.
     * @param pBuf              Unused.
     * @param[out] pProcessed   Set to 1 (silent).
     * @return size.
     */
    virtual int processSignal(int size, float* pBuf, int* pProcessed);

    /**
     * @brief Renders interleaved stereo output for one block.
     *
     * The block is split into segments that end at the next arpeggiator event, at most
     * 32 frames while the pitch bend glides (processChangeParams() runs once per segment),
     * and at the cycle boundaries of enabled sample & hold / smooth random LFOs. Each
     * segment triggers voices and pulls the signal through the chain from the chorus. Once
     * the chain reports silence the loop stops; if some output was already produced, the
     * last segment is zeroed and the block is reported as not silent. The master volume is
     * then applied (smoothed with processMainVolume() while it changes), and message 0x723
     * is posted to the GUI window when the voice status changed.
     *
     * If CSignalDevice::m_bSSE is 0 nothing is rendered and the block is reported silent.
     *
     * @param size              Number of stereo frames.
     * @param[in,out] pBuf      Interleaved L/R buffer of 2*size floats.
     * @param[out] pProcessed   Nonzero if the block is silent (pBuf is then not written).
     * @return Number of frames processed (normally size).
     * @note Runs on the audio thread.
     */
    virtual int processSignalW(int size, float* pBuf, int* pProcessed);

    /**
     * @brief Propagates m_samplingFreq to the mixer, EQs, effects, arpeggiator, LFOs and
     *        every voice's VCO, VCF and VCA, and recomputes the volume smoothing
     *        coefficient.
     */
    virtual void onChangeSamplingFreq();

    // CInst
    /**
     * @brief Presses a key in the MIDI keyboard state.
     *
     * On the first held key, the arpeggiator timing is reset (if it is playing) and
     * key-synced LFOs are restarted.
     *
     * @param nn   Note number, 0..127.
     * @param vel  Velocity, 1..127.
     */
    virtual void noteOn(int nn, int vel);

    /**
     * @brief Releases a key in the MIDI keyboard state; resets the arpeggiator timing when
     *        no key remains held.
     *
     * @param nn  Note number, 0..127.
     */
    virtual void noteOff(int nn);

    /**
     * @brief Sets the sustain (damper) pedal state; resets the arpeggiator timing when no
     *        key remains held.
     *
     * @param b  Nonzero: pedal down.
     */
    virtual void setSustainEnable(int b);

    /**
     * @brief Presses a key in the arpeggiator keyboard state (m_kbdInfoArp).
     *
     * @param nn   Note number.
     * @param vel  Velocity.
     */
    virtual void noteOnFromArp(int nn, int vel);

    /**
     * @brief Releases a key in the arpeggiator keyboard state (m_kbdInfoArp).
     *
     * @param nn  Note number.
     */
    virtual void noteOffFromArp(int nn);

    /**
     * @brief Handles a MIDI program change: requests the program and loads it.
     *
     * @param progNo  Program number, 0..127.
     */
    virtual void receiveProgramChange(int progNo);

    /**
     * @brief Handles a MIDI bank select: requests the bank (it is loaded with the next
     *        program change).
     *
     * @param bankNo  Bank number.
     */
    virtual void receiveBankChange(int bankNo);

    /**
     * @brief Sets the pitch bend target; processSignalW() glides towards it.
     *
     * @param v  Pitch bend, -8192..8191; converted to -63..63 steps.
     */
    virtual void setPitchBend(short v);

    /**
     * @brief Sets the master volume (common parameter 254) and notifies the GUI window.
     *
     * @param v  Volume, 0..127.
     */
    virtual void setVolume(unsigned char v);

    /**
     * @brief Applies MIDI control changes to sound parameters and updates the controller
     *        assignment slots.
     *
     * A CC below 120 sets the parameter mapped to it in s1g->m_ccMap (the value is wrapped
     * into the parameter range by checkSoundParam()). In MIDI learn mode the CC is first
     * mapped to m_midiLearnParamID (removing that parameter's previous CC), or ignored if no
     * parameter is selected. Then each m_midiCtrl slot whose source matches the message
     * (CC number, channel aftertouch or pitch bend) stores the new controller value and
     * re-applies its assigned parameter.
     *
     * @param st   Status byte.
     * @param dt1  First data byte.
     * @param dt2  Second data byte.
     */
    virtual void processMIDIShortDataIndividual(unsigned char st, unsigned char dt1, unsigned char dt2);

    /**
     * @brief Computes m_volLPFCoef for the current sampling rate.
     *
     * The smoothed volume closes half of the remaining distance every 2 ms.
     */
    void calcVolLPFCoef();

    /**
     * @brief Fills m_initSound with the built-in default patch ("initial sound",
     *        version 113, colour 0xffffffff).
     *
     * m_initSound supplies the defaults when a patch is loaded or converted.
     */
    void createInitialSound();

    /**
     * @brief Loads the requested bank/program from the sound library and applies it to the
     *        engine, unless it is already loaded.
     *
     * The patch is upgraded with convertParams(), every parameter (ids below 99 or above
     * 250) is applied with setSoundParam() without notification, message 0x721 is posted
     * to the GUI window, and the free-running oscillator phases are reinitialised. The
     * program to load is the one set with CSoundLibrary::setReqProgNo().
     *
     * @param progNo  Not a program number: passed as wParam of message 0x721 (0 makes the
     *                window also notify the host).
     */
    void loadProgram(int progNo);

    /**
     * @brief Returns whether the host may select programs.
     *
     * Reads "hostselectprogram" (default 0) from the ini file on the first call and caches
     * it in m_bCanVSTHostChangePatch.
     *
     * @return Nonzero if the host may select programs.
     */
    static int canVSTHostChangePatch();

    /**
     * @brief Enables or disables MIDI bank select and saves the setting to the ini file.
     *
     * @param b  Nonzero to enable.
     */
    void setEnableBankSelect(int b);

    /**
     * @brief Enables or disables MIDI program change and saves the setting to the ini file.
     *
     * @param b  Nonzero to enable.
     */
    void setEnableProgramChange(int b);

    /**
     * @brief Sets the GUI window that receives notifications.
     *
     * @param pWin  The window, or NULL for none; stored in m_pSynWin.
     */
    void setSynWin(void* pWin);

    /**
     * @brief Moves the current pitch bend one step towards the target and applies it to
     *        all voices.
     *
     * Clears m_bPitchBendChanging once the target is reached.
     */
    void processChangeParams();

    /**
     * @brief Multiplies a buffer by the master volume while smoothing it towards a new value.
     *
     * The gain follows a one-pole low-pass (m_volLPFCoef) per sample; it snaps to vol once
     * within 1/65536. The result is stored in m_curVolume.
     *
     * @param[in,out] pBuf  Samples to scale.
     * @param vol           Target gain.
     * @param size          Number of floats (2 per stereo frame).
     */
    void processMainVolume(float* pBuf, float vol, int size);

    /**
     * @brief Gets the current patch (the sound library's working copy).
     *
     * @return m_pCurSound.
     */
    CSoundData* getCurrentSound();

    /**
     * @brief Sets the master tuning and rebuilds the note frequency table.
     *
     * @param tune  Offset from A4 = 440 Hz in cents.
     */
    static void setMasterTune(int tune);

    /**
     * @brief Sets a parameter that is not stored in the patch.
     *
     * 254: master volume (0..127, mapped to a gain curve); 251: requests a bank;
     * 252: requests a program. Bank and program requests are not loaded here and are never
     * notified. Other ids only send the notification.
     *
     * @param id       Parameter id (251, 252 or 254).
     * @param val      Value.
     * @param bNotify  Nonzero to post message 0x720 to the GUI window.
     */
    void setCommonParam(int id, int val, int bNotify);

    /**
     * @brief Sets a patch parameter: stores it in the current patch and applies it to the
     *        engine.
     *
     * Does nothing if the same id and value were set by the previous call.
     *
     * @param id       Parameter id (sound parameters are 0..98).
     * @param val      Value.
     * @param bNotify  Nonzero to post message 0x720 to the GUI window.
     */
    void setSoundParam(int id, int val, int bNotify);

    /**
     * @brief Applies a parameter value to the engine without storing it in the patch.
     *
     * @param id   Parameter id.
     * @param val  Value.
     * @see setSoundParam2EngineMain()
     */
    void setSoundParam2Engine(int id, int val);

    /**
     * @brief Applies a parameter value to the voices, effects, arpeggiator or LFOs.
     *
     * For parameters with a MIDI range, the offsets of the controller slots assigned to
     * the parameter are added first and the result is clamped to the range. The value is
     * then converted to the engine's units (times, frequencies, levels) and passed to
     * the components concerned. Unknown ids are ignored.
     *
     * @param id   Parameter id.
     * @param val  Value (usually 0..127).
     */
    void setSoundParam2EngineMain(int id, int val);

    /**
     * @brief Sets the delay time as a tempo-relative note length.
     *
     * @param val  Index into the delay beat table, 0..19 (clamped).
     */
    void setDelayTime(int val);

    /**
     * @brief Sets the filter in the delay feedback path.
     *
     * 64 disables it; below 64 it is a low-pass from 200 Hz (0) to 22 kHz (63); above
     * 64 a high-pass from 10 Hz (65) to 8 kHz (127).
     *
     * @param val  Tone, 0..127.
     */
    void setDelayToneVal(int val);

    /**
     * @brief Enables or disables the effector.
     *
     * @param b  Nonzero to enable, 0 to disable.
     */
    void setEFX1Enable(int b);

    /**
     * @brief Sets the tone EQ.
     *
     * Up to 64 it is a low-pass from 200 Hz (0) to 22 kHz (64); above 64 a high-pass
     * from about 11 Hz (65) to about 7 kHz (127).
     *
     * @param val  Tone, 0..127.
     */
    void setToneVal(int val);

    /**
     * @brief Enables or disables an LFO and updates the voices and the panner.
     *
     * @param no  LFO index, 0..1.
     * @param b   1 enables; any other value disables.
     */
    void setLFOEnable(int no, int b);

    /**
     * @brief Sets an LFO destination and updates the voices and the panner.
     *
     * @param no    LFO index, 0..1.
     * @param dest  Destination index.
     */
    void setLFODest(int no, int dest);

    /**
     * @brief Sets an LFO speed dial.
     *
     * @param no   LFO index, 0..1.
     * @param val  Speed, 0..127.
     */
    void setLFOSpeed(int no, int val);

    /**
     * @brief Sets an LFO depth dial.
     *
     * @param no   LFO index, 0..1.
     * @param val  Depth, 0..127.
     */
    void setLFODepth(int no, int val);

    /**
     * @brief Enables or disables tempo sync of an LFO.
     *
     * @param no  LFO index, 0..1.
     * @param b   Nonzero to enable.
     */
    void setLFOTempoSync(int no, int b);

    /**
     * @brief Enables or disables key sync of an LFO (restart on the first key).
     *
     * @param no  LFO index, 0..1.
     * @param b   Nonzero to enable.
     */
    void setLFOKeySync(int no, int b);

    /**
     * @brief Returns whether an LFO is tempo synced.
     *
     * @param no  LFO index, 0..1.
     * @return Nonzero if tempo synced.
     */
    int isLFOTempoSync(int no);

    /**
     * @brief Gets the base tempo.
     *
     * @return The tempo in BPM (m_baseTempo).
     */
    float getBaseTempo();

    /**
     * @brief Does nothing (the delay is always tempo synced).
     *
     * @param b  Ignored.
     */
    static void setDelayTempoSync(int b);

    /**
     * @brief Gets the name of a program in a bank.
     *
     * The bank cache is refreshed when progNo is 0.
     *
     * @param bankNo     Bank number, or -1 for a zip bank given by bankPath.
     * @param bankPath   Zip bank path (used when bankNo is -1), may be NULL.
     * @param progNo     Program number, 0..127.
     * @param[out] name  Receives the name, or "not found" if the bank does not exist.
     */
    void getProgramName(int bankNo, char* bankPath, int progNo, char* name);

    /**
     * @brief Upgrades a patch from an older format version to version 113.
     *
     * Each parameter (ids below 99 or above 250) is converted with convertSingleParam();
     * parameters introduced after the patch's version get their values from m_initSound
     * (only for ids already present in the patch).
     *
     * @param[in,out] pSound  Patch to upgrade; its m_version becomes 113.
     */
    void convertParams(CSoundData* pSound);

    /**
     * @brief Converts one parameter value from an older patch version to version 113.
     *
     * Applies the per-version steps from ver onwards in sequence. Versions not listed
     * (for example 110 or 113) return the value unchanged.
     *
     * @param id    Parameter id.
     * @param val   Value in the old format.
     * @param ver   Patch version (100, 105..109, 111, 112).
     * @param pSrc  The old patch (some conversions read other parameters from it).
     * @param pDst  Patch that supplies defaults for parameters added later.
     * @return The converted value.
     */
    static int convertSingleParam(int id, int val, int ver, CSoundData* pSrc, CSoundData* pDst);

    /**
     * @brief Version 1.00 to 1.05 step: remaps the osc2 fine tune, envelope attack, amp
     *        sustain/gain/velocity, and LFO speed curves to the nearest value on the new
     *        curves.
     *
     * Parameters 67..72 take their default from pDst.
     *
     * @param id    Parameter id.
     * @param val   Old value, 0..127.
     * @param pDst  Patch supplying defaults.
     * @return The converted value.
     */
    static int convertSingleParam100To105(int id, int val, CSoundData* pDst);

    /**
     * @brief Version 1.05 to 1.07 step: remaps the filter amount, delay dry/wet, chorus
     *        delay time/rate/feedback, and decay/release curves.
     *
     * Parameters 75..85 and 90 take their default from pDst.
     *
     * @param id    Parameter id.
     * @param val   Old value, 0..127.
     * @param pDst  Patch supplying defaults.
     * @return The converted value.
     */
    static int convertSingleParam105To107(int id, int val, CSoundData* pDst);

    /**
     * @brief Version 1.07 to 1.071 step: sets fixed defaults for the unison pan spread (64),
     *        unison pitch (24) and the MIDI controller sources and assignments (0).
     *
     * @param id   Parameter id.
     * @param val  Old value.
     * @return The converted value.
     */
    static int convertSingleParam107To1071(int id, int val);

    /**
     * @brief Version 1.071 to 1.09 step: recomputes the unison detune and voice count from
     *        the play mode, rescales the MIDI controller sensitivities (which used to be
     *        scaled by the LFO depth), and takes defaults from pDst for parameters 86..89,
     *        91, 92 and 94.
     *
     * @param id    Parameter id.
     * @param val   Old value.
     * @param pSrc  The old patch.
     * @param pDst  Patch supplying defaults.
     * @return The converted value.
     */
    static int convertSingleParam1071To109(int id, int val, CSoundData* pSrc, CSoundData* pDst);

    /**
     * @brief Version 1.09 to 1.11 step: parameters 95..97 (sub oscillator) take their
     *        default from pDst.
     *
     * @param id    Parameter id.
     * @param val   Old value.
     * @param pSrc  Unused.
     * @param pDst  Patch supplying defaults.
     * @return The converted value.
     */
    static int convertSingleParam109To111(int id, int val, CSoundData* pSrc, CSoundData* pDst);

    /**
     * @brief Version 1.11 to 1.12 step: parameter 98 (delay tone) takes its default from pDst.
     *
     * @param id    Parameter id.
     * @param val   Old value.
     * @param pSrc  Unused.
     * @param pDst  Patch supplying defaults.
     * @return The converted value.
     */
    static int convertSingleParam111To112(int id, int val, CSoundData* pSrc, CSoundData* pDst);

    /**
     * @brief Version 1.12 to 1.13 step: no changes.
     *
     * @param id    Unused.
     * @param val   Value.
     * @param pSrc  Unused.
     * @param pDst  Unused.
     * @return val.
     */
    static int convertSingleParam112To113(int id, int val, CSoundData* pSrc, CSoundData* pDst);

    /**
     * @brief Saves the current patch to a bank slot and requests that slot as the current
     *        bank/program on success.
     *
     * @param pInfo  Destination bank/program, name and colour.
     * @return 1 on success, 0 if the destination path could not be determined, -1 on
     *         failure (zip bank or file error).
     */
    int writeProgram(tagWriteProgramInfo* pInfo);

    /**
     * @brief Identifies the format of saved plugin state.
     *
     * @param pData  State data.
     * @param size   Size in bytes.
     * @return 0 for DXi data (0x814 bytes with the DXi header), the chunk version (1 or 2)
     *         for a "Synth1 VST Chunk Data" chunk, or -1 if unrecognised.
     */
    int getChunkVer(unsigned char* pData, int size);

    /**
     * @brief Writes the current state as a version 2 VST chunk.
     *
     * The chunk holds the host-select-program flag, the bank path, the bank, program,
     * version and volume, and every parameter of the current patch.
     *
     * @param[out] pData  Destination buffer.
     * @param size        Buffer size in bytes.
     * @return Number of bytes written, or 0 (after a message box) if the buffer is too small.
     */
    int makeVSTChunkData(unsigned char* pData, int size);

    /**
     * @brief Sets the current patch's bank, program, path and name from the pending library
     *        request without loading its parameters.
     *
     * Posts 0x720 notifications for the program and bank to the GUI window.
     */
    void setCurrentProgNameOnly();

    /**
     * @brief Restores state from a VST chunk.
     *
     * Sets the volume, requests the stored bank and program (setting only the name), then
     * builds a patch from m_initSound plus the stored parameters 0..98, upgrades it with
     * convertParams() and applies every parameter with notification.
     *
     * @param pData     Chunk data.
     * @param size      Unused.
     * @param chunkVer  Chunk version from getChunkVer(), 1 or 2.
     */
    void setVSTChunkData(unsigned char* pData, int size, int chunkVer);

    /**
     * @brief Restores state from DXi save data.
     *
     * Applies the stored common parameters 251..254, sets the program name, then builds a
     * patch from m_initSound plus the stored parameters 0..98 (version from parameter 250,
     * else 100), upgrades it and applies every parameter with notification.
     *
     * @param pData  DXi data (see getChunkVer()).
     * @return 1.
     */
    int setDxiSaveParams(unsigned char* pData);

    /**
     * @brief Sets the host tempo and updates the arpeggiator, the delay time and tempo-synced
     *        LFOs. Does nothing if the tempo is unchanged.
     *
     * @param tempo  Tempo in BPM.
     */
    void setBaseTempo(float tempo);

    /**
     * @brief Converts an internal parameter value to the 0..1 host parameter value.
     *
     * @param id   Parameter id.
     * @param val  Internal value.
     * @param ver  Patch version (some parameters had other ranges in old versions).
     * @return The host value (undefined for unknown ids).
     * @see convValIntFloat()
     */
    static float valSyn2Vst(int id, int val, int ver);

    /**
     * @brief Converts a parameter value between the internal integer and the 0..1 float of
     *        the host parameter.
     *
     * Option lists map each choice to the centre of an equal slot (or to fixed values);
     * continuous parameters use 128 slots.
     *
     * @param id              Parameter id.
     * @param[in,out] pInt    Internal value: read when bToFloat is nonzero, else written.
     * @param[in,out] pFloat  Host value 0..1: written when bToFloat is nonzero, else read.
     * @param bToFloat        Nonzero: int to float; 0: float to int.
     * @param ver             Patch version (affects the arpeggiator octave range, the LFO
     *                        destinations and the bank count).
     * @return TRUE if the id is known, FALSE otherwise (nothing written).
     */
    static int convValIntFloat(int id, int* pInt, float* pFloat, int bToFloat, int ver);

    /**
     * @brief Converts a 0..1 host parameter value to the internal value.
     *
     * @param id   Parameter id.
     * @param val  Host value, 0..1.
     * @param ver  Patch version.
     * @return The internal value (undefined for unknown ids).
     * @see convValIntFloat()
     */
    static int valVst2Syn(int id, float val, int ver);

    /**
     * @brief Converts a host parameter value of an old patch version to the current internal
     *        value (valVst2Syn() followed by convertSingleParam()).
     *
     * @param id    Parameter id.
     * @param val   Host value, 0..1.
     * @param ver   Patch version.
     * @param pSrc  The old patch.
     * @param pDst  Patch supplying defaults.
     * @return The internal value.
     */
    static int valVstOld2Syn(int id, float val, int ver, CSoundData* pSrc, CSoundData* pDst);

    /**
     * @brief Formats a CC map as 128 comma-separated parameter ids.
     *
     * @param pMap      128 entries (CC number -> parameter id, -1 for none).
     * @param[out] str  Destination; must hold the whole string (callers use 0x400 bytes).
     */
    static void createCSVStringFromCCMap(int* pMap, char* str);

    /**
     * @brief Parses a CC map from a list of numbers separated by spaces, commas, tabs or
     *        newlines.
     *
     * pMap is first filled with -1; values outside 0..255 leave the entry at -1.
     *
     * @param[out] pMap     128 entries.
     * @param[in,out] str   Text to parse; modified by strtok().
     * @return TRUE if 128 values were read; otherwise shows a message box and returns
     *         FALSE (pMap is then partially filled).
     */
    static int getCCMapFromCSV(int* pMap, char* str);

    /**
     * @brief Maps a controller value into a parameter's MIDI range.
     *
     * @param id          Parameter id.
     * @param val         Controller value, 0..127.
     * @param[out] pVal   min + val modulo (max - min + 1).
     * @return TRUE if the parameter has a range, FALSE otherwise (pVal not written).
     */
    static int checkSoundParam(int id, int val, int* pVal);

    // synmidi.cpp
    /**
     * @brief Initialises the process-wide MIDI tables in s1g (only once per process).
     *
     * Reads "installfolder" and "CCMap" from the ini file (falling back to the default CC
     * map), fills the parameter range table from g_midiAssignParams, and builds the lists of
     * controller sources and of parameters assignable to the controller slots.
     */
    static void initCCMap();

    /**
     * @brief Builds the default CC map from the default CCs in g_midiAssignParams.
     *
     * @param[out] pMap  128 entries (CC number -> parameter id, -1 for none).
     */
    static void getDefaultCcMap(int* pMap);

    /**
     * @brief Sets the process-wide CC map and saves it to the ini file.
     *
     * @param pMap  128 entries (CC number -> parameter id, -1 for none).
     */
    static void setCCMap(int* pMap);

    /**
     * @brief Leaves MIDI learn mode, saves the CC map to the ini file and shows a message
     *        box if a GUI window is set.
     *
     * @return TRUE if learn mode was active, FALSE otherwise.
     */
    int endMidiLearning();

    /**
     * @brief Enters MIDI learn mode (shows a message box if a GUI window is set).
     *
     * While learning, the next CC received is mapped to m_midiLearnParamID.
     *
     * @return 1 if learn mode was entered, 0 if it was already active.
     */
    int startMidiLearning();

    /**
     * @brief Returns a parameter's MIDI value range.
     *
     * @param id         Parameter id, 0..255.
     * @param[out] pMin  Minimum, or -1 if the parameter is not in the table.
     * @param[out] pMax  Maximum, or -1 if the parameter is not in the table.
     */
    static void getParamValRange(int id, int* pMin, int* pMax);

    /**
     * @brief Assigns a parameter to a MIDI controller slot.
     *
     * The previously assigned parameter is restored without the controller offset and the
     * new one is re-applied. Ids not in the assignable list are ignored.
     *
     * @param no  Slot index, 0..1.
     * @param id  Parameter id, or -1 for none.
     */
    void setMidiCtrlAssignParamID(int no, int id);

    /**
     * @brief Sets the source of a MIDI controller slot, resets its value and re-applies the
     *        assigned parameter. Sources not in the source list are ignored.
     *
     * @param no   Slot index, 0..1.
     * @param src  (status << 8) | cc, e.g. 0xb001 for CC1, 0xd000 for channel aftertouch,
     *             0xe000 for pitch bend.
     */
    void setMidiCtrlSrc(int no, int src);

    /**
     * @brief Sets the sensitivity of a MIDI controller slot and re-applies the assigned
     *        parameter.
     *
     * @param no    Slot index, 0..1.
     * @param sens  Sensitivity, -1..1 (fraction of the parameter's range).
     */
    void setMidiCtrlSens(int no, float sens);

    /**
     * @brief Returns whether a parameter can be mapped to a MIDI CC.
     *
     * @param id  Parameter id.
     * @return 1 if the parameter is in g_midiAssignParams and its default CC is not -2,
     *         otherwise 0.
     */
    static int isMidiCCAssignable(int id);

    /**
     * @brief Gets the display name of a parameter from g_midiAssignParams.
     *
     * @param id         Parameter id.
     * @param[out] name  Receives the name (not written if the id is unknown).
     * @return 1 if found, 0 otherwise.
     */
    static int getParamName(int id, char* name);

    static int m_bCanVSTHostChangePatch;

    // +0x00 CSignalDevice (vptr, m_pInput, m_samplingFreq)
    // +0x18 CInst
    int m_volumeVal;                // +0x0038  master volume 0..127 (param 254)
    float m_volume;                 // +0x003c  master volume gain
    void* m_hNotifyWnd;             // +0x0040  AU: view that gets PostMessage(0x8003) on parameter changes
    int m_bParamChanged[256];       // +0x0048  AU: set to 1 by Synth1::SetParameter
    void* m_pSynWin;                // +0x0448  GUI window (HWND)
    CVoice m_voice[32];             // +0x0450
    CDelay m_delay;                 // +0x9750
    CEq m_delayEq;                  // +0x9800  delay tone
    CChorus m_chorus;               // +0x9880
    CEffector m_effector;           // +0x9930
    CEq m_eq;                       // +0x9ca0  peaking EQ
    CEq m_toneEq;                   // +0x9d20  tone (LPF/HPF)
    CPan m_pan;                     // +0x9da0
    CMixDevice m_mixer;             // +0x9de0
    CArpeggiator m_arp;             // +0x9f08
    CKeyboardInfo m_kbdInfo;        // +0x9f70  keys from MIDI
    CKeyboardInfo m_kbdInfoArp;     // +0xa588  keys from the arpeggiator
    CVoiceCtl m_voiceCtl;           // +0xaba0
    CLFO m_lfo[2];                  // +0xad70
    int m_delayTimeVal;             // +0xb030
    CSoundLibrary m_soundLib;       // +0xb038
    CSoundData m_initSound;         // +0xc3a0  initial / work sound
    CSoundData* m_pCurSound;        // +0xcf00  -> m_soundLib's current sound
    short m_curPitchBend;           // +0xcf08
    short m_targetPitchBend;        // +0xcf0a
    int m_bPitchBendChanging;       // +0xcf0c
    MidiCtrlAssign m_midiCtrl[2];   // +0xcf10
    float m_baseTempo;              // +0xcf30  (120)
    float m_curVolume;              // +0xcf34  smoothed master volume
    float m_volLPFCoef;             // +0xcf38
    int m_lastParamID;              // +0xcf3c
    int m_lastParamVal;             // +0xcf40
    int m_bMidiLearning;            // +0xcf44
    int m_midiLearnParamID;         // +0xcf48
    int _pad_cf4c;                  // +0xcf4c
};

// ---------------------------------------------------------------------------
/**
 * @brief One entry of the parameter table for MIDI assignment (g_midiAssignParams in
 *        synmidi.cpp): id, MIDI value range, default CC and names.
 */
struct MidiAssignParam {
    int id;                 // +0x00  patch parameter id
    int min;                // +0x04
    int max;                // +0x08
    int cc;                 // +0x0c  default CC (-1 none, -2 special)
    const char* name;       // +0x10
    int bAssignable;        // +0x18
    const char* shortName;  // +0x20
};
extern MidiAssignParam g_midiAssignParams[];
extern int g_midiAssignParamsNum;

// ---------------------------------------------------------------------------
/**
 * @brief Process-wide globals of Synth1, shared by all plugin instances.
 *
 * One instance, s1gArea in audiounit/Synth1.cpp; the pointer s1g is defined in
 * synth1.cpp. sizeof is about 0x169298 (s1gArea .. g_rnd). Holds the sound library
 * state, the MIDI tables and the frequency tables.
 */
struct S1G {
    /**
     * @brief Initialises the scalar fields (the tables are filled by their owners).
     */
    S1G(){
        m_zipFilesHash = 0;
        m_zipFolder[0] = 0;
        m_soundLibRefCount = 0;
        m_zipBankLock = 0;
        m_installFolder[0] = 0;
        m_zipCheckTime = 0;
        m_pZipBankMap = NULL;
        m_pZipRootBanks = NULL;
        m_bCCMapNotLoaded = 1;
        m_masterTune = 9999;
    }

    // sound library (sound.cpp)
    int m_zipFilesHash;                     // +0x000000
    char m_zipFolder[0x1000];               // +0x000004
    int m_soundLibRefCount;                 // +0x001004  CSoundLibrary instances
    int m_zipBankLock;                      // +0x001008
    int _pad_00100c;                        // +0x00100c
    CBank m_banks[BANK_FOLDER_NUM];         // +0x001010  (100 x 0x3940)
    std::vector<CBank*>* m_pZipRootBanks;   // +0x166d10
    BankMap* m_pZipBankMap;                 // +0x166d18
    LONGLONG m_zipCheckTime;                // +0x166d20
    CMyCS m_csZip;                          // +0x166d28
    CMyCS m_csLib;                          // +0x166d70
    CMyCS m_csZipLock;                      // +0x166db8
    char m_installFolder[0x400];            // +0x166e00
    int m_bCCMapNotLoaded;                  // +0x167200  1 until initCCMap() ran
    int m_ccMap[128];                       // +0x167204  CC no -> parameter id (-1 = none)
    int m_paramRange[256 * 2];              // +0x167404  parameter id -> min, max (-1 = not assignable)
    int _pad_167c04;                        // +0x167c04
    int m_midiCtrlSrcNum;                   // +0x167c08
    int* m_midiCtrlSrcVals;                 // +0x167c10
    const char** m_midiCtrlSrcText;         // +0x167c18
    int m_wheelAssignNum;                   // +0x167c20
    int* m_wheelAssignVals;                 // +0x167c28
    const char** m_wheelAssignText;         // +0x167c30
    float m_freqTable[128];                 // +0x167c38  note -> Hz (CVco::makeFreqTable)
    int m_masterTune;                       // +0x167e38  cent
    float m_baseFreq;                       // +0x167e3c  A4 / 2 (OSC2 without key tracking)
    float m_fineTable[257];                 // +0x167e40  2^((i-128)/128) (vco.cpp initVco)
    float m_octTable[21];                   // +0x168244  2^(i-10)
    float m_vcfFreqTable[1024];             // +0x168298  cutoff index -> Hz (initVcf)
};
extern S1G* s1g;
