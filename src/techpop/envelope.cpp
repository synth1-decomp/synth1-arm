/**
 * @file envelope.cpp
 * @brief CEnvelopedObject: ADSR envelope generator shared by the VCO, VCF and VCA.
 */

#include <stdlib.h>
#include <string.h>
#include <math.h>
#include "envelope.h"
#include "lfo.h"

#define MIN(a,b)    ((a)<(b)?(a):(b))
#define MAX(a,b)    ((a)>(b)?(a):(b))

CEnvelopedObject::CEnvelopedObject()
{
    m_max = 100;
    m_attackLen = 1;
    m_decayLen = 1;
    m_sustain = 1.0f;
    m_sustainLevel = 100;
    m_gain = 1.0f;
    m_releaseLen = 1;
    m_curLevel = 0;
    m_curPos = 0;
    m_releaseLevel = 0;
    m_bProcessIdle = 0;
    m_bNoInput = 0;
    m_bEnvEnable = 1;
    m_bInvert = 0;
    m_bPostProcess = 0;
    m_peakLevel = 100;
    m_bSmooth = 0;
    m_envStatus = ENV_OFF;
    m_decayType = 0;
    m_releaseType = 0;
    m_bLfoEnable[0] = 0;
    m_bLfoEnable[1] = 0;
    m_pLfo[1] = NULL;
    m_pLfo[0] = NULL;
    m_prevSamplingFreq = m_samplingFreq;
}

void CEnvelopedObject::setMax(int max)
{
    m_max = max;
    int prev = m_peakLevel;
    m_peakLevel = (int)(max * m_gain);
    m_sustainLevel = (int)(max * m_gain * m_sustain);
    m_curLevel = prev ? m_peakLevel * m_curLevel / prev : 0;
}

void CEnvelopedObject::setDecayType(int type)
{
    m_decayType = type;
}

void CEnvelopedObject::setGain(float gain)
{
    bool bNegative = gain < 0;
    if(bNegative) gain = -gain;
    int bInvert = bNegative;
    m_gain = gain;
    m_bInvert = bInvert;
    int prev = m_peakLevel;
    m_peakLevel = (int)(m_max * gain);
    m_sustainLevel = (int)(m_max * gain * m_sustain);
    if(m_envStatus == ENV_SUSTAIN){
        m_curLevel = m_sustainLevel;
    }else{
        m_curLevel = prev ? (int)((float)m_curLevel * m_peakLevel / prev) : 0;
    }
}

void CEnvelopedObject::onChangeSamplingFreq()
{
    int prevFs = m_prevSamplingFreq;
    m_prevSamplingFreq = m_samplingFreq;

    int prev = m_attackLen;
    m_attackLen = (long)(prev / (float)prevFs * m_samplingFreq);
    if(m_envStatus == ENV_ATTACK){
        m_curPos = prev ? (long)m_curPos * m_attackLen / prev : 0;
    }
    prev = m_decayLen;
    m_decayLen = (long)(prev / (float)prevFs * m_samplingFreq);
    if(m_envStatus == ENV_DECAY){
        m_curPos = prev ? (long)m_curPos * m_decayLen / prev : 0;
    }
    prev = m_releaseLen;
    m_releaseLen = (long)(prev / (float)prevFs * m_samplingFreq);
    if(m_envStatus == ENV_RELEASE){
        m_curPos = prev ? (long)m_curPos * m_releaseLen / prev : 0;
    }
}

void CEnvelopedObject::setAttack(float sec)
{
    int prev = m_attackLen;
    m_attackLen = (long)(m_samplingFreq * sec);
    if(m_envStatus == ENV_ATTACK){
        m_curPos = prev ? (long)m_curPos * m_attackLen / prev : 0;
    }
}

void CEnvelopedObject::setDecay(float sec)
{
    int prev = m_decayLen;
    m_decayLen = (long)(m_samplingFreq * sec);
    if(m_envStatus == ENV_DECAY){
        m_curPos = prev ? (long)m_curPos * m_decayLen / prev : 0;
    }
}

void CEnvelopedObject::setRelease(float sec)
{
    int prev = m_releaseLen;
    m_releaseLen = (long)(m_samplingFreq * sec);
    if(m_envStatus == ENV_RELEASE){
        m_curPos = prev ? (long)m_curPos * m_releaseLen / prev : 0;
    }
}

void CEnvelopedObject::setSustain(float level)
{
    m_sustain = level;
    m_sustainLevel = (int)(m_max * m_gain * level);
    if(m_envStatus == ENV_SUSTAIN){
        m_curLevel = m_sustainLevel;
    }
}

void CEnvelopedObject::setEnvelopEnable(int bEnable)
{
    m_bEnvEnable = bEnable;
}

int CEnvelopedObject::getEnvStatus()
{
    return m_envStatus;
}

void CEnvelopedObject::envelopeOn()
{
    m_envStatus = ENV_ATTACK;
    // restart the attack from the current level
    m_curPos = m_curLevel ? m_curLevel * m_attackLen / m_peakLevel : 0;
}

void CEnvelopedObject::envelopeOff()
{
    if(m_envStatus == ENV_OFF || m_envStatus == ENV_RELEASE) return;
    m_envStatus = ENV_RELEASE;
    m_releaseLevel = m_curLevel;
    m_curPos = 0;
    m_nextResPos = 0;
    calcDRResolution();
}

// step size of the decay/release segment: finer at the start of the curve
void CEnvelopedObject::calcDRResolution()
{
    int len, range;
    if(m_envStatus == ENV_DECAY){
        len = m_decayLen;
        range = m_peakLevel - m_sustainLevel;
    }else{
        len = m_releaseLen;
        range = m_releaseLevel;
    }
    int res = len / MAX(range, 1);
    if(m_decayType == ENV_CURVE_LINEAR){
        if(m_bSmooth) res = len;
        m_resolution = res;
        m_nextResPos = len;
    }else{
        int next;
        if(m_nextResPos >= len / 2){
            res *= 5;
            next = len;
        }else if(m_nextResPos >= len / 3){
            res *= 2;
            next = len / 2;
        }else if(m_nextResPos >= len / 4){
            next = len / 3;
        }else if(m_nextResPos >= len / 10){
            res /= 4;
            next = len / 4;
        }else if(m_nextResPos >= len / 20){
            res /= 6;
            next = len / 10;
        }else{
            res /= 9;
            next = len / 20;
        }
        m_resolution = res;
        m_nextResPos = next;
        if(m_bSmooth){
            m_resolution = res = next / 2;
        }
    }
    m_resolution = MAX(res, 1);
}

void CEnvelopedObject::envelopeReset()
{
    m_envStatus = ENV_OFF;
    m_curLevel = 0;
    m_curPos = 0;
}

void CEnvelopedObject::releaseEnd()
{
}

void CEnvelopedObject::setLfo(int no, CLFO* pLfo)
{
    m_pLfo[no] = pLfo;
}

void CEnvelopedObject::enableLfo(int no, int bEnable)
{
    m_bLfoEnable[no] = bEnable;
}

// Note: the two early returns are separate ifs on purpose; merging them changes the
// generated code (significant for reproducing the released binary).
int CEnvelopedObject::processSignal(int size, float* pBuf, int* pProcessed)
{
    if(!m_bNoInput && !m_pInput){
        *pProcessed = 1;
        return size;
    }
    if(m_envStatus == ENV_OFF && !m_bProcessIdle){
        *pProcessed = 1;
        return size;
    }
    int n = size;
    if(!m_bNoInput){
        n = m_pInput->processSignal(size, pBuf, pProcessed);
        if(*pProcessed) return n;
    }
    *pProcessed = 1;

    // split the buffer where the LFO values change
    int remain = n;
    float* p1 = pBuf;
    int pos1 = 0;
    int pos2 = 0;
    while(remain){
        int lfo1, len1;
        if(m_bLfoEnable[0]){
            lfo1 = m_pLfo[0]->getSameValAndTime(&len1, pos1);
            if(len1 == -1) len1 = remain;
            else len1 = MIN(len1, remain);
        }else{
            len1 = remain;
            lfo1 = 0;
        }
        int remain2 = len1;
        float* p2 = p1;
        while(remain2){
            int lfo2, len2;
            if(m_bLfoEnable[1]){
                lfo2 = m_pLfo[1]->getSameValAndTime(&len2, pos2);
                if(len2 == -1) len2 = remain2;
                else len2 = MIN(len2, remain2);
            }else{
                len2 = remain2;
                lfo2 = 0;
            }
            int processed = 0;
            processSignalSub(len2, p2, lfo1, lfo2, &processed);
            if(*pProcessed && p2 != pBuf && !processed){
                memset(pBuf, 0, (p2 - pBuf) * sizeof(float));
            }else if(processed && !*pProcessed){
                memset(p2, 0, len2 * sizeof(float));
            }
            *pProcessed &= processed;
            p2 += len2;
            pos2 += len2;
            remain2 -= len2;
        }
        p1 += len1;
        pos1 += len1;
        remain -= len1;
    }
    if(m_bPostProcess){
        postProcessSignal(pBuf, size, pProcessed);
    }
    return size;
}

// Note: the declaration order, the loop flags (bAttack/bRemain), the operand order of the
// MIN/MAX calls and the order of the tests below are significant for reproducing the
// released binary; keep them as they are.
int CEnvelopedObject::processSignalSub(int size, float* pBuf, int lfo1, int lfo2, int* pProcessed)
{
    *pProcessed = 1;
    if(!m_bEnvEnable){
        *pProcessed = processEnvelopedSignal(0, pBuf, size, lfo1, lfo2);
        return size;
    }

    int* pLen;
    int* pType;
    BOOL bAttack;
    int remain = size;
    int start, range, bottom;
    float* p = pBuf;
    switch(m_envStatus){
    case ENV_RELEASE:
        pLen = &m_releaseLen;
        start = m_releaseLevel;
        range = start;
        pType = &m_releaseType;
        bottom = 0;
        goto decay_release;
    case ENV_ATTACK:
        {
            int step;
            if(m_bSmooth){
                step = m_attackLen;
            }else{
                step = MAX(m_attackLen / MAX(abs(m_peakLevel), 1), 1);
            }
            bAttack = m_attackLen > m_curPos;
            while(remain > 0 && bAttack){
                int n = MIN(m_attackLen - m_curPos, MIN(remain, step));
                long level = (long)m_peakLevel * (m_curPos + n) / m_attackLen;
                m_curLevel = level > m_peakLevel ? m_peakLevel : (int)level;
                int ret = processEnvelopedSignal(m_bInvert ? -m_curLevel : m_curLevel, p, n, lfo1, lfo2);
                if(*pProcessed && p != pBuf && !ret){
                    memset(pBuf, 0, (p - pBuf) * sizeof(float));
                }else if(ret && !*pProcessed){
                    memset(p, 0, n * sizeof(float));
                }
                *pProcessed &= ret;
                p += n;
                m_curPos += n;
                remain -= n;
                bAttack = m_attackLen > m_curPos;
            }
            if(bAttack && m_curLevel < m_peakLevel) return size;
            m_envStatus = ENV_DECAY;
            m_curLevel = m_peakLevel;
            m_curPos = 0;
            m_nextResPos = 0;
            calcDRResolution();
        }
        // fall through
    case ENV_DECAY:
        pLen = &m_decayLen;
        start = m_peakLevel;
        bottom = m_sustainLevel;
        range = start - bottom;
        pType = &m_decayType;
    decay_release:
        {
            int type = *pType;
            int len = *pLen;
            float flen = (float)len;
            float rlen = 1.0f / flen;
            float frange = (float)range;
            float rate = frange / flen;
            BOOL bRemain = m_curPos < len;
            while(remain > 0 && bRemain){
                if(m_curLevel <= bottom) break;
                int n = MIN(len - m_curPos, MIN(remain, m_resolution));
                if(type == ENV_CURVE_LINEAR){
                    long d = (long)((m_curPos + n) * rate);
                    m_curLevel = (int)MAX((long)bottom, start - d);
                }else{
                    float x = (len - m_curPos - n) * rlen;
                    float y = exp((x - 1.0f) * 8.0f) * x;
                    m_curLevel = ftoi(y * frange) + bottom;
                }
                int ret = processEnvelopedSignal(m_bInvert ? -m_curLevel : m_curLevel, p, n, lfo1, lfo2);
                if(*pProcessed && p != pBuf && !ret){
                    memset(pBuf, 0, (p - pBuf) * sizeof(float));
                }else if(ret && !*pProcessed){
                    memset(p, 0, n * sizeof(float));
                }
                *pProcessed &= ret;
                p += n;
                remain -= n;
                m_curPos += n;
                if(m_curPos >= m_nextResPos) calcDRResolution();
                bRemain = m_curPos < len;
            }
            if(bRemain && m_curLevel > bottom){
                // a smoothed exponential release may end a little early
                if(!m_bSmooth || m_envStatus != ENV_RELEASE || type != ENV_CURVE_EXP
                    || m_curPos < ftoi(flen * 0.9f)) return size;
            }
            if(m_envStatus == ENV_DECAY){
                m_envStatus = ENV_SUSTAIN;
                m_curLevel = m_sustainLevel;
                m_curPos = 0;
            }else{
                m_envStatus = ENV_OFF;
                m_curLevel = 0;
                releaseEnd();
            }
        }
        break;
    }

    if(remain > 0){
        int level = 0;
        if(m_envStatus == ENV_SUSTAIN){
            level = m_bInvert ? -m_sustainLevel : m_sustainLevel;
        }
        int ret = processEnvelopedSignal(level, p, remain, lfo1, lfo2);
        if(*pProcessed && p != pBuf && !ret){
            memset(pBuf, 0, (p - pBuf) * sizeof(float));
        }else if(ret && !*pProcessed){
            memset(p, 0, remain * sizeof(float));
        }
        *pProcessed &= ret;
    }
    return size;
}
