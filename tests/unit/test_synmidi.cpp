/**
 * @file test_synmidi.cpp
 * @brief Unit tests for CSynthesizer1's MIDI handling (synmidi.cpp, plus the CInst overrides in
 *        synth1.cpp): note on/off reaching the voices, sustain, the CC map and MIDI learn,
 *        program change and bank select with their enable flags, pitch bend, the controller
 *        assignment slots (mod wheel, aftertouch, pitch bend as sources) and the parameter
 *        table helpers.
 *
 * The sound library is isolated from the user's folders with SynthEnv (test_synth1.cpp,
 * through t1s_newEnv()). The tests never call the functions that write the preferences (setCCMap(),
 * setEnableBankSelect(), setEnableProgramChange(), endMidiLearning() while learning); they
 * change the process-wide CC map and enable flags directly and restore them afterwards.
 */
#include "test.h"

#include <string.h>

#include "engine.h"
#include "stubs.h"
#include "synth1.h"

// helpers from test_synth1.cpp
void* t1s_newEnv();
void t1s_deleteEnv(void* p);

namespace {

/**
 * @brief CSynthesizer1 with accessors for the protected CInst members.
 */
struct TSynth : public CSynthesizer1 {
    short instBankNo() { return m_bankNo; }
    short pitchBendRange() { return m_pitchBendRange; }
};

/**
 * @brief Dummy non-NULL GUI window handle (the stubs only record it).
 */
void* const kWin = (void*)0x5678;

/**
 * @brief Isolates the sound library (SynthEnv) and saves the process-wide CC map and the CInst
 *        enable flags; restores everything on exit.
 */
struct GlobalsGuard {
    void* env;
    int ccMap[128];
    int bBank, bProg;
    GlobalsGuard()
    {
        t1::initEngine();
        env = t1s_newEnv();
        CSynthesizer1::initCCMap();
        memcpy(ccMap, s1g->m_ccMap, sizeof ccMap);
        bBank = CInst::s_bEnableBankSelect;
        bProg = CInst::s_bEnableProgChange;
    }
    ~GlobalsGuard()
    {
        memcpy(s1g->m_ccMap, ccMap, sizeof ccMap);
        CInst::s_bEnableBankSelect = bBank;
        CInst::s_bEnableProgChange = bProg;
        t1s_deleteEnv(env);
    }
};

/**
 * @brief Builds a synth at 44.1 kHz with an empty CC map.
 */
TSynth* newSynth()
{
    t1::initEngine();
    TSynth* s = new TSynth();
    s->setSamplingFreq(44100);
    for (int i = 0; i < 128; i++) s1g->m_ccMap[i] = -1;
    return s;
}

/**
 * @brief Renders one block (the voices are triggered inside processSignalW()).
 */
void renderBlock(CSynthesizer1* s)
{
    static float buf[512];
    int bProcessed;
    s->processSignalW(256, buf, &bProcessed);
}

/**
 * @brief Counts the voices holding note nn.
 */
int voicesOn(CSynthesizer1* s, int nn)
{
    int n = 0;
    for (int i = 0; i < 32; i++) {
        if (s->m_voice[i].m_bNoteOn && s->m_voice[i].m_noteNo == nn) n++;
    }
    return n;
}

/**
 * @brief Counts the voices holding any note.
 */
int voicesOnAll(CSynthesizer1* s)
{
    int n = 0;
    for (int i = 0; i < 32; i++) {
        if (s->m_voice[i].m_bNoteOn) n++;
    }
    return n;
}

}  // namespace

// ---------------------------------------------------------------------------
// notes

/** @brief Note on/off messages update the keyboard state and, at the next block, trigger and
 *         release voices; velocity 0 is a note off; the released voice keeps running its
 *         envelope (VOICE_RELEASING). */
TEST(midi, note_on_off_reach_voices)
{
    GlobalsGuard g;
    TSynth* s = newSynth();
    s->processMIDIShortData(0x90, 60, 100);
    CHECK_EQ(s->m_kbdInfo.getKeyOnNum(), 1);
    CHECK_EQ(s->m_kbdInfo.getVelocity(60), 100);
    CHECK_EQ(voicesOn(s, 60), 0);               // not yet triggered
    renderBlock(s);
    CHECK_EQ(voicesOn(s, 60), 1);
    int v = -1;
    for (int i = 0; i < 32; i++) if (s->m_voice[i].m_bNoteOn) v = i;
    REQUIRE(v >= 0);
    CHECK_EQ(s->m_voice[v].m_velocity, 100);
    CHECK_EQ(s->m_voice[v].getCurStatus(), VOICE_NOTEON);

    s->processMIDIShortData(0x93, 64, 80);      // any channel
    s->processMIDIShortData(0x90, 67, 1);
    renderBlock(s);
    CHECK_EQ(voicesOnAll(s), 3);
    CHECK_EQ(voicesOn(s, 64), 1);
    CHECK_EQ(voicesOn(s, 67), 1);

    s->processMIDIShortData(0x80, 60, 0);
    s->processMIDIShortData(0x90, 64, 0);       // velocity 0 = note off
    CHECK_EQ(s->m_kbdInfo.getKeyOnNum(), 1);
    renderBlock(s);
    CHECK_EQ(voicesOn(s, 60), 0);
    CHECK_EQ(voicesOn(s, 64), 0);
    CHECK_EQ(voicesOn(s, 67), 1);
    CHECK_EQ(s->m_voice[v].getCurStatus(), VOICE_RELEASING);

    s->processMIDIShortData(0x90, 200, 100);    // invalid note number: ignored
    CHECK_EQ(s->m_kbdInfo.getKeyOnNum(), 1);
    delete s;
}

/** @brief Polyphony limits the number of sounding voices; mono mode uses one voice. */
TEST(midi, polyphony_and_mono)
{
    GlobalsGuard g;
    TSynth* s = newSynth();
    s->setSoundParam(94, 2, 0);
    for (int n = 60; n < 64; n++) s->noteOn(n, 100);
    renderBlock(s);
    CHECK_EQ(voicesOnAll(s), 2);
    CHECK_EQ(voicesOn(s, 63), 1);               // the latest notes win
    CHECK_EQ(voicesOn(s, 62), 1);
    for (int n = 60; n < 64; n++) s->noteOff(n);
    renderBlock(s);
    CHECK_EQ(voicesOnAll(s), 0);

    s->setSoundParam(38, 1, 0);                 // mono
    s->noteOn(50, 100);
    s->noteOn(55, 100);
    renderBlock(s);
    CHECK_EQ(voicesOnAll(s), 1);
    CHECK_EQ(voicesOn(s, 55), 1);
    s->noteOff(55);
    renderBlock(s);
    CHECK_EQ(voicesOn(s, 50), 1);               // falls back to the held key
    delete s;
}

/** @brief The damper pedal (CC64 >= 64) holds released keys until it is lifted. */
TEST(midi, sustain_pedal)
{
    GlobalsGuard g;
    TSynth* s = newSynth();
    s->processMIDIShortData(0xB0, 64, 127);
    CHECK_EQ(s->m_kbdInfo.m_bDumper, 1);
    s->noteOn(60, 100);
    renderBlock(s);
    s->noteOff(60);
    renderBlock(s);
    CHECK_EQ(s->m_kbdInfo.getKeyOnNum(), 1);
    CHECK_EQ(voicesOn(s, 60), 1);
    s->processMIDIShortData(0xB0, 64, 63);      // < 64: pedal up
    CHECK_EQ(s->m_kbdInfo.m_bDumper, 0);
    CHECK_EQ(s->m_kbdInfo.getKeyOnNum(), 0);
    renderBlock(s);
    CHECK_EQ(voicesOn(s, 60), 0);
    delete s;
}

/** @brief All notes off (CC123) and all sound off (CC120) release every key, also lifting the
 *         pedal. */
TEST(midi, all_notes_off)
{
    GlobalsGuard g;
    TSynth* s = newSynth();
    s->setSustainEnable(1);
    s->noteOn(60, 100);
    s->noteOn(61, 100);
    s->processMIDIShortData(0xB0, 123, 0);
    CHECK_EQ(s->m_kbdInfo.getKeyOnNum(), 0);
    CHECK_EQ(s->m_kbdInfo.m_bDumper, 0);
    s->noteOn(62, 100);
    s->processMIDIShortData(0xB0, 120, 0);
    CHECK_EQ(s->m_kbdInfo.getKeyOnNum(), 0);
    delete s;
}

/** @brief The first held key restarts key-synced LFOs; further keys do not. */
TEST(midi, key_sync_restarts_lfo)
{
    GlobalsGuard g;
    TSynth* s = newSynth();
    s->setSoundParam(70, 1, 0);                 // lfo2 key sync
    CHECK_EQ(s->m_lfo[1].m_bKeySync, 1);
    CHECK_EQ(s->m_lfo[0].m_bKeySync, 0);
    s->m_lfo[0].m_curTime = 100;
    s->m_lfo[1].m_curTime = 100;
    s->noteOn(60, 100);
    CHECK_EQ(s->m_lfo[1].m_curTime, 0);
    CHECK_EQ(s->m_lfo[0].m_curTime, 100);
    s->m_lfo[1].m_curTime = 50;
    s->noteOn(61, 100);
    CHECK_EQ(s->m_lfo[1].m_curTime, 50);
    delete s;
}

/** @brief Arpeggiator note messages go to the separate arpeggiator keyboard state. */
TEST(midi, arp_keyboard)
{
    GlobalsGuard g;
    TSynth* s = newSynth();
    s->noteOnFromArp(48, 90);
    CHECK_EQ(s->m_kbdInfoArp.getVelocity(48), 90);
    CHECK_EQ(s->m_kbdInfo.getKeyOnNum(), 0);
    s->noteOffFromArp(48);
    CHECK_EQ(s->m_kbdInfoArp.getKeyOnNum(), 0);
    delete s;
}

// ---------------------------------------------------------------------------
// program change / bank select

/** @brief Program change loads the program (posting 0x721) when enabled, and is ignored when
 *         disabled. */
TEST(midi, program_change_respects_enable)
{
    GlobalsGuard g;
    TSynth* s = newSynth();
    s->setSynWin(kWin);
    s->enableProgramChange(1);
    CHECK_EQ(s->getEnableProgramChange(), 1);
    t1stub::reset();
    s->processMIDIShortData(0xC0, 5, 0);
    CHECK_EQ(s->getCurrentSound()->m_progNo, 5);
    CHECK_EQ(t1stub::lastMessage, 0x721u);
    s->enableProgramChange(0);
    CHECK_EQ(s->getEnableProgramChange(), 0);
    t1stub::reset();
    s->processMIDIShortData(0xC0, 9, 0);
    CHECK_EQ(s->getCurrentSound()->m_progNo, 5);
    CHECK_EQ(s->m_soundLib.m_reqProgNo, 5);
    CHECK_EQ(t1stub::postEventCalls, 0);
    delete s;
}

/** @brief Bank select (CC0 MSB, CC32 LSB) requests a bank when enabled, applied with the next
 *         program change; disabled, it is ignored. */
TEST(midi, bank_select_respects_enable)
{
    GlobalsGuard g;
    TSynth* s = newSynth();
    s->enableBankSelect(1);
    s->processMIDIShortData(0xB0, 32, 3);
    CHECK_EQ(s->instBankNo(), 3);
    CHECK_EQ(s->m_soundLib.m_reqBankNo, 3);
    CHECK_EQ(s->getCurrentSound()->m_bankNo, 0);   // not loaded yet
    s->processMIDIShortData(0xC0, 7, 0);
    CHECK_EQ(s->getCurrentSound()->m_bankNo, 3);
    CHECK_EQ(s->getCurrentSound()->m_progNo, 7);
    s->processMIDIShortData(0xB0, 0, 1);           // MSB: bank 0x103
    CHECK_EQ(s->instBankNo(), 0x103);
    CHECK_EQ(s->m_soundLib.m_reqBankNo, 0x103);
    s->processMIDIShortData(0xB0, 0, 0);
    s->processMIDIShortData(0xB0, 32, 1);
    CHECK_EQ(s->m_soundLib.m_reqBankNo, 1);

    s->enableBankSelect(0);
    CHECK_EQ(s->getEnableBankSelect(), 0);
    s->processMIDIShortData(0xB0, 32, 9);
    s->processMIDIShortData(0xB0, 0, 9);
    CHECK_EQ(s->instBankNo(), 1);
    CHECK_EQ(s->m_soundLib.m_reqBankNo, 1);
    delete s;
}

/** @brief The enable flags are shared by all instances (process-wide). */
TEST(midi, enable_flags_are_shared)
{
    GlobalsGuard g;
    TSynth* a = newSynth();
    TSynth* b = newSynth();
    a->enableBankSelect(0);
    CHECK_EQ(b->getEnableBankSelect(), 0);
    b->enableProgramChange(0);
    CHECK_EQ(a->getEnableProgramChange(), 0);
    delete a;
    delete b;
}

// ---------------------------------------------------------------------------
// pitch bend, volume

/** @brief A pitch bend message sets the glide target; rendering moves the voices' pitch
 *         offset towards it in steps (at most 32 frames per step) until it is reached. */
TEST(midi, pitch_bend_message)
{
    GlobalsGuard g;
    TSynth* s = newSynth();
    s->processMIDIShortData(0xE0, 0x7f, 0x7f);     // +8191
    CHECK_EQ(s->m_targetPitchBend, 63);
    CHECK_EQ(s->m_bPitchBendChanging, 1);
    s->noteOn(60, 100);
    float buf[512];
    int bProcessed;
    for (int i = 0; i < 10 && s->m_bPitchBendChanging; i++) s->processSignalW(256, buf, &bProcessed);
    CHECK_EQ(s->m_curPitchBend, 63);
    CHECK_EQ(s->m_bPitchBendChanging, 0);
    CHECK_EQ(s->m_voice[0].m_vco.m_pitchEnvLevel, 128);
    s->processMIDIShortData(0xE0, 0x00, 0x40);     // centre
    CHECK_EQ(s->m_targetPitchBend, 0);
    s->processMIDIShortData(0xE0, 0x00, 0x00);     // -8192 -> -63
    CHECK_EQ(s->m_targetPitchBend, -63);
    delete s;
}

/** @brief While a pitch bend glides and the synth is silent, processSignalW() stops after the
 *         first 32-frame segment and returns 32 instead of the block size. */
TEST(midi, pitch_bend_glide_silent_block_is_short)
{
    GlobalsGuard g;
    TSynth* s = newSynth();
    float buf[512];
    int bProcessed = 0;
    for (int i = 0; i < 2000 && !bProcessed; i++) s->processSignalW(256, buf, &bProcessed);
    REQUIRE(bProcessed);
    s->setPitchBend(8191);
    CHECK_EQ(s->processSignalW(256, buf, &bProcessed), 32);
    CHECK(bProcessed != 0);
    CHECK_EQ(s->m_curPitchBend, 1);
    delete s;
}

/** @brief CC7 sets the master volume and notifies the GUI (0x720, 254, value). */
TEST(midi, volume_cc)
{
    GlobalsGuard g;
    TSynth* s = newSynth();
    s->setSynWin(kWin);
    t1stub::reset();
    s->processMIDIShortData(0xB0, 7, 100);
    CHECK_EQ(s->m_volumeVal, 100);
    CHECK_EQ(t1stub::lastMessage, 0x720u);
    CHECK_EQ(t1stub::lastWParam, 254);
    CHECK_EQ(t1stub::lastLParam, 100);
    delete s;
}

// ---------------------------------------------------------------------------
// controller assignment slots

/** @brief The mod wheel (CC1, the default source of both slots) adds value * range * sens to
 *         the assigned parameters (lfo1 depth and speed in the initial sound) without changing
 *         the patch. */
TEST(midi, mod_wheel_slots)
{
    GlobalsGuard g;
    TSynth* s = newSynth();
    CHECK_EQ(s->m_lfo[0].m_amountDial, 0);
    CHECK_EQ(s->m_lfo[0].m_speedDial, 64);
    s->processMIDIShortData(0xB0, 1, 127);
    CHECK_CLOSE(s->m_midiCtrl[0].val, 1.0, 0);
    CHECK_CLOSE(s->m_midiCtrl[1].val, 1.0, 0);
    // 128 * 10/63 = 20.3
    CHECK_EQ(s->m_lfo[0].m_amountDial, 20);
    CHECK_EQ(s->m_lfo[0].m_speedDial, 84);
    CHECK_EQ(s->getCurrentSound()->getParamVal(44), 0);
    CHECK_EQ(s->getCurrentSound()->getParamVal(43), 64);
    s->processMIDIShortData(0xB1, 1, 0);           // any channel
    CHECK_EQ(s->m_lfo[0].m_amountDial, 0);
    CHECK_EQ(s->m_lfo[0].m_speedDial, 64);
    s->processMIDIShortData(0xB0, 2, 127);         // other CC: no effect
    CHECK_CLOSE(s->m_midiCtrl[0].val, 0.0, 0);
    delete s;
}

/** @brief Sensitivity: setMidiCtrlSens() and parameters 50/51 ((v - 63 or 64) / 63) scale the
 *         offset; negative sensitivities subtract; the result is clamped to the range. */
TEST(midi, ctrl_sensitivity)
{
    GlobalsGuard g;
    TSynth* s = newSynth();
    s->processMIDIShortData(0xB0, 1, 127);
    s->setMidiCtrlSens(0, 0.5f);
    CHECK_EQ(s->m_lfo[0].m_amountDial, 64);
    s->setSoundParam(50, 127, 0);                  // +1
    CHECK_CLOSE(s->m_midiCtrl[0].sens, 1.0, 1e-6);
    CHECK_EQ(s->m_lfo[0].m_amountDial, 127);       // 0 + 128 clamped
    s->setSoundParam(50, 0, 0);                    // -1
    CHECK_CLOSE(s->m_midiCtrl[0].sens, -1.0, 1e-6);
    CHECK_EQ(s->m_lfo[0].m_amountDial, 0);
    s->setSoundParam(51, 0, 0);
    CHECK_EQ(s->m_lfo[0].m_speedDial, 0);          // 64 - 128 clamped
    s->setSoundParam(51, 63, 0);
    CHECK_CLOSE(s->m_midiCtrl[1].sens, 0.0, 0);
    s->setSoundParam(51, 64, 0);
    CHECK_CLOSE(s->m_midiCtrl[1].sens, 0.0, 0);
    CHECK_EQ(s->m_lfo[0].m_speedDial, 64);
    delete s;
}

/** @brief Reassigning a slot restores the previous parameter without the offset and applies
 *         the offset to the new one; unknown sources and non-assignable ids are ignored. */
TEST(midi, ctrl_assign_and_source)
{
    GlobalsGuard g;
    TSynth* s = newSynth();
    s->processMIDIShortData(0xB0, 1, 127);
    CHECK_EQ(s->m_lfo[0].m_amountDial, 20);
    s->setMidiCtrlAssignParamID(0, 45);            // osc1 FM
    CHECK_EQ(s->m_midiCtrl[0].paramID, 45);
    CHECK_EQ(s->m_lfo[0].m_amountDial, 0);
    CHECK_EQ(s->m_voice[0].m_vco.m_fmAmountVal, 20);
    s->setMidiCtrlAssignParamID(0, 0);             // osc1 shape: not assignable, ignored
    CHECK_EQ(s->m_midiCtrl[0].paramID, 45);
    s->setMidiCtrlAssignParamID(0, -1);
    CHECK_EQ(s->m_midiCtrl[0].paramID, -1);
    CHECK_EQ(s->m_voice[0].m_vco.m_fmAmountVal, 0);
    s->setSoundParam(87, 44, 0);                   // via the parameter
    CHECK_EQ(s->m_midiCtrl[0].paramID, 44);
    CHECK_EQ(s->m_lfo[0].m_amountDial, 20);

    s->setMidiCtrlSrc(0, 0xb0ff);                  // not a source: ignored
    CHECK_EQ(s->m_midiCtrl[0].src, 0xb001);
    s->setMidiCtrlSrc(0, 0xb002);                  // new source resets the value
    CHECK_EQ(s->m_midiCtrl[0].src, 0xb002);
    CHECK_CLOSE(s->m_midiCtrl[0].val, 0.0, 0);
    CHECK_EQ(s->m_lfo[0].m_amountDial, 0);
    s->processMIDIShortData(0xB0, 2, 127);
    CHECK_EQ(s->m_lfo[0].m_amountDial, 20);
    delete s;
}

/** @brief Channel aftertouch (0xd000) and pitch bend (0xe000) work as slot sources; pitch bend
 *         is bipolar. */
TEST(midi, aftertouch_and_pitch_bend_sources)
{
    GlobalsGuard g;
    TSynth* s = newSynth();
    s->setSoundParam(86, 0xd000, 0);
    s->setSoundParam(88, 0xe000, 0);
    CHECK_EQ(s->m_midiCtrl[0].src, 0xd000);
    CHECK_EQ(s->m_midiCtrl[1].src, 0xe000);
    s->processMIDIShortData(0xD0, 127, 0);
    CHECK_CLOSE(s->m_midiCtrl[0].val, 1.0, 0);
    CHECK_EQ(s->m_lfo[0].m_amountDial, 20);
    s->processMIDIShortData(0xB0, 1, 0);           // CC1 no longer drives the slots
    CHECK_CLOSE(s->m_midiCtrl[0].val, 1.0, 0);
    s->processMIDIShortData(0xE0, 0x7f, 0x7f);
    CHECK_CLOSE(s->m_midiCtrl[1].val, 8191 / 8192.0, 1e-6);
    CHECK_EQ(s->m_lfo[0].m_speedDial, 84);
    s->processMIDIShortData(0xE0, 0, 0);
    CHECK_CLOSE(s->m_midiCtrl[1].val, -1.0, 0);
    CHECK_EQ(s->m_lfo[0].m_speedDial, 44);         // 64 - 20.3
    s->processMIDIShortData(0xE0, 0, 0x40);
    CHECK_CLOSE(s->m_midiCtrl[1].val, 0.0, 0);
    CHECK_EQ(s->m_lfo[0].m_speedDial, 64);
    delete s;
}

/** @brief An unassigned slot (parameter -1) ignores its source; a slot whose source kind is not
 *         CC, aftertouch or pitch bend is skipped even when the message kind matches. */
TEST(midi, unassigned_and_unknown_kind_slots)
{
    GlobalsGuard g;
    TSynth* s = newSynth();
    s->setMidiCtrlAssignParamID(0, -1);
    REQUIRE(s->m_midiCtrl[0].paramID == -1);
    s->processMIDIShortData(0xB0, 1, 127);
    CHECK_CLOSE(s->m_midiCtrl[0].val, 0.0, 0);     // unassigned: not updated
    CHECK_CLOSE(s->m_midiCtrl[1].val, 1.0, 0);     // slot 2 still follows CC1
    CHECK_EQ(s->m_lfo[0].m_speedDial, 84);
    CHECK_EQ(s->m_lfo[0].m_amountDial, 0);

    // a note-on kind source (never offered by setMidiCtrlSrc): matched kind, but no value
    s->m_midiCtrl[1].src = 0x9000;
    s->processMIDIShortDataIndividual(0x90, 60, 100);
    CHECK_CLOSE(s->m_midiCtrl[1].val, 1.0, 0);
    CHECK_EQ(s->m_lfo[0].m_speedDial, 84);
    delete s;
}

// ---------------------------------------------------------------------------
// CC map and MIDI learn

/** @brief A mapped CC (< 120) sets its parameter (wrapped into the parameter's range), stores
 *         it in the patch and notifies the GUI; unmapped CCs and CCs >= 120 do nothing. */
TEST(midi, cc_map_sets_param)
{
    GlobalsGuard g;
    TSynth* s = newSynth();
    s->setSynWin(kWin);
    s1g->m_ccMap[74] = 19;
    s1g->m_ccMap[20] = 9;
    s1g->m_ccMap[21] = 0;
    t1stub::reset();
    s->processMIDIShortData(0xB0, 74, 100);
    CHECK_EQ(s->getCurrentSound()->getParamVal(19), 100);
    CHECK_EQ(t1stub::lastMessage, 0x720u);
    CHECK_EQ(t1stub::lastWParam, 19);
    CHECK_EQ(t1stub::lastLParam, 100);
    s->processMIDIShortData(0xB0, 20, 0);
    CHECK_EQ(s->getCurrentSound()->getParamVal(9), -24);
    s->processMIDIShortData(0xB0, 20, 127);        // 127 % 49 - 24
    CHECK_EQ(s->getCurrentSound()->getParamVal(9), 5);
    s->processMIDIShortData(0xB0, 21, 6);          // 6 % 4
    CHECK_EQ(s->getCurrentSound()->getParamVal(0), 2);
    CHECK_EQ(s->m_voice[0].m_vco.m_shape, 2);
    int calls = t1stub::postEventCalls;
    s->processMIDIShortData(0xB0, 75, 10);         // unmapped
    s1g->m_ccMap[121] = 19;
    s->processMIDIShortDataIndividual(0xB0, 121, 5);   // >= 120: not mapped
    CHECK_EQ(s->getCurrentSound()->getParamVal(19), 100);
    s1g->m_ccMap[30] = 86;                         // no MIDI range: ignored
    s->processMIDIShortData(0xB0, 30, 5);
    CHECK_EQ(s->getCurrentSound()->getParamVal(86), 0xb001);
    CHECK_EQ(t1stub::postEventCalls, calls);
    delete s;
}

/** @brief MIDI learn maps the next CC to the selected parameter (removing its previous CC) and
 *         applies the value; with no parameter selected the CC is ignored. */
TEST(midi, midi_learn)
{
    GlobalsGuard g;
    TSynth* s = newSynth();
    t1stub::reset();
    CHECK_EQ(s->startMidiLearning(), 1);
    CHECK_EQ(t1stub::messageBoxCalls, 0);          // no window: no message box
    CHECK_EQ(s->startMidiLearning(), 0);
    CHECK_EQ(s->m_bMidiLearning, 1);
    CHECK_EQ(s->m_midiLearnParamID, -1);

    s->processMIDIShortData(0xB0, 40, 99);         // nothing selected
    CHECK_EQ(s1g->m_ccMap[40], -1);

    s1g->m_ccMap[10] = 20;
    s->m_midiLearnParamID = 20;
    s->processMIDIShortData(0xB0, 41, 64);
    CHECK_EQ(s1g->m_ccMap[41], 20);
    CHECK_EQ(s1g->m_ccMap[10], -1);
    CHECK_EQ(s->getCurrentSound()->getParamVal(20), 64);
    s->processMIDIShortData(0xB0, 42, 70);         // still learning: moves again
    CHECK_EQ(s1g->m_ccMap[42], 20);
    CHECK_EQ(s1g->m_ccMap[41], -1);
    CHECK_EQ(s->getCurrentSound()->getParamVal(20), 70);

    // leave learn mode without endMidiLearning(), which writes the preferences
    s->m_bMidiLearning = 0;
    s->m_midiLearnParamID = -1;
    s->processMIDIShortData(0xB0, 42, 3);
    CHECK_EQ(s->getCurrentSound()->getParamVal(20), 3);
    CHECK_EQ(s->endMidiLearning(), 0);             // not learning: nothing happens

    s->setSynWin(kWin);
    CHECK_EQ(s->startMidiLearning(), 1);
    CHECK_EQ(t1stub::messageBoxCalls, 1);          // with a window: message box
    s->m_bMidiLearning = 0;
    delete s;
}

/** @brief getDefaultCcMap(): no parameter has a default CC, so the default map is empty. */
TEST(midi, default_cc_map)
{
    int map[128];
    for (int i = 0; i < 128; i++) map[i] = 5;
    CSynthesizer1::getDefaultCcMap(map);
    for (int i = 0; i < 128; i++) CHECK_EQ(map[i], -1);
}

/** @brief The CC map CSV round-trips; entries outside 0..255 read as -1; a short list fails
 *         with a message box and leaves the rest of the map at -1. */
TEST(midi, cc_map_csv)
{
    int map[128], back[128];
    for (int i = 0; i < 128; i++) map[i] = (i % 3 == 0) ? -1 : i * 2;
    char str[0x400];
    CSynthesizer1::createCSVStringFromCCMap(map, str);
    CHECK(strncmp(str, "-1,2,4,-1,8,", 12) == 0);
    CHECK(str[strlen(str) - 1] != ',');
    CHECK_EQ(CSynthesizer1::getCCMapFromCSV(back, str), 1);
    for (int i = 0; i < 128; i++) CHECK_EQ(back[i], map[i]);

    char s2[0x400];
    strcpy(s2, "1 2\t3\n300,");
    for (int i = 4; i < 128; i++) strcat(s2, "7,");
    t1stub::reset();
    CHECK_EQ(CSynthesizer1::getCCMapFromCSV(back, s2), 1);
    CHECK_EQ(back[0], 1);
    CHECK_EQ(back[2], 3);
    CHECK_EQ(back[3], -1);
    CHECK_EQ(back[127], 7);
    CHECK_EQ(t1stub::messageBoxCalls, 0);

    char s3[64];
    strcpy(s3, "1,2,3");
    CHECK_EQ(CSynthesizer1::getCCMapFromCSV(back, s3), 0);
    CHECK_EQ(t1stub::messageBoxCalls, 1);
    CHECK_EQ(back[2], 3);
    CHECK_EQ(back[3], -1);
}

/** @brief checkSoundParam() wraps a controller value into the parameter's range; parameters
 *         without a range are rejected. */
TEST(midi, check_sound_param)
{
    GlobalsGuard g;
    int v = 1234;
    CHECK_EQ(CSynthesizer1::checkSoundParam(19, 100, &v), 1);
    CHECK_EQ(v, 100);
    CHECK_EQ(CSynthesizer1::checkSoundParam(9, 0, &v), 1);
    CHECK_EQ(v, -24);
    CHECK_EQ(CSynthesizer1::checkSoundParam(1, 4, &v), 1);     // 1 + 4 % 4
    CHECK_EQ(v, 1);
    CHECK_EQ(CSynthesizer1::checkSoundParam(93, 10, &v), 1);   // 2 + 10 % 7
    CHECK_EQ(v, 5);
    v = 1234;
    CHECK_EQ(CSynthesizer1::checkSoundParam(86, 5, &v), 0);
    CHECK_EQ(CSynthesizer1::checkSoundParam(94, 5, &v), 0);
    CHECK_EQ(CSynthesizer1::checkSoundParam(150, 5, &v), 0);
    CHECK_EQ(v, 1234);
}

/** @brief The parameter table helpers: ranges, CC assignability, names, the controller source
 *         list and the wheel assignment list built by initCCMap(). */
TEST(midi, param_table_helpers)
{
    GlobalsGuard g;
    int mn, mx;
    CSynthesizer1::getParamValRange(0, &mn, &mx);
    CHECK_EQ(mn, 0);
    CHECK_EQ(mx, 3);
    CSynthesizer1::getParamValRange(9, &mn, &mx);
    CHECK_EQ(mn, -24);
    CHECK_EQ(mx, 24);
    CSynthesizer1::getParamValRange(86, &mn, &mx);
    CHECK_EQ(mn, -1);
    CHECK_EQ(mx, -1);
    CSynthesizer1::getParamValRange(200, &mn, &mx);
    CHECK_EQ(mn, -1);
    CHECK_EQ(mx, -1);

    CHECK_EQ(CSynthesizer1::isMidiCCAssignable(19), 1);
    CHECK_EQ(CSynthesizer1::isMidiCCAssignable(86), 0);
    CHECK_EQ(CSynthesizer1::isMidiCCAssignable(89), 0);
    CHECK_EQ(CSynthesizer1::isMidiCCAssignable(94), 0);       // not in the table
    char name[64] = "unchanged";
    CHECK_EQ(CSynthesizer1::getParamName(150, name), 0);
    CHECK_STREQ(name, "unchanged");
    CHECK_EQ(CSynthesizer1::getParamName(19, name), 1);
    CHECK_STREQ(name, "*filter freq");
    CHECK_EQ(CSynthesizer1::getParamName(40, name), 1);
    CHECK_STREQ(name, "pitch bend range");

    CHECK_EQ(g_midiAssignParamsNum, 98);
    CHECK_EQ(s1g->m_midiCtrlSrcNum, 13);
    CHECK_EQ(s1g->m_midiCtrlSrcVals[0], 0xb001);
    CHECK_EQ(s1g->m_midiCtrlSrcVals[11], 0xd000);
    CHECK_EQ(s1g->m_midiCtrlSrcVals[12], 0xe000);
    CHECK_STREQ(s1g->m_midiCtrlSrcText[0], "#1:mod wheel");
    int assignable = 0;
    for (int i = 0; i < g_midiAssignParamsNum; i++) if (g_midiAssignParams[i].bAssignable) assignable++;
    CHECK_EQ(s1g->m_wheelAssignNum, assignable + 1);
    CHECK_EQ(s1g->m_wheelAssignVals[0], -1);
    CHECK_STREQ(s1g->m_wheelAssignText[0], "---");
    CHECK_EQ(s1g->m_wheelAssignVals[1], 76);
    CHECK_STREQ(s1g->m_wheelAssignText[1], "osc1 det");          // short name
    CHECK_EQ(s1g->m_wheelAssignVals[2], 45);
    CHECK_STREQ(s1g->m_wheelAssignText[2], "osc1 FM");           // full name
    // initCCMap() runs only once
    s1g->m_wheelAssignNum = 0;
    CSynthesizer1::initCCMap();
    CHECK_EQ(s1g->m_wheelAssignNum, 0);
    s1g->m_wheelAssignNum = assignable + 1;
}
