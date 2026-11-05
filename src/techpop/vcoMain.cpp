/**
 * @file vcoMain.cpp
 * @brief Inner loops of the oscillator.
 *
 * calcSignalLoop() is written once with all
 * the switches as arguments; the calcXXXXXX() variants call it with constant
 * arguments so that every combination gets its own branch-free loop.
 * The variants are declared in vco.h and called from CVco::createWaveMain().
 */
#include <emmintrin.h>
#include "common.h"
#include "vco.h"

#define CACHE_ALIGN32   __attribute__((aligned(32)))

#define TABLE_FRAC  (1.f / 65536.f)

// linear interpolated table lookup, 16.16 phase
#define INTERP(t, ph) \
    (t[((ph) >> 16) & VCO_TABLE_MASK] + \
     (t[(((ph) >> 16) + 1) & VCO_TABLE_MASK] - t[((ph) >> 16) & VCO_TABLE_MASK]) * (float)((ph) & 0xffff) * TABLE_FRAC)

// o = t[i] on entry: linear interpolation towards t[i + 1]
#define INTERP_NEXT(o, t, ph) \
    o += (t[(((ph) >> 16) + 1) & VCO_TABLE_MASK] - o) * (float)((ph) & 0xffff) * TABLE_FRAC

// difference of two saws = pulse
#define INTERP_PULSE(t, ph, pw) \
    (INTERP(t, ph) - (t[(((ph) >> 16) + (pw)) & VCO_TABLE_MASK] + \
     (t[(((ph) >> 16) + ((pw) + 1)) & VCO_TABLE_MASK] - t[(((ph) >> 16) + (pw)) & VCO_TABLE_MASK]) * (float)((ph) & 0xffff) * TABLE_FRAC))

// one oscillator -> o * gain
//   scales: detuned pulse, pulse, detuned saw, noise; pNoise: M-sequence state (OSC2 only)
#define CALC_OSC(o, wave, table, phase, detune, pNoise, scalePD, scaleP, scaleSD, scaleN, gain) \
    if (detune) { \
        idx.m[0] = _mm_and_si128(_mm_srli_epi32(*(__m128i*)&p.ssPhase[0], 16), mask); \
        idx.m[1] = _mm_and_si128(_mm_srli_epi32(*(__m128i*)&p.ssPhase[4], 16), mask); \
    } \
    switch (wave) { \
    case 1: \
        if (detune) { \
            __m128i pw = _mm_set1_epi32(p.pwOffset); \
            idx2.m[0] = _mm_and_si128(_mm_add_epi32(_mm_srli_epi32(*(__m128i*)&p.ssPhase[0], 16), pw), mask); \
            idx2.m[1] = _mm_and_si128(_mm_add_epi32(_mm_srli_epi32(*(__m128i*)&p.ssPhase[4], 16), pw), mask); \
            o = (table[(phase >> 16) & VCO_TABLE_MASK] - table[((phase >> 16) + p.pwOffset) & VCO_TABLE_MASK] \
                + table[idx.u[0]] - table[idx2.u[0]] + table[idx.u[1]] - table[idx2.u[1]] \
                + table[idx.u[2]] - table[idx2.u[2]] + table[idx.u[3]] - table[idx2.u[3]] \
                + table[idx.u[4]] - table[idx2.u[4]] + table[idx.u[5]] - table[idx2.u[5]] \
                + table[idx.u[6]] - table[idx2.u[6]] + table[idx.u[7]] - table[idx2.u[7]]) * scalePD * gain; \
        } else { \
            o = INTERP_PULSE(table, phase, p.pwOffset) * scaleP * gain; \
        } \
        break; \
    case 2: \
        o = noise(pNoise, scaleN * gain); \
        break; \
    default: \
        o = table[(phase >> 16) & VCO_TABLE_MASK]; \
        if (!detune) { \
            INTERP_NEXT(o, table, phase); \
        } else { \
            o = o + table[idx.u[0]] + table[idx.u[1]] + table[idx.u[2]] + table[idx.u[3]] \
                + table[idx.u[4]] + table[idx.u[5]] + table[idx.u[6]] + table[idx.u[7]]; \
        } \
        if (detune) o *= scaleSD; \
        o *= gain; \
        break; \
    }

#define CALC_OSC1(o, scalePD, scaleP, scaleSD) \
    CALC_OSC(o, wave1, p.pTable1, p.phase1, bDetune, pNoise1, scalePD, scaleP, scaleSD, 1.f, 1.f)
#define CALC_OSC2(o, scaleP, scaleN, gain) \
    CALC_OSC(o, wave2, p.pTable2, p.phase2, 0, p.pNoise, 1.f, scaleP, 1.f, scaleN, gain)

/**
 * @brief M-sequence noise: advances the shift register and returns +o or -o.
 *
 * @param[in,out] pNoise    Shift register state.
 * @param o                 Output magnitude.
 * @return o if bit 19 of the state was set before the step, otherwise -o.
 */
static inline float noise(unsigned int* pNoise, float o)
{
    if (*pNoise & 0x80000) {
        *pNoise = (*pNoise << 1) ^ 0x87;
    } else {
        *pNoise = *pNoise << 1;
        o = -o;
    }
    return o;
}

/**
 * @brief Eight super saw table indices, computed four at a time with SSE.
 */
union SSIndex {
    __m128i m[2];
    unsigned int u[8];
};

/**
 * @brief Renders one block of OSC1 + OSC2 + sub oscillator samples.
 *
 * Called with constant switches so the compiler generates one specialized loop per
 * combination. Per sample: computes OSC1 and OSC2 (or the ring/FM combination), adds the sub
 * oscillator, advances the phases (with FM, multi detune and sync), and steps the FM amount
 * smoothing every 32 samples. At the end, the phases and FM state are written back through
 * the pointers in @p prm.
 *
 * @param bSync     Nonzero: restart OSC2 (or reset the noise) when OSC1 wraps.
 * @param bFM       Nonzero: OSC2 frequency-modulates OSC1 (prm.fmAmount * OSC2 sample).
 * @param bRing     Nonzero: OSC2 is multiplied by OSC1.
 * @param bDetune   Nonzero: OSC1 adds the 8 super saw voices.
 * @param wave1     OSC1 loop type: 0 simple table, 1 pulse.
 * @param wave2     OSC2 loop type: 0 simple table, 1 pulse, 2 noise.
 * @param prm       Block parameters (copied; prm.pBuf receives prm.size samples, overwritten).
 * @note The OSC1 noise path dereferences a NULL state pointer: OSC1 has no noise, and Synth1 1.13
 *       has the same undefined path (wave1 == 2 traps).
 */
inline void calcSignalLoop(int bSync, int bFM, int bRing, int bDetune, int wave1, int wave2, tagCalcSigParams prm)
{
    CACHE_ALIGN32 tagCalcSigParams p = prm;
    const __m128i mask = _mm_set1_epi32(VCO_TABLE_MASK);
    __m128i ssDPhase0 = *(__m128i*)&p.ssDPhase[0];
    __m128i ssDPhase1 = *(__m128i*)&p.ssDPhase[4];
    int subMode = 0;
    if (p.subGain != 0.f) {
        subMode = p.subOct + 1;
    }
    SSIndex idx, idx2;
    unsigned int* pNoise1 = NULL;   // OSC1 has no noise
    int bFMSmooth = 0;
    if (bFM) {
        bFMSmooth = (p.fmTarget != p.fmAmount);
    }
    for (long i = 0; i < p.size; i++) {
        float out, o1, o2, mod; // mod: ring modulator (OSC1) / FM modulator (raw OSC2)
        if (bRing) {
            CALC_OSC1(mod, 0.15f, 0.5f, 0.3f);
            out = p.gain1 * mod;
            CALC_OSC2(o2, 1.f, 1.f, p.gain2);
            o2 *= mod;
        } else if (bFM) {
            CALC_OSC1(o1, 1.f, 1.f, 1.f);
            out = o1 * p.gain1;
            CALC_OSC2(mod, 0.5f, 0.65f, 1.f);
            o2 = mod * p.gain2;
        } else {
            CALC_OSC1(o1, 1.f, 1.f, 1.f);
            out = o1 * p.gain1;
            CALC_OSC2(o2, 1.f, 1.f, p.gain2);
        }
        p.pBuf[i] = out + o2;

        // sub oscillator
        if (subMode == 1) {
            float s = p.pSubTable[(p.phase1 >> 16) & VCO_TABLE_MASK];
            if (!bDetune) {
                INTERP_NEXT(s, p.pSubTable, p.phase1);
            } else {
                s = s + p.pSubTable[idx.u[0]] + p.pSubTable[idx.u[1]] + p.pSubTable[idx.u[2]] + p.pSubTable[idx.u[3]]
                    + p.pSubTable[idx.u[4]] + p.pSubTable[idx.u[5]] + p.pSubTable[idx.u[6]] + p.pSubTable[idx.u[7]];
            }
            p.pBuf[i] += s * p.subGain;
        } else if (subMode == 2) {
            float s;
            if (bDetune) {
                idx.m[0] = _mm_and_si128(_mm_srli_epi32(*(__m128i*)&p.ssPhase[0], 17), mask);
                idx.m[1] = _mm_and_si128(_mm_srli_epi32(*(__m128i*)&p.ssPhase[4], 17), mask);
                s = p.pSubTable[(p.phase1 >> 17) & VCO_TABLE_MASK]
                    + p.pSubTable[idx.u[0]] + p.pSubTable[idx.u[1]] + p.pSubTable[idx.u[2]] + p.pSubTable[idx.u[3]]
                    + p.pSubTable[idx.u[4]] + p.pSubTable[idx.u[5]] + p.pSubTable[idx.u[6]] + p.pSubTable[idx.u[7]];
            } else {
                s = p.pSubTable[(p.phase1 >> 17) & VCO_TABLE_MASK];
                s += (p.pSubTable[((p.phase1 >> 17) + 1) & VCO_TABLE_MASK] - s) * (float)((p.phase1 >> 1) & 0xffff) * TABLE_FRAC;
            }
            p.pBuf[i] += s * p.subGain;
        }

        // advance the phases
        unsigned int prevPhase = p.phase1;
        if (bFM) {
            int d = ftoi((float)p.fmAmount * mod);
            p.phase1 = p.phase1 + p.dPhase1 + d;
            if (bDetune) {
                __m128i dv = _mm_set1_epi32(d);
                *(__m128i*)&p.ssPhase[0] = _mm_add_epi32(*(__m128i*)&p.ssPhase[0], _mm_add_epi32(dv, ssDPhase0));
                *(__m128i*)&p.ssPhase[4] = _mm_add_epi32(*(__m128i*)&p.ssPhase[4], _mm_add_epi32(dv, ssDPhase1));
            }
        } else {
            p.phase1 += p.dPhase1;
            if (bDetune) {
                *(__m128i*)&p.ssPhase[0] = _mm_add_epi32(*(__m128i*)&p.ssPhase[0], ssDPhase0);
                *(__m128i*)&p.ssPhase[4] = _mm_add_epi32(*(__m128i*)&p.ssPhase[4], ssDPhase1);
            }
        }
        if ((p.phase1 & VCO_PHASE_MASK) < (prevPhase & VCO_PHASE_MASK) && bSync) {
            // OSC1 wrapped around: restart OSC2
            if (wave2 != 2) {
                p.phase2 = (prevPhase & VCO_PHASE_MASK) * p.dPhase2 / p.dPhase1;
            } else {
                *p.pNoise = 10;
            }
        } else if (wave2 != 2) {
            p.phase2 += p.dPhase2;
        }

        // smooth FM amount changes
        if (bFM && bFMSmooth) {
            if ((p.fmSmoothCount & 0x1f) == 0) {
                p.fmAmount = (int)((float)p.fmDelta * g_smoothCosTable[(p.fmSmoothCount << 8) / p.fmSmoothLen]) + p.fmStart;
            }
            p.fmSmoothCount--;
        }
    }
    *p.pPhase1 = p.phase1;
    *p.pPhase2 = p.phase2;
    if (bDetune) {
        for (int k = 0; k < 8; k++) {
            p.pSSPhase[k] = p.ssPhase[k];
        }
    }
    if (bFM) {
        if (bFMSmooth) {
            *p.pFmSmoothCount = p.fmSmoothCount;
            if (p.fmSmoothCount == 0) {
                p.fmAmount = p.fmTarget;
            }
        }
        *p.pFmAmount = p.fmAmount;
    }
}

// Defines one render variant: name(p) runs calcSignalLoop() with constant switches.
#define DEFINE_CALC_FUNC(name, s, f, r, d, w1, w2) \
    void name(tagCalcSigParams* p) { calcSignalLoop(s, f, r, d, w1, w2, *p); }

// Defines the six OSC1/OSC2 wave variants (PP, PN, PS, SP, SN, SS) of one switch combination.
#define DEFINE_CALC_FUNCS(S, F, R, D, s, f, r, d) \
    DEFINE_CALC_FUNC(calc##S##F##R##D##PP, s, f, r, d, 1, 1) \
    DEFINE_CALC_FUNC(calc##S##F##R##D##PN, s, f, r, d, 1, 2) \
    DEFINE_CALC_FUNC(calc##S##F##R##D##PS, s, f, r, d, 1, 0) \
    DEFINE_CALC_FUNC(calc##S##F##R##D##SP, s, f, r, d, 0, 1) \
    DEFINE_CALC_FUNC(calc##S##F##R##D##SN, s, f, r, d, 0, 2) \
    DEFINE_CALC_FUNC(calc##S##F##R##D##SS, s, f, r, d, 0, 0)

DEFINE_CALC_FUNCS(_,_,_,_, 0, 0, 0, 0)
DEFINE_CALC_FUNCS(_,_,_,D, 0, 0, 0, 1)
DEFINE_CALC_FUNCS(_,F,_,_, 0, 1, 0, 0)
DEFINE_CALC_FUNCS(_,F,_,D, 0, 1, 0, 1)
DEFINE_CALC_FUNCS(_,_,R,_, 0, 0, 1, 0)
DEFINE_CALC_FUNCS(_,_,R,D, 0, 0, 1, 1)
DEFINE_CALC_FUNCS(S,_,_,_, 1, 0, 0, 0)
DEFINE_CALC_FUNCS(S,_,_,D, 1, 0, 0, 1)
DEFINE_CALC_FUNCS(S,F,_,_, 1, 1, 0, 0)
DEFINE_CALC_FUNCS(S,F,_,D, 1, 1, 0, 1)
DEFINE_CALC_FUNCS(S,_,R,_, 1, 0, 1, 0)
DEFINE_CALC_FUNCS(S,_,R,D, 1, 0, 1, 1)
