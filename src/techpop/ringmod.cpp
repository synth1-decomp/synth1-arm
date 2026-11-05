/**
 * @file ringmod.cpp
 * @brief Ring modulator effect (CRingMod).
 */
#include "common.h"
#include "efxs.h"

CRingMod::CRingMod()
{
    m_x = 0.0f;
    m_y = 1.0f;
    setFreq(1000.0f);
    setLevel(0.5f);
}

void CRingMod::setFreq(float freq)
{
    m_freq = freq;
    m_coef = 2.0 * sin(freq * 3.141592653589793 / m_samplingFreq);
    // keep the oscillator on the unit circle
    m_x = MAX(-1.0f, MIN(1.0f, m_x));
    float y = cos(asin(m_x));
    m_y = (m_y >= 0.0f) ? y : -y;
}

void CRingMod::setLevel(float level)
{
    m_level = level;
}

CRingMod::~CRingMod()
{
}

void CRingMod::onChangeSamplingFreq()
{
    calcCoefs();
}

void CRingMod::calcCoefs()
{
    m_coef = 2.0 * sin(m_freq * 3.141592653589793 / m_samplingFreq);
}

int CRingMod::processSignal(int size, float* pBuf, int* pProcessed)
{
    if (*pProcessed) return size;
    float dry = 1.0f - m_level;
    for (int i = 0; i < size; i++) {
        m_x = m_x - m_coef * m_y;
        m_y = m_y + m_coef * m_x;
        pBuf[i] *= m_x * m_level + dry;
    }
    return size;
}

int CRingMod::processSignalW(int size, float* pBuf, int* pProcessed)
{
    if (*pProcessed) return size;
    tagStereoData* p = (tagStereoData*)pBuf;
    float dry = 1.0f - m_level;
    for (int i = 0; i < size; i++) {
        m_x = m_x - m_coef * m_y;
        m_y = m_y + m_coef * m_x;
        float g = m_x * m_level + dry;
        p[i].l *= g;
        p[i].r *= g;
    }
    return size;
}
