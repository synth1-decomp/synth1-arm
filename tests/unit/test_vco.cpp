/**
 * @file test_vco.cpp
 * @brief Unit tests for techpop/vco.cpp (CVco): the shared wavetables built by initVco(), the
 *        band limited table selection, the pitch tables in s1g, calcFreqMod(), the oscillator
 *        frequencies (note, key shift, tunes, detune, OSC2 pitch and key tracking, super saw),
 *        the gain normalization, the modulation envelope settings, the phase reset modes and the
 *        portamento state (getNotePitch() via setNote()).
 *
 * Rendering (calcSignalLoop() and the calcXXXXXX() variants through createWaveMain() and
 * processEnvelopedSignal()) is covered in test_vcomain.cpp.
 */
#include "test.h"
#include "engine.h"

#include <new>
#include <stdlib.h>
#include <math.h>

#include "common.h"
#include "synth1.h"
#include "vco.h"
#include "vcf.h"

// Non-static helper of vco.cpp (external linkage, not declared in a header).
float* getWaveTablePointerSize(int wave, float freq);

namespace {

/**
 * @brief A CVco wired to a CVcf like CVoice does, constructed in zero-filled memory.
 *
 * CVco::init() leaves a few members (FM smoothing state, unison info, portamento resolution,
 * OSC2 glide target) to be set by later calls; constructing the objects in zeroed memory keeps
 * the tests free of reads of uninitialized values. The note frequency table is reset to master
 * tune 0 (initVco() reads it from the "mastertune" preference).
 */
struct VcoRig {
    void* vcfMem;
    void* vcoMem;
    CVcf* vcf;
    CVco* vco;

    VcoRig()
    {
        t1::initEngine();
        vcfMem = calloc(1, sizeof(CVcf));
        vcoMem = calloc(1, sizeof(CVco));
        vcf = new (vcfMem) CVcf();
        vco = new (vcoMem) CVco();
        CVco::makeFreqTable(0);
        vco->setVCF(vcf);
        vco->init();
        vco->setSamplingFreq(44100);
        vco->setOscModEnvDest(0);
        vco->setVoiceNo(0);
        vco->setUnisonInfo(1, 0, 0.f);
    }

    ~VcoRig()
    {
        vco->~CVco();
        vcf->~CVcf();
        free(vcoMem);
        free(vcfMem);
    }
};

/**
 * @brief Normalized amplitude of harmonic k of a 2048 point table (1.0 for a unit sine).
 */
double harmonicAmp(const float* t, int k)
{
    static double cs[VCO_TABLE_SIZE], sn[VCO_TABLE_SIZE];
    static bool bInit = false;
    if (!bInit) {
        bInit = true;
        for (int j = 0; j < VCO_TABLE_SIZE; j++) {
            cs[j] = cos(2.0 * M_PI * j / VCO_TABLE_SIZE);
            sn[j] = sin(2.0 * M_PI * j / VCO_TABLE_SIZE);
        }
    }
    double re = 0, im = 0;
    for (int j = 0; j < VCO_TABLE_SIZE; j++) {
        int a = (j * k) & VCO_TABLE_MASK;
        re += t[j] * cs[a];
        im -= t[j] * sn[a];
    }
    return 2.0 / VCO_TABLE_SIZE * sqrt(re * re + im * im);
}

/**
 * @brief Highest harmonic of a saw table whose amplitude is at least a quarter of 2/(pi k).
 */
int highestSawHarmonic(const float* t)
{
    int h = 0;
    for (int k = 1; k < VCO_TABLE_SIZE / 2; k++) {
        if (harmonicAmp(t, k) > 0.25 * 2.0 / (M_PI * k)) h = k;
    }
    return h;
}

}  // namespace

/** @brief The sine table is one exact cycle of sin() and is used for every frequency. */
TEST(vco, sine_table_is_one_cycle)
{
    VcoRig r;
    float* s = getWaveTablePointerSize(VCO_SINE, 100.f);
    REQUIRE(s != 0);
    CHECK(getWaveTablePointerSize(VCO_SINE, 20000.f) == s);
    CHECK(getWaveTablePointerSize(VCO_SINE, 1.f) == s);
    CHECK_FEQ(s[0], 0);
    CHECK_CLOSE(s[512], 1.0, 1e-7);
    CHECK_CLOSE(s[1024], 0.0, 1e-6);
    CHECK_CLOSE(s[1536], -1.0, 1e-7);
    for (int i = 0; i < VCO_TABLE_SIZE; i += 37) {
        CHECK_CLOSE(s[i], sin(i * 2.0 * M_PI / VCO_TABLE_SIZE), 1e-7);
    }
}

/** @brief The richest saw is a falling ramp 1 - x/pi (amplitude 1, Gibbs overshoot below 1.2, no DC). */
TEST(vco, saw_table_shape)
{
    VcoRig r;
    float* t = getWaveTablePointerSize(VCO_SAW, 27.f);
    CHECK_CLOSE(t[0], 0.0, 1e-5);
    CHECK_CLOSE(t[256], 0.75, 0.01);
    CHECK_CLOSE(t[512], 0.5, 0.01);
    CHECK_CLOSE(t[1024], 0.0, 1e-4);
    CHECK_CLOSE(t[1536], -0.5, 0.01);
    double sum = 0, mx = 0;
    for (int j = 0; j < VCO_TABLE_SIZE; j++) {
        sum += t[j];
        if (fabs(t[j]) > mx) mx = fabs(t[j]);
    }
    CHECK_CLOSE(sum / VCO_TABLE_SIZE, 0.0, 1e-5);
    CHECK(mx > 1.0);
    CHECK(mx < 1.2);
    // falling between the edges
    for (int j = 100; j < 1948; j += 50) {
        CHECK(t[j + 50] < t[j]);
    }
}

/** @brief The richest triangle peaks at +-1 a quarter cycle in and has no DC. */
TEST(vco, triangle_table_shape)
{
    VcoRig r;
    float* t = getWaveTablePointerSize(VCO_TRIANGLE, 27.f);
    CHECK_CLOSE(t[0], 0.0, 1e-5);
    CHECK_CLOSE(t[256], 0.5, 0.01);
    CHECK_CLOSE(t[512], 1.0, 0.01);
    CHECK_CLOSE(t[1024], 0.0, 1e-4);
    CHECK_CLOSE(t[1536], -1.0, 0.01);
    double sum = 0, mx = 0;
    for (int j = 0; j < VCO_TABLE_SIZE; j++) {
        sum += t[j];
        if (fabs(t[j]) > mx) mx = fabs(t[j]);
    }
    CHECK_CLOSE(sum / VCO_TABLE_SIZE, 0.0, 1e-5);
    CHECK(mx <= 1.0 + 1e-3);
    CHECK(mx > 0.99);
}

/** @brief The square table is the difference of two half-cycle shifted saws: +-0.5. */
TEST(vco, square_table_is_difference_of_saws)
{
    VcoRig r;
    const float freqs[] = { 27.f, 300.f, 2000.f, 9000.f };
    for (int f = 0; f < 4; f++) {
        float* sq = getWaveTablePointerSize(VCO_SQUARE, freqs[f]);
        float* saw = getWaveTablePointerSize(VCO_SAW, freqs[f]);
        for (int j = 0; j < VCO_TABLE_SIZE; j += 13) {
            CHECK_FEQ(sq[j], (saw[j] - saw[(j + 1024) & VCO_TABLE_MASK]) * 0.5f);
        }
    }
    float* sq = getWaveTablePointerSize(VCO_SQUARE, 27.f);
    CHECK_CLOSE(sq[512], 0.5, 0.01);
    CHECK_CLOSE(sq[1536], -0.5, 0.01);
    double mx = 0;
    for (int j = 0; j < VCO_TABLE_SIZE; j++) {
        if (fabs(sq[j]) > mx) mx = fabs(sq[j]);
    }
    CHECK(mx < 0.6);
}

/** @brief The pulse oscillator uses the saw tables. */
TEST(vco, pulse_uses_saw_tables)
{
    VcoRig r;
    const float freqs[] = { 30.f, 440.f, 1300.f, 5000.f, 21000.f };
    for (int f = 0; f < 5; f++) {
        CHECK(getWaveTablePointerSize(VCO_PULSE, freqs[f]) == getWaveTablePointerSize(VCO_SAW, freqs[f]));
    }
}

/** @brief Higher frequencies never select a richer table (selection is monotonic). */
TEST(vco, table_selection_is_monotonic)
{
    VcoRig r;
    const int waves[] = { VCO_SAW, VCO_TRIANGLE, VCO_SQUARE };
    for (int w = 0; w < 3; w++) {
        float* prev = getWaveTablePointerSize(waves[w], 20.f);
        int changes = 0;
        for (float f = 20.f; f < 24000.f; f *= 1.03f) {
            float* p = getWaveTablePointerSize(waves[w], f);
            CHECK(p >= prev);
            if (p != prev) changes++;
            prev = p;
        }
        CHECK(changes > 30);
        // everything at or above 22050 Hz shares the last (single harmonic) table
        CHECK(getWaveTablePointerSize(waves[w], 22050.f) == getWaveTablePointerSize(waves[w], 40000.f));
    }
}

/**
 * @brief Band limiting: above the alias border (22050 / 14 = 1575 Hz) the selected saw has H
 *        harmonics with H * f <= 22050 < (H + 1) * f; below it the next richer table is used, so
 *        harmonics may exceed 22050 Hz by up to one table step (2^(1/4) below 1244 Hz, 1575/1244
 *        between the last log-spaced table and the border).
 */
TEST(vco, saw_tables_are_band_limited)
{
    VcoRig r;
    const float high[] = { 1600.f, 2500.f, 4100.f, 7000.f, 12000.f };
    for (int i = 0; i < 5; i++) {
        int h = highestSawHarmonic(getWaveTablePointerSize(VCO_SAW, high[i]));
        CHECK(h * high[i] <= 22050.f);
        CHECK((h + 1) * high[i] > 22050.f);
    }
    const float low[] = { 55.f, 333.f, 900.f, 1300.f, 1500.f };
    for (int i = 0; i < 5; i++) {
        int h = highestSawHarmonic(getWaveTablePointerSize(VCO_SAW, low[i]));
        CHECK(h >= (int)(22050.f / low[i]));
        CHECK(h * low[i] <= 22050.0 * 1.27);
    }
    // 1300 Hz uses the 17-harmonic table although 17 * 1300 > 22050 (one step below the border)
    CHECK_EQ(highestSawHarmonic(getWaveTablePointerSize(VCO_SAW, 1300.f)), 17);
}

/** @brief Above 11025 Hz only the fundamental is left: saw = 2/pi sin, tri = 8/pi^2 sin, square = 2/pi sin. */
TEST(vco, single_harmonic_tables)
{
    VcoRig r;
    float* s = getWaveTablePointerSize(VCO_SINE, 0.f);
    float* saw = getWaveTablePointerSize(VCO_SAW, 15000.f);
    float* tri = getWaveTablePointerSize(VCO_TRIANGLE, 15000.f);
    float* sq = getWaveTablePointerSize(VCO_SQUARE, 15000.f);
    for (int j = 0; j < VCO_TABLE_SIZE; j += 7) {
        CHECK_CLOSE(saw[j], s[j] * 2.0 / M_PI, 1e-6);
        CHECK_CLOSE(tri[j], s[j] * 8.0 / M_PI / M_PI, 1e-6);
        CHECK_CLOSE(sq[j], s[j] * 2.0 / M_PI, 1e-6);
    }
}

/** @brief The fine and octave pitch tables in s1g hold 2^((i-128)/128) and 2^(i-10). */
TEST(vco, pitch_tables)
{
    VcoRig r;
    CHECK_FEQ(s1g->m_fineTable[128], 1.f);
    CHECK_CLOSE(s1g->m_fineTable[0], 0.5, 1e-7);
    CHECK_CLOSE(s1g->m_fineTable[256], 2.0, 1e-6);
    CHECK_CLOSE(s1g->m_fineTable[192], sqrt(2.0), 1e-6);
    for (int i = 1; i <= 256; i++) {
        CHECK(s1g->m_fineTable[i] > s1g->m_fineTable[i - 1]);
    }
    for (int i = 0; i < 21; i++) {
        CHECK_FEQ(s1g->m_octTable[i], (float)ldexp(1.0, i - 10));
    }
}

/** @brief g_smoothCosTable falls from 1 towards 0 over indices 0..255 (index 256 is left at 0). */
TEST(vco, smooth_cos_table)
{
    VcoRig r;
    CHECK_FEQ(g_smoothCosTable[0], 1.0);
    CHECK_CLOSE(g_smoothCosTable[128], (cos(128 * M_PI / 257.0) + 1.0) * 0.5, 1e-15);
    CHECK(g_smoothCosTable[255] > 0.0);
    CHECK(g_smoothCosTable[255] < 1e-3);
    CHECK_FEQ(g_smoothCosTable[256], 0.0);
    for (int i = 1; i < 256; i++) {
        CHECK(g_smoothCosTable[i] < g_smoothCosTable[i - 1]);
    }
}

/** @brief makeFreqTable(): equal temperament around A4 = 440 Hz * 2^(tune/1200), base = A4/2. */
TEST(vco, make_freq_table)
{
    VcoRig r;
    CHECK_EQ(s1g->m_masterTune, 0);
    CHECK_CLOSE(s1g->m_freqTable[69], 440.0, 1e-4);
    CHECK_CLOSE(s1g->m_freqTable[57], 220.0, 1e-4);
    CHECK_CLOSE(s1g->m_freqTable[60], 261.6255653, 1e-4);
    CHECK_CLOSE(s1g->m_freqTable[0], 8.1757989, 1e-5);
    CHECK_CLOSE(s1g->m_baseFreq, 220.0, 1e-4);
    for (int i = 12; i < 128; i++) {
        CHECK_CLOSE(s1g->m_freqTable[i] / s1g->m_freqTable[i - 12], 2.0, 1e-5);
    }

    CVco::makeFreqTable(100);
    CHECK_EQ(s1g->m_masterTune, 100);
    CHECK_CLOSE(s1g->m_freqTable[69], 440.0 * pow(2.0, 1.0 / 12.0), 1e-3);
    CHECK_CLOSE(s1g->m_baseFreq, 220.0 * pow(2.0, 1.0 / 12.0), 1e-3);

    // the table is only rebuilt when the tune changes
    float saved = s1g->m_freqTable[3];
    s1g->m_freqTable[3] = -1.f;
    CVco::makeFreqTable(100);
    CHECK_FEQ(s1g->m_freqTable[3], -1.f);
    s1g->m_freqTable[3] = saved;

    CVco::makeFreqTable(0);
    CHECK_CLOSE(s1g->m_freqTable[69], 440.0, 1e-4);
    CHECK_CLOSE(s1g->m_baseFreq, 220.0, 1e-4);
}

/** @brief calcFreqMod(): 128 units per octave, clamped to +-10 octaves, output limited to 22000 Hz. */
TEST(vco, calc_freq_mod)
{
    VcoRig r;
    CVco& v = *r.vco;
    CHECK_FEQ(v.calcFreqMod(440.f, 0), 440.f);
    CHECK_FEQ(v.calcFreqMod(440.f, 128), 880.f);
    CHECK_FEQ(v.calcFreqMod(440.f, -128), 220.f);
    CHECK_FEQ(v.calcFreqMod(440.f, 256), 1760.f);
    CHECK_CLOSE(v.calcFreqMod(440.f, 64), 440.0 * sqrt(2.0), 1e-3);
    CHECK_CLOSE(v.calcFreqMod(440.f, -64), 440.0 / sqrt(2.0), 1e-3);
    CHECK_CLOSE(v.calcFreqMod(440.f, -192), 440.0 * pow(2.0, -1.5), 1e-3);
    CHECK_CLOSE(v.calcFreqMod(440.f, 1), 440.0 * pow(2.0, 1.0 / 128.0), 1e-3);
    // clamp to +-1280 (10 octaves)
    CHECK_FEQ(v.calcFreqMod(10.f, 1280), 10240.f);
    CHECK_FEQ(v.calcFreqMod(10.f, 5000), 10240.f);
    CHECK_CLOSE(v.calcFreqMod(10.f, -5000), 10.0 / 1024.0, 1e-9);
    // 22000 Hz limit (also without modulation)
    CHECK_FEQ(v.calcFreqMod(30000.f, 0), 22000.f);
    CHECK_FEQ(v.calcFreqMod(15000.f, 128), 22000.f);
    CHECK_FEQ(v.calcFreqMod(22000.f, 0), 22000.f);
    CHECK_FEQ(v.calcFreqMod(21999.f, 0), 21999.f);
    // monotonic in mod
    float prev = 0;
    for (int m = -1280; m <= 1000; m += 7) {
        float f = v.calcFreqMod(20.f, m);
        CHECK(f > prev);
        prev = f;
    }
}

/** @brief init() defaults: OSC1 saw, OSC2 noise, mix 0.5, pw 1, key tracking, envelope max 640. */
TEST(vco, init_defaults)
{
    VcoRig r;
    CVco& v = *r.vco;
    CHECK_EQ(v.m_shape, VCO_SAW);
    CHECK_EQ(v.m_shape2, VCO_NOISE);
    CHECK_FEQ(v.m_mix, 0.5f);
    CHECK_FEQ(v.m_osc1Mix, 0.5f);
    CHECK_FEQ(v.m_osc2Mix, 0.5f);
    CHECK_FEQ(v.m_osc1MixHalf, 0.25f);
    CHECK_FEQ(v.m_gainScale, 1.f);
    CHECK_FEQ(r.vcf->m_gainScale, 1.f);
    CHECK_FEQ(v.m_pw, 1.f);
    CHECK_EQ(v.m_bOsc2KbdTrack, 1);
    CHECK_EQ(v.m_bSync, 0);
    CHECK_EQ(v.m_bRing, 0);
    CHECK_FEQ(v.m_subGain, 0.f);
    CHECK_EQ(v.m_subWaveForm, 1);
    CHECK_EQ(v.m_subOct, 1);
    CHECK_FEQ(v.m_ssDetune, 0.f);
    CHECK_EQ(v.m_noise, 10);
    CHECK_EQ(v.m_max, 640);
    CHECK_FEQ(v.m_gain, 0.f);
    CHECK_EQ(v.m_bNoInput, 1);
    CHECK_EQ(v.m_bProcessIdle, 1);
    CHECK_EQ(v.m_fsPerMs, 44);
    CHECK_CLOSE(v.m_invFs, 1.0 / 44100.0, 1e-18);
    CHECK_EQ(v.m_osc2PitchPos, 2205);
    CHECK_EQ(v.m_bFMReset, 1);
    CHECK_EQ(v.m_portLen, 0);
}

/** @brief calcCoeff()/setMix()/setSubOscGain(): gains normalized by 1/((sub+1)*osc1 + osc2), sum sent to the VCF. */
TEST(vco, mix_gain_normalization)
{
    VcoRig r;
    CVco& v = *r.vco;
    v.setMix(0.25f);
    CHECK_FEQ(v.m_osc1Mix, 0.75f);
    CHECK_FEQ(v.m_osc2Mix, 0.25f);
    CHECK_FEQ(v.m_osc1MixHalf, 0.375f);
    CHECK_FEQ(v.m_osc2MixHalf, 0.125f);
    CHECK_FEQ(v.m_gainScale, 1.f);
    v.setSubOscGain(1.f);
    CHECK_FEQ(r.vcf->m_gainScale, 1.75f);
    CHECK_CLOSE(v.m_gainScale, 1.0 / 1.75, 1e-7);
    v.setMix(1.f);
    CHECK_FEQ(r.vcf->m_gainScale, 1.f);
    v.setMix(0.f);
    CHECK_FEQ(r.vcf->m_gainScale, 2.f);
    CHECK_FEQ(v.m_gainScale, 0.5f);
    v.setSubOscGain(3.f);
    CHECK_FEQ(r.vcf->m_gainScale, 4.f);
    CHECK_FEQ(v.m_gainScale, 0.25f);
    v.calcMaxGain();
    CHECK_FEQ(r.vcf->m_gainScale, 4.f);
}

/** @brief Changing sampling rate refreshes 1/fs, samples per ms, the FM smoothing time and portamento length. */
TEST(vco, sampling_rate_change)
{
    VcoRig r;
    CVco& v = *r.vco;
    v.setPortamentTime(0.5f);
    CHECK_EQ(v.m_portLen, 22050);
    v.setSamplingFreq(48000);
    CHECK_CLOSE(v.m_invFs, 1.0 / 48000.0, 1e-18);
    CHECK_EQ(v.m_fsPerMs, 48);
    CHECK_EQ(v.m_fmTargetTime, 4800);
    CHECK_EQ(v.m_fmPrevTarget, -1);
    CHECK_EQ(v.m_portLen, 24000);
    CHECK_FEQ(v.m_portamentTime, 0.5f);
}

/** @brief OSC1 frequency: note table * 2^((common tune + detune)/1200), with key shift. */
TEST(vco, osc1_frequency)
{
    VcoRig r;
    CVco& v = *r.vco;
    v.setNote(69, 0);
    CHECK_CLOSE(v.m_freq1, 440.0, 1e-3);
    v.setKeyShift(12);
    CHECK_CLOSE(v.m_freq1, 880.0, 1e-3);
    v.setKeyShift(-24);
    CHECK_CLOSE(v.m_freq1, 220.0 / 2.0, 1e-3);
    v.setKeyShift(0);
    v.setCommonTune(1200);
    CHECK_CLOSE(v.m_freq1, 880.0, 1e-3);
    v.setDetune(-600.f);
    CHECK_CLOSE(v.m_freq1, 440.0 * sqrt(2.0), 1e-3);
    v.setDetune(0.f);
    v.setCommonTune(-100);
    CHECK_CLOSE(v.m_freq1, 440.0 * pow(2.0, -1.0 / 12.0), 1e-3);

    // setCommonTune() only recomputes when the value changes
    v.m_freq1 = 1.f;
    v.setCommonTune(-100);
    CHECK_FEQ(v.m_freq1, 1.f);
    v.setCommonTune(0);
    CHECK_CLOSE(v.m_freq1, 440.0, 1e-3);

    // notes below 0 (after the key shift) clamp to note 0
    v.setKeyShift(-30);
    v.setNote(10, 0);
    CHECK_FEQ(v.m_freq1, s1g->m_freqTable[0]);
    v.setKeyShift(0);
    v.setNote(127, 0);
    CHECK_FEQ(v.m_freq1, s1g->m_freqTable[127]);
}

/** @brief OSC2 frequency with key tracking: m_freq1 * 2^(pitch + tune/1200); pitch applied at note on when the envelope is off. */
TEST(vco, osc2_frequency_kbd_track)
{
    VcoRig r;
    CVco& v = *r.vco;
    v.setNote(69, 0);
    CHECK_CLOSE(v.m_freq2, 440.0, 1e-3);
    v.setOsc2Tune(1200);
    CHECK_CLOSE(v.m_freq2, 880.0, 1e-3);
    CHECK_FEQ(v.m_osc2Tune, 1200.f);
    v.setOsc2Tune(-700);
    CHECK_CLOSE(v.m_freq2, 440.0 * pow(2.0, -7.0 / 12.0), 1e-3);
    v.setOsc2Tune(0);

    // envelope off: the pitch is taken at once, the frequency is refreshed at the next note on
    v.setOsc2Pitch(1.f);
    CHECK_FEQ(v.m_osc2Pitch, 1.f);
    CHECK_EQ(v.m_bOsc2PitchChanged, 1);
    CHECK_CLOSE(v.m_freq2, 440.0, 1e-3);
    v.noteOn();
    CHECK_EQ(v.m_bOsc2PitchChanged, 0);
    CHECK_CLOSE(v.m_freq2, 880.0, 1e-3);

    // follows the note and the detune
    v.setNote(57, 0);
    CHECK_CLOSE(v.m_freq2, 440.0, 1e-3);
    v.setDetune(1200.f);
    CHECK_CLOSE(v.m_freq2, 880.0, 1e-3);
}

/** @brief OSC2 without key tracking: A4/2 * 2^(pitch + (osc2 tune + common tune + detune)/1200), independent of the note. */
TEST(vco, osc2_frequency_fixed)
{
    VcoRig r;
    CVco& v = *r.vco;
    v.setNote(80, 0);
    v.setOsc2KbdTrack(0);
    CHECK_CLOSE(v.m_freq2, 220.0, 1e-3);
    v.setNote(30, 0);
    CHECK_CLOSE(v.m_freq2, 220.0, 1e-3);
    v.setKeyShift(7);
    CHECK_CLOSE(v.m_freq2, 220.0, 1e-3);
    v.setOsc2Tune(1200);
    CHECK_CLOSE(v.m_freq2, 440.0, 1e-3);
    v.setCommonTune(1200);
    CHECK_CLOSE(v.m_freq2, 880.0, 1e-3);
    v.setDetune(-2400.f);
    CHECK_CLOSE(v.m_freq2, 220.0, 1e-3);
    v.setOsc2Pitch(-2.f);
    v.noteOn();
    CHECK_CLOSE(v.m_freq2, 55.0, 1e-3);
    v.setOsc2KbdTrack(1);
    CHECK_CLOSE(v.m_freq2, v.m_freq1 / 2.0, 1e-3);  // pitch -2 octaves, OSC2 tune +1200
}

/** @brief Super saw: 8 voices detuned by +-1, 3, 5, 7 steps; enabled by a positive detune and follows the note. */
TEST(vco, super_saw_frequencies)
{
    VcoRig r;
    CVco& v = *r.vco;
    v.setNote(69, 0);
    for (int i = 0; i < 8; i++) CHECK_FEQ(v.m_ssFreq[i], 100.f);
    v.setSSDetune(10.f);
    CHECK_FEQ(v.m_ssDetune, 10.f);
    for (int i = 0; i < 8; i += 2) {
        CHECK_CLOSE(v.m_ssFreq[i], 440.0 * pow(2.0, (i + 1) * 10.0 / 1200.0), 1e-3);
        CHECK_CLOSE(v.m_ssFreq[i + 1], 440.0 * pow(2.0, -(i + 1) * 10.0 / 1200.0), 1e-3);
    }
    v.setNote(57, 0);
    CHECK_CLOSE(v.m_ssFreq[0], 220.0 * pow(2.0, 10.0 / 1200.0), 1e-3);
    CHECK_CLOSE(v.m_ssFreq[7], 220.0 * pow(2.0, -70.0 / 1200.0), 1e-3);

    // switched off: frequencies are no longer updated
    v.setSSDetune(0.f);
    v.setNote(69, 0);
    CHECK_CLOSE(v.m_ssFreq[0], 220.0 * pow(2.0, 10.0 / 1200.0), 1e-3);
}

/** @brief setSSDetune() recomputes the gain normalization only when the super saw is switched on or off. */
TEST(vco, super_saw_toggle_recomputes_gain)
{
    VcoRig r;
    CVco& v = *r.vco;
    r.vcf->m_gainScale = -5.f;
    v.setSSDetune(5.f);
    CHECK_FEQ(r.vcf->m_gainScale, 1.f);
    r.vcf->m_gainScale = -5.f;
    v.setSSDetune(7.f);
    CHECK_FEQ(r.vcf->m_gainScale, -5.f);
    v.setSSDetune(0.f);
    CHECK_FEQ(r.vcf->m_gainScale, 1.f);
    r.vcf->m_gainScale = -5.f;
    v.setSSDetune(0.f);
    CHECK_FEQ(r.vcf->m_gainScale, -5.f);
}

/** @brief Plain setters store their value (shape, sub oscillator, sync, ring, pulse width, FM dial). */
TEST(vco, simple_setters)
{
    VcoRig r;
    CVco& v = *r.vco;
    v.setShape(VCO_TRIANGLE);
    CHECK_EQ(v.m_shape, VCO_TRIANGLE);
    v.setShape2(VCO_SQUARE);
    CHECK_EQ(v.m_shape2, VCO_SQUARE);
    v.setSubWaveForm(3);
    CHECK_EQ(v.m_subWaveForm, 3);
    v.setSubOct(0);
    CHECK_EQ(v.m_subOct, 0);
    v.setSync(1);
    CHECK_EQ(v.m_bSync, 1);
    v.setRing(1);
    CHECK_EQ(v.m_bRing, 1);
    v.setPW(0.3f);
    CHECK_FEQ(v.m_pw, 0.3f);
    v.m_fmTargetTime = 0;
    v.m_fmPrevTarget = 0;
    v.setFMAmount(77);
    CHECK_EQ(v.m_fmAmountVal, 77);
    CHECK_EQ(v.m_fmPrevTarget, -1);
    CHECK_EQ(v.m_fmTargetTime, 4410);
    v.m_bFMReset = 0;
    v.resetFMSmooth();
    CHECK_EQ(v.m_bFMReset, 1);
}

/** @brief Modulation envelope: amount 0..127 maps to gain -1..+1 (64 = 0), attack/decay/enable go to the envelope. */
TEST(vco, mod_envelope_amount_and_times)
{
    VcoRig r;
    CVco& v = *r.vco;
    v.setOsc2PitchAmount(64);
    CHECK_FEQ(v.m_gain, 0.f);
    CHECK_EQ(v.m_bInvert, 0);
    v.setOsc2PitchAmount(127);
    CHECK_FEQ(v.m_gain, 1.f);
    CHECK_EQ(v.m_bInvert, 0);
    CHECK_EQ(v.m_peakLevel, 640);
    v.setOsc2PitchAmount(0);
    CHECK_FEQ(v.m_gain, 1.f);
    CHECK_EQ(v.m_bInvert, 1);
    v.setOsc2PitchAmount(32);
    CHECK_FEQ(v.m_gain, 0.5f);
    CHECK_EQ(v.m_bInvert, 1);
    v.setOsc2PitchAmount(96);
    CHECK_CLOSE(v.m_gain, 32.0 / 63.0, 1e-6);
    CHECK_EQ(v.m_bInvert, 0);

    v.setOsc2PitchAttack(0.1f);
    CHECK_EQ(v.m_attackLen, (int)(44100 * 0.1f));
    v.setOsc2PitchDecay(0.5f);
    CHECK_EQ(v.m_decayLen, 22050);
    v.setOsc2PitchEnvEnable(5);
    CHECK_EQ(v.m_bEnvEnable, 1);
    v.setOsc2PitchEnvEnable(0);
    CHECK_EQ(v.m_bEnvEnable, 0);
}

/** @brief setOscModEnvDest(): pitch 640/exp, FM 512/linear, pulse width 128/exp; other values keep the range. */
TEST(vco, mod_envelope_destination)
{
    VcoRig r;
    CVco& v = *r.vco;
    v.setOscModEnvDest(1);
    CHECK_EQ(v.m_oscModEnvDest, 1);
    CHECK_EQ(v.m_max, 512);
    CHECK_EQ(v.m_decayType, ENV_CURVE_LINEAR);
    v.setOscModEnvDest(2);
    CHECK_EQ(v.m_max, 128);
    CHECK_EQ(v.m_decayType, ENV_CURVE_EXP);
    v.setOscModEnvDest(0);
    CHECK_EQ(v.m_max, 640);
    CHECK_EQ(v.m_decayType, ENV_CURVE_EXP);
    v.setOscModEnvDest(3);
    CHECK_EQ(v.m_oscModEnvDest, 3);
    CHECK_EQ(v.m_max, 640);
}

/** @brief changeLfoSetting(): only OSC2, OSC1+2, PW and FM destinations enable the LFO input. */
TEST(vco, lfo_destinations)
{
    VcoRig r;
    CVco& v = *r.vco;
    const int ok[] = { LFO_DEST_OSC2, LFO_DEST_OSC12, LFO_DEST_PW, LFO_DEST_FM };
    for (int i = 0; i < 4; i++) {
        v.changeLfoSetting(i & 1, 1, ok[i]);
        CHECK_EQ(v.m_lfoDest[i & 1], ok[i]);
        CHECK_EQ(v.m_bLfoEnable[i & 1], 1);
    }
    const int no[] = { LFO_DEST_FILTER, LFO_DEST_AMP, LFO_DEST_PAN };
    for (int i = 0; i < 3; i++) {
        v.changeLfoSetting(1, 1, no[i]);
        CHECK_EQ(v.m_lfoDest[1], 0);
        CHECK_EQ(v.m_bLfoEnable[1], 0);
    }
    v.changeLfoSetting(0, 0, LFO_DEST_OSC2);
    CHECK_EQ(v.m_lfoDest[0], 0);
    CHECK_EQ(v.m_bLfoEnable[0], 0);
}

/** @brief Phase knob: 0 = free running (new random phases requested), 1..127 = restart with an OSC2 offset. */
TEST(vco, phase_shift_settings)
{
    VcoRig r;
    CVco& v = *r.vco;
    v.m_bInitPhase = 0;
    v.setPhaseShift(0);
    CHECK_EQ(v.m_phaseMode, 0);
    CHECK_FEQ(v.m_phaseShift, 0.f);
    CHECK_EQ(v.m_bInitPhase, 1);
    v.m_bInitPhase = 0;
    v.setPhaseShift(1);
    CHECK_EQ(v.m_phaseMode, 1);
    CHECK_FEQ(v.m_phaseShift, 0.f);
    CHECK_EQ(v.m_bInitPhase, 0);
    v.setPhaseShift(64);
    CHECK_FEQ(v.m_phaseShift, 0.5f);
    v.setPhaseShift(127);
    CHECK_FEQ(v.m_phaseShift, 1.f);

    v.setPhaseMode(1);
    CHECK_EQ(v.m_bInitPhase, 0);
    v.setPhaseMode(0);
    CHECK_EQ(v.m_bInitPhase, 1);
    v.m_bInitPhase = 0;
    v.initFreePhase();
    CHECK_EQ(v.m_bInitPhase, 1);
    v.setPhaseShift(50);
    v.initPhaseParam(3);
    CHECK_EQ(v.m_phaseMode, 0);
    CHECK_FEQ(v.m_phaseShift, 0.f);
    CHECK_EQ(v.m_bInitPhase, 1);
    v.setPhaseShift(50);
    v.m_bInitPhase = 0;
    v.setVoiceNo(2);
    CHECK_EQ(v.m_phaseMode, 0);
    CHECK_EQ(v.m_bInitPhase, 1);

    v.setUnisonInfo(4, 2, 0.75f);
    CHECK_EQ(v.m_unisonNum, 4);
    CHECK_EQ(v.m_unisonNo, 2);
    CHECK_FEQ(v.m_unisonPhaseSpread, 0.75f);
}

/** @brief calcMultiDetunePhaseCoef() reads the shared random table scaled by amount, wrapping at 512. */
TEST(vco, multi_detune_phase_coef)
{
    VcoRig r;
    float c[4];
    r.vco->calcMultiDetunePhaseCoef(0.5f, 510, 4, c);
    CHECK_FEQ(c[0], g_rnd.m_table[510] * 0.5f);
    CHECK_FEQ(c[1], g_rnd.m_table[511] * 0.5f);
    CHECK_FEQ(c[2], g_rnd.m_table[0] * 0.5f);
    CHECK_FEQ(c[3], g_rnd.m_table[1] * 0.5f);
    for (int i = 0; i < RND_TABLE_SIZE; i++) {
        CHECK(g_rnd.m_table[i] >= 0.f && g_rnd.m_table[i] <= 1.f);
    }
}

/** @brief Restart mode: OSC1 at 0, OSC2 at phaseShift * half a cycle, super saw at fixed table offsets. */
TEST(vco, start_phase_restart)
{
    VcoRig r;
    CVco& v = *r.vco;
    v.setPhaseShift(64);
    v.m_phase1 = 123;
    v.startPhase();
    CHECK_EQ(v.m_phase1, 0);
    CHECK_EQ(v.m_phase2, 1 << 25);
    for (int i = 0; i < 8; i++) {
        float coef = g_rnd.m_table[i] * 0.5f;
        CHECK_EQ(v.m_ssPhase[i], ftoi(coef * 134217728.f) & VCO_PHASE_MASK);
    }
    CHECK_EQ(v.m_bInitPhase, 0);

    v.setPhaseShift(1);
    v.startPhase();
    CHECK_EQ(v.m_phase2, 0);
    for (int i = 0; i < 8; i++) CHECK_EQ(v.m_ssPhase[i], 0);

    // repeated note ons restart at the same place
    v.m_phase1 = 999;
    v.m_phase2 = 999;
    v.startPhase();
    CHECK_EQ(v.m_phase1, 0);
    CHECK_EQ(v.m_phase2, 0);
}

/** @brief Restart mode with unison: each voice adds offsets from random table entries 10*no .. 10*no+9 scaled by the spread. */
TEST(vco, start_phase_unison_offsets)
{
    VcoRig r;
    CVco& v = *r.vco;
    v.setPhaseShift(1);
    v.setUnisonInfo(3, 1, 0.25f);
    v.startPhase();
    CHECK_EQ(v.m_phase1, ftoi(g_rnd.m_table[10] * 0.25f * 134217728.f) & VCO_PHASE_MASK);
    CHECK_EQ(v.m_phase2, ftoi(g_rnd.m_table[11] * 0.25f * 134217728.f) & VCO_PHASE_MASK);
    for (int i = 0; i < 8; i++) {
        CHECK_EQ(v.m_ssPhase[i], ftoi(g_rnd.m_table[12 + i] * 0.25f * 134217728.f) & VCO_PHASE_MASK);
    }
    // a single voice or zero spread adds nothing
    v.setUnisonInfo(1, 1, 0.25f);
    v.startPhase();
    CHECK_EQ(v.m_phase1, 0);
    v.setUnisonInfo(3, 1, 0.f);
    v.startPhase();
    CHECK_EQ(v.m_phase1, 0);
}

/** @brief Free running: random phases (from g_rnd.m_rand) only when requested, otherwise the phases continue. */
TEST(vco, start_phase_free_running)
{
    VcoRig r;
    CVco& v = *r.vco;
    v.setPhaseShift(0);
    g_rnd.m_rand.setSeed(777);
    CMyRand ref;
    ref.setSeed(777);
    v.startPhase();
    CHECK_EQ(v.m_bInitPhase, 0);
    unsigned int e1 = ftoi((float)((float)ref.rand() * (1.0 / MYRAND_MAX)) * 134217728.f);
    unsigned int e2 = ftoi((float)((float)ref.rand() * (1.0 / MYRAND_MAX)) * 134217728.f);
    CHECK_EQ(v.m_phase1, e1);
    CHECK_EQ(v.m_phase2, e2);
    for (int i = 0; i < 8; i++) {
        unsigned int e = ftoi((float)((float)ref.rand() * (1.0 / MYRAND_MAX)) * 134217728.f);
        CHECK_EQ(v.m_ssPhase[i], e);
        CHECK(v.m_ssPhase[i] <= (1u << 27));
    }
    // not requested: phases are kept
    v.m_phase1 = 4242;
    v.startPhase();
    CHECK_EQ(v.m_phase1, 4242);
    // noteOn() starts the phases too
    v.initFreePhase();
    v.noteOn();
    CHECK(v.m_phase1 != 4242u);
    CHECK_EQ(v.m_bInitPhase, 0);
}

/** @brief noteOn(): starts the envelope, forces an FM target refresh and applies a pending OSC2 pitch. */
TEST(vco, note_on_state)
{
    VcoRig r;
    CVco& v = *r.vco;
    v.setPhaseShift(1);
    v.m_fmPrevTarget = 5;
    v.m_fmTargetTime = 5;
    v.noteOn();
    CHECK_EQ(v.m_envStatus, ENV_ATTACK);
    CHECK_EQ(v.m_fmPrevTarget, -1);
    CHECK_EQ(v.m_fmTargetTime, 0);
}

/** @brief setOsc2Pitch() while the envelope runs starts a glide from the current pitch instead of jumping. */
TEST(vco, osc2_pitch_glide_setup)
{
    VcoRig r;
    CVco& v = *r.vco;
    v.setPhaseShift(1);
    v.setOsc2Pitch(0.5f);
    v.noteOn();
    CHECK_FEQ(v.m_osc2Pitch, 0.5f);
    v.setOsc2Pitch(2.f);
    CHECK_FEQ(v.m_osc2Pitch, 0.5f);
    CHECK_FEQ(v.m_osc2PitchStart, 0.5f);
    CHECK_FEQ(v.m_osc2PitchTarget, 2.f);
    CHECK_EQ(v.m_osc2PitchPos, 0);
    CHECK_EQ(v.m_bOsc2PitchChanged, 1);
    // the next note on jumps to the target
    v.noteOn();
    CHECK_FEQ(v.m_osc2Pitch, 2.f);
}

/** @brief setNote() with portamento: glide distance = previous pitch - new pitch (1/128 octave, 128*nn/12) + glide in progress. */
TEST(vco, set_note_portamento_distance)
{
    VcoRig r;
    CVco& v = *r.vco;
    v.setPortamentTime(0.1f);
    CHECK_EQ(v.m_portLen, (int)(44100 * 0.1f));
    v.setNote(57, 0);
    CHECK_EQ(v.m_portDiff, 0);
    v.setNote(69, 1);
    CHECK_EQ(v.m_portDiff, 57 * 128 / 12 - 69 * 128 / 12);
    CHECK_EQ(v.m_portCur, v.m_portDiff);
    CHECK_EQ(v.m_portPos, 0);
    CHECK_EQ(v.m_portStep, 1);
    CHECK_EQ(v.m_portRes, 32);  // 4410 / 128 / 10 = 3, at least 32
    CHECK_EQ(v.m_portNextRes, 441);
    // a new glide continues from the current offset
    v.m_portCur = -50;
    v.setNote(60, 1);
    CHECK_EQ(v.m_portDiff, 69 * 128 / 12 - 60 * 128 / 12 - 50);
    // integer division of the pitch: 128 * nn / 12 is truncated per note
    v.setNote(61, 0);
    v.setNote(62, 1);
    CHECK_EQ(v.m_portDiff, 61 * 128 / 12 - 62 * 128 / 12);
    CHECK_EQ(v.m_portDiff, -11);
    // without portamento the glide is cancelled
    v.setNote(40, 0);
    CHECK_EQ(v.m_portDiff, 0);
    CHECK_EQ(v.m_portCur, 0);
    CHECK_EQ(v.m_portPos, 0);
}

/** @brief getNotePitch() maps negative key numbers (after key shift) to pitch 0. */
TEST(vco, note_pitch_clamps_negative_keys)
{
    VcoRig r;
    CVco& v = *r.vco;
    v.setPortamentTime(0.1f);
    v.setKeyShift(-12);
    v.setNote(5, 0);
    v.setNote(10, 1);
    CHECK_EQ(v.m_portDiff, 0);
    v.setNote(20, 1);
    CHECK_EQ(v.m_portDiff, -(8 * 128 / 12));
}

/** @brief setPortamentTime() during a glide rescales the position proportionally. */
TEST(vco, portament_time_rescales_glide)
{
    VcoRig r;
    CVco& v = *r.vco;
    v.setPortamentTime(0.1f);
    int len = v.m_portLen;
    v.setNote(40, 0);
    v.setNote(80, 1);
    v.m_portPos = 1000;
    v.m_portRes = 100;
    v.m_portNextRes = 2000;
    v.setPortamentTime(0.2f);
    int len2 = v.m_portLen;
    CHECK_EQ(len2, (int)(44100 * 0.2f));
    CHECK_EQ(v.m_portPos, (int)((long)len2 * 1000 / len));
    CHECK_EQ(v.m_portRes, (int)((long)100 * len2 / len));
    CHECK_EQ(v.m_portNextRes, (int)((long)2000 * len2 / len));
    // turning portamento off: the position is clamped to the (zero) length
    v.setPortamentTime(0.f);
    CHECK_EQ(v.m_portLen, 0);
    CHECK_EQ(v.m_portPos, 0);
    CHECK_EQ(v.m_portRes, 32);
}
