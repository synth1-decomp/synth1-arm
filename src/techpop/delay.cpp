/**
 * @file delay.cpp
 * @brief CDelay: stereo / cross / ping-pong delay with smoothed delay time changes.
 */

#include <math.h>
#include <string.h>
#include "../windef.h"
#include "synth1.h"
#include "common.h"
#include "delay.h"

CDelay::CDelay()
{
    m_pBuf = (float*)_aligned_malloc((DELAY_BUFSIZE * 2 + 6) * sizeof(float), 16);
    m_pBufR = m_pBuf + DELAY_BUFSIZE + 4;
    m_pOut = (float*)_aligned_malloc(DELAY_OUTBUFSIZE * 2 * sizeof(float), 16);
    m_bufSize = DELAY_BUFSIZE;
    m_delayL = m_delayR = 256;
    resetPointers();
    m_feedback = 0.7f;
    m_releaseTime = 1;
    m_silentCount = 99999999;
    setDelayTime(100.0f);
    m_bEnable = 1;
    m_type = DELAY_TYPE_STEREO;
    m_timeBalance = 0.1f;
    calcCoef();
    for (int i = 0; i < 4; i++) m_pInsFilter[i] = NULL;
}

CDelay::~CDelay()
{
    _aligned_free(m_pBuf);
    _aligned_free(m_pOut);
}

void CDelay::onChangeSamplingFreq()
{
    setDelayTime(m_delayTime);
    calcCoef();
}

void CDelay::setType(int type)
{
    m_type = type;
    calcReleaseTime();
}

void CDelay::calcReleaseTime()
{
    int prev = m_releaseTime;
    int n = (int)(log(1.0 / 32768) / log(m_feedback));
    int d = MAX(m_delayL, m_delayR);
    if (m_type == DELAY_TYPE_PINGPONG) d *= 2;
    m_releaseTime = (n + 2) * d;
    if (m_silentCount > prev) m_silentCount = m_releaseTime + 1;
}

void CDelay::calcDelaySample()
{
    int d = m_delaySample / 4 * 4;
    m_delayL = m_delayR = d;
    if (m_timeBalance >= 0) {
        m_delayL = d + (int)(m_samplingFreq * m_timeBalance);
    } else {
        m_delayR = d + (int)(m_samplingFreq * -m_timeBalance);
    }
    int minDelay = (int)(m_samplingFreq * 0.0001);
    int maxDelay = m_bufSize - 256;
    m_delayL = MAX(minDelay, MIN(maxDelay, m_delayL));
    m_delayR = MAX(minDelay, MIN(maxDelay, m_delayR));

    if (m_silentCount <= m_releaseTime && m_bEnable) {
        m_bSmoothing = 1;
    } else {
        m_curDelayL = m_delayL;
        m_curDelayR = m_delayR;
    }
}

void CDelay::setDelaySample(int n)
{
    m_delaySample = n;
    calcDelaySample();
    calcReleaseTime();
}

void CDelay::setDelayTime(float t)
{
    m_delayTime = t;
    m_delaySample = (int)(m_samplingFreq * t);
    calcDelaySample();
    calcReleaseTime();
}

void CDelay::setTimeBalance(float bal)
{
    m_timeBalance = bal;
    calcDelaySample();
    calcReleaseTime();
}

void CDelay::setFeedback(float fb)
{
    if (fb > 0.999f) fb = 0.999f;
    if (fb < 0) fb = 0;
    m_feedback = fb;
    calcReleaseTime();
}

void CDelay::setDelayLevel(int v)
{
    calcDryWet(v, m_dry, m_wet);
}

void CDelay::setMixDelay(int v)
{
}

void CDelay::resetPointers()
{
    m_writePos = 0;
    m_bSmoothing = 0;
    m_curDelayL = m_delayL;
    m_curDelayR = m_delayR;
    memset(m_pBuf, 0, m_bufSize * sizeof(float));
    memset(m_pBufR, 0, m_bufSize * sizeof(float));
}

void CDelay::setEnable(int b)
{
    if (b == 1 && m_bEnable == 0) resetPointers();
    m_bEnable = b;
}

void CDelay::calcCoef()
{
    m_smoothCoef = (float)(1.0 - fpow(0.5, 1.0 / (m_samplingFreq * 0.15f)));
}

void CDelay::setInsertionFilter(CSignalDevice* p1, CSignalDevice* p2, CSignalDevice* p3, CSignalDevice* p4)
{
    m_pInsFilter[0] = p1;
    m_pInsFilter[1] = p2;
    m_pInsFilter[2] = p3;
    m_pInsFilter[3] = p4;
}

int CDelay::processSignal(int size, float* pBuf, int* pProcessed)
{
    return size;
}

int CDelay::doPingPongDelay(int size, tagStereoData* pIn, tagStereoData* pOut)
{
    unsigned int pos = m_writePos;
    float* pBufL = m_pBuf;
    float* pBufR = m_pBufR;
    int i;

    if (m_bSmoothing) {
        double curL = m_curDelayL;
        double curR = m_curDelayR;
        float dl = m_delayL;
        float dr = m_delayR;
        float coef = m_smoothCoef;
        for (i = 0; i < size; i++) {
            curL += (dl - curL) * coef;
            float rpL = (float)((pos + i) - curL);
            if (rpL < 0) rpL += DELAY_BUFSIZE;
            unsigned int ipL = (int)rpL;
            float a = pBufL[ipL & DELAY_BUFMASK];
            float b = pBufL[(ipL + 1) & DELAY_BUFMASK];

            curR += (dr - curR) * coef;
            float rpR = (float)((pos + i) - curR);
            if (rpR < 0) rpR += DELAY_BUFSIZE;
            unsigned int ipR = (int)rpR;
            float c = pBufR[ipR & DELAY_BUFMASK];
            float d = pBufR[(ipR + 1) & DELAY_BUFMASK];

            float outL = (b - a) * (rpL - ipL) + a;
            float outR = (d - c) * (rpR - ipR) + c;

            // do not read beyond the write position
            if (size > curL) {
                size = (int)curL;
                if (i >= size) break;
            }
            if (size > curR) {
                size = (int)curR;
                if (i >= size) break;
            }
            pOut[i].l = outL;
            pOut[i].r = outR;
        }
        m_curDelayL = curL;
        m_curDelayR = curR;
    } else {
        size = MIN(MIN(m_delayL, m_delayR), size);
        for (i = 0; i < size; i++) {
            pOut[i].l = pBufL[(pos + i - m_delayL) & DELAY_BUFMASK];
            pOut[i].r = pBufR[(pos + i - m_delayR) & DELAY_BUFMASK];
        }
    }

    for (i = 0; i < 4; i++) {
        if (m_pInsFilter[i]) m_pInsFilter[i]->processSignalWForced(size, (float*)pOut, NULL);
    }

    float fb = m_feedback;
    for (i = 0; i < size; i++) {
        int idx = (pos + i) & DELAY_BUFMASK;
        pBufL[idx] = pOut[i].r * fb + (pIn[i].l + pIn[i].r) * 0.5f;
        pBufR[idx] = pOut[i].l;
    }
    m_writePos = (pos + size) & DELAY_BUFMASK;
    return size;
}

int CDelay::doStereoDelay(int size, tagStereoData* pIn, tagStereoData* pOut)
{
    unsigned int pos = m_writePos;
    tagStereoData* pBuf = (tagStereoData*)m_pBuf;
    int i;

    if (m_bSmoothing) {
        double curL = m_curDelayL;
        double curR = m_curDelayR;
        float dl = m_delayL;
        float dr = m_delayR;
        float coef = m_smoothCoef;
        for (i = 0; i < size; i++) {
            curL += (dl - curL) * coef;
            float rpL = (float)((pos + i) - curL);
            if (rpL < 0) rpL += DELAY_BUFSIZE;
            unsigned int ipL = (int)rpL;
            float a = pBuf[ipL & DELAY_BUFMASK].l;
            float b = pBuf[(ipL + 1) & DELAY_BUFMASK].l;

            curR += (dr - curR) * coef;
            float rpR = (float)((pos + i) - curR);
            if (rpR < 0) rpR += DELAY_BUFSIZE;
            unsigned int ipR = (int)rpR;
            float c = pBuf[ipR & DELAY_BUFMASK].r;
            float d = pBuf[(ipR + 1) & DELAY_BUFMASK].r;
            float outL = (b - a) * (rpL - ipL) + a;
            float outR = (d - c) * (rpR - ipR) + c;

            if (size > curL) {
                size = (int)curL;
                if (i >= size) break;
            }
            if (size > curR) {
                size = (int)curR;
                if (i >= size) break;
            }
            pOut[i].l = outL;
            pOut[i].r = outR;
        }
        m_curDelayL = curL;
        m_curDelayR = curR;
    } else {
        size = MIN(MIN(m_delayL, m_delayR), size);
        for (i = 0; i < size; i++) {
            pOut[i].l = pBuf[(pos + i - m_delayL) & DELAY_BUFMASK].l;
            pOut[i].r = pBuf[(pos + i - m_delayR) & DELAY_BUFMASK].r;
        }
    }

    for (i = 0; i < 4; i++) {
        if (m_pInsFilter[i]) m_pInsFilter[i]->processSignalWForced(size, (float*)pOut, NULL);
    }

    float fb = m_feedback;
    if (m_type != DELAY_TYPE_STEREO) {
        // cross feedback
        for (i = 0; i < size; i++) {
            tagStereoData* p = &pBuf[(pos + i) & DELAY_BUFMASK];
            p->l = pIn[i].r + pOut[i].r * fb;
            p->r = pIn[i].l + pOut[i].l * fb;
        }
    } else {
        for (i = 0; i < size; i++) {
            tagStereoData* p = &pBuf[(pos + i) & DELAY_BUFMASK];
            p->l = pIn[i].l + pOut[i].l * fb;
            p->r = pIn[i].r + pOut[i].r * fb;
        }
    }
    m_writePos = (pos + size) & DELAY_BUFMASK;
    return size;
}

int CDelay::processSignalW(int size, float* pBuf, int* pProcessed)
{
    if (!m_pInput) {
        *pProcessed = 1;
        return size;
    }
    int n = m_pInput->processSignalW(size, pBuf, pProcessed);
    if (!m_bEnable) {
        m_bOutput = 0;
        return n;
    }
    if (!*pProcessed) {
        m_silentCount = 0;
    } else {
        if (m_silentCount > m_releaseTime && !m_bSmoothing) {
            m_bOutput = 0;
            return n;
        }
        if (!m_bSmoothing) m_silentCount += n;
        *pProcessed = 0;
        for (int i = 0; i < size * 2; i++) pBuf[i] = 0;
    }

    for (int done = 0; done < n; ) {
        if (m_type == DELAY_TYPE_PINGPONG) {
            done += doPingPongDelay(n - done, (tagStereoData*)&pBuf[done * 2], (tagStereoData*)&m_pOut[done * 2]);
        } else {
            done += doStereoDelay(n - done, (tagStereoData*)&pBuf[done * 2], (tagStereoData*)&m_pOut[done * 2]);
        }
        if (m_bSmoothing) {
            if (fabs(m_curDelayL - m_delayL) < 0.1) m_curDelayL = m_delayL;
            if (fabs(m_curDelayR - m_delayR) < 0.1) m_curDelayR = m_delayR;
            if (m_curDelayL == m_delayL && m_curDelayR == m_delayR) m_bSmoothing = 0;
        }
    }
    if (m_dry != 1.0f) mulArray2SSE(pBuf, m_dry, n * 2);
    if (m_wet != 1.0f) mulArray2SSE(m_pOut, m_wet, n * 2);
    m_bOutput = 1;
    return n;
}
