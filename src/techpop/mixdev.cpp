/**
 * @file mixdev.cpp
 * @brief Mixer device (CMixDevice): mono sum of the inputs and panned stereo mix of the voices.
 */
#include <string.h>
#if defined(__aarch64__)
#include "../compat/sse_neon.h"
#else
#include <xmmintrin.h>
#endif
#include "common.h"
#include "mixdev.h"

CMixDevice::CMixDevice()
{
    m_pWork = (float*)_aligned_malloc(MIXDEV_WORK_SIZE * sizeof(float), 16);
    memset(m_pInputs, 0, sizeof(m_pInputs));
    m_channelNum = 0;
    m_enableVoiceNum = 0;
}

CMixDevice::~CMixDevice()
{
    _aligned_free(m_pWork);
}

void CMixDevice::setChannelNum(int num)
{
    m_channelNum = num;
}

void CMixDevice::inputMixDeviceIs(int ch, CSignalDevice* pDev)
{
    m_pInputs[ch] = pDev;
}

void CMixDevice::setEnableVoiceNum(int num)
{
    m_enableVoiceNum = num;
}

// mono inputs -> interleaved stereo output, panned by each input's getPanVal()
int CMixDevice::processSignalW(int size, float* pBuf, int* pProcessed)
{
    int bFirst = 1;
    *pProcessed = 1;
    for(int i = 0; i < m_enableVoiceNum; i++){
        int processed;
        m_pInputs[i]->processSignal(size, m_pWork, &processed);
        *pProcessed = *pProcessed && processed;
        if(processed) continue;
        if(bFirst){
            for(int j = 0; j < size * 2; j++) pBuf[j] = 0;
        }
        float* pWork = m_pWork;
        float pan = m_pInputs[i]->getPanVal();
        if(pan == 0.0f){
            bFirst = 0;
            for(int j = 0; j < size; j++){
                float v = pWork[j];
                pBuf[j * 2 + 1] += v;
                pBuf[j * 2] += v;
            }
        }else{
            float lv = 1.0f;
            float rv = 1.0f;
            if(pan > 0.0f){
                lv = MAX(1.0f - pan, 0.000001f);
            }else{
                rv = MAX(1.0f + pan, 0.000001f);
            }
            bFirst = 0;
            for(int j = 0; j < size; j++){
                float v = pWork[j];
                pBuf[j * 2 + 1] += v * rv;
                pBuf[j * 2] += v * lv;
            }
        }
    }
    return size;
}

// processed/n live at function scope after the null-input check (their lifetime.end keeps the
// final `*pProcessed = 1` block apart from the early-return one).
int CMixDevice::processSignal(int size, float* pBuf, int* pProcessed)
{
    int bufSize = (size > 256) ? 256 : size;
    int first = -1;
    for(int i = 0; i < m_channelNum; i++){
        if(m_pInputs[i]){
            first = i;
            break;
        }
    }
    if(first == -1){
        *pProcessed = 1;
        return size;
    }
    *pProcessed = 0;
    int bAdded = 0;
    int processed, n;
    for(int i = 0; i < m_channelNum; i++){
        if(m_pInputs[i] == NULL) continue;
        if(bAdded == 0){
            // the first active input writes directly to the output
            int rest = bufSize;
            float* p = pBuf;
            while(rest){
                n = m_pInputs[i]->processSignal(rest, p, &processed);
                if(processed && n != bufSize){
                    for(int j = 0; j < n; j++) p[j] = 0;
                }
                rest -= n;
                p += n;
            }
            bAdded = !processed || n != bufSize;
        }else{
            // the others are processed into the work buffer and added
            float* pWork = m_pWork;
            if(m_bSSE) pWork = (float*)((long)m_pWork + ((long)pBuf & 15));
            int rest = bufSize;
            float* p = pBuf;
            while(rest){
                n = m_pInputs[i]->processSignal(rest, pWork, &processed);
                if(!processed){
                    int j = 0;
                    if(m_bSSE){
                        for(j = 0; j < n; j++){
                            if(((long)p & 15) == 0) break;
                            p[j] = p[j] + pWork[j];
                        }
                        for(; j < n - 3; j += 4){
                            _mm_store_ps(&p[j], _mm_add_ps(_mm_load_ps(&p[j]), _mm_load_ps(&pWork[j])));
                        }
                    }
                    for(; j < n; j++) p[j] = p[j] + pWork[j];
                }
                p += n;
                rest -= n;
            }
        }
    }
    if(bAdded == 0) *pProcessed = 1;
    return bufSize;
}
