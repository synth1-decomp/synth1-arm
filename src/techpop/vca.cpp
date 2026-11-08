/**
 * @file vca.cpp
 * @brief CVca: envelope controlled amplifier
 *
 * Applies the amp envelope with linear per-block gain ramps (SSE when available) and resets
 * the rest of the voice when the release ends.
 */

#include <math.h>
#if defined(__aarch64__)
#include "../compat/sse_neon.h"
#else
#include <xmmintrin.h>
#endif
#include "vca.h"
#include "vcf.h"
#include "vco.h"

#ifndef FPOW_DEFINED
#define FPOW_DEFINED

/**
 * @brief pow() via exp(); falls back to pow() if the result is not a number
 *
 * @param a     Base.
 * @param b     Exponent.
 * @return a raised to the power b.
 */
inline double fpow(double a, double b)
{
    double r = exp(log(a) * b);
    if(isnan(r)) r = pow(a, b);
    return r;
}
#endif

#define VCA_MAX     2048

CVca::CVca()
{
    setMax(VCA_MAX);
    m_bProcessIdle = 0;
    m_velGain = 1.0f;
    m_velocitySens = 0;
    m_lastLevel = 0;
    m_lastLfo1 = 0;
    m_lastLfo2 = 0;
    m_curGain = 0;
    m_targetGain = 0;
    m_bSmooth = 1;
    CEnvelopedObject::setGain(1.0f);
    m_gain = 0.5f;
    m_bGainChanged = 1;
}

void CVca::setGain(float gain)
{
    if(m_gain != gain){
        m_gain = gain;
        m_bGainChanged = 1;
    }
}

void CVca::changeLfoSetting(int no, int bEnable, int dest)
{
    enableLfo(no, bEnable && dest == 4);
}

int CVca::processEnvelopedSignal(int level, float* pBuf, int size, int lfo1, int lfo2)
{
    if(level == m_lastLevel && lfo1 == m_lastLfo1 && lfo2 == m_lastLfo2 && !m_bGainChanged){
        // constant gain
        if(m_targetGain == 0) return 1;
        mulArray2SSE(pBuf, m_targetGain, size);
        return 0;
    }

    float target = level * m_gain * m_velGain * (1.0f / VCA_MAX);
    if(lfo1 || lfo2){
        target *= (VCA_MAX - lfo1) * (VCA_MAX - lfo2) * (1.0f / (VCA_MAX * VCA_MAX));
    }
    m_targetGain = target;
    float g = m_curGain;
    float delta = (target - g) / size;
    int i = 0;
    if(size >= 7 && m_bSSE){
        for(; (size_t)&pBuf[i] & 15; i++){
            g += delta;
            pBuf[i] *= g;
        }
        __m128 vg = _mm_set_ps(g, g - delta, g - delta - delta, g - delta - delta - delta);
        if(i < size - 3){
            __m128 vd = _mm_set1_ps(4.0f * delta);
            for(; i < size - 3; i += 4){
                vg = _mm_add_ps(vg, vd);
                _mm_store_ps(&pBuf[i], _mm_mul_ps(_mm_load_ps(&pBuf[i]), vg));
            }
        }
        g = vg[3];
    }
    for(; i < size; i++){
        g += delta;
        pBuf[i] *= g;
    }
    m_curGain = g;
    m_lastLevel = level;
    m_lastLfo1 = lfo1;
    m_lastLfo2 = lfo2;
    m_bGainChanged = 0;
    return 0;
}

void CVca::releaseEnd()
{
    m_curGain = 0;
    CVcf* pVcf = (CVcf*)m_pInput;
    pVcf->resetFilterBuf();
    CVco* pVco = (CVco*)pVcf->m_pInput;
    pVco->resetFMSmooth();
    pVco->envelopeReset();
}

void CVca::setVelocitySens(float sens)
{
    m_velocitySens = sens;
}

void CVca::noteOn(int velocity)
{
    double db = m_velocitySens * -30.0f;
    m_velGain = (float)fpow(10.0, db * (127 - velocity) / 127.0 / 20.0);
    envelopeOn();
}

void CVca::noteOff()
{
    envelopeOff();
}
