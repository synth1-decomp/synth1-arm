/**
 * @file test_vcf.cpp
 * @brief Unit tests for techpop/vcf.h: the cutoff table (initVcf), the CVcf filter types (12 dB
 *        state variable LPF/HPF/BPF, 24 dB ladder, diode ladder) and their frequency response,
 *        resonance and stability, the cutoff glide, envelope amount, key tracking, velocity and
 *        unison paths, the limiter and distortion, and the CBQF biquad.
 */
#include "test.h"

#include <math.h>
#include <vector>

#include "engine.h"
#include "vcf.h"

// after the engine headers: <complex> pulls in <cmath>, which hides the isnan macro common.h uses
#include <complex>

namespace {

/**
 * @brief Restores CSignalDevice::m_bSSE when it goes out of scope.
 */
struct SSEGuard {
    int saved;
    SSEGuard() : saved(CSignalDevice::m_bSSE) {}
    ~SSEGuard() { CSignalDevice::m_bSSE = saved; }
};

/**
 * @brief Input device producing a sine wave.
 */
class SineSource : public CSignalDevice {
public:
    SineSource(double hz, double amp) : m_hz(hz), m_amp(amp), m_pos(0) {}

    virtual int processSignal(int size, float* pBuf, int* pProcessed)
    {
        for (int i = 0; i < size; i++, m_pos++) {
            pBuf[i] = (float)(m_amp * sin(2 * M_PI * m_hz * m_pos / m_samplingFreq));
        }
        *pProcessed = 0;
        return size;
    }

    double m_hz;
    double m_amp;
    long m_pos;
};

/**
 * @brief Runs the filter routine of @p type over a buffer in blocks of 256 samples.
 */
void runFilter(CVcf& f, int type, int cutoff, float reso, float* p, int n)
{
    for (int i = 0; i < n; i += 256) {
        int len = n - i < 256 ? n - i : 256;
        if (type == VCF_LPF24) f.lowpass3(p + i, cutoff, reso, p + i, len);
        else if (type == VCF_LPFDIODE) f.lowpassDiodeLadder(p + i, cutoff, reso, p + i, len);
        else f.lowpass2(p + i, cutoff, reso, p + i, len);
    }
}

/**
 * @brief Steady-state RMS gain of a filter type for a sine of @p hz (amplitude 0.1).
 */
double gainAt(int type, int cutoff, float reso, double hz, int fs = 44100)
{
    t1::initEngine();
    CVcf f;
    f.setSamplingFreq(fs);
    f.setType(type);
    const int N = 16384;
    const double amp = 0.1;
    std::vector<float> buf(N);
    for (int i = 0; i < N; i++) buf[i] = (float)(amp * sin(2 * M_PI * hz * i / fs));
    runFilter(f, type, cutoff, reso, &buf[0], N);
    double s = 0;
    for (int i = N / 2; i < N; i++) s += (double)buf[i] * buf[i];
    return sqrt(s / (N / 2)) / (amp / sqrt(2.0));
}

/**
 * @brief Larger of two doubles (fmax is not C++98).
 */
double dmax(double a, double b)
{
    return a > b ? a : b;
}

/** @brief Cutoff index of about 1 kHz (s1g->m_vcfFreqTable[617] = 1001.4 Hz). */
const int C1K = 617;

/**
 * @brief Fills a buffer with deterministic white noise in -1..1.
 */
void noise(float* p, int n, unsigned seed)
{
    unsigned s = seed;
    for (int i = 0; i < n; i++) {
        s = s * 1103515245u + 12345u;
        p[i] = ((s >> 16) & 0x7fff) / 16384.0f - 1.0f;
    }
}

/**
 * @brief Complex response of a CBQF at normalized angular frequency w.
 */
double bqfMag(const CBQF& b, double w)
{
    std::complex<double> z1 = std::polar(1.0, -w), z2 = z1 * z1;
    std::complex<double> num = (double)b.m_b[0] + (double)b.m_b[1] * z1 + (double)b.m_b[2] * z2;
    std::complex<double> den = 1.0 + (double)b.m_a[1] * z1 + (double)b.m_a[2] * z2;
    return std::abs(num / den);
}

/**
 * @brief A zeroed CBQF with coefficients for (type, freq, q, gain) at 44.1 kHz.
 */
CBQF makeBqf(int type, float freq, float q, float gain)
{
    CBQF b;
    memset(&b, 0, sizeof(b));
    b.setParam(type, freq, q, gain, 44100);
    return b;
}

}  // namespace

/** @brief initVcf() builds an exponential cutoff table from 15 Hz towards 16 kHz. */
TEST(vcf, cutoff_table)
{
    t1::initEngine();
    initVcf();
    const float* t = s1g->m_vcfFreqTable;
    CHECK_CLOSE(t[0], 15.0, 1e-5);
    CHECK_CLOSE(t[512], 15.0 * sqrt(16000.0 / 15.0), 1e-3);
    CHECK_CLOSE(t[617], 1001.375, 1e-2);
    CHECK_CLOSE(t[1023], 15891.43, 1e-1);
    double ratio = pow(16000.0 / 15.0, 1.0 / 1024);
    for (int i = 1; i < VCF_FREQ_MAX; i++) {
        CHECK(t[i] > t[i - 1]);
        CHECK_CLOSE(t[i] / t[i - 1], ratio, 1e-5);
    }
    // a second call does nothing
    float saved = t[100];
    initVcf();
    CHECK(t[100] == saved);
}

/** @brief The constructor sets a 24 dB lowpass at cutoff 512, resonance 0.4, velocity sensitive. */
TEST(vcf, constructor_defaults)
{
    t1::initEngine();
    CVcf f;
    CHECK_EQ(f.m_type, VCF_LPF24);
    CHECK_EQ(f.m_freq, 512);
    CHECK_CLOSE(f.m_resonance, 0.4, 1e-7);
    CHECK_EQ(f.m_bVelocitySens, 1);
    CHECK_EQ(f.m_velocity, 0);
    CHECK_CLOSE(f.m_amount, 0, 0);
    CHECK_CLOSE(f.m_kbdTrack, 0, 0);
    CHECK_EQ(f.m_kbdTrackOfs, 0);
    CHECK_EQ(f.m_unisonOfs, 0);
    CHECK_EQ(f.m_note, 48);
    CHECK_CLOSE(f.m_distortion, 0, 0);
    CHECK_CLOSE(f.m_gainScale, 1.0, 0);
    CHECK_EQ(f.m_max, VCF_FREQ_MAX);
    CHECK_EQ(f.m_peakLevel, VCF_FREQ_MAX);
    CHECK_EQ(f.m_bProcessIdle, 1);
    CHECK_EQ(f.m_bPostProcess, 1);
    CHECK_EQ(f.m_lastFreq, -1);
    CHECK_EQ(f.m_interpPos, 44100 / 20);
    CHECK_CLOSE(f.m_limitGain, 1.0, 0);
    CHECK_CLOSE(f.m_limitEnv, 0, 0);
    CHECK_EQ(f.getEnvStatus(), ENV_OFF);
}

/** @brief The limiter and coefficient smoothing constants follow the sampling rate. */
TEST(vcf, sampling_rate_constants)
{
    t1::initEngine();
    CVcf f;
    static const int rates[3] = {44100, 48000, 96000};
    for (int i = 0; i < 3; i++) {
        int fs = rates[i];
        f.m_lastFreq = 5;
        f.setSamplingFreq(fs);
        CHECK_EQ(f.m_lastFreq, -1);
        CHECK_CLOSE(f.m_limitAttack, pow(0.5, 1.0 / (0.3 * fs)), 1e-7);
        CHECK_CLOSE(f.m_limitRelease, exp(-1.0 / (fs * 0.005)), 1e-6);
        CHECK_CLOSE(f.m_coefSmooth, pow(0.02, 1.0 / (fs * 0.001)), 1e-6);
    }
    f.setSamplingFreq(44100);
    CHECK_CLOSE(f.m_limitRelease, 0.995475113, 1e-7);
    CHECK_CLOSE(f.m_coefSmooth, 0.915112734, 1e-7);
}

/** @brief The simple setters store their values; setType() also invalidates the coefficient cache. */
TEST(vcf, setters)
{
    t1::initEngine();
    CVcf f;
    f.setFreq(1.0f);
    CHECK_EQ(f.m_freq, 1023);
    f.setFreq(0.5f);
    CHECK_EQ(f.m_freq, 511);
    f.setFreq(0.0f);
    CHECK_EQ(f.m_freq, 0);
    f.setResonance(0.7f);
    CHECK_CLOSE(f.m_resonance, 0.7, 1e-7);
    f.m_lastFreq = 100;
    f.setType(VCF_HPF12);
    CHECK_EQ(f.m_type, VCF_HPF12);
    CHECK_EQ(f.m_lastFreq, -1);
    f.setDistortion(0.3f);
    CHECK_CLOSE(f.m_distortion, 0.3, 1e-7);
    f.setGainScale(2.5f);
    CHECK_CLOSE(f.m_gainScale, 2.5, 0);
    f.setVelocitySensEnable(0);
    CHECK_EQ(f.m_bVelocitySens, 0);
    f.changeLfoSetting(0, 1, 3);
    CHECK_EQ(f.m_bLfoEnable[0], 1);
    f.changeLfoSetting(0, 1, 4);
    CHECK_EQ(f.m_bLfoEnable[0], 0);
    f.changeLfoSetting(1, 0, 3);
    CHECK_EQ(f.m_bLfoEnable[1], 0);
}

/** @brief Cutoff, amount, sustain and key tracking changes restart the glide from the current cutoff. */
TEST(vcf, parameter_changes_restart_glide)
{
    t1::initEngine();
    CVcf f;
    f.m_curFreq = 300;
    f.m_interpPos = 999;
    f.setFreq(0.25f);
    CHECK_EQ(f.m_startFreq, 300);
    CHECK_EQ(f.m_interpPos, 0);
    f.m_curFreq = 310;
    f.m_interpPos = 999;
    f.setAmount(0.2f);
    CHECK_EQ(f.m_startFreq, 310);
    CHECK_EQ(f.m_interpPos, 0);
    f.m_curFreq = 320;
    f.m_interpPos = 999;
    f.setSustain(0.5f);
    CHECK_EQ(f.m_startFreq, 320);
    CHECK_EQ(f.m_interpPos, 0);
    CHECK_EQ(f.m_sustainLevel, 512);
    f.m_curFreq = 330;
    f.m_interpPos = 999;
    f.setKbdTrack(0.5f);
    CHECK_EQ(f.m_startFreq, 330);
    CHECK_EQ(f.m_interpPos, 0);
}

/** @brief SVF coefficients: f = 2 sin(pi fc / 2fs) (2x oversampled), q = 1 - 0.7 reso; cached per (cutoff, reso). */
TEST(vcf, svf_coefficients)
{
    t1::initEngine();
    CVcf f;
    f.setType(VCF_LPF12);
    float buf[4] = {0, 0, 0, 0};
    f.lowpass2(buf, C1K, 0.5f, buf, 4);
    CHECK_CLOSE(f.m_f, 2 * sin(s1g->m_vcfFreqTable[C1K] * M_PI / 88200), 1e-6);
    CHECK_CLOSE(f.m_q, 0.65, 1e-6);
    CHECK_CLOSE(f.m_reso2, 0.5, 0);
    CHECK_EQ(f.m_lastFreq, C1K);
    CHECK_CLOSE(f.m_lastReso, 0.5, 0);
    // resonance above 1 is limited in the coefficient but kept as the cache key
    f.lowpass2(buf, C1K, 1.5f, buf, 4);
    CHECK_CLOSE(f.m_reso2, 1.0, 0);
    CHECK_CLOSE(f.m_q, 0.3, 1e-6);
    CHECK_CLOSE(f.m_lastReso, 1.5, 0);
}

/** @brief Ladder coefficients follow the polynomial fit and are smoothed per sample after the first call. */
TEST(vcf, ladder_coefficients_and_smoothing)
{
    t1::initEngine();
    CVcf f;
    float buf[256] = {0};
    f.lowpass3(buf, C1K, 0.5f, buf, 1);
    double fc = 2.0 * s1g->m_vcfFreqTable[C1K] / 44100;
    double k = 3.6 * fc - 1.6 * fc * fc - 1.0;
    double t = 4.3 * fc - 2.5 * fc * fc - 1.0;
    CHECK_CLOSE(f.m_k, k, 1e-6);
    CHECK_CLOSE(f.m_p, (k + 1) * 0.5, 1e-6);
    CHECK_CLOSE(f.m_r, exp((1.0 - (t + 1.0) * 0.5) * 1.386249) * 0.5, 1e-5);
    // first call after a reset: the smoothed coefficients start at the target
    CHECK_CLOSE(f.m_curK, f.m_k, 1e-6);
    CHECK_CLOSE(f.m_curP, f.m_p, 1e-6);
    CHECK_CLOSE(f.m_curR, f.m_r, 1e-6);
    // a new cutoff: the smoothed values glide towards it
    float oldP = f.m_curP;
    f.lowpass3(buf, 900, 0.5f, buf, 1);
    CHECK(f.m_p > oldP);
    CHECK(f.m_curP > oldP);
    CHECK(f.m_curP < f.m_p);
    CHECK_CLOSE(f.m_curP, oldP + (f.m_p - oldP) * (1 - f.m_coefSmooth), 1e-6);
    f.lowpass3(buf, 900, 0.5f, buf, 256);
    CHECK_CLOSE(f.m_curP, f.m_p, 1e-6);
    CHECK_CLOSE(f.m_curK, f.m_k, 1e-6);
    CHECK_CLOSE(f.m_curR, f.m_r, 1e-6);
}

/** @brief The lowpass types pass low frequencies and attenuate high ones; 24 dB rolls off faster than 12 dB. */
TEST(vcf, lowpass_frequency_response)
{
    double g12lo = gainAt(VCF_LPF12, C1K, 0, 50);
    double g12mid = gainAt(VCF_LPF12, C1K, 0, 5000);
    double g12hi = gainAt(VCF_LPF12, C1K, 0, 10000);
    CHECK_CLOSE(g12lo, 1.0, 0.02);
    CHECK(g12hi < 0.02);
    CHECK_CLOSE(g12hi / g12mid, 0.25, 0.05);       // about -12 dB per octave
    double g24lo = gainAt(VCF_LPF24, C1K, 0, 50);
    double g24mid = gainAt(VCF_LPF24, C1K, 0, 5000);
    double g24hi = gainAt(VCF_LPF24, C1K, 0, 10000);
    CHECK_CLOSE(g24lo, 1.0, 0.02);
    CHECK(g24mid < g12mid / 5);
    CHECK(g24hi / g24mid < 0.1);                   // steeper than -12 dB per octave
    double gdlo = gainAt(VCF_LPFDIODE, C1K, 0, 200);
    double gdhi = gainAt(VCF_LPFDIODE, C1K, 0, 10000);
    CHECK(gdlo > 0.5);
    CHECK(gdhi < 0.002);
    // the response falls monotonically above the cutoff
    static const double hz[4] = {2000, 4000, 8000, 16000};
    for (int t = 0; t < 3; t++) {
        int type = t == 0 ? VCF_LPF12 : t == 1 ? VCF_LPF24 : VCF_LPFDIODE;
        double prev = 1e9;
        for (int i = 0; i < 4; i++) {
            double g = gainAt(type, C1K, 0, hz[i]);
            CHECK(g < prev);
            prev = g;
        }
    }
}

/** @brief Moving the cutoff up lets more of a fixed high frequency through. */
TEST(vcf, cutoff_moves_response)
{
    static const int types[3] = {VCF_LPF12, VCF_LPF24, VCF_LPFDIODE};
    for (int t = 0; t < 3; t++) {
        double prev = 0;
        for (int c = 300; c <= 700; c += 100) {
            double g = gainAt(types[t], c, 0, 3000);
            CHECK(g > prev);
            prev = g;
        }
    }
    // the highpass does the opposite (measured in its stopband)
    double prev = 1e9;
    for (int c = 300; c <= 700; c += 100) {
        double g = gainAt(VCF_HPF12, c, 0, 50);
        CHECK(g < prev);
        prev = g;
    }
}

/** @brief The 12 dB highpass blocks low frequencies and passes high ones. */
TEST(vcf, highpass_frequency_response)
{
    CHECK(gainAt(VCF_HPF12, C1K, 0, 50) < 0.01);
    CHECK(gainAt(VCF_HPF12, C1K, 0, 200) < 0.06);
    CHECK_CLOSE(gainAt(VCF_HPF12, C1K, 0, 10000), 1.0, 0.02);
    CHECK_CLOSE(gainAt(VCF_HPF12, C1K, 0, 5000), 1.0, 0.05);
}

/** @brief The 12 dB bandpass peaks near the cutoff with unity gain and falls off on both sides. */
TEST(vcf, bandpass_frequency_response)
{
    double peak = gainAt(VCF_BPF12, C1K, 0, 1000);
    CHECK_CLOSE(peak, 1.0, 0.02);
    CHECK(gainAt(VCF_BPF12, C1K, 0, 200) < peak / 4);
    CHECK(gainAt(VCF_BPF12, C1K, 0, 5000) < peak / 4);
    CHECK(gainAt(VCF_BPF12, C1K, 0, 50) < 0.06);
    CHECK(gainAt(VCF_BPF12, C1K, 0, 10000) < 0.12);
}

/** @brief Resonance raises the response at the cutoff and lowers the SVF passband by q = 1 - 0.7 reso. */
TEST(vcf, resonance_peak)
{
    static const int types[5] = {VCF_LPF12, VCF_LPF24, VCF_HPF12, VCF_BPF12, VCF_LPFDIODE};
    for (int t = 0; t < 5; t++) {
        double g0 = gainAt(types[t], C1K, 0, 1000);
        double g5 = gainAt(types[t], C1K, 0.5f, 1000);
        double g9 = gainAt(types[t], C1K, 0.9f, 1000);
        CHECK(g5 > g0);
        CHECK(g9 > g5);
        CHECK(g9 > 1.5 * g0);
    }
    CHECK_CLOSE(gainAt(VCF_LPF12, C1K, 0.5f, 50), 0.65, 0.02);
    CHECK_CLOSE(gainAt(VCF_LPF12, C1K, 0.9f, 50), 0.37, 0.02);
    CHECK_CLOSE(gainAt(VCF_HPF12, C1K, 0.5f, 10000), 0.65, 0.02);
    // the ladder loses passband gain with resonance as well
    CHECK(gainAt(VCF_LPF24, C1K, 0.9f, 50) < 0.3);
    // the SVF resonance peak at 0.9 is about 3.7x
    CHECK_CLOSE(gainAt(VCF_LPF12, C1K, 0.9f, 1000), 3.7, 0.1);
}

/** @brief Every type stays finite and bounded for noise over the whole cutoff range and resonance 0..1. */
TEST(vcf, stability_over_cutoff_and_resonance)
{
    t1::initEngine();
    static const int types[5] = {VCF_LPF12, VCF_LPF24, VCF_HPF12, VCF_BPF12, VCF_LPFDIODE};
    static const float resos[4] = {0, 0.5f, 0.9f, 1.0f};
    std::vector<float> buf(4096);
    for (int t = 0; t < 5; t++) {
        for (int r = 0; r < 4; r++) {
            double worst = 0;
            int bad = 0;
            for (int c = 0; c < VCF_FREQ_MAX + 31; c += 31) {
                int cut = c < VCF_FREQ_MAX ? c : VCF_FREQ_MAX - 1;
                CVcf f;
                f.setType(types[t]);
                noise(&buf[0], 2048, 12345 + c);
                for (int i = 2048; i < 4096; i++) buf[i] = 0;
                runFilter(f, types[t], cut, resos[r], &buf[0], 4096);
                for (int i = 0; i < 4096; i++) {
                    double a = fabs(buf[i]);
                    if (!(a < 1e9)) bad++;
                    else if (a > worst) worst = a;
                }
            }
            CHECK_EQ(bad, 0);
            if (resos[r] <= 0.9f) CHECK(worst < 10.0);
            else CHECK(worst <= 20.0);        // self-oscillation is clipped at +-20
        }
    }
}

/** @brief Without resonance the output dies away after the input stops (no limit cycles). */
TEST(vcf, output_decays_after_input_stops)
{
    t1::initEngine();
    static const int types[5] = {VCF_LPF12, VCF_LPF24, VCF_HPF12, VCF_BPF12, VCF_LPFDIODE};
    static const float resos[2] = {0, 0.5f};
    std::vector<float> buf(2048 + 8192);
    for (int t = 0; t < 5; t++) {
        for (int r = 0; r < 2; r++) {
            if (types[t] == VCF_LPFDIODE && r) continue;
            for (int c = 248; c < VCF_FREQ_MAX; c += 31) {
                CVcf f;
                f.setType(types[t]);
                noise(&buf[0], 2048, 99 + c);
                for (size_t i = 2048; i < buf.size(); i++) buf[i] = 0;
                runFilter(f, types[t], c, resos[r], &buf[0], (int)buf.size());
                double tail = 0;
                for (size_t i = buf.size() - 256; i < buf.size(); i++) tail = dmax(tail, fabs(buf[i]));
                CHECK(tail < 1e-3);
            }
        }
    }
}

/** @brief Above resonance 0.9 the outputs are clipped to +-20; at or below 0.9 they are not. */
TEST(vcf, clipping_above_resonance_0_9)
{
    t1::initEngine();
    static const int types[4] = {VCF_LPF12, VCF_LPF24, VCF_HPF12, VCF_BPF12};
    for (int t = 0; t < 4; t++) {
        // a loud step: the resonant output overshoots past 20 unless clipped
        std::vector<float> a(4096, 100.0f), b(4096, 100.0f);
        CVcf fa, fb;
        fa.setType(types[t]);
        fb.setType(types[t]);
        runFilter(fa, types[t], 200, 0.9f, &a[0], 4096);
        runFilter(fb, types[t], 200, 0.95f, &b[0], 4096);
        double ma = 0, mb = 0;
        for (int i = 0; i < 4096; i++) {
            ma = dmax(ma, fabs(a[i]));
            mb = dmax(mb, fabs(b[i]));
        }
        CHECK(ma > 20.0);
        CHECK(mb <= 20.0);
        CHECK(mb >= 19.99);
    }
}

/** @brief The SSE and scalar paths of the 12 dB filter give the same output and state. */
TEST(vcf, svf_sse_matches_scalar)
{
    t1::initEngine();
    SSEGuard g;
    static const int types[3] = {VCF_LPF12, VCF_HPF12, VCF_BPF12};
    static const float resos[3] = {0, 0.6f, 0.95f};
    for (int t = 0; t < 3; t++) {
        for (int r = 0; r < 3; r++) {
            std::vector<float> a(1024), b(1024);
            noise(&a[0], 1024, 7);
            b = a;
            CVcf fa, fb;
            fa.setType(types[t]);
            fb.setType(types[t]);
            CSignalDevice::m_bSSE = 0;
            runFilter(fa, types[t], 500, resos[r], &a[0], 1024);
            CSignalDevice::m_bSSE = 1;
            runFilter(fb, types[t], 500, resos[r], &b[0], 1024);
            double md = 0;
            for (int i = 0; i < 1024; i++) md = dmax(md, fabs(a[i] - b[i]));
            CHECK(md < 1e-4);
            CHECK_CLOSE(fa.m_low, fb.m_low, 1e-4);
            CHECK_CLOSE(fa.m_band, fb.m_band, 1e-4);
        }
    }
}

/**
 * @brief When the +-20 clip engages, the scalar path clips the SVF state as well, the SSE path only the
 *        output: both outputs stay within +-20 but the stored state differs.
 */
TEST(vcf, svf_clip_state_scalar_vs_sse)
{
    t1::initEngine();
    SSEGuard g;
    std::vector<float> a(2048, 100.0f), b(2048, 100.0f);
    CVcf fa, fb;
    fa.setType(VCF_LPF12);
    fb.setType(VCF_LPF12);
    CSignalDevice::m_bSSE = 0;
    runFilter(fa, VCF_LPF12, 600, 0.95f, &a[0], 2048);
    CSignalDevice::m_bSSE = 1;
    runFilter(fb, VCF_LPF12, 600, 0.95f, &b[0], 2048);
    for (int i = 0; i < 2048; i++) {
        CHECK(fabs(a[i]) <= 20.0f);
        CHECK(fabs(b[i]) <= 20.0f);
    }
    CHECK_CLOSE(a[2047], 20.0, 0);
    CHECK_CLOSE(b[2047], 20.0, 0);
    CHECK(fabs(fa.m_low) <= 20.0f);
    CHECK_CLOSE(fb.m_low, 100.0 * (1.0 - 0.95 * 0.7), 0.1);   // unclipped DC level q * in
}

/** @brief The diode ladder also works without oversampling (fs > 48 kHz) and limits its cutoff to 0.45 fs. */
TEST(vcf, diode_ladder_high_and_low_rates)
{
    double lo = gainAt(VCF_LPFDIODE, C1K, 0, 200, 96000);
    double hi = gainAt(VCF_LPFDIODE, C1K, 0, 10000, 96000);
    CHECK(lo > 0.5);
    CHECK(hi < 0.01);
    // top cutoff at a low rate is clamped to 0.45 fs and stays stable
    double g = gainAt(VCF_LPFDIODE, 1023, 0.9f, 1000, 22050);
    CHECK(g > 0.1);
    CHECK(g < 10.0);
}

/** @brief Fixed-input characterization of each filter type (impulse response samples). */
TEST(vcf, impulse_response_characterization)
{
    t1::initEngine();
    static const int types[5] = {VCF_LPF12, VCF_LPF24, VCF_HPF12, VCF_BPF12, VCF_LPFDIODE};
    for (int t = 0; t < 5; t++) {
        CVcf f;
        f.setType(types[t]);
        float buf[64] = {1.0f};
        runFilter(f, types[t], C1K, 0.5f, buf, 64);
        double e = 0;
        for (int i = 0; i < 64; i++) e += buf[i] * buf[i];
        CHECK(e > 0);
        // causal: identical filters give identical results
        CVcf f2;
        f2.setType(types[t]);
        float buf2[64] = {1.0f};
        runFilter(f2, types[t], C1K, 0.5f, buf2, 64);
        for (int i = 0; i < 64; i++) CHECK(buf[i] == buf2[i]);
    }
    // LPF12 first sample: low = f*band ... = f^2 * q * (1 + ...) for an impulse
    CVcf f;
    f.setType(VCF_LPF12);
    float buf[2] = {1.0f, 0.0f};
    f.lowpass2(buf, C1K, 0.0f, buf, 2);
    float c = f.m_f;
    // first oversampled step: high = 1, band = c; second: low = c^2, high = 1 - c^2 - c, band = c + c(1 - c^2 - c)
    CHECK_CLOSE(buf[0], c * c, 1e-7);
}

/** @brief processEnvelopedSignal() targets cutoff + level + key tracking + unison, clamped to 0..1023. */
TEST(vcf, target_cutoff_and_clamping)
{
    t1::initEngine();
    CVcf f;
    float buf[32] = {0};
    f.m_freq = 400;
    f.m_kbdTrackOfs = 20;
    f.m_unisonOfs = -5;
    f.m_interpPos = 44100;         // no glide
    CHECK_EQ(f.processEnvelopedSignal(100, buf, 32, 0, 0), 0);
    CHECK_EQ(f.m_curFreq, 515);
    CHECK_EQ(f.m_lastFreq, 515);   // the 24 dB filter ran at that cutoff
    f.processEnvelopedSignal(2000, buf, 32, 0, 0);
    CHECK_EQ(f.m_curFreq, 1023);
    f.processEnvelopedSignal(-2000, buf, 32, 0, 0);
    CHECK_EQ(f.m_curFreq, 0);
}

/** @brief Positive LFO values move the cutoff towards the top, negative ones towards 0, proportionally. */
TEST(vcf, lfo_moves_cutoff)
{
    t1::initEngine();
    CVcf f;
    float buf[16] = {0};
    f.m_freq = 500;
    f.m_interpPos = 44100;
    f.processEnvelopedSignal(0, buf, 16, 512, 0);
    CHECK_EQ(f.m_curFreq, 762);    // 500 + 524 * 512 / 1024
    f.processEnvelopedSignal(0, buf, 16, -256, -256);
    CHECK_EQ(f.m_curFreq, 250);    // 500 - 500 * 512 / 1024
    f.processEnvelopedSignal(0, buf, 16, 1024, 0);
    CHECK_EQ(f.m_curFreq, 1023);
    f.processEnvelopedSignal(0, buf, 16, 0, -1024);
    CHECK_EQ(f.m_curFreq, 0);
}

/** @brief A cutoff change glides linearly from the start cutoff over 50 ms (fs / 20 samples). */
TEST(vcf, cutoff_glide)
{
    t1::initEngine();
    CVcf f;
    f.m_freq = 200;
    f.m_interpPos = 44100;
    float buf[2205];
    memset(buf, 0, sizeof(buf));
    f.processEnvelopedSignal(0, buf, 16, 0, 0);
    REQUIRE(f.m_curFreq == 200);
    f.setFreq(500.0f / 1023.0f);
    CHECK_EQ(f.m_freq, 500);
    int prev = f.m_curFreq;
    int total = 0;
    while (total < 2205) {
        f.processEnvelopedSignal(0, buf, 105, 0, 0);
        total += 105;
        CHECK(f.m_curFreq >= prev);
        CHECK(f.m_curFreq <= 500);
        if (total == 1050) CHECK(f.m_curFreq > 300 && f.m_curFreq < 360);
        prev = f.m_curFreq;
    }
    CHECK(f.m_curFreq > 480);
    f.processEnvelopedSignal(0, buf, 16, 0, 0);
    CHECK_EQ(f.m_curFreq, 500);
}

/** @brief The velocity curve maps 0..127 to 0..116 (1 - exp(-1.1 v/127), normalized by 1 - exp(-1.3)). */
TEST(vcf, velocity_curve)
{
    t1::initEngine();
    CVcf f;
    f.noteOn(48, 0);
    CHECK_EQ(f.m_velocity, 0);
    f.noteOn(48, 127);
    CHECK_EQ(f.m_velocity, 116);
    f.noteOn(48, 64);
    CHECK_EQ(f.m_velocity, (int)((float)(1.0 - exp((64 / 127.0f) * -1.1)) / 0.7274682f * 127.0f));
    int prev = -1;
    for (int v = 0; v <= 127; v++) {
        f.noteOn(48, v);
        CHECK(f.m_velocity >= prev);
        CHECK(f.m_velocity <= 116);
        prev = f.m_velocity;
    }
    CHECK_EQ(f.getEnvStatus(), ENV_ATTACK);
    CHECK_EQ(f.m_interpPos, 44100 / 20);
}

/** @brief calcSetAmount() scales the amount by velocity and limits it to the remaining cutoff range. */
TEST(vcf, envelope_amount_limits)
{
    t1::initEngine();
    CVcf f;
    f.m_freq = 512;
    f.m_velocity = 127;
    f.m_amount = 0.4f;
    f.calcSetAmount();
    CHECK_CLOSE(f.CEnvelopedObject::m_gain, 0.4, 1e-6);
    CHECK_EQ(f.m_bInvert, 0);
    CHECK_EQ(f.m_peakLevel, 409);
    f.m_velocity = 0;
    f.calcSetAmount();
    CHECK_CLOSE(f.CEnvelopedObject::m_gain, 0.0, 0);
    f.setVelocitySensEnable(0);
    f.calcSetAmount();
    CHECK_CLOSE(f.CEnvelopedObject::m_gain, 0.4, 1e-6);
    // limited to the room above the cutoff ((1024 - 512) / 1024)
    f.m_amount = 0.9f;
    f.calcSetAmount();
    CHECK_CLOSE(f.CEnvelopedObject::m_gain, 0.5, 1e-6);
    // and below it: at least (room above - 1)
    f.m_amount = -0.9f;
    f.calcSetAmount();
    CHECK_CLOSE(f.CEnvelopedObject::m_gain, 0.5, 1e-6);
    CHECK_EQ(f.m_bInvert, 1);
    // key tracking and unison offsets reduce the room above
    f.m_amount = 0.9f;
    f.m_kbdTrackOfs = 100;
    f.m_unisonOfs = 12;
    f.calcSetAmount();
    CHECK_CLOSE(f.CEnvelopedObject::m_gain, 400 / 1024.0, 1e-6);
}

/** @brief setFreq()/setAmount() recompute the envelope amount only while the envelope runs. */
TEST(vcf, amount_recomputed_only_when_active)
{
    t1::initEngine();
    CVcf f;
    f.setVelocitySensEnable(0);
    f.setAmount(0.3f);
    CHECK_CLOSE(f.CEnvelopedObject::m_gain, 1.0, 0);   // still the default
    f.noteOn(48, 127);
    CHECK_CLOSE(f.CEnvelopedObject::m_gain, 0.3, 1e-6);
    f.setAmount(0.2f);
    CHECK_CLOSE(f.CEnvelopedObject::m_gain, 0.2, 1e-6);
    f.setAmount(0.9f);
    f.setFreq(0.75f);              // cutoff 767: room 257/1024
    CHECK_CLOSE(f.CEnvelopedObject::m_gain, 257 / 1024.0, 1e-6);
    f.noteOff();
    CHECK_EQ(f.getEnvStatus(), ENV_RELEASE);
}

/** @brief A note with a positive (or negative) envelope amount moves the cutoff by the envelope level. */
TEST(vcf, envelope_moves_cutoff)
{
    t1::initEngine();
    for (int sign = -1; sign <= 1; sign += 2) {
        CVcf f;
        f.setAttack(0.0f);
        f.setDecay(0.0f);
        f.setSustain(1.0f);
        f.setAmount(0.4f * sign);
        f.noteOn(48, 127);
        int peak = (int)(1024 * (float)(0.4f * 116 / 127.0f));
        CHECK_EQ(f.m_peakLevel, peak);
        float buf[64] = {0};
        int processed = 0;
        f.processSignalSub(64, buf, 0, 0, &processed);
        CHECK_EQ(f.getEnvStatus(), ENV_SUSTAIN);
        CHECK_EQ(f.m_curFreq, 512 + sign * peak);
    }
}

/** @brief Key tracking: 0 is off; amount 1 shifts the cutoff by an octave per octave around note 48. */
TEST(vcf, key_tracking)
{
    t1::initEngine();
    CVcf f;
    f.calcKbdTrack(84);
    CHECK_EQ(f.m_kbdTrackOfs, 0);
    f.m_kbdTrack = 1.0f;
    f.calcKbdTrack(60);
    CHECK_EQ(f.m_kbdTrackOfs, 101);    // 1024 * log(2) / log(16000/15) = 101.8
    f.calcKbdTrack(36);
    CHECK_EQ(f.m_kbdTrackOfs, -102);
    f.calcKbdTrack(48);
    CHECK_EQ(f.m_kbdTrackOfs, -1);     // truncation: note 48 is one step below the cutoff
    f.m_kbdTrack = 0.5f;
    f.calcKbdTrack(60);
    CHECK(f.m_kbdTrackOfs >= 58 && f.m_kbdTrackOfs <= 60);
    // the tracked cutoff is limited to 0..VCF_FREQ_MAX
    f.m_kbdTrack = 1.0f;
    f.m_freq = 1000;
    f.calcKbdTrack(127);
    CHECK_EQ(f.m_kbdTrackOfs, 24);
    f.m_freq = 10;
    f.calcKbdTrack(0);
    CHECK_EQ(f.m_kbdTrackOfs, -10);
}

/** @brief setKbdTrack() recomputes the offset for the current note only while the envelope runs. */
TEST(vcf, set_kbd_track_when_active)
{
    t1::initEngine();
    CVcf f;
    f.m_note = 60;
    f.setKbdTrack(1.0f);
    CHECK_EQ(f.m_kbdTrackOfs, 0);
    f.noteOn(60, 100);
    CHECK_EQ(f.m_kbdTrackOfs, 101);
    CHECK_EQ(f.m_note, 60);
    f.setKbdTrack(0.0f);
    CHECK_EQ(f.m_kbdTrackOfs, 0);
}

/** @brief The unison detune in cents converts to cutoff steps (1200 cents = 101 steps, truncated). */
TEST(vcf, unison_offset)
{
    t1::initEngine();
    CVcf f;
    f.setUnisonDetunedMod(1200.0f);
    CHECK_EQ(f.m_unisonOfs, 101);
    f.setUnisonDetunedMod(-50.0f);
    CHECK_EQ(f.m_unisonOfs, -4);
    f.setUnisonDetunedMod(0.0f);
    CHECK_EQ(f.m_unisonOfs, 0);
    f.setUnisonDetunedMod(-1200.0f);
    CHECK_EQ(f.m_unisonOfs, -101);
}

/** @brief The limiter leaves quiet signals alone and pulls a sustained loud signal down to 1.1. */
TEST(vcf, limiter)
{
    t1::initEngine();
    SSEGuard g;
    for (int sse = 0; sse < 2; sse++) {
        CVcf f;
        CSignalDevice::m_bSSE = sse;
        std::vector<float> buf(512);
        for (int i = 0; i < 512; i++) buf[i] = (float)sin(i * 0.1) * 1.05f;
        std::vector<float> orig = buf;
        f.processLimitter(&buf[0], 512);
        for (int i = 0; i < 512; i++) CHECK(buf[i] == orig[i]);
        CHECK_CLOSE(f.m_limitEnv, 0, 0);
        // sustained 2.0 (and -2.0): gain falls to 1.1 / 2
        for (int b = 0; b < 8; b++) {
            for (int i = 0; i < 512; i++) buf[i] = (i & 1) ? -2.0f : 2.0f;
            f.processLimitter(&buf[1], 511);
        }
        CHECK_CLOSE(buf[511], -1.1, 1e-3);
        CHECK_CLOSE(buf[510], 1.1, 1e-3);
        for (int i = 1; i < 512; i++) CHECK(fabs(buf[i]) <= 2.0f);
        CHECK(f.m_limitEnv > 0.89f);
        // released afterwards: the envelope decays back to zero
        for (int b = 0; b < 40; b++) {
            for (int i = 0; i < 512; i++) buf[i] = 0.5f;
            f.processLimitter(&buf[0], 512);
            for (int i = 0; i < 512; i++) CHECK(buf[i] <= 0.5f);
        }
        CHECK_CLOSE(f.m_limitEnv, 0, 0);
        CHECK_CLOSE(buf[511], 0.5, 0);
    }
}

/** @brief The limiter's SSE and scalar paths agree, including where the first loud sample lies. */
TEST(vcf, limiter_sse_matches_scalar)
{
    t1::initEngine();
    SSEGuard g;
    for (int at = 0; at < 12; at++) {
        std::vector<float> a(40, 0.3f), b;
        a[3 + at] = 3.0f;
        b = a;
        CVcf fa, fb;
        CSignalDevice::m_bSSE = 0;
        fa.processLimitter(&a[3], 37);
        CSignalDevice::m_bSSE = 1;
        fb.processLimitter(&b[3], 37);
        for (int i = 0; i < 40; i++) CHECK_CLOSE(a[i], b[i], 1e-6);
        CHECK_CLOSE(fa.m_limitEnv, fb.m_limitEnv, 1e-7);
        CHECK(a[3 + at] < 3.0f);
    }
}

/** @brief Distortion applies g x / (|x| d + 1) with d = 100 amount, g = d + 1 (odd, saturating). */
TEST(vcf, distortion_curve)
{
    t1::initEngine();
    SSEGuard g;
    for (int sse = 0; sse < 2; sse++) {
        CVcf f;
        CSignalDevice::m_bSSE = sse;
        float buf[21];
        for (int i = 0; i < 21; i++) buf[i] = (i - 10) * 0.1f;
        int processed = 0;
        f.postProcessSignal(buf, 21, &processed);   // distortion off: only the limiter (inactive)
        for (int i = 0; i < 21; i++) CHECK_CLOSE(buf[i], (i - 10) * 0.1f, 0);
        f.setDistortion(0.01f);
        f.postProcessSignal(buf, 21, &processed);
        for (int i = 0; i < 21; i++) {
            double x = (i - 10) * 0.1f;
            CHECK_CLOSE(buf[i], 2.0 * x / (fabs(x) + 1.0), 1e-6);
            CHECK_CLOSE(buf[i], -buf[20 - i], 1e-6);
            if (i > 0) CHECK(buf[i] > buf[i - 1]);
        }
        CHECK_CLOSE(buf[15], 2.0 * 0.5 / 1.5, 1e-6);
        // heavy drive approaches +-g/d
        f.setDistortion(1.0f);
        float big[8] = {0.5f, -0.5f, 0.2f, -0.2f, 1.0f, -1.0f, 0.01f, -0.01f};
        f.postProcessSignal(big, 8, &processed);
        for (int i = 0; i < 6; i++) CHECK(fabs(big[i]) > 0.95 && fabs(big[i]) < 1.02);
    }
}

/** @brief resetFilterBuf() stops the envelope and clears all filter, cache and limiter state. */
TEST(vcf, reset_filter_buf)
{
    t1::initEngine();
    CVcf f;
    f.noteOn(60, 100);
    std::vector<float> buf(512);
    noise(&buf[0], 512, 3);
    int processed;
    f.processSignalSub(512, &buf[0], 0, 0, &processed);
    f.m_low = f.m_band = f.m_high = 1.0f;
    f.m_z[0] = f.m_z[4] = 1.0;
    f.m_limitEnv = 0.5f;
    f.m_limitGain = 0.5f;
    f.m_interpPos = 0;
    f.resetFilterBuf();
    CHECK_EQ(f.getEnvStatus(), ENV_OFF);
    CHECK_EQ(f.m_curLevel, 0);
    CHECK_CLOSE(f.m_low + f.m_band + f.m_high, 0, 0);
    for (int i = 0; i < 4; i++) {
        CHECK_CLOSE(f.m_x[i], 0, 0);
        CHECK_CLOSE(f.m_y[i], 0, 0);
    }
    for (int i = 0; i < 5; i++) CHECK_CLOSE(f.m_z[i], 0, 0);
    CHECK_EQ(f.m_lastFreq, -1);
    CHECK_CLOSE(f.m_lastReso, 0, 0);
    CHECK_CLOSE(f.m_limitEnv, 0, 0);
    CHECK_CLOSE(f.m_limitGain, 1.0, 0);
    CHECK_EQ(f.m_interpPos, 44100 / 20);
}

/** @brief Through processSignal() the filter runs even with the envelope off and lowpasses its input. */
TEST(vcf, process_signal_chain)
{
    t1::initEngine();
    for (int hz = 100; hz <= 10000; hz *= 100) {
        CVcf f;
        SineSource src(hz, 0.5);
        f.inputDeviceIs(&src);
        f.setFreq(C1K / 1023.0f);
        f.setResonance(0.0f);
        std::vector<float> buf(4096);
        int processed = 1;
        for (int b = 0; b < 4; b++) {
            CHECK_EQ(f.processSignal(1024, &buf[b * 1024], &processed), 1024);
            CHECK_EQ(processed, 0);
        }
        double peak = 0;
        for (int i = 3072; i < 4096; i++) peak = dmax(peak, fabs(buf[i]));
        if (hz == 100) CHECK(peak > 0.4);
        else CHECK(peak < 0.002);
        CHECK_EQ(f.getEnvStatus(), ENV_OFF);
    }
}

/** @brief CBQF lowpass/highpass coefficients: unity DC (LPF) or Nyquist (HPF) gain and a zero at the other end. */
TEST(cbqf, lowpass_highpass_coefficients)
{
    CBQF lp = makeBqf(0, 1000, 0.7071f, 0);
    CHECK_CLOSE(bqfMag(lp, 0), 1.0, 1e-5);
    CHECK_CLOSE(bqfMag(lp, M_PI), 0.0, 1e-5);
    CHECK_CLOSE(bqfMag(lp, 2 * M_PI * 1000 / 44100), 0.7071, 1e-3);   // -3 dB at the cutoff (Q = 1/sqrt 2)
    CHECK_CLOSE(lp.m_b[0], lp.m_b[2], 0);
    CHECK_CLOSE(lp.m_b[1], 2 * lp.m_b[0], 1e-7);
    CBQF hp = makeBqf(1, 1000, 0.7071f, 0);
    CHECK_CLOSE(bqfMag(hp, 0), 0.0, 1e-5);
    CHECK_CLOSE(bqfMag(hp, M_PI), 1.0, 1e-5);
    CHECK_CLOSE(hp.m_a[1], lp.m_a[1], 0);
    CHECK_CLOSE(hp.m_a[2], lp.m_a[2], 0);
}

/** @brief CBQF bandpass types: constant peak gain (unity at the center) and constant skirt gain (Q at the center). */
TEST(cbqf, bandpass_coefficients)
{
    double w0 = 2 * M_PI * 2000 / 44100;
    CBQF bp = makeBqf(3, 2000, 2.0f, 0);
    CHECK_CLOSE(bqfMag(bp, w0), 1.0, 1e-4);
    CHECK_CLOSE(bqfMag(bp, 0), 0.0, 1e-6);
    CHECK(bqfMag(bp, w0 / 2) < 0.5);
    CBQF sk = makeBqf(2, 2000, 2.0f, 0);
    CHECK_CLOSE(bqfMag(sk, w0), 2.0, 1e-3);
    CHECK_CLOSE(sk.m_b[1], 0, 0);
}

/** @brief CBQF peaking EQ and low shelf: the gain in dB appears at the center / at DC. */
TEST(cbqf, peaking_and_shelf_coefficients)
{
    double w0 = 2 * M_PI * 1000 / 44100;
    CBQF pk = makeBqf(6, 1000, 1.0f, 6.0f);
    CHECK_CLOSE(bqfMag(pk, w0), pow(10.0, 6.0 / 20), 1e-3);
    CHECK_CLOSE(bqfMag(pk, 0), 1.0, 1e-4);
    CBQF cut = makeBqf(6, 1000, 1.0f, -12.0f);
    CHECK_CLOSE(bqfMag(cut, w0), pow(10.0, -12.0 / 20), 1e-3);
    CBQF ls = makeBqf(7, 300, 0.7071f, 6.0f);
    CHECK_CLOSE(bqfMag(ls, 0), pow(10.0, 6.0 / 20), 1e-3);
    CHECK_CLOSE(bqfMag(ls, M_PI), 1.0, 1e-3);
}

/** @brief CBQF::process() runs the direct-form recursion and limits the output to at most 2.0 (positive side). */
TEST(cbqf, process_and_positive_limit)
{
    CBQF lp = makeBqf(0, 2000, 0.7071f, 0);
    float buf[64];
    for (int i = 0; i < 64; i++) buf[i] = i == 0 ? 1.0f : 0.0f;
    CHECK_EQ(lp.process(64, buf), 64);
    // reference recursion
    double x1 = 0, x2 = 0, y1 = 0, y2 = 0;
    for (int i = 0; i < 64; i++) {
        double in = i == 0 ? 1.0 : 0.0;
        double out = lp.m_b[0] * in + lp.m_b[1] * x1 + lp.m_b[2] * x2 - lp.m_a[1] * y1 - lp.m_a[2] * y2;
        x2 = x1;
        x1 = in;
        y2 = y1;
        y1 = out;
        CHECK_CLOSE(buf[i], out, 1e-6);
    }
    CHECK_EQ(lp.process(0, buf), 0);
    // a DC level of 3 settles at 3 for negative input but is limited to 2 for positive input
    CBQF a = makeBqf(0, 2000, 0.7071f, 0), b = makeBqf(0, 2000, 0.7071f, 0);
    float pos[512], neg[512];
    for (int i = 0; i < 512; i++) {
        pos[i] = 3.0f;
        neg[i] = -3.0f;
    }
    a.process(512, pos);
    b.process(512, neg);
    CHECK_CLOSE(pos[511], 2.0, 0);
    CHECK_CLOSE(neg[511], -3.0, 1e-4);
    for (int i = 0; i < 512; i++) CHECK(pos[i] <= 2.0f);
}

/** @brief processEnvelopedSignal() runs the diode ladder for VCF_LPFDIODE, at the target cutoff. */
TEST(vcf, envelope_path_runs_diode_ladder)
{
    t1::initEngine();
    CVcf f, g;
    f.setType(VCF_LPFDIODE);
    g.setType(VCF_LPFDIODE);
    f.setResonance(0.6f);
    g.setResonance(0.6f);
    f.m_freq = 500;
    f.m_interpPos = 44100;     // no glide
    std::vector<float> a(512), b(512);
    noise(&a[0], 512, 3);
    b = a;
    CHECK_EQ(f.processEnvelopedSignal(0, &a[0], 512, 0, 0), 0);
    CHECK_EQ(f.m_curFreq, 500);
    g.lowpassDiodeLadder(&b[0], 500, g.m_resonance, &b[0], 512);
    int same = 1, changed = 0;
    std::vector<float> in(512);
    noise(&in[0], 512, 3);
    for (int i = 0; i < 512; i++) {
        if (a[i] != b[i]) same = 0;
        if (a[i] != in[i]) changed = 1;
    }
    CHECK(same);
    CHECK(changed);
}

/** @brief The scalar 12 dB paths clip at +-20 above resonance 0.9 for both signs (low, high and band pass). */
TEST(vcf, svf_scalar_clip_both_signs)
{
    t1::initEngine();
    SSEGuard g;
    static const int types[3] = {VCF_LPF12, VCF_HPF12, VCF_BPF12};
    for (int t = 0; t < 3; t++) {
        for (int sign = -1; sign <= 1; sign += 2) {
            std::vector<float> a(4096, 100.0f * sign);
            CVcf f;
            CSignalDevice::m_bSSE = 0;      // after the constructor (it detects SSE)
            f.setType(types[t]);
            runFilter(f, types[t], 200, 0.95f, &a[0], 4096);
            double mx = -1e9, mn = 1e9;
            for (int i = 0; i < 4096; i++) {
                mx = a[i] > mx ? a[i] : mx;
                mn = a[i] < mn ? a[i] : mn;
            }
            CHECK(mx <= 20.0 && mn >= -20.0);
            if (sign > 0) CHECK_CLOSE(mx, 20.0, 0);
            else CHECK_CLOSE(mn, -20.0, 0);
        }
    }
}

/**
 * @brief Unit square wave with a 128-sample period (-1 first).
 */
static float p0(int i)
{
    return (i / 64) % 2 ? 1.0f : -1.0f;
}

/** @brief Large inputs drive the diode ladder's input stage into hard saturation: bounded, odd-symmetric output. */
TEST(vcf, diode_ladder_saturates_large_input)
{
    t1::initEngine();
    std::vector<float> p(1024), n(1024);
    for (int i = 0; i < 1024; i++) {
        p[i] = 100.0f * p0(i);
        n[i] = -p[i];
    }
    CVcf a, b;
    a.setType(VCF_LPFDIODE);
    b.setType(VCF_LPFDIODE);
    runFilter(a, VCF_LPFDIODE, 600, 0.5f, &p[0], 1024);
    runFilter(b, VCF_LPFDIODE, 600, 0.5f, &n[0], 1024);
    double mx = 0;
    int sym = 1;
    for (int i = 0; i < 1024; i++) {
        CHECK(p[i] == p[i]);
        mx = dmax(mx, fabs(p[i]));
        if (p[i] != -n[i]) sym = 0;
    }
    CHECK(sym);
    // the same square wave at amplitude 1: a linear stage would give 100 times its output
    std::vector<float> q(1024);
    for (int i = 0; i < 1024; i++) q[i] = p0(i);
    CVcf c;
    c.setType(VCF_LPFDIODE);
    runFilter(c, VCF_LPFDIODE, 600, 0.5f, &q[0], 1024);
    double mx1 = 0;
    for (int i = 0; i < 1024; i++) mx1 = dmax(mx1, fabs(q[i]));
    CHECK(mx1 > 0.01);
    CHECK(mx > mx1);
    CHECK(mx < 5.0 * mx1);
}

/** @brief The SSE soft saturation handles a buffer that does not start on a 16-byte boundary (scalar head). */
TEST(vcf, distortion_unaligned_buffer)
{
    t1::initEngine();
    SSEGuard g;
    CSignalDevice::m_bSSE = 1;
    CVcf f;
    f.setDistortion(0.01f);
    float store[32] __attribute__((aligned(16)));
    float* buf = store + 1;
    for (int i = 0; i < 21; i++) buf[i] = (i - 10) * 0.1f;
    int processed = 0;
    f.postProcessSignal(buf, 21, &processed);
    for (int i = 0; i < 21; i++) {
        double x = (i - 10) * 0.1f;
        CHECK_CLOSE(buf[i], 2.0 * x / (fabs(x) + 1.0), 1e-6);
    }
}

/** @brief A target that returns to the glide's start cutoff (zero distance) jumps there in one long piece. */
TEST(vcf, glide_back_to_start_cutoff)
{
    t1::initEngine();
    CVcf f;
    f.m_freq = 200;
    f.m_interpPos = 44100;
    float buf[2205];
    memset(buf, 0, sizeof(buf));
    f.processEnvelopedSignal(0, buf, 16, 0, 0);
    REQUIRE(f.m_curFreq == 200);
    f.setFreq(500.0f / 1023.0f);
    REQUIRE(f.m_startFreq == 200);
    f.processEnvelopedSignal(0, buf, 105, 0, 0);
    CHECK(f.m_curFreq > 200);
    int pos = f.m_interpPos;
    // envelope level -300: target 200 = start, distance 0 -> one piece of the whole block
    f.processEnvelopedSignal(-300, buf, 1000, 0, 0);
    CHECK_EQ(f.m_curFreq, 200);
    CHECK_EQ(f.m_interpPos, pos + 1000);
}

/** @brief At a low sampling rate (8 kHz: 400-sample glide) a large cutoff jump glides in one-sample pieces. */
TEST(vcf, glide_steps_every_sample_at_low_rate)
{
    t1::initEngine();
    CVcf f;
    f.setSamplingFreq(8000);
    f.m_freq = 100;
    f.m_interpPos = 44100;
    float buf[64];
    memset(buf, 0, sizeof(buf));
    f.processEnvelopedSignal(0, buf, 16, 0, 0);
    REQUIRE(f.m_curFreq == 100);
    f.setFreq(900.0f / 1023.0f);
    int diff = f.m_freq - 100;
    REQUIRE(diff > 400);
    f.processEnvelopedSignal(0, buf, 10, 0, 0);
    CHECK_EQ(f.m_interpPos, 10);
    CHECK_EQ(f.m_curFreq, diff * 9 / 400 + 100);    // value of the last one-sample piece
    for (int i = 0; i < 10; i++) CHECK_FEQ(buf[i], 0.0f);
}
