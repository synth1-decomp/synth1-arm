/**
 * @file test_distortion.cpp
 * @brief Unit tests for techpop/distortion.h: CBiquadFilter, CWaveShaper, CGtLPF/CGtHPF,
 *        CGtAmpSim1 and the CDistortion device (types, drive/level mapping, speaker filter,
 *        silence handling).
 */
#include "test.h"

#include <math.h>
#include <string.h>

#include "distortion.h"

namespace {

const double kPi = 3.14159265358979323846;

/**
 * @brief Gives the tests read access to the protected CDistortion state.
 */
class DistProbe : public CDistortion {
public:
    float drive() const { return m_drive; }
    int type() const { return m_type; }
    float level() const { return m_level; }
    int silentLimit() const { return m_silentLimit; }
    int speakerTone() const { return m_speakerTone; }
    const CBiquadFilter& speaker() const { return m_speaker; }
    const CWaveShaper& shaper() const { return m_shaper; }
    CGtAmpSim1* amp() const { return m_pAmp; }
};

/**
 * @brief Runs a sine through a mono biquad and returns the steady-state peak amplitude.
 *
 * @param f     Filter (state is used as is).
 * @param freq  Sine frequency in Hz.
 * @param fs    Sampling frequency in Hz.
 * @return Peak |output| over the last half of a 1 s run.
 */
double biquadSineGain(CBiquadFilter& f, double freq, int fs)
{
    const int n = fs;
    static float buf[96000];
    for (int i = 0; i < n; i++) buf[i] = (float)sin(2.0 * kPi * freq * i / fs);
    f.process(n, buf);
    double peak = 0;
    for (int i = n / 2; i < n; i++) peak = fabs(buf[i]) > peak ? fabs(buf[i]) : peak;
    return peak;
}

/**
 * @brief Makes a cleared single-stage biquad.
 */
void initBiquad(CBiquadFilter& f, int type, float freq, float q, float gain, int fs)
{
    memset(&f, 0, sizeof f);
    f.setPoles(1);
    f.setParam(0, type, freq, q, gain, fs);
    f.clearOldSig();
}

/**
 * @brief Puts a CDistortion into a fully initialised state (type, drive, level, tone).
 */
void initDist(CDistortion& d, int type, int drive, int level)
{
    d.setDrive(drive);
    d.setType(type);
    d.setDrive(drive);
    d.setLevel(level);
}

}  // namespace

/** @brief A biquad with zero stages passes mono and stereo buffers through unchanged. */
TEST(biquad, zero_stages_is_passthrough)
{
    CBiquadFilter f;
    memset(&f, 0, sizeof f);
    f.setPoles(0);
    float m[4] = {0.25f, -1.0f, 3.0f, 0.0f};
    CHECK_EQ(f.process(4, m), 4);
    CHECK(m[0] == 0.25f && m[1] == -1.0f && m[2] == 3.0f && m[3] == 0.0f);
    tagStereoData s[2] = {{0.5f, -0.5f}, {1.5f, 2.0f}};
    CHECK_EQ(f.processLR(2, s), 2);
    CHECK(s[0].l == 0.5f && s[0].r == -0.5f && s[1].l == 1.5f && s[1].r == 2.0f);
}

/** @brief RBJ low pass coefficients at fs/4 with Q = 1/sqrt(2) match the closed form. */
TEST(biquad, lpf_coefficients_at_quarter_rate)
{
    CBiquadFilter f;
    memset(&f, 0, sizeof f);
    f.setParam(0, BQ_LPF, 11025.0f, (float)(1.0 / sqrt(2.0)), 0.0f, 44100);
    // w = pi/2: cos 0, sin 1, alpha = 1/sqrt(2)
    double a0 = 1.0 + 1.0 / sqrt(2.0);
    CHECK_CLOSE(f.m_b0[0], 0.5 / a0, 1e-6);
    CHECK_CLOSE(f.m_b1[0], 1.0 / a0, 1e-6);
    CHECK_CLOSE(f.m_b2[0], 0.5 / a0, 1e-6);
    CHECK_CLOSE(f.m_a1[0], 0.0, 1e-6);
    CHECK_CLOSE(f.m_a2[0], (1.0 - 1.0 / sqrt(2.0)) / a0, 1e-6);
}

/** @brief The low pass has unity DC gain and attenuates frequencies far above the cutoff. */
TEST(biquad, lpf_passes_dc_and_attenuates_highs)
{
    CBiquadFilter f;
    initBiquad(f, BQ_LPF, 500.0f, 0.707f, 0.0f, 44100);
    float buf[4000];
    for (int i = 0; i < 4000; i++) buf[i] = 1.0f;
    f.process(4000, buf);
    CHECK_CLOSE(buf[3999], 1.0, 1e-4);

    initBiquad(f, BQ_LPF, 500.0f, 0.707f, 0.0f, 44100);
    double g = biquadSineGain(f, 10000.0, 44100);
    CHECK(g < 0.01);          // two decades above: about -54 dB
    initBiquad(f, BQ_LPF, 500.0f, 0.707f, 0.0f, 44100);
    CHECK_CLOSE(biquadSineGain(f, 50.0, 44100), 1.0, 0.01);
}

/** @brief The high pass blocks DC and passes frequencies far above the cutoff. */
TEST(biquad, hpf_blocks_dc)
{
    CBiquadFilter f;
    initBiquad(f, BQ_HPF, 100.0f, 0.707f, 0.0f, 44100);
    float buf[20000];
    for (int i = 0; i < 20000; i++) buf[i] = 1.0f;
    f.process(20000, buf);
    CHECK(fabs(buf[19999]) < 1e-4);
    initBiquad(f, BQ_HPF, 100.0f, 0.707f, 0.0f, 44100);
    CHECK_CLOSE(biquadSineGain(f, 5000.0, 44100), 1.0, 0.01);
}

/** @brief Peaking: the gain at the centre frequency equals the requested boost or cut. */
TEST(biquad, peaking_gain_at_centre)
{
    CBiquadFilter f;
    initBiquad(f, BQ_PEAKING, 1000.0f, 2.0f, 12.0f, 44100);
    CHECK_CLOSE(biquadSineGain(f, 1000.0, 44100), pow(10.0, 12.0 / 20.0), 0.02);
    initBiquad(f, BQ_PEAKING, 1000.0f, 2.0f, -12.0f, 44100);
    CHECK_CLOSE(biquadSineGain(f, 1000.0, 44100), pow(10.0, -12.0 / 20.0), 0.005);
    initBiquad(f, BQ_PEAKING, 1000.0f, 2.0f, 12.0f, 44100);
    CHECK_CLOSE(biquadSineGain(f, 50.0, 44100), 1.0, 0.03);
}

/** @brief Low shelf: DC gain is 10^(gain/20) and high frequencies pass at unity. */
TEST(biquad, lowshelf_dc_gain)
{
    CBiquadFilter f;
    initBiquad(f, BQ_LOWSHELF, 300.0f, 0.707f, 6.0f, 44100);
    float buf[20000];
    for (int i = 0; i < 20000; i++) buf[i] = 1.0f;
    f.process(20000, buf);
    CHECK_CLOSE(buf[19999], pow(10.0, 6.0 / 20.0), 1e-3);
    initBiquad(f, BQ_LOWSHELF, 300.0f, 0.707f, 6.0f, 44100);
    CHECK_CLOSE(biquadSineGain(f, 15000.0, 44100), 1.0, 0.01);
}

/** @brief Band passes: BPF2 peaks at 0 dB, BPF (constant skirt) peaks at Q. */
TEST(biquad, bandpass_peak_gains)
{
    CBiquadFilter f;
    initBiquad(f, BQ_BPF2, 2000.0f, 4.0f, 0.0f, 44100);
    CHECK_CLOSE(biquadSineGain(f, 2000.0, 44100), 1.0, 0.01);
    initBiquad(f, BQ_BPF, 2000.0f, 4.0f, 0.0f, 44100);
    CHECK_CLOSE(biquadSineGain(f, 2000.0, 44100), 4.0, 0.04);
    initBiquad(f, BQ_BPF2, 2000.0f, 4.0f, 0.0f, 44100);
    CHECK(biquadSineGain(f, 100.0, 44100) < 0.02);
}

/** @brief Stereo processing filters each channel like the mono path with separate state. */
TEST(biquad, stereo_channels_match_mono)
{
    CBiquadFilter mono, st;
    memset(&mono, 0, sizeof mono);
    memset(&st, 0, sizeof st);
    mono.setPoles(2);
    st.setPoles(2);
    mono.setParam(0, BQ_LPF, 3000.0f, 1.5f, 0.0f, 44100);
    mono.setParam(1, BQ_HPF, 100.0f, 0.7f, 0.0f, 44100);
    st.setParam(0, BQ_LPF, 3000.0f, 1.5f, 0.0f, 44100);
    st.setParam(1, BQ_HPF, 100.0f, 0.7f, 0.0f, 44100);
    mono.clearOldSig();
    st.clearOldSig();
    float m[512];
    tagStereoData s[512];
    for (int i = 0; i < 512; i++) {
        m[i] = (float)sin(i * 0.37) + ((i % 7) == 0 ? 0.5f : 0.0f);
        s[i].l = m[i];
        s[i].r = 0.0f;
    }
    mono.process(512, m);
    st.processLR(512, s);
    int same = 1, rzero = 1;
    for (int i = 0; i < 512; i++) {
        if (fabs(m[i] - s[i].l) > 1e-6) same = 0;
        if (s[i].r != 0.0f) rzero = 0;
    }
    CHECK(same);
    CHECK(rzero);   // silence on R stays silent: channels do not leak
}

/** @brief clearOldSig() resets the history so zero input gives zero output again. */
TEST(biquad, clear_old_sig_resets_state)
{
    CBiquadFilter f;
    initBiquad(f, BQ_LPF, 1000.0f, 5.0f, 0.0f, 44100);
    float buf[64];
    for (int i = 0; i < 64; i++) buf[i] = 1.0f;
    f.process(64, buf);
    f.clearOldSig();
    for (int i = 0; i < 64; i++) buf[i] = 0.0f;
    f.process(64, buf);
    int zero = 1;
    for (int i = 0; i < 64; i++) if (buf[i] != 0.0f) zero = 0;
    CHECK(zero);
}

/** @brief One-pole CGtLPF converges to a step and CGtHPF decays a step back to zero. */
TEST(gtfilters, one_pole_step_responses)
{
    CGtLPF lp;
    lp.m_coef = 0.1f;
    CHECK_CLOSE(lp.process(1.0f), 0.1, 1e-7);
    CHECK_CLOSE(lp.process(1.0f), 0.19, 1e-6);
    for (int i = 0; i < 500; i++) lp.process(1.0f);
    CHECK_CLOSE(lp.m_z, 1.0, 1e-5);

    CGtHPF hp;
    hp.m_coef = 0.9f;
    CHECK_CLOSE(hp.process(1.0f), 0.9, 1e-7);
    CHECK_CLOSE(hp.process(1.0f), 0.81, 1e-6);
    float y = 0;
    for (int i = 0; i < 500; i++) y = hp.process(1.0f);
    CHECK(fabs(y) < 1e-6);
}

/** @brief Fold back reflects values above the threshold back into range; output gain compensation for drive 1 is 1.25. */
TEST(waveshaper, fold_back_values)
{
    CWaveShaper w;
    w.setDrive(1.0f);
    w.setType(2);
    w.setDrive(1.0f);
    CHECK_CLOSE(w.m_gain, 1.25, 1e-6);   // 1 / (1 - 1/((d+1)^2 d + 1)) with d = 1
    CHECK_CLOSE(w.m_threshold, 0.7, 1e-7);
    float b[5] = {0.5f, 0.9f, -0.9f, 1.6f, 0.0f};
    w.process(b, 5, 1.0f);
    CHECK_CLOSE(b[0], 0.5 * 1.25, 1e-6);
    CHECK_CLOSE(b[1], 0.5 * 1.25, 1e-5);  // 0.7 - (0.9 - 0.7)
    CHECK_CLOSE(b[2], -0.5 * 1.25, 1e-5);
    CHECK_CLOSE(b[3], -0.2 * 1.25, 1e-5); // 1.6 folds past the top to -0.2
    CHECK(b[4] == 0.0f);
}

/** @brief Fold back output never exceeds threshold * gain * level for any input. */
TEST(waveshaper, fold_back_is_bounded)
{
    CWaveShaper w;
    w.setDrive(20.0f);
    w.setType(2);
    w.setDrive(20.0f);
    float b[2001];
    for (int i = 0; i <= 2000; i++) b[i] = (i - 1000) / 250.0f;
    w.process(b, 2001, 0.5f);
    float lim = 0.7f * w.m_gain * 0.5f * 1.0001f;
    int ok = 1;
    for (int i = 0; i <= 2000; i++) if (fabs(b[i]) > lim) ok = 0;
    CHECK(ok);
}

/** @brief Soft clip is monotonic, keeps the sign and saturates at gain * level. */
TEST(waveshaper, soft_clip_shape)
{
    CWaveShaper w;
    w.setDrive(4.0f);
    w.setType(1);
    w.setDrive(4.0f);
    // gain = 1 / (1 - 1/(25*4 + 1)) = 101 / 100
    CHECK_CLOSE(w.m_gain, 1.01, 1e-5);
    float b[401];
    for (int i = 0; i <= 400; i++) b[i] = (i - 200) / 100.0f;
    w.process(b, 401, 1.0f);
    int mono = 1, sign = 1, bounded = 1;
    for (int i = 1; i <= 400; i++) if (b[i] < b[i - 1]) mono = 0;
    for (int i = 0; i <= 400; i++) {
        float x = (i - 200) / 100.0f;
        if (x > 0.001f && b[i] <= 0) sign = 0;
        if (x < -0.001f && b[i] >= 0) sign = 0;
        if (fabs(b[i]) > w.m_gain) bounded = 0;
    }
    CHECK(mono);
    CHECK(sign);
    CHECK(bounded);
    // at |x| = 1 (drive 4) the curve is 1 - 1/101, so the output is exactly 1 (times level)
    CHECK_CLOSE(b[300], 1.0, 1e-3);
    CHECK_CLOSE(b[100], -1.0, 1e-3);
}

/** @brief Type 0 uses the tube gain compensation 2 / tubeCurve(drive) and fold back in process(). */
TEST(waveshaper, tube_gain_compensation)
{
    CWaveShaper w;
    w.setDrive(1.0f);
    w.setType(0);
    // tubeCurve(1) = 1 - 1/(2 * 1.4 + 1) = 1 - 1/3.8
    CHECK_CLOSE(w.m_gain, 2.0 / (1.0 - 1.0 / 3.8), 1e-5);
    float b[2] = {0.9f, 0.3f};
    w.process(b, 2, 1.0f);
    CHECK_CLOSE(b[0], 0.5 * w.m_gain, 1e-5);   // not type 1: fold back
    CHECK_CLOSE(b[1], 0.3 * w.m_gain, 1e-5);
}

/** @brief processLR() shapes each channel exactly like process(). */
TEST(waveshaper, stereo_matches_mono)
{
    for (int type = 1; type <= 2; type++) {
        CWaveShaper w;
        w.setDrive(3.0f);
        w.setType(type);
        float m[64];
        tagStereoData s[64];
        for (int i = 0; i < 64; i++) {
            m[i] = (float)sin(i * 0.3) * 0.8f;
            s[i].l = m[i];
            s[i].r = -m[i];
        }
        w.process(m, 64, 0.7f);
        w.processLR(s, 64, 0.7f);
        int ok = 1;
        for (int i = 0; i < 64; i++) {
            if (s[i].l != m[i]) ok = 0;
            if (type == 2 && fabs(s[i].r + m[i]) > 1e-6) ok = 0;   // fold back is odd-symmetric
        }
        CHECK(ok);
    }
}

/** @brief The amp simulator's filter coefficients follow the sampling rate. */
TEST(ampsim, coefficients_follow_sampling_rate)
{
    CGtAmpSim1 a;
    CHECK_EQ(a.m_samplingFreq, 44100);
    CHECK_CLOSE(a.m_bias1[0].m_coef, 2.0 * kPi * 86.0 / 44100, 1e-7);
    CHECK_CLOSE(a.m_hpf1[1].m_coef, 1.0 / (2.0 * kPi * 31.0 / 44100 + 1.0), 1e-7);
    CHECK_CLOSE(a.m_lpf2[0].m_coef, 2.0 * kPi * 6531.0 / 44100, 1e-7);
    CHECK_CLOSE(a.m_bias3[1].m_coef, 2.0 * kPi * 194.0 / 44100, 1e-7);
    a.setSamplingFreq(96000);
    CHECK_CLOSE(a.m_bias2[1].m_coef, 2.0 * kPi * 132.0 / 96000, 1e-7);
    CHECK_CLOSE(a.m_hpf3[0].m_coef, 1.0 / (2.0 * kPi * 31.0 / 96000 + 1.0), 1e-7);
    a.setDrive(5.0f);
    CHECK(a.m_drive == 5.0f);
    CHECK(a.m_shaper.m_drive == 5.0f);
    CHECK_EQ(a.m_shaper.m_type, 0);
}

/** @brief Silence through the settled amp simulator stays (near) silent; output is bounded. */
TEST(ampsim, silence_and_bounded_output)
{
    CGtAmpSim1 a;
    a.setDrive(1.0f);
    float b[8192];
    memset(b, 0, sizeof b);
    a.process(b, 8192, 1.0f);
    double peak = 0;
    for (int i = 4096; i < 8192; i++) peak = fabs(b[i]) > peak ? fabs(b[i]) : peak;
    CHECK(peak < 1e-3);

    a.setDrive(100.0f);
    for (int i = 0; i < 8192; i++) b[i] = (float)sin(2 * kPi * 220.0 * i / 44100);
    a.process(b, 8192, 1.0f);
    int finite = 1;
    double p2 = 0;
    for (int i = 0; i < 8192; i++) {
        if (!(b[i] == b[i])) finite = 0;
        p2 = fabs(b[i]) > p2 ? fabs(b[i]) : p2;
    }
    CHECK(finite);
    CHECK(p2 > 0.05);                     // the signal gets through
    CHECK(p2 < 4.0 * a.m_shaper.m_gain);  // tube curve outputs are < 1, HPF output < 2
}

/** @brief processLR() gives each channel the same result as the mono process() path. */
TEST(ampsim, stereo_matches_mono)
{
    CGtAmpSim1 a, b;
    a.setDrive(10.0f);
    b.setDrive(10.0f);
    float m[1024];
    tagStereoData s[1024];
    for (int i = 0; i < 1024; i++) {
        m[i] = (float)sin(2 * kPi * 330.0 * i / 44100) * 0.5f;
        s[i].l = m[i];
        s[i].r = m[i];
    }
    a.process(m, 1024, 0.8f);
    b.processLR(s, 1024, 0.8f);
    int ok = 1;
    for (int i = 0; i < 1024; i++) {
        if (fabs(s[i].l - m[i]) > 1e-5 || fabs(s[i].r - m[i]) > 1e-5) ok = 0;
    }
    CHECK(ok);
}

/** @brief Drive maps 0..127 to -10..+60 dB, level to (v+2)/129 and the type is taken modulo 3. */
TEST(distortion, parameter_mappings)
{
    DistProbe d;
    initDist(d, 0, 0, 0);
    CHECK_CLOSE(d.drive(), pow(10.0, -0.5), 1e-6);
    CHECK_CLOSE(d.level(), 2.0 / 129.0, 1e-7);
    d.setDrive(127);
    CHECK_CLOSE(d.drive(), 1000.0, 1e-2);
    d.setDrive(18);   // 18/127*70 - 10 = -0.0787 dB
    CHECK_CLOSE(d.drive(), pow(10.0, (18 / 127.0 * 70.0 - 10.0) / 20.0), 1e-5);
    CHECK(d.amp()->m_drive == d.drive());
    CHECK(d.shaper().m_drive == d.drive());
    d.setLevel(127);
    CHECK_CLOSE(d.level(), 1.0, 1e-7);
    d.setType(4);
    CHECK_EQ(d.type(), 1);
    CHECK_EQ(d.shaper().m_type, 1);
    d.setType(5);
    CHECK_EQ(d.type(), 2);
    d.setType(3);
    CHECK_EQ(d.type(), 0);
}

/** @brief The speaker filter is a 2-stage LPF at 400*40^(tone/127) Hz (Q 3) and HPF at 100 Hz (Q 2.3). */
TEST(distortion, speaker_filter_coefficients)
{
    DistProbe d;
    CHECK_EQ(d.speakerTone(), 64);
    d.setSpeakerTone(127);
    CBiquadFilter ref;
    memset(&ref, 0, sizeof ref);
    ref.setParam(0, BQ_LPF, 16000.0f, 3.0f, 0.0f, 44100);
    ref.setParam(1, BQ_HPF, 100.0f, 2.3f, 0.0f, 44100);
    CHECK_EQ(d.speaker().m_nStage, 2);
    for (int s = 0; s < 2; s++) {
        CHECK_CLOSE(d.speaker().m_b0[s], ref.m_b0[s], 1e-6);
        CHECK_CLOSE(d.speaker().m_b1[s], ref.m_b1[s], 1e-6);
        CHECK_CLOSE(d.speaker().m_a1[s], ref.m_a1[s], 1e-6);
        CHECK_CLOSE(d.speaker().m_a2[s], ref.m_a2[s], 1e-6);
    }
    d.setSpeakerTone(0);
    ref.setParam(0, BQ_LPF, 400.0f, 3.0f, 0.0f, 44100);
    CHECK_CLOSE(d.speaker().m_b0[0], ref.m_b0[0], 1e-7);
    CHECK_CLOSE(d.speaker().m_a1[0], ref.m_a1[0], 1e-6);
}

/** @brief A sampling rate change updates the 25 ms silence limit and the amp simulator. */
TEST(distortion, sampling_rate_change)
{
    DistProbe d;
    CHECK_EQ(d.silentLimit(), 1102);
    d.setSamplingFreq(48000);
    CHECK_EQ(d.silentLimit(), 1200);
    CHECK_EQ(d.amp()->m_samplingFreq, 48000);
}

/** @brief With silent input the tail runs for 25 ms (zero-filled blocks), then the buffer is left alone. */
TEST(distortion, silence_tail_then_idle)
{
    DistProbe d;
    initDist(d, 2, 64, 100);
    float buf[256];
    int processed = 0;
    for (int i = 0; i < 256; i++) buf[i] = 0.1f;
    d.processSignal(256, buf, &processed);
    CHECK_EQ(processed, 0);
    // counts at entry: 0, 256, 512, 768, 1024 -> rendered; 1280 > 1102 -> idle
    for (int blk = 0; blk < 5; blk++) {
        for (int i = 0; i < 256; i++) buf[i] = 123.0f;
        processed = 1;
        CHECK_EQ(d.processSignal(256, buf, &processed), 256);
        CHECK_EQ(processed, 0);
        CHECK(buf[0] != 123.0f);
    }
    for (int i = 0; i < 256; i++) buf[i] = 123.0f;
    processed = 1;
    d.processSignal(256, buf, &processed);
    CHECK_EQ(processed, 1);
    CHECK(buf[0] == 123.0f && buf[255] == 123.0f);
    // non-silent input restarts processing
    for (int i = 0; i < 256; i++) buf[i] = 0.0f;
    processed = 0;
    d.processSignal(256, buf, &processed);
    processed = 1;
    buf[0] = 123.0f;
    d.processSignal(256, buf, &processed);
    CHECK_EQ(processed, 0);
}

/** @brief Fold back with silent (zero) input produces exact silence, mono and stereo. */
TEST(distortion, fold_back_silence_in_silence_out)
{
    DistProbe d;
    initDist(d, 2, 90, 127);
    float buf[512];
    memset(buf, 0, sizeof buf);
    int processed = 0;
    d.processSignal(512, buf, &processed);
    int zero = 1;
    for (int i = 0; i < 512; i++) if (buf[i] != 0.0f) zero = 0;
    CHECK(zero);
    memset(buf, 0, sizeof buf);
    d.processSignalW(256, buf, &processed);
    zero = 1;
    for (int i = 0; i < 512; i++) if (buf[i] != 0.0f) zero = 0;
    CHECK(zero);
}

/** @brief Every type distorts a sine (changes it) while the output stays finite and bounded. */
TEST(distortion, all_types_bounded)
{
    for (int type = 0; type < 3; type++) {
        DistProbe d;
        initDist(d, type, 100, 127);
        d.setSpeakerTone(127);
        float buf[4096], in[4096];
        for (int i = 0; i < 4096; i++) in[i] = buf[i] = (float)sin(2 * kPi * 220.0 * i / 44100) * 0.5f;
        int processed = 0;
        for (int off = 0; off < 4096; off += 256) d.processSignal(256, buf + off, &processed);
        int finite = 1, differs = 0;
        double peak = 0;
        for (int i = 0; i < 4096; i++) {
            if (!(buf[i] == buf[i])) finite = 0;
            if (fabs(buf[i] - in[i]) > 0.01) differs = 1;
            peak = fabs(buf[i]) > peak ? fabs(buf[i]) : peak;
        }
        CHECK(finite);
        CHECK(differs);
        CHECK(peak > 0.01);
        CHECK(peak < 20.0);
    }
}

/** @brief Stereo processing with identical channels gives identical, mono-equal outputs. */
TEST(distortion, stereo_matches_mono)
{
    for (int type = 0; type < 3; type++) {
        DistProbe a, b;
        initDist(a, type, 80, 110);
        initDist(b, type, 80, 110);
        float m[512], s[1024];
        for (int i = 0; i < 512; i++) {
            m[i] = (float)sin(i * 0.05) * 0.7f;
            s[i * 2] = s[i * 2 + 1] = m[i];
        }
        int p1 = 0, p2 = 0;
        a.processSignal(512, m, &p1);
        b.processSignalW(512, s, &p2);
        int ok = 1;
        for (int i = 0; i < 512; i++) {
            if (fabs(s[i * 2] - m[i]) > 1e-5 || s[i * 2] != s[i * 2 + 1]) ok = 0;
        }
        CHECK(ok);
    }
}

/** @brief Characterization: fold back distortion output for a fixed short input. */
TEST(distortion, characterization_fold_back)
{
    DistProbe d;
    initDist(d, 2, 64, 64);
    float buf[8] = {0.0f, 0.1f, 0.2f, 0.3f, -0.1f, -0.2f, 0.05f, 0.0f};
    int processed = 0;
    d.processSignal(8, buf, &processed);
    // reference values from the current (release-matching) build
    const double expect[8] = {0.0, -0.00693386095, -0.0176285803, -0.0164225213,
                              -0.000889355317, 0.0169247705, 0.0283207074, 0.0417689085};
    for (int i = 0; i < 8; i++) CHECK_CLOSE(buf[i], expect[i], 1e-7);
}

/** @brief Stereo silent input: both channels are zero-filled and rendered for the 25 ms tail, then left alone. */
TEST(distortion, silence_tail_stereo)
{
    DistProbe d;
    initDist(d, 2, 64, 100);
    float buf[2 * 256];
    int processed = 0;
    for (int i = 0; i < 2 * 256; i++) buf[i] = 0.1f;
    d.processSignalW(256, buf, &processed);
    for (int blk = 0; blk < 5; blk++) {
        for (int i = 0; i < 2 * 256; i++) buf[i] = 123.0f;
        processed = 1;
        CHECK_EQ(d.processSignalW(256, buf, &processed), 256);
        CHECK_EQ(processed, 0);
        int changed = 1;
        for (int i = 0; i < 2 * 256; i++) if (buf[i] == 123.0f || buf[i] != buf[i]) changed = 0;
        CHECK(changed);
    }
    for (int i = 0; i < 2 * 256; i++) buf[i] = 123.0f;
    processed = 1;
    d.processSignalW(256, buf, &processed);
    CHECK_EQ(processed, 1);
    CHECK(buf[0] == 123.0f && buf[511] == 123.0f);
}
