/**
 * @file ssai.cpp
 * @brief CSeqSyncAudioInterface: renders the master output device, splitting the
 *        buffer at sequencer event boundaries.
 */
#include <math.h>
#include "common.h"
#include "sigdev.h"
#include "ssai.h"

#define MAX_RENDER  256

CSeqSyncAudioInterface::CSeqSyncAudioInterface()
{
    m_pMasterOut = NULL;
    m_pSequencer = NULL;
}

void CSeqSyncAudioInterface::setMasterOutDevice(CSignalDevice* pDev)
{
    m_pMasterOut = pDev;
}

void CSeqSyncAudioInterface::setSequencer(CSequencer* pSeq)
{
    m_pSequencer = pSeq;
}

int CSeqSyncAudioInterface::getSoundData(int nCh, float** ppBuf, int bMix, int size)
{
    DWORD tick = GetTickCount();
    if (m_pSequencer && m_pSequencer->isPlaying()) {
        int rest = size;
        while (rest) {
            int wait = m_pSequencer->getNextWaitTime();
            int n = MIN(rest, wait);
            outputMaster(m_pMasterOut, ppBuf, nCh, bMix, n);
            m_pSequencer->incCurTime(n);
            if (rest >= wait) {
                m_pSequencer->processNote();
            }
            rest -= n;
        }
    } else if (m_pMasterOut) {
        outputMaster(m_pMasterOut, ppBuf, nCh, bMix, size);
    }
    return size * sizeof(float);
}

void CSeqSyncAudioInterface::outputMaster(CSignalDevice* pDev, float** ppBuf, int nCh, int bMix, int size)
{
    float buf[MAX_RENDER * 2];
    float* pL = ppBuf[0];
    float* pR;
    if (nCh >= 2) {
        pR = ppBuf[1];
    }
    while (size) {
        int len = size;
        if (len > MAX_RENDER) len = MAX_RENDER;
        int bZero;
        int n = pDev->processSignalW(len, buf, &bZero);
        if (bZero) {
            if (nCh == 1) {
                if (bMix == 1) {
                    for (int i = 0; i < n; i++) {
                        *pL++ = 0.f;
                    }
                } else {
                    for (int i = 0; i < n; i++) {
                        *pL++ = 0.f;
                        *pL++ = 0.f;
                    }
                }
            } else {
                for (int i = 0; i < n; i++) {
                    *pL++ = 0.f;
                    *pR++ = 0.f;
                }
            }
        } else {
            if (nCh == 1) {
                if (bMix == 1) {
                    for (int i = 0; i < n * 2; i += 2) {
                        *pL++ = buf[i] + buf[i + 1];
                    }
                } else {
                    for (int i = 0; i < n * 2; i += 2) {
                        *pL++ = buf[i];
                        *pL++ = buf[i + 1];
                    }
                }
            } else {
                for (int i = 0; i < n * 2; i += 2) {
                    *pL++ = buf[i];
                    *pR++ = buf[i + 1];
                }
            }
        }
        size -= n;
    }
}

short keep_bits_from_16(short input, int keepBits)
{
    return input & (-1 << (16 - keepBits));
}

float foldback(float in, float threshold)
{
    if (in > threshold || in < -threshold) {
        in = fabs(fabs(fmod(in - threshold, threshold * 4)) - threshold * 2) - threshold;
    }
    return in;
}
