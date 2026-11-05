/**
 * @file test_synth1.cpp
 * @brief Unit tests for techpop/synth1.cpp: CSynthesizer1 construction, sampling rate
 *        propagation, parameter handling, block rendering (processSignalW), the master volume,
 *        the GUI notifications posted through the Cocoa stubs, and the VST chunk / DXi state
 *        save and restore.
 *
 * Every test that builds a synth first sets up a SynthEnv: the shared sound library state in s1g
 * is isolated with soundtest::LibState (so the first-instance CSoundLibrary setup, which reads the
 * preferences and creates folders in ~/Documents, never runs) and banks 0..7 and the zip
 * folder point into a temporary folder; bank 0 is an empty folder. The loaded program is
 * therefore always the built-in initial sound, whatever the user's patch folders hold.
 */
#include "test.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

#include "common.h"
#include "engine.h"
#include "stubs.h"
#include "synth1.h"
#include "sound_fixture.h"

namespace {

/**
 * @brief Isolated sound library for one test: soundtest::LibState plus banks 0..7 and the zip folder
 *        in a temporary folder (bank 0 exists and is empty, the others do not exist).
 */
typedef soundtest::IsolatedLibrary SynthEnv;

/**
 * @brief CSynthesizer1 with accessors for the protected CInst members.
 */
struct TSynth : public CSynthesizer1 {
    short pitchBendRange() { return m_pitchBendRange; }
};

/**
 * @brief Reads the protected CEq::m_bEnable through a member pointer taken in a derived class.
 */
struct EqPeek : public CEq {
    static int enabled(CEq* eq) { int CEq::* p = &EqPeek::m_bEnable; return eq->*p; }
};

/**
 * @brief Sets up the globals and builds a synth running at 44.1 kHz with no GUI window.
 */
TSynth* newSynth()
{
    t1::initEngine();
    TSynth* s = new TSynth();
    s->setSamplingFreq(44100);
    return s;
}

/**
 * @brief Dummy non-NULL GUI window handle (the stubs only record it).
 */
void* const kWin = (void*)0x1234;

/**
 * @brief Master volume gain for a 0..127 value, as documented for setCommonParam(254).
 */
double volumeGain(int val)
{
    double v = val / 127.0f;
    if (v == 0) return 0;
    double x = v + 0.01;
    return pow(10.0, (x * 20.0 - 1.0 / x - 10.0) / 20.0);
}

/**
 * @brief Renders frames stereo frames into out (zeros for silent blocks), in blocks of 256.
 *
 * @return Number of blocks reported as silent.
 */
int render(CSynthesizer1* s, float* out, int frames)
{
    int silent = 0;
    while (frames > 0) {
        int n = frames < 256 ? frames : 256;
        int bProcessed = 0;
        int done = s->processSignalW(n, out, &bProcessed);
        if (done <= 0) break;
        if (bProcessed) {
            silent++;
            memset(out, 0, sizeof(float) * 2 * done);
        }
        out += done * 2;
        frames -= done;
    }
    return silent;
}

/**
 * @brief Renders blocks until the synth reports a silent block.
 *
 * @return True if silence was reached within maxBlocks blocks of 256 frames.
 */
bool renderUntilSilent(CSynthesizer1* s, int maxBlocks)
{
    static float buf[512];
    for (int i = 0; i < maxBlocks; i++) {
        int bProcessed = 0;
        s->processSignalW(256, buf, &bProcessed);
        if (bProcessed) return true;
    }
    return false;
}

/**
 * @brief Seeds the random generators used for the free-running oscillator phases (g_rnd) and
 *        by the LFO / arpeggiator (rand()), so that renders are reproducible.
 */
void seedRandom()
{
    g_rnd.m_rand.setSeed(12345);
    srand(12345);
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

}  // namespace

/**
 * @brief Creates a SynthEnv (isolated sound library); must exist while synths are alive.
 */
void* t1s_newEnv()
{
    return new SynthEnv();
}

/**
 * @brief Deletes a SynthEnv created with t1s_newEnv().
 */
void t1s_deleteEnv(void* p)
{
    delete (SynthEnv*)p;
}

/**
 * @brief Creates a synth for the C interface tests (test_synth1if.cpp cannot include
 *        synth1.h because synth1if.h redeclares g_midiAssignParams).
 */
void* t1s_newSynth()
{
    return newSynth();
}

/**
 * @brief Deletes a synth created with t1s_newSynth().
 */
void t1s_deleteSynth(void* p)
{
    delete (CSynthesizer1*)p;
}

/**
 * @brief Returns the current patch of a synth (CSoundData*).
 */
void* t1s_currentSound(void* p)
{
    return ((CSynthesizer1*)p)->m_pCurSound;
}

/**
 * @brief Returns s1g->m_ccMap.
 */
int* t1s_ccMap()
{
    return s1g->m_ccMap;
}

/**
 * @brief Returns s1g->m_zipFolder.
 */
char* t1s_zipFolder()
{
    return s1g->m_zipFolder;
}

/**
 * @brief Returns s1g->m_masterTune.
 */
int t1s_masterTune()
{
    return s1g->m_masterTune;
}

/**
 * @brief Returns s1g->m_freqTable[nn].
 */
float t1s_freq(int nn)
{
    return s1g->m_freqTable[nn];
}

/**
 * @brief Sets the bank number of the synth's current patch (for the C interface tests).
 */
void t1s_setCurrentBankNo(void* p, int bankNo)
{
    ((CSynthesizer1*)p)->m_pCurSound->m_bankNo = bankNo;
}

// ---------------------------------------------------------------------------
// construction

/** @brief The constructor sets the documented defaults: 120 BPM, volume 64, no window,
 *         smoothed volume 0, MIDI learn off, both CInst enable flags as read from the ini. */
TEST(synth1, constructor_defaults)
{
    SynthEnv env;
    TSynth* s = newSynth();
    CHECK(s->m_pSynWin == 0);
    CHECK_CLOSE(s->getBaseTempo(), 120.0, 0);
    CHECK_EQ(s->m_volumeVal, 64);
    CHECK_CLOSE(s->m_volume, volumeGain(64), 1e-5);
    CHECK_CLOSE(s->m_curVolume, 0.0, 0);
    CHECK_EQ(s->m_bMidiLearning, 0);
    CHECK_EQ(s->m_midiLearnParamID, -1);
    CHECK_EQ(s->m_bPitchBendChanging, 0);
    CHECK_EQ(s->m_curPitchBend, 0);
    CHECK(s->getCurrentSound() == s->m_pCurSound);
    CHECK(s->m_pCurSound == &s->m_soundLib.m_sound);
    CHECK_EQ(s->m_samplingFreq, 44100);
    // smoothing: half of the distance every 2 ms
    CHECK_CLOSE(s->m_volLPFCoef, 1.0 - pow(0.5, 1.0 / (44100 * 0.002f)), 1e-7);
    delete s;
}

/** @brief The constructor builds m_initSound with all 99 sound parameters, version 113 and
 *         colour 0xffffffff, and a few known default values. */
TEST(synth1, initial_sound)
{
    SynthEnv env;
    TSynth* s = newSynth();
    CSoundData* init = &s->m_initSound;
    CHECK_STREQ(init->m_name, "initial sound");
    CHECK_EQ(init->m_version, 113);
    CHECK_EQ(init->m_color, 0xffffffffu);
    CHECK_EQ(init->getParamsNum(), 99);
    for (int id = 0; id < 99; id++) {
        bool found = false;
        for (int i = 0; i < init->getParamsNum(); i++) {
            int pid, val;
            init->enumParam(i, &pid, &val);
            if (pid == id) found = true;
        }
        CHECK(found);
    }
    CHECK_EQ(init->getParamVal(0), 2);
    CHECK_EQ(init->getParamVal(19), 81);
    CHECK_EQ(init->getParamVal(21), 128);   // out of the 0..127 range; clamped by the engine
    CHECK_EQ(init->getParamVal(37), 20);
    CHECK_EQ(init->getParamVal(40), 12);
    CHECK_EQ(init->getParamVal(86), 0xb001);
    CHECK_EQ(init->getParamVal(87), 44);
    CHECK_EQ(init->getParamVal(88), 0xb001);
    CHECK_EQ(init->getParamVal(89), 43);
    CHECK_EQ(init->getParamVal(94), 16);
    delete s;
}

/** @brief The first program (empty bank 0) is the initial sound applied to the engine: the
 *         current patch holds the same values, and the engine state reflects them. */
TEST(synth1, constructor_loads_initial_program)
{
    SynthEnv env;
    TSynth* s = newSynth();
    CSoundData* cur = s->getCurrentSound();
    CHECK_EQ(cur->m_bankNo, 0);
    CHECK_EQ(cur->m_progNo, 0);
    CHECK_EQ(cur->m_version, 113);
    for (int id = 0; id < 99; id++) {
        CHECK_EQ(cur->getParamVal(id), s->m_initSound.getParamVal(id));
    }
    CHECK_EQ(s->pitchBendRange(), 12);
    CHECK_EQ(s->m_voice[0].m_vco.m_shape, 2);
    CHECK_EQ(s->m_voice[31].m_vco.m_shape, 2);
    CHECK_EQ(s->m_voiceCtl.m_polyNum, 16);
    CHECK_EQ(s->m_midiCtrl[0].src, 0xb001);
    CHECK_EQ(s->m_midiCtrl[0].paramID, 44);
    CHECK_EQ(s->m_midiCtrl[1].src, 0xb001);
    CHECK_EQ(s->m_midiCtrl[1].paramID, 43);
    CHECK_CLOSE(s->m_midiCtrl[0].sens, 10 / 63.0, 1e-6);   // param 50 = 74
    CHECK_CLOSE(s->m_midiCtrl[1].sens, 10 / 63.0, 1e-6);   // param 51 = 74
    CHECK_EQ(s->m_lfo[0].m_bEnable, 1);                    // param 57 = 1
    CHECK_EQ(s->m_lfo[1].m_bEnable, 1);                    // param 58 = 1
    CHECK_EQ(s->m_delay.m_bEnable, 1);                     // param 65 = 1
    CHECK_EQ(EqPeek::enabled(&s->m_delayEq), 0);                   // param 98 = 64: tone filter off
    for (int i = 0; i < 32; i++) CHECK_EQ(s->m_voice[i].m_bNoteOn, 0);
    delete s;
}

/** @brief Two instances can coexist and be destroyed in any order (shared sound library). */
TEST(synth1, two_instances)
{
    SynthEnv env;
    TSynth* a = newSynth();
    TSynth* b = newSynth();
    a->setSoundParam(0, 1, 0);
    CHECK_EQ(a->getCurrentSound()->getParamVal(0), 1);
    CHECK_EQ(b->getCurrentSound()->getParamVal(0), 2);
    delete a;
    b->setSoundParam(0, 3, 0);
    CHECK_EQ(b->getCurrentSound()->getParamVal(0), 3);
    delete b;
    TSynth* c = newSynth();
    CHECK_EQ(c->getCurrentSound()->getParamVal(0), 2);
    delete c;
}

// ---------------------------------------------------------------------------
// sampling rate

/** @brief setSamplingFreq() propagates the rate to the chain, the LFOs and every voice, and
 *         recomputes the volume smoothing coefficient. */
TEST(synth1, sampling_freq_propagates)
{
    SynthEnv env;
    TSynth* s = newSynth();
    s->setSamplingFreq(48000);
    CHECK_EQ(s->m_samplingFreq, 48000);
    CHECK_EQ(s->m_mixer.m_samplingFreq, 48000);
    CHECK_EQ(s->m_eq.m_samplingFreq, 48000);
    CHECK_EQ(s->m_toneEq.m_samplingFreq, 48000);
    CHECK_EQ(s->m_effector.m_samplingFreq, 48000);
    CHECK_EQ(s->m_delay.m_samplingFreq, 48000);
    CHECK_EQ(s->m_delayEq.m_samplingFreq, 48000);
    CHECK_EQ(s->m_chorus.m_samplingFreq, 48000);
    CHECK_EQ(s->m_lfo[0].m_samplingFreq, 48000);
    CHECK_EQ(s->m_lfo[1].m_samplingFreq, 48000);
    for (int i = 0; i < 32; i++) {
        CHECK_EQ(s->m_voice[i].m_vco.m_samplingFreq, 48000);
        CHECK_EQ(s->m_voice[i].m_vcf.m_samplingFreq, 48000);
        CHECK_EQ(s->m_voice[i].m_vca.m_samplingFreq, 48000);
    }
    CHECK_CLOSE(s->m_volLPFCoef, 1.0 - pow(0.5, 1.0 / (48000 * 0.002f)), 1e-7);
    float c48 = s->m_volLPFCoef;
    s->setSamplingFreq(96000);
    CHECK(s->m_volLPFCoef < c48);
    delete s;
}

// ---------------------------------------------------------------------------
// parameter handling

/** @brief setSoundParam() stores the value in the current patch and applies it to the engine. */
TEST(synth1, set_sound_param_stores_and_applies)
{
    SynthEnv env;
    TSynth* s = newSynth();
    s->setSoundParam(0, 3, 0);
    CHECK_EQ(s->getCurrentSound()->getParamVal(0), 3);
    for (int i = 0; i < 32; i++) CHECK_EQ(s->m_voice[i].m_vco.m_shape, 3);
    s->setSoundParam(45, 99, 0);
    CHECK_EQ(s->m_voice[7].m_vco.m_fmAmountVal, 99);
    s->setSoundParam(43, 100, 0);
    CHECK_EQ(s->m_lfo[0].m_speedDial, 100);
    s->setSoundParam(48, 7, 0);
    CHECK_EQ(s->m_lfo[1].m_speedDial, 7);
    s->setSoundParam(97, 1, 0);
    CHECK_EQ(s->m_voice[3].m_vco.m_subOct, 1);
    s->setSoundParam(96, 2, 0);
    CHECK_EQ(s->m_voice[3].m_vco.m_subWaveForm, 2);
    s->setSoundParam(40, 2, 0);
    CHECK_EQ(s->pitchBendRange(), 2);
    s->setSoundParam(94, 4, 0);
    CHECK_EQ(s->m_voiceCtl.m_polyNum, 4);
    s->setSoundParam(38, 1, 0);
    CHECK_EQ(s->m_voiceCtl.m_polyMode, 1);
    s->setSoundParam(93, 5, 0);
    CHECK_EQ(s->m_voiceCtl.m_unisonVoiceNum, 5);
    s->setSoundParam(73, 1, 0);
    CHECK_EQ(s->m_voiceCtl.m_bUnison, 1);
    s->setSoundParam(65, 0, 0);
    CHECK_EQ(s->m_delay.m_bEnable, 0);
    s->setSoundParam(36, 127, 0);
    CHECK_CLOSE(s->m_delay.m_feedback, 0.999, 1e-6);     // clamped by CDelay
    s->setSoundParam(36, 64, 0);
    CHECK_CLOSE(s->m_delay.m_feedback, 64 / 127.0, 1e-6);
    s->setSoundParam(57, 0, 0);
    CHECK_EQ(s->m_lfo[0].m_bEnable, 0);
    s->setSoundParam(57, 2, 0);                         // clamped to the range 0..1: enables
    CHECK_EQ(s->m_lfo[0].m_bEnable, 1);
    s->setLFOEnable(0, 2);                              // setLFOEnable(): only 1 enables
    CHECK_EQ(s->m_lfo[0].m_bEnable, 0);
    s->setSoundParam(57, 1, 0);
    CHECK_EQ(s->m_lfo[0].m_bEnable, 1);
    s->setSoundParam(67, 1, 0);
    CHECK(s->isLFOTempoSync(0) != 0);
    s->setSoundParam(67, 0, 0);
    CHECK(s->isLFOTempoSync(0) == 0);
    s->setSoundParam(74, 1, 0);
    CHECK_EQ(s->m_voice[5].m_bAutoPortament, 1);
    delete s;
}

/** @brief Parameter 59 switches the voices between the MIDI and the arpeggiator keyboard. */
TEST(synth1, arp_switches_keyboard)
{
    SynthEnv env;
    TSynth* s = newSynth();
    CHECK(s->m_voiceCtl.m_pKbdInfo == &s->m_kbdInfo);
    s->setSoundParam(59, 1, 0);
    CHECK(s->m_voiceCtl.m_pKbdInfo == &s->m_kbdInfoArp);
    s->setSoundParam(59, 0, 0);
    CHECK(s->m_voiceCtl.m_pKbdInfo == &s->m_kbdInfo);
    delete s;
}

/** @brief Delay time (35) is a beat fraction of the tempo, clamped to the 20-entry table, and
 *         follows tempo changes; the delay tone (98) enables the feedback filter except at 64. */
TEST(synth1, delay_time_and_tone)
{
    SynthEnv env;
    TSynth* s = newSynth();
    s->setSoundParam(35, 12, 0);                // 24/24 beat at 120 BPM = 0.5 s
    CHECK_EQ(s->m_delayTimeVal, 12);
    CHECK_CLOSE(s->m_delay.m_delayTime, 0.5, 1e-6);
    s->setBaseTempo(60.0f);
    CHECK_CLOSE(s->getBaseTempo(), 60.0, 0);
    CHECK_CLOSE(s->m_delay.m_delayTime, 1.0, 1e-6);
    s->setSoundParam2EngineMain(35, 40);        // clamped to 19 by the MIDI range: 96/24 beat
    CHECK_CLOSE(s->m_delay.m_delayTime, 4.0, 1e-6);
    s->setDelayTime(500);                       // clamped to the table: 96/24 beat
    CHECK_EQ(s->m_delayTimeVal, 500);
    CHECK_CLOSE(s->m_delay.m_delayTime, 4.0, 1e-6);
    s->setDelayTime(0);
    CHECK_CLOSE(s->m_delay.m_delayTime, 0.0, 0);
    s->setDelayTime(-3);
    CHECK_CLOSE(s->m_delay.m_delayTime, 0.0, 0);
    s->setSoundParam(98, 10, 0);
    CHECK_EQ(EqPeek::enabled(&s->m_delayEq), 1);
    s->setSoundParam(98, 64, 0);
    CHECK_EQ(EqPeek::enabled(&s->m_delayEq), 0);
    s->setSoundParam(98, 100, 0);
    CHECK_EQ(EqPeek::enabled(&s->m_delayEq), 1);
    delete s;
}

/** @brief Values outside a parameter's MIDI range are clamped before being applied (but the
 *         patch keeps the raw value); parameters without a range are passed through. */
TEST(synth1, engine_value_clamped_to_range)
{
    SynthEnv env;
    TSynth* s = newSynth();
    s->setSoundParam(0, 10, 0);
    CHECK_EQ(s->getCurrentSound()->getParamVal(0), 10);
    CHECK_EQ(s->m_voice[0].m_vco.m_shape, 3);
    s->setSoundParam(0, -5, 0);
    CHECK_EQ(s->m_voice[0].m_vco.m_shape, 0);
    s->setSoundParam2EngineMain(45, 200);
    CHECK_EQ(s->m_voice[0].m_vco.m_fmAmountVal, 127);
    s->setSoundParam2EngineMain(43, -1);
    CHECK_EQ(s->m_lfo[0].m_speedDial, 0);
    s->setSoundParam2EngineMain(94, 100);       // polyphony has no MIDI range: passed through
    CHECK_EQ(s->m_voiceCtl.m_polyNum, 32);      // CVoiceCtl clamps to the voice count
    delete s;
}

/** @brief setSoundParam2Engine() changes the engine only, not the stored patch; unknown ids
 *         are ignored. */
TEST(synth1, set_sound_param_2_engine_does_not_store)
{
    SynthEnv env;
    TSynth* s = newSynth();
    s->setSoundParam2Engine(0, 1);
    CHECK_EQ(s->m_voice[0].m_vco.m_shape, 1);
    CHECK_EQ(s->getCurrentSound()->getParamVal(0), 2);
    int before = s->getCurrentSound()->getParamsNum();
    s->setSoundParam2EngineMain(99, 5);
    s->setSoundParam2EngineMain(200, 5);
    s->setSoundParam2EngineMain(253, 5);
    CHECK_EQ(s->getCurrentSound()->getParamsNum(), before);
    CHECK_EQ(s->m_voice[0].m_vco.m_shape, 1);
    delete s;
}

/** @brief setSoundParam() ignores a call that repeats the previous id and value (no store, no
 *         notification), but not an earlier one. */
TEST(synth1, set_sound_param_dedupes_last_call)
{
    SynthEnv env;
    TSynth* s = newSynth();
    s->setSynWin(kWin);
    t1stub::reset();
    s->setSoundParam(19, 50, 1);
    CHECK_EQ(t1stub::postEventCalls, 1);
    s->setSoundParam(19, 50, 1);
    CHECK_EQ(t1stub::postEventCalls, 1);
    // a direct patch change is not seen by the dedupe
    s->getCurrentSound()->setParamVal(19, 7);
    s->setSoundParam(19, 50, 1);
    CHECK_EQ(s->getCurrentSound()->getParamVal(19), 7);
    s->setSoundParam(20, 50, 1);
    s->setSoundParam(19, 50, 1);
    CHECK_EQ(t1stub::postEventCalls, 3);
    CHECK_EQ(s->getCurrentSound()->getParamVal(19), 50);
    delete s;
}

/** @brief setCommonParam(): 254 sets the volume curve (0 is silent, 127 above unity), 251/252
 *         only request the bank/program, other ids change nothing. */
TEST(synth1, common_params)
{
    SynthEnv env;
    TSynth* s = newSynth();
    s->setCommonParam(254, 0, 0);
    CHECK_EQ(s->m_volumeVal, 0);
    CHECK_CLOSE(s->m_volume, 0.0, 0);
    s->setCommonParam(254, 127, 0);
    CHECK_CLOSE(s->m_volume, volumeGain(127), 1e-5);
    CHECK(s->m_volume > 1.0f);
    float prev = 0;
    for (int v = 1; v <= 127; v++) {
        s->setCommonParam(254, v, 0);
        CHECK(s->m_volume > prev);
        prev = s->m_volume;
    }
    s->setCommonParam(251, 5, 1);
    CHECK_EQ(s->m_soundLib.m_reqBankNo, 5);
    s->setCommonParam(252, 9, 1);
    CHECK_EQ(s->m_soundLib.m_reqProgNo, 9);
    CHECK_EQ(s->getCurrentSound()->m_progNo, 0);    // not loaded
    s->setVolume(100);
    CHECK_EQ(s->m_volumeVal, 100);
    delete s;
}

/** @brief Pitch bend glides one step per processChangeParams() call to the target and sets
 *         every voice's pitch offset to range * steps * 128 / (63 * 12). */
TEST(synth1, pitch_bend_glide)
{
    SynthEnv env;
    TSynth* s = newSynth();
    s->setPitchBend(8191);
    CHECK_EQ(s->m_targetPitchBend, 63);
    CHECK_EQ(s->m_bPitchBendChanging, 1);
    for (int i = 0; i < 62; i++) s->processChangeParams();
    CHECK_EQ(s->m_curPitchBend, 62);
    CHECK_EQ(s->m_bPitchBendChanging, 1);
    s->processChangeParams();
    CHECK_EQ(s->m_curPitchBend, 63);
    CHECK_EQ(s->m_bPitchBendChanging, 0);
    for (int i = 0; i < 32; i++) CHECK_EQ(s->m_voice[i].m_vco.m_pitchEnvLevel, 128);
    s->setPitchBend(-8192);                         // -64 is folded to -63
    CHECK_EQ(s->m_targetPitchBend, -63);
    s->setPitchBend(0);
    CHECK_EQ(s->m_targetPitchBend, 0);
    s->setSoundParam(40, 2, 0);
    s->setPitchBend(-4096);                         // -32 steps
    for (int i = 0; i < 95; i++) s->processChangeParams();
    CHECK_EQ(s->m_curPitchBend, -32);
    CHECK_EQ(s->m_voice[0].m_vco.m_pitchEnvLevel, 2 * -32 * 128 / (63 * 12));
    delete s;
}

// ---------------------------------------------------------------------------
// rendering

/** @brief Without notes the synth reaches silence, reports it with *pProcessed and leaves the
 *         buffer untouched; the smoothed volume snaps to the target while silent. */
TEST(synth1, render_silence_without_notes)
{
    SynthEnv env;
    TSynth* s = newSynth();
    REQUIRE(renderUntilSilent(s, 2000));
    float buf[512];
    for (int i = 0; i < 512; i++) buf[i] = 7.0f;
    int bProcessed = 0;
    int n = s->processSignalW(256, buf, &bProcessed);
    CHECK_EQ(n, 256);
    CHECK(bProcessed != 0);
    bool untouched = true;
    for (int i = 0; i < 512; i++) if (buf[i] != 7.0f) untouched = false;
    CHECK(untouched);
    CHECK_CLOSE(s->m_curVolume, s->m_volume, 0);
    delete s;
}

/** @brief processSignal() (mono entry) does nothing and reports silence. */
TEST(synth1, process_signal_mono_is_silent)
{
    SynthEnv env;
    TSynth* s = newSynth();
    float buf[4] = {1, 2, 3, 4};
    int bProcessed = 0;
    CHECK_EQ(s->processSignal(4, buf, &bProcessed), 4);
    CHECK_EQ(bProcessed, 1);
    CHECK_CLOSE(buf[2], 3.0, 0);
    delete s;
}

/** @brief With SSE disabled (m_bSSE == 0) nothing is rendered and the block is silent even
 *         with a held note. */
TEST(synth1, render_without_sse_is_silent)
{
    SynthEnv env;
    TSynth* s = newSynth();
    s->noteOn(60, 100);
    int saved = CSignalDevice::m_bSSE;
    CSignalDevice::m_bSSE = 0;
    float buf[64];
    for (int i = 0; i < 64; i++) buf[i] = 5.0f;
    int bProcessed = 0;
    CHECK_EQ(s->processSignalW(32, buf, &bProcessed), 32);
    CSignalDevice::m_bSSE = saved;
    CHECK_EQ(bProcessed, 1);
    CHECK_CLOSE(buf[10], 5.0, 0);
    CHECK_EQ(voicesOn(s, 60), 0);       // the voices were not even triggered
    delete s;
}

/** @brief A held note produces bounded, finite, non-silent stereo output; after note off the
 *         synth returns to silence. */
TEST(synth1, render_held_note)
{
    SynthEnv env;
    TSynth* s = newSynth();
    REQUIRE(renderUntilSilent(s, 2000));
    s->noteOn(60, 100);
    const int frames = 8192;
    float* out = new float[frames * 2];
    int silent = render(s, out, frames);
    CHECK_EQ(silent, 0);
    double peak = 0, sum2 = 0;
    bool finite = true;
    for (int i = 0; i < frames * 2; i++) {
        if (!(out[i] == out[i]) || fabs(out[i]) > 1e6) finite = false;
        if (fabs(out[i]) > peak) peak = fabs(out[i]);
        sum2 += out[i] * out[i];
    }
    CHECK(finite);
    CHECK(peak > 0.01);
    CHECK(peak < 8.0);
    CHECK(sqrt(sum2 / (frames * 2)) > 0.001);
    CHECK_EQ(voicesOn(s, 60), 1);
    s->noteOff(60);
    CHECK(renderUntilSilent(s, 4000));
    CHECK_EQ(voicesOn(s, 60), 0);
    delete[] out;
    delete s;
}

/** @brief Two synths given identical input (and the same random seeds, which set the
 *         free-running oscillator phases) render bit-identical output. */
TEST(synth1, render_is_deterministic)
{
    SynthEnv env;
    TSynth* a = newSynth();
    TSynth* b = newSynth();
    const int frames = 4096;
    float* oa = new float[frames * 2];
    float* ob = new float[frames * 2];
    TSynth* both[2] = {a, b};
    float* outs[2] = {oa, ob};
    for (int k = 0; k < 2; k++) {
        TSynth* s = both[k];
        seedRandom();
        s->setSoundParam(0, 1, 0);
        s->setSoundParam(19, 60, 0);
        s->noteOn(57, 90);
        s->noteOn(64, 70);
        render(s, outs[k], frames / 2);
        s->noteOff(57);
        render(s, outs[k] + frames, frames / 2);
    }
    CHECK(memcmp(oa, ob, sizeof(float) * frames * 2) == 0);
    delete[] oa;
    delete[] ob;
    delete a;
    delete b;
}

/** @brief The master volume scales the output: with the smoothed volume settled, two synths
 *         differing only in volume differ by exactly the ratio of their gains. */
TEST(synth1, master_volume_scales_output)
{
    SynthEnv env;
    TSynth* a = newSynth();
    TSynth* b = newSynth();
    a->setVolume(64);
    b->setVolume(127);
    REQUIRE(renderUntilSilent(a, 2000));
    REQUIRE(renderUntilSilent(b, 2000));
    CHECK_CLOSE(a->m_curVolume, a->m_volume, 0);
    CHECK_CLOSE(b->m_curVolume, b->m_volume, 0);
    const int frames = 2048;
    float oa[frames * 2], ob[frames * 2];
    seedRandom();
    a->noteOn(60, 100);
    render(a, oa, frames);
    seedRandom();
    b->noteOn(60, 100);
    render(b, ob, frames);
    double ratio = (double)b->m_volume / a->m_volume;
    int bad = 0;
    for (int i = 0; i < frames * 2; i++) {
        if (fabs(ob[i] - oa[i] * ratio) > 1e-5 * (1 + fabs(ob[i]))) bad++;
    }
    CHECK_EQ(bad, 0);
    delete a;
    delete b;
}

/** @brief Volume 0 silences a playing note once the smoothed volume has reached 0, and the
 *         smoothed volume moves gradually (not in one step) after a change. */
TEST(synth1, master_volume_smoothing)
{
    SynthEnv env;
    TSynth* s = newSynth();
    REQUIRE(renderUntilSilent(s, 2000));
    s->noteOn(60, 100);
    float buf[512];
    render(s, buf, 256);
    float start = s->m_curVolume;
    CHECK_CLOSE(start, s->m_volume, 0);
    s->setVolume(0);
    render(s, buf, 16);
    CHECK(s->m_curVolume < start);
    CHECK(s->m_curVolume > 0.0f);
    for (int i = 0; i < 100; i++) render(s, buf, 256);
    CHECK_CLOSE(s->m_curVolume, 0.0, 0);
    render(s, buf, 256);
    double peak = 0;
    for (int i = 0; i < 512; i++) if (fabs(buf[i]) > peak) peak = fabs(buf[i]);
    CHECK_CLOSE(peak, 0.0, 0);
    delete s;
}

/** @brief processMainVolume() scales a buffer while gliding the gain with m_volLPFCoef and
 *         snaps to the target once within 1/65536. */
TEST(synth1, process_main_volume)
{
    SynthEnv env;
    TSynth* s = newSynth();
    s->m_curVolume = 0;
    float buf[8];
    for (int i = 0; i < 8; i++) buf[i] = 1.0f;
    s->processMainVolume(buf, 1.0f, 8);
    float c = s->m_volLPFCoef;
    float g = 0;
    for (int i = 0; i < 8; i++) {
        g += (1.0f - g) * c;
        CHECK_CLOSE(buf[i], g, 1e-7);
    }
    CHECK_CLOSE(s->m_curVolume, g, 1e-7);
    s->m_curVolume = 1.0f - 1.0f / 131072;
    s->processMainVolume(buf, 1.0f, 1);
    CHECK_CLOSE(s->m_curVolume, 1.0, 0);
    delete s;
}

// ---------------------------------------------------------------------------
// GUI notifications

/** @brief Parameter changes post 0x720 (id, value) to the GUI window only when notification is
 *         requested and a window is set; nothing is ever sent with SendEvent. */
TEST(synth1, notify_param_changed)
{
    SynthEnv env;
    TSynth* s = newSynth();
    t1stub::reset();
    s->setSoundParam(19, 10, 1);                    // no window
    CHECK_EQ(t1stub::postEventCalls, 0);
    s->setSynWin(kWin);
    CHECK(s->m_pSynWin == kWin);
    s->setSoundParam(19, 11, 0);                    // no notification requested
    CHECK_EQ(t1stub::postEventCalls, 0);
    s->setSoundParam(19, 12, 1);
    CHECK_EQ(t1stub::postEventCalls, 1);
    CHECK_EQ(t1stub::lastMessage, 0x720u);
    CHECK_EQ(t1stub::lastWParam, 19);
    CHECK_EQ(t1stub::lastLParam, 12);
    s->setVolume(77);
    CHECK_EQ(t1stub::postEventCalls, 2);
    CHECK_EQ(t1stub::lastMessage, 0x720u);
    CHECK_EQ(t1stub::lastWParam, 254);
    CHECK_EQ(t1stub::lastLParam, 77);
    s->setCommonParam(251, 3, 1);                   // bank / program requests are never notified
    s->setCommonParam(252, 3, 1);
    CHECK_EQ(t1stub::postEventCalls, 2);
    s->setCommonParam(200, 3, 1);                   // other ids: notification only
    CHECK_EQ(t1stub::postEventCalls, 3);
    CHECK_EQ(t1stub::lastWParam, 200);
    CHECK_EQ(t1stub::sendEventCalls, 0);
    s->setSynWin(0);
    s->setSoundParam(19, 13, 1);
    CHECK_EQ(t1stub::postEventCalls, 3);
    delete s;
}

/** @brief Loading a program posts 0x721 with wParam = the notify parameter and lParam = the
 *         current patch, and only when the program actually changes. */
TEST(synth1, notify_program_loaded)
{
    SynthEnv env;
    TSynth* s = newSynth();
    s->setSynWin(kWin);
    t1stub::reset();
    s->loadProgram(0);                              // already loaded
    CHECK_EQ(t1stub::postEventCalls, 0);
    s->m_soundLib.setReqProgNo(3);
    s->loadProgram(5);
    CHECK_EQ(s->getCurrentSound()->m_progNo, 3);
    CHECK_EQ(t1stub::lastMessage, 0x721u);
    CHECK_EQ(t1stub::lastWParam, 5);
    CHECK(t1stub::lastLParam == (long)s->getCurrentSound());
    int calls = t1stub::postEventCalls;
    s->receiveProgramChange(3);
    CHECK_EQ(t1stub::postEventCalls, calls);
    s->receiveProgramChange(4);
    CHECK_EQ(t1stub::lastMessage, 0x721u);
    CHECK_EQ(t1stub::lastWParam, 0);
    CHECK_EQ(s->getCurrentSound()->m_progNo, 4);
    CHECK_EQ(t1stub::sendEventCalls, 0);
    delete s;
}

/** @brief processSignalW() posts 0x723 when the voice status changes (note on, note off) and
 *         not otherwise. */
TEST(synth1, notify_voice_status)
{
    SynthEnv env;
    TSynth* s = newSynth();
    s->setSynWin(kWin);
    REQUIRE(renderUntilSilent(s, 2000));
    float buf[512];
    render(s, buf, 256);
    t1stub::reset();
    render(s, buf, 256);
    CHECK_EQ(t1stub::postEventCalls, 0);
    s->noteOn(60, 100);
    render(s, buf, 256);
    CHECK_EQ(t1stub::postEventCalls, 1);
    CHECK_EQ(t1stub::lastMessage, 0x723u);
    CHECK_EQ(t1stub::lastWParam, 0);
    CHECK_EQ(t1stub::lastLParam, 0);
    render(s, buf, 256);
    CHECK_EQ(t1stub::postEventCalls, 1);
    s->noteOff(60);
    render(s, buf, 256);
    CHECK_EQ(t1stub::postEventCalls, 2);
    CHECK_EQ(t1stub::lastMessage, 0x723u);
    delete s;
}

/** @brief setCurrentProgNameOnly() takes bank/program from the library request without loading
 *         parameters and posts 0x720 for the program (252) and then the bank (251). */
TEST(synth1, set_current_prog_name_only)
{
    SynthEnv env;
    TSynth* s = newSynth();
    s->setSoundParam(0, 1, 0);
    s->setSynWin(kWin);
    s->m_soundLib.setReqBankNo(0, 0);
    s->m_soundLib.setReqProgNo(17);
    t1stub::reset();
    s->setCurrentProgNameOnly();
    CHECK_EQ(s->getCurrentSound()->m_progNo, 17);
    CHECK_EQ(s->getCurrentSound()->m_bankNo, 0);
    CHECK_EQ(s->getCurrentSound()->getParamVal(0), 1);
    CHECK_EQ(t1stub::postEventCalls, 2);
    CHECK_EQ(t1stub::lastMessage, 0x720u);
    CHECK_EQ(t1stub::lastWParam, 251);
    CHECK_EQ(t1stub::lastLParam, 0);
    delete s;
}

// ---------------------------------------------------------------------------
// plugin state

/**
 * @brief Offsets of the version 2 VST chunk (see VSTChunk in synth1.cpp).
 */
enum {
    kChunkSize = 0x238 + 0x101 * 8,
    kChunkVer = 0x20,
    kChunkHostSel = 0x24,
    kChunkPath = 0x28,
    kChunkSoundVer = 0x230,
    kChunkParamNum = 0x234,
    kChunkParams = 0x238
};

static int chunkInt(const unsigned char* p, int off)
{
    int v;
    memcpy(&v, p + off, 4);
    return v;
}

static void chunkParam(const unsigned char* p, int base, int id, int* pValid, int* pVal)
{
    *pValid = chunkInt(p, base + id * 8);
    *pVal = chunkInt(p, base + id * 8 + 4);
}

/** @brief makeVSTChunkData() refuses a too small buffer (message box, returns 0). */
TEST(synth1, chunk_buffer_too_small)
{
    SynthEnv env;
    TSynth* s = newSynth();
    t1stub::reset();
    unsigned char buf[16];
    CHECK_EQ(s->makeVSTChunkData(buf, kChunkSize - 1), 0);
    CHECK_EQ(t1stub::messageBoxCalls, 1);
    delete s;
}

/** @brief makeVSTChunkData() writes a version 2 chunk with the header, sound version 113,
 *         256 parameters, the bank/program/volume slots and every patch parameter. */
TEST(synth1, chunk_layout)
{
    SynthEnv env;
    TSynth* s = newSynth();
    s->setSoundParam(19, 33, 0);
    s->setVolume(90);
    unsigned char* buf = new unsigned char[kChunkSize + 64];
    memset(buf, 0xcc, kChunkSize + 64);
    int size = s->makeVSTChunkData(buf, kChunkSize + 64);
    CHECK_EQ(size, kChunkSize);
    CHECK_STREQ((char*)buf, "Synth1 VST Chunk Data");
    CHECK_EQ(chunkInt(buf, kChunkVer), 2);
    CHECK_EQ(chunkInt(buf, kChunkHostSel), CSynthesizer1::canVSTHostChangePatch());
    CHECK_STREQ((char*)buf + kChunkPath, "");
    CHECK_EQ(chunkInt(buf, kChunkSoundVer), 113);
    CHECK_EQ(chunkInt(buf, kChunkParamNum), 0x100);
    int valid, val;
    chunkParam(buf, kChunkParams, 250, &valid, &val);
    CHECK_EQ(valid, 1);
    CHECK_EQ(val, 113);
    chunkParam(buf, kChunkParams, 251, &valid, &val);
    CHECK_EQ(valid, 1);
    CHECK_EQ(val, 0);
    chunkParam(buf, kChunkParams, 252, &valid, &val);
    CHECK_EQ(valid, 1);
    CHECK_EQ(val, 0);
    chunkParam(buf, kChunkParams, 254, &valid, &val);
    CHECK_EQ(valid, 1);
    CHECK_EQ(val, 90);
    chunkParam(buf, kChunkParams, 253, &valid, &val);
    CHECK_EQ(valid, 0);
    chunkParam(buf, kChunkParams, 120, &valid, &val);
    CHECK_EQ(valid, 0);
    for (int id = 0; id < 99; id++) {
        chunkParam(buf, kChunkParams, id, &valid, &val);
        CHECK_EQ(valid, 1);
        CHECK_EQ(val, s->getCurrentSound()->getParamVal(id));
    }
    chunkParam(buf, kChunkParams, 19, &valid, &val);
    CHECK_EQ(val, 33);
    CHECK_EQ(s->getChunkVer(buf, size), 2);
    CHECK_EQ(buf[kChunkSize], 0xcc);             // nothing written past the chunk
    delete[] buf;
    delete s;
}

/** @brief A chunk saved by one synth and loaded into another restores every sound parameter,
 *         the engine state and the volume, notifying the GUI for each parameter. */
TEST(synth1, chunk_round_trip)
{
    SynthEnv env;
    TSynth* a = newSynth();
    a->setSoundParam(0, 3, 0);
    a->setSoundParam(19, 40, 0);
    a->setSoundParam(45, 77, 0);
    a->setSoundParam(37, 99, 0);
    a->setSoundParam(94, 8, 0);
    a->setSoundParam(41, 7, 0);
    a->setSoundParam(87, 19, 0);
    a->setVolume(101);
    unsigned char* buf = new unsigned char[kChunkSize];
    int size = a->makeVSTChunkData(buf, kChunkSize);
    REQUIRE(size == kChunkSize);

    TSynth* b = newSynth();
    b->setSynWin(kWin);
    t1stub::reset();
    b->setVSTChunkData(buf, size, b->getChunkVer(buf, size));
    for (int id = 0; id < 99; id++) {
        CHECK_EQ(b->getCurrentSound()->getParamVal(id), a->getCurrentSound()->getParamVal(id));
    }
    CHECK_EQ(b->m_volumeVal, 101);
    CHECK_EQ(b->m_voice[0].m_vco.m_shape, 3);
    CHECK_EQ(b->m_voice[0].m_vco.m_fmAmountVal, 77);
    CHECK_EQ(b->m_voiceCtl.m_polyNum, 8);
    CHECK_EQ(b->m_midiCtrl[0].paramID, 19);
    CHECK_EQ(b->getCurrentSound()->m_bankNo, 0);
    CHECK_EQ(b->getCurrentSound()->m_progNo, 0);
    // volume + program + bank + every parameter (the first parameter may be deduped)
    CHECK(t1stub::postEventCalls >= 3 + 98);
    CHECK_EQ(t1stub::lastMessage, 0x720u);
    CHECK_EQ(t1stub::sendEventCalls, 0);

    // saving again gives the same chunk
    unsigned char* buf2 = new unsigned char[kChunkSize];
    CHECK_EQ(b->makeVSTChunkData(buf2, kChunkSize), kChunkSize);
    CHECK(memcmp(buf, buf2, kChunkSize) == 0);
    delete[] buf;
    delete[] buf2;
    delete a;
    delete b;
}

/** @brief A chunk restores the stored bank/program numbers (name only, parameters from the
 *         chunk). */
TEST(synth1, chunk_restores_program_number)
{
    SynthEnv env;
    TSynth* a = newSynth();
    a->receiveProgramChange(12);
    a->setSoundParam(19, 5, 0);
    unsigned char* buf = new unsigned char[kChunkSize];
    int size = a->makeVSTChunkData(buf, kChunkSize);
    TSynth* b = newSynth();
    b->setVSTChunkData(buf, size, 2);
    CHECK_EQ(b->getCurrentSound()->m_progNo, 12);
    CHECK_EQ(b->m_soundLib.m_reqProgNo, 12);
    CHECK_EQ(b->getCurrentSound()->getParamVal(19), 5);
    delete[] buf;
    delete a;
    delete b;
}

/** @brief A version 1 chunk (version at 0x28, parameters at 0x30) of an old patch version is
 *         upgraded: e.g. the 1.05 delay dry/wet is halved, and missing parameters come from the
 *         initial sound. */
TEST(synth1, chunk_version1_is_converted)
{
    SynthEnv env;
    const int size = 0x30 + 0x100 * 8;
    unsigned char* buf = new unsigned char[size];
    memset(buf, 0, size);
    strcpy((char*)buf, "Synth1 VST Chunk Data");
    int one = 1, ver = 105;
    memcpy(buf + 0x20, &one, 4);
    memcpy(buf + 0x28, &ver, 4);
    int setParams[][2] = {{37, 100}, {0, 1}, {254, 70}, {252, 0}, {251, 0}};
    for (int i = 0; i < 5; i++) {
        memcpy(buf + 0x30 + setParams[i][0] * 8, &one, 4);
        memcpy(buf + 0x30 + setParams[i][0] * 8 + 4, &setParams[i][1], 4);
    }
    TSynth* s = newSynth();
    CHECK_EQ(s->getChunkVer(buf, size), 1);
    s->setVSTChunkData(buf, size, 1);
    CHECK_EQ(s->getCurrentSound()->getParamVal(37), 50);
    CHECK_EQ(s->getCurrentSound()->getParamVal(0), 1);
    CHECK_EQ(s->m_volumeVal, 70);
    CHECK_EQ(s->getCurrentSound()->getParamVal(19), s->m_initSound.getParamVal(19));
    CHECK_EQ(s->getCurrentSound()->m_version, 113);
    delete[] buf;
    delete s;
}

/** @brief getChunkVer() recognises DXi data only at exactly 0x814 bytes, rejects short or
 *         foreign data. */
TEST(synth1, chunk_version_detection)
{
    SynthEnv env;
    TSynth* s = newSynth();
    unsigned char buf[0x814];
    memset(buf, 0, sizeof buf);
    strcpy((char*)buf, "Synth1 DXi Data Header V1.0");
    CHECK_EQ(s->getChunkVer(buf, 0x814), 0);
    CHECK_EQ(s->getChunkVer(buf, 0x815), -1);
    memset(buf, 0, sizeof buf);
    strcpy((char*)buf, "Synth1 VST Chunk Data");
    int v = 7;
    memcpy(buf + 0x20, &v, 4);
    CHECK_EQ(s->getChunkVer(buf, 0x100), 7);
    CHECK_EQ(s->getChunkVer(buf, 0x814), 7);
    CHECK_EQ(s->getChunkVer(buf, 0x13), -1);
    strcpy((char*)buf, "something else");
    CHECK_EQ(s->getChunkVer(buf, 0x100), -1);
    delete s;
}

/** @brief setDxiSaveParams() applies the valid common parameters and the sound parameters; a
 *         missing version means 1.00 and the patch is upgraded. */
TEST(synth1, dxi_save_params)
{
    SynthEnv env;
    unsigned char buf[0x814];
    memset(buf, 0, sizeof buf);
    strcpy((char*)buf, "Synth1 DXi Data Header V1.0");
    // DxiParam {bool bValid; int val;} at 0x1c, 8 bytes each
    int entries[][2] = {{37, 100}, {19, 30}, {254, 90}};
    for (int i = 0; i < 3; i++) {
        buf[0x1c + entries[i][0] * 8] = 1;
        memcpy(buf + 0x1c + entries[i][0] * 8 + 4, &entries[i][1], 4);
    }
    TSynth* s = newSynth();
    CHECK_EQ(s->getChunkVer(buf, sizeof buf), 0);
    CHECK_EQ(s->setDxiSaveParams(buf), 1);
    CHECK_EQ(s->m_volumeVal, 90);
    CHECK_EQ(s->getCurrentSound()->getParamVal(37), 50);
    CHECK_EQ(s->getCurrentSound()->getParamVal(19), 30);
    delete s;

    // with version 113 nothing is converted
    int ver = 113;
    buf[0x1c + 250 * 8] = 1;
    memcpy(buf + 0x1c + 250 * 8 + 4, &ver, 4);
    s = newSynth();
    s->setDxiSaveParams(buf);
    CHECK_EQ(s->getCurrentSound()->getParamVal(37), 100);
    delete s;
}

/** @brief convertParams() upgrades a patch to version 113 (and clamps newer versions to 113). */
TEST(synth1, convert_params)
{
    SynthEnv env;
    TSynth* s = newSynth();
    CSoundData snd;
    snd.setParamVal(37, 80);
    snd.setParamVal(19, 50);
    snd.setParamVal(84, 3);
    snd.m_version = 105;
    s->convertParams(&snd);
    CHECK_EQ(snd.m_version, 113);
    CHECK_EQ(snd.getParamVal(37), 40);
    CHECK_EQ(snd.getParamVal(19), 50);
    CHECK_EQ(snd.getParamVal(84), 64);              // 1.07 -> 1.071 fixed default
    CHECK_EQ(snd.getParamsNum(), 3);                // only present ids are converted

    CSoundData snd2;
    snd2.setParamVal(37, 80);
    snd2.m_version = 120;
    s->convertParams(&snd2);
    CHECK_EQ(snd2.m_version, 113);
    CHECK_EQ(snd2.getParamVal(37), 80);
    delete s;
}

/** @brief setBaseTempo() with an unchanged tempo does nothing; a new tempo recomputes the
 *         delay time. */
TEST(synth1, base_tempo_unchanged_is_noop)
{
    SynthEnv env;
    TSynth* s = newSynth();
    s->setSoundParam(35, 12, 0);
    s->m_delay.m_delayTime = 9.0f;                  // marker: not recomputed for the same tempo
    s->setBaseTempo(120.0f);
    CHECK_CLOSE(s->m_delay.m_delayTime, 9.0, 0);
    s->setBaseTempo(240.0f);
    CHECK_CLOSE(s->m_delay.m_delayTime, 0.25, 1e-6);
    delete s;
}

// ---------------------------------------------------------------------------
// LFO, effect and delay setters

/** @brief setLFOSpeed() and setLFODepth() set the dials of one LFO (frequency and amount range follow). */
TEST(synth1, lfo_speed_and_depth_setters)
{
    SynthEnv env;
    TSynth* s = newSynth();
    int speed1 = s->m_lfo[1].m_speedDial;
    s->setLFOSpeed(0, 127);
    CHECK_EQ(s->m_lfo[0].m_speedDial, 127);
    CHECK_CLOSE(s->m_lfo[0].m_freq, 1500.0 / 12.0, 1e-3);
    CHECK_EQ(s->m_lfo[1].m_speedDial, speed1);
    s->setLFOSpeed(1, 0);
    CHECK_EQ(s->m_lfo[1].m_speedDial, 0);
    CHECK_CLOSE(s->m_lfo[1].m_freq, 1.0 / 12.0, 1e-6);

    s->setLFODest(1, LFO_DEST_PAN);
    s->setLFODepth(1, 127);
    CHECK_EQ(s->m_lfo[1].m_amountDial, 127);
    CHECK_EQ(s->m_lfo[1].m_amountLo, -64);
    CHECK_EQ(s->m_lfo[1].m_amountHi, 64);
    s->setLFODepth(1, 0);
    CHECK_EQ(s->m_lfo[1].m_amountLo, 0);
    CHECK_EQ(s->m_lfo[1].m_amountHi, 0);
    s->setLFODest(0, LFO_DEST_FILTER);
    s->setLFODepth(0, 127);
    CHECK_EQ(s->m_lfo[0].m_amountLo, 0);
    CHECK_EQ(s->m_lfo[0].m_amountHi, 1024);
    delete s;
}

/** @brief setLFOTempoSync() switches an LFO to note lengths at the base tempo; setBaseTempo() then rescales it. */
TEST(synth1, lfo_tempo_sync_setter)
{
    SynthEnv env;
    TSynth* s = newSynth();
    s->setLFOSpeed(0, 60);          // index 14: half a bar
    s->setLFOSpeed(1, 60);
    s->setLFOTempoSync(0, 5);       // any nonzero value enables
    CHECK_EQ(s->m_lfo[0].m_bTempoSync, 1);
    CHECK_EQ(s->isLFOTempoSync(0), 1);
    CHECK_CLOSE(s->m_lfo[0].m_freq, 1.0, 1e-6);      // 1 Hz at 120 BPM
    s->setLFOTempoSync(1, 1);
    s->setBaseTempo(180.0f);
    CHECK_CLOSE(s->m_lfo[0].m_freq, 1.5, 1e-6);
    CHECK_CLOSE(s->m_lfo[1].m_freq, 1.5, 1e-6);
    s->setLFOTempoSync(0, 0);
    CHECK_EQ(s->m_lfo[0].m_bTempoSync, 0);
    CHECK_CLOSE(s->m_lfo[0].m_freq, pow(1500.0, 60 / 127.0) / 12.0, 1e-4);
    s->setBaseTempo(90.0f);         // free-running LFO not affected
    CHECK_CLOSE(s->m_lfo[0].m_freq, pow(1500.0, 60 / 127.0) / 12.0, 1e-4);
    CHECK_CLOSE(s->m_lfo[1].m_freq, 0.75, 1e-6);
    delete s;
}

/** @brief setLFOKeySync(): the first key of a phrase restarts the LFO only while key sync is on. */
TEST(synth1, lfo_key_sync_setter)
{
    SynthEnv env;
    TSynth* s = newSynth();
    s->setLFOKeySync(1, 2);
    CHECK_EQ(s->m_lfo[1].m_bKeySync, 1);
    CHECK_EQ(s->m_lfo[0].m_bKeySync, 0);
    s->m_lfo[0].m_curTime = 1000;
    s->m_lfo[1].m_curTime = 1000;
    s->noteOn(60, 100);
    CHECK_EQ(s->m_lfo[1].m_curTime, 0);
    CHECK_EQ(s->m_lfo[0].m_curTime, 1000);
    s->m_lfo[1].m_curTime = 500;
    s->noteOn(64, 100);             // second key: no restart
    CHECK_EQ(s->m_lfo[1].m_curTime, 500);
    s->noteOff(60);
    s->noteOff(64);
    s->setLFOKeySync(1, 0);
    CHECK_EQ(s->m_lfo[1].m_bKeySync, 0);
    s->noteOn(60, 100);
    CHECK_EQ(s->m_lfo[1].m_curTime, 500);
    s->noteOff(60);
    delete s;
}

/** @brief setEFX1Enable() switches the effector; setDelayTempoSync() does nothing. */
TEST(synth1, effect_enable_and_delay_tempo_sync_setters)
{
    SynthEnv env;
    TSynth* s = newSynth();
    s->setEFX1Enable(0);
    CHECK_EQ(s->m_effector.getEnable(), 0);
    s->setEFX1Enable(3);
    CHECK_EQ(s->m_effector.getEnable(), 1);
    s->setEFX1Enable(0);
    CHECK_EQ(s->m_effector.getEnable(), 0);

    float t = s->m_delay.m_delayTime;
    int dl = s->m_delay.m_delayL;
    CSynthesizer1::setDelayTempoSync(0);
    CSynthesizer1::setDelayTempoSync(1);
    CHECK_FEQ(s->m_delay.m_delayTime, t);
    CHECK_EQ(s->m_delay.m_delayL, dl);
    delete s;
}

// ---------------------------------------------------------------------------
// writeProgram

/**
 * @brief writeProgram() saves the current patch into a bank folder, requests that slot, and the
 *        file loads back into another instance.
 */
TEST(synth1, write_program_and_read_back)
{
    SynthEnv env;
    TSynth* s = newSynth();
    s->setSoundParam(0, 1, 0);      // osc1 shape
    s->setSoundParam(45, 33, 0);    // osc1 FM
    WriteProgramInfo info;
    memset(&info, 0, sizeof(info));
    info.bankNo = 0;
    info.progNo = 6;
    strcpy(info.name, "Written Lead");
    info.color = RGB(255, 255, 0);
    CHECK_EQ(s->writeProgram(&info), 1);
    std::string path = env.tmp.sub("bank00") + "/007.sy1";
    REQUIRE(soundtest::fileExists(path));
    std::string text = soundtest::readFile(path);
    CHECK_STR(text.substr(0, 33), "Written Lead\ncolor=yellow\nver=113");
    CHECK(text.find("\n0,1\n") != std::string::npos);
    CHECK(text.find("\n45,33\n") != std::string::npos);
    int lines = 0;
    for (size_t i = 0; i < text.size(); i++) if (text[i] == '\n') lines++;
    CHECK_EQ(lines, 3 + s->getCurrentSound()->getParamsNum());
    CHECK_EQ(s->m_soundLib.m_reqBankNo, 0);
    CHECK_EQ(s->m_soundLib.m_reqProgNo, 6);
    CHECK_STREQ(s->getCurrentSound()->m_name, "Written Lead");
    CHECK_EQ(s->getCurrentSound()->m_progNo, 6);

    TSynth* b = newSynth();
    CHECK_EQ(b->getCurrentSound()->getParamVal(0), 2);   // the initial sound
    b->m_soundLib.setReqBankNo(0, NULL);
    b->m_soundLib.setReqProgNo(6);
    b->loadProgram(0);
    CHECK_STREQ(b->getCurrentSound()->m_name, "Written Lead");
    CHECK_EQ(b->getCurrentSound()->m_color, RGB(255, 255, 0));
    CHECK_EQ(b->getCurrentSound()->getParamVal(0), 1);
    CHECK_EQ(b->getCurrentSound()->getParamVal(45), 33);
    CHECK_EQ(b->m_voice[0].m_vco.m_shape, 1);
    delete b;
    delete s;
}

/** @brief writeProgram() to a zip bank (bank -1) fails and leaves the requested slot alone. */
TEST(synth1, write_program_to_zip_bank_fails)
{
    SynthEnv env;
    TSynth* s = newSynth();
    s->m_soundLib.setReqProgNo(3);
    WriteProgramInfo info;
    memset(&info, 0, sizeof(info));
    info.bankNo = -1;
    info.progNo = 9;
    strcpy(info.name, "Nope");
    CHECK_EQ(s->writeProgram(&info), -1);
    CHECK_EQ(s->m_soundLib.m_reqProgNo, 3);
    CHECK_EQ(s->getCurrentSound()->m_progNo, 0);
    delete s;
}
