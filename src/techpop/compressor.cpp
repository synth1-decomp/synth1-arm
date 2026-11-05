/**
 * @file compressor.cpp
 * @brief Compressor effect: envelope follower, single-channel compressor and the stereo
 *        CCompressor device.
 */
#include <math.h>
#include "common.h"
#include "compressor.h"
#include "debug.h"


//------------------------------------------------------------------
// EnvelopeDetector

EnvelopeDetector::EnvelopeDetector(float ms, float sampleRate)
{
    m_sampleRate = sampleRate;
    m_ms = ms;
    setCoef();
}

void EnvelopeDetector::setCoef()
{
    m_coef = exp(-1.0 / (m_ms * 0.001 * m_sampleRate));
}

void EnvelopeDetector::setTc(float ms)
{
    m_ms = ms;
    setCoef();
}

void EnvelopeDetector::setSampleRate(float sampleRate)
{
    m_sampleRate = sampleRate;
    setCoef();
}

//------------------------------------------------------------------
// SimpleComp

SimpleComp::SimpleComp()
{
    m_att.setSampleRate(44100.0f);
    m_att.setTc(10.0f);
    m_rel.setSampleRate(44100.0f);
    m_rel.setTc(100.0f);
    m_env = 0.0f;
    setThresh(0.0f);
    setRatio(1.0f);
}

void SimpleComp::setThresh(float dB)
{
    m_threshdB = dB;
    m_thresh = exp(dB * DB2EXP);
}

void SimpleComp::setRatio(float ratio)
{
    m_ratio = ratio;
}

void SimpleComp::setAttack(float ms)
{
    m_att.setTc(ms);
}

void SimpleComp::setRelease(float ms)
{
    m_rel.setTc(ms);
}

void SimpleComp::setSampleRate(float sampleRate)
{
    m_att.setSampleRate(sampleRate);
    m_rel.setSampleRate(sampleRate);
}

void SimpleComp::initRuntime()
{
    m_env = 0.0f;
}

float SimpleComp::process(float in)
{
    float over = fabs(in) - m_thresh;
    if (over < 0.0f) over = 0.0f;
    m_att.run(over, m_env);
    killDenormal(m_env);
    return in * m_thresh / (m_thresh + m_env);
}

//------------------------------------------------------------------
// CCompressor

CCompressor::CCompressor()
{
    for (int i = 0; i < 2; i++) {
        m_comp[i].setThresh(-10.0f);
        m_comp[i].setRatio(-1.0f);
        m_comp[i].setAttack(1.0f);
        m_comp[i].setRelease(1.0f);
    }
}

CCompressor::~CCompressor()
{
}

void CCompressor::onChangeSamplingFreq()
{
    m_comp[0].setSampleRate((float)m_samplingFreq);
    m_comp[1].setSampleRate((float)m_samplingFreq);
}

void CCompressor::setType(int type)
{
    m_type = type;
}

void CCompressor::setAttack(float v)
{
    m_attack = v;
    float ms = fpow(2000.0, sqrt(v)) * 0.1;
    edPrintf("attack time=%.1f ms\n", ms);
    for (int i = 0; i < 2; i++) {
        m_comp[i].setAttack(ms);
        m_comp[i].setRelease(ms);
    }
}

void CCompressor::setRelease(float v)
{
    m_release = v;
}

void CCompressor::setRatio(float v)
{
    m_ratio = v;
}

void CCompressor::setDepth(float v)
{
    float thresh = v * -40.0f - 20.0f;
    for (int i = 0; i < 2; i++) {
        m_comp[i].setThresh(thresh);
        m_comp[i].setRatio(0.0f);
    }
    if (thresh > -20.0f) m_makeup = 1.0f;
    else m_makeup = exp((-20.0f - thresh) * DB2EXP);
    edPrintf("thresh =%.0f db\n", thresh);
}

void CCompressor::setLevel(int v)
{
    m_level = exp(((float)v / 127.0f * 30.0f - 20.0f) * DB2EXP);
}

int CCompressor::processSignal(int size, float* pBuf, int* pProcessed)
{
    if (*pProcessed) {
        m_comp[0].initRuntime();
        return size;
    }
    float gain = m_makeup * m_level;
    for (int i = 0; i < size; i++) {
        pBuf[i] = m_comp[0].process(pBuf[i]) * gain;
    }
    return size;
}

int CCompressor::processSignalW(int size, float* pBuf, int* pProcessed)
{
    if (*pProcessed) {
        m_comp[0].initRuntime();
        m_comp[1].initRuntime();
        return size;
    }
    float gain = m_makeup * m_level;
    tagStereoData* p = (tagStereoData*)pBuf;
    for (int i = 0; i < size; i++) {
        p[i].l = m_comp[0].process(p[i].l) * gain;
        p[i].r = m_comp[1].process(p[i].r) * gain;
    }
    return size;
}
