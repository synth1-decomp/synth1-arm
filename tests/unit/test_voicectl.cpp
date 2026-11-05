/**
 * @file test_voicectl.cpp
 * @brief Unit tests for techpop/voicectl.h: CKeyboardInfo (key order, retrigger, damper) and
 *        CVoiceCtl (voice allocation in poly / mono / legato modes, round robin, voice stealing,
 *        the trigger order, enabled voice count, unison detune / pitch / pan, status tracking).
 */
#include "test.h"
#include "engine.h"

#include <string.h>
#include <list>
#include "lfo.h"
#include "voice.h"
#include "mixdev.h"
#include "voicectl.h"
#include "envelope.h"

namespace {

/**
 * @brief 32 voices, a mixer, a keyboard state and a voice controller wired like CSynthesizer1.
 */
struct CtlRig {
    CLFO lfo[2];
    CVoice voice[32];
    CMixDevice mixer;
    CKeyboardInfo kbd;
    CVoiceCtl ctl;

    CtlRig()
    {
        for (int i = 0; i < 2; i++) {
            lfo[i].setSamplingFreq(44100);
            lfo[i].setDestination(0);
        }
        mixer.setChannelNum(32);
        for (int i = 0; i < 32; i++) mixer.inputMixDeviceIs(i, &voice[i]);
        ctl.m_pMixDevice = &mixer;
        for (int i = 0; i < 32; i++) {
            voice[i].setLfo(&lfo[0], &lfo[1]);
            voice[i].enable(1);
            voice[i].m_vco.m_pitchEnvLevel = 0;
            voice[i].m_vco.setVoiceNo(i);
            voice[i].m_vco.setVCF(&voice[i].m_vcf);
            voice[i].m_vco.init();
            voice[i].setUnisonPhaseShift(0);
            voice[i].m_vca.setSustain(1.0f);
        }
        ctl.setKeyboardInfo(&kbd);
        ctl.setVoiceNum(32);
        for (int i = 0; i < 32; i++) ctl.setVoice(i, &voice[i]);
        ctl.calcUnisonPan();
        for (int i = 0; i < 32; i++) ctl.m_voiceStatus[i] = -1;
    }

    /** @brief Presses a key and lets the controller handle it. */
    void on(int nn, int vel = 100) { kbd.onKey(nn, vel); ctl.controlVoiceTrigger(); }

    /** @brief Releases a key and lets the controller handle it. */
    void off(int nn) { kbd.offKey(nn); ctl.controlVoiceTrigger(); }

    /** @brief Counts the voices holding a note. */
    int notesOn()
    {
        int n = 0;
        for (int i = 0; i < 32; i++) if (voice[i].m_bNoteOn) n++;
        return n;
    }

    /** @brief Counts the voices holding note nn. */
    int voicesOn(int nn)
    {
        int n = 0;
        for (int i = 0; i < 32; i++) if (voice[i].m_bNoteOn && voice[i].m_noteNo == nn) n++;
        return n;
    }

    /** @brief Renders all enabled voices for a few blocks (moves the envelopes on). */
    void render(int blocks)
    {
        float buf[256];
        for (int b = 0; b < blocks; b++) {
            for (int i = 0; i < 32; i++) {
                int p;
                memset(buf, 0, sizeof buf);
                voice[i].processSignal(256, buf, &p);
            }
        }
    }
};

/**
 * @brief Creates a rig after setting up the engine globals.
 *
 * @return A new rig; delete it when done.
 */
CtlRig* makeRig()
{
    t1::initEngine();
    return new CtlRig();
}

/**
 * @brief Returns the i-th voice of the controller's active list.
 *
 * @param ctl  Controller.
 * @param i    Index in trigger order.
 * @return The voice, or NULL past the end.
 */
CVoice* active(CVoiceCtl& ctl, int i)
{
    std::list<CVoice*>::iterator it = ctl.m_activeVoices.begin();
    for (; it != ctl.m_activeVoices.end() && i > 0; ++it, --i) {}
    return it == ctl.m_activeVoices.end() ? 0 : *it;
}

}  // namespace

// ---------------------------------------------------------------------------------------------
// CKeyboardInfo

/** @brief A new keyboard state has no keys, damper up, nothing changed. */
TEST(kbd, constructor_defaults)
{
    CKeyboardInfo k;
    CHECK_EQ(k.getKeyOnNum(), 0);
    CHECK_EQ(k.m_bDumper, 0);
    CHECK_EQ(k.m_bChanged, 0);
    for (int i = 0; i < 128; i++) {
        CHECK_EQ(k.getVelocity(i), 0);
        CHECK_EQ(k.m_bOffReserved[i], 0);
    }
}

/** @brief onKey() records velocity and appends the key to the order, flagged new (+0x100). */
TEST(kbd, on_key_records_order)
{
    CKeyboardInfo k;
    k.onKey(60, 100);
    k.onKey(64, 90);
    k.onKey(67, 80);
    CHECK_EQ(k.getKeyOnNum(), 3);
    CHECK_EQ(k.getVelocity(60), 100);
    CHECK_EQ(k.getVelocity(64), 90);
    CHECK_EQ(k.getVelocity(67), 80);
    CHECK_EQ(k.m_keyOrder[0], 60 + 0x100);
    CHECK_EQ(k.m_keyOrder[1], 64 + 0x100);
    CHECK_EQ(k.m_keyOrder[2], 67 + 0x100);
    CHECK_EQ(k.m_bChanged, 1);
}

/** @brief offKey() clears the key and closes the gap in the order. */
TEST(kbd, off_key_removes_from_order)
{
    CKeyboardInfo k;
    k.onKey(60, 100);
    k.onKey(64, 90);
    k.onKey(67, 80);
    k.m_bChanged = 0;
    k.offKey(64);
    CHECK_EQ(k.getKeyOnNum(), 2);
    CHECK_EQ(k.getVelocity(64), 0);
    CHECK_EQ(k.m_keyOrder[0], 60 + 0x100);
    CHECK_EQ(k.m_keyOrder[1], 67 + 0x100);
    CHECK_EQ(k.m_bChanged, 1);
    // releasing a key that is not held only clears its velocity
    k.offKey(10);
    CHECK_EQ(k.getKeyOnNum(), 2);
    // handled keys (without the flag) are found too
    k.m_keyOrder[0] = 60;
    k.offKey(60);
    CHECK_EQ(k.getKeyOnNum(), 1);
    CHECK_EQ(k.m_keyOrder[0], 67 + 0x100);
}

/** @brief Pressing a held key again moves it to the end of the order with the new velocity. */
TEST(kbd, repress_moves_key_to_end)
{
    CKeyboardInfo k;
    k.onKey(60, 100);
    k.onKey(64, 90);
    k.onKey(60, 30);
    CHECK_EQ(k.getKeyOnNum(), 2);
    CHECK_EQ(k.m_keyOrder[0], 64 + 0x100);
    CHECK_EQ(k.m_keyOrder[1], 60 + 0x100);
    CHECK_EQ(k.getVelocity(60), 30);
}

/** @brief With the damper down releases are deferred until the pedal goes up. */
TEST(kbd, damper_defers_release)
{
    CKeyboardInfo k;
    k.onKey(60, 100);
    k.onKey(64, 100);
    k.setDumper(1);
    k.offKey(60);
    CHECK_EQ(k.getVelocity(60), 100);
    CHECK_EQ(k.getKeyOnNum(), 2);
    CHECK_EQ(k.m_bOffReserved[60], 1);
    // a deferred key pressed again stays held
    k.offKey(64);
    k.onKey(64, 70);
    CHECK_EQ(k.m_bOffReserved[64], 0);
    k.setDumper(0);
    CHECK_EQ(k.getVelocity(60), 0);
    CHECK_EQ(k.m_bOffReserved[60], 0);
    CHECK_EQ(k.getVelocity(64), 70);
    CHECK_EQ(k.getKeyOnNum(), 1);
    CHECK_EQ(k.m_keyOrder[0], 64 + 0x100);
}

// ---------------------------------------------------------------------------------------------
// CVoiceCtl setup

/** @brief The constructor starts in poly mode without unison; setVoiceNum/setVoice wire the voices. */
TEST(voicectl, defaults_and_setup)
{
    CVoiceCtl c;
    CHECK_EQ(c.m_polyMode, POLYMODE_POLY);
    CHECK_EQ(c.m_lastVoice, -1);
    CHECK(c.m_pKbdInfo == 0);
    CHECK_EQ(c.m_bUnison, 0);
    CHECK_EQ(c.m_unisonVoiceNum, 1);
    CHECK_EQ(c.m_unisonDetune, 0);
    CHECK_EQ(c.m_unisonPitch, 0);
    CHECK_CLOSE(c.m_unisonPan, 0.0, 0);
    CHECK(c.m_activeVoices.empty());

    CtlRig* r = makeRig();
    CHECK_EQ(r->ctl.m_voiceNum, 32);
    CHECK_EQ(r->ctl.m_polyNum, 32);
    CHECK_EQ(r->ctl.m_enableVoiceNum, 32);
    for (int i = 0; i < 32; i++) {
        CHECK(r->ctl.m_pVoice[i] == &r->voice[i]);
        CHECK_EQ(r->voice[i].m_voiceNo, i);
    }
    CHECK(r->ctl.getVoicesStatus() == r->ctl.m_voiceStatus);
    delete r;
}

/** @brief Nothing happens unless the keyboard state changed. */
TEST(voicectl, no_change_no_action)
{
    CtlRig* r = makeRig();
    r->kbd.onKey(60, 100);
    r->kbd.m_bChanged = 0;
    r->ctl.controlVoiceTrigger();
    CHECK_EQ(r->notesOn(), 0);
    CHECK_EQ(r->kbd.m_keyOrder[0], 60 + 0x100);   // still new
    r->kbd.m_bChanged = 1;
    r->ctl.controlVoiceTrigger();
    CHECK_EQ(r->notesOn(), 1);
    CHECK_EQ(r->kbd.m_bChanged, 0);
    CHECK_EQ(r->kbd.m_keyOrder[0], 60);            // handled
    delete r;
}

// ---------------------------------------------------------------------------------------------
// poly

/** @brief Poly: each key gets its own voice with its note and velocity; key off releases it. */
TEST(voicectl, poly_note_on_off)
{
    CtlRig* r = makeRig();
    r->on(60, 100);
    CHECK_EQ(r->notesOn(), 1);
    CHECK_EQ(r->voice[0].m_noteNo, 60);
    CHECK_EQ(r->voice[0].m_velocity, 100);
    CHECK_EQ(r->voice[0].getCurStatus(), VOICE_NOTEON);
    r->on(64, 80);
    r->on(67, 60);
    CHECK_EQ(r->notesOn(), 3);
    CHECK_EQ(r->voice[1].m_noteNo, 64);
    CHECK_EQ(r->voice[2].m_noteNo, 67);
    CHECK_EQ(r->voice[2].m_velocity, 60);
    CHECK_EQ((int)r->ctl.m_activeVoices.size(), 3);

    r->off(64);
    CHECK_EQ(r->notesOn(), 2);
    CHECK_EQ(r->voice[1].m_bNoteOn, 0);
    CHECK_EQ(r->voice[1].getCurStatus(), VOICE_RELEASING);
    CHECK_EQ((int)r->ctl.m_activeVoices.size(), 2);
    r->off(60);
    r->off(67);
    CHECK_EQ(r->notesOn(), 0);
    CHECK(r->ctl.m_activeVoices.empty());
    delete r;
}

/** @brief Poly: keys arriving in one batch are triggered newest first. */
TEST(voicectl, poly_trigger_order_newest_first)
{
    CtlRig* r = makeRig();
    r->kbd.onKey(60, 100);
    r->kbd.onKey(64, 100);
    r->kbd.onKey(67, 100);
    r->ctl.controlVoiceTrigger();
    CHECK_EQ(r->voice[0].m_noteNo, 67);
    CHECK_EQ(r->voice[1].m_noteNo, 64);
    CHECK_EQ(r->voice[2].m_noteNo, 60);
    CHECK(active(r->ctl, 0) == &r->voice[0]);
    CHECK(active(r->ctl, 2) == &r->voice[2]);
    CHECK_EQ(r->ctl.m_lastVoice, 2);
    for (int i = 0; i < 3; i++) CHECK(r->kbd.m_keyOrder[i] < 0x100);
    delete r;
}

/** @brief Poly: a new key goes to the next free voice after the last one used (round robin). */
TEST(voicectl, poly_round_robin)
{
    CtlRig* r = makeRig();
    r->on(60);
    r->on(62);
    r->on(64);
    r->off(62);           // voice 1 is free again
    r->on(65);
    CHECK_EQ(r->voice[3].m_noteNo, 65);
    CHECK_EQ(r->voice[3].m_bNoteOn, 1);
    CHECK_EQ(r->voice[1].m_bNoteOn, 0);
    CHECK_EQ(r->ctl.m_lastVoice, 3);
    // wraps around the enabled voices
    r->ctl.setPolyNum(4);
    CHECK_EQ(r->ctl.m_lastVoice, -1);
    r->ctl.m_lastVoice = 3;
    r->on(67);
    CHECK_EQ(r->voice[1].m_noteNo, 67);   // 0 is busy, 1 is free
    CHECK_EQ(r->voice[1].m_bNoteOn, 1);
    delete r;
}

/** @brief Poly: pressing a sounding key again releases its voice and triggers a new one. */
TEST(voicectl, poly_repress_same_key)
{
    CtlRig* r = makeRig();
    r->on(60, 100);
    r->on(60, 50);
    CHECK_EQ(r->voicesOn(60), 1);
    CHECK_EQ(r->voice[0].m_bNoteOn, 0);
    CHECK_EQ(r->voice[1].m_bNoteOn, 1);
    CHECK_EQ(r->voice[1].m_velocity, 50);
    CHECK_EQ((int)r->ctl.m_activeVoices.size(), 1);
    delete r;
}

/** @brief Poly: with all voices busy the oldest voice is stolen. */
TEST(voicectl, poly_steals_oldest)
{
    CtlRig* r = makeRig();
    r->ctl.setPolyNum(2);
    CHECK_EQ(r->ctl.m_enableVoiceNum, 2);
    r->on(60);
    r->on(62);
    r->on(64);
    CHECK_EQ(r->notesOn(), 2);
    CHECK_EQ(r->voicesOn(60), 0);
    CHECK_EQ(r->voice[0].m_noteNo, 64);   // voice 0 (with the oldest note) was stolen
    CHECK_EQ(r->voice[1].m_noteNo, 62);
    CHECK(active(r->ctl, 0) == &r->voice[1]);
    CHECK(active(r->ctl, 1) == &r->voice[0]);
    r->on(65);
    CHECK_EQ(r->voice[1].m_noteNo, 65);
    CHECK_EQ(r->voicesOn(62), 0);
    // releasing the stolen key does not touch the voice that took it over
    r->off(60);
    r->off(62);
    CHECK_EQ(r->notesOn(), 2);
    delete r;
}

/** @brief Poly: a batch with more keys than voices only triggers as many keys as there are voices. */
TEST(voicectl, poly_batch_limited_to_voices)
{
    CtlRig* r = makeRig();
    r->ctl.setPolyNum(3);
    for (int k = 0; k < 6; k++) r->kbd.onKey(60 + k, 100);
    r->ctl.controlVoiceTrigger();
    CHECK_EQ(r->notesOn(), 3);
    CHECK_EQ(r->voicesOn(65), 1);
    CHECK_EQ(r->voicesOn(64), 1);
    CHECK_EQ(r->voicesOn(63), 1);
    CHECK_EQ(r->voicesOn(60), 0);
    delete r;
}

/** @brief Poly unison: each key triggers m_unisonVoiceNum voices numbered 0..n-1 in the group. */
TEST(voicectl, poly_unison_triggers_groups)
{
    CtlRig* r = makeRig();
    r->ctl.setUnisonVoiceNum(4);
    r->ctl.setUnisonMode(1);
    CHECK_EQ(r->ctl.m_bUnison, 1);
    r->ctl.setPolyNum(8);
    r->on(60);
    CHECK_EQ(r->voicesOn(60), 4);
    for (int i = 0; i < 4; i++) {
        CHECK_EQ(r->voice[i].m_noteNo, 60);
        CHECK_EQ(r->voice[i].m_vco.m_unisonNum, 4);
        CHECK_EQ(r->voice[i].m_vco.m_unisonNo, i);
    }
    r->on(64);
    CHECK_EQ(r->voicesOn(64), 4);
    // a third key steals the oldest group
    r->on(67);
    CHECK_EQ(r->voicesOn(67), 4);
    CHECK_EQ(r->voicesOn(60), 0);
    CHECK_EQ(r->voicesOn(64), 4);
    r->off(64);
    CHECK_EQ(r->voicesOn(64), 0);
    CHECK_EQ(r->notesOn(), 4);
    delete r;
}

// ---------------------------------------------------------------------------------------------
// enabled voices

/** @brief The enabled voice count follows the mode, unison and polyphony; the mixer is told. */
TEST(voicectl, enabled_voice_count)
{
    CtlRig* r = makeRig();
    r->ctl.setPolyNum(8);
    CHECK_EQ(r->ctl.m_enableVoiceNum, 8);
    CHECK_EQ(r->mixer.m_enableVoiceNum, 8);
    CHECK_EQ(r->voice[7].m_bEnable, 1);
    CHECK_EQ(r->voice[8].m_bEnable, 0);
    CHECK_EQ(r->voice[31].getCurStatus(), VOICE_DISABLED);

    r->ctl.setPolyMode(POLYMODE_MONO);
    CHECK_EQ(r->ctl.m_enableVoiceNum, 1);
    CHECK_EQ(r->mixer.m_enableVoiceNum, 1);
    CHECK_EQ(r->voice[1].m_bEnable, 0);

    r->ctl.setUnisonVoiceNum(4);
    CHECK_EQ(r->ctl.m_enableVoiceNum, 1);   // unison off: no change
    r->ctl.setUnisonMode(1);
    CHECK_EQ(r->ctl.m_enableVoiceNum, 4);
    r->ctl.setPolyNum(3);
    CHECK_EQ(r->ctl.m_enableVoiceNum, 3);   // limited by the polyphony

    r->ctl.setPolyMode(POLYMODE_LEGATO);
    CHECK_EQ(r->ctl.m_enableVoiceNum, 3);
    r->ctl.setPolyMode(POLYMODE_POLY);
    CHECK_EQ(r->ctl.m_enableVoiceNum, 3);
    CHECK_EQ(r->mixer.m_enableVoiceNum, 3);
    delete r;
}

/** @brief setPolyNum() clamps to 1..m_voiceNum. */
TEST(voicectl, poly_num_clamped)
{
    CtlRig* r = makeRig();
    r->ctl.setPolyNum(0);
    CHECK_EQ(r->ctl.m_polyNum, 1);
    r->ctl.setPolyNum(-5);
    CHECK_EQ(r->ctl.m_polyNum, 1);
    r->ctl.setPolyNum(100);
    CHECK_EQ(r->ctl.m_polyNum, 32);
    CHECK_EQ(r->ctl.m_enableVoiceNum, 32);
    delete r;
}

/** @brief Disabling voices releases their notes and drops them from the active list. */
TEST(voicectl, disabling_releases_voices)
{
    CtlRig* r = makeRig();
    r->on(60);
    r->on(64);
    r->on(67);
    CHECK_EQ(r->notesOn(), 3);
    r->ctl.setPolyNum(1);
    CHECK_EQ(r->notesOn(), 1);
    CHECK_EQ(r->voice[0].m_bNoteOn, 1);
    CHECK_EQ(r->voice[1].m_bEnable, 0);
    CHECK_EQ((int)r->ctl.m_activeVoices.size(), 1);
    CHECK(active(r->ctl, 0) == &r->voice[0]);
    delete r;
}

// ---------------------------------------------------------------------------------------------
// mono / legato

/** @brief Mono: the newest key plays on voice 0; releasing it returns to the previous held key. */
TEST(voicectl, mono_last_note_priority)
{
    CtlRig* r = makeRig();
    r->ctl.setPolyMode(POLYMODE_MONO);
    r->on(60, 100);
    CHECK_EQ(r->voice[0].m_noteNo, 60);
    CHECK_EQ(r->voice[0].m_bNoteOn, 1);
    CHECK_EQ(r->ctl.m_monoVelocity, 100);
    r->on(64, 70);
    CHECK_EQ(r->voice[0].m_noteNo, 64);
    CHECK_EQ(r->voice[0].m_velocity, 70);
    CHECK_EQ(r->notesOn(), 1);
    r->off(64);
    CHECK_EQ(r->voice[0].m_noteNo, 60);
    CHECK_EQ(r->voice[0].m_velocity, 70);   // keeps the last pressed key's velocity
    CHECK_EQ(r->voice[0].m_bNoteOn, 1);
    // releasing a key that is not playing changes nothing
    r->on(67, 90);
    r->off(60);
    CHECK_EQ(r->voice[0].m_noteNo, 67);
    CHECK_EQ(r->voice[0].m_bNoteOn, 1);
    r->off(67);
    CHECK_EQ(r->voice[0].m_bNoteOn, 0);
    CHECK_EQ(r->ctl.m_monoVelocity, 0);
    CHECK(r->ctl.m_activeVoices.empty());
    delete r;
}

/** @brief Mono retriggers the envelopes on each new note; legato only changes the pitch. */
TEST(voicectl, mono_retriggers_legato_does_not)
{
    CtlRig* r = makeRig();
    r->ctl.setPolyMode(POLYMODE_MONO);
    r->on(60);
    r->render(20);
    CHECK(r->voice[0].m_vca.getEnvStatus() != ENV_ATTACK);
    r->on(64);
    CHECK_EQ(r->voice[0].m_vca.getEnvStatus(), ENV_ATTACK);
    r->off(64);
    r->off(60);

    r->ctl.setPolyMode(POLYMODE_LEGATO);
    r->on(60);
    CHECK_EQ(r->voice[0].m_vca.getEnvStatus(), ENV_ATTACK);   // first note triggers
    r->render(20);
    int st = r->voice[0].m_vca.getEnvStatus();
    CHECK(st != ENV_ATTACK);
    r->on(64);
    CHECK_EQ(r->voice[0].m_noteNo, 64);
    CHECK_EQ(r->voice[0].m_vco.m_note, 64);
    CHECK_EQ(r->voice[0].m_vca.getEnvStatus(), st);            // not retriggered
    CHECK_EQ((int)r->ctl.m_activeVoices.size(), 1);
    r->off(64);
    CHECK_EQ(r->voice[0].m_noteNo, 60);
    CHECK_EQ(r->voice[0].m_vca.getEnvStatus(), st);
    r->off(60);
    CHECK_EQ(r->voice[0].m_bNoteOn, 0);
    // after a full release legato triggers again
    r->on(62);
    CHECK_EQ(r->voice[0].m_vca.getEnvStatus(), ENV_ATTACK);
    delete r;
}

/** @brief Mono unison: all unison voices play the key, each with its index in the group. */
TEST(voicectl, mono_unison)
{
    CtlRig* r = makeRig();
    r->ctl.setPolyMode(POLYMODE_MONO);
    r->ctl.setUnisonVoiceNum(3);
    r->ctl.setUnisonMode(1);
    r->on(50, 110);
    CHECK_EQ(r->voicesOn(50), 3);
    for (int i = 0; i < 3; i++) {
        CHECK_EQ(r->voice[i].m_vco.m_unisonNum, 3);
        CHECK_EQ(r->voice[i].m_vco.m_unisonNo, i);
        CHECK_EQ(r->voice[i].m_velocity, 110);
    }
    r->on(55);
    CHECK_EQ(r->voicesOn(55), 3);
    CHECK_EQ((int)r->ctl.m_activeVoices.size(), 3);
    r->off(55);
    r->off(50);
    CHECK_EQ(r->notesOn(), 0);
    delete r;
}

// ---------------------------------------------------------------------------------------------
// unison detune / pan

/** @brief Unison detune spreads 0..100 cents evenly; odd voices get the unison pitch on top. */
TEST(voicectl, unison_detune_and_pitch)
{
    CtlRig* r = makeRig();
    r->ctl.setUnisonVoiceNum(4);
    r->ctl.setUnisonDetune(127);
    // unison off: no detune yet
    for (int i = 0; i < 32; i++) CHECK_CLOSE(r->voice[i].m_vco.m_detune, 0.0, 0);
    r->ctl.setUnisonMode(1);
    CHECK_CLOSE(r->voice[0].m_vco.m_detune, -50.0, 1e-3);
    CHECK_CLOSE(r->voice[1].m_vco.m_detune, -50.0 / 3, 1e-3);
    CHECK_CLOSE(r->voice[2].m_vco.m_detune, 50.0 / 3, 1e-3);
    CHECK_CLOSE(r->voice[3].m_vco.m_detune, 50.0, 1e-3);
    CHECK_CLOSE(r->voice[5].m_vco.m_detune, -50.0 / 3, 1e-3);   // pattern repeats per group
    CHECK(r->voice[0].m_vcf.m_unisonOfs < 0);
    CHECK(r->voice[3].m_vcf.m_unisonOfs > 0);

    // the detune curve is exponential: half the dial gives less than half the range
    r->ctl.setUnisonDetune(64);
    double range = (exp(64 / 127.0 * 2.0) - 1.0) / (exp(2.0) - 1.0) * 100.0;
    CHECK_CLOSE(r->voice[3].m_vco.m_detune, range * 0.5, 1e-3);
    CHECK(range < 50.0);
    r->ctl.setUnisonDetune(0);
    CHECK_CLOSE(r->voice[0].m_vco.m_detune, 0.0, 1e-6);

    // unison pitch: (val - 24) semitones on the odd voices
    r->ctl.setUnisonPitch(36);
    CHECK_EQ(r->ctl.m_unisonPitch, 1200);
    CHECK_CLOSE(r->voice[0].m_vco.m_detune, 0.0, 1e-6);
    CHECK_CLOSE(r->voice[1].m_vco.m_detune, 1200.0, 1e-3);
    CHECK_CLOSE(r->voice[2].m_vco.m_detune, 0.0, 1e-6);
    CHECK_CLOSE(r->voice[3].m_vco.m_detune, 1200.0, 1e-3);
    r->ctl.setUnisonPitch(24);
    CHECK_EQ(r->ctl.m_unisonPitch, 0);
    CHECK_CLOSE(r->voice[1].m_vco.m_detune, 0.0, 1e-6);

    // detuned oscillator frequency follows the cents
    r->ctl.setUnisonPitch(31);    // +7 semitones
    r->voice[1].setNoteInfo(60, 100);
    CHECK_CLOSE(r->voice[1].m_vco.m_freq1 / s1g->m_freqTable[60], pow(2.0, 7 / 12.0), 1e-4);

    // unison off resets all detune
    r->ctl.setUnisonMode(0);
    for (int i = 0; i < 32; i++) CHECK_CLOSE(r->voice[i].m_vco.m_detune, 0.0, 0);
    delete r;
}

/** @brief Unison pan spreads the group over -pan..+pan; without unison every voice is centered. */
TEST(voicectl, unison_pan)
{
    CtlRig* r = makeRig();
    r->ctl.setUnisonVoiceNum(4);
    r->ctl.setVoicePan(0.6f);
    CHECK_CLOSE(r->ctl.m_unisonPan, 0.6, 1e-7);
    for (int i = 0; i < 32; i++) CHECK_CLOSE(r->voice[i].getPanVal(), 0.0, 0);
    r->ctl.setUnisonMode(1);
    CHECK_CLOSE(r->voice[0].getPanVal(), -0.6, 1e-6);
    CHECK_CLOSE(r->voice[1].getPanVal(), -0.2, 1e-6);
    CHECK_CLOSE(r->voice[2].getPanVal(), 0.2, 1e-6);
    CHECK_CLOSE(r->voice[3].getPanVal(), 0.6, 1e-6);
    CHECK_CLOSE(r->voice[4].getPanVal(), -0.6, 1e-6);
    r->ctl.setUnisonVoiceNum(2);
    CHECK_CLOSE(r->voice[0].getPanVal(), -0.6, 1e-6);
    CHECK_CLOSE(r->voice[1].getPanVal(), 0.6, 1e-6);
    r->ctl.setVoicePan(0.0f);
    CHECK_CLOSE(r->voice[1].getPanVal(), 0.0, 1e-6);
    r->ctl.setVoicePan(1.0f);
    r->ctl.setUnisonMode(0);
    for (int i = 0; i < 32; i++) CHECK_CLOSE(r->voice[i].getPanVal(), 0.0, 0);
    delete r;
}

// ---------------------------------------------------------------------------------------------
// status

/** @brief isVoicesStatusChanged() reports changes once and fills the status array. */
TEST(voicectl, status_tracking)
{
    CtlRig* r = makeRig();
    r->ctl.setPolyNum(4);
    CHECK_EQ(r->ctl.isVoicesStatusChanged(), 1);   // from the -1 sentinels
    int* st = r->ctl.getVoicesStatus();
    CHECK_EQ(st[0], VOICE_IDLE);
    CHECK_EQ(st[4], VOICE_DISABLED);
    CHECK_EQ(r->ctl.isVoicesStatusChanged(), 0);
    r->on(60);
    CHECK_EQ(r->ctl.isVoicesStatusChanged(), 1);
    CHECK_EQ(st[0], VOICE_NOTEON);
    CHECK_EQ(r->ctl.isVoicesStatusChanged(), 0);
    r->off(60);
    CHECK_EQ(r->ctl.isVoicesStatusChanged(), 1);
    CHECK_EQ(st[0], VOICE_RELEASING);
    delete r;
}

/** @brief getNextOffVoice() returns NULL when every enabled voice holds a note. */
TEST(voicectl, next_off_voice_none_free)
{
    CtlRig* r = makeRig();
    r->ctl.setPolyNum(2);
    CHECK(r->ctl.getNextOffVoice() == &r->voice[0]);
    r->on(60);
    CHECK(r->ctl.getNextOffVoice() == &r->voice[1]);
    r->on(62);
    CHECK(r->ctl.getNextOffVoice() == 0);
    delete r;
}
