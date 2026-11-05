/**
 * @file chorus.cpp
 * @brief Chorus / flanger (CChorus): modulated stereo delay line with mono, stereo and ensemble
 *        tap layouts.
 */

#include <math.h>
#include <string.h>
#include "common.h"
#include "../windef.h"
#include "chorus.h"

CChorus::CChorus()
{
    m_pBuf = (tagStereoData*)_aligned_malloc((CHORUS_BUFSIZE + 1) * sizeof(tagStereoData), 16);
    memset(m_pBuf, 0, (CHORUS_BUFSIZE + 1) * sizeof(tagStereoData));
    m_readPos = 0;
    setDelayTime(0.02f);
    setDepth(0.5f);
    setRate(1.0f);
    setFeedback(0);
    m_silentCount = 99999999;
    m_bEnable = 1;
    m_kind = CHORUS_KIND_STEREO;
    m_inLevel[0] = m_inLevel[1] = 1.0f;
    setLevel(0);
    m_oscSin = 0;
    m_oscCos = 0.9999f;
}

CChorus::~CChorus()
{
    _aligned_free(m_pBuf);
}

void CChorus::onChangeSamplingFreq()
{
    setDelayTime(m_delayTime);
    setRate(m_rate);
}

void CChorus::setEnable(int b)
{
    if (b && !m_bEnable) memset(m_pBuf, 0, (CHORUS_BUFSIZE + 1) * sizeof(tagStereoData));
    m_bEnable = b;
}

void CChorus::setKind(int kind)
{
    m_kind = kind;
    switch (kind) {
    case CHORUS_KIND_MONO:
    case CHORUS_KIND_STEREO:
    case CHORUS_KIND_ENSEMBLE:
        break;
    default:
        m_kind = CHORUS_KIND_STEREO;
        break;
    }
}

void CChorus::calcReleaseTime()
{
    int prev = m_releaseTime;
    m_releaseTime = ((int)(log(1.0 / 32768) / log(fabs(m_feedback))) + 2) * m_delaySample;
    if (m_silentCount > prev) m_silentCount = m_releaseTime + 1;
    else m_silentCount = 0;
}

void CChorus::calcDepthCoef()
{
    if (m_period >= m_depthSample * 2 * M_PI) m_depthCoef = 1.0f;
    else m_depthCoef = (float)(m_period / (m_depthSample * 2 * M_PI));
}

void CChorus::setDelayTime(float t)
{
    m_delayTime = t;
    m_delaySample = (unsigned int)(m_samplingFreq * t);
    setDepth(m_depth);
    m_writePos = (m_readPos + m_delaySample) & CHORUS_BUFMASK;
    calcReleaseTime();
    calcDepthCoef();
}

void CChorus::setDepth(float depth)
{
    m_depth = depth;
    m_depthSample = (int)(m_delaySample * depth);
    int n = m_period / MAX(m_depthSample, 1);
    if (n < 300) m_step = 6;
    else m_step = (unsigned int)n / 50;
    calcDepthCoef();
}

void CChorus::setRate(float rate)
{
    m_rate = rate;
    m_period = (unsigned int)(m_samplingFreq / rate);
    int n = m_period / MAX(m_depthSample, 1);
    if (n < 300) m_step = 6;
    else m_step = (unsigned int)n / 50;
    m_oscCoef = (float)(2 * sin(rate * M_PI / m_samplingFreq));
    calcDepthCoef();
}

void CChorus::setFeedback(float fb)
{
    m_feedback = fb;
    m_bFeedback = (fb != 0);
    calcReleaseTime();
}

void CChorus::setLevel(float level)
{
    m_level = level;
}

void CChorus::setDelaySigPointer(float* pSig, int* pbValid)
{
    m_pDelaySig = pSig;
    m_pbDelaySigValid = pbValid;
}

int CChorus::processSignal(int size, float* pBuf, int* pProcessed)
{
    *pProcessed = 1;
    return size;
}

/**
 * @brief Adds (or copies) pSrc to pDst; returns whether pDst holds a signal.
 *
 * If pSrc is silent nothing is done. If pDst is silent, pSrc is copied instead of added.
 *
 * @param[in,out] pDst  Destination buffer.
 * @param bDst          Non-zero if pDst holds a signal.
 * @param pSrc          Source buffer.
 * @param bSrc          Non-zero if pSrc holds a signal.
 * @param size          Number of floats.
 * @return Non-zero if pDst holds a signal afterwards.
 */
int mixSigWithAll0(float* pDst, int bDst, float* pSrc, int bSrc, int size)
{
    if (!bSrc) return bDst;
    if (bDst) CSignalDevice::addArraySSE(pDst, pSrc, size);
    else bcopy(pSrc, pDst, size * sizeof(float));
    return 1;
}

#define CHORUS_FRAC (1.0f / 65536)

int CChorus::processSignalW(int size, float* pBuf, int* pProcessed)
{
    int n = m_pInput->processSignalW(size, pBuf, pProcessed);
    if (!m_bEnable) {
        *pProcessed = !mixSigWithAll0(pBuf, *pProcessed == 0, m_pDelaySig, *m_pbDelaySigValid, n * 2);
        return n;
    }

    if (*pProcessed && !*m_pbDelaySigValid) {
        if (m_silentCount > m_releaseTime) return n;
        m_silentCount += n;
    } else {
        m_silentCount = 0;
    }
    if (*pProcessed) {
        for (int i = 0; i < n * 2; i++) pBuf[i] = 1.0e-9f;
    }

    // write the input (and the delay output) into the line
    unsigned int rest = CHORUS_BUFSIZE - m_writePos;
    int n1 = MIN(n, rest);
    if (*m_pbDelaySigValid) addMulArraySSE((float*)&m_pBuf[m_writePos], m_pDelaySig, pBuf, m_level, n1 * 2);
    else mulArraySSE((float*)&m_pBuf[m_writePos], pBuf, m_level, n1 * 2);
    if (n > rest) {
        if (*m_pbDelaySigValid) addMulArraySSE((float*)m_pBuf, m_pDelaySig + n1 * 2, (float*)&((tagStereoData*)pBuf)[n1], m_level, (n - n1) * 2);
        else mulArraySSE((float*)m_pBuf, (float*)&((tagStereoData*)pBuf)[n1], m_level, (n - n1) * 2);
    }

    if (m_oscCos > 0.9999f) {
        m_oscSin = 0;
        m_oscCos = 0.9999f;
    }

    tagStereoData* p = (tagStereoData*)pBuf;
    tagStereoData* pLine;
    unsigned int wp, rp, depth;
    float coef, fb;
    int i;

    if (m_kind == CHORUS_KIND_MONO) {
        pLine = m_pBuf;
        wp = m_writePos;
        rp = m_readPos;
        coef = m_oscCoef;
        depth = ftoi(m_depthSample * m_depthCoef);
        fb = m_feedback;
        for (i = 0; i < n; i++) {
            m_oscSin = m_oscSin - coef * m_oscCos;
            m_oscCos = m_oscCos + coef * m_oscSin;
            unsigned int pos = ftoi(m_oscSin * (float)(depth << 16)) + (rp << 16);
            int frac = pos & 0xffff;
            unsigned int i0 = (pos >> 16) & CHORUS_BUFMASK;
            unsigned int i1 = ((pos >> 16) + 1) & CHORUS_BUFMASK;
            float outL = pLine[i0].l + (pLine[i1].l - pLine[i0].l) * frac * CHORUS_FRAC;
            float outR = pLine[i0].r + (pLine[i1].r - pLine[i0].r) * frac * CHORUS_FRAC;
            p[i].l += outL;
            p[i].r += outR;
            pLine[wp & CHORUS_BUFMASK].l += fb * outL;
            pLine[wp & CHORUS_BUFMASK].r += fb * outR;
            rp = (rp + 1) & CHORUS_BUFMASK;
            wp++;
        }
    } else if (m_kind == CHORUS_KIND_STEREO) {
        pLine = m_pBuf;
        wp = m_writePos;
        rp = m_readPos;
        coef = m_oscCoef;
        depth = ftoi(m_depthSample * m_depthCoef);
        fb = m_feedback;
        for (i = 0; i < n; i++) {
            m_oscSin = m_oscSin - coef * m_oscCos;
            m_oscCos = m_oscCos + coef * m_oscSin;
            unsigned int pos = ftoi(m_oscSin * (float)(depth << 16)) + (rp << 16);
            int frac = pos & 0xffff;
            unsigned int i0 = (pos >> 16) & CHORUS_BUFMASK;
            unsigned int i1 = ((pos >> 16) + 1) & CHORUS_BUFMASK;
            float outL = pLine[i0].l + (pLine[i1].l - pLine[i0].l) * frac * CHORUS_FRAC;
            pos = ftoi(m_oscCos * (float)(depth << 16)) + (rp << 16);
            i0 = (pos >> 16) & CHORUS_BUFMASK;
            i1 = ((pos >> 16) + 1) & CHORUS_BUFMASK;
            frac = pos & 0xffff;
            float outR = pLine[i0].r + (pLine[i1].r - pLine[i0].r) * frac * CHORUS_FRAC;
            p[i].l += outL;
            p[i].r += outR;
            pLine[wp & CHORUS_BUFMASK].l += fb * outL;
            pLine[wp & CHORUS_BUFMASK].r += fb * outR;
            rp = (rp + 1) & CHORUS_BUFMASK;
            wp++;
        }
    } else {
        pLine = m_pBuf;
        wp = m_writePos;
        rp = m_readPos;
        coef = m_oscCoef;
        depth = ftoi(m_depthSample * m_depthCoef);
        fb = m_feedback;
        for (i = 0; i < n; i++) {
            m_oscSin = m_oscSin - coef * m_oscCos;
            m_oscCos = m_oscCos + coef * m_oscSin;
            // tap and its mirror around the read position
            unsigned int pos = ftoi(m_oscSin * (float)(depth << 16)) + (rp << 16);
            unsigned int i0 = (pos >> 16) & CHORUS_BUFMASK;
            int frac = pos & 0xffff;
            unsigned int i1 = ((pos >> 16) + 1) & CHORUS_BUFMASK;
            float l1 = pLine[i0].l + (pLine[i1].l - pLine[i0].l) * frac * CHORUS_FRAC;
            unsigned int j0 = (rp * 2 - i0) & CHORUS_BUFMASK;
            unsigned int j1 = (rp * 2 - i0 - 1) & CHORUS_BUFMASK;
            int frac2 = 0x10000 - frac;
            float outR = l1 * 0.1f;
            float outL = l1 * 0.9f;
            float l2 = pLine[j1].l + (pLine[j0].l - pLine[j1].l) * frac2 * CHORUS_FRAC;
            outL += l2 * 0.1f;
            outR += l2 * 0.9f;
            pos = ftoi(m_oscCos * (float)(depth << 16)) + (rp << 16);
            i0 = (pos >> 16) & CHORUS_BUFMASK;
            i1 = ((pos >> 16) + 1) & CHORUS_BUFMASK;
            float r1 = pLine[i0].r + (pLine[i1].r - pLine[i0].r) * (pos & 0xffff) * CHORUS_FRAC;
            j0 = (rp * 2 - i0) & CHORUS_BUFMASK;
            outL += r1 * 0.7f;
            outR += r1 * 0.3f;
            j1 = (rp * 2 - i0 - 1) & CHORUS_BUFMASK;
            frac2 = 0x10000 - (pos & 0xffff);
            float r2 = pLine[j1].r + (pLine[j0].r - pLine[j1].r) * frac2 * CHORUS_FRAC;
            outL += r2 * 0.3f;
            outR += r2 * 0.7f;
            p[i].l += outL;
            p[i].r += outR;
            pLine[wp & CHORUS_BUFMASK].l += fb * l1;
            pLine[wp & CHORUS_BUFMASK].r += fb * r1;
            rp = (rp + 1) & CHORUS_BUFMASK;
            wp++;
        }
    }
    m_readPos = (m_readPos + n) & CHORUS_BUFMASK;
    m_writePos = (m_writePos + n) & CHORUS_BUFMASK;
    *pProcessed = 0;
    return n;
}
