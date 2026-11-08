/**
 * @file vcf.cpp
 * @brief CVcf: envelope controlled filter (12dB SVF, 24dB ladder, diode ladder)
 *
 * Also contains the cutoff glide, keyboard tracking, the output peak limiter and distortion,
 * and the unused CBQF biquad.
 */

#include <math.h>
#include <string.h>
#if defined(__aarch64__)
#include "../compat/sse_neon.h"
#else
#include <xmmintrin.h>
#endif

#include "common.h"
#include "vcf.h"
#include "synth1.h"


#ifndef MIN
#define MIN(a,b)    ((a)<(b)?(a):(b))
#endif
#ifndef MAX
#define MAX(a,b)    ((a)>(b)?(a):(b))
#endif

#define VCF_PI      3.141592653589793
#define VCF_CLIP    20.0f
#define LIMIT_LEVEL 1.1f

// cutoff table: 15Hz .. 16kHz, exponential
void initVcf()
{
    static bool bFirst = false;
    if(bFirst) return;
    bFirst = true;
    for(int i = 0; i < VCF_FREQ_MAX; i++){
        s1g->m_vcfFreqTable[i] = (float)(fpow(16000.0f / 15.0f, i / 1024.0f) * 15.0);
    }
}

CVcf::CVcf()
{
    initVcf();
    setMax(VCF_FREQ_MAX);
    m_bProcessIdle = 1;
    m_bPostProcess = 1;
    m_kbdTrack = 0;
    m_kbdTrackOfs = 0;
    m_freq = 512;
    m_resonance = 0.4f;
    m_bVelocitySens = 1;
    m_velocity = 0;
    m_distortion = 0;
    m_gainScale = 1.0f;
    m_curFreq = 0;
    m_interpPos = 0;
    m_startFreq = 0;
    m_type = VCF_LPF24;
    m_amount = 0;
    _reserved118 = 0;
    m_y[0] = m_y[1] = m_y[2] = m_y[3] = 0;
    m_x[0] = m_x[1] = m_x[2] = m_x[3] = 0;
    m_limitGain = 1.0f;
    m_limitEnv = 0;
    onChangeSamplingFreq();
    m_unisonOfs = 0;
    m_note = 48;
    resetFilterBuf();
}

void CVcf::resetFilterBuf()
{
    envelopeReset();
    m_low = m_high = m_band = 0;
    m_y[0] = m_y[1] = m_y[2] = m_y[3] = 0;
    m_x[0] = m_x[1] = m_x[2] = m_x[3] = 0;
    _reserved118 = 0;
    m_lastFreq = -1;
    m_lastReso = 0;
    _reserved198 = 0;
    for(int i = 0; i < 4; i++){
        _reserved138[0][i] = 0;
        _reserved138[1][i] = 0;
        _reserved138[2][i] = 0;
    }
    for(int i = 0; i < 5; i++){
        m_z[i] = 0;
    }
    m_limitGain = 1.0f;
    m_limitEnv = 0;
    m_interpPos = m_samplingFreq / 20;
}

void CVcf::onChangeSamplingFreq()
{
    CEnvelopedObject::onChangeSamplingFreq();
    m_lastFreq = -1;
    m_lastReso = 0;
    int fs = m_samplingFreq;
    m_limitAttack = (float)fpow(0.5, 1.0 / (0.3 * fs));
    m_limitRelease = (float)exp(-1.0f / (fs * (5 * 0.001f)));   // 5ms
    m_coefSmooth = (float)pow(1.0 - 0.98, 1.0 / (fs * 0.001));
}

void CVcf::setResonance(float reso)
{
    m_resonance = reso;
}

void CVcf::setFreq(float freq)
{
    m_freq = (int)(freq * 1023.0f);
    if(m_envStatus != ENV_OFF){
        calcSetAmount();
    }
    m_startFreq = m_curFreq;
    m_interpPos = 0;
}

// envelope amount limited to the remaining cutoff range
void CVcf::calcSetAmount()
{
    float maxAmount = (VCF_FREQ_MAX - m_freq - m_kbdTrackOfs - m_unisonOfs) / 1024.0f;
    float minAmount = maxAmount - 1.0f;
    float amount = m_bVelocitySens ? m_amount * m_velocity / 127.0f : m_amount;
    amount = MIN(maxAmount, amount);
    // MATCH-HACK: the empty asm emits no code; it keeps this clamp a branch (as in the
    // released binary) instead of a min/max instruction. See docs/MATCH_HACKS.md.
    if(minAmount > amount) amount = minAmount;
    else __asm__("");
    CEnvelopedObject::setGain(amount);
}

void CVcf::setType(int type)
{
    m_type = type;
    m_lastFreq = -1;
}

void CVcf::setAmount(float amount)
{
    m_amount = amount;
    if(m_envStatus != ENV_OFF){
        calcSetAmount();
    }
    m_startFreq = m_curFreq;
    m_interpPos = 0;
}

void CVcf::setSustain(float level)
{
    CEnvelopedObject::setSustain(level);
    m_startFreq = m_curFreq;
    m_interpPos = 0;
}

void CVcf::changeLfoSetting(int no, int bEnable, int dest)
{
    enableLfo(no, bEnable && dest == 3);
}

void CVcf::setVelocitySensEnable(int bEnable)
{
    m_bVelocitySens = bEnable;
}

void CVcf::setDistortion(float amount)
{
    m_distortion = amount;
}

void CVcf::setGainScale(float scale)
{
    m_gainScale = scale;
}

int CVcf::processEnvelopedSignal(int level, float* pBuf, int size, int lfo1, int lfo2)
{
    int freq = level + m_freq + m_kbdTrackOfs + m_unisonOfs;
    int lfo = lfo1 + lfo2;
    freq += (lfo > 0 ? VCF_FREQ_MAX - freq : freq) * lfo / 1024;
    int target = freq > 0 ? (freq < VCF_FREQ_MAX ? freq : VCF_FREQ_MAX - 1) : 0;

    int interpLen = m_samplingFreq / 20;
    while(size){
        int n;
        if(target != m_curFreq && m_interpPos < interpLen){
            // glide to the new cutoff
            int diff = target - m_startFreq;
            m_curFreq = diff * m_interpPos / interpLen + m_startFreq;
            int step = interpLen / MAX(abs(diff), 1);
            step = MAX(step, 1);
            n = MIN(size, step);
        }else{
            m_curFreq = target;
            n = size;
        }
        switch(m_type){
        case VCF_LPF12:
        case VCF_HPF12:
        case VCF_BPF12:
            lowpass2(pBuf, m_curFreq, m_resonance, pBuf, n);
            break;
        case VCF_LPF24:
            lowpass3(pBuf, m_curFreq, m_resonance, pBuf, n);
            break;
        case VCF_LPFDIODE:
            lowpassDiodeLadder(pBuf, m_curFreq, m_resonance, pBuf, n);
            break;
        }
        m_interpPos += n;
        size -= n;
        pBuf += n;
    }
    return 0;
}

// 12dB state variable filter, 2x oversampled (LPF/HPF/BPF)
void CVcf::lowpass2(float* pIn, int freq, float reso, float* pOut, unsigned int size)
{
    float f, q, r;
    if(freq != m_lastFreq || reso != m_lastReso){
        int fs = m_samplingFreq * 2;    // oversampled
        f = (float)(2.0 * sin(s1g->m_vcfFreqTable[freq] * VCF_PI / fs));
        m_f = f;
        r = MIN(reso, 1.0f);
        m_reso2 = r;
        q = (float)(1.0 - r * 0.7);
        m_q = q;
        m_lastFreq = freq;
        m_lastReso = reso;
    }else{
        f = m_f;
        q = m_q;
        r = m_reso2;
    }
    r = r - 1.0f;
    BOOL bClip = reso > 0.9;

    switch(m_type){
    case VCF_LPF12:
        if(m_bSSE){
            __m128 vf = _mm_set_ss(f), vq = _mm_set_ss(q), vr = _mm_set_ss(r);
            __m128 low = _mm_set_ss(m_low), band = _mm_set_ss(m_band), high = _mm_set_ss(m_high);
            for(unsigned int i = 0; i < size; i++){
                __m128 in = _mm_mul_ss(_mm_load_ss(&pIn[i]), vq);
                // 2x oversampling
                low = _mm_add_ss(low, _mm_mul_ss(vf, band));
                high = _mm_add_ss(_mm_sub_ss(in, low), _mm_mul_ss(vr, band));
                band = _mm_add_ss(_mm_mul_ss(vf, high), band);
                low = _mm_add_ss(low, _mm_mul_ss(vf, band));
                high = _mm_add_ss(_mm_sub_ss(in, low), _mm_mul_ss(vr, band));
                band = _mm_add_ss(_mm_mul_ss(vf, high), band);
                _mm_store_ss(&pOut[i], low);
                if(bClip && fabsf(low[0]) > VCF_CLIP) pOut[i] = low[0] > 0 ? VCF_CLIP : -VCF_CLIP;
            }
            _mm_store_ss(&m_low, low);
            _mm_store_ss(&m_high, high);
            _mm_store_ss(&m_band, band);
        }else{
            // the declaration order is significant for reproducing the released binary
            float band, low, high;
            low = m_low;
            high = m_high;
            band = m_band;
            for(unsigned int i = 0; i < size; i++){
                // 2x oversampling
                low += f * band;
                float in = pIn[i] * q;
                high = in - low + r * band;
                band += f * high;
                low += f * band;
                high = in - low + r * band;
                band += f * high;
                if(bClip && fabsf(low) > VCF_CLIP) low = low > 0 ? VCF_CLIP : -VCF_CLIP;
                pOut[i] = low;
            }
            m_low = low;
            m_high = high;
            m_band = band;
        }
        break;
    case VCF_HPF12:
        if(m_bSSE){
            __m128 vf = _mm_set_ss(f), vq = _mm_set_ss(q), vr = _mm_set_ss(r);
            __m128 low = _mm_set_ss(m_low), band = _mm_set_ss(m_band), high = _mm_set_ss(m_high);
            for(unsigned int i = 0; i < size; i++){
                __m128 in = _mm_mul_ss(_mm_load_ss(&pIn[i]), vq);
                // 2x oversampling
                low = _mm_add_ss(low, _mm_mul_ss(vf, band));
                high = _mm_add_ss(_mm_sub_ss(in, low), _mm_mul_ss(vr, band));
                band = _mm_add_ss(_mm_mul_ss(vf, high), band);
                low = _mm_add_ss(low, _mm_mul_ss(vf, band));
                high = _mm_add_ss(_mm_sub_ss(in, low), _mm_mul_ss(vr, band));
                band = _mm_add_ss(_mm_mul_ss(vf, high), band);
                _mm_store_ss(&pOut[i], high);
                if(bClip && fabsf(high[0]) > VCF_CLIP) pOut[i] = high[0] > 0 ? VCF_CLIP : -VCF_CLIP;
            }
            _mm_store_ss(&m_low, low);
            _mm_store_ss(&m_high, high);
            _mm_store_ss(&m_band, band);
        }else{
            float low = m_low, high = m_high, band = m_band;
            for(unsigned int i = 0; i < size; i++){
                // 2x oversampling
                low += f * band;
                float in = pIn[i] * q;
                high = in - low + r * band;
                band += f * high;
                low += f * band;
                high = in - low + r * band;
                band += f * high;
                if(bClip && fabsf(high) > VCF_CLIP) high = high > 0 ? VCF_CLIP : -VCF_CLIP;
                pOut[i] = high;
            }
            m_low = low;
            m_high = high;
            m_band = band;
        }
        break;
    case VCF_BPF12:
        if(m_bSSE){
            __m128 vf = _mm_set_ss(f), vq = _mm_set_ss(q), vr = _mm_set_ss(r);
            __m128 low = _mm_set_ss(m_low), band = _mm_set_ss(m_band), high = _mm_set_ss(m_high);
            for(unsigned int i = 0; i < size; i++){
                __m128 in = _mm_mul_ss(_mm_load_ss(&pIn[i]), vq);
                // 2x oversampling
                low = _mm_add_ss(low, _mm_mul_ss(vf, band));
                high = _mm_add_ss(_mm_sub_ss(in, low), _mm_mul_ss(vr, band));
                band = _mm_add_ss(_mm_mul_ss(vf, high), band);
                low = _mm_add_ss(low, _mm_mul_ss(vf, band));
                high = _mm_add_ss(_mm_sub_ss(in, low), _mm_mul_ss(vr, band));
                band = _mm_add_ss(_mm_mul_ss(vf, high), band);
                _mm_store_ss(&pOut[i], band);
                if(bClip && fabsf(band[0]) > VCF_CLIP) pOut[i] = band[0] > 0 ? VCF_CLIP : -VCF_CLIP;
            }
            _mm_store_ss(&m_low, low);
            _mm_store_ss(&m_high, high);
            _mm_store_ss(&m_band, band);
        }else{
            float low = m_low, high = m_high, band = m_band;
            for(unsigned int i = 0; i < size; i++){
                // 2x oversampling
                low += f * band;
                float in = pIn[i] * q;
                high = in - low + r * band;
                band += f * high;
                low += f * band;
                high = in - low + r * band;
                band += f * high;
                if(bClip && fabsf(band) > VCF_CLIP) band = band > 0 ? VCF_CLIP : -VCF_CLIP;
                pOut[i] = band;
            }
            m_low = low;
            m_high = high;
            m_band = band;
        }
        break;
    }
}

// 24dB ladder (Moog type) with smoothed coefficients
void CVcf::lowpass3(float* pIn, int freq, float reso, float* pOut, unsigned int size)
{
    float k, p, r;
    if(freq != m_lastFreq || reso != m_lastReso){
        double fc = (s1g->m_vcfFreqTable[freq] + s1g->m_vcfFreqTable[freq]) / (float)m_samplingFreq;
        k = (float)(3.6 * fc - 1.6 * fc * fc - 1.0);
        m_k = k;
        p = (k + 1.0f) * 0.5f;
        m_p = p;
        double t = 4.3 * fc - 2.5 * fc * fc - 1.0;
        r = (float)exp((1.0 - (t + 1.0) * 0.5) * 1.386249) * reso;
        m_r = r;
        if(m_lastFreq == -1){
            m_curK = k;
            m_curP = p;
            m_curR = r;
        }
        m_lastFreq = freq;
        m_lastReso = reso;
    }else{
        r = m_r;
        k = m_k;
        p = m_p;
    }

    float oldy3, y2, y4, y3, oldy2, y1, oldy1, curK, curP, curR, oldx;
    oldx = m_x[0]; oldy1 = m_x[1]; oldy2 = m_x[2]; oldy3 = m_x[3];
    y1 = m_y[0]; y2 = m_y[1]; y3 = m_y[2]; y4 = m_y[3];
    curK = m_curK; curP = m_curP; curR = m_curR;
    float smooth = 1.0f - m_coefSmooth;
    for(unsigned int i = 0; i < size; i++){
        float x = pIn[i] - y4 * curR;
        y1 = (x + oldx) * curP - y1 * curK;
        y2 = (y1 + oldy1) * curP - y2 * curK;
        y3 = (y2 + oldy2) * curP - y3 * curK;
        y4 = (y3 + oldy3) * curP - y4 * curK;
        if(reso > 0.9 && fabsf(y4) > VCF_CLIP) y4 = y4 > 0 ? VCF_CLIP : -VCF_CLIP;
        pOut[i] = y4;
        curK += (k - curK) * smooth;
        curP += (p - curP) * smooth;
        curR += (r - curR) * smooth;
        oldx = x;
        oldy1 = y1;
        oldy2 = y2;
        oldy3 = y3;
    }
    m_curK = curK;
    m_curP = curP;
    m_curR = curR;
    m_x[0] = oldx;
    m_x[1] = oldy1;
    m_x[2] = oldy2;
    m_x[3] = oldy3;
    m_y[0] = y1;
    m_y[1] = y2;
    m_y[2] = y3;
    m_y[3] = y4;
}

// diode ladder (zero delay feedback), 2x oversampled up to 48kHz
// Diode ladder lowpass (4 poles, zero-delay feedback) with a one-pole highpass in the feedback path.
// The statement order is significant for reproducing the released binary.
void CVcf::lowpassDiodeLadder(float* pIn, int freq, float reso, float* pOut, unsigned int size)
{
    int fs = m_samplingFreq;
    int over = fs <= 48000 ? 2 : 1;
    double fc = MIN(s1g->m_vcfFreqTable[freq] * 1.5, fs * 0.5 * 0.9);
    double wc = fc * VCF_PI / (fs * over);
    double z[5];
    // MATCH-HACK: the empty asm statements emit no code; they keep these as five separate copies,
    // as in the released binary. See docs/MATCH_HACKS.md.
    z[0] = m_z[0]; __asm__("" : "+m"(z[0]));
    z[1] = m_z[1]; __asm__("" : "+m"(z[1]));
    z[2] = m_z[2]; __asm__("" : "+m"(z[2]));
    z[3] = m_z[3]; __asm__("" : "+m"(z[3]));
    z[4] = m_z[4]; __asm__("" : "+m"(z[4]));
    double g = tan(wc);
    // ladder coefficients (computed before the feedback highpass)
    double a = 1.0 / (g + 1.0);
    double G = g * a;
    double h = G * 0.5;
    double d1 = 1.0 - h * h;
    double b1 = h / d1;
    double hb = h * b1;
    double c = h / (1.0 - hb);
    double G2 = h + h;
    double e = G2 / (1.0 - c * G2);
    double hc = c * h;
    double d3 = 1.0 - hc;
    double k = 15.0 * reso;
    double r1 = 1.0 / d1;
    // feedback highpass
    double hp;
    if(fc < 100.0){
        hp = 30.0 * VCF_PI;
    }else if(fc < 300.0){
        hp = exp((log(fc) - log(100.0)) / (log(300.0) - log(100.0)) * (log(120.0) - log(30.0)) + log(30.0)) * VCF_PI;
    }else{
        hp = 120.0 * VCF_PI;
    }
    double gh = tan(hp / (fs * over));
    double hr1 = h * r1;
    double r2 = 1.0 / (1.0 - hb);
    double hr2 = r2 * h;
    double r3 = 1.0 / d3;
    double ah = 1.0 / (gh + 1.0);
    double hbc = hb * c;
    double hbce = hbc * e;
    double og = over * 0.65f;       // output gain: oversampling part
    double G2r3 = G2 * r3;
    double r1a = a * r1;
    double r2a = r2 * a;
    double r3a = r3 * a;
    double kg = 1.0 + k * 0.2f;     // output gain: resonance compensation
    double kah = ah * k;
    double q = 1.0 / (hbce * kah + 1.0);
    double gh2 = ah * (gh + gh);
    double outGain = kg * og;
    for(unsigned int i = 0; i < size; i++){
        double s4, s2, s1, s3;
        double in = pIn[i];
        for(int j = 0; j < over; j++){
            if(j != 0) in = 0;
            s4 = z[3] * a;
            s3 = hr1 * s4 + z[2] * r1a;
            s2 = hr2 * s3 + r2a * z[1];
            s1 = s2 * G2r3 + r3a * z[0];
            double fb = (hbc * s1 + hb * s2 + h * s3 + s4) * kah;
            double hpo = z[4] * ah;     // feedback highpass state
            double u = (in - fb + hpo) * q;
            double sat;
            if(u < -3.0) sat = -1.0;
            else if(u > 3.0) sat = 1.0;
            else sat = (u * u + 27.0) * u / (u * 9.0 * u + 27.0);
            sat *= 1.0 / (7.0 / 9.0);
            s1 = sat * e + s1;
            s2 = c * s1 + s2;
            s3 = s2 * b1 + s3;
            s4 = s4 + h * s3;
            z[0] = s1 + ((sat + s2) - z[0]) * G;
            z[1] = s2 + G * ((s1 + s3) * 0.5 - z[1]);
            z[2] = s3 + ((s2 + s4) * 0.5 - z[2]) * G;
            z[3] = s4 + (s3 * 0.5 - z[3]) * G;
            z[4] = (k * s4 - z[4]) * gh2 + z[4];
        }
        pOut[i] = (float)(s3 * outGain);
    }
    memcpy(m_z, z, sizeof(z));
}

void CVcf::postProcessSignal(float* pBuf, int size, int* pProcessed)
{
    static int _abs_mask = 0x7fffffff;
    processLimitter(pBuf, size);
    if(m_distortion == 0) return;

    // soft saturation
    float d = m_distortion * 100.0f;
    float g = d + 1.0f;
    int i = 0;
    if(size > 6 && m_bSSE){
        __m128 vd = _mm_set1_ps(d);
        __m128 vg = _mm_set1_ps(g);
        __m128 mask = _mm_load1_ps((float*)&_abs_mask);
        for(; i < size; i++){
            if(((size_t)&pBuf[i] & 15) == 0) break;
            pBuf[i] = g * pBuf[i] / (fabsf(pBuf[i]) * d + 1.0f);
        }
        for(; i < size - 3; i += 4){
            __m128 x = _mm_load_ps(&pBuf[i]);
            __m128 t = _mm_add_ps(_mm_mul_ps(_mm_and_ps(x, mask), vd), _mm_set1_ps(1.0f));
            _mm_store_ps(&pBuf[i], _mm_mul_ps(_mm_div_ps(x, t), vg));
        }
    }
    for(; i < size; i++){
        pBuf[i] = g * pBuf[i] / (fabsf(pBuf[i]) * d + 1.0f);
    }
}

// peak limiter at LIMIT_LEVEL
void CVcf::processLimitter(float* pBuf, int size)
{
    float env = m_limitEnv;
    int i = 0;
    if(env > 0) goto limit;
    {
        // nothing to release: look for the first sample over the limit
        if(m_bSSE && size > 6){
            for(; i < size; i++){
                if(((size_t)&pBuf[i] & 15) == 0) break;
                if(fabsf(pBuf[i]) > LIMIT_LEVEL) goto limit;
            }
            for(; i < size - 3; i += 4){
                __m128 x = _mm_and_ps(_mm_load_ps(&pBuf[i]), (__m128)_mm_set1_epi32(0x7fffffff));
                int mask = _mm_movemask_ps(_mm_cmplt_ps(_mm_set1_ps(LIMIT_LEVEL), x));
                if(mask){
                    if(mask & 1) ;
                    else if(mask & 2) i += 1;
                    else if(mask & 4) i += 2;
                    else i += 3;
                    goto limit;
                }
            }
        }
        for(; i < size; i++){
            if(fabsf(pBuf[i]) > LIMIT_LEVEL) goto limit;
        }
        return;
    }

limit:
    {
        float rel = m_limitRelease;
        if(m_bSSE && size > 6){
            for(; i < size; i++){
                if(((size_t)&pBuf[i] & 15) == 0) break;
                float over = (float)MAX(0.0, fabs(pBuf[i]) - LIMIT_LEVEL);
                env = (env - over) * rel + over;
                pBuf[i] = LIMIT_LEVEL / (env + LIMIT_LEVEL) * pBuf[i];
            }
            __m128 venv;
            venv[3] = env;
            for(; i < size - 3; i += 4){
                __m128 x = _mm_load_ps(&pBuf[i]);
                __m128 over = _mm_max_ps(_mm_setzero_ps(),
                    _mm_add_ps(_mm_and_ps(x, (__m128)_mm_set1_epi32(0x7fffffff)), _mm_set1_ps(-LIMIT_LEVEL)));
                venv[0] = (venv[3] - over[0]) * rel + over[0];
                venv[1] = (venv[0] - over[1]) * rel + over[1];
                venv[2] = (venv[1] - over[2]) * rel + over[2];
                venv[3] = (venv[2] - over[3]) * rel + over[3];
                _mm_store_ps(&pBuf[i], _mm_mul_ps(_mm_div_ps(_mm_set1_ps(LIMIT_LEVEL), _mm_add_ps(venv, _mm_set1_ps(LIMIT_LEVEL))), x));
            }
            env = venv[3];
        }
        for(; i < size; i++){
            float over = (float)MAX(0.0, fabs(pBuf[i]) - LIMIT_LEVEL);
            env = (env - over) * rel + over;
            pBuf[i] = LIMIT_LEVEL / (env + LIMIT_LEVEL) * pBuf[i];
        }
        if(env < 1e-05) env = 0;
        m_limitEnv = env;
    }
}

void CVcf::noteOn(int note, int velocity)
{
    m_note = note;
    calcKbdTrack(note);
    // velocity curve normalized to 0..127
    m_velocity = (int)((float)(1.0 - exp((velocity / 127.0f) * -1.1)) / 0.7274682f * 127.0f);
    calcSetAmount();
    m_interpPos = m_samplingFreq / 20;
    envelopeOn();
}

void CVcf::calcKbdTrack(int note)
{
    int ofs = 0;
    if(m_kbdTrack > 0){
        double ratio = fpow(m_kbdTrack + 1.0f, (note - 48) / 12.0f);
        float hz = (float)(s1g->m_vcfFreqTable[m_freq] * ratio) / 15.0f;
        int freq = (int)(log(hz) * 1024.0 / 6.972293761972735);
        freq = MAX(freq, 0);
        freq = MIN(freq, VCF_FREQ_MAX);
        ofs = freq - m_freq;
    }
    m_kbdTrackOfs = ofs;
}

void CVcf::noteOff()
{
    envelopeOff();
}

void CVcf::setUnisonDetunedMod(float cent)
{
    static float recip_log_freq1unit = (float)(1024.0 / log(16000.0 / 15.0));
    m_unisonOfs = (int)((float)log(fpow(2.0, cent / 1200.0f)) * recip_log_freq1unit);
}

void CVcf::setKbdTrack(float amount)
{
    m_kbdTrack = amount;
    if(m_envStatus != ENV_OFF){
        calcKbdTrack(m_note);
        calcSetAmount();
    }
    m_startFreq = m_curFreq;
    m_interpPos = 0;
}

// biquad (unused by the synth)
// same RBJ biquad cookbook code as CBiquadFilter::setParam (distortion.cpp)
void CBQF::setParam(int type, float freq, float q, float gain, int fs)
{
    float w = 2.0 * M_PI * freq / fs;
    float sn = sin(w);
    float cs = cos(w);
    float alpha = sn / (q + q);
    float A = pow(10.0, gain / 40.0f);
    float b0, b2, b1, a0, a1, a2;
    switch (type) {
    case 0: // LPF
        b0 = (1.0f - cs) * 0.5f;
        b1 = 1.0f - cs;
        b2 = (1.0f - cs) * 0.5f;
        a0 = 1.0f + alpha;
        a1 = cs * -2.0f;
        a2 = 1.0f - alpha;
        break;
    case 1: // HPF
        b0 = (1.0f + cs) * 0.5f;
        b1 = -(1.0f + cs);
        b2 = (1.0f + cs) * 0.5f;
        a0 = 1.0f + alpha;
        a1 = cs * -2.0f;
        a2 = 1.0f - alpha;
        break;
    case 2: // BPF (constant skirt gain)
        b0 = sn * 0.5f;
        b1 = 0.0f;
        b2 = sn * -0.5f;
        a0 = 1.0f + alpha;
        a1 = cs * -2.0f;
        a2 = 1.0f - alpha;
        break;
    case 3: // BPF (constant peak gain)
        b0 = alpha;
        b1 = 0.0f;
        b2 = -alpha;
        a0 = 1.0f + alpha;
        a1 = cs * -2.0f;
        a2 = 1.0f - alpha;
        break;
    case 6: // peaking EQ
        b0 = 1.0f + alpha * A;
        b1 = cs * -2.0f;
        b2 = 1.0f - alpha * A;
        a0 = 1.0f + alpha / A;
        a1 = cs * -2.0f;
        a2 = 1.0f - alpha / A;
        break;
    case 7: // low shelf
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
    m_b[0] = b0 / a0;
    m_b[1] = b1 / a0;
    m_b[2] = b2 / a0;
    m_a[1] = a1 / a0;
    m_a[2] = a2 / a0;
}

int CBQF::process(int size, float* pBuf)
{
    for(int i = 0; i < size; i++){
        float in = pBuf[i];
        float out = (float)((double)m_b[0] * in + (double)m_b[1] * m_x[1] + (double)m_b[2] * m_x[2]
            - (double)m_a[1] * m_y[1] - (double)m_a[2] * m_y[2]);
        out = MIN(2.0f, out);
        m_y[0] = out;
        m_x[2] = m_x[1];
        m_x[1] = in;
        m_y[2] = m_y[1];
        m_y[1] = out;
        pBuf[i] = out;
    }
    return size;
}
