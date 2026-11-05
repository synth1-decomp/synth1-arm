/**
 * @file phatcont.cpp
 * @brief "Phat" controller (CPhatCont): feedback with a timed tail after the input falls silent.
 */
#include <math.h>
#include "common.h"
#include "phatcont.h"

CPhatCont::CPhatCont()
{
    setPhatLevel(0.0f);
    m_silentCount = 99999999;
}

CPhatCont::~CPhatCont()
{
}

void CPhatCont::setPhatLevel(float level)
{
    if(fpow(level, 0.5) > 0.99f){
        m_coef = 0.99f;
    }else{
        m_coef = (float)fpow(level, 0.5);
        if(m_coef == 0.0f) m_state = 0.0f;
    }
    m_gain = m_coef * m_coef * -0.9f + 1.0f;
    calcReleaseTime();
}

void CPhatCont::calcReleaseTime()
{
    int old = m_releaseTime;
    m_releaseTime = (int)(log(1.0 / 32768.0) / log(m_coef)) + 2;
    if(m_releaseTime < old) m_silentCount = 0;
}

int CPhatCont::processSignal(int size, float* pBuf, int* pProcessed)
{
    if(m_pInput == 0){
        *pProcessed = 1;
        return size;
    }
    int n = m_pInput->processSignal(size, pBuf, pProcessed);
    if(m_coef == 0.0f) return n;
    if(*pProcessed){
        if(m_silentCount > m_releaseTime){
            m_state = 0.0f;
            return n;
        }
        m_silentCount += n;
    }else{
        m_silentCount = 0;
    }
    float s = m_state;
    if(*pProcessed){
        for(int i = 0; i < n; i++){
            s *= m_coef;
            pBuf[i] = s;
            pBuf[i] = s * m_gain;
        }
        *pProcessed = 0;
    }else{
        for(int i = 0; i < n; i++){
            s = s * m_coef + pBuf[i];
            pBuf[i] = s;
            pBuf[i] = s * m_gain;
        }
    }
    m_state = s;
    return n;
}
