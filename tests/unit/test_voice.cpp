/**
 * @file test_voice.cpp
 * @brief Unit tests for techpop/voice.h (CVoice): enable state, note on/off through the
 *        oscillator, filter and amplifier, status values, pan and unison info, portamento
 *        selection, LFO routing and the silence of a disabled or idle voice.
 */
#include "test.h"
#include "engine.h"

#include "lfo.h"
#include "voice.h"
#include "envelope.h"

namespace {

/**
 * @brief One voice wired like CSynthesizer1 does it, with its two LFOs, at 44100 Hz.
 */
struct VoiceRig {
    CLFO lfo[2];
    CVoice v;

    VoiceRig()
    {
        for (int i = 0; i < 2; i++) {
            lfo[i].setSamplingFreq(44100);
            lfo[i].setDestination(0);
        }
        v.setLfo(&lfo[0], &lfo[1]);
        v.m_vco.m_pitchEnvLevel = 0;
        v.m_vco.setVoiceNo(0);
        v.m_vco.setVCF(&v.m_vcf);
        v.m_vco.init();
        v.m_vco.setSamplingFreq(44100);
        v.m_vcf.setSamplingFreq(44100);
        v.m_vca.setSamplingFreq(44100);
        v.m_voiceNo = 0;
        v.m_pan = 0.0f;
        v.setUnisonInfo(&v, 1, 0);
        v.setUnisonPhaseShift(0);
    }
};

/**
 * @brief Creates a rig after setting up the engine globals (the VCO needs the shared tables).
 *
 * @return A new rig; delete it when done.
 */
VoiceRig* makeRig()
{
    t1::initEngine();
    return new VoiceRig();
}

/**
 * @brief Renders one block and reports whether it carried any signal.
 *
 * @param v     Voice to render.
 * @param size  Number of samples, at most 256.
 * @param[out] pProcessed  The silence flag returned by the voice.
 * @return Peak absolute sample value of the block (0 if the voice reported silence).
 */
float renderPeak(CVoice& v, int size, int* pProcessed)
{
    float buf[256];
    for (int i = 0; i < size; i++) buf[i] = 0.0f;
    v.processSignal(size, buf, pProcessed);
    if (*pProcessed) return 0.0f;
    float peak = 0.0f;
    for (int i = 0; i < size; i++) {
        float a = fabsf(buf[i]);
        if (a != a) return -1.0f;   // NaN
        if (a > peak) peak = a;
    }
    return peak;
}

}  // namespace

/** @brief A new voice is disabled, not holding a note, without auto portamento. */
TEST(voice, constructor_defaults)
{
    VoiceRig* r = makeRig();
    CVoice& v = r->v;
    CHECK_EQ(v.m_bEnable, 0);
    CHECK_EQ(v.m_bNoteOn, 0);
    CHECK_EQ(v.m_noteNo, 0);
    CHECK_EQ(v.m_bAutoPortament, 0);
    CHECK_EQ(v.getCurStatus(), VOICE_DISABLED);
    CHECK_EQ(v.isBusy(), 0);
    CHECK(v.m_vca.m_pInput == &v.m_vcf);
    CHECK(v.m_vcf.m_pInput == &v.m_vco);
    CHECK_EQ(v.m_vca.getEnvStatus(), ENV_OFF);
    delete r;
}

/** @brief A disabled voice reports silence and leaves the buffer untouched, even with a note on. */
TEST(voice, disabled_voice_is_silent)
{
    VoiceRig* r = makeRig();
    CVoice& v = r->v;
    v.setNoteInfo(60, 100);
    v.trigger();
    float buf[64];
    for (int i = 0; i < 64; i++) buf[i] = 0.25f;
    int processed = 0;
    CHECK_EQ(v.processSignal(64, buf, &processed), 64);
    CHECK_EQ(processed, 1);
    int untouched = 1;
    for (int i = 0; i < 64; i++) if (buf[i] != 0.25f) untouched = 0;
    CHECK(untouched);
    CHECK_EQ(v.getCurStatus(), VOICE_DISABLED);   // disabled wins over note on
    delete r;
}

/** @brief An enabled voice without a note is idle and renders silence. */
TEST(voice, enabled_idle_voice)
{
    VoiceRig* r = makeRig();
    CVoice& v = r->v;
    v.enable(1);
    CHECK_EQ(v.m_bEnable, 1);
    CHECK_EQ(v.getCurStatus(), VOICE_IDLE);
    int processed = 0;
    renderPeak(v, 128, &processed);
    CHECK_EQ(processed, 1);
    v.enable(0);
    CHECK_EQ(v.getCurStatus(), VOICE_DISABLED);
    delete r;
}

/** @brief setNoteInfo() stores note and velocity and retunes the oscillator (an octave doubles OSC1). */
TEST(voice, set_note_info_retunes)
{
    VoiceRig* r = makeRig();
    CVoice& v = r->v;
    v.setNoteInfo(60, 100);
    CHECK_EQ(v.m_noteNo, 60);
    CHECK_EQ(v.m_velocity, 100);
    CHECK_EQ(v.m_vco.m_note, 60);
    float f60 = v.m_vco.m_freq1;
    CHECK_CLOSE(f60, s1g->m_freqTable[60], 1e-3);
    v.setNoteInfo(72, 50);
    CHECK_EQ(v.m_vco.m_note, 72);
    CHECK_CLOSE(v.m_vco.m_freq1 / f60, 2.0, 1e-4);
    CHECK_EQ(v.getCurStatus(), VOICE_DISABLED);   // no trigger yet
    CHECK_EQ(v.m_bNoteOn, 0);
    delete r;
}

/** @brief trigger() holds the note, starts the amp and filter envelopes and passes the unison info. */
TEST(voice, trigger_starts_note)
{
    VoiceRig* r = makeRig();
    CVoice& v = r->v;
    v.enable(1);
    v.setUnisonInfo(&v, 4, 2);
    v.setUnisonPhaseShift(127);
    CHECK_EQ(v.m_unisonNum, 4);
    CHECK_EQ(v.m_unisonNo, 2);
    CHECK_CLOSE(v.m_unisonPhaseShift, 1.0, 1e-7);
    v.setNoteInfo(64, 90);
    v.trigger();
    CHECK_EQ(v.m_bNoteOn, 1);
    CHECK_EQ(v.getCurStatus(), VOICE_NOTEON);
    CHECK(v.isBusy());
    CHECK(v.m_vca.getEnvStatus() != ENV_OFF);
    CHECK(v.m_vcf.getEnvStatus() != ENV_OFF);
    CHECK_EQ(v.m_vcf.m_note, 64);
    CHECK_EQ(v.m_vco.m_unisonNum, 4);
    CHECK_EQ(v.m_vco.m_unisonNo, 2);
    CHECK_CLOSE(v.m_vco.m_unisonPhaseSpread, 1.0, 1e-7);
    delete r;
}

/** @brief A triggered voice produces a finite, non-silent signal. */
TEST(voice, triggered_voice_sounds)
{
    VoiceRig* r = makeRig();
    CVoice& v = r->v;
    v.enable(1);
    v.setNoteInfo(57, 127);
    v.trigger();
    float peak = 0.0f;
    int anySound = 0;
    for (int b = 0; b < 8; b++) {
        int processed = 1;
        float p = renderPeak(v, 256, &processed);
        CHECK(p >= 0.0f);           // no NaN
        CHECK(p < 100.0f);
        if (!processed) anySound = 1;
        if (p > peak) peak = p;
    }
    CHECK(anySound);
    CHECK(peak > 1e-3f);
    delete r;
}

/** @brief release() lets the note ring out: releasing, then idle and silent after the release time. */
TEST(voice, release_then_idle)
{
    VoiceRig* r = makeRig();
    CVoice& v = r->v;
    v.enable(1);
    v.m_vca.setSustain(1.0f);
    v.m_vca.setRelease(0.1f);
    v.setNoteInfo(60, 100);
    v.trigger();
    int processed;
    for (int b = 0; b < 20; b++) renderPeak(v, 256, &processed);
    CHECK_EQ(v.getCurStatus(), VOICE_NOTEON);
    v.release(0);
    CHECK_EQ(v.m_bNoteOn, 0);
    CHECK_EQ(v.getCurStatus(), VOICE_RELEASING);
    CHECK_EQ(v.m_vca.getEnvStatus(), ENV_RELEASE);
    CHECK(v.isBusy());
    int blocks = 0;
    while (v.isBusy() && blocks < 44100 * 5 / 256) {
        renderPeak(v, 256, &processed);
        blocks++;
    }
    CHECK_EQ(v.isBusy(), 0);
    CHECK_EQ(v.getCurStatus(), VOICE_IDLE);
    // 0.1 s release: done after more than ~0.05 s and well before 1 s
    CHECK(blocks * 256 > 44100 / 20);
    CHECK(blocks * 256 < 44100);
    renderPeak(v, 256, &processed);
    CHECK_EQ(processed, 1);
    delete r;
}

/** @brief Retriggering a held voice keeps it on and restarts the amp envelope attack. */
TEST(voice, retrigger_restarts_envelope)
{
    VoiceRig* r = makeRig();
    CVoice& v = r->v;
    v.enable(1);
    v.m_vca.setAttack(0.01f);
    v.m_vca.setSustain(0.5f);
    v.setNoteInfo(60, 100);
    v.trigger();
    int processed;
    for (int b = 0; b < 100; b++) renderPeak(v, 256, &processed);
    CHECK(v.m_vca.getEnvStatus() != ENV_ATTACK);
    v.trigger();
    CHECK_EQ(v.m_vca.getEnvStatus(), ENV_ATTACK);
    CHECK_EQ(v.getCurStatus(), VOICE_NOTEON);
    delete r;
}

/** @brief getPanVal() returns m_pan; setUnisonPhaseShift() maps 0..127 to 0..1. */
TEST(voice, pan_and_phase_shift)
{
    VoiceRig* r = makeRig();
    CVoice& v = r->v;
    v.m_pan = -0.75f;
    CHECK_CLOSE(v.getPanVal(), -0.75, 0);
    CSignalDevice* dev = &v;   // virtual, as the mixer calls it
    CHECK_CLOSE(dev->getPanVal(), -0.75, 0);
    v.setUnisonPhaseShift(0);
    CHECK_CLOSE(v.m_unisonPhaseShift, 0.0, 0);
    v.setUnisonPhaseShift(127);
    CHECK_CLOSE(v.m_unisonPhaseShift, 1.0, 1e-7);
    v.setUnisonPhaseShift(64);
    CHECK_CLOSE(v.m_unisonPhaseShift, 64 / 127.0, 1e-6);
    delete r;
}

/** @brief Portamento is always requested unless auto portamento is on, which glides only while held. */
TEST(voice, auto_portamento_only_legato)
{
    VoiceRig* r = makeRig();
    CVoice& v = r->v;
    v.enable(1);
    // normal: a glide is set up even without a held note
    v.setNoteInfo(60, 100);
    v.setNoteInfo(72, 100);
    CHECK(v.m_vco.m_portDiff != 0);

    // auto: no glide from a released state
    v.m_bAutoPortament = 1;
    v.setNoteInfo(60, 100);
    CHECK_EQ(v.m_vco.m_portDiff, 0);
    CHECK_EQ(v.m_vco.m_portCur, 0);
    v.trigger();
    // held: glide
    v.setNoteInfo(67, 100);
    CHECK(v.m_vco.m_portDiff < 0);    // going up: starts below the new pitch
    v.release(0);
    v.setNoteInfo(55, 100);
    CHECK_EQ(v.m_vco.m_portDiff, 0);
    delete r;
}

/** @brief changeLFOSetting() routes each LFO to the oscillator, filter or amplifier by destination. */
TEST(voice, lfo_routing)
{
    VoiceRig* r = makeRig();
    CVoice& v = r->v;
    CHECK(v.m_pLfo[0] == &r->lfo[0]);
    CHECK(v.m_pLfo[1] == &r->lfo[1]);
    CHECK(v.m_vca.m_pLfo[0] == &r->lfo[0]);
    CHECK(v.m_vcf.m_pLfo[1] == &r->lfo[1]);

    r->lfo[0].setEnable(1);
    r->lfo[0].setDestination(LFO_DEST_PW);
    v.changeLFOSetting(0);
    CHECK_EQ(v.m_vco.m_lfoDest[0], LFO_DEST_PW);
    CHECK_EQ(v.m_vco.m_bLfoEnable[0], 1);
    CHECK_EQ(v.m_vcf.m_bLfoEnable[0], 0);
    CHECK_EQ(v.m_vca.m_bLfoEnable[0], 0);

    r->lfo[1].setEnable(1);
    r->lfo[1].setDestination(LFO_DEST_FILTER);
    v.changeLFOSetting(1);
    CHECK_EQ(v.m_vco.m_lfoDest[1], 0);
    CHECK_EQ(v.m_vco.m_bLfoEnable[1], 0);
    CHECK_EQ(v.m_vcf.m_bLfoEnable[1], 1);
    CHECK_EQ(v.m_vca.m_bLfoEnable[1], 0);

    r->lfo[0].setDestination(LFO_DEST_AMP);
    v.changeLFOSetting(0);
    CHECK_EQ(v.m_vco.m_lfoDest[0], 0);
    CHECK_EQ(v.m_vca.m_bLfoEnable[0], 1);

    // a disabled LFO is routed nowhere
    r->lfo[0].setEnable(0);
    v.changeLFOSetting(0);
    CHECK_EQ(v.m_vca.m_bLfoEnable[0], 0);
    CHECK_EQ(v.m_vco.m_bLfoEnable[0], 0);
    delete r;
}

/** @brief An amp LFO modulates a held note: the output still stays finite. */
TEST(voice, amp_lfo_renders)
{
    VoiceRig* r = makeRig();
    CVoice& v = r->v;
    v.enable(1);
    v.m_vca.setSustain(1.0f);
    r->lfo[0].setEnable(1);
    r->lfo[0].setFreq(20.0f);
    r->lfo[0].setFreqMain(20.0f);
    r->lfo[0].setType(LFO_SQUARE);
    r->lfo[0].setDestination(LFO_DEST_AMP);
    r->lfo[0].setAmountDial(127);
    v.changeLFOSetting(0);
    v.setNoteInfo(60, 127);
    v.trigger();
    int processed;
    int sounding = 0;
    for (int b = 0; b < 40; b++) {
        float p = renderPeak(v, 256, &processed);
        CHECK(p >= 0.0f && p < 100.0f);
        if (!processed) sounding++;
        r->lfo[0].incCurTime(256);
    }
    CHECK(sounding > 0);
    delete r;
}
