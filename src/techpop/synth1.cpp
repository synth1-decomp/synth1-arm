/**
 * @file synth1.cpp
 * @brief CSynthesizer1: the synth engine.
 *
 * Signal chain setup, block rendering, parameter handling, patch version conversion,
 * plugin state (VST chunk / DXi data) save and restore, host parameter value
 * conversion, and the C interface functions declared in synth1if.h. The MIDI controller
 * handling is in synmidi.cpp.
 */

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "common.h"
#include "debug.h"
#include "synth1.h"


S1G* s1g;

int CSynthesizer1::m_bCanVSTHostChangePatch = TRUE;

// delay time table in 1/24 beat units
static const int g_nDelayBeat[] = {
    0, 1, 2, 3, 4, 6, 8, 9, 12, 16, 18, 21, 24, 32, 36, 42, 48, 72, 84, 96
};

CSynthesizer1::~CSynthesizer1()
{
}

// (the mixer loop as a while loop in its own block, and pSound as a local declared early,
// are significant for reproducing the released binary)
CSynthesizer1::CSynthesizer1()
{
    edPrintf("synth1() constructor\n");
    CSoundData* pSound;
    initCCMap();
    edPrintf("synth1() constructor a \n");

    m_pSynWin = NULL;
#ifdef SYNTH1_MODERN_BUILD
    // The original never sets this, so SetParameter() can post to a garbage pointer and crash the host
    m_hNotifyWnd = NULL;
#endif

    // voices -> mixer -> effector -> eq -> tone -> pan -> delay -> chorus
    m_mixer.setChannelNum(32);
    {
        int i = 0;
        while (i < 32) {
            m_mixer.inputMixDeviceIs(i, &m_voice[i]);
            i++;
        }
    }
    m_effector.inputDeviceIs(&m_mixer);
    m_eq.inputDeviceIs(&m_effector);
    m_eq.setMode(EQ_MODE_PEAKING);
    m_eq.setGain(0);
    m_eq.setFreq(1000.0f);
    m_eq.setQ(8.0f);
    m_toneEq.inputDeviceIs(&m_eq);
    m_toneEq.setMode(EQ_MODE_LOWPASS);
    m_toneEq.setFreq(10000.0f);
    m_pan.inputDeviceIs(&m_toneEq);
    m_pan.setLfo(&m_lfo[0], &m_lfo[1]);
    m_delay.inputDeviceIs(&m_pan);
    m_delay.setMixDelay(0);
    m_delayEq.m_bEnable = 0;
    m_delayEq.setFreq(10000.0f);
    m_delay.setInsertionFilter(&m_delayEq, NULL, NULL, NULL);
    m_chorus.inputDeviceIs(&m_delay);
    m_chorus.setDelaySigPointer(m_delay.m_pOut, &m_delay.m_bOutput);

    m_arp.setInst(this);
    m_arp.setKbdInfo(&m_kbdInfo);
    m_lfo[0].setSynth1(this);
    m_lfo[1].setSynth1(this);

    m_voiceCtl.m_pMixDevice = &m_mixer;
    m_voiceCtl.setPolyMode(0);
    m_voiceCtl.setUnisonMode(0);
    for (int i = 0; i < 32; i++) {
        m_voice[i].setLfo(&m_lfo[0], &m_lfo[1]);
        m_voice[i].enable(1);
        m_voice[i].m_vco.m_pitchEnvLevel = 0;
        m_voice[i].m_vco.setVoiceNo(i);
        m_voice[i].m_vco.setVCF(&m_voice[i].m_vcf);
        m_voice[i].m_vco.init();
    }
    m_voiceCtl.setKeyboardInfo(&m_kbdInfo);
    m_voiceCtl.setVoiceNum(32);
    for (int i = 0; i < 32; i++) {
        m_voiceCtl.setVoice(i, &m_voice[i]);
    }

    m_baseTempo = 120.0f;
    setVolume(64);
    m_lastParamID = -1;
    for (int i = 0; i < 2; i++) {
        m_midiCtrl[i].src = 0xb001;
        m_midiCtrl[i].sens = 0;
        m_midiCtrl[i].paramID = -1;
        m_midiCtrl[i].val = 0;
    }
    m_curPitchBend = 0;
    m_bPitchBendChanging = 0;
    enableBankSelect(getIniInt("enableBankSelect", 1));
    enableProgramChange(getIniInt("enableProgramChange", 1));
    m_bMidiLearning = 0;
    m_midiLearnParamID = -1;
    m_curVolume = 0;
    calcVolLPFCoef();

    m_soundLib.setReqProgNo(0);
    m_soundLib.setReqBankNo(0, NULL);
    pSound = &m_soundLib.m_sound;
    m_pCurSound = pSound;
    pSound->m_progNo = -1;
    createInitialSound();
    loadProgram(0);
    edPrintf("synth1() constructor end \n");
}

void CSynthesizer1::calcVolLPFCoef()
{
    double t = 1.0 / (m_samplingFreq * 0.002f);
    m_volLPFCoef = (float)(1.0 - fpow(0.5, t));
}

int CSynthesizer1::canVSTHostChangePatch()
{
    static BOOL bFirst;
    if (bFirst) return m_bCanVSTHostChangePatch;
    bFirst = TRUE;
    m_bCanVSTHostChangePatch = getIniInt("hostselectprogram", 0);
    return m_bCanVSTHostChangePatch;
}

void CSynthesizer1::createInitialSound()
{
    static const int initProgram[] = {
        0, 2, 45, 0, 76, 0, 1, 1, 2, 64, 3, 81, 4, 1, 5, 64,
        6, 0, 7, 0, 8, 64, 9, 0, 10, 0, 11, 64, 12, 0, 13, 0,
        71, 0, 72, 64, 91, 0, 95, 0, 96, 1, 97, 1, 14, 1, 15, 0,
        16, 64, 17, 32, 18, 64, 19, 81, 20, 14, 21, 128, 22, 64, 23, 0,
        24, 1, 25, 64, 26, 64, 27, 107, 28, 64, 29, 107, 30, 64, 59, 0,
        31, 1, 32, 0, 33, 11, 34, 64, 65, 1, 82, 0, 35, 8, 83, 66,
        36, 40, 98, 64, 37, 20, 66, 1, 64, 2, 52, 64, 53, 64, 54, 50,
        55, 64, 56, 40, 60, 64, 61, 64, 62, 64, 63, 64, 90, 64, 77, 0,
        78, 0, 79, 64, 80, 64, 81, 64, 38, 0, 94, 16, 39, 0, 74, 0,
        73, 0, 93, 2, 75, 22, 84, 64, 85, 24, 92, 0, 40, 12, 86, 0xb001,
        50, 74, 87, 44, 88, 0xb001, 51, 74, 89, 43, 57, 1, 41, 2, 42, 1,
        43, 64, 44, 0, 67, 0, 68, 0, 58, 1, 46, 5, 47, 1, 48, 64,
        49, 64, 69, 0, 70, 0,
    };

    m_initSound.m_version = 113;
    m_initSound.m_color = 0xffffffff;
    strcpy(m_initSound.m_name, "initial sound");
    for (unsigned int i = 0; i < sizeof(initProgram) / sizeof(int); i += 2) {
        m_initSound.setParamVal(initProgram[i], initProgram[i + 1]);
    }
}

void CSynthesizer1::setEnableBankSelect(int b)
{
    enableBankSelect(b);
    setIniInt("enableBankSelect", b);
}

void CSynthesizer1::setEnableProgramChange(int b)
{
    enableProgramChange(b);
    setIniInt("enableProgramChange", b);
}

void CSynthesizer1::setSynWin(void* pWin)
{
    m_pSynWin = pWin;
}

int CSynthesizer1::processSignal(int size, float* pBuf, int* pProcessed)
{
    *pProcessed = 1;
    return size;
}

int CSynthesizer1::processSignalW(int size, float* pBuf, int* pProcessed)
{
    int bArp = m_arp.isPlaying();
    if (m_bSSE == 0) {
        *pProcessed = 1;
        return size;
    }
    int rest = size;
    float* p = pBuf;
    int bEnd = FALSE;
    while (rest && !bEnd) {
        // process up to the next event
        int len = rest;
        if (bArp) {
            len = m_arp.getNextWaitTime();
            if (len == 0) len = m_arp.processArpeggiator();
            if (len > rest) len = rest;
        }
        m_voiceCtl.controlVoiceTrigger();
        if (m_bPitchBendChanging) {
            if (len > 32) len = 32;
            processChangeParams();
        }
        for (int i = 0; i < 2; i++) {
            if (m_lfo[i].m_bEnable == 1 &&
                (m_lfo[i].m_type == LFO_SAMPLEHOLD || m_lfo[i].m_type == LFO_SMOOTHRANDOM)) {
                if (len >= m_lfo[i].getNextCycleRestTime()) len = m_lfo[i].getNextCycleRestTime();
            }
        }
        len = m_chorus.processSignalW(len, p, pProcessed);
        if (*pProcessed) {
            bEnd = TRUE;
            if (rest != size) {
                // silent after some output: fill the rest with zero
                for (int i = 0; i < len * 2; i += 2) {
                    p[i] = 0;
                    p[i + 1] = 0;
                }
                *pProcessed = 0;
            }
        }
        if (bArp) m_arp.incCurTime(len);
        m_lfo[0].incCurTime(len);
        m_lfo[1].incCurTime(len);
        rest -= len;
        p += len * 2;
    }
    size -= rest;

    if (*pProcessed) {
        m_curVolume = m_volume;
    } else if (m_curVolume != m_volume) {
        processMainVolume(pBuf, m_volume, size * 2);
    } else {
        mulArray2SSE(pBuf, m_volume, size * 2);
    }

    if (m_pSynWin && m_voiceCtl.isVoicesStatusChanged()) {
        PostMessageControl(m_pSynWin, 0x723, 0, 0);
    }
    return size;
}

void CSynthesizer1::processChangeParams()
{
    // pitch bend moves by one step per block
    if (m_curPitchBend < m_targetPitchBend) m_curPitchBend++;
    else if (m_curPitchBend > m_targetPitchBend) m_curPitchBend--;
    short pb = m_pitchBendRange * m_curPitchBend * 128 / (63 * 12);
    for (int i = 0; i < 32; i++) {
        m_voice[i].m_vco.m_pitchEnvLevel = pb;
    }
    if (m_curPitchBend == m_targetPitchBend) m_bPitchBendChanging = 0;
}

void CSynthesizer1::processMainVolume(float* pBuf, float vol, int size)
{
    float cur = m_curVolume;
    float coef = m_volLPFCoef;
    for (int i = 0; i < size; i++) {
        cur += (vol - cur) * coef;
        pBuf[i] *= cur;
    }
    if (fabsf(vol - cur) < 1.0f / 65536) cur = vol;
    m_curVolume = cur;
}

CSoundData* CSynthesizer1::getCurrentSound()
{
    return m_pCurSound;
}

extern "C" int Synthesizer1_getCurrentSoundBankNo(CSynthesizer1* pSynth)
{
    return pSynth->getCurrentSound()->m_bankNo;
}

extern "C" char* Synthesizer1_getZipFolderPath(CSynthesizer1* pSynth)
{
    return CSoundLibrary::getZipFolderPath();
}

extern "C" int Synthesizer1_getEnableBankSelect(CSynthesizer1* pSynth)
{
    return pSynth->getEnableBankSelect();
}

extern "C" int Synthesizer1_getEnableProgramChange(CSynthesizer1* pSynth)
{
    return pSynth->getEnableProgramChange();
}

extern "C" CSoundData* Synthesizer1_getCurrentSound(CSynthesizer1* pSynth)
{
    return pSynth->getCurrentSound();
}

extern "C" void Synthesizer1_setEnableBankSelect(CSynthesizer1* pSynth, int b)
{
    pSynth->setEnableBankSelect(b);
}

extern "C" void Synthesizer1_setEnableProgramChange(CSynthesizer1* pSynth, int b)
{
    pSynth->setEnableProgramChange(b);
}

extern "C" void Synthesizer1_setCCMap(CSynthesizer1* pSynth, int* pMap)
{
    pSynth->setCCMap(pMap);
}

extern "C" void Synthesizer1_changeBankFolder(CSynthesizer1* pSynth, int no, char* path)
{
    pSynth->m_soundLib.changeBankFolder(no, path);
}

extern "C" void Synthesizer1_updateZipFolderPath(CSynthesizer1* pSynth, char* path)
{
    CSoundLibrary::updateZipFolderPath(path);
}

extern "C" void Synthesizer1_setMasterTune(CSynthesizer1* pSynth, int tune)
{
    CSynthesizer1::setMasterTune(tune);
}

void CSynthesizer1::setMasterTune(int tune)
{
    CVco::makeFreqTable(tune);
}

extern "C" int SoundData_getBankNo(CSoundData* pSound)
{
    return pSound->m_bankNo;
}

extern "C" int SoundData_getProgramNo(CSoundData* pSound)
{
    return pSound->m_progNo;
}

extern "C" char* SoundData_getName(CSoundData* pSound)
{
    return pSound->m_name;
}

extern "C" unsigned int SoundData_getColor(CSoundData* pSound)
{
    return pSound->m_color;
}

void CSynthesizer1::setCommonParam(int id, int val, int bNotify)
{
    switch (id) {
    case 254:   // master volume
        {
            m_volumeVal = val;
            float v = val / 127.0f;
            float vol = 0;
            if (v != 0) {
                double x = v + 0.01;
                vol = (float)fpow(10.0, (x * 20.0 + -1.0 / x - 10.0) / 20.0);
            }
            m_volume = vol;
        }
        break;
    case 251:   // bank
        m_soundLib.setReqBankNo(val, NULL);
        return;
    case 252:   // program
        m_soundLib.setReqProgNo(val);
        return;
    }
    if (id != 251 && id != 252 && bNotify && m_pSynWin) {
        PostMessageControl(m_pSynWin, 0x720, id, val);
    }
}

void CSynthesizer1::setSoundParam(int id, int val, int bNotify)
{
    if (m_lastParamID == id && m_lastParamVal == val) return;
    m_lastParamID = id;
    m_lastParamVal = val;
    m_pCurSound->setParamVal(id, val);
    setSoundParam2Engine(id, val);
    if (bNotify && m_pSynWin) {
        PostMessageControl(m_pSynWin, 0x720, id, val);
    }
}

void CSynthesizer1::setSoundParam2Engine(int id, int val)
{
    setSoundParam2EngineMain(id, val);
}

// delay time from the tempo (g_nDelayBeat is in 1/24 beats)
void CSynthesizer1::setDelayTime(int val)
{
    m_delayTimeVal = val;
    int n = val;
    n = MAX(0, MIN(n, 19));
    float sec = g_nDelayBeat[n] * 60 / (m_baseTempo * 24.0f);
    m_delay.setDelayTime(sec);
}

void CSynthesizer1::setDelayToneVal(int val)
{
    if (val == 64) {
        m_delayEq.m_bEnable = 0;
        return;
    }
    m_delayEq.m_bEnable = 1;
    if (val < 64) {
        // high cut
        m_delayEq.setMode(EQ_MODE_LOWPASS);
        m_delayEq.setFreq(fpow(110.0, val / 63.0f) * 200.0);
    } else {
        // low cut
        m_delayEq.setMode(EQ_MODE_HIGHPASS);
        m_delayEq.setFreq(fpow(800.0, (val - 65) / 62.0f) * 10.0);
    }
}

void CSynthesizer1::setEFX1Enable(int b)
{
    m_effector.setEnable(b != 0);
}

void CSynthesizer1::setToneVal(int val)
{
    if (val <= 64) {
        m_toneEq.setMode(EQ_MODE_LOWPASS);
        m_toneEq.setFreq(fpow(110.0, val / 64.0f) * 200.0);
    } else {
        m_toneEq.setMode(EQ_MODE_HIGHPASS);
        m_toneEq.setFreq(fpow(800.0, (val - 64) / 64.0f) * 10.0);
    }
}

void CSynthesizer1::setLFOEnable(int no, int b)
{
    m_lfo[no].setEnable(b == 1);
    for (int i = 0; i < 32; i++) {
        m_voice[i].changeLFOSetting(no);
    }
    m_pan.changeLFOSetting(no);
}

void CSynthesizer1::setLFODest(int no, int dest)
{
    m_lfo[no].setDestination(dest);
    for (int i = 0; i < 32; i++) {
        m_voice[i].changeLFOSetting(no);
    }
    m_pan.changeLFOSetting(no);
}

void CSynthesizer1::setLFOSpeed(int no, int val)
{
    m_lfo[no].setSpeedDial(val);
}

void CSynthesizer1::setLFODepth(int no, int val)
{
    m_lfo[no].setAmountDial(val);
}

void CSynthesizer1::setLFOTempoSync(int no, int b)
{
    m_lfo[no].setTempoSync(b != 0);
}

void CSynthesizer1::setLFOKeySync(int no, int b)
{
    m_lfo[no].setKeySync(b != 0);
}

/**
 * @brief Converts the delay time spread parameter to a left/right time offset.
 *
 * 64 gives 0; the magnitude grows exponentially from 0.1 ms to 0.1 s towards either end,
 * negative below 64.
 *
 * @param val  Spread, 0..127.
 * @return Time offset in seconds, -0.1..0.1.
 */
inline float calcDelaySpread(int val)
{
    float bal = 0;
    if (val != 64) {
        float x = fabsf((float)((val - 64) / 63.0));
        if (x > 1.0f) x = 1.0f;
        bal = (float)(fpow(1000.0, x) * 0.0001);
        if (val < 64) bal = -bal;
    }
    return bal;
}

/**
 * @brief Converts the chorus delay time parameter to seconds.
 *
 * Exponential from 0.05 ms up to 15 ms over the lower half, linear (val/127 * 30 ms)
 * over the upper half.
 *
 * @param val  Delay time, 0..127.
 * @return Delay time in seconds.
 */
inline float calcChorusDelayTime(int val)
{
    float x = val / 127.0f;
    double ms;
    if (x >= 0.5) ms = x * 30.0f;
    else ms = fpow(300.0, pow(x * 2.0, 0.6)) * 0.05;
    return (float)(ms / 1000.0);
}

/**
 * @brief Loop header over the 32 voices (loop variable i).
 */
#define FOR_VOICES  for (int i = 0; i < 32; i++)

// apply a sound parameter to the engine
void CSynthesizer1::setSoundParam2EngineMain(int id, int val)
{
    int min, max;
    getParamValRange(id, &min, &max);
    if (!(min == -1 && max == -1)) {
        // add the midi controller offsets
        float v = (float)val;
        for (int i = 0; i < 2; i++) {
            if (m_midiCtrl[i].paramID == id) {
                v += m_midiCtrl[i].val * (float)(max - min + 1) * m_midiCtrl[i].sens;
            }
        }
        val = ftoi(v);
        val = MIN(max, MAX(min, val));
    }

    switch (id) {
    case 0:     // osc1 shape
        FOR_VOICES m_voice[i].m_vco.setShape(val);
        break;
    case 1:     // osc2 shape
        FOR_VOICES m_voice[i].m_vco.setShape2(val);
        break;
    case 3:     // osc2 fine tune
        if (val >= 62 && val <= 66) {
            FOR_VOICES m_voice[i].m_vco.setOsc2Tune(0);
        } else if (val < 62) {
            FOR_VOICES m_voice[i].m_vco.setOsc2Tune(val - 62);
        } else {
            FOR_VOICES m_voice[i].m_vco.setOsc2Tune(val - 66);
        }
        break;
    case 4:     // osc2 kbd track
        FOR_VOICES m_voice[i].m_vco.setOsc2KbdTrack(val);
        break;
    case 5:     // osc mix
        {
            float mix = val / 127.0f;
            FOR_VOICES m_voice[i].m_vco.setMix(mix);
        }
        break;
    case 6:     // osc2 sync
        FOR_VOICES m_voice[i].m_vco.setSync(val);
        break;
    case 7:     // osc2 ring modulation
        FOR_VOICES m_voice[i].m_vco.setRing(val);
        break;
    case 8:     // osc pulse width
        {
            float pw = val / 127.0f;
            FOR_VOICES m_voice[i].m_vco.setPW(pw);
        }
        break;
    case 9:     // osc key shift
        FOR_VOICES m_voice[i].m_vco.setKeyShift(val);
        break;
    case 10:    // osc mod env on/off
        FOR_VOICES m_voice[i].m_vco.setOsc2PitchEnvEnable(val);
        break;
    case 11:    // osc mod env amount
        FOR_VOICES m_voice[i].m_vco.setOsc2PitchAmount(val);
        break;
    case 12:    // osc mod env attack
        FOR_VOICES m_voice[i].m_vco.setOsc2PitchAttack((float)(fpow(56000.0, val / 127.0f) * 0.0005));
        break;
    case 13:    // osc mod env decay
        FOR_VOICES m_voice[i].m_vco.setOsc2PitchDecay((float)(fpow(6000.0, val / 127.0f) * 0.01));
        break;
    case 14:    // filter type
        FOR_VOICES m_voice[i].m_vcf.setType(val);
        break;
    case 15:    // filter attack
        FOR_VOICES m_voice[i].m_vcf.setAttack((float)(fpow(56000.0, val / 127.0f) * 0.0005));
        break;
    case 16:    // filter decay
        FOR_VOICES m_voice[i].m_vcf.setDecay((float)(fpow(6000.0, val / 127.0f) * 0.01));
        break;
    case 17:    // filter sustain
        {
            float s = val / 127.0f;
            FOR_VOICES m_voice[i].m_vcf.setSustain(s);
        }
        break;
    case 18:    // filter release
        FOR_VOICES m_voice[i].m_vcf.setRelease((float)(fpow(6000.0, val / 127.0f) * 0.01));
        break;
    case 19:    // filter freq
        {
            float f = val / 127.0f;
            FOR_VOICES m_voice[i].m_vcf.setFreq(f);
        }
        break;
    case 20:    // filter resonance
        {
            float r = val / 127.0f;
            FOR_VOICES m_voice[i].m_vcf.setResonance(r);
        }
        break;
    case 21:    // filter amount
        {
            float a = (val - 63) * 0.015625f;
            FOR_VOICES m_voice[i].m_vcf.setAmount(a);
        }
        break;
    case 22:    // filter kbd track
        {
            float t = val / 127.0f;
            FOR_VOICES m_voice[i].m_vcf.setKbdTrack(t);
        }
        break;
    case 23:    // filter saturation
        FOR_VOICES m_voice[i].m_vcf.setDistortion((float)(exp((val / 127.0f - 1.0f) * 5.8) * val / 127.0));
        break;
    case 24:    // filter velocity switch
        FOR_VOICES m_voice[i].m_vcf.setVelocitySensEnable(val);
        break;
    case 25:    // amp attack
        FOR_VOICES m_voice[i].m_vca.setAttack((float)(fpow(56000.0, val / 127.0f) * 0.0005));
        break;
    case 26:    // amp decay
        FOR_VOICES m_voice[i].m_vca.setDecay((float)(fpow(6000.0, val / 127.0f) * 0.01));
        break;
    case 27:    // amp sustain
        {
            float s = val / 127.0f;
            FOR_VOICES m_voice[i].m_vca.setSustain((float)(s == 0 ? 0 : fpow(2048.0, pow(val / 127.0, 0.3)) / 2048.0));
        }
        break;
    case 28:    // amp release
        FOR_VOICES m_voice[i].m_vca.setRelease((float)(fpow(6000.0, val / 127.0f) * 0.01));
        break;
    case 29:    // amp gain
        {
            double g = val / 127.0;
            FOR_VOICES m_voice[i].m_vca.setGain((float)(g != 0 ? fpow(2048.0, pow(g, 0.3)) / 2048.0 : 0));
        }
        break;
    case 30:    // amp velocity sens
        {
            float s = val / 127.0f;
            FOR_VOICES m_voice[i].m_vca.setVelocitySens(s);
        }
        break;
    case 31:    // arpeggiator type
        m_arp.setType(val);
        break;
    case 32:    // arpeggiator oct range
        m_arp.setOctRange(val);
        break;
    case 33:    // arpeggiator beat
        m_arp.setBeat(val);
        break;
    case 34:    // arpeggiator gate
        m_arp.setGateTime(val);
        break;
    case 76:    // osc1 detune
        {
            float d = val / 127.0f * 20.0f;
            FOR_VOICES m_voice[i].m_vco.setSSDetune(d);
        }
        break;
    case 2:     // osc2 pitch
        {
            float pitch = (float)((val * 120) / 127 - 60) * 5.0f / 60.0f;
            FOR_VOICES m_voice[i].m_vco.setOsc2Pitch(pitch);
        }
        break;
    case 35:    // delay time
        {
            m_delayTimeVal = val;
            int n = val;
            n = MAX(0, MIN(n, 19));
            float sec = g_nDelayBeat[n] * 60 / (m_baseTempo * 24.0f);
            m_delay.setDelayTime(sec);
        }
        break;
    case 83:    // delay time spread
        m_delay.setTimeBalance(calcDelaySpread(val));
        break;
    case 36:    // delay feedback
        m_delay.setFeedback(val / 127.0f);
        break;
    case 37:    // delay dry/wet
        m_delay.setDelayLevel(val);
        break;
    case 38:    // play mode
        m_voiceCtl.setPolyMode(val);
        break;
    case 40:    // pitch bend range
        m_pitchBendRange = (short)val;
        break;
    case 41:    // lfo1 destination
        setLFODest(0, val);
        break;
    case 42:    // lfo1 type
        m_lfo[0].setType(val);
        break;
    case 43:    // lfo1 speed
        m_lfo[0].setSpeedDial(val);
        break;
    case 44:    // lfo1 depth
        m_lfo[0].setAmountDial(val);
        break;
    case 45:    // osc1 FM
        FOR_VOICES m_voice[i].m_vco.setFMAmount(val);
        break;
    case 47:    // lfo2 type
        m_lfo[1].setType(val);
        break;
    case 48:    // lfo2 speed
        m_lfo[1].setSpeedDial(val);
        break;
    case 49:    // lfo2 depth
        m_lfo[1].setAmountDial(val);
        break;
    case 50:    // midi ctrl sens1
        setMidiCtrlSens(0, (float)(val - (val < 64 ? 63 : 64)) / 63.0f);
        break;
    case 52:    // chorus delay time
        m_chorus.setDelayTime(calcChorusDelayTime(val));
        break;
    case 53:    // chorus depth
        m_chorus.setDepth((float)((fpow(100.0, val / 127.0f) - 1.0) / 99.0));
        break;
    case 54:    // chorus rate
        m_chorus.setRate(val == 0 ? 0.01f : (float)(fpow(20000.0, val / 127.0f) * 0.02));
        break;
    case 55:    // chorus feedback
        m_chorus.setFeedback((val - 64) * 0.015625f * 0.99f);
        break;
    case 56:    // chorus level
        m_chorus.setLevel(val / 127.0f);
        break;
    case 57:    // lfo1 on/off
        setLFOEnable(0, val);
        break;
    case 58:    // lfo2 on/off
        setLFOEnable(1, val);
        break;
    case 59:    // arpeggiator on/off
        m_arp.setEnable(val != 0);
        m_voiceCtl.setKeyboardInfo(val ? &m_kbdInfoArp : &m_kbdInfo);
        break;
    case 60:    // equalizer tone
        setToneVal(val);
        break;
    case 61:    // equalizer freq
        m_eq.setFreq((float)(fpow(320.0, val / 127.0f) * 50.0));
        break;
    case 62:    // equalizer level
        m_eq.setGain((val - 64) / 127.0f * 50.0f);
        break;
    case 63:    // equalizer Q
        m_eq.setQ((float)fpow(10.0, val / 127.0f));
        break;
    case 64:    // chorus type
        m_chorus.setKind(val);
        break;
    case 65:    // delay on/off
        m_delay.setEnable(val != 0);
        break;
    case 66:    // chorus on/off
        m_chorus.setEnable(val != 0);
        break;
    case 67:    // lfo1 tempo sync
        m_lfo[0].setTempoSync(val != 0);
        break;
    case 69:    // lfo2 tempo sync
        m_lfo[1].setTempoSync(val != 0);
        break;
    case 70:    // lfo2 key sync
        m_lfo[1].setKeySync(val != 0);
        break;
    case 71:    // osc mod dest
        FOR_VOICES m_voice[i].m_vco.setOscModEnvDest(val);
        break;
    case 72:    // osc1,2 fine tune
        if (val >= 62 && val <= 66) {
            FOR_VOICES m_voice[i].m_vco.setCommonTune(0);
        } else if (val < 62) {
            FOR_VOICES m_voice[i].m_vco.setCommonTune(val - 62);
        } else {
            FOR_VOICES m_voice[i].m_vco.setCommonTune(val - 66);
        }
        break;
    case 75:    // unison detune
        m_voiceCtl.setUnisonDetune(val);
        break;
    case 77:    // effect on/off
        m_effector.setEnable(val != 0);
        break;
    case 79:    // effect control1
        m_effector.setCont1(val);
        break;
    case 80:    // effect control2
        m_effector.setCont2(val);
        break;
    case 81:    // effect level/mix
        m_effector.setLevel(val);
        break;
    case 82:    // delay type
        m_delay.setType(val);
        break;
    case 85:    // unison pitch
        m_voiceCtl.setUnisonPitch(val);
        break;
    case 86:    // midi ctrl src1
        setMidiCtrlSrc(0, val);
        break;
    case 88:    // midi ctrl src2
        setMidiCtrlSrc(1, val);
        break;
    case 91:    // osc phase shift
        FOR_VOICES m_voice[i].m_vco.setPhaseShift(val);
        break;
    case 92:    // unison phase shift
        FOR_VOICES m_voice[i].setUnisonPhaseShift(val);
        break;
    case 93:    // unison voice num
        m_voiceCtl.setUnisonVoiceNum(val);
        break;
    case 94:    // polyphony
        m_voiceCtl.setPolyNum(val);
        break;
    case 96:    // osc1 sub shape
        FOR_VOICES m_voice[i].m_vco.setSubWaveForm(val);
        break;
    case 98:    // delay tone
        setDelayToneVal(val);
        break;

    // parameters added later
    case 46:    // lfo2 destination
        setLFODest(1, val);
        break;
    case 68:    // lfo1 key sync
        m_lfo[0].setKeySync(val != 0);
        break;
    case 73:    // unison mode
        {
            m_voiceCtl.setUnisonMode(val);
            int bStereo = (val == 1);
            m_effector.setStereoMode(bStereo);
            m_eq.setStereoMode(bStereo);
            m_toneEq.setStereoMode(bStereo);
        }
        break;
    case 74:    // portament auto mode
        FOR_VOICES m_voice[i].m_bAutoPortament = val;
        break;
    case 78:    // effect type
        m_effector.setType(val);
        break;
    case 87:    // midi ctrl assign1
        setMidiCtrlAssignParamID(0, val);
        break;
    case 89:    // midi ctrl assign2
        setMidiCtrlAssignParamID(1, val);
        break;
    case 90:    // pan
        m_pan.setBalance((float)(val - 64) / (val > 64 ? 63.0f : 64.0f));
        break;
    case 95:    // osc1 sub gain
        {
            float g = (val * 4) / 127.0f;
            FOR_VOICES m_voice[i].m_vco.setSubOscGain(g);
        }
        break;
    case 97:    // osc1 sub octave
        FOR_VOICES m_voice[i].m_vco.setSubOct(val);
        break;
    case 39:    // portament time
        FOR_VOICES m_voice[i].m_vco.setPortamentTime((float)(exp((val / 127.0f - 1.0f) * 2.5) * val / 127.0 * 2.5));
        break;
    case 84:    // unison pan spread
        m_voiceCtl.setVoicePan((val - 64) * 0.015625f);
        break;
    case 51:    // midi ctrl sens2
        setMidiCtrlSens(1, (float)(val - (val < 64 ? 63 : 64)) / 63.0f);
        break;
    }
}

int CSynthesizer1::isLFOTempoSync(int no)
{
    return m_lfo[no].m_bTempoSync;
}

float CSynthesizer1::getBaseTempo()
{
    return m_baseTempo;
}

void CSynthesizer1::setDelayTempoSync(int b)
{
}

void CSynthesizer1::noteOn(int nn, int vel)
{
    m_kbdInfo.onKey(nn, vel);
    if (m_kbdInfo.getKeyOnNum() == 1) {
        // first key
        if (m_arp.isPlaying()) m_arp.timingReset(vel);
        if (m_lfo[0].m_bKeySync) m_lfo[0].reset();
        if (m_lfo[1].m_bKeySync) m_lfo[1].reset();
    }
}

void CSynthesizer1::noteOff(int nn)
{
    m_kbdInfo.offKey(nn);
    if (m_arp.isPlaying() && m_kbdInfo.getKeyOnNum() == 0) {
        m_arp.timingReset(0);
    }
}

void CSynthesizer1::setSustainEnable(int b)
{
    m_kbdInfo.setDumper(b);
    if (m_arp.isPlaying() && m_kbdInfo.getKeyOnNum() == 0) {
        m_arp.timingReset(0);
    }
}

void CSynthesizer1::noteOnFromArp(int nn, int vel)
{
    m_kbdInfoArp.onKey(nn, vel);
}

void CSynthesizer1::noteOffFromArp(int nn)
{
    m_kbdInfoArp.offKey(nn);
}

void CSynthesizer1::receiveProgramChange(int progNo)
{
    m_soundLib.setReqProgNo(progNo);
    loadProgram(0);
}

void CSynthesizer1::receiveBankChange(int bankNo)
{
    m_soundLib.setReqBankNo(bankNo, NULL);
}

void CSynthesizer1::getProgramName(int bankNo, char* bankPath, int progNo, char* name)
{
    CSoundLibrary::getProgramNameColor(bankNo, bankPath, progNo, name, NULL, progNo == 0);
}

void CSynthesizer1::setVolume(unsigned char v)
{
    setCommonParam(254, v, 1);
}

void CSynthesizer1::setPitchBend(short v)
{
    short pb = v / 128;
    if (pb == -64) pb = -63;
    m_targetPitchBend = pb;
    m_bPitchBendChanging = 1;
}

void CSynthesizer1::convertParams(CSoundData* pSound)
{
    int ver = pSound->m_version;
    if (ver >= 113) {
        pSound->m_version = 113;
        return;
    }
    CSoundData sound;
    int id, val, newVal;
    for (int i = 0; i < pSound->getParamsNum(); i++) {
        pSound->enumParam(i, &id, &val);
        if (id < 99 || id > 250) {
            newVal = convertSingleParam(id, val, ver, pSound, &m_initSound);
            if (newVal != val) {
                sound.setParamVal(id, newVal);
            }
        }
    }
    for (int i = 0; i < sound.getParamsNum(); i++) {
        sound.enumParam(i, &id, &newVal);
        pSound->setParamVal(id, newVal);
    }
    pSound->m_version = 113;
}

void CSynthesizer1::loadProgram(int notifyParam)
{
    if (m_soundLib.isReqPatchLoaded()) return;
    m_soundLib.loadProgram(&m_initSound);
    convertParams(m_pCurSound);
    for (int i = 0; i < m_pCurSound->getParamsNum(); i++) {
        int id, val;
        m_pCurSound->enumParam(i, &id, &val);
        if (id < 99 || id > 250) {
            setSoundParam(id, val, 0);
        }
    }
    if (m_pSynWin) {
        PostMessageControl(m_pSynWin, 0x721, notifyParam, (LPARAM)m_pCurSound);
    }
    for (int i = 0; i < 32; i++) {
        m_voice[i].m_vco.initFreePhase();
    }
}

// convert a parameter value of an old patch version to the current one
int CSynthesizer1::convertSingleParam(int id, int val, int ver, CSoundData* pSrc, CSoundData* pDst)
{
    switch (ver) {
    case 100:
        val = convertSingleParam100To105(id, val, pDst);
    case 105:
    case 106:
        val = convertSingleParam105To107(id, val, pDst);
        val = convertSingleParam107To1071(id, val);
    case 107:
    case 108:
        val = convertSingleParam1071To109(id, val, pSrc, pDst);
    case 109:
        val = convertSingleParam109To111(id, val, pSrc, pDst);
    case 111:
        val = convertSingleParam111To112(id, val, pSrc, pDst);
    case 112:
        val = convertSingleParam112To113(id, val, pSrc, pDst);
    }
    return val;
}

int CSynthesizer1::convertSingleParam100To105(int id, int val, CSoundData* pDst)
{
    int i;
    switch (id) {
    case 3:     // osc2 fine tune
        {
            // old: +-1 semitone
            float semi = (float)(val / 127.0 - 0.5);
            semi = semi * 2;
            for (int k = -1; k <= 1; k++) {
                if (k - semi < 0.03 && k - semi > -0.03) {
                    semi = (float)k;
                    break;
                }
            }
            double target = fpow(2.0, semi / 12.0);
            double minDiff = 999.0;
            int i;
            for (i = 0; i < 128; i++) {
                int cent;
                if (i >= 62 && i <= 66) cent = 0;
                else cent = (i < 62) ? i - 62 : i - 66;
                double d = fabs(fpow(2.0, cent / 1200.0) - target);
                if (d > minDiff) break;
                minDiff = d;
            }
            return i - 1;
        }
    case 12:    // osc mod env attack
    case 15:    // filter attack
    case 25:    // amp attack
        {
            double x = val / 127.0;
            double target;
            double minDiff = 999.0;
            if (x >= 0.5) {
                target = fpow(30.0, fpow(x, 4.6)) - 1.0;
            } else {
                target = fpow(1.75, fpow(x, 2.0)) - 1.0 + 0.0005;
            }
            for (i = 0; i < 128; i++) {
                double d = fabs((float)(fpow(56000.0, i / 127.0) * 0.0005) - target);
                if (d > minDiff) break;
                minDiff = d;
            }
            return i - 1;
        }
    case 27:    // amp sustain
    case 29:    // amp gain
        {
            double target = val / 127.0;
            double minDiff = 999.0;
            for (i = 0; i < 128; i++) {
                double x = i / 127.0;
                double v = (x == 0) ? 0 : fpow(2048.0, pow(x, 0.3)) / 2048.0;
                double d = fabs((float)v - target);
                if (d > minDiff) break;
                minDiff = d;
            }
            return i - 1;
        }
    case 30:    // amp velocity sens
        {
            double target = -(val / 127.0) * 0.9 + 1.0;
            double minDiff = 999.0;
            for (i = 0; i < 128; i++) {
                double d = fabs(fpow(10.0, i / 127.0 * -30.0 / 20.0) - target);
                if (d > minDiff) break;
                minDiff = d;
            }
            return i - 1;
        }
    case 43:    // lfo1 speed
    case 48:    // lfo2 speed
        {
            double target = fpow(20.0, val / 127.0) - 0.9833333333333333;
            double minDiff = 999.0;
            for (i = 0; i < 128; i++) {
                double d = fabs((float)(fpow(1500.0, i / 127.0) * (1.0 / 12.0)) - target);
                if (d > minDiff) break;
                minDiff = d;
            }
            return i - 1;
        }
    case 67:
    case 68:
    case 69:
    case 70:
    case 71:
    case 72:
        return pDst->getParamVal(id);
    }
    return val;
}

int CSynthesizer1::convertSingleParam105To107(int id, int val, CSoundData* pDst)
{
    int i;
    switch (id) {
    case 21:    // filter amount
        {
            double target = (float)val / 127.0;
            double minDiff = 999.0;
            for (i = 0; i < 128; i++) {
                double d = fabs((i - 63) * 0.015625f - target);
                if (d > minDiff) break;
                minDiff = d;
            }
            return i - 1;
        }
    case 37:    // delay dry/wet
        return val / 2;
    case 52:    // chorus delay time
        {
            double x = val / 127.0;
            double t;
            if (x >= 0.5) t = x * 30.0;
            else t = fpow(50.0, x * 2) * 0.3;
            double target = t / 1000.0;
            double minDiff = 999.0;
            for (i = 0; i < 128; i++) {
                x = i / 127.0;
                if (x >= 0.5) t = x * 30.0;
                else t = fpow(300.0, pow(x * 2, 0.6)) * 0.05;
                double d = fabs((float)(t / 1000.0) - target);
                if (d > minDiff) break;
                minDiff = d;
            }
            return i - 1;
        }
    case 54:    // chorus rate
        {
            int i;
            double target = fpow(10.0, fpow(val / 127.0, 1.8)) - 1.0 + 0.05;
            double minDiff = 999.0;
            for (i = 0; i < 128; i++) {
                double rate = 0.01;
                if (i != 0) rate = fpow(20000.0, i / 127.0f) * 0.02;
                double d = fabs((float)rate - target);
                if (d > minDiff) break;
                minDiff = d;
            }
            return i - 1;
        }
    case 55:    // chorus feedback
        {
            double target = -(float)(val / 127) * 0.9;
            double minDiff = 999.0;
            for (i = 0; i < 128; i++) {
                double d = fabs((i - 64) * 0.015625f * 0.99f - target);
                if (d > minDiff) break;
                minDiff = d;
            }
            return i - 1;
        }
    case 13:    // osc mod env decay
    case 16:    // filter decay
    case 18:    // filter release
    case 26:    // amp decay
    case 28:    // amp release
        {
            double target = fpow(60.0, fpow(val / 127.0, 2.95)) - 1.0;
            double minDiff = 999.0;
            for (i = 0; i < 128; i++) {
                double d = fabs((float)(fpow(6000.0, i / 127.0) * 0.01) - target);
                if (d > minDiff) break;
                minDiff = d;
            }
            return i - 1;
        }
    case 75:
    case 76:
    case 77:
    case 78:
    case 79:
    case 80:
    case 81:
    case 82:
    case 83:
    case 84:
    case 85:
    case 90:
        return pDst->getParamVal(id);
    }
    return val;
}

int CSynthesizer1::convertSingleParam107To1071(int id, int val)
{
    switch (id) {
    case 84:    return 64;  // unison pan spread
    case 85:    return 24;  // unison pitch
    case 86:    return 0;   // midi ctrl src1
    case 87:    return 0;   // midi ctrl assign1
    case 88:    return 0;   // midi ctrl src2
    case 89:    return 0;   // midi ctrl assign2
    }
    return val;
}

int CSynthesizer1::convertSingleParam1071To109(int id, int val, CSoundData* pSrc, CSoundData* pDst)
{
    int depthID;
    switch (id) {
    case 86:
    case 87:
    case 88:
    case 89:
    case 91:
    case 92:
    case 94:
        return pDst->getParamVal(id);
    case 75:    // unison detune
        {
            float range = pSrc->getParamVal(38) ? 180.0f : 100.0f;
            int v = (int)(log((val * range / 127.0f + 0.5f) * 0.06389056098930651 + 1.0) * 63.5);
            if (v > 127) v = 127;
            return v;
        }
    case 93:    // unison voice num
        return pSrc->getParamVal(38) ? 4 : 2;
    case 50:    // midi ctrl sens1
        depthID = 44;
        break;
    case 51:    // midi ctrl sens2
        depthID = 43;
        break;
    default:
        return val;
    }
    // the old version scaled the sensitivity by the lfo depth
    int depth = pSrc->getParamVal(depthID);
    return (int)(val * (1.0f - depth / 127.0f) / 127.0f * 63.0f) + 64;
}

int CSynthesizer1::convertSingleParam109To111(int id, int val, CSoundData* pSrc, CSoundData* pDst)
{
    switch (id) {
    case 95:
    case 96:
    case 97:
        return pDst->getParamVal(id);
    }
    return val;
}

int CSynthesizer1::convertSingleParam111To112(int id, int val, CSoundData* pSrc, CSoundData* pDst)
{
    if (id == 98) return pDst->getParamVal(98);
    return val;
}

int CSynthesizer1::convertSingleParam112To113(int id, int val, CSoundData* pSrc, CSoundData* pDst)
{
    return val;
}

int CSynthesizer1::writeProgram(tagWriteProgramInfo* pInfo)
{
    int rtn = m_soundLib.saveProgram(pInfo);
    if (rtn == 1) {
        m_soundLib.setReqBankNo(m_pCurSound->m_bankNo, NULL);
        m_soundLib.setReqProgNo(m_pCurSound->m_progNo);
    }
    return rtn;
}

/**
 * @brief One parameter slot of a VST chunk: valid flag and value.
 */
struct VSTChunkParam {
    int bValid;
    int val;
};

/**
 * @brief Layout of a version 2 VST chunk (the plugin state saved by the host).
 *
 * params is indexed by parameter id; ids 250..254 hold the patch version, bank, program
 * and volume (CHUNK_ID_*).
 */
struct VSTChunk {
    char header[0x20];              // +0x000  "Synth1 VST Chunk Data"
    int chunkVer;                   // +0x020  2
    int bHostSelectProgram;         // +0x024
    char bankPath[0x208];           // +0x028
    int soundVer;                   // +0x230
    int paramNum;                   // +0x234
    VSTChunkParam params[0x101];    // +0x238  indexed by parameter id
};

#define CHUNK_ID_VERSION    250
#define CHUNK_ID_BANK       251
#define CHUNK_ID_PROGRAM    252
#define CHUNK_ID_VOLUME     254

int CSynthesizer1::getChunkVer(unsigned char* pData, int size)
{
    if (size == 0x814) {
        if (strcmp((char*)pData, "Synth1 DXi Data Header V1.0\0") == 0) return 0;
    } else if (size < 0x14) {
        return -1;
    }
    if (strcmp((char*)pData, "Synth1 VST Chunk Data") == 0) {
        return ((VSTChunk*)pData)->chunkVer;
    }
    return -1;
}

int CSynthesizer1::makeVSTChunkData(unsigned char* pData, int size)
{
    if (size < (int)sizeof(VSTChunk)) {
        MessageBox(NULL, "Error", "makeVSTChunkData()", 2);
        return 0;
    }
    VSTChunk* pChunk = (VSTChunk*)pData;
    memset(pChunk, 0, sizeof(VSTChunk));
    strcpy(pChunk->header, "Synth1 VST Chunk Data");
    pChunk->chunkVer = 2;
    pChunk->bHostSelectProgram = canVSTHostChangePatch();
    strcpy(pChunk->bankPath, m_pCurSound->m_path);
    pChunk->soundVer = 113;
    pChunk->paramNum = 0x100;
    pChunk->params[CHUNK_ID_VERSION].bValid = 1;
    pChunk->params[CHUNK_ID_VERSION].val = 113;
    pChunk->params[CHUNK_ID_BANK].bValid = 1;
    pChunk->params[CHUNK_ID_BANK].val = m_pCurSound->m_bankNo;
    pChunk->params[CHUNK_ID_PROGRAM].bValid = 1;
    pChunk->params[CHUNK_ID_PROGRAM].val = m_pCurSound->m_progNo;
    pChunk->params[CHUNK_ID_VOLUME].bValid = 1;
    pChunk->params[CHUNK_ID_VOLUME].val = m_volumeVal;
    int num = m_pCurSound->getParamsNum();
    for (int i = 0; i < num; i++) {
        int id, val;
        m_pCurSound->enumParam(i, &id, &val);
        pChunk->params[id].bValid = 1;
        pChunk->params[id].val = val;
    }
    return sizeof(VSTChunk);
}

void CSynthesizer1::setCurrentProgNameOnly()
{
    int progNo = m_soundLib.m_reqProgNo;
    int bankNo = m_soundLib.m_reqBankNo;
    char* path = m_soundLib.m_reqZipPath;
    m_pCurSound->m_bankNo = bankNo;
    m_pCurSound->m_progNo = progNo;
    strcpy(m_pCurSound->m_path, path);
    CSoundLibrary::getProgramNameColor(bankNo, path, progNo, m_pCurSound->m_name, NULL, progNo == 0);
    if (m_pSynWin) {
        PostMessageControl(m_pSynWin, 0x720, CHUNK_ID_PROGRAM, 0);
        PostMessageControl(m_pSynWin, 0x720, CHUNK_ID_BANK, 0);
    }
}

void CSynthesizer1::setVSTChunkData(unsigned char* pData, int size, int chunkVer)
{
    int ver;
    char* path;
    VSTChunkParam* pParams = (VSTChunkParam*)pData;
    switch (chunkVer) {
    case 1:
        ver = *(int*)(pData + 0x28);
        pParams = (VSTChunkParam*)(pData + 0x30);
        path = NULL;
        break;
    case 2:
        ver = ((VSTChunk*)pData)->soundVer;
        pParams = ((VSTChunk*)pData)->params;
        path = ((VSTChunk*)pData)->bankPath;
        break;
    }
    setCommonParam(CHUNK_ID_VOLUME, pParams[CHUNK_ID_VOLUME].val, 1);
    int progNo = pParams[CHUNK_ID_PROGRAM].val;
    m_soundLib.setReqProgNo(progNo);
    m_soundLib.setReqBankNo(pParams[CHUNK_ID_BANK].val, path);
    setCurrentProgNameOnly();

    CSoundData sound = m_initSound;
    for (int i = 0; i <= 98; i++) {
        if (pParams[i].bValid) {
            sound.setParamVal(i, pParams[i].val);
        }
    }
    sound.m_version = ver;
    convertParams(&sound);
    for (int i = 0; i < sound.getParamsNum(); i++) {
        int id, val;
        sound.enumParam(i, &id, &val);
        setSoundParam(id, val, 1);
    }
}

/**
 * @brief One parameter slot of DXi save data (at offset 0x1c), indexed by parameter id.
 */
struct DxiParam {
    bool bValid;
    int val;
};

int CSynthesizer1::setDxiSaveParams(unsigned char* pData)
{
    DxiParam* pParams = (DxiParam*)(pData + 0x1c);
    for (int i = 0; i < 4; i++) {
        if (pParams[CHUNK_ID_BANK + i].bValid) {
            setCommonParam(CHUNK_ID_BANK + i, pParams[CHUNK_ID_BANK + i].val, 1);
        }
    }
    setCurrentProgNameOnly();

    CSoundData sound = m_initSound;
    for (int i = 0; i <= 98; i++) {
        if (pParams[i].bValid) {
            sound.setParamVal(i, pParams[i].val);
        }
    }
    if (pParams[CHUNK_ID_VERSION].bValid) {
        sound.m_version = pParams[CHUNK_ID_VERSION].val;
    } else {
        sound.m_version = 100;
    }
    convertParams(&sound);
    for (int i = 0; i < sound.getParamsNum(); i++) {
        int id, val;
        sound.enumParam(i, &id, &val);
        setSoundParam(id, val, 1);
    }
    return 1;
}

void CSynthesizer1::setBaseTempo(float tempo)
{
    if (m_baseTempo == tempo) return;
    m_baseTempo = tempo;
    m_arp.setTempo(tempo);
    int n = m_delayTimeVal;
    n = MAX(0, MIN(n, 19));
    float sec = g_nDelayBeat[n] * 60 / (m_baseTempo * 24.0f);
    m_delay.setDelayTime(sec);
    if (m_lfo[0].m_bTempoSync) m_lfo[0].recalcSpeed();
    if (m_lfo[1].m_bTempoSync) m_lfo[1].recalcSpeed();
}

float CSynthesizer1::valSyn2Vst(int id, int val, int ver)
{
    float f;
    convValIntFloat(id, &val, &f, 1, ver);
    return f;
}

// conversion between the internal integer value and the 0..1 float of the
// VST/AU parameter.  bToFloat: int -> float, otherwise float -> int.
// The option lists are conditional-operator chains, which the compiler turns into lookup
// tables. The case order is significant: it sets the order of the constant tables in the
// released binary, so don't reorder the cases.
int CSynthesizer1::convValIntFloat(int id, int* pInt, float* pFloat, int bToFloat, int ver)
{
    float f;
    switch (id) {
    case 71:    // osc mod dest
        if (bToFloat) {
            *pFloat = (*pInt == 0) ? 0.166f : (*pInt == 1) ? 0.5f : 0.833f;
        } else {
            f = *pFloat;
            *pInt = (f < 0.33) ? 0 : (f < 0.66) ? 1 : 2;
        }
        break;
    case 0:     // osc1 shape
    case 96:    // osc1 sub shape
        if (bToFloat) {
            *pFloat = (*pInt == 0) ? 0.125f : (*pInt == 1) ? 0.375f : (*pInt == 2) ? 0.625f : 0.875f;
        } else {
            f = *pFloat;
            *pInt = (f < 0.25) ? 0 : (f < 0.5) ? 1 : (f < 0.75) ? 2 : 3;
        }
        break;
    case 1:     // osc2 shape
    case 31:    // arpeggiator type
        if (bToFloat) {
            *pFloat = (*pInt == 1) ? 0.125f : (*pInt == 2) ? 0.375f : (*pInt == 3) ? 0.625f : 0.875f;
        } else {
            f = *pFloat;
            *pInt = (f < 0.25) ? 1 : (f < 0.5) ? 2 : (f < 0.75) ? 3 : 4;
        }
        break;
    case 9:     // osc key shift -24..24
        if (bToFloat) {
            *pFloat = (float)((*pInt + 24) / 49.0f + 0.5 / 49.0);
        } else {
            f = *pFloat * 49.0f;
            *pInt = (f < 48) ? (int)f - 24 : 24;
        }
        break;
    case 14:    // filter type
        if (bToFloat) {
            *pFloat = (*pInt == 0) ? 0.1f : (*pInt == 1) ? 0.3f : (*pInt == 2) ? 0.5f : (*pInt == 3) ? 0.7f : 0.9f;
        } else {
            f = *pFloat;
            *pInt = (f < 0.2) ? 0 : (f < 0.4) ? 1 : (f < 0.6) ? 2 : (f < 0.8) ? 3 : 4;
        }
        break;
    case 32:    // arpeggiator oct range
        if (ver == 100) {
            // 1..4
            if (bToFloat) {
                *pFloat = (*pInt - 1) * 0.25f + 0.125f;
            } else {
                f = *pFloat * 4.0f;
                *pInt = (f < 3) ? (int)f + 1 : 4;
            }
        } else {
            // 0..3
            if (bToFloat) {
                *pFloat = *pInt * 0.25f + 0.125f;
            } else {
                f = *pFloat * 4.0f;
                *pInt = (f < 3) ? (int)f : 3;
            }
        }
        break;
    case 33:    // arpeggiator beat 0..18
        if (bToFloat) {
            *pFloat = (float)(*pInt / 19.0f + 0.5 / 19.0);
        } else {
            f = *pFloat * 19.0f;
            *pInt = (f < 18) ? (int)f : 18;
        }
        break;
    case 78:    // effect type 0..9
        if (bToFloat) {
            *pFloat = (float)(*pInt / 10.0f + 0.5 / 10.0);
        } else {
            f = *pFloat * 10.0f;
            *pInt = (f < 9) ? (int)f : 9;
        }
        break;
    case 82:    // delay type 0..2
        if (bToFloat) {
            *pFloat = (float)(*pInt / 3.0f + 0.5 / 3.0);
        } else {
            f = *pFloat * 3.0f;
            *pInt = (f < 2) ? (int)f : 2;
        }
        break;
    case 35:    // delay time 0..19
        if (bToFloat) {
            *pFloat = (float)(*pInt / 20.0f + 0.5 / 20.0);
        } else {
            f = *pFloat * 20.0f;
            *pInt = (f < 19) ? (int)f : 19;
        }
        break;
    case 64:    // chorus type
        if (bToFloat) {
            *pFloat = (*pInt == 1) ? 0.125f : (*pInt == 2) ? 0.375f : 0.875f;
        } else {
            f = *pFloat;
            *pInt = (f < 0.25) ? 1 : (f < 0.5) ? 2 : (f < 0.75) ? 2 : 4;
        }
        return TRUE;
    case 38:    // play mode
        if (bToFloat) {
            *pFloat = (*pInt == 0) ? 0.125f : (*pInt == 1) ? 0.625f : 0.875f;
        } else {
            f = *pFloat;
            *pInt = (f < 0.25) ? 0 : (f < 0.5) ? 0 : (f < 0.75) ? 1 : 2;
        }
        break;
    case 94:    // polyphony 1..32
        if (bToFloat) {
            *pFloat = (*pInt - 1) * 0.03125f + 0.015625f;
        } else {
            f = *pFloat * 32.0f;
            *pInt = (f < 31) ? (int)f + 1 : 32;
        }
        break;
    case 86:    // midi ctrl src1
    case 87:    // midi ctrl assign1
    case 88:    // midi ctrl src2
    case 89:    // midi ctrl assign2
        if (bToFloat) {
            *pFloat = (*pInt + 1) / 65536.0f;
        } else {
            *pInt = (int)(*pFloat * 65536.0f) - 1;
        }
        break;
    case 93:    // unison voice num 2..8
        if (bToFloat) {
            *pFloat = (float)((*pInt - 2) / 7.0f + 0.5 / 7.0);
        } else {
            f = *pFloat * 7.0f;
            *pInt = (f < 6) ? (int)f + 2 : 8;
        }
        break;
    case 85:    // unison pitch 0..48
        if (bToFloat) {
            *pFloat = (float)(*pInt / 49.0f + 0.5 / 49.0);
        } else {
            f = *pFloat * 49.0f;
            *pInt = (f < 48) ? (int)f : 48;
        }
        break;
    case 40:    // pitch bend range 0..24
        if (bToFloat) {
            *pFloat = (float)(*pInt / 25.0f + 0.5 / 25.0);
        } else {
            f = *pFloat * 25.0f;
            *pInt = (f < 24) ? (int)f : 24;
        }
        break;
    case 41:                                // lfo1 destination
        __builtin_assume(bToFloat != 7);    // MATCH-HACK
    case 46:                                // lfo2 destination
        if (ver <= 100) {
            if (bToFloat) {
                *pFloat = (*pInt == 1) ? 0.1f : (*pInt == 2) ? 0.3f : (*pInt == 3) ? 0.5f : (*pInt == 4) ? 0.7f : 0.9f;
            } else {
                f = *pFloat;
                *pInt = (f < 0.2) ? 1 : (f < 0.4) ? 2 : (f < 0.6) ? 3 : (f < 0.8) ? 4 : 5;
            }
        } else if (ver <= 106) {
            if (bToFloat) {
                *pFloat = (*pInt == 1) ? 0.2f : (*pInt == 2) ? 0.3f : (*pInt == 3) ? 0.4f : (*pInt == 4) ? 0.5f : (*pInt == 5) ? 0.6f : 0.7f;
            } else {
                f = *pFloat;
                *pInt = (f < 0.25) ? 1 : (f < 0.35) ? 2 : (f < 0.45) ? 3 : (f < 0.55) ? 4 : (f < 0.65) ? 5 : 6;
            }
        } else {
            if (bToFloat) {
                *pFloat = (*pInt == 1) ? 0.2f : (*pInt == 2) ? 0.3f : (*pInt == 3) ? 0.4f : (*pInt == 4) ? 0.5f : (*pInt == 5) ? 0.6f : (*pInt == 6) ? 0.7f : 0.8f;
            } else {
                f = *pFloat;
                *pInt = (f < 0.25) ? 1 : (f < 0.35) ? 2 : (f < 0.45) ? 3 : (f < 0.55) ? 4 : (f < 0.65) ? 5 : (f < 0.75) ? 6 : 7;
            }
        }
        return TRUE;
    L_lfotypeDummy:
        f = 0;  // MATCH-HACK: dead store (see below)
        goto L_lfotypeFloat;
    case 42:                                // lfo1 type
        __builtin_assume(bToFloat != 7);    // MATCH-HACK
    case 47:                                // lfo2 type
        // MATCH-HACK: the switches below, like the __builtin_assume lines and the dead store in
        // this function, emit no code. They only adjust the compiler's branch probabilities so that
        // the code is laid out as in the released binary. See docs/MATCH_HACKS.md.
        switch (bToFloat) {
        case 101 ... 110: goto L_lfotypeQ;
        case 201 ... 218: goto L_lfotypeFloat;
        default: goto L_lfotype;
        }
    L_lfotypeQ:
        if ((unsigned long)(unsigned)bToFloat == 300) goto L_lfotypeFloat;
    L_lfotype:
        // slider order: saw, tri, sine, square, s&h, smooth random
        switch (__builtin_expect((unsigned)bToFloat, 0)) {
        case 401 ... 409: goto L_lfotypeDummy;
        case 501 ... 510: goto L_lfotypeDummy;
        case 0:
            f = *pFloat;
            *pInt = (f < 0.25) ? 0 : (f < 0.35) ? 1 : (f < 0.45) ? 5 : (f < 0.55) ? 2 : (f < 0.65) ? 3 : 4;
            break;
        default:
        L_lfotypeFloat:
            *pFloat = (*pInt == 0) ? 0.2f : (*pInt == 1) ? 0.3f : (*pInt == 5) ? 0.4f : (*pInt == 2) ? 0.5f : (*pInt == 3) ? 0.6f : 0.7f;
            break;
        }
        break;
    case 97:
    L_onoff:
        // on/off
        if (bToFloat) {
            *pFloat = (*pInt == 0) ? 0.25f : 0.75f;
        } else {
            *pInt = (*pFloat < 0.5) ? 0 : 1;
        }
        break;
    case 252:   // program
        if (bToFloat) {
            *pFloat = *pInt / 128.0f + 0.5f / 128.0f;
        } else {
            f = *pFloat * 128.0f;
            *pInt = (f < 127) ? (int)f : 127;
        }
        break;
    case 2: case 3: case 5: case 8: case 11: case 12: case 13: case 15: case 16: case 17: case 18: case 19: case 20: case 21: case 22: case 23: case 25: case 26: case 27: case 28: case 29: case 30: case 34: case 36: case 37: case 39: case 43: case 44:
    L_127:
        // 0..127
        if (bToFloat) {
            *pFloat = (float)(*pInt / 128.0 + 0.5 / 128.0);
        } else {
            f = *pFloat * 128.0f;
            *pInt = (f < 127) ? (int)f : 127;
        }
        break;
    case 250:   // version
        if (bToFloat) {
            *pFloat = *pInt / 1000.0f;
        } else {
            *pInt = (int)(*pFloat * 1000.0f);
        }
        break;
    case 251:   // bank
        {
            int num = 100;
            if (ver == 108) num = 10;
            if (ver < 107) num = 10;
            if (bToFloat) {
                *pFloat = (float)(*pInt / (float)num + 1.0 / num * 0.5);
            } else {
                int nmax = num - 1;
                f = num * *pFloat;
                f = MIN(f, (float)nmax);
                *pInt = (int)f;
            }
        }
        break;
    case 4: case 6: case 7: case 10: case 24: case 57: case 58: case 59: case 65: case 66: case 67: case 68: case 69: case 70: case 73:
        __builtin_assume(bToFloat != 7);    // MATCH-HACK
        goto L_onoff;
    case 74: case 77:
        __builtin_assume(bToFloat != 7);    // MATCH-HACK
        goto L_onoff;
    case 45: case 48: case 49: case 50: case 51: case 52: case 53: case 54: case 55: case 56: case 60: case 61: case 62: case 63: case 72: case 75: case 76: case 79: case 80: case 81: case 83: case 84: case 90: case 91: case 92: case 95: case 98: case 254:
        __builtin_assume(bToFloat != 7);    // MATCH-HACK
        goto L_127;
    default:
        return FALSE;
    }
    return TRUE;
}

int CSynthesizer1::valVst2Syn(int id, float val, int ver)
{
    int v;
    convValIntFloat(id, &v, &val, 0, ver);
    return v;
}

int CSynthesizer1::valVstOld2Syn(int id, float val, int ver, CSoundData* pSrc, CSoundData* pDst)
{
    int v;
    convValIntFloat(id, &v, &val, 0, ver);
    return convertSingleParam(id, v, ver, pSrc, pDst);
}

void CSynthesizer1::onChangeSamplingFreq()
{
    int fs = m_samplingFreq;
    calcVolLPFCoef();
    m_mixer.setSamplingFreq(fs);
    m_eq.setSamplingFreq(fs);
    m_toneEq.setSamplingFreq(fs);
    m_effector.setSamplingFreq(fs);
    m_delay.setSamplingFreq(fs);
    m_delayEq.setSamplingFreq(fs);
    m_chorus.setSamplingFreq(fs);
    m_arp.setSamplingFreq(fs);
    m_lfo[0].setSamplingFreq(fs);
    m_lfo[1].setSamplingFreq(fs);
    for (int i = 0; i < 32; i++) {
        m_voice[i].m_vco.setSamplingFreq(fs);
        m_voice[i].m_vcf.setSamplingFreq(fs);
        m_voice[i].m_vca.setSamplingFreq(fs);
    }
}

void CSynthesizer1::createCSVStringFromCCMap(int* pMap, char* str)
{
    char tmp[256];
    str[0] = 0;
    int i;
    for (i = 0; i < 127; i++) {
        sprintf(tmp, "%d,", pMap[i]);
        strcat(str, tmp);
    }
    sprintf(tmp, "%d", pMap[i]);
    strcat(str, tmp);
}

extern "C" void Synthesizer1_createCSVStringFromCCMap(CSynthesizer1* pSynth, int* pMap, char* str)
{
    CSynthesizer1::createCSVStringFromCCMap(pMap, str);
}

extern "C" int Synthesizer1_getCCMapFromCSV(CSynthesizer1* pSynth, int* pMap, char* str)
{
    return CSynthesizer1::getCCMapFromCSV(pMap, str);
}

int CSynthesizer1::getCCMapFromCSV(int* pMap, char* str)
{
    char sep[] = " ,\t\n";
    memset(pMap, 0xff, sizeof(int) * 128);
    char* p = strtok(str, sep);
    int i = 0;
    while (p) {
        int v = atoi(p);
        if (v >= 0 && v < 256) pMap[i] = v;
        p = strtok(NULL, sep);
        i++;
        if (i >= 128) break;
    }
    if (i == 128) return TRUE;
    MessageBox(NULL, "getCCMapFromCSV()", "format Error", 2);
    return FALSE;
}

extern "C" void Synthesizer1_getProgramName(CSynthesizer1* pSynth, int bankNo, char* bankPath, int progNo, char* name)
{
    pSynth->getProgramName(bankNo, bankPath, progNo, name);
}

extern "C" int* Synthesizer1_getCCMapTable(CSynthesizer1* pSynth)
{
    return s1g->m_ccMap;
}

int CSynthesizer1::checkSoundParam(int id, int val, int* pVal)
{
    int min, max;
    getParamValRange(id, &min, &max);
    if (min == -1 && max == -1) return FALSE;
    int range = max - min;
    if (range < 0) return FALSE;
    *pVal = val % (range + 1) + min;
    return TRUE;
}
