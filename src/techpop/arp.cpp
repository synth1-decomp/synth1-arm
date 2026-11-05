/**
 * @file arp.cpp
 * @brief Arpeggiator (CArpeggiator): step timing and up / down / up-down / random note selection.
 */
#include <stdlib.h>
#include "common.h"
#include "inst.h"
#include "voicectl.h"
#include "arp.h"

// step lengths in 1/24 beats (1/1 .. 1/64)
static const int g_nBeat[] = {
    96, 84, 72, 48, 42, 36, 32, 24, 21, 18,
    16, 12, 9, 8, 6, 4, 3, 2, 1
};

CArpeggiator::CArpeggiator()
{
    m_samplingFreq = 44100;
    m_bPlaying = 0;
    m_bEnable = 0;
    m_tempo = 120.0f;
    m_cycleTime = 22050;
    m_beat = 24;
    m_curTime = 0;
    m_bDown = 0;
    m_octRange = 1;
    m_gateTime = 0.5f;
    m_bNewStep = 1;
    m_curNote = -1;
    m_playingNote = -1;
    m_velocity = 127;
    m_type = ARP_UPDOWN;
}

void CArpeggiator::setSamplingFreq(int fs)
{
    m_samplingFreq = fs;
    float tempo = m_tempo;
    setTempo(tempo + 1.0f); // force a recalculation
    setTempo(tempo);
}

void CArpeggiator::setTempo(float tempo)
{
    if(m_tempo == tempo) return;
    if(tempo < 1.0f) tempo = 1.0f;
    m_tempo = tempo;
    float oldCycle = (float)m_cycleTime;
    m_cycleTime = (int)((float)(m_samplingFreq * m_beat * 60) / (m_tempo * 24.0f));
    m_curTime = (int)((float)m_curTime * m_cycleTime / oldCycle);
}

void CArpeggiator::setInst(CInst* pInst)
{
    m_pInst = pInst;
}

void CArpeggiator::setKbdInfo(CKeyboardInfo* pKbdInfo)
{
    m_pKbdInfo = pKbdInfo;
}

int CArpeggiator::isPlaying()
{
    return m_bPlaying;
}

void CArpeggiator::setEnable(int bEnable)
{
    m_bEnable = bEnable;
    setType(m_type);
}

void CArpeggiator::setType(int type)
{
    m_type = type;
    if(m_bEnable == 1){
        arpeggiatorOn();
    }else{
        arpeggiatorOff();
    }
}

void CArpeggiator::arpeggiatorOn()
{
    m_bPlaying = 1;
    m_curTime = 0;
    m_bNewStep = 1;
    m_curOct = 0;
    m_bDown = (m_type == ARP_DOWN);
}

void CArpeggiator::arpeggiatorOff()
{
    m_bPlaying = 0;
    if(m_playingNote != -1){
        m_pInst->noteOffFromArp(m_playingNote);
        m_playingNote = -1;
    }
}

void CArpeggiator::setOctRange(int range)
{
    m_octRange = range;
}

void CArpeggiator::setBeat(int beat)
{
    if(beat < 0) beat = 0;
    else if(beat > 18) beat = 18;
    int n = g_nBeat[beat];
    if(m_beat == n) return;
    m_beat = n;
    float oldCycle = (float)m_cycleTime;
    m_cycleTime = (int)((float)(m_samplingFreq * m_beat * 60) / (m_tempo * 24.0f));
    m_curTime = (int)((float)m_curTime * m_cycleTime / oldCycle);
}

void CArpeggiator::recalcCycleTime()
{
    float oldCycle = (float)m_cycleTime;
    m_cycleTime = (int)((float)(m_samplingFreq * m_beat * 60) / (m_tempo * 24.0f));
    m_curTime = (int)((float)m_curTime * m_cycleTime / oldCycle);
}

void CArpeggiator::setGateTime(int val)
{
    m_gateTime = val / 127.0f;
}

void CArpeggiator::timingReset(int velocity)
{
    m_curTime = 0;
    m_bNewStep = 1;
    m_velocity = velocity;
    m_curOct = 0;
    m_curNote = -1;
    m_bDown = (m_type == ARP_DOWN);
}

int CArpeggiator::getNextWaitTime()
{
    if(m_curTime == 0) return 0;
    int gateLen = ftoi(m_cycleTime * m_gateTime);
    if(gateLen > m_curTime) return gateLen - m_curTime;
    if(gateLen == m_curTime) return 0;
    return m_cycleTime - m_curTime;
}

void CArpeggiator::incCurTime(int size)
{
    m_curTime += size;
    if(m_curTime >= m_cycleTime) m_curTime %= m_cycleTime;
}

// Called at the start of each step and at the end of the gate time.
// Returns the time until the next call.
int CArpeggiator::processArpeggiator()
{
    if(m_playingNote != -1){
        m_pInst->noteOffFromArp(m_playingNote);
        m_playingNote = -1;
    }
    if(m_curTime != 0){
        m_bNewStep = 1;
        return m_cycleTime - m_curTime;
    }
    if(m_gateTime == 0.0f) return m_cycleTime;

    int note;
    if(m_type != ARP_RANDOM){
        if(m_bDown == 0){
            note = GetUpArpeggioNote();
            if(note == -1){
                if(m_curOct < m_octRange){
                    m_curNote = -1;
                    note = GetUpArpeggioNote();
                    m_curOct++;
                }else if(m_type == ARP_UPDOWN){
                    m_bDown = 1;
                    note = GetDownArpeggioNote();
                    if(note == -1 && m_pKbdInfo->getVelocity(m_curNote)){
                        // only one key is held: repeat it an octave lower
                        note = m_curNote;
                        if(m_octRange > 0) m_curOct--;
                    }
                }else{
                    m_curNote = -1;
                    note = GetUpArpeggioNote();
                    m_curOct = 0;
                }
            }
        }else{
            note = GetDownArpeggioNote();
            if(note == -1){
                if(m_curOct > 0){
                    m_curNote = -1;
                    note = GetDownArpeggioNote();
                    m_curOct--;
                }else if(m_type == ARP_UPDOWN){
                    m_bDown = 0;
                    note = GetUpArpeggioNote();
                    if(note == -1 && m_pKbdInfo->getVelocity(m_curNote)){
                        // only one key is held: repeat it an octave higher
                        note = m_curNote;
                        if(m_octRange > 0) m_curOct++;
                    }
                }else{
                    m_curNote = -1;
                    note = GetDownArpeggioNote();
                    m_curOct = m_octRange;
                }
            }
        }
    }else{
        m_curOct = rand() % (m_octRange + 1);
        note = getRandomArpNote();
    }
    m_curNote = note;
    m_playingNote = -1;
    if(note != -1){
        int nn = note + m_curOct * 12;
        if(nn >= 0 && nn < 128){
            m_pInst->noteOnFromArp(nn, m_velocity);
            m_playingNote = nn;
        }
    }
    if(m_gateTime < 1.0f) m_bNewStep = 0;
    return ftoi(m_gateTime * m_cycleTime);
}

int CArpeggiator::GetUpArpeggioNote()
{
    int i = (m_curNote == -1) ? 0 : m_curNote + 1;
    for(; i < 128; i++){
        if(m_pKbdInfo->m_velocity[i]) break;
    }
    if(i == 128) return -1;
    return i;
}

int CArpeggiator::GetDownArpeggioNote()
{
    int i = (m_curNote == -1) ? 127 : m_curNote - 1;
    for(; i >= 0; i--){
        if(m_pKbdInfo->m_velocity[i]) break;
    }
    if(i == 0) return -1;
    return i;
}

int CArpeggiator::getRandomArpNote()
{
    int list[128];
    int num = 0;
    for(int i = 0; i < 128; i++){
        if(m_pKbdInfo->m_velocity[i]) list[num++] = i;
    }
    if(num == 0) return -1;
    return list[rand() % num];
}
