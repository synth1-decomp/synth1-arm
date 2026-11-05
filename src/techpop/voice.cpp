/**
 * @file voice.cpp
 * @brief CVoice: wires a voice's oscillator, filter and amplifier together and handles note
 *        on/off, LFO routing and unison info.
 */
#include "common.h"
#include "debug.h"
#include "lfo.h"
#include "voice.h"

CVoice::CVoice()
{
    m_vca.inputDeviceIs(&m_vcf);
    m_vcf.inputDeviceIs(&m_vco);

    m_vco.setShape(1);
    m_vco.setShape2(1);
    m_vco.setPortamentTime(0.0f);

    m_vcf.setType(0);
    m_vcf.setFreq(0.0f);
    m_vcf.setResonance(0.0f);
    m_vcf.setAttack(0.0f);
    m_vcf.setDecay(0.4f);
    m_vcf.setSustain(0.0f);
    m_vcf.setRelease(1.0f);
    m_vcf.setAmount(1.0f);

    m_vca.setGain(1.0f);
    m_vca.setAttack(0.0f);
    m_vca.setDecay(0.5f);
    m_vca.setSustain(0.0f);
    m_vca.setRelease(1.0f);

    m_bEnable = 0;
    m_bAutoPortament = 0;
    m_bNoteOn = 0;
    m_noteNo = 0;
}

void CVoice::setLfo(CLFO* pLfo1, CLFO* pLfo2)
{
    m_pLfo[0] = pLfo1;
    m_pLfo[1] = pLfo2;
    m_vco.setLfo(0, pLfo1);
    m_vcf.setLfo(0, pLfo1);
    m_vca.setLfo(0, pLfo1);
    m_vco.setLfo(1, pLfo2);
    m_vcf.setLfo(1, pLfo2);
    m_vca.setLfo(1, pLfo2);
}

void CVoice::changeLFOSetting(int no)
{
    m_vco.changeLfoSetting(no, m_pLfo[no]->m_bEnable, m_pLfo[no]->m_destination);
    m_vcf.changeLfoSetting(no, m_pLfo[no]->m_bEnable, m_pLfo[no]->m_destination);
    m_vca.changeLfoSetting(no, m_pLfo[no]->m_bEnable, m_pLfo[no]->m_destination);
}

void CVoice::enable(int bEnable)
{
    m_bEnable = bEnable;
}

int CVoice::isBusy()
{
    return m_vca.getEnvStatus() != ENV_OFF;
}

int CVoice::getCurStatus()
{
    if(!m_bEnable) return VOICE_DISABLED;
    if(m_bNoteOn) return VOICE_NOTEON;
    if(m_vca.getEnvStatus() != ENV_OFF) return VOICE_RELEASING;
    return VOICE_IDLE;
}

int CVoice::processSignal(int size, float* pBuf, int* pProcessed)
{
    if(m_bEnable) return m_vca.processSignal(size, pBuf, pProcessed);
    *pProcessed = 1;
    return size;
}

void CVoice::setUnisonInfo(CVoice* pBase, int num, int no)
{
    m_unisonNum = num;
    m_unisonNo = no;
}

void CVoice::setUnisonPhaseShift(int val)
{
    m_unisonPhaseShift = val / 127.0f;
}

void CVoice::setNoteInfo(int nn, int velocity)
{
    m_noteNo = nn;
    m_velocity = velocity;
    int bPortament = 1;
    if(m_bAutoPortament == 1) bPortament = m_bNoteOn;   // legato only
    m_vco.setNote(nn, bPortament);
}

void CVoice::trigger()
{
    m_bNoteOn = 1;
    edPrintf("trigger %d\n", m_voiceNo);
    m_vco.setUnisonInfo(m_unisonNum, m_unisonNo, m_unisonPhaseShift);
    m_vca.noteOn(m_velocity);
    m_vcf.noteOn(m_noteNo, m_velocity);
    m_vco.noteOn();
}

void CVoice::release(int velocity)
{
    edPrintf("release %d\n", m_voiceNo);
    m_bNoteOn = 0;
    m_vca.noteOff();
    m_vcf.noteOff();
}
