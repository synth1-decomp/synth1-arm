/**
 * @file test_vcomain.cpp
 * @brief Unit tests for the oscillator render path: CVco::createWaveMain(),
 *        CVco::processEnvelopedSignal() and the calcXXXXXX() render variants of vcoMain.cpp
 *        (calcSignalLoop()).
 *
 * Most tests use OSC1 = 44100/128 Hz, whose 16.16 phase increment is exactly 2^20, so every sample
 * reads a table entry with a zero fraction and the expected output can be computed exactly from the
 * wavetables.
 */
#include "test.h"
#include "engine.h"

#include <new>
#include <stdlib.h>
#include <string.h>
#include <math.h>

#include "common.h"
#include "synth1.h"
#include "vco.h"
#include "vcf.h"

// Non-static helper of vco.cpp (external linkage, not declared in a header).
float* getWaveTablePointerSize(int wave, float freq);

namespace {

const int FS = 44100;
const float F1 = 44100.f / 128.f;   // phase increment exactly 2^20: period 128 samples
const unsigned int D1 = 1u << 20;

/**
 * @brief A CVco wired to a CVcf like CVoice does, constructed in zero-filled memory.
 *
 * Zero-filled memory keeps the members that init() leaves to later calls (FM smoothing state,
 * unison info, portamento resolution, OSC2 glide target) defined. Phases restart at 0 (phase knob 1).
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
        vco->setSamplingFreq(FS);
        vco->setOscModEnvDest(0);
        vco->setVoiceNo(0);
        vco->setUnisonInfo(1, 0, 0.f);
        vco->setPhaseShift(1);
    }

    ~VcoRig()
    {
        vco->~CVco();
        vcf->~CVcf();
        free(vcoMem);
        free(vcfMem);
    }

    /**
     * @brief Sets shapes, mix and frequencies directly and restarts the phases and the noise.
     */
    void setup(int shape1, int shape2, float mix, float f1, float f2)
    {
        vco->setShape(shape1);
        vco->setShape2(shape2);
        vco->setMix(mix);
        vco->m_freq1 = f1;
        vco->m_freq2 = f2;
        vco->m_phase1 = 0;
        vco->m_phase2 = 0;
        vco->m_noise = 10;
    }

    /**
     * @brief Renders size samples with no modulation; returns the count rendered.
     */
    int render(float* buf, int size)
    {
        return vco->createWaveMain(0, 0, 0, 0, buf, size);
    }
};

/**
 * @brief Number of sign changes in buf.
 */
int signChanges(const float* buf, int n)
{
    int c = 0;
    for (int i = 1; i < n; i++) {
        if ((buf[i - 1] < 0) != (buf[i] < 0)) c++;
    }
    return c;
}

/**
 * @brief The same 16.16 phase increment as createWaveMain() for OSC2 (and the super saw voices).
 */
unsigned int dPhaseOf(const CVco& v, float freq)
{
    return ftoi((float)(freq * 134217728.f * v.m_invFs));
}

/**
 * @brief Reference of the M-sequence noise of vcoMain.cpp.
 */
float refNoise(unsigned int* s, float o)
{
    if (*s & 0x80000) {
        *s = (*s << 1) ^ 0x87;
        return o;
    }
    *s = *s << 1;
    return -o;
}

/**
 * @brief The FM depth createWaveMain() derives from an effective amount 0..127.
 */
int fmTargetOf(float amount)
{
    double v = amount / 127.f;
    return (int)((fpow(1000.0, v) - 1.0) / 999.0 * 67108864.0);
}

}  // namespace

/** @brief A sine OSC1 alone reproduces the sine table, starting at phase 0, and stores the final phase. */
TEST(vcomain, sine_output_matches_table)
{
    VcoRig r;
    r.setup(VCO_SINE, VCO_SINE, 0.f, F1, F1);
    float buf[1024];
    CHECK_EQ(r.render(buf, 1024), 1024);
    float* s = getWaveTablePointerSize(VCO_SINE, F1);
    for (int n = 0; n < 1024; n++) {
        CHECK_FEQ(buf[n], s[(n * 16) & VCO_TABLE_MASK]);
    }
    CHECK_EQ(r.vco->m_phase1, 1024u * D1);
    CHECK_EQ(r.vco->m_phase2, 1024u * D1);
    // the next block continues the phase
    r.render(buf, 1);
    CHECK_FEQ(buf[0], s[(1024 * 16) & VCO_TABLE_MASK]);
}

/** @brief A non-power-of-two frequency still produces a sine with the requested frequency (interpolated). */
TEST(vcomain, sine_interpolated_frequency)
{
    VcoRig r;
    r.setup(VCO_SINE, VCO_SINE, 0.f, 441.f, 441.f);
    static float buf[4410];
    r.render(buf, 4410);
    for (int n = 0; n < 4410; n += 3) {
        CHECK_CLOSE(buf[n], sin(2.0 * M_PI * 441.0 * n / FS), 2e-4);
    }
}

/** @brief Every OSC1 waveform repeats exactly with the oscillator period (128 and 32 samples). */
TEST(vcomain, waveforms_are_periodic)
{
    const int shapes[] = { VCO_SINE, VCO_SAW, VCO_PULSE, VCO_TRIANGLE, VCO_SQUARE };
    const int mult[] = { 1, 4 };
    for (int s = 0; s < 5; s++) {
        for (int m = 0; m < 2; m++) {
            VcoRig r;
            r.setup(shapes[s], VCO_SINE, 0.f, F1 * mult[m], F1);
            float buf[1024];
            r.render(buf, 1024);
            int period = 128 / mult[m];
            int bad = 0;
            for (int n = 0; n + period < 1024; n++) {
                if (buf[n + period] != buf[n]) bad++;
            }
            CHECK_EQ(bad, 0);
            // and is not trivially constant
            float mn = buf[0], mx = buf[0];
            for (int n = 0; n < period; n++) {
                if (buf[n] < mn) mn = buf[n];
                if (buf[n] > mx) mx = buf[n];
            }
            CHECK(mx - mn > 0.9f);
        }
    }
}

/** @brief Every OSC1 waveform has no DC and stays within its expected range (pulse is halved). */
TEST(vcomain, waveforms_range_and_dc)
{
    const int shapes[] = { VCO_SINE, VCO_SAW, VCO_PULSE, VCO_TRIANGLE, VCO_SQUARE };
    const float lo[] = { 0.99f, 0.9f, 0.45f, 0.98f, 0.45f };  // peak at least
    const float hi[] = { 1.0f, 1.2f, 0.6f, 1.0f, 0.6f };     // peak at most
    for (int s = 0; s < 5; s++) {
        VcoRig r;
        r.setup(shapes[s], VCO_SINE, 0.f, F1, F1);
        float buf[1024];
        r.render(buf, 1024);
        double sum = 0, mx = 0;
        for (int n = 0; n < 1024; n++) {
            sum += buf[n];
            if (fabs(buf[n]) > mx) mx = fabs(buf[n]);
        }
        CHECK_CLOSE(sum / 1024, 0.0, 1e-4);
        CHECK(mx >= lo[s]);
        CHECK(mx <= hi[s] + 1e-6);
    }
}

/** @brief The output frequency follows m_freq1: two zero crossings per cycle at 1000 Hz and 3000 Hz. */
TEST(vcomain, waveforms_zero_crossings_match_frequency)
{
    const int shapes[] = { VCO_SINE, VCO_SAW, VCO_PULSE, VCO_TRIANGLE, VCO_SQUARE };
    const float freqs[] = { 1000.f, 3000.f };
    static float buf[FS];
    for (int s = 0; s < 5; s++) {
        for (int f = 0; f < 2; f++) {
            VcoRig r;
            r.setup(shapes[s], VCO_SINE, 0.f, freqs[f], 100.f);
            r.vco->m_phase1 = 1 << 20;  // avoid starting exactly on a zero
            r.render(buf, FS);
            CHECK_CLOSE(signChanges(buf, FS), 2.0 * freqs[f], 4.0);
        }
    }
}

/** @brief Pulse: output = (saw[i] - saw[i + offset]) / 2 with offset = clamp(pw + mod/128 * range, 0.01, 1.99) * 1024. */
TEST(vcomain, pulse_width_offsets)
{
    const float pw[] = { 1.f, 0.5f, 0.5f, 0.5f, 0.5f, 0.5f, 0.2f };
    const int mod[] = { 0, 0, 64, -64, 128, -128, 32 };
    for (int c = 0; c < 7; c++) {
        // range: 2 - pw upwards, pw downwards
        float range = mod[c] > 0 ? 2.f - pw[c] : pw[c];
        double w = pw[c] + mod[c] * (1.f / 128.f) * range;
        int off = (int)(MIN(1.99, MAX(0.01, w)) * 1024.0);
        VcoRig r;
        r.setup(VCO_PULSE, VCO_SINE, 0.f, F1, F1);
        r.vco->setPW(pw[c]);
        float buf[256];
        r.vco->createWaveMain(0, 0, mod[c], 0, buf, 256);
        float* saw = getWaveTablePointerSize(VCO_SAW, F1);
        for (int n = 0; n < 256; n++) {
            int i = (n * 16) & VCO_TABLE_MASK;
            CHECK_FEQ(buf[n], (saw[i] - saw[(i + off) & VCO_TABLE_MASK]) * 0.5f);
        }
    }
    // the documented offsets for pw 0.5
    CHECK_EQ((int)(MIN(1.99, MAX(0.01, 0.5 + 128 * (1.0 / 128) * 1.5)) * 1024.0), 2037);
    CHECK_EQ((int)(MIN(1.99, MAX(0.01, 0.5 - 128 * (1.0 / 128) * 0.5)) * 1024.0), 10);
}

/** @brief Pulse duty cycle: pw 1 gives 50 %, pw 0.5 gives 75 % high (levels 0.25 / -0.75). */
TEST(vcomain, pulse_duty_cycle)
{
    const float pw[] = { 1.f, 0.5f };
    const float mid[] = { 0.f, -0.25f };
    const int high[] = { 64, 96 };
    for (int c = 0; c < 2; c++) {
        VcoRig r;
        r.setup(VCO_PULSE, VCO_SINE, 0.f, F1, F1);
        r.vco->setPW(pw[c]);
        float buf[128];
        r.render(buf, 128);
        int n = 0;
        double sum = 0;
        for (int i = 0; i < 128; i++) {
            if (buf[i] > mid[c]) n++;
            sum += buf[i];
        }
        CHECK_CLOSE(n, high[c], 3);
        CHECK_CLOSE(sum / 128, 0.0, 1e-4);
    }
}

/** @brief OSC2 shape does not leak into the output at mix 0, nor OSC1 shape at mix 1 (no ring/FM/sync). */
TEST(vcomain, mix_isolates_oscillators)
{
    const int shapes[] = { VCO_SINE, VCO_SAW, VCO_PULSE, VCO_TRIANGLE, VCO_NOISE, VCO_SQUARE };
    float ref[300], buf[300];
    for (int s1 = 0; s1 < 6; s1++) {
        if (shapes[s1] == VCO_NOISE) continue;   // OSC1 has no noise
        VcoRig r;
        r.setup(shapes[s1], VCO_SINE, 0.f, 500.f, 777.f);
        r.render(ref, 300);
        for (int s2 = 0; s2 < 6; s2++) {
            r.setup(shapes[s1], shapes[s2], 0.f, 500.f, 777.f);
            r.render(buf, 300);
            int bad = 0;
            for (int n = 0; n < 300; n++) if (buf[n] != ref[n]) bad++;
            CHECK_EQ(bad, 0);
        }
    }
    for (int s2 = 0; s2 < 6; s2++) {
        VcoRig r;
        r.setup(VCO_SINE, shapes[s2], 1.f, 500.f, 777.f);
        r.render(ref, 300);
        for (int s1 = 0; s1 < 6; s1++) {
            if (shapes[s1] == VCO_NOISE) continue;
            r.setup(shapes[s1], shapes[s2], 1.f, 500.f, 777.f);
            r.render(buf, 300);
            int bad = 0;
            for (int n = 0; n < 300; n++) if (buf[n] != ref[n]) bad++;
            CHECK_EQ(bad, 0);
        }
    }
}

/** @brief Mix 0.5 sums both oscillators with half gain each. */
TEST(vcomain, mix_sums_oscillators)
{
    VcoRig r;
    r.setup(VCO_SINE, VCO_SINE, 0.5f, F1, 2 * F1);
    float buf[256];
    r.render(buf, 256);
    float* s = getWaveTablePointerSize(VCO_SINE, F1);
    for (int n = 0; n < 256; n++) {
        CHECK_CLOSE(buf[n], 0.5f * s[(n * 16) & VCO_TABLE_MASK] + 0.5f * s[(n * 32) & VCO_TABLE_MASK], 1e-7);
    }
    CHECK_EQ(r.vco->m_phase2, 256u * 2 * D1);
}

/** @brief Noise: M-sequence from state 10, +-0.65 (gain 0.65 without FM), deterministic, roughly balanced. */
TEST(vcomain, noise_sequence)
{
    VcoRig r;
    r.setup(VCO_SINE, VCO_NOISE, 1.f, F1, F1);
    static float buf[4096];
    r.render(buf, 4096);
    unsigned int s = 10;
    int pos = 0;
    for (int n = 0; n < 4096; n++) {
        float e = refNoise(&s, 0.65f);
        CHECK_FEQ(buf[n], e);
        if (buf[n] > 0) pos++;
    }
    CHECK_EQ(r.vco->m_noise, s);
    for (int n = 0; n < 16; n++) CHECK_FEQ(buf[n], -0.65f);
    CHECK_FEQ(buf[16], 0.65f);
    CHECK(pos > 4096 / 5);
    CHECK(pos < 4096 * 4 / 5);
    // the OSC2 phase does not move for noise
    CHECK_EQ(r.vco->m_phase2, 0u);
}

/** @brief Sub oscillator at the same octave adds the sub wave on OSC1's phase; gains are normalized by (sub + 1). */
TEST(vcomain, sub_oscillator_same_octave)
{
    VcoRig r;
    r.setup(VCO_SINE, VCO_SINE, 0.f, F1, F1);
    r.vco->setSubOscGain(1.f);
    r.vco->setSubWaveForm(0);   // sine
    r.vco->setSubOct(0);
    float buf[256];
    r.render(buf, 256);
    float* s = getWaveTablePointerSize(VCO_SINE, F1);
    for (int n = 0; n < 256; n++) {
        CHECK_CLOSE(buf[n], s[(n * 16) & VCO_TABLE_MASK], 1e-7);
    }
}

/** @brief Sub oscillator one octave down: half frequency, period 256 samples. */
TEST(vcomain, sub_oscillator_octave_down)
{
    VcoRig r;
    r.setup(VCO_SINE, VCO_SINE, 0.f, F1, F1);
    r.vco->setSubOscGain(1.f);
    r.vco->setSubWaveForm(0);
    r.vco->setSubOct(1);
    float buf[768];
    r.render(buf, 768);
    float* s = getWaveTablePointerSize(VCO_SINE, F1);
    for (int n = 0; n < 768; n++) {
        CHECK_CLOSE(buf[n], 0.5f * s[(n * 16) & VCO_TABLE_MASK] + 0.5f * s[(n * 8) & VCO_TABLE_MASK], 1e-7);
    }
    int bad128 = 0, bad256 = 0;
    for (int n = 0; n + 256 < 768; n++) {
        if (buf[n + 128] != buf[n]) bad128++;
        if (buf[n + 256] != buf[n]) bad256++;
    }
    CHECK(bad128 > 0);
    CHECK_EQ(bad256, 0);
}

/** @brief Sub oscillator waveforms (triangle, saw, square) one octave down: period 256, no DC, bounded. */
TEST(vcomain, sub_oscillator_waveforms)
{
    for (int wf = 1; wf < 4; wf++) {
        VcoRig r;
        r.setup(VCO_SINE, VCO_SINE, 0.f, F1, F1);
        r.vco->setSubOscGain(2.f);
        r.vco->setSubWaveForm(wf);
        r.vco->setSubOct(1);
        float buf[768];
        r.render(buf, 768);
        double sum = 0, mx = 0;
        int bad = 0;
        for (int n = 0; n < 512; n++) {
            sum += buf[n];
            if (fabs(buf[n]) > mx) mx = fabs(buf[n]);
            if (buf[n + 256] != buf[n]) bad++;
        }
        CHECK_EQ(bad, 0);
        CHECK_CLOSE(sum / 512, 0.0, 1e-4);
        CHECK(mx < 1.2);
        CHECK(mx > 0.5);
    }
}

/** @brief Ring modulation: OSC2 is multiplied by OSC1 (OSC1 is also mixed by its own gain). */
TEST(vcomain, ring_modulation)
{
    VcoRig r;
    r.setup(VCO_SINE, VCO_SINE, 1.f, F1, 2 * F1);
    r.vco->setRing(1);
    float buf[256];
    r.render(buf, 256);
    float* s = getWaveTablePointerSize(VCO_SINE, F1);
    for (int n = 0; n < 256; n++) {
        CHECK_CLOSE(buf[n], s[(n * 32) & VCO_TABLE_MASK] * s[(n * 16) & VCO_TABLE_MASK], 1e-7);
    }
    r.setup(VCO_SINE, VCO_SINE, 0.5f, F1, 2 * F1);
    r.render(buf, 256);
    for (int n = 0; n < 256; n++) {
        float a = s[(n * 16) & VCO_TABLE_MASK], b = s[(n * 32) & VCO_TABLE_MASK];
        CHECK_CLOSE(buf[n], 0.5f * a + 0.5f * b * a, 1e-7);
    }
}

/**
 * @brief Sync: OSC2 restarts at every OSC1 wrap, so the output repeats with OSC1's period.
 *
 * The restart phase is (OSC1 phase before the wrap) * dPhase2 / dPhase1 computed in 32-bit
 * unsigned arithmetic (the product wraps), which is pinned here.
 */
TEST(vcomain, oscillator_sync)
{
    VcoRig r;
    r.setup(VCO_SINE, VCO_SAW, 1.f, F1, 441.f);
    r.vco->setSync(1);
    float buf[640];
    r.render(buf, 128);
    unsigned int d2 = dPhaseOf(*r.vco, 441.f);
    unsigned int expect = ((127u << 20) * d2) / D1;
    CHECK_EQ(r.vco->m_phase2, expect);
    r.render(buf + 128, 512);
    int bad = 0;
    for (int n = 128; n + 128 < 640; n++) {
        if (buf[n + 128] != buf[n]) bad++;
    }
    CHECK_EQ(bad, 0);

    // without sync the output does not repeat with OSC1's period
    r.setup(VCO_SINE, VCO_SAW, 1.f, F1, 441.f);
    r.vco->setSync(0);
    r.render(buf, 640);
    bad = 0;
    for (int n = 128; n + 128 < 640; n++) {
        if (buf[n + 128] != buf[n]) bad++;
    }
    CHECK(bad > 100);
    CHECK_EQ(r.vco->m_phase2, 640u * d2);
}

/** @brief Sync with OSC2 noise: the M-sequence restarts from state 10 at every OSC1 wrap. */
TEST(vcomain, oscillator_sync_noise)
{
    VcoRig r;
    r.setup(VCO_SINE, VCO_NOISE, 1.f, F1, F1);
    r.vco->setSync(1);
    float buf[512];
    r.render(buf, 128);
    CHECK_EQ(r.vco->m_noise, 10u);
    r.render(buf + 128, 384);
    int bad = 0;
    for (int n = 0; n + 128 < 512; n++) {
        if (buf[n + 128] != buf[n]) bad++;
    }
    CHECK_EQ(bad, 0);
}

/** @brief FM: OSC1 phase advances by dPhase1 + ftoi(fmAmount * OSC2 sample); fmAmount follows the documented curve. */
TEST(vcomain, fm_phase_advance)
{
    VcoRig r;
    CVco& v = *r.vco;
    r.setup(VCO_SINE, VCO_SINE, 0.5f, F1, 2 * F1);
    v.setFMAmount(64);
    float buf[256];
    CHECK_EQ(r.render(buf, 256), 256);
    int target = fmTargetOf(64.f);
    CHECK_EQ(v.m_fmTarget, target);
    CHECK_EQ(v.m_fmAmount, target);   // first change jumps (m_bFMReset)
    CHECK_EQ(v.m_bFMReset, 0);
    float* s = getWaveTablePointerSize(VCO_SINE, F1);
    unsigned int ph = 0;
    for (int n = 0; n < 256; n++) {
        ph += D1 + ftoi((float)target * s[(n * 32) & VCO_TABLE_MASK]);
    }
    CHECK_EQ(v.m_phase1, ph);
    CHECK_EQ(v.m_phase2, 256u * 2 * D1);
    for (int n = 0; n < 256; n++) {
        CHECK(fabs(buf[n]) <= 1.0f + 1e-6);
    }

    // the same settings without FM advance OSC1 linearly and give a different signal
    float ref[256];
    VcoRig r2;
    r2.setup(VCO_SINE, VCO_SINE, 0.5f, F1, 2 * F1);
    r2.render(ref, 256);
    CHECK_EQ(r2.vco->m_phase1, 256u * D1);
    int diff = 0;
    for (int n = 0; n < 256; n++) if (buf[n] != ref[n]) diff++;
    CHECK(diff > 100);
}

/** @brief FM amount curve: (1000^(v/127) - 1) / 999 * 2^26; the FM modulation input pushes it towards 127 or 0. */
TEST(vcomain, fm_amount_curve_and_modulation)
{
    float buf[64];
    {
        VcoRig r;
        r.setup(VCO_SINE, VCO_SINE, 0.5f, F1, F1);
        r.vco->setFMAmount(127);
        r.render(buf, 64);
        CHECK_EQ(r.vco->m_fmTarget, fmTargetOf(127.f));
        CHECK_CLOSE(r.vco->m_fmTarget, 67108864.0, 1.0);   // full depth = 2^26
        r.vco->setFMAmount(0);
        r.vco->resetFMSmooth();
        r.render(buf, 64);
        CHECK_EQ(r.vco->m_fmTarget, 0);
        CHECK_EQ(r.vco->m_fmAmount, 0);
    }
    {
        // envelope destination FM: +512 = full depth from dial 0
        VcoRig r;
        r.setup(VCO_SINE, VCO_SINE, 0.5f, F1, F1);
        r.vco->setOscModEnvDest(1);
        r.vco->processEnvelopedSignal(512, buf, 64, 0, 0);
        CHECK_EQ(r.vco->m_fmTarget, fmTargetOf(127.f));
        // half depth: amount 63.5
        r.vco->setFMAmount(0);
        r.vco->processEnvelopedSignal(256, buf, 64, 0, 0);
        CHECK_EQ(r.vco->m_fmTarget, fmTargetOf(63.5f));
    }
    {
        // negative modulation scales the dial down: -512 = 0
        VcoRig r;
        r.setup(VCO_SINE, VCO_SINE, 0.5f, F1, F1);
        r.vco->setFMAmount(64);
        r.vco->createWaveMain(0, 0, 0, -512, buf, 64);
        CHECK_EQ(r.vco->m_fmTarget, 0);
        r.vco->setFMAmount(64);
        r.vco->createWaveMain(0, 0, 0, -256, buf, 64);
        CHECK_EQ(r.vco->m_fmTarget, fmTargetOf(32.f));
        // LFO destination FM
        r.vco->changeLfoSetting(0, 1, LFO_DEST_FM);
        r.vco->setFMAmount(0);
        r.vco->processEnvelopedSignal(0, buf, 64, 512, 0);
        CHECK_EQ(r.vco->m_fmTarget, fmTargetOf(127.f));
    }
}

/** @brief A dial change is smoothed over 100 ms with the raised cosine (updated every 32 samples); blocks are cut at its end. */
TEST(vcomain, fm_amount_smoothing)
{
    VcoRig r;
    CVco& v = *r.vco;
    r.setup(VCO_SINE, VCO_SINE, 0.5f, F1, F1);
    v.setFMAmount(64);
    static float buf[8192];
    r.render(buf, 64);
    int start = v.m_fmAmount;
    int target = fmTargetOf(100.f);

    v.setFMAmount(100);
    CHECK_EQ(r.render(buf, 1000), 1000);
    CHECK_EQ(v.m_fmTarget, target);
    CHECK_EQ(v.m_fmSmoothLen, FS / 10);
    CHECK_EQ(v.m_fmStart, start);
    CHECK_EQ(v.m_fmSmoothCount, FS / 10 - 1000);
    // last update at count 3424 (the last multiple of 32 seen)
    int expect = (int)((float)(target - start) * g_smoothCosTable[(3424 << 8) / (FS / 10)]) + start;
    CHECK_EQ(v.m_fmAmount, expect);
    CHECK(v.m_fmAmount > start);
    CHECK(v.m_fmAmount < target);

    // the next block stops where the smoothing ends
    CHECK_EQ(r.render(buf, 8192), FS / 10 - 1000);
    CHECK_EQ(v.m_fmSmoothCount, 0);
    CHECK_EQ(v.m_fmAmount, target);
    CHECK_EQ(r.render(buf, 8192), 8192);
    CHECK_EQ(v.m_fmAmount, target);
}

/** @brief resetFMSmooth() makes the next dial change jump without smoothing. */
TEST(vcomain, fm_reset_jumps)
{
    VcoRig r;
    CVco& v = *r.vco;
    r.setup(VCO_SINE, VCO_SINE, 0.5f, F1, F1);
    v.setFMAmount(64);
    float buf[1000];
    r.render(buf, 64);
    v.resetFMSmooth();
    v.setFMAmount(100);
    CHECK_EQ(r.render(buf, 1000), 1000);
    CHECK_EQ(v.m_fmAmount, v.m_fmTarget);
    CHECK_EQ(v.m_fmSmoothLen, 0);
}

/** @brief Super saw: OSC1 + 8 detuned voices summed and scaled by 0.3; each voice keeps its own phase. */
TEST(vcomain, super_saw_sum_and_phases)
{
    VcoRig r;
    CVco& v = *r.vco;
    r.setup(VCO_SINE, VCO_SINE, 0.f, F1, F1);
    v.setSSDetune(10.f);
    unsigned int p0 = 512u << 16;   // sine peak
    v.m_phase1 = p0;
    for (int i = 0; i < 8; i++) v.m_ssPhase[i] = p0;
    float buf[1];
    r.render(buf, 1);
    CHECK_CLOSE(buf[0], 9.f * 0.3f, 1e-6);
    double k = v.m_invFs * 134217728.0;
    for (int i = 0; i < 8; i++) {
        unsigned int d = ftoi((float)(v.calcFreqMod(v.m_ssFreq[i], 0) * k));
        CHECK_EQ(v.m_ssPhase[i], p0 + d);
    }
    CHECK(v.m_ssPhase[0] > v.m_phase1);   // +10 cent voice runs faster
    CHECK(v.m_ssPhase[1] < v.m_phase1);   // -10 cent voice runs slower
    CHECK_EQ(v.m_phase1, p0 + D1);
}

/** @brief Super saw saw output: bounded by 9 * 0.3 * peak, no longer periodic at the OSC1 period (beating). */
TEST(vcomain, super_saw_detuned_output)
{
    VcoRig r;
    CVco& v = *r.vco;
    r.setup(VCO_SAW, VCO_SINE, 0.f, F1, F1);
    v.setSSDetune(20.f);
    v.setPhaseShift(64);
    v.startPhase();
    static float buf[4096];
    r.render(buf, 4096);
    double mx = 0;
    int bad = 0;
    for (int n = 0; n < 4096; n++) {
        if (fabs(buf[n]) > mx) mx = fabs(buf[n]);
        if (n + 128 < 4096 && buf[n + 128] != buf[n]) bad++;
    }
    CHECK(mx < 9 * 0.3 * 1.2);
    CHECK(mx > 0.5);
    CHECK(bad > 1000);
    // the 8 voices advanced at their own rates
    for (int i = 0; i < 8; i++) {
        float coef = g_rnd.m_table[i] * 0.5f;
        unsigned int start = ftoi(coef * 134217728.f) & VCO_PHASE_MASK;
        unsigned int d = ftoi((float)(v.calcFreqMod(v.m_ssFreq[i], 0) * (v.m_invFs * 134217728.0)));
        CHECK_EQ(v.m_ssPhase[i], start + 4096u * d);
    }
}

/** @brief Pitch bend (m_pitchEnvLevel) moves OSC1, and OSC2 only with key tracking. */
TEST(vcomain, pitch_env_level_modulates)
{
    VcoRig r;
    CVco& v = *r.vco;
    r.setup(VCO_SINE, VCO_SINE, 0.5f, F1, F1);
    v.m_pitchEnvLevel = 128;
    float buf[256];
    CHECK_EQ(v.processEnvelopedSignal(0, buf, 256, 0, 0), 0);
    CHECK_EQ(v.m_phase1, 256u * 2 * D1);
    CHECK_EQ(v.m_phase2, 256u * 2 * D1);
    float* s = getWaveTablePointerSize(VCO_SINE, F1);
    for (int n = 0; n < 256; n++) {
        CHECK_CLOSE(buf[n], s[(n * 32) & VCO_TABLE_MASK], 1e-7);
    }
    r.setup(VCO_SINE, VCO_SINE, 0.5f, F1, F1);
    v.m_bOsc2KbdTrack = 0;
    v.m_pitchEnvLevel = -128;
    v.processEnvelopedSignal(0, buf, 256, 0, 0);
    CHECK_EQ(v.m_phase1, 256u * D1 / 2);
    CHECK_EQ(v.m_phase2, 256u * D1);
}

/** @brief LFO routing in processEnvelopedSignal(): OSC2 dest moves OSC2 only, OSC1+2 both, PW only the pulse. */
TEST(vcomain, lfo_routing)
{
    VcoRig r;
    CVco& v = *r.vco;
    float buf[256];

    r.setup(VCO_SINE, VCO_SINE, 0.5f, F1, F1);
    v.changeLfoSetting(0, 1, LFO_DEST_OSC2);
    v.processEnvelopedSignal(0, buf, 256, 128, 0);
    CHECK_EQ(v.m_phase1, 256u * D1);
    CHECK_EQ(v.m_phase2, 256u * 2 * D1);

    r.setup(VCO_SINE, VCO_SINE, 0.5f, F1, F1);
    v.changeLfoSetting(0, 0, 0);
    v.changeLfoSetting(1, 1, LFO_DEST_OSC12);
    v.processEnvelopedSignal(0, buf, 256, 0, -128);
    CHECK_EQ(v.m_phase1, 256u * D1 / 2);
    CHECK_EQ(v.m_phase2, 256u * D1 / 2);

    // LFO value ignored when the destination is not an oscillator one
    r.setup(VCO_SINE, VCO_SINE, 0.5f, F1, F1);
    v.changeLfoSetting(1, 1, LFO_DEST_FILTER);
    v.processEnvelopedSignal(0, buf, 256, 0, 128);
    CHECK_EQ(v.m_phase1, 256u * D1);
    CHECK_EQ(v.m_phase2, 256u * D1);

    // pulse width: LFO +128 with pw 0.5 -> offset 2037
    r.setup(VCO_PULSE, VCO_SINE, 0.f, F1, F1);
    v.setPW(0.5f);
    v.changeLfoSetting(0, 1, LFO_DEST_PW);
    v.processEnvelopedSignal(0, buf, 128, 128, 0);
    CHECK_EQ(v.m_phase1, 128u * D1);
    float* saw = getWaveTablePointerSize(VCO_SAW, F1);
    for (int n = 0; n < 128; n++) {
        int i = (n * 16) & VCO_TABLE_MASK;
        CHECK_FEQ(buf[n], (saw[i] - saw[(i + 2037) & VCO_TABLE_MASK]) * 0.5f);
    }
}

/** @brief Modulation envelope level goes to OSC2 pitch (dest 0) or pulse width (dest 2). */
TEST(vcomain, mod_envelope_routing)
{
    VcoRig r;
    CVco& v = *r.vco;
    float buf[128];
    r.setup(VCO_SINE, VCO_SINE, 0.5f, F1, F1);
    v.setOscModEnvDest(0);
    v.processEnvelopedSignal(256, buf, 128, 0, 0);
    CHECK_EQ(v.m_phase1, 128u * D1);
    CHECK_EQ(v.m_phase2, 128u * 4 * D1);

    r.setup(VCO_PULSE, VCO_SINE, 0.f, F1, F1);
    v.setPW(0.5f);
    v.setOscModEnvDest(2);
    v.processEnvelopedSignal(-64, buf, 128, 0, 0);   // 0.5 - 0.25 -> offset 256
    CHECK_EQ(v.m_phase2, 128u * D1);
    float* saw = getWaveTablePointerSize(VCO_SAW, F1);
    for (int n = 0; n < 128; n++) {
        int i = (n * 16) & VCO_TABLE_MASK;
        CHECK_FEQ(buf[n], (saw[i] - saw[(i + 256) & VCO_TABLE_MASK]) * 0.5f);
    }
}

/** @brief Portamento: starts at the previous pitch, glides with a decreasing offset and ends exactly at the note. */
TEST(vcomain, portamento_glide)
{
    VcoRig r;
    CVco& v = *r.vco;
    r.setup(VCO_SINE, VCO_SINE, 0.5f, F1, F1);
    v.setPortamentTime(0.1f);
    v.setNote(57, 0);
    v.setNote(69, 1);
    v.m_phase1 = 0;
    float buf[256];
    // first step (32 samples): one octave below the new note
    v.processEnvelopedSignal(0, buf, 32, 0, 0);
    float f = v.calcFreqMod(v.m_freq1, -128);
    CHECK_CLOSE(f, 220.0, 1e-3);
    CHECK_EQ(v.m_phase1, 32u * (unsigned int)(int)(float)(f * 134217728.f * v.m_invFs));
    CHECK_EQ(v.m_portCur, -128);
    CHECK_EQ(v.m_portPos, 32);

    int prev = v.m_portCur;
    int total = 32;
    while (total < 6000) {
        v.processEnvelopedSignal(0, buf, 256, 0, 0);
        total += 256;
        CHECK(v.m_portCur <= 0);
        CHECK(v.m_portCur >= prev);
        prev = v.m_portCur;
    }
    CHECK(v.m_portPos >= v.m_portLen);
    CHECK_EQ(v.m_portCur, 0);
    // after the glide OSC1 runs at the note frequency
    unsigned int p = v.m_phase1;
    v.processEnvelopedSignal(0, buf, 256, 0, 0);
    CHECK_EQ(v.m_phase1 - p, 256u * (unsigned int)(int)(float)(v.m_freq1 * 134217728.f * v.m_invFs));
}

/**
 * @brief OSC2 pitch glide over 50 ms while the envelope runs.
 *
 * The pitch reaches the target exactly, but m_freq2 keeps the last interpolated value (the final
 * step does not call calcOsc2Freq()) until the next note on.
 */
TEST(vcomain, osc2_pitch_glide)
{
    VcoRig r;
    CVco& v = *r.vco;
    r.setup(VCO_SINE, VCO_SINE, 0.5f, F1, F1);
    v.setNote(69, 0);
    v.noteOn();
    v.setOsc2Pitch(1.f);
    float buf[256];
    for (int i = 0; i < 4; i++) v.processEnvelopedSignal(0, buf, 256, 0, 0);
    CHECK(v.m_osc2Pitch > 0.4f);
    CHECK(v.m_osc2Pitch < 0.5f);
    CHECK(v.m_freq2 > v.m_freq1 * 1.3f);
    CHECK(v.m_freq2 < v.m_freq1 * 1.42f);
    for (int i = 0; i < 16; i++) v.processEnvelopedSignal(0, buf, 256, 0, 0);
    CHECK_FEQ(v.m_osc2Pitch, 1.f);
    CHECK(v.m_osc2PitchPos >= FS / 20);
    CHECK(v.m_freq2 < v.m_freq1 * 2.f);
    CHECK(v.m_freq2 > v.m_freq1 * 1.985f);
    v.noteOn();
    CHECK_CLOSE(v.m_freq2, v.m_freq1 * 2.0, 1e-3);
}

/** @brief All 72 render variants (sync x FM x ring x detune x OSC1 P/S x OSC2 P/N/S) render finite, bounded, non-silent blocks. */
TEST(vcomain, all_render_variants)
{
    const int shapes1[] = { VCO_SINE, VCO_SAW, VCO_PULSE, VCO_TRIANGLE, VCO_SQUARE };
    const int shapes2[] = { VCO_SINE, VCO_SAW, VCO_PULSE, VCO_TRIANGLE, VCO_NOISE, VCO_SQUARE };
    static float buf[512];
    int runs = 0;
    for (int mode = 0; mode < 16; mode++) {
        int bSync = mode & 1, bFM = (mode >> 1) & 1, bRing = (mode >> 2) & 1, bDet = (mode >> 3) & 1;
        for (int s1 = 0; s1 < 5; s1++) {
            for (int s2 = 0; s2 < 6; s2++) {
                VcoRig r;
                CVco& v = *r.vco;
                r.setup(shapes1[s1], shapes2[s2], 0.5f, 523.f, 811.f);
                v.m_phase1 = 12345;
                v.m_phase2 = 678910;
                v.setSync(bSync);
                v.setRing(bRing);
                v.setSubOscGain(0.5f);
                v.setPW(0.7f);
                v.setSSDetune(bDet ? 15.f : 0.f);
                v.setFMAmount(bFM ? 40 : 0);
                v.resetFMSmooth();
                CHECK_EQ(v.createWaveMain(0, 0, 20, 0, buf, 512), 512);
                int bad = 0;
                double mx = 0;
                for (int n = 0; n < 512; n++) {
                    if (!(buf[n] == buf[n]) || fabs(buf[n]) > 8.0) bad++;
                    if (fabs(buf[n]) > mx) mx = fabs(buf[n]);
                }
                CHECK_EQ(bad, 0);
                CHECK(mx > 0.05);
                CHECK(v.m_phase1 != 12345u);
                if (bFM && !bRing) CHECK(v.m_fmAmount > 0);
                runs++;
            }
        }
    }
    CHECK_EQ(runs, 480);
}

/** @brief Calling a render variant directly: renders p->size samples and writes the phases back through the pointers. */
TEST(vcomain, direct_render_variant)
{
    VcoRig r;
    float* s = getWaveTablePointerSize(VCO_SINE, F1);
    CalcSigParams p;
    memset(&p, 0, sizeof(p));
    float buf[100];
    unsigned int ph1 = 0xdead, ph2 = 0xbeef, ss[8];
    unsigned int noise = 10;
    int fmA = 0, fmC = 0;
    p.size = 100;
    p.pBuf = buf;
    p.pTable1 = s;
    p.pTable2 = s;
    p.pSubTable = s;
    p.dPhase1 = D1;
    p.dPhase2 = 2 * D1;
    p.gain1 = 1.f;
    p.gain2 = 0.5f;
    p.pNoise = &noise;
    p.pPhase1 = &ph1;
    p.pPhase2 = &ph2;
    p.pSSPhase = ss;
    p.pFmAmount = &fmA;
    p.pFmSmoothCount = &fmC;
    calc____SS(&p);
    for (int n = 0; n < 100; n++) {
        CHECK_CLOSE(buf[n], s[(n * 16) & VCO_TABLE_MASK] + 0.5f * s[(n * 32) & VCO_TABLE_MASK], 1e-7);
    }
    CHECK_EQ(ph1, 100u * D1);
    CHECK_EQ(ph2, 200u * D1);
    CHECK_EQ(p.phase1, 0u);   // the parameter block itself is not modified

    // sub oscillator one octave down through the same variant
    ph1 = ph2 = 0;
    p.subGain = 0.25f;
    p.subOct = 1;
    calc____SS(&p);
    for (int n = 0; n < 100; n++) {
        CHECK_CLOSE(buf[n], s[(n * 16) & VCO_TABLE_MASK] + 0.5f * s[(n * 32) & VCO_TABLE_MASK]
                            + 0.25f * s[(n * 8) & VCO_TABLE_MASK], 1e-6);
    }
}

/** @brief Super saw with a same-octave sub oscillator: the sub sums its table at OSC1 and the 8 voice phases. */
TEST(vcomain, super_saw_with_same_octave_sub)
{
    VcoRig r;
    CVco& v = *r.vco;
    r.setup(VCO_SINE, VCO_SINE, 0.f, F1, F1);
    v.setSubOscGain(1.f);
    v.setSubWaveForm(0);    // sine
    v.setSubOct(0);
    v.setSSDetune(10.f);
    unsigned int p0 = 512u << 16;   // sine peak
    v.m_phase1 = p0;
    for (int i = 0; i < 8; i++) v.m_ssPhase[i] = p0;
    float buf[1];
    r.render(buf, 1);
    float* s = getWaveTablePointerSize(VCO_SINE, F1);
    // gain1 = 1 / (sub + 1) * 0.3 for OSC1 and the sub: 9 * 0.15 * s[512] each
    CHECK_CLOSE(buf[0], 2.0 * 9.0 * 0.15 * s[512], 1e-5);

    // later samples: the sub follows the same nine phases as OSC1, so with equal OSC1 and sub
    // tables the output is twice the OSC1-only output
    VcoRig a, b;
    a.setup(VCO_SINE, VCO_SINE, 0.f, F1, F1);
    b.setup(VCO_SINE, VCO_SINE, 0.f, F1, F1);
    a.vco->setSubOscGain(1.f);
    a.vco->setSubWaveForm(0);
    a.vco->setSubOct(0);
    b.vco->setSubOscGain(1.f);
    b.vco->setSubWaveForm(0);
    b.vco->setSubOct(0);
    a.vco->setSSDetune(25.f);
    b.vco->setSSDetune(25.f);
    b.vco->m_subGain = 0.f;     // keep the gain scale (set above), drop the sub (prm.subGain = 0)
    for (int i = 0; i < 8; i++) a.vco->m_ssPhase[i] = b.vco->m_ssPhase[i] = (unsigned int)(i * 77777);
    static float ba[512], bb[512];
    a.render(ba, 512);
    b.render(bb, 512);
    double mx = 0;
    for (int n = 0; n < 512; n++) {
        CHECK_CLOSE(ba[n], 2.0 * bb[n], 1e-5);
        if (fabs(ba[n]) > mx) mx = fabs(ba[n]);
    }
    CHECK(mx > 0.5);
    CHECK(mx <= 2.0 * 9 * 0.15 + 1e-5);
}

/** @brief LFO2 routed to FM modulates exactly like LFO1 routed to FM. */
TEST(vcomain, lfo2_fm_routing)
{
    VcoRig a, b, c;
    a.setup(VCO_SINE, VCO_SINE, 0.5f, F1, 3 * F1);
    b.setup(VCO_SINE, VCO_SINE, 0.5f, F1, 3 * F1);
    c.setup(VCO_SINE, VCO_SINE, 0.5f, F1, 3 * F1);
    a.vco->changeLfoSetting(0, 1, LFO_DEST_FM);
    b.vco->changeLfoSetting(1, 1, LFO_DEST_FM);
    CHECK_EQ(b.vco->m_lfoDest[1], LFO_DEST_FM);
    float ba[256], bb[256], bc[256];
    a.vco->processEnvelopedSignal(0, ba, 256, 256, 0);
    b.vco->processEnvelopedSignal(0, bb, 256, 0, 256);
    c.vco->processEnvelopedSignal(0, bc, 256, 0, 256);   // no FM routing: LFO2 ignored
    int same = 1, differs = 0;
    for (int n = 0; n < 256; n++) {
        if (ba[n] != bb[n]) same = 0;
        if (bb[n] != bc[n]) differs = 1;
    }
    CHECK(same);
    CHECK(differs);
    CHECK_EQ(a.vco->m_phase1, b.vco->m_phase1);
    CHECK_EQ(a.vco->m_fmTarget, fmTargetOf(127 * 256 / 512.f));
    CHECK_EQ(b.vco->m_fmTarget, a.vco->m_fmTarget);
    CHECK_EQ(c.vco->m_phase1, 256u * D1);
}

/**
 * @brief OSC2 pitch glide step length: a tiny pitch change renders in long (50 ms) pieces, a
 *        huge one (more than 17 octaves) sample by sample.
 */
TEST(vcomain, osc2_pitch_glide_step_limits)
{
    VcoRig r;
    CVco& v = *r.vco;
    r.setup(VCO_SINE, VCO_SINE, 0.5f, F1, F1);
    v.setNote(69, 0);
    v.noteOn();
    v.setOsc2Pitch(0.001f);     // |diff| * 128 rounds to 0 -> one piece per call
    float buf[256];
    v.processEnvelopedSignal(0, buf, 256, 0, 0);
    CHECK_EQ(v.m_osc2PitchPos, 256);
    CHECK_FEQ(v.m_osc2Pitch, 0.f);   // set at the start of the single piece
    v.processEnvelopedSignal(0, buf, 256, 0, 0);
    CHECK_CLOSE(v.m_osc2Pitch, 0.001 * 256 / (FS / 20), 1e-9);
    for (int i = 0; i < 10; i++) v.processEnvelopedSignal(0, buf, 256, 0, 0);
    CHECK_FEQ(v.m_osc2Pitch, 0.001f);

    VcoRig q;
    CVco& w = *q.vco;
    q.setup(VCO_SINE, VCO_SINE, 0.5f, F1, F1);
    w.setNote(69, 0);
    w.noteOn();
    w.setOsc2Pitch(20.f);       // 2560 steps over 2205 samples -> pieces of 1 sample
    w.processEnvelopedSignal(0, buf, 256, 0, 0);
    CHECK_EQ(w.m_osc2PitchPos, 256);
    CHECK_FEQ(w.m_osc2Pitch, 20.f * 255 / (FS / 20));
    int bad = 0;
    for (int n = 0; n < 256; n++) if (!(buf[n] == buf[n]) || fabs(buf[n]) > 2.0) bad++;
    CHECK_EQ(bad, 0);
}

/** @brief The portamento offset bends OSC2 only with OSC2 key tracking (the default); without it OSC2 keeps its pitch. */
TEST(vcomain, portamento_bends_osc2_only_with_kbd_track)
{
    for (int track = 0; track < 2; track++) {
        VcoRig r;
        CVco& v = *r.vco;
        r.setup(VCO_SINE, VCO_SINE, 0.5f, F1, F1);
        v.setOsc2KbdTrack(track);
        v.setPortamentTime(0.1f);
        v.setNote(57, 0);
        v.setNote(69, 1);
        v.m_phase1 = 0;
        v.m_phase2 = 0;
        float buf[32];
        v.processEnvelopedSignal(0, buf, 32, 0, 0);
        CHECK_EQ(v.m_portCur, -128);
        float f2 = v.calcFreqMod(v.m_freq2, track ? -128 : 0);
        if (track) CHECK_CLOSE(f2, v.m_freq2 * 0.5, 1e-3);
        CHECK_EQ(v.m_phase2, 32u * dPhaseOf(v, f2));
        CHECK_EQ(v.m_phase1, 32u * (unsigned int)(int)(float)(v.calcFreqMod(v.m_freq1, -128) * 134217728.f * v.m_invFs));
    }
}

/**
 * @brief A portamento time raised during a glide can leave the next resolution change below 10 %
 *        of the new length; the glide then takes the first-stage resolution again and still
 *        ends exactly at the note.
 */
TEST(vcomain, portamento_late_first_stage)
{
    VcoRig r;
    CVco& v = *r.vco;
    r.setup(VCO_SINE, VCO_SINE, 0.5f, F1, F1);
    v.setPortamentTime(4419.f / FS);
    v.setNote(57, 0);
    v.setNote(69, 1);
    int len1 = v.m_portLen;
    CHECK(len1 % 10 != 0);
    v.setPortamentTime(10.f);
    CHECK_EQ(v.m_portLen, 441000);
    CHECK_EQ(v.m_portNextRes, (int)((long)(len1 / 10) * 441000 / len1));
    REQUIRE(v.m_portNextRes < v.m_portLen / 10);
    static float buf[4096];
    int prev = v.m_portCur;
    int sawFirstStage = 0, done = 0, mono = 1;
    while (v.m_portPos < v.m_portLen && done < 500000) {
        // sample by sample around the stage change (the first-stage target lasts one sample)
        int n = (v.m_portPos > 40000 && v.m_portPos < 50000) ? 1 : 4096;
        v.processEnvelopedSignal(0, buf, n, 0, 0);
        done += n;
        if (v.m_portNextRes == v.m_portLen / 10) sawFirstStage = 1;
        if (v.m_portCur < prev) mono = 0;
        prev = v.m_portCur;
    }
    CHECK(sawFirstStage);
    CHECK(mono);
    CHECK(v.m_portPos >= v.m_portLen);
    v.processEnvelopedSignal(0, buf, 256, 0, 0);
    CHECK_EQ(v.m_portCur, 0);
}

/**
 * @brief A finished glide may run past the portamento length; a new portamento time then clamps
 *        the position to the new length, so the glide stays finished.
 */
TEST(vcomain, portamento_time_change_after_glide)
{
    VcoRig r;
    CVco& v = *r.vco;
    r.setup(VCO_SINE, VCO_SINE, 0.5f, F1, F1);
    v.setPortamentTime(0.1f);
    v.setNote(57, 0);
    v.setNote(69, 1);
    static float buf[256];
    int n = 0;
    while (v.m_portPos < v.m_portLen && n < 100) {
        v.processEnvelopedSignal(0, buf, 256, 0, 0);
        n++;
    }
    REQUIRE(v.m_portPos >= v.m_portLen);
    CHECK(v.m_portPos > v.m_portLen);   // the last step overshoots the length
    v.setPortamentTime(0.2f);
    CHECK_EQ(v.m_portLen, (int)(FS * 0.2f));
    CHECK_EQ(v.m_portPos, v.m_portLen);
    v.processEnvelopedSignal(0, buf, 256, 0, 0);
    CHECK_EQ(v.m_portCur, 0);
}
