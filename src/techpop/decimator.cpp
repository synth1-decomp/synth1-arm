/**
 * @file decimator.cpp
 * @brief Decimator effect (CDecimator): sample-and-hold rate reduction and bit reduction.
 */
#include "common.h"
#include "efxs.h"

/**
 * @brief Converts a float to int with the x87 FISTP instruction (current rounding mode, normally
 *        round to nearest).
 *
 * @param f  Value to convert.
 * @return The rounded integer.
 */
static inline int floatToInt(const float& f)
{
#if defined(__aarch64__)
    return ftoi(f);
#else
    int n;
    __asm {
        fld f
        fistp n
    }
    return n;
#endif
}

CDecimator::CDecimator()
{
    m_count = 0;
}

CDecimator::~CDecimator()
{
}

void CDecimator::onChangeSamplingFreq()
{
}

void CDecimator::setLevel(float level)
{
    m_level = level;
}

void CDecimator::setSample(int n)
{
    m_sample = n;
}

void CDecimator::setBit(int n)
{
    m_bit = n;
}

int CDecimator::processSignal(int size, float* pBuf, int* pProcessed)
{
    if (*pProcessed) return size;
    float dry = 1.0f - m_level;
    for (int i = 0; i < size; i++) {
        if (m_count % (m_sample + 1) == 0) {
            int v = floatToInt(pBuf[i] * 65536.0f);
            v &= -1 << (m_bit + 1);
            m_hold[0] = v / 65536.0f;
        }
        pBuf[i] = pBuf[i] * dry + m_hold[0] * m_level;
        m_count++;
    }
    return size;
}

int CDecimator::processSignalW(int size, float* pBuf, int* pProcessed)
{
    if (*pProcessed) return size;
    tagStereoData* p = (tagStereoData*)pBuf;
    float dry = 1.0f - m_level;
    for (int i = 0; i < size; i++) {
        float l, r;
        if (m_count % (m_sample + 1) == 0) {
            float fl = p[i].l * 65536.0f;
            l = m_hold[0] = (floatToInt(fl) & (-1 << (m_bit + 1))) / 65536.0f;
            float fr = p[i].r * 65536.0f;
            r = m_hold[1] = (floatToInt(fr) & (-1 << (m_bit + 1))) / 65536.0f;
        } else {
            l = m_hold[0];
            r = m_hold[1];
        }
        p[i].l = p[i].l * dry + l * m_level;
        p[i].r = p[i].r * dry + r * m_level;
        m_count++;
    }
    return size;
}
