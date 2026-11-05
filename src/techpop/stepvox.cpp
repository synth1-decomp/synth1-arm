/**
 * @file stepvox.cpp
 * CStepVox: a small test sequencer that plays random four-note chords on a CInst.
 */
#include <stdlib.h>
#include <time.h>
#include "inst.h"
#include "stepvox.h"

CStepVox::CStepVox()
{
    m_curTime = 0;
    m_bPlaying = 0;
}

void CStepVox::setInst(CInst* pInst)
{
    m_pInst = pInst;
}

void CStepVox::incCurTime(int time)
{
    m_curTime += time;
    if(m_curTime >= STEPVOX_CYCLE) m_curTime = 0;
}

int CStepVox::getNextWaitTime()
{
    return STEPVOX_CYCLE - m_curTime % STEPVOX_CYCLE;
}

void CStepVox::processNote()
{
    static int freq = 20;
    static bool fdir = false;
    static int oddeven = 0;
    static int oldNoteNo1, oldNoteNo2, oldNoteNo3, oldNoteNo4;

    if(oddeven == 0) srand((unsigned int)time(NULL));
    if((oddeven & 1) == 0){
        m_pInst->noteOff(oldNoteNo1);
        m_pInst->noteOff(oldNoteNo2);
        m_pInst->noteOff(oldNoteNo3);
        m_pInst->noteOff(oldNoteNo4);
        int r = rand() % 24;
        oldNoteNo1 = r + 30;
        oldNoteNo2 = r + 34;
        oldNoteNo3 = r + 37;
        oldNoteNo4 = r + 40;
        m_pInst->noteOn(oldNoteNo1, 127);
        m_pInst->noteOn(oldNoteNo2, 127);
        m_pInst->noteOn(oldNoteNo3, 127);
        m_pInst->noteOn(oldNoteNo4, 127);
    }
    oddeven++;
    freq += fdir ? -10 : 10;
    if(freq >= 127) fdir = true;
    if(freq <= 0) fdir = false;
}

void CStepVox::resetCurTime()
{
    m_curTime = 0;
}

int CStepVox::isPlaying()
{
    return m_bPlaying;
}

void CStepVox::startPlay()
{
    m_bPlaying = 1;
}

void CStepVox::stopPlay()
{
    m_bPlaying = 0;
}
