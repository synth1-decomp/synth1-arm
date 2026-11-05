/**
 * @file distortion.cpp
 * @brief Implementation of the distortion effect: CBiquadFilter, CWaveShaper, CGtAmpSim1 and CDistortion.
 */
#include <math.h>
#include <string.h>
#include "common.h"
#include "distortion.h"

/**
 * @brief Tube transfer curve: 0 for negative input, rising smoothly towards 1 for large input.
 *
 * @param x  Input value.
 * @return 1 - 1 / (2 x^2 (x + 0.4) + 1) for x >= 0, otherwise 0.
 */
static inline float tubeCurve(float x)
{
    if (x < 0.0f) return 0.0f;
    return 1.0f - 1.0f / ((x + 0.4f) * x * x * 2.0f + 1.0f);
}

//------------------------------------------------------------------
// CDistortion

CDistortion::CDistortion()
{
    m_type = 0;
    m_threshold = 0.7f;
    m_level = 1.0f;
    m_silentCount = 0;
    m_silentLimit = (int)(m_samplingFreq * 0.025f);
    m_speaker.clearOldSig();
    setSpeakerTone(64);
    m_pAmp = new CGtAmpSim1;
}

void CBiquadFilter::clearOldSig()
{
    for (int i = 0; i < BIQUAD_MAX_STAGE; i++) {
        m_y2[0][i] = 0.0f;
        m_y1[0][i] = 0.0f;
        m_y0[0][i] = 0.0f;
        m_x2[0][i] = 0.0f;
        m_x1[0][i] = 0.0f;
        m_x0[0][i] = 0.0f;
        m_y2[1][i] = 0.0f;
        m_y1[1][i] = 0.0f;
        m_y0[1][i] = 0.0f;
        m_x2[1][i] = 0.0f;
        m_x1[1][i] = 0.0f;
        m_x0[1][i] = 0.0f;
    }
}

CDistortion::~CDistortion()
{
    if (m_pAmp) delete m_pAmp;
}

void CDistortion::onChangeSamplingFreq()
{
    m_silentLimit = (int)(m_samplingFreq * 0.025f);
    calcCoefsForSpeakerTone();
    m_pAmp->setSamplingFreq(m_samplingFreq);
}

void CDistortion::setSpeakerTone(int v)
{
    m_speakerTone = v;
    calcCoefsForSpeakerTone();
}

void CDistortion::calcCoefsForSpeakerTone()
{
    m_speaker.setPoles(2);
    float freq = fpow(40.0, m_speakerTone / 127.0) * 400.0;
    m_speaker.setParam(0, BQ_LPF, freq, 3.0f, 0.0f, m_samplingFreq);
    m_speaker.setParam(1, BQ_HPF, 100.0f, 2.3f, 0.0f, m_samplingFreq);
}

void CBiquadFilter::setPoles(int n)
{
    m_nStage = n;
}

void CBiquadFilter::setParam(int stage, int type, float freq, float q, float gain, int fs)
{
    float w = 2.0 * M_PI * freq / fs;
    float sn = sin(w);
    float cs = cos(w);
    float alpha = sn / (q + q);
    float A = pow(10.0, gain / 40.0f);
    float b0, b2, b1, a0, a1, a2;
    switch (type) {
    case BQ_LPF:
        b0 = (1.0f - cs) * 0.5f;
        b1 = 1.0f - cs;
        b2 = (1.0f - cs) * 0.5f;
        a0 = 1.0f + alpha;
        a1 = cs * -2.0f;
        a2 = 1.0f - alpha;
        break;
    case BQ_HPF:
        b0 = (1.0f + cs) * 0.5f;
        b1 = -(1.0f + cs);
        b2 = (1.0f + cs) * 0.5f;
        a0 = 1.0f + alpha;
        a1 = cs * -2.0f;
        a2 = 1.0f - alpha;
        break;
    case BQ_BPF:
        b0 = sn * 0.5f;
        b1 = 0.0f;
        b2 = sn * -0.5f;
        a0 = 1.0f + alpha;
        a1 = cs * -2.0f;
        a2 = 1.0f - alpha;
        break;
    case BQ_BPF2:
        b0 = alpha;
        b1 = 0.0f;
        b2 = -alpha;
        a0 = 1.0f + alpha;
        a1 = cs * -2.0f;
        a2 = 1.0f - alpha;
        break;
    case BQ_PEAKING:
        b0 = 1.0f + alpha * A;
        b1 = cs * -2.0f;
        b2 = 1.0f - alpha * A;
        a0 = 1.0f + alpha / A;
        a1 = cs * -2.0f;
        a2 = 1.0f - alpha / A;
        break;
    case BQ_LOWSHELF:
        {
            float S = 1.0f;
            float beta = sqrt(A) * sqrt((A + 1.0f / A) * (1.0f / S - 1.0f) + 2.0f);
            b0 = A * ((A + 1.0f) - (A - 1.0f) * cs + beta * sn);
            b1 = 2.0f * A * ((A - 1.0f) - (A + 1.0f) * cs);
            b2 = A * ((A + 1.0f) - (A - 1.0f) * cs - beta * sn);
            a0 = (A + 1.0f) + (A - 1.0f) * cs + beta * sn;
            a1 = -2.0f * ((A - 1.0f) + (A + 1.0f) * cs);
            a2 = (A + 1.0f) + (A - 1.0f) * cs - beta * sn;
        }
        break;
    }
    m_b0[stage] = b0 / a0;
    m_b1[stage] = b1 / a0;
    m_b2[stage] = b2 / a0;
    m_a1[stage] = a1 / a0;
    m_a2[stage] = a2 / a0;
}

void CDistortion::setDrive(int v)
{
    m_drive = fpow(10.0, (v / 127.0 * 70.0 - 10.0) / 20.0);
    m_pAmp->setDrive(m_drive);
    m_shaper.setDrive(m_drive);
}

void CGtAmpSim1::setDrive(float drive)
{
    m_drive = drive;
    m_shaper.setDrive(drive);
}

void CWaveShaper::setDrive(float drive)
{
    m_drive = drive;
    if (m_type == 1 || m_type == 2) {
        m_gain = 1.0f / (1.0f - 1.0f / ((drive + 1.0f) * (drive + 1.0f) * drive + 1.0f));
    } else if (m_type == 0) {
        m_gain = 1.0f / tubeCurve(drive) * 2.0f;
    }
}

void CDistortion::setLevel(int v)
{
    m_level = (v + 2) / 129.0f;
}

void CDistortion::setType(int type)
{
    m_type = type % 3;
    m_shaper.setType(m_type);
}

void CWaveShaper::setType(int type)
{
    m_type = type;
    setDrive(m_drive);
}

int CBiquadFilter::process(int size, float* pBuf)
{
    int n = m_nStage;
    for (int i = 0; i < size; i++) {
        float in = pBuf[i];
        for (long j = 0; j < n; j++) {
            m_y0[0][j] = m_b0[j] * in + m_b1[j] * m_x1[0][j] + m_b2[j] * m_x2[0][j]
                - m_a1[j] * m_y1[0][j] - m_a2[j] * m_y2[0][j];
            m_x2[0][j] = m_x1[0][j];
            m_x1[0][j] = in;
            m_y2[0][j] = m_y1[0][j];
            m_y1[0][j] = m_y0[0][j];
            in = m_y0[0][j];
        }
        pBuf[i] = in;
    }
    return size;
}

// both channels go through the stages only when there are stages; the right sample is
// read back either way
int CBiquadFilter::processLR(int size, tagStereoData* pBuf)
{
    int n = m_nStage;
    for (int i = 0; i < size; i++) {
        float in = pBuf[i].l;
        if (n > 0) {
            for (long j = 0; j < n; j++) {
                m_y0[0][j] = m_b0[j] * in + m_b1[j] * m_x1[0][j] + m_b2[j] * m_x2[0][j]
                    - m_a1[j] * m_y1[0][j] - m_a2[j] * m_y2[0][j];
                m_x2[0][j] = m_x1[0][j];
                m_x1[0][j] = in;
                m_y2[0][j] = m_y1[0][j];
                m_y1[0][j] = m_y0[0][j];
                in = m_y0[0][j];
            }
            pBuf[i].l = in;
            in = pBuf[i].r;
            for (long j = 0; j < n; j++) {
                m_y0[1][j] = m_b0[j] * in + m_b1[j] * m_x1[1][j] + m_b2[j] * m_x2[1][j]
                    - m_a1[j] * m_y1[1][j] - m_a2[j] * m_y2[1][j];
                m_x2[1][j] = m_x1[1][j];
                m_x1[1][j] = in;
                m_y2[1][j] = m_y1[1][j];
                m_y1[1][j] = m_y0[1][j];
                in = m_y0[1][j];
            }
        } else {
            in = pBuf[i].r;
        }
        pBuf[i].r = in;
    }
    return size;
}

/**
 * @brief Soft clipper that keeps the sign of the input.
 *
 * The magnitude a = |(x + 0.0001) * drive| is mapped to 1 - 1 / ((a + 1)^2 a + 1), which saturates
 * towards 1; the sign bit of the driven input is copied onto the result.
 *
 * @param x      Input sample.
 * @param drive  Input gain.
 * @param gain   Output gain (level times gain compensation).
 * @return The clipped sample.
 */
static inline float softClip(float x, float drive, float gain)
{
    float v = (x + 0.0001f) * drive;
    unsigned int sign = *(unsigned int*)&v & 0x80000000;
    double a = fabs(v);
    float y = 1.0 - 1.0 / ((a + 1.0) * (a + 1.0) * a + 1.0);
    y *= gain;
    *(unsigned int*)&y |= sign;
    return y;
}

/**
 * @brief Fold back distortion: values beyond +/-th are reflected back into the range.
 *
 * @param th  Threshold (positive).
 * @param x   Input sample.
 * @return x unchanged if |x| <= th, otherwise x folded back into -th..th.
 */
static inline float foldBack(float th, float x)
{
    if (x > th || x < -th) {
        x = fabs(fabs(fmod(x - th, th * 4.0f)) - th * 2.0f) - th;
    }
    return x;
}

void CWaveShaper::process(float* pBuf, int size, float level)
{
    float gain = level * m_gain;
    float drive = m_drive;
    if (m_type == 1) {
        for (int i = 0; i < size; i++) {
            pBuf[i] = softClip(pBuf[i], drive, gain);
        }
    } else {
        for (int i = 0; i < size; i++) {
            pBuf[i] = foldBack(m_threshold, pBuf[i] * drive) * gain;
        }
    }
}

void CWaveShaper::processLR(tagStereoData* pBuf, int size, float level)
{
    float gain = level * m_gain;
    float drive = m_drive;
    if (m_type == 1) {
        for (int i = 0; i < size; i++) {
            pBuf[i].l = softClip(pBuf[i].l, drive, gain);
            pBuf[i].r = softClip(pBuf[i].r, drive, gain);
        }
    } else {
        for (int i = 0; i < size; i++) {
            pBuf[i].l = foldBack(m_threshold, pBuf[i].l * drive) * gain;
            pBuf[i].r = foldBack(m_threshold, pBuf[i].r * drive) * gain;
        }
    }
}

//------------------------------------------------------------------
// CGtAmpSim1

CGtAmpSim1::CGtAmpSim1()
{
    m_samplingFreq = 44100;
    m_shaper.setDrive(1.0f);
    m_shaper.setType(0);
    calcCoef();
    m_out1[0] = m_out1[1] = 0.0f;
    m_out2[0] = m_out2[1] = 0.0f;
    m_out3[0] = m_out3[1] = 0.0f;
    // let the bias filters settle
    // MATCH-HACK: the `i < 0` exit never triggers and emits no code; it makes the compiler keep
    // this loop's exit test as in the released binary. See docs/MATCH_HACKS.md.
    for (int i = 0; i < 600; i++) {
        processSingle(0.0f, 0, 0.0f, 0.0f);
        processSingle(0.0f, 1, 0.0f, 0.0f);
        if (i < 0) break;
    }
}

void CGtAmpSim1::setSamplingFreq(int fs)
{
    m_samplingFreq = fs;
    calcCoef();
}

void CGtAmpSim1::calcCoef()
{
    double fs = m_samplingFreq;
    float cBias1 = 2.0 * M_PI * 86.0 / fs;
    float cHpf = 1.0 / (2.0 * M_PI * 31.0 / fs + 1.0);
    float cLpf = 2.0 * M_PI * 6531.0 / fs;
    float cBias2 = 2.0 * M_PI * 132.0 / fs;
    float cBias3 = 2.0 * M_PI * 194.0 / fs;
    for (int ch = 0; ch < 2; ch++) {
        m_bias1[ch].m_coef = cBias1;
        m_hpf1[ch].m_coef = cHpf;
        m_lpf1[ch].m_coef = cLpf;
        m_bias2[ch].m_coef = cBias2;
        m_hpf2[ch].m_coef = cHpf;
        m_lpf2[ch].m_coef = cLpf;
        m_bias3[ch].m_coef = cBias3;
        m_hpf3[ch].m_coef = cHpf;
    }
}

inline float CGtAmpSim1::processSingle(float in, int ch, float drive, float level)
{
    float x = in * drive;
    x += m_bias1[ch].process((1.0f - m_out1[ch]) * 5.0f);
    m_out1[ch] = tubeCurve(x);
    x = m_lpf1[ch].process(m_hpf1[ch].process(m_out1[ch]));
    x += m_bias2[ch].process(1.0f - m_out2[ch]);
    m_out2[ch] = tubeCurve(x);
    x = m_lpf2[ch].process(m_hpf2[ch].process(m_out2[ch]));
    x += m_bias3[ch].process(1.0f - m_out3[ch]);
    m_out3[ch] = tubeCurve(x);
    return m_hpf3[ch].process(m_out3[ch]) * level;
}

void CGtAmpSim1::process(float* pBuf, int size, float level)
{
    float drive = m_shaper.m_drive;
    level *= m_shaper.m_gain;
    for (int i = 0; i < size; i++) {
        pBuf[i] = processSingle(pBuf[i], 0, drive, level);
    }
}

void CGtAmpSim1::processLR(tagStereoData* pBuf, int size, float level)
{
    float drive = m_shaper.m_drive;
    level *= m_shaper.m_gain;
    for (int i = 0; i < size; i++) {
        processSingleLR(&pBuf[i], drive, level);
    }
}

inline void CGtAmpSim1::processSingleLR(tagStereoData* pBuf, float drive, float level)
{
    float xl = pBuf->l * drive;
    float xr = pBuf->r * drive;
    xl += m_bias1[0].process((1.0f - m_out1[0]) * 5.0f);
    xr += m_bias1[1].process((1.0f - m_out1[1]) * 5.0f);
    m_out1[0] = tubeCurve(xl);
    m_out1[1] = tubeCurve(xr);
    xl = m_hpf1[0].process(m_out1[0]);
    xr = m_hpf1[1].process(m_out1[1]);
    xl = m_lpf1[0].process(xl);
    xr = m_lpf1[1].process(xr);
    xl += m_bias2[0].process(1.0f - m_out2[0]);
    xr += m_bias2[1].process(1.0f - m_out2[1]);
    m_out2[0] = tubeCurve(xl);
    m_out2[1] = tubeCurve(xr);
    xl = m_hpf2[0].process(m_out2[0]);
    xr = m_hpf2[1].process(m_out2[1]);
    xl = m_lpf2[0].process(xl);
    xr = m_lpf2[1].process(xr);
    xl += m_bias3[0].process(1.0f - m_out3[0]);
    xr += m_bias3[1].process(1.0f - m_out3[1]);
    m_out3[0] = tubeCurve(xl);
    m_out3[1] = tubeCurve(xr);
    xl = m_hpf3[0].process(m_out3[0]);
    xr = m_hpf3[1].process(m_out3[1]);
    pBuf->l = xl * level;
    pBuf->r = xr * level;
}

//------------------------------------------------------------------

int CDistortion::processSignal(int size, float* pBuf, int* pProcessed)
{
    return processSignalMain(size, pBuf, pProcessed, 0);
}

int CDistortion::processSignalW(int size, float* pBuf, int* pProcessed)
{
    return processSignalMain(size, pBuf, pProcessed, 1);
}

int CDistortion::processSignalMain(int size, float* pBuf, int* pProcessed, int bStereo)
{
    if (*pProcessed) {
        // input is silent: keep running until the tail has decayed
        if (m_silentCount > m_silentLimit) return size;
        m_silentCount += size;
        if (bStereo) {
            for (int i = 0; i < size * 2; i++) pBuf[i] = 0.0f;
        } else {
            for (int i = 0; i < size; i++) pBuf[i] = 0.0f;
        }
        *pProcessed = 0;
    } else {
        m_silentCount = 0;
    }
    if (!bStereo) processMono(size, pBuf);
    else processStereo(size, (tagStereoData*)pBuf);
    return size;
}

void CDistortion::processMono(int size, float* pBuf)
{
    if (m_type == 1 || m_type == 2) {
        m_shaper.process(pBuf, size, m_level);
    } else if (m_type == 0) {
        m_pAmp->process(pBuf, size, m_level);
    }
    m_speaker.process(size, pBuf);
}

void CDistortion::processStereo(int size, tagStereoData* pBuf)
{
    if (m_type == 1 || m_type == 2) {
        m_shaper.processLR(pBuf, size, m_level);
    } else if (m_type == 0) {
        m_pAmp->processLR(pBuf, size, m_level);
    }
    m_speaker.processLR(size, pBuf);
}
