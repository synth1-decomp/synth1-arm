/**
 * @file lfo.cpp
 * @brief CLFO: low frequency oscillator (saw, triangle, square, sample & hold, smooth random, sine).
 */
#include <stdlib.h>
#include <math.h>
#include "lfo.h"
#include "synth1.h"

float* getWaveTablePointerSize(int type, float freq);

// lfo.cpp does not include common.h (Synth1 1.13's LFO code has none of common.h's inline
// functions), so it carries its own copy of fpow(), identical to common.h's.
#ifndef FPOW_DEFINED
#define FPOW_DEFINED

/**
 * @brief pow() via exp(log(a) * b); falls back to pow() if a is 0 or the result is not a number.
 *
 * @param a  Base.
 * @param b  Exponent.
 * @return a raised to the power b.
 */
inline double fpow(double a, double b)
{
    if (a == 0) return pow(a, b);
    double r = exp(log(a) * b);
    if (isnan(r)) r = pow(a, b);
    return r;
}
#endif

// tempo sync cycle lengths (in 1/24 beats, i.e. 1/96 bars), 16 bars .. 1/32T
static const int g_nLFOTime[] = {
    1536, 1152, 864, 768, 576, 432, 384, 288, 216, 192,
    144, 96, 84, 72, 48, 42, 36, 32, 24, 21,
    18, 16, 12, 9, 8, 6, 4, 3, 2, 1
};
#define LFOTIME_NUM (sizeof(g_nLFOTime) / sizeof(int))

CLFO::CLFO()
{
    m_bEnable = 0;
    m_type = LFO_SAW;
    m_bTempoSync = 0;
    m_bKeySync = 0;
    m_speedDial = 0;
    m_amountDial = 0;
    m_freqMain = 1.0f;
    m_freq = 1.0f;
    m_amountLo = 0;
    m_amountHi = 0;
    m_curTime = 0;
    m_cycleTime = 44100;
    m_resetCount = 0;
    m_reserved = 0;
    m_samplingFreq = 44100;
}

void CLFO::setSynth1(CSynthesizer1* pSynth)
{
    m_pSynth = pSynth;
}

void CLFO::setSamplingFreq(int fs)
{
    m_samplingFreq = fs;
    m_minStep = 64;
    setFreq(m_freq);
    setFreqMain(m_freqMain);
}

void CLFO::setEnable(int bEnable)
{
    m_bEnable = bEnable;
}

void CLFO::setTempoSync(int bSync)
{
    m_bTempoSync = bSync;
    recalcSpeed();
}

void CLFO::recalcSpeed()
{
    float freq;
    if(m_bTempoSync){
        int idx = m_speedDial * LFOTIME_NUM / 128;
        freq = m_pSynth->getBaseTempo() * 48.0f / g_nLFOTime[idx] / 120.0f;
    }else{
        freq = (float)(fpow(1500.0, (float)m_speedDial / 127.0) * (1.0 / 12.0));
    }
    setFreq(freq);
}

void CLFO::setKeySync(int bSync)
{
    m_bKeySync = bSync;
}

void CLFO::setDestination(int dest)
{
    m_destination = dest;
    recalcAmount();
}

void CLFO::recalcAmount()
{
    double amount;
    if(m_destination == LFO_DEST_OSC2 || m_destination == LFO_DEST_OSC12){
        amount = (fpow(10.0, (float)m_amountDial / 127.0) - 1.0) / 9.0;
    }else{
        amount = m_amountDial / 127.0;
    }
    float f = (float)amount;
    int lo = 0, hi = 0;
    switch(m_destination){
    case LFO_DEST_OSC2:
    case LFO_DEST_OSC12:
        lo = (int)(f * -5.0f * 128.0f);
        hi = (int)(f * 5.0f * 128.0f);
        break;
    case LFO_DEST_PW:
        lo = (int)(f * -128.0f);
        hi = (int)(f * 128.0f);
        break;
    case LFO_DEST_FILTER:
        hi = (int)(f * 1024.0f);
        break;
    case LFO_DEST_AMP:
        lo = (int)(f * 2048.0f);
        break;
    case LFO_DEST_FM:
        hi = (int)(f * 512.0f);
        break;
    case LFO_DEST_PAN:
        lo = (int)(f * -64.0f);
        hi = (int)(f * 64.0f);
        break;
    }
    m_amountLo = lo;
    m_amountHi = hi;
}

void CLFO::setAmountDial(int val)
{
    m_amountDial = val;
    recalcAmount();
}

void CLFO::setAmount(int lo, int hi)
{
    m_amountLo = lo;
    m_amountHi = hi;
}

void CLFO::setSpeedDial(int val)
{
    m_speedDial = val;
    recalcSpeed();
}

void CLFO::setType(int type)
{
    m_type = type;
    if(m_type == LFO_SAMPLEHOLD || m_type == LFO_SMOOTHRANDOM){
        makeRandomLevel((m_amountHi + m_amountLo) / 2);
    }
}

void CLFO::makeRandomLevel(int prevLevel)
{
    m_prevRandLevel = prevLevel;
    int range = m_amountHi - m_amountLo;
    if(range == 0){
        m_randLevel = m_amountHi;
        return;
    }
    if(range > 0){
        m_randLevel = rand() % (m_amountHi - m_amountLo) + m_amountLo;
    }else{
        m_randLevel = rand() % (m_amountLo - m_amountHi) + m_amountHi;
    }
}

void CLFO::setFreq(float freq)
{
    m_freq = freq;
    processSmoothFreq(1);
    m_minStep = (freq > 10.0f) ? 32 : 64;
}

void CLFO::setFreqMain(float freq)
{
    m_freqMain = freq;
    int oldCycle = m_cycleTime;
    m_cycleTime = (int)(m_samplingFreq / freq);
    m_curTime = (int)((float)m_curTime * m_cycleTime / oldCycle);
}

// approach the target frequency by 1Hz/ms
void CLFO::processSmoothFreq(int size)
{
    if(m_freqMain == m_freq) return;
    float step = (float)size / (m_samplingFreq * 0.001f);
    float freq = m_freqMain;
    if(m_freq > freq){
        freq += step;
        if(freq > m_freq) freq = m_freq;
    }else{
        freq -= step;
        if(freq < m_freq) freq = m_freq;
    }
    setFreqMain(freq);
}

void CLFO::reset()
{
    m_curTime = 0;
    m_resetCount = 0;
    if(m_type == LFO_SAMPLEHOLD || m_type == LFO_SMOOTHRANDOM){
        makeRandomLevel(m_randLevel);
    }
}

int CLFO::getSameValAndTime(int* pTime, int offset)
{
    int time = m_curTime + offset;
    if(time >= m_cycleTime) time %= m_cycleTime;
    return getSameValAndTimeMain(pTime, time);
}

// Returns the LFO value at 'time' and stores in *pTime how many samples
// the value stays the same.
int CLFO::getSameValAndTimeMain(int* pTime, int time)
{
    if(m_amountHi == m_amountLo){
        *pTime = -1;
        return m_amountHi;
    }
    int bInvert = m_amountHi < m_amountLo;
    int max, min;
    if(bInvert){
        max = m_amountLo;
        min = m_amountHi;
    }else{
        max = m_amountHi;
        min = m_amountLo;
    }
    int cycle = m_cycleTime;
    int next, val, half, range;
    switch(m_type){
    default:    // LFO_SMOOTHRANDOM
        if(m_prevRandLevel == m_randLevel){
            val = m_prevRandLevel;
            next = cycle;
            break;
        }
        if(m_prevRandLevel > m_randLevel){
            min = m_randLevel;
            max = m_prevRandLevel;
        }else{
            bInvert = !bInvert;
            min = m_prevRandLevel;
            max = m_randLevel;
        }
        // fall through
    case LFO_SAW:
        range = max - min;
        if(cycle > range + 1){
            val = max - ((range + 1) * time) / cycle;
            // written with max - min (not range): Reassociate then keeps range as the add's LHS
            next = ((max + 1 - val) * cycle + max - min) / (range + 1);
        }else{
            val = max - (range * time) / (cycle - 1);
            next = time + 1;
        }
        break;
    case LFO_TRIANGLE:
        half = cycle / 2;
        if(time < half){
            range = max - min;
            int d = (range * time) / half;
            val = d + min;
            if(cycle > range){
                next = ((d + 1) * half + (max - 1 - min)) / range;
            }else{
                next = time + 1;
            }
        }else{
            int len = cycle - half;
            int t = time - half;
            range = max - min;
            val = max - (t * range) / len;
            if(len > range){
                next = ((max + 1 - val) * len + (max - 1 - min)) / range;
            }else{
                next = t + 1;
            }
            next += half;
        }
        break;
    case LFO_SQUARE:
        half = cycle / 2;
        if(time < half){
            next = half;
            val = max;
        }else{
            next = cycle;
            val = min;
        }
        break;
    case LFO_SAMPLEHOLD:
        val = m_randLevel;
        next = cycle;
        break;
    case LFO_SINE:
        {
            int ival;
            float fval;
            float* pTable = getWaveTablePointerSize(0, 0.0f);
            unsigned int idx = (unsigned int)(time << 11) / cycle;
            fval = (min + max) + (max - min) * pTable[idx];
#if defined(__aarch64__)
            ival = ftoi(fval);
#else
            __asm {
                fld fval
                fistp ival
            }
#endif
            val = ival / 2;
            next = time + (m_minStep - time % m_minStep);
        }
        break;
    }
    if(bInvert) val = min - val + max;
    *pTime = ((next < cycle) ? next : cycle) - time;
    if(*pTime < m_minStep) *pTime = m_minStep;
    return val;
}

void CLFO::incCurTime(int size)
{
    m_curTime += size;
    if(m_curTime >= m_cycleTime){
        m_curTime %= m_cycleTime;
        if(m_type == LFO_SAMPLEHOLD || m_type == LFO_SMOOTHRANDOM){
            makeRandomLevel(m_randLevel);
        }
    }
    processSmoothFreq(size);
}

int CLFO::getNextCycleRestTime()
{
    return m_cycleTime - m_curTime;
}
