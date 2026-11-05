/**
 * @file vco.cpp
 * @brief CVco: oscillators (OSC1, OSC2, sub oscillator) of a voice.
 *
 * Band limited wavetables (2048 points, one table per harmonics count),
 * 16.16 fixed point phase.  The sample loops are in vcoMain.cpp.
 * This file builds the shared tables, handles the parameters, portamento, phase reset and FM
 * smoothing, and picks the render loop for each block.
 */
#include <stdlib.h>
#include <math.h>
#include "common.h"
#include "synth1.h"
#include "vco.h"
#include "vcf.h"

/**
 * @brief Describes one band limited table: where it starts and the highest frequency it serves.
 */
typedef struct tagTableInfo {
    int size;               // +0x0  points
    unsigned int offset;    // +0x4  start index in the table memory
    float freq;             // +0x8  highest frequency this table is used for
} TableInfo;

#define TABLE_INFO_MAX  52

// compares positive floats as integers
#define FLOAT_AS_INT(f) (*(int*)&(f))

static float* sinTable;
static int g_tableNum;
static int g_freqAliasBorder;
static float* g_waveTablesPointer[6];
static float* sawTable;
static float* triTable;
static float* squTable;

/**
 * @brief Static helper whose destructor frees the shared wavetables at program exit.
 */
class _CFreeResource {
public:
    ~_CFreeResource(){
        if (sinTable) free(sinTable);
        if (sawTable) free(sawTable);
        if (triTable) free(triTable);
        if (squTable) free(squTable);
    }
};
static _CFreeResource g_freeRes;

double g_smoothCosTable[257];
TableInfo g_tableInfoTri[TABLE_INFO_MAX];
TableInfo g_tableInfoSaw[TABLE_INFO_MAX];
TableInfo g_tableInfoSqu[TABLE_INFO_MAX];
TableInfo* g_tableInfosPointer[6];

// sub oscillator wave -> table
static const int g_subwfmap[4] = { VCO_SINE, VCO_TRIANGLE, VCO_SAW, VCO_SQUARE };


/**
 * @brief Returns the number of harmonics of @p freq that fit below 22050 Hz.
 *
 * @param freq  Fundamental in Hz.
 * @return floor(22050 / freq), at least 1.
 */
inline int getHarmonicsNum(float freq)
{
    float h = 22050.f / freq;
    return (int)MAX(1.f, h);
}

/**
 * @brief Returns the band limited table to use for a waveform at a given frequency.
 *
 * Binary search (comparing the positive floats as integers) for the first table whose
 * upper frequency is above @p freq; below the alias border the next lower (richer) table
 * is used unless the frequency matches exactly.
 *
 * @param wave  VCO_SINE, VCO_SAW, VCO_PULSE, VCO_TRIANGLE or VCO_SQUARE (pulse uses the saw
 *              tables; VCO_NOISE has no table and must not be passed).
 * @param freq  Oscillator frequency in Hz.
 * @return Pointer to the first of VCO_TABLE_SIZE samples of the selected table.
 */
float* getWaveTablePointerSize(int wave, float freq)
{
    if (wave == VCO_SINE) {
        return sinTable;
    }
    TableInfo* pInfo = g_tableInfosPointer[wave];
    int hi = g_tableNum - 1;
    int lo = 0;
    while (lo < hi) {
        int mid = (lo + hi) / 2;
        if (FLOAT_AS_INT(pInfo[mid].freq) > FLOAT_AS_INT(freq)) {
            hi = mid;
        } else {
            lo = mid + 1;
        }
    }
    if (pInfo[lo].freq != freq) {
        if (freq <= g_freqAliasBorder && lo > 0) {
            lo--;
        }
    }
    return g_waveTablesPointer[wave] + pInfo[lo].offset;
}

void CVco::makeFreqTable(int masterTune)
{
    if (s1g->m_masterTune != masterTune) {
        s1g->m_baseFreq = (float)(fpow(2.0, masterTune / 1200.0) * 440.0);
        for (int i = 0; i < 128; i++) {
            s1g->m_freqTable[i] = (float)(s1g->m_baseFreq * fpow(2.0, (float)(i - 69) / 12.0));
        }
        s1g->m_baseFreq *= 0.5f;
    }
    s1g->m_masterTune = masterTune;
}

/**
 * @brief Builds the shared oscillator tables once (later calls return immediately).
 *
 * Creates the sine table, the band limited saw, triangle and square tables (one 2048 point
 * table per harmonics count, square = difference of two saws), the table lookup info, the
 * pitch tables (s1g->m_fineTable, s1g->m_octTable, note frequencies from the "mastertune" ini
 * value) and g_smoothCosTable.
 * @note Not thread safe; called from the CVco constructor.
 */
static void initVco()
{
    static bool bFirst = false;
    if (bFirst) {
        return;
    }
    bFirst = true;

    // band limited tables: one per harmonics count
    int offset = 0;
    int prevHarm = 0;
    int n = 0;
    int harm;
    for (int i = 0; ; i++) {
        float freq = (float)(fpow(512.0, i / 36.f) * 27.5);
        harm = getHarmonicsNum(freq);
        if (i == 23) {
            break;
        }
        if (prevHarm != harm) {
            g_tableInfoSaw[n].size = VCO_TABLE_SIZE;
            g_tableInfoSaw[n].freq = freq;
            g_tableInfoSaw[n].offset = offset;
            g_tableInfoSqu[n] = g_tableInfoSaw[n];
            g_tableInfoTri[n] = g_tableInfoSaw[n];
            n++;
            offset += VCO_TABLE_SIZE;
            prevHarm = harm;
        }
    }
    g_freqAliasBorder = MAX(22050 / harm, 1);
    for (; harm > 0; harm--) {
        float freq = MAX(1.f, 22050.f / harm);
        g_tableInfoSaw[n].size = VCO_TABLE_SIZE;
        g_tableInfoSaw[n].freq = freq;
        g_tableInfoSaw[n].offset = offset;
        g_tableInfoSqu[n] = g_tableInfoSaw[n];
        g_tableInfoTri[n] = g_tableInfoSaw[n];
        offset += VCO_TABLE_SIZE;
        n++;
    }
    g_tableNum = n;

    sinTable = (float*)malloc(sizeof(float) * VCO_TABLE_SIZE);
    for (int i = 0; i < VCO_TABLE_SIZE; i++) {
        sinTable[i] = (float)sin(i * 6.283185307179586 / VCO_TABLE_SIZE);
    }

    // saw
    sawTable = (float*)malloc(sizeof(float) * offset);
    float* p = sawTable;
    for (int i = 0; i < n; i++) {
        int h = getHarmonicsNum(g_tableInfoSaw[i].freq);
        int size = g_tableInfoSaw[i].size;
        for (int j = 0; j < size; j++) {
            p[j] = 0;
            for (int k = 1; k <= h; k++) {
                p[j] += 1.f / k * sinTable[(j * k) % size];
            }
            p[j] = p[j] * 2.0 / M_PI;
        }
        p += size;
    }

    // triangle
    triTable = (float*)malloc(sizeof(float) * offset);
    p = triTable;
    for (int i = 0; i < n; i++) {
        int h = getHarmonicsNum(g_tableInfoTri[i].freq);
        int size = g_tableInfoTri[i].size;
        for (int j = 0; j < size; j++) {
            p[j] = 0;
            for (int k = 1; k <= h; k += 2) {
                if (k % 4 == 1) {
                    p[j] = p[j] + 1.0 / (float)(k * k) * sinTable[(j * k) % size];
                } else {
                    p[j] = p[j] - 1.0 / (float)(k * k) * sinTable[(j * k) % size];
                }
            }
            p[j] = p[j] * 8.0 / M_PI / M_PI;
        }
        p += size;
    }

    // square = difference of two saws
    squTable = (float*)malloc(sizeof(float) * offset);
    p = squTable;
    float* pSaw = sawTable;
    for (int i = 0; i < n; i++) {
        int size = g_tableInfoSqu[i].size;
        for (int j = 0; j < size; j++) {
            p[j] = (pSaw[j] - pSaw[(j + size / 2) & VCO_TABLE_MASK]) * 0.5f;
        }
        p += size;
        pSaw += size;
    }

    g_waveTablesPointer[VCO_SINE] = sinTable;
    g_waveTablesPointer[VCO_SAW] = sawTable;
    g_waveTablesPointer[VCO_PULSE] = sawTable;
    g_waveTablesPointer[VCO_TRIANGLE] = triTable;
    g_waveTablesPointer[VCO_SQUARE] = squTable;
    g_tableInfosPointer[VCO_SAW] = g_tableInfoSaw;
    g_tableInfosPointer[VCO_PULSE] = g_tableInfoSaw;
    g_tableInfosPointer[VCO_TRIANGLE] = g_tableInfoTri;
    g_tableInfosPointer[VCO_SQUARE] = g_tableInfoSqu;

    // pitch tables
    for (int i = 0; i <= 256; i++) {
        s1g->m_fineTable[i] = (float)fpow(2.0, (i - 128) / 128.f);
    }
    for (int i = 0; i < 21; i++) {
        s1g->m_octTable[i] = (float)fpow(2.0, i - 10);
    }
    CVco::makeFreqTable(getIniInt("mastertune", 0));

    for (int i = 0; i < 256; i++) {
        g_smoothCosTable[i] = (cos(i * M_PI / 257.0) + 1.0) * 0.5;
    }
}

CVco::CVco()
{
    initVco();
}

CVco::~CVco()
{
}

void CVco::init()
{
    m_freq2 = 100.f;
    m_freq1 = 100.f;
    for (int i = 0; i < 8; i++) {
        m_ssFreq[i] = 100.f;
    }
    m_ssDetune = 0;
    m_shape = VCO_SAW;
    m_fmAmountVal = 0;
    m_shape2 = VCO_NOISE;
    m_osc2Pitch = 0;
    m_osc2Tune = 0;
    m_bOsc2KbdTrack = 1;
    m_mix = 0.5f;
    m_bRing = 0;
    m_lfoDest[1] = 0;
    m_bSync = 0;
    m_pw = 1.f;
    m_commonTune = 0;
    m_fmPrevTarget = 0;
    m_bFMReset = 1;
    m_fmAmount = 0;
    m_fmTarget = 0;
    m_detune = 0;
    m_lfoDest[0] = 0;
    m_subGain = 0;
    m_subWaveForm = 1;
    m_subOct = 1;
    m_gainScale = 1.f;
    setMax(640);
    setGain(0);
    setAttack(0);
    setDecay(0);
    setSustain(0);
    setRelease(0);
    m_bProcessIdle = 1;
    m_bNoInput = 1;
    m_note = 0;
    m_pitchEnvLevel = 0;
    m_keyShift = 0;
    m_portDiff = 0;
    m_portPos = 0;
    m_portCur = 0;
    m_portamentTime = 0;
    m_portLen = 0;
    calcCoeff();
    m_invFs = 1.0 / m_samplingFreq;
    m_fsPerMs = (int)(m_samplingFreq * 0.001f);
    m_noise = 10;
    m_osc2PitchPos = m_samplingFreq / 20;
    m_bOsc2PitchChanged = 0;
}

void CVco::calcCoeff()
{
    m_osc1Mix = 1.f - m_mix;
    m_osc2Mix = m_mix;
    m_osc1MixHalf = m_osc1Mix * 0.5f;
    m_osc2MixHalf = m_osc2Mix * 0.5f;
    float g = (m_subGain + 1.f) * m_osc1Mix + m_osc2Mix;
    m_gainScale = 1.f / g;
    m_pVcf->setGainScale(g);
}

void CVco::onChangeSamplingFreq()
{
    CEnvelopedObject::onChangeSamplingFreq();
    setPortamentTime(m_portamentTime);
    m_fmPrevTarget = -1;
    m_fmTargetTime = m_samplingFreq / 10;
    calcCoeff();
    m_invFs = 1.0 / m_samplingFreq;
    m_fsPerMs = m_samplingFreq / 1000;
}

void CVco::setPortamentTime(float sec)
{
    m_portamentTime = sec;
    int prevLen = m_portLen;
    int len = (int)(m_samplingFreq * sec);
    m_portLen = len;
    if (prevLen == 0) {
        m_portPos = len;
    } else {
        m_portPos = MIN(len, (int)((long)len * m_portPos / prevLen));
        m_portRes = MAX((int)((long)m_portRes * len / prevLen), 32);
        m_portNextRes = (int)((long)m_portNextRes * len / prevLen);
    }
}

void CVco::setFMAmount(int val)
{
    m_fmAmountVal = val;
    m_fmPrevTarget = -1;
    m_fmTargetTime = m_samplingFreq / 10;
}

void CVco::setDetune(float cent)
{
    m_detune = cent;
    calcOsc1Freq();
    calcOsc2Freq();
}

void CVco::calcOsc1Freq()
{
    int nn = m_note + m_keyShift;
    nn = nn > 128 ? 128 : (nn < 0 ? 0 : nn);
    m_freq1 = (float)(s1g->m_freqTable[nn] * fpow(2.0, (m_commonTune + m_detune) / 1200.0));
    if (m_ssDetune > 0) {
        calcSuperSawFreq();
    }
}

void CVco::calcOsc2Freq()
{
    float* pBase;
    float cent;
    if (m_bOsc2KbdTrack) {
        pBase = &m_freq1;
        cent = m_osc2Tune;
    } else {
        pBase = &s1g->m_baseFreq;
        cent = m_osc2Tune + m_commonTune + m_detune;
    }
    m_freq2 = (float)(*pBase * fpow(2.0, m_osc2Pitch + cent / 1200.0));
}

void CVco::calcSuperSawFreq()
{
    float detune = m_ssDetune;
    for (int i = 0; i < 8; i += 2) {
        m_ssFreq[i] = (float)(m_freq1 * exp((i + 1) * detune / 1200.f * log(2.0)));
        m_ssFreq[i + 1] = (float)(m_freq1 * exp(-((i + 1) * detune) / 1200.f * log(2.0)));
    }
}

void CVco::setSSDetune(float cent)
{
    float prev = m_ssDetune;
    m_ssDetune = cent;
    if (cent > 0) {
        calcSuperSawFreq();
    }
    if ((cent > 0) != (prev > 0)) {
        calcMaxGain();
    }
}

void CVco::calcMaxGain()
{
    float g = (m_subGain + 1.f) * m_osc1Mix + m_osc2Mix;
    m_gainScale = 1.f / g;
    m_pVcf->setGainScale(g);
}

float CVco::calcFreqMod(float freq, int mod)
{
    if (mod) {
        if (abs(mod) > 1280) {
            mod = mod > 0 ? 1280 : -1280;
        }
        freq *= s1g->m_octTable[10 + mod / 128] * s1g->m_fineTable[128 + mod % 128];
    }
    if (FLOAT_AS_INT(freq) > 0x46abdfff) {  // > 22000.f
        freq = 22000.f;
    }
    return freq;
}

void CVco::setShape(int shape)
{
    m_shape = shape;
}

void CVco::setSubOscGain(float gain)
{
    m_subGain = gain;
    calcMaxGain();
}

void CVco::setSubWaveForm(int wf)
{
    m_subWaveForm = wf;
}

void CVco::setSubOct(int oct)
{
    m_subOct = oct;
}

void CVco::setVCF(CVcf* pVcf)
{
    m_pVcf = pVcf;
}

void CVco::setShape2(int shape)
{
    m_shape2 = shape;
}

void CVco::setOsc2Pitch(float pitch)
{
    m_osc2PitchTarget = pitch;
    if (m_envStatus == ENV_OFF) {
        m_osc2Pitch = pitch;
    } else {
        m_osc2PitchStart = m_osc2Pitch;
        m_osc2PitchPos = 0;
    }
    m_bOsc2PitchChanged = 1;
}

void CVco::setOsc2Tune(int cent)
{
    m_osc2Tune = (float)cent;
    calcOsc2Freq();
}

void CVco::setOsc2KbdTrack(int bTrack)
{
    m_bOsc2KbdTrack = bTrack;
    calcOsc2Freq();
}

void CVco::setMix(float mix)
{
    m_mix = mix;
    calcCoeff();
}

void CVco::setCommonTune(int cent)
{
    if (m_commonTune != (float)cent) {
        m_commonTune = (float)cent;
        calcOsc1Freq();
        calcOsc2Freq();
    }
}

void CVco::changeLfoSetting(int no, int bEnable, int dest)
{
    if (bEnable && (dest == LFO_DEST_OSC2 || dest == LFO_DEST_OSC12 || dest == LFO_DEST_PW || dest == LFO_DEST_FM)) {
        enableLfo(no, 1);
    } else {
        dest = 0;
        enableLfo(no, 0);
    }
    m_lfoDest[no] = dest;
}

void CVco::setSync(int bSync)
{
    m_bSync = bSync;
}

void CVco::setRing(int bRing)
{
    m_bRing = bRing;
}

void CVco::setPW(float pw)
{
    m_pw = pw;
}

void CVco::setKeyShift(int shift)
{
    m_keyShift = shift;
    calcOsc1Freq();
    calcOsc2Freq();
}

void CVco::setOsc2PitchAmount(int val)
{
    float amount = 0;
    if (val != 64) {
        if (val < 64) {
            amount = val / 64.f - 1.f;
        } else {
            amount = (val - 64) / 63.f;
        }
    }
    setGain(amount);
}

void CVco::setOsc2PitchAttack(float sec)
{
    setAttack(sec);
}

void CVco::setOsc2PitchDecay(float sec)
{
    setDecay(sec);
}

void CVco::setOsc2PitchEnvEnable(int bEnable)
{
    if (bEnable) {
        setEnvelopEnable(1);
    } else {
        setEnvelopEnable(0);
    }
}

void CVco::setOscModEnvDest(int dest)
{
    m_oscModEnvDest = dest;
    switch (dest) {
    case 0:
        setMax(640);
        setDecayType(ENV_CURVE_EXP);
        break;
    case 1:
        setMax(512);
        setDecayType(ENV_CURVE_LINEAR);
        break;
    case 2:
        setMax(128);
        setDecayType(ENV_CURVE_EXP);
        break;
    }
}

void CVco::setVoiceNo(int no)
{
    m_phaseMode = 0;
    m_phaseShift = 0;
    m_bInitPhase = 1;
}

void CVco::initPhaseParam(int no)
{
    m_phaseMode = 0;
    m_phaseShift = 0;
    m_bInitPhase = 1;
}

void CVco::setUnisonInfo(int num, int no, float spread)
{
    m_unisonNum = num;
    m_unisonNo = no;
    m_unisonPhaseSpread = spread;
}

void CVco::setPhaseShift(int val)
{
    m_phaseShift = MAX(val - 1, 0) / 126.f;
    m_phaseMode = val > 0;
    if (val <= 0) {
        m_bInitPhase = 1;
    }
}

void CVco::setPhaseMode(int mode)
{
    m_phaseMode = mode;
    if (mode == 0) {
        m_bInitPhase = 1;
    }
}

void CVco::initFreePhase()
{
    m_bInitPhase = 1;
}

void CVco::calcMultiDetunePhaseCoef(float amount, int start, int num, float* pCoef)
{
    for (int i = 0; i < num; i++) {
        pCoef[i] = g_rnd.m_table[(start + i) % RND_TABLE_SIZE] * amount;
    }
}

void CVco::startPhase()
{
    float coef[10];
    if (m_phaseMode == 1) {
        m_phase1 = 0;
        m_phase2 = ftoi(m_phaseShift * 67108864.f) & VCO_PHASE_MASK;
        calcMultiDetunePhaseCoef(m_phaseShift, 0, 8, coef);
        for (int i = 0; i < 8; i++) {
            m_ssPhase[i] = ftoi(coef[i] * 134217728.f) & VCO_PHASE_MASK;
        }
        if (m_unisonNum > 1 && m_unisonPhaseSpread > 0) {
            calcMultiDetunePhaseCoef(m_unisonPhaseSpread, m_unisonNo * 10, 10, coef);
            m_phase1 += ftoi(coef[0] * 134217728.f) & VCO_PHASE_MASK;
            m_phase2 += ftoi(coef[1] * 134217728.f) & VCO_PHASE_MASK;
            for (int i = 0; i < 8; i++) {
                m_ssPhase[i] += ftoi(coef[i + 2] * 134217728.f) & VCO_PHASE_MASK;
            }
        }
        m_bInitPhase = 0;
        return;
    }
    if (m_bInitPhase) {
        // free running: random phases
        m_phase1 = ftoi((float)((float)g_rnd.m_rand.rand() * (1.0 / MYRAND_MAX)) * 134217728.f);
        m_phase2 = ftoi((float)((float)g_rnd.m_rand.rand() * (1.0 / MYRAND_MAX)) * 134217728.f);
        for (int i = 0; i < 8; i++) {
            m_ssPhase[i] = ftoi((float)((float)g_rnd.m_rand.rand() * (1.0 / MYRAND_MAX)) * 134217728.f);
        }
    }
    m_bInitPhase = 0;
}

/**
 * @brief Converts a key number to a pitch for portamento.
 *
 * @param nn    Key number (with key shift); values above 128 count as 128, negative ones as 0.
 * @return Pitch in 1/128 octave units (nn * 128 / 12).
 */
static inline int getNotePitch(int nn)
{
    int pitch = 0;
    if (nn > 128) pitch = 128 * 128 / 12;
    if (nn >= 0 && nn <= 128) pitch = nn * 128 / 12;
    return pitch;
}

void CVco::setNote(int nn, int bPortament)
{
    int prevPitch = getNotePitch(m_note + m_keyShift);
    m_note = nn;
    calcOsc1Freq();
    calcOsc2Freq();
    int pitch = getNotePitch(m_note + m_keyShift);
    if (bPortament) {
        int diff = prevPitch - pitch + m_portCur;
        m_portDiff = diff;
        m_portPos = 0;
        m_portCur = diff;
        m_portStep = 1;
        int res = m_portLen / MAX(abs(diff), 1);
        m_portRes = res / 10 < 32 ? 32 : res / 10;
        m_portNextRes = m_portLen / 10;
    } else {
        m_portDiff = 0;
        m_portPos = 0;
        m_portCur = 0;
    }
}

void CVco::noteOn()
{
    envelopeOn();
    m_fmPrevTarget = -1;
    m_fmTargetTime = 0;
    if (m_bOsc2PitchChanged) {
        m_osc2Pitch = m_osc2PitchTarget;
        m_bOsc2PitchChanged = 0;
        calcOsc2Freq();
    }
    startPhase();
}

void CVco::resetFMSmooth()
{
    m_bFMReset = 1;
}

/**
 * @brief Renders size samples, gliding the OSC2 pitch to its new value.
 *
 * While m_osc2Pitch differs from its target, the pitch is interpolated linearly over 50 ms
 * (m_samplingFreq / 20 samples) and the block is cut into short pieces so the frequency is
 * refreshed (calcOsc2Freq()) as it moves. Advances m_osc2PitchPos.
 *
 * @param pVco      The oscillator.
 * @param pitchMod1 OSC1 pitch modulation (1/128 octave).
 * @param pitchMod2 OSC2 pitch modulation (1/128 octave).
 * @param pwMod     Pulse width modulation.
 * @param fmMod     FM modulation.
 * @param[out] pBuf Receives size samples.
 * @param size      Number of samples.
 */
static inline void createWaveGlide(CVco* pVco, int pitchMod1, int pitchMod2, int pwMod, int fmMod, float* pBuf, int size)
{
    int len = pVco->m_samplingFreq / 20;
    while (size) {
        int n = size;
        if (pVco->m_osc2PitchTarget == pVco->m_osc2Pitch || pVco->m_osc2PitchPos >= len) {
            pVco->m_osc2Pitch = pVco->m_osc2PitchTarget;
        } else {
            pVco->m_osc2Pitch = (pVco->m_osc2PitchTarget - pVco->m_osc2PitchStart) * pVco->m_osc2PitchPos / len + pVco->m_osc2PitchStart;
            pVco->calcOsc2Freq();
            int d = abs(ftoi((pVco->m_osc2PitchTarget - pVco->m_osc2PitchStart) * 128.f));
            if (d < 1) d = 1;
            d = len / d;
            if (d < 1) d = 1;
            n = MIN(n, d);
        }
        n = pVco->createWaveMain(pitchMod1, pitchMod2, pwMod, fmMod, pBuf, n);
        pVco->m_osc2PitchPos += n;
        size -= n;
        pBuf += n;
    }
}

int CVco::processEnvelopedSignal(int level, float* pBuf, int size, int lfo1, int lfo2)
{
    int pitchEnv = m_pitchEnvLevel;
    // OSC1 pitch
    int osc1Lfo1 = 0;
    if (m_lfoDest[0] == 8 || m_lfoDest[0] == LFO_DEST_OSC12) osc1Lfo1 = lfo1;
    int osc1Lfo2 = 0;
    if (m_lfoDest[1] == 8 || m_lfoDest[1] == LFO_DEST_OSC12) osc1Lfo2 = lfo2;
    // OSC2 pitch
    int osc2Kbd = 0;
    if (m_bOsc2KbdTrack) osc2Kbd = pitchEnv;
    int osc2Env = 0;
    if (m_oscModEnvDest == 0) osc2Env = level;
    int osc2Lfo1 = 0;
    if (m_lfoDest[0] == LFO_DEST_OSC2 || m_lfoDest[0] == LFO_DEST_OSC12) osc2Lfo1 = lfo1;
    int osc2Lfo2 = 0;
    if (m_lfoDest[1] == LFO_DEST_OSC2 || m_lfoDest[1] == LFO_DEST_OSC12) osc2Lfo2 = lfo2;
    // pulse width
    int pwLfo1 = 0;
    if (m_lfoDest[0] == LFO_DEST_PW) pwLfo1 = lfo1;
    int pwLfo2 = 0;
    if (m_lfoDest[1] == LFO_DEST_PW) pwLfo2 = lfo2;
    int pwEnv = 0;
    if (m_oscModEnvDest == 2) pwEnv = level;
    // FM
    int fmLfo1 = 0;
    if (m_lfoDest[0] == LFO_DEST_FM) fmLfo1 = lfo1;
    int fmLfo2 = 0;
    if (m_lfoDest[1] == LFO_DEST_FM) fmLfo2 = lfo2;
    int fmEnv = 0;
    if (m_oscModEnvDest == 1) fmEnv = level;

    int mod1 = osc1Lfo1 + pitchEnv + osc1Lfo2;
    int mod2 = osc2Lfo2 + osc2Lfo1 + osc2Kbd + osc2Env;
    int modPW = pwLfo1 + pwLfo2 + pwEnv;
    int modFM = fmLfo2 + fmLfo1 + fmEnv;

    int rest = size;
    float* p = pBuf;
    while (rest > 0) {
        int n;
        if (m_portPos < m_portLen && m_portDiff != 0) {
            // portamento
            int step = m_portPos / m_portRes;
            int cur;
            if (m_portStep == step) {
                cur = m_portCur;
            } else {
                double r = (double)m_portPos / (double)m_portLen;
                double diff = m_portDiff;
                cur = (int)(exp(r * -4.0) * (1.0 - r) * diff);
                m_portCur = cur;
                if (m_portPos >= m_portNextRes) {
                    int diff = MAX(abs(m_portDiff), 1);
                    int next, res;
                    if (m_portNextRes >= m_portLen * 55 / 100) {
                        res = m_portLen * 9 / diff;
                        next = m_portLen;
                    } else if (m_portNextRes >= m_portLen / 4) {
                        res = m_portLen / diff;
                        next = m_portLen * 55 / 100;
                    } else if (m_portNextRes >= m_portLen / 10) {
                        res = m_portLen * 2 / diff / 5;
                        next = m_portLen / 4;
                    } else {
                        res = m_portLen / diff / 4;
                        next = m_portLen / 10;
                    }
                    m_portNextRes = next;
                    res = MAX(res, 32);
                    res = MIN(res, next - m_portPos);
                    m_portRes = MAX(res, 1);
                    step = m_portPos / m_portRes;
                }
                m_portStep = step;
            }
            n = MIN((step + 1) * m_portRes - m_portPos, rest);
            createWaveGlide(this, cur + mod1, (m_bOsc2KbdTrack ? cur : 0) + mod2, modPW, modFM, p, n);
            m_portPos += n;
        } else {
            m_portCur = 0;
            createWaveGlide(this, mod1, mod2, modPW, modFM, p, rest);
            n = rest;
        }
        p += n;
        rest -= n;
    }
    return 0;
}

int CVco::createWaveMain(int pitchMod1, int pitchMod2, int pwMod, int fmMod, float* pBuf, int size)
{
    tagCalcSigParams prm;
    prm.size = size;
    prm.pBuf = pBuf;
    prm.gain1 = m_osc1Mix * m_gainScale;
    prm.subGain = prm.gain1 * m_subGain;
    prm.gain2 = m_osc2Mix * m_gainScale;
    prm.subOct = m_subOct;
    float freq1 = calcFreqMod(m_freq1, pitchMod1);
    float freq2 = calcFreqMod(m_freq2, pitchMod2);
    prm.pTable1 = getWaveTablePointerSize(m_shape, freq1);
    if (m_shape2 != VCO_NOISE) {
        prm.pTable2 = getWaveTablePointerSize(m_shape2, freq2);
    }
    prm.pSubTable = getWaveTablePointerSize(g_subwfmap[m_subWaveForm], prm.subOct ? freq1 * 0.5f : freq1);
    prm.phase1 = m_phase1;
    prm.phase2 = m_phase2;
    prm.dPhase1 = (int)(float)(freq1 * 134217728.f * m_invFs);
    prm.dPhase2 = ftoi((float)(freq2 * 134217728.f * m_invFs));

    if (m_shape == VCO_PULSE || m_shape2 == VCO_PULSE) {
        float range = (pwMod > 0) ? 2.f - m_pw : m_pw;
        double w = m_pw + pwMod * (1.f / 128.f) * range;
        prm.pwOffset = (int)(MIN(1.99, MAX(0.01, w)) * 1024.0);
        if (m_shape == VCO_PULSE && !m_bRing) {
            prm.gain1 *= 0.5f;
        }
    }

    int bDetune = 0;
    if (m_ssDetune > 0) {
        double k = m_invFs * 134217728.0;
        for (int i = 0; i < 8; i++) {
            prm.ssPhase[i] = m_ssPhase[i];
            prm.ssDPhase[i] = ftoi((float)(calcFreqMod(m_ssFreq[i], pitchMod1) * k));
        }
        prm.pSSPhase = m_ssPhase;
        bDetune = 1;
        if (!m_bRing) {
            prm.gain1 *= 0.3f;
            prm.subGain *= 0.3f;
        }
    }

    // FM amount (smoothed)
    if (fmMod != 0 || m_fmPrevTarget != 0) {
        float amount = (float)m_fmAmountVal;
        if (fmMod > 0) {
            amount += (float)(127 - m_fmAmountVal) * MIN(fmMod, 512) / 512.f;
        } else {
            amount -= MIN(-fmMod, 512) * amount / 512.f;
        }
        double v = amount / 127.f;
        m_fmTarget = (int)((fpow(1000.0, v) - 1.0) / 999.0 * 67108864.0);
    }
    int target = m_fmTarget;
    if (m_fmPrevTarget != target) {
        if ((unsigned int)abs(m_fmAmount - target) > 0x4000000 || m_bFMReset) {
            m_bFMReset = 0;
            m_fmSmoothLen = 0;
            m_fmAmount = target;
        } else if (m_fmSmoothLen <= m_fmTargetTime) {
            m_fmSmoothLen = m_fmTargetTime;
            m_fmSmoothCount = m_fmTargetTime;
            m_fmStart = m_fmAmount;
        }
        if (m_fmSmoothCount == 0) {
            m_fmAmount = target;
        }
        m_fmTargetTime = 0;
        m_fmPrevTarget = target;
    }
    if (m_fmTargetTime < m_fsPerMs) {
        m_fmTargetTime = MIN(m_fmTargetTime + size, m_fsPerMs);
    }
    if (m_fmAmount != target) {
        prm.size = MIN(prm.size, m_fmSmoothCount);
    }
    int noFM = (m_fmAmount == 0 && target == 0);
    prm.fmAmount = m_fmAmount;
    prm.fmDelta = target - m_fmStart;
    prm.fmTarget = target;
    prm.fmSmoothLen = m_fmSmoothLen;
    prm.fmSmoothCount = m_fmSmoothCount;
    prm.fmStart = m_fmStart;
    prm.pFmAmount = &m_fmAmount;
    prm.pFmSmoothCount = &m_fmSmoothCount;
    int shape2 = m_shape2;
    if (noFM && shape2 == VCO_PULSE) {
        prm.gain2 *= 0.5f;
    } else if (noFM && shape2 == VCO_NOISE) {
        prm.gain2 *= 0.65f;
    }
    prm.pNoise = &m_noise;
    prm.pPhase1 = &m_phase1;
    prm.pPhase2 = &m_phase2;

#define CALL_CALC(S, F, R, D) \
    if (m_shape == VCO_PULSE) { \
        if (shape2 == VCO_PULSE) calc##S##F##R##D##PP(&prm); \
        else if (shape2 == VCO_NOISE) calc##S##F##R##D##PN(&prm); \
        else calc##S##F##R##D##PS(&prm); \
    } else { \
        if (shape2 == VCO_PULSE) calc##S##F##R##D##SP(&prm); \
        else if (shape2 == VCO_NOISE) calc##S##F##R##D##SN(&prm); \
        else calc##S##F##R##D##SS(&prm); \
    }

    if (!m_bSync) {
        if (!m_bRing) {
            if (noFM) {
                if (bDetune) { CALL_CALC(_,_,_,D) } else { CALL_CALC(_,_,_,_) }
            } else {
                if (bDetune) { CALL_CALC(_,F,_,D) } else { CALL_CALC(_,F,_,_) }
            }
        } else {
            if (bDetune) { CALL_CALC(_,_,R,D) } else { CALL_CALC(_,_,R,_) }
        }
    } else {
        if (!m_bRing) {
            if (noFM) {
                if (bDetune) { CALL_CALC(S,_,_,D) } else { CALL_CALC(S,_,_,_) }
            } else {
                if (bDetune) { CALL_CALC(S,F,_,D) } else { CALL_CALC(S,F,_,_) }
            }
        } else {
            if (bDetune) { CALL_CALC(S,_,R,D) } else { CALL_CALC(S,_,R,_) }
        }
    }
    return prm.size;
}
