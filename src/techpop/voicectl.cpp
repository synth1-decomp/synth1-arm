/**
 * @file voicectl.cpp
 * @brief CVoiceCtl: voice allocation for poly, mono and legato modes, with unison detune and pan.
 */
#include <math.h>
#include "common.h"
#include "inst.h"
#include "mixdev.h"
#include "voice.h"
#include "voicectl.h"

CVoiceCtl::CVoiceCtl()
{
    m_polyMode = POLYMODE_POLY;
    m_lastVoice = -1;
    m_pKbdInfo = 0;
    m_bUnison = 0;
    m_unisonVoiceNum = 1;
    m_unisonDetune = 0;
    m_unisonPitch = 0;
    m_unisonPan = 0.0f;
}

void CVoiceCtl::setVoiceNum(int num)
{
    m_voiceNum = num;
    m_polyNum = num;
    m_enableVoiceNum = num;
}

void CVoiceCtl::setVoice(int no, CVoice* pVoice)
{
    m_pVoice[no] = pVoice;
    pVoice->m_voiceNo = no;
}

void CVoiceCtl::setPolyMode(int mode)
{
    if(m_polyMode == mode) return;
    m_polyMode = mode;
    calcEnableVoices();
}

void CVoiceCtl::setPolyNum(int num)
{
    if(m_polyNum == num) return;
    m_polyNum = MIN(m_voiceNum, MAX(num, 1));
    calcEnableVoices();
    m_lastVoice = -1;
}

void CVoiceCtl::setUnisonMode(int mode)
{
    int bUnison = (mode == 1);
    if(m_bUnison == bUnison) return;
    m_bUnison = bUnison;
    calcUnisonDetuneParams();
    calcUnisonPan();
    calcEnableVoices();
    m_lastVoice = -1;
}

void CVoiceCtl::calcUnisonDetuneParams()
{
    int i;
    if(m_bUnison == 0 || m_unisonVoiceNum == 1){
        for(i = 0; i < m_voiceNum; i++){
            m_pVoice[i]->m_vco.setDetune(0.0f);
            m_pVoice[i]->m_vcf.setUnisonDetunedMod(0.0f);
        }
        return;
    }
    float detune[VOICECTL_MAX_VOICE];
    float mod[VOICECTL_MAX_VOICE];
    int num = m_unisonVoiceNum;
    float range = (float)((exp(m_unisonDetune / 127.0f * 2.0f) - 1.0) / (exp(2.0) - 1.0)) * 100.0f;
    for(i = 0; i < num; i++){
        detune[i] = i / (float)(num - 1) * range - range * 0.5f;
        mod[i] = (i / (float)num - 0.5f) * range;
    }
    for(i = 0; i < num; i++){
        if(i & 1) detune[i] += m_unisonPitch;
    }
    for(i = 0; i < m_voiceNum; i++){
        m_pVoice[i]->m_vco.setDetune(detune[i % m_unisonVoiceNum]);
        m_pVoice[i]->m_vcf.setUnisonDetunedMod(mod[i % m_unisonVoiceNum]);
    }
}

void CVoiceCtl::calcUnisonPan()
{
    int i;
    if(m_bUnison){
        for(i = 0; i < m_voiceNum; i++){
            m_pVoice[i]->m_pan = (i % m_unisonVoiceNum) / (float)(m_unisonVoiceNum - 1) * (m_unisonPan * 2.0f) - m_unisonPan;
        }
    }else{
        for(i = 0; i < m_voiceNum; i++){
            m_pVoice[i]->m_pan = 0.0f;
        }
    }
}

void CVoiceCtl::setUnisonVoiceNum(int num)
{
    if(m_unisonVoiceNum == num) return;
    m_unisonVoiceNum = num;
    if(m_bUnison){
        calcUnisonDetuneParams();
        calcUnisonPan();
        calcEnableVoices();
        m_lastVoice = -1;
    }
}

void CVoiceCtl::setUnisonDetune(int val)
{
    if(m_unisonDetune == val) return;
    m_unisonDetune = val;
    if(m_bUnison) calcUnisonDetuneParams();
}

void CVoiceCtl::setUnisonPitch(int val)
{
    int pitch = val * 100 - 2400;
    if(pitch == m_unisonPitch) return;
    m_unisonPitch = pitch;
    if(m_bUnison) calcUnisonDetuneParams();
}

void CVoiceCtl::setVoicePan(float pan)
{
    if(m_unisonPan == pan) return;
    m_unisonPan = pan;
    if(m_bUnison) calcUnisonPan();
}

void CVoiceCtl::calcEnableVoices()
{
    if(m_polyMode == POLYMODE_POLY){
        m_enableVoiceNum = m_polyNum;
    }else{
        int num = m_bUnison ? m_unisonVoiceNum : 1;
        m_enableVoiceNum = MIN(num, m_polyNum);
    }
    int i;
    for(i = 0; i < m_enableVoiceNum; i++){
        m_pVoice[i]->enable(1);
    }
    for(; i < m_voiceNum; i++){
        if(m_pVoice[i]->m_bNoteOn) m_pVoice[i]->release(0);
        m_activeVoices.remove(m_pVoice[i]);
        m_pVoice[i]->enable(0);
    }
    m_pMixDevice->setEnableVoiceNum(m_enableVoiceNum);
}

void CVoiceCtl::setKeyboardInfo(CKeyboardInfo* pKbdInfo)
{
    m_pKbdInfo = pKbdInfo;
}

CVoice* CVoiceCtl::getNextOffVoice()
{
    int no = m_lastVoice;
    for(int i = 0; i < m_enableVoiceNum; i++){
        no++;
        if(no >= m_enableVoiceNum) no = 0;
        if(!m_pVoice[no]->m_bNoteOn) return m_pVoice[no];
    }
    return 0;
}

void CVoiceCtl::controlVoiceTrigger()
{
    if(!m_pKbdInfo->m_bChanged) return;
    if(m_polyMode == POLYMODE_POLY){
        controlVoicePoly();
    }else{
        controlVoiceMono();
    }
    m_pKbdInfo->m_bChanged = 0;
    // the keys are no longer new
    for(int i = 0; i < m_pKbdInfo->m_keyOnNum; i++){
        if(m_pKbdInfo->m_keyOrder[i] >= 0x100) m_pKbdInfo->m_keyOrder[i] -= 0x100;
    }
}

void CVoiceCtl::controlVoicePoly()
{
    std::list<CVoice*>::iterator it;
    // release the voices whose key is off
    for(it = m_activeVoices.begin(); it != m_activeVoices.end();){
        if(m_pKbdInfo->m_velocity[(*it)->m_noteNo] == 0){
            (*it)->release(0);
            it = m_activeVoices.erase(it);
        }else{
            ++it;
        }
    }
    int num = m_enableVoiceNum;
    if(num == 0) return;
    int uniNum = m_bUnison ? m_unisonVoiceNum : 1;
    if(num > m_pKbdInfo->m_keyOnNum) num = m_pKbdInfo->m_keyOnNum;
    for(int i = 0; i < num && m_pKbdInfo->m_keyOnNum > i; i++){
        // newly pressed keys, newest first
        int key = m_pKbdInfo->m_keyOrder[m_pKbdInfo->m_keyOnNum - 1 - i];
        int nn = (key >= 0x100) ? key - 0x100 : -1;
        if(nn == -1) return;
        for(it = m_activeVoices.begin(); it != m_activeVoices.end();){
            if((*it)->m_noteNo == nn){
                (*it)->release(0);
                it = m_activeVoices.erase(it);
            }else{
                ++it;
            }
        }
        CVoice* pBase;
        for(int u = 0; u < uniNum; u++){
            CVoice* pVoice = getNextOffVoice();
            if(pVoice == 0){
                // steal the oldest voice
                pVoice = m_activeVoices.front();
                if(pVoice == 0) return;
                m_activeVoices.pop_front();
            }
            m_lastVoice = pVoice->m_voiceNo;
            if(u == 0) pBase = pVoice;
            pVoice->setNoteInfo(nn, m_pKbdInfo->m_velocity[nn]);
            pVoice->setUnisonInfo(pBase, uniNum, u);
            pVoice->trigger();
            m_activeVoices.push_back(pVoice);
        }
    }
}

void CVoiceCtl::controlVoiceMono()
{
    int uniNum = m_bUnison ? m_unisonVoiceNum : 1;
    int i;
    if(m_pKbdInfo->m_keyOnNum == 0){
        for(i = 0; i < uniNum; i++){
            m_activeVoices.remove(m_pVoice[i]);
            if(m_pVoice[i]->m_bNoteOn) m_pVoice[i]->release(0);
        }
        m_monoVelocity = 0;
        return;
    }
    CVoice* pFirst;
    // latest key (the index is computed unsigned: no nsw, so it is not folded into the address)
    int last = m_pKbdInfo->m_keyOnNum - 1U;
    int key = (m_pKbdInfo->m_keyOnNum < 1) ? -1 : m_pKbdInfo->m_keyOrder[last];
    int nn;
    if (key < 0x100) nn = -1; else nn = key - 0x100;
    if(nn != -1){
        // newly pressed
        m_monoVelocity = m_pKbdInfo->m_velocity[nn];
        pFirst = m_pVoice[0];
    }else{
        // a key was released: go back to the previous key
        nn = key;
        pFirst = m_pVoice[0];
        if(pFirst->m_bNoteOn && pFirst->m_noteNo == nn) return;
    }
    switch(m_polyMode){
    case POLYMODE_MONO:
        for(i = 0; i < uniNum; i++){
            CVoice* pVoice = m_pVoice[i];
            pVoice->setNoteInfo(nn, m_monoVelocity);
            pVoice->setUnisonInfo(pFirst, uniNum, i);
            pVoice->trigger();
            m_activeVoices.remove(pVoice);
            m_activeVoices.push_back(pVoice);
        }
        break;
    case POLYMODE_LEGATO:
        for(i = 0; i < uniNum; i++){
            CVoice* pVoice = m_pVoice[i];
            pVoice->setNoteInfo(nn, m_monoVelocity);
            if(!pVoice->m_bNoteOn){
                pVoice->setUnisonInfo(pFirst, uniNum, i);
                pVoice->trigger();
                m_activeVoices.push_back(pVoice);
            }
        }
        break;
    }
}

int* CVoiceCtl::getVoicesStatus()
{
    return m_voiceStatus;
}

int CVoiceCtl::isVoicesStatusChanged()
{
    int bChanged = 0;
    for(int i = 0; i < VOICECTL_MAX_VOICE; i++){
        int status = m_pVoice[i]->getCurStatus();
        if(status != m_voiceStatus[i]){
            m_voiceStatus[i] = status;
            bChanged = 1;
        }
    }
    return bChanged;
}
