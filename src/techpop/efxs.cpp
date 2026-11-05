/**
 * @file efxs.cpp
 * @brief Effect section (CEffector): effect selection and parameter distribution.
 */
#include <math.h>
#include "common.h"
#include "efxs.h"

CEffector::CEffector()
{
    m_bEnable = 1;
    setType(EFX_DIST1);
    setCont1(0);
    setCont2(0);
    setLevel(0);
}

void CEffector::setType(int type)
{
    m_type = type;
    switch (type) {
    case EFX_DIST1:
        m_pEffect = &m_dist;
        m_dist.setType(0);
        break;
    case EFX_DIST2:
        m_pEffect = &m_dist;
        m_dist.setType(1);
        break;
    case EFX_DIST3:
        m_pEffect = &m_dist;
        m_dist.setType(2);
        break;
    case EFX_DECIMATOR:
        m_pEffect = &m_decimator;
        break;
    case EFX_RINGMOD:
        m_pEffect = &m_ringMod;
        break;
    case EFX_COMPRESSOR:
        m_pEffect = &m_comp;
        break;
    case EFX_PHASER1:
        m_pEffect = &m_phaser;
        m_phaser.setStage(1);
        break;
    case EFX_PHASER2:
        m_pEffect = &m_phaser;
        m_phaser.setStage(2);
        break;
    case EFX_PHASER3:
        m_pEffect = &m_phaser;
        m_phaser.setStage(4);
        break;
    case EFX_PHASER4:
        m_pEffect = &m_phaser;
        m_phaser.setStage(6);
        break;
    }
}

void CEffector::setCont1(int v)
{
    m_cont1 = v;
    m_dist.setDrive(v);
    m_comp.setDepth(v / 127.0f);
    m_ringMod.setFreq(fpow(8000.0, v / 127.0f));
    m_decimator.setSample(v);
    float minFreq = pow(10.0, (127 - v) / 127.0f) * 100.0;
    float maxFreq = pow(10.0, v / 127.0f) * 1000.0;
    m_phaser.setRange(minFreq, maxFreq);
}

void CEffector::setCont2(int v)
{
    m_cont2 = v;
    m_dist.setSpeakerTone(v);
    m_comp.setAttack(v / 127.0f);
    m_decimator.setBit(v / 8);
    if (v == 0) m_phaser.setRate(0.01f);
    else m_phaser.setRate(fpow(1000.0, v / 127.0f) * 0.02);
}

void CEffector::setLevel(int v)
{
    m_level = v;
    m_dist.setLevel(v);
    m_comp.setLevel(v);
    m_ringMod.setLevel(v / 127.0f);
    m_decimator.setLevel(v / 127.0f);
    m_phaser.setFeedback((v - 64) / 64.0f * 0.99f);
}

CEffector::~CEffector()
{
}

void CEffector::onChangeSamplingFreq()
{
    m_dist.setSamplingFreq(m_samplingFreq);
    m_comp.setSamplingFreq(m_samplingFreq);
    m_ringMod.setSamplingFreq(m_samplingFreq);
    m_decimator.setSamplingFreq(m_samplingFreq);
    m_phaser.setSamplingFreq(m_samplingFreq);
}

void CEffector::setEnable(int b)
{
    m_bEnable = b;
}

int CEffector::getEnable()
{
    return m_bEnable;
}

int CEffector::getType()
{
    return m_type;
}

int CEffector::processSignal(int size, float* pBuf, int* pProcessed)
{
    int n = m_pInput->processSignal(size, pBuf, pProcessed);
    if (m_bEnable) m_pEffect->processSignal(n, pBuf, pProcessed);
    return n;
}

int CEffector::processSignalW(int size, float* pBuf, int* pProcessed)
{
    int n = m_pInput->processSignalW(size, pBuf, pProcessed);
    if (m_bEnable) m_pEffect->processSignalW(n, pBuf, pProcessed);
    return n;
}
