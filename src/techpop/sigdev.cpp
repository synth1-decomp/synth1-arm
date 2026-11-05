/**
 * @file sigdev.cpp
 * @brief CSignalDevice: base class of the signal-processing devices, plus SSE helpers.
 *
 * Also owns the global SSE checker (g_sseChecker), whose result is copied into
 * CSignalDevice::m_bSSE by CheckSSEFunc() and each device constructor.
 */
#include <string.h>
#include <strings.h>
#include <xmmintrin.h>
#include "common.h"
#include "ssechk.h"
#include "sigdev.h"

CSSEChecker g_sseChecker;
int CSignalDevice::m_bSSE;

extern "C" void CheckSSEFunc()
{
    CSignalDevice::m_bSSE = g_sseChecker.m_bSSE;
}

void CSignalDevice::checkSSEFunc()
{
    m_bSSE = g_sseChecker.m_bSSE;
}

CSignalDevice::CSignalDevice()
{
    checkSSEFunc();
    m_pInput = NULL;
    m_samplingFreq = 44100;
}

void CSignalDevice::setSamplingFreq(int fs)
{
    m_samplingFreq = fs;
    onChangeSamplingFreq();
}

void CSignalDevice::inputDeviceIs(CSignalDevice* pDev)
{
    m_pInput = pDev;
}

// p[i] *= v
void CSignalDevice::mulArray2SSE(float* p, float v, int size)
{
    int i = 0;
    if (m_bSSE) {
        for (i = 0; i < size; i++) {
            if (((long)&p[i] & 15) == 0) break;
            p[i] *= v;
        }
        __m128 vv = _mm_set1_ps(v);
        for (; i < size - 3; i += 4) {
            _mm_store_ps(&p[i], _mm_mul_ps(_mm_load_ps(&p[i]), vv));
        }
    }
    for (; i < size; i++) {
        p[i] *= v;
    }
}

// d[i] += s[i]
void CSignalDevice::addArraySSE(float* d, float* s, int size)
{
    int i = 0;
    if (m_bSSE && ((long)d - (long)s <= 0 || (long)d - (long)s >= 16)) {
        for (i = 0; i < size; i++) {
            if (((long)&d[i] & 15) == 0) break;
            d[i] += s[i];
        }
        int size4 = size - 3;
        if ((((long)d ^ (long)s) & 15) == 0) {
            for (; i < size4; i += 4) {
                _mm_store_ps(&d[i], _mm_add_ps(_mm_load_ps(&d[i]), _mm_load_ps(&s[i])));
            }
        } else {
            for (; i < size4; i += 4) {
                _mm_store_ps(&d[i], _mm_add_ps(_mm_loadu_ps(&s[i]), _mm_load_ps(&d[i])));
            }
        }
    }
    for (; i < size; i++) {
        d[i] += s[i];
    }
}

// d[i] = s[i] * v
void CSignalDevice::mulArraySSE(float* d, float* s, float v, int size)
{
    if (v == 0.f) {
        memset(d, 0, size * sizeof(float));
        return;
    }
    int i = 0;
    if (m_bSSE && ((long)d - (long)s <= 0 || (long)d - (long)s >= 16)) {
        for (i = 0; i < size; i++) {
            if (((long)&d[i] & 15) == 0) break;
            d[i] = s[i] * v;
        }
        __m128 vv = _mm_set1_ps(v);
        int size4 = size - 3;
        if ((((long)d ^ (long)s) & 15) == 0) {
            for (; i < size4; i += 4) {
                _mm_store_ps(&d[i], _mm_mul_ps(_mm_load_ps(&s[i]), vv));
            }
        } else {
            for (; i < size4; i += 4) {
                _mm_store_ps(&d[i], _mm_mul_ps(_mm_loadu_ps(&s[i]), vv));
            }
        }
    }
    for (; i < size; i++) {
        d[i] = s[i] * v;
    }
}

// d[i] = s1[i] + s2[i] * v
void CSignalDevice::addMulArraySSE(float* d, float* s1, float* s2, float v, int size)
{
    if (v == 0.f) {
        bcopy(s1, d, size * sizeof(float));
        return;
    }
    int i = 0;
    if (m_bSSE && ((long)d - (long)s2 <= 0 || (long)d - (long)s2 >= 16)) {
        int ad = (long)d & 15;
        int a1 = (long)s1 & 15;
        int a2 = (long)s2 & 15;
        __m128 vv = _mm_set1_ps(v);
        int size4 = size - 3;
        if (ad == a1 || ad == a2) {
            // align d
            for (i = 0; i < size; i++) {
                if (((long)&d[i] & 15) == 0) break;
                d[i] = s1[i] + s2[i] * v;
            }
            if (a1 == a2) {
                for (; i < size4; i += 4) {
                    _mm_store_ps(&d[i], _mm_add_ps(_mm_mul_ps(_mm_load_ps(&s2[i]), vv), _mm_load_ps(&s1[i])));
                }
            } else if (ad == a1) {
                for (; i < size4; i += 4) {
                    _mm_store_ps(&d[i], _mm_add_ps(_mm_mul_ps(_mm_loadu_ps(&s2[i]), vv), _mm_load_ps(&s1[i])));
                }
            } else {
                for (; i < size4; i += 4) {
                    _mm_store_ps(&d[i], _mm_add_ps(_mm_loadu_ps(&s1[i]), _mm_mul_ps(_mm_load_ps(&s2[i]), vv)));
                }
            }
        } else {
            // align s1
            for (i = 0; i < size; i++) {
                if (((long)&s1[i] & 15) == 0) break;
                d[i] = s1[i] + s2[i] * v;
            }
            if (a1 == a2) {
                for (; i < size4; i += 4) {
                    _mm_storeu_ps(&d[i], _mm_add_ps(_mm_mul_ps(_mm_load_ps(&s2[i]), vv), _mm_load_ps(&s1[i])));
                }
            } else {
                for (; i < size4; i += 4) {
                    _mm_storeu_ps(&d[i], _mm_add_ps(_mm_mul_ps(_mm_loadu_ps(&s2[i]), vv), _mm_load_ps(&s1[i])));
                }
            }
        }
    }
    for (; i < size; i++) {
        d[i] = s1[i] + s2[i] * v;
    }
}

// expand a mono buffer in place to interleaved stereo
void CSignalDevice::mono2Stereo(float* p, int size)
{
    long j = (size - 1) * 2;
    for (int i = size - 1; i >= 0; i--, j -= 2) {
        p[j] = p[j + 1] = p[i];
    }
}

int CSignalDevice::processSignalW(int size, float* pBuf, int* pProcessed)
{
    int n = processSignal(size, pBuf, pProcessed);
    BOOL bStereo = *pProcessed;
    if (n > 0 && !bStereo) {
        mono2Stereo(pBuf, n);
    }
    return n;
}

int CSignalDevice::processSignalForced(int size, float* pBuf, int* pProcessed)
{
    return processSignal(size, pBuf, pProcessed);
}

int CSignalDevice::processSignalWForced(int size, float* pBuf, int* pProcessed)
{
    return processSignalW(size, pBuf, pProcessed);
}
