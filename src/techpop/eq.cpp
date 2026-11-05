/**
 * @file eq.cpp
 * @brief CEq: peaking / low pass / high pass equalizer band, plus the per-sample filter loops.
 */

#include <math.h>
#include <string.h>
#include "common.h"
#include "eq.h"

#define E_VAL   2.718281828f

CEq::CEq()
{
    m_gain = 0;
    m_level = 1.0f;
    m_freq = 100.0f;
    m_q = 3.0f;
    m_mode = EQ_MODE_PEAKING;
    m_bEnable = 1;
    m_bStereo = 1;
    m_silentCount = 0;
    memset(&m_x1, 0, sizeof(tagStereoData) * 4);
    m_silentLimit = (int)(m_samplingFreq * 0.2f);
}

void CEq::onChangeSamplingFreq()
{
    m_silentLimit = (int)(m_samplingFreq * 0.2f);
    calcCoefficients();
}

// Note: calling fpow() inside each arm, keeping c in double, and summing den/num where they
// are defined are significant for reproducing the released binary.
void CEq::calcCoefficients()
{
    if (m_mode == EQ_MODE_PEAKING) {
        float w = (float)(2.0 * M_PI * m_freq);
        float A = (float)fpow(10.0, m_gain / 20.0f);
        m_invFs = 1.0f / m_samplingFreq;
        float T = m_invFs;
        float dp, dz;   // damping of poles / zeros
        if (A >= 1.0f) {
            dp = 1.0f / (m_q * 2);
            dz = dp * A;
        } else {
            dz = 1.0f / (m_q * 2);
            dp = dz / A;
        }
        float wT = T * w;
        float ez = (float)fpow(E_VAL, dz * -wT);
        float ep = (float)fpow(E_VAL, -wT * dp);

        double c;
        if (fabs(dz) > 1.0) {
            float s = sqrtf((float)fpow(dz, 2.0) - 1.0f);
            c = cosh(s * T * w);
        } else {
            float s = sqrtf(1.0f - (float)fpow(dz, 2.0));
            c = cos(s * T * w);
        }
        m_b1 = -2.0f * ez * (float)c;
        m_b2 = (float)fpow(ez, 2.0);

        if (fabs(dp) > 1.0) {
            float s = sqrtf((float)fpow(dp, 2.0) - 1.0f);
            c = cosh(T * s * w);
        } else {
            float s = sqrtf(1.0f - (float)fpow(dp, 2.0));
            c = cos(T * s * w);
        }
        m_a1 = -2.0f * ep * (float)c;
        m_a2 = (float)fpow(ep, 2.0);

        // normalize so that the gain at the center frequency is A
        float den = (float)fpow(m_a1 + (1.0 + m_a2) * (float)cos(wT), 2.0)
                  + (float)fpow(-(m_a2 - 1.0) * (float)sin(wT), 2.0);
        float num = (float)fpow(m_b1 + (1.0 + m_b2) * (float)cos(wT), 2.0)
                  + (float)fpow(-(m_b2 - 1.0) * (float)sin(wT), 2.0);
        float g = sqrtf(den) * A / sqrtf(num);
        m_b0 = g;
        m_nb1 = m_b1 * g;
        m_nb2 = m_b2 * g;
    } else if (m_mode == EQ_MODE_LOWPASS) {
        float k = (float)(2.0 * m_samplingFreq);
        float wd = (float)(2 * m_freq * M_PI);
        float inv = 1.0f / (k + wd);
        m_a1 = (k - wd) * inv;
        m_b0 = m_b1 = wd * inv;
    } else {
        float k = (float)(2.0 * m_samplingFreq);
        float wd = (float)(2 * m_freq * M_PI);
        float inv = 1.0f / (k + wd);
        m_b0 = k * inv;
        m_b1 = -m_b0;
        m_a1 = (k - wd) * inv;
    }
}

void CEq::setMode(int mode)
{
    m_mode = mode;
    calcCoefficients();
}

void CEq::setStereoMode(int bStereo)
{
    m_bStereo = bStereo;
}

void CEq::setGain(float gain)
{
    m_gain = gain;
    if (m_mode != EQ_MODE_PEAKING) {
        m_level = (float)fpow(10.0, gain / 20.0f);
    } else {
        calcCoefficients();
    }
}

void CEq::setFreq(float freq)
{
    if (freq < 1.0f) freq = 1.0f;
    m_freq = freq;
    calcCoefficients();
}

void CEq::setQ(float q)
{
    m_q = q;
    calcCoefficients();
}

/**
 * @brief First-order low pass, mono, in place: y = (x + x1) * b0 + y1 * a1.
 *
 * @param[in,out] pBuf  Samples to filter.
 * @param[in,out] pY1   Previous output (filter state).
 * @param[in,out] pX1   Previous input (filter state).
 * @param b0            Feed-forward coefficient (used for both taps).
 * @param b1            Not used (equal to b0 for this filter).
 * @param a1            Feedback coefficient.
 * @param size          Number of samples.
 */
void processLowPass(float* pBuf, float* pY1, float* pX1, float b0, float b1, float a1, int size)
{
    float x;
    float y1 = *pY1;
    float x1 = *pX1;
    for (int i = 0; i < size; i++) {
        x = pBuf[i];
        y1 = (x + x1) * b0 + y1 * a1;
        pBuf[i] = y1;
        x1 = x;
    }
    *pY1 = y1;
    *pX1 = x1;
}

/**
 * @brief First-order high pass, mono, in place: y = (x - x1) * b0 + y1 * a1.
 *
 * @param[in,out] pBuf  Samples to filter.
 * @param[in,out] pY1   Previous output (filter state).
 * @param[in,out] pX1   Previous input (filter state).
 * @param b0            Feed-forward coefficient (used for both taps, with opposite signs).
 * @param b1            Not used (equal to -b0 for this filter).
 * @param a1            Feedback coefficient.
 * @param size          Number of samples.
 */
void processHighPass(float* pBuf, float* pY1, float* pX1, float b0, float b1, float a1, int size)
{
    float y1 = *pY1;
    float x1 = *pX1;
    for (int i = 0; i < size; i++) {
        float x = pBuf[i];
        y1 = (x - x1) * b0 + y1 * a1;
        pBuf[i] = y1;
        x1 = x;
    }
    *pY1 = y1;
    *pX1 = x1;
}

/**
 * @brief First-order low pass on interleaved stereo data, in place (see processLowPass()).
 *
 * @param[in,out] pBuf  Stereo samples to filter.
 * @param[in,out] pY1   Previous output of both channels.
 * @param[in,out] pX1   Previous input of both channels.
 * @param b0            Feed-forward coefficient.
 * @param b1            Not used.
 * @param a1            Feedback coefficient.
 * @param size          Number of stereo samples.
 */
void processLowPassW(tagStereoData* pBuf, tagStereoData* pY1, tagStereoData* pX1, float b0, float b1, float a1, int size)
{
    tagStereoData x;
    tagStereoData y1 = *pY1;
    tagStereoData x1 = *pX1;
    for (int i = 0; i < size; i++) {
        x = pBuf[i];
        y1.l = (x.l + x1.l) * b0 + y1.l * a1;
        y1.r = (x.r + x1.r) * b0 + y1.r * a1;
        pBuf[i] = y1;
        x1 = x;
    }
    *pY1 = y1;
    *pX1 = x1;
}

/**
 * @brief First-order high pass on interleaved stereo data, in place (see processHighPass()).
 *
 * @param[in,out] pBuf  Stereo samples to filter.
 * @param[in,out] pY1   Previous output of both channels.
 * @param[in,out] pX1   Previous input of both channels.
 * @param b0            Feed-forward coefficient.
 * @param b1            Not used.
 * @param a1            Feedback coefficient.
 * @param size          Number of stereo samples.
 */
void processHighPassW(tagStereoData* pBuf, tagStereoData* pY1, tagStereoData* pX1, float b0, float b1, float a1, int size)
{
    tagStereoData x;
    tagStereoData y1 = *pY1;
    tagStereoData x1 = *pX1;
    for (int i = 0; i < size; i++) {
        x = pBuf[i];
        y1.l = (x.l - x1.l) * b0 + y1.l * a1;
        y1.r = (x.r - x1.r) * b0 + y1.r * a1;
        pBuf[i] = y1;
        x1 = x;
    }
    *pY1 = y1;
    *pX1 = x1;
}

/**
 * @brief Biquad (peaking EQ) on interleaved stereo data, in place.
 *
 * y = x b0 + x1 b1 + x2 b2 - y1 a1 - y2 a2 for each channel.
 *
 * @param[in,out] pBuf  Stereo samples to filter.
 * @param[in,out] pY1   Output delayed by one sample (both channels).
 * @param[in,out] pY2   Output delayed by two samples.
 * @param[in,out] pX1   Input delayed by one sample.
 * @param[in,out] pX2   Input delayed by two samples.
 * @param b0            Feed-forward coefficient of x.
 * @param b1            Feed-forward coefficient of x1.
 * @param b2            Feed-forward coefficient of x2.
 * @param a1            Feedback coefficient of y1.
 * @param a2            Feedback coefficient of y2.
 * @param size          Number of stereo samples.
 */
void processParaEQW(tagStereoData* pBuf, tagStereoData* pY1, tagStereoData* pY2, tagStereoData* pX1, tagStereoData* pX2,
    float b0, float b1, float b2, float a1, float a2, int size)
{
    tagStereoData x, y;
    tagStereoData x1 = *pX1, x2 = *pX2, y1 = *pY1, y2 = *pY2;
    for (int i = 0; i < size; i++) {
        x = pBuf[i];
        y.l = x.l * b0 + x1.l * b1 + x2.l * b2 - y1.l * a1 - y2.l * a2;
        y.r = x.r * b0 + x1.r * b1 + x2.r * b2 - y1.r * a1 - y2.r * a2;
        pBuf[i] = y;
        x2 = x1;
        x1 = x;
        y2 = y1;
        y1 = y;
    }
    *pX1 = x1;
    *pX2 = x2;
    *pY1 = y1;
    *pY2 = y2;
}

/**
 * @brief Biquad (peaking EQ) on a mono buffer, in place (see processParaEQW()).
 *
 * @param[in,out] pBuf  Samples to filter.
 * @param[in,out] pY1   Output delayed by one sample.
 * @param[in,out] pY2   Output delayed by two samples.
 * @param[in,out] pX1   Input delayed by one sample.
 * @param[in,out] pX2   Input delayed by two samples.
 * @param b0            Feed-forward coefficient of x.
 * @param b1            Feed-forward coefficient of x1.
 * @param b2            Feed-forward coefficient of x2.
 * @param a1            Feedback coefficient of y1.
 * @param a2            Feedback coefficient of y2.
 * @param size          Number of samples.
 */
void processParaEQ(float* pBuf, float* pY1, float* pY2, float* pX1, float* pX2,
    float b0, float b1, float b2, float a1, float a2, int size)
{
    float x, y;
    float x1 = *pX1, x2 = *pX2, y1 = *pY1, y2 = *pY2;
    for (int i = 0; i < size; i++) {
        x = pBuf[i];
        y = x * b0 + x1 * b1 + x2 * b2 - y1 * a1 - y2 * a2;
        pBuf[i] = y;
        y2 = y1;
        y1 = y;
        x2 = x1;
        x1 = x;
    }
    *pX1 = x1;
    *pX2 = x2;
    *pY1 = y1;
    *pY2 = y2;
}

int CEq::processSignalW(int size, float* pBuf, int* pProcessed)
{
    if (!m_bStereo) return CSignalDevice::processSignalW(size, pBuf, pProcessed);

    int n = m_pInput->processSignalW(size, pBuf, pProcessed);
    if (!m_bEnable) return n;
    switch (m_mode) {
    case EQ_MODE_PEAKING:
        if (m_gain == 0) return n;
        break;
    case EQ_MODE_LOWPASS:
        if (m_freq >= m_samplingFreq / 2) return n;
        break;
    case EQ_MODE_HIGHPASS:
        if (m_freq <= 1.0f) return n;
        break;
    }
    if (!*pProcessed) {
        m_silentCount = 0;
    } else {
        if (m_silentCount > m_silentLimit) return n;
        m_silentCount += n;
        for (int i = 0; i < n * 2; i++) pBuf[i] = 1.0e-6f;
        *pProcessed = 0;
    }
    return processSignalWForced(n, pBuf, pProcessed);
}

int CEq::processSignal(int size, float* pBuf, int* pProcessed)
{
    int n = m_pInput->processSignal(size, pBuf, pProcessed);
    if (!m_bEnable) return n;
    switch (m_mode) {
    case EQ_MODE_PEAKING:
        if (m_gain == 0) return n;
        break;
    case EQ_MODE_LOWPASS:
        if (m_freq >= m_samplingFreq / 2) return n;
        break;
    case EQ_MODE_HIGHPASS:
        if (m_freq <= 1.0f) return n;
        break;
    }
    if (!*pProcessed) {
        m_silentCount = 0;
    } else {
        if (m_silentCount > m_silentLimit) return n;
        m_silentCount += n;
        for (int i = 0; i < n; i++) pBuf[i] = 1.0e-6f;
        *pProcessed = 0;
    }
    return processSignalForced(n, pBuf, pProcessed);
}

int CEq::processSignalForced(int size, float* pBuf, int* pProcessed)
{
    if (m_bEnable) {
        if (m_mode == EQ_MODE_PEAKING) {
            processParaEQ(pBuf, &m_y1.l, &m_y2.l, &m_x1.l, &m_x2.l, m_b0, m_nb1, m_nb2, m_a1, m_a2, size);
        } else {
            if (m_mode == EQ_MODE_LOWPASS) processLowPass(pBuf, &m_y1.l, &m_x1.l, m_b0, m_b1, m_a1, size);
            else processHighPass(pBuf, &m_y1.l, &m_x1.l, m_b0, m_b1, m_a1, size);
            if (m_level != 1.0f) mulArray2SSE(pBuf, m_level, size);
        }
    }
    return size;
}

int CEq::processSignalWForced(int size, float* pBuf, int* pProcessed)
{
    if (m_bEnable) {
        if (m_mode == EQ_MODE_PEAKING) {
            processParaEQW((tagStereoData*)pBuf, &m_y1, &m_y2, &m_x1, &m_x2, m_b0, m_nb1, m_nb2, m_a1, m_a2, size);
        } else {
            if (m_mode == EQ_MODE_LOWPASS) processLowPassW((tagStereoData*)pBuf, &m_y1, &m_x1, m_b0, m_b1, m_a1, size);
            else processHighPassW((tagStereoData*)pBuf, &m_y1, &m_x1, m_b0, m_b1, m_a1, size);
            if (m_level != 1.0f) mulArray2SSE(pBuf, m_level, size * 2);
        }
    }
    return size;
}
