/**
 * @file phaser.cpp
 * @brief Multi-stage all-pass phaser (CPhaser).
 */

#include <math.h>
#include <string.h>
#include "phaser.h"

CPhaser::CPhaser()
{
    memset(m_zL, 0, sizeof(m_zL));
    memset(m_zR, 0, sizeof(m_zR));
    m_bEnable = 1;
    m_silentCount = 0;
    calcFsParams();
    m_fbL = m_fbR = 0;
    m_freq = 0;
    m_feedback = 0.7f;
    m_depth = 0.5f;
    setStage(1);
    setRange(200.0f, 10000.0f);
    setRate(0.1f);
}

void CPhaser::setStage(int n)
{
    m_nStage = n;
}

void CPhaser::setRange(float minFreq, float maxFreq)
{
    m_minFreq = minFreq;
    m_maxFreq = maxFreq;
    if (m_freq == 0) m_freq = minFreq;
    setRate(m_rate);
}

void CPhaser::setRate(float rate)
{
    m_rate = rate;
    float mul = (float)exp(log(m_maxFreq / m_minFreq) / (m_samplingFreq / m_rate * 0.5f));
    if (m_freqMul > 1.0f) m_freqMul = mul;
    else m_freqMul = 1.0f / mul;
}

CPhaser::~CPhaser()
{
}

void CPhaser::onChangeSamplingFreq()
{
    calcFsParams();
    setRate(m_rate);
}

void CPhaser::setEnable(int b)
{
    m_bEnable = b;
}

void CPhaser::setFeedback(float v)
{
    m_feedback = v;
}

void CPhaser::setDepth(float v)
{
    m_depth = v;
}

int CPhaser::processSignal(int size, float* pBuf, int* pProcessed)
{
    return processSignalMain(size, pBuf, pProcessed, 0);
}

int CPhaser::processSignalW(int size, float* pBuf, int* pProcessed)
{
    return processSignalMain(size, pBuf, pProcessed, 1);
}

int CPhaser::processSignalMain(int size, float* pBuf, int* pProcessed, int bStereo)
{
    if (!m_bEnable) return size;

    if (!*pProcessed) {
        m_silentCount = 0;
    } else {
        if (m_silentCount > m_silentLimit) return size;
        m_silentCount += size;
        if (bStereo) {
            for (int i = 0; i < size * 2; i++) pBuf[i] = 0;
        } else {
            for (int i = 0; i < size; i++) pBuf[i] = 0;
        }
        *pProcessed = 0;
    }

    // reverse the sweep direction at the ends of the range
    if (m_freq > m_maxFreq) {
        if (m_freqMul > 1.0f) m_freqMul = 1.0f / m_freqMul;
    } else if (m_freq < m_minFreq) {
        if (m_freqMul < 1.0f) m_freqMul = 1.0f / m_freqMul;
    }

    if (!bStereo) processMono(size, pBuf);
    else processStereo(size, (tagStereoData*)pBuf);
    return size;
}

void CPhaser::processMono(int size, float* pBuf)
{
    float freq = m_freq;
    float x = m_fbL;
    for (int i = 0; i < size; i++) {
        freq *= m_freqMul;
        x = pBuf[i] + x * m_feedback;
        float w = m_freqCoef * freq;
        float a = (1.0f - w) / (1.0f + w);
        for (int j = 0; j < m_nStage; j++) {
            float y = -a * x + m_zL[j].z1;
            m_zL[j].z1 = a * y + x;
            x = -a * y + m_zL[j].z2;
            m_zL[j].z2 = a * x + y;
        }
        pBuf[i] = (1.0f - m_depth) * pBuf[i] + m_depth * x;
    }
    m_freq = freq;
    m_fbL = x;
}

void CPhaser::processStereo(int size, tagStereoData* pBuf)
{
    float freq = m_freq;
    float xl = m_fbL;
    float xr = m_fbR;
    for (int i = 0; i < size; i++) {
        freq *= m_freqMul;
        xl = pBuf[i].l + xl * m_feedback;
        xr = pBuf[i].r + xr * m_feedback;
        float w = m_freqCoef * freq;
        float a = (1.0f - w) / (1.0f + w);
        for (int j = 0; j < m_nStage; j++) {
            float yl = -a * xl + m_zL[j].z1;
            m_zL[j].z1 = a * yl + xl;
            xl = -a * yl + m_zL[j].z2;
            m_zL[j].z2 = a * xl + yl;
            float yr = -a * xr + m_zR[j].z1;
            m_zR[j].z1 = a * yr + xr;
            xr = -a * yr + m_zR[j].z2;
            m_zR[j].z2 = a * xr + yr;
        }
        pBuf[i].l = (1.0f - m_depth) * pBuf[i].l + m_depth * xl;
        pBuf[i].r = (1.0f - m_depth) * pBuf[i].r + m_depth * xr;
    }
    m_freq = freq;
    m_fbL = xl;
    m_fbR = xr;
}
