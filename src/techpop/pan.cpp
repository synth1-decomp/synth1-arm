/**
 * @file pan.cpp
 * @brief Balance / LFO auto-pan (CPan).
 */

#include <math.h>
#include "common.h"
#include "pan.h"
#include "lfo.h"

CPan::CPan()
{
    m_balance = 0;
    calcCoef();
}

void CPan::calcCoef()
{
    // one-pole smoothing with a 2ms half-life
    m_smoothCoef = (float)(1.0 - fpow(0.5, 1.0 / (m_samplingFreq * 0.002f)));
}

void CPan::onChangeSamplingFreq()
{
    calcCoef();
}

void CPan::setLfo(CLFO* pLfo1, CLFO* pLfo2)
{
    m_pLfo[0] = pLfo1;
    m_pLfo[1] = pLfo2;
    m_bLfoEnable[0] = m_bLfoEnable[1] = 0;
}

void CPan::setLfoEnable(int no, int bEnable)
{
    m_bLfoEnable[no] = bEnable;
}

void CPan::setBalance(float balance)
{
    m_balance = balance;
}

void CPan::changeLFOSetting(int no)
{
    CLFO* pLfo = m_pLfo[no];
    m_bLfoEnable[no] = pLfo->m_bEnable && pLfo->m_destination == LFO_DEST_PAN;
}

int CPan::processSignal(int size, float* pBuf, int* pProcessed)
{
    return size;
}

// Note: the per-channel targets are computed right after each curve on purpose
// (significant for reproducing the released binary).
int CPan::calcLfoPanMain(tagStereoData* pBuf, int size, float gainL, float gainR, int lfoVal, float* pCurL, float* pCurR)
{
    float curL, curR;
    float v = lfoVal * (1.0f / 64);
    float t;
    double l, r;

    if (v < 0) {
        t = v + 1.0f;
        l = t * t * t;
    } else {
        t = v - 1.0f;
        l = t * t * t * 0.5 + 1.5;
    }
    float tgtL = (float)l * gainL;
    if (-v < 0) {
        t = 1.0f - v;
        r = t * t * t;
    } else {
        t = -v - 1.0f;
        r = t * t * t * 0.5 + 1.5;
    }
    float tgtR = (float)r * gainR;
    curL = *pCurL;
    curR = *pCurR;
    float coef = m_smoothCoef;
    for (int i = 0; i < size; i++) {
        curL += (tgtL - curL) * coef;
        curR += (tgtR - curR) * coef;
        pBuf[i].l *= curL;
        pBuf[i].r *= curR;
    }
    *pCurL = curL;
    *pCurR = curR;
    return size;
}

// Note: the plain float indexing in the no-LFO loop and the function-scope time1/time2
// are significant for reproducing the released binary.
int CPan::processSignalW(int size, float* pBuf, int* pProcessed)
{
    int n = m_pInput->processSignalW(size, pBuf, pProcessed);
    if (*pProcessed) return n;

    float gainL, gainR;
    if (m_balance > 0) {
        gainR = 1.0f;
        gainL = 1.0f - m_balance;
    } else if (m_balance < 0) {
        gainL = 1.0f;
        gainR = m_balance + 1.0f;
    } else {
        gainL = gainR = 1.0f;
    }

    if (!m_bLfoEnable[0] && !m_bLfoEnable[1]) {
        if (gainL != gainR) {
            for (int i = 0; i < size; i++) {
                pBuf[i * 2] *= gainL;
                pBuf[i * 2 + 1] *= gainR;
            }
        }
        return size;
    }

    tagStereoData* p = (tagStereoData*)pBuf;
    int pos1 = 0, pos2 = 0;
    int time1, time2;
    int rest = n;
    while (rest) {
        int val1, val2;
        if (m_bLfoEnable[0]) {
            val1 = m_pLfo[0]->getSameValAndTime(&time1, pos1);
            if (time1 == -1) time1 = rest;
            else time1 = MIN(time1, rest);
        } else {
            time1 = rest;
            val1 = 0;
        }
        int rest1 = time1;
        while (rest1) {
            if (m_bLfoEnable[1]) {
                val2 = m_pLfo[1]->getSameValAndTime(&time2, pos2);
                if (time2 == -1) time2 = rest1;
                else time2 = MIN(time2, rest1);
            } else {
                time2 = rest1;
                val2 = 0;
            }
            time2 = calcLfoPanMain(p, time2, gainL, gainR, val1 + val2, &m_curGainL, &m_curGainR);
            p += time2;
            pos2 += time2;
            rest1 -= time2;
        }
        pos1 += time1;
        rest -= time1;
    }
    return n;
}
