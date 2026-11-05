/**
 * @file test_fftwin.cpp
 * @brief Unit tests for techpop/fftwin.h (CFFTWin power switch) and the Ooura FFT routines of
 *        techpop/fft4g.c that the spectrum window was built on.
 *
 * fft4g.c has no header in src/, so the prototypes documented at the top of that file are declared
 * here.
 */
#include "test.h"

#include <string.h>

#include "fftwin.h"

extern "C" {
void cdft(int n, int isgn, double* a, int* ip, double* w);
void rdft(int n, int isgn, double* a, int* ip, double* w);
void ddct(int n, int isgn, double* a, int* ip, double* w);
}

namespace {

/**
 * @brief CFFTWin with read access to its protected state.
 */
class FFTWinProbe : public CFFTWin {
public:
    int power() const { return m_bPower; }
    void* buf() const { return m_pBuf; }
    void setBuf(void* p) { m_pBuf = p; }
};

const double kPi = 3.14159265358979323846;

/**
 * @brief Fills a with a deterministic test signal.
 */
void fillSignal(double* a, int n)
{
    for (int j = 0; j < n; j++) {
        a[j] = sin(0.37 * j) + 0.5 * cos(1.3 * j + 0.2) + 0.01 * (j % 7);
    }
}

}  // namespace

/**
 * @brief powerOn() sets the power flag; powerOff() clears it and the buffer pointer.
 */
TEST(fftwin, power_switch)
{
    FFTWinProbe w;
    memset(&w, 0, sizeof w);
    CHECK_EQ(w.power(), 0);
    w.powerOn();
    CHECK_EQ(w.power(), 1);
    int dummy;
    w.setBuf(&dummy);
    w.powerOff();
    CHECK_EQ(w.power(), 0);
    CHECK(w.buf() == 0);
    w.powerOn();
    CHECK_EQ(w.power(), 1);
    CHECK(w.buf() == 0);
}

/**
 * @brief The class layout matches the documented offsets (power flag at +0x288, buffer at +0x410).
 */
TEST(fftwin, layout)
{
    FFTWinProbe w;
    memset(&w, 0, sizeof w);
    w.powerOn();
    CHECK_EQ(*(int*)((char*)&w + 0x288), 1);
    int dummy;
    w.setBuf(&dummy);
    CHECK(*(void**)((char*)&w + 0x410) == &dummy);
    CHECK_EQ(sizeof(CFFTWin), 0x418);
}

/**
 * @brief cdft(2n, 1) of a unit impulse at 0 gives a flat spectrum of ones.
 */
TEST(fft4g, cdft_impulse_is_flat)
{
    const int n = 16;
    double a[2 * n];
    int ip[2 + 8];
    double w[n / 2];
    memset(a, 0, sizeof a);
    a[0] = 1.0;
    ip[0] = 0;
    cdft(2 * n, 1, a, ip, w);
    for (int k = 0; k < n; k++) {
        CHECK_CLOSE(a[2 * k], 1.0, 1e-12);
        CHECK_CLOSE(a[2 * k + 1], 0.0, 1e-12);
    }
}

/**
 * @brief cdft matches a direct DFT: isgn = 1 uses exp(+2 pi i jk/n), isgn = -1 exp(-2 pi i jk/n).
 */
TEST(fft4g, cdft_matches_direct_dft)
{
    const int n = 32;
    double x[2 * n];
    for (int j = 0; j < n; j++) {
        x[2 * j] = cos(0.3 * j) + 0.1 * j;
        x[2 * j + 1] = sin(0.7 * j);
    }
    for (int sgn = -1; sgn <= 1; sgn += 2) {
        double a[2 * n];
        int ip[2 + 8];
        double w[n / 2];
        memcpy(a, x, sizeof a);
        ip[0] = 0;
        cdft(2 * n, sgn, a, ip, w);
        for (int k = 0; k < n; k++) {
            double re = 0, im = 0;
            for (int j = 0; j < n; j++) {
                double ph = sgn * 2.0 * kPi * j * k / n;
                re += x[2 * j] * cos(ph) - x[2 * j + 1] * sin(ph);
                im += x[2 * j] * sin(ph) + x[2 * j + 1] * cos(ph);
            }
            CHECK_CLOSE(a[2 * k], re, 1e-9);
            CHECK_CLOSE(a[2 * k + 1], im, 1e-9);
        }
    }
}

/**
 * @brief cdft(-1) followed by cdft(+1) and scaling by 1/n restores the input; the tables
 *        initialized by the first call are reused by the second.
 */
TEST(fft4g, cdft_round_trip)
{
    const int n = 256;
    double x[2 * n], a[2 * n];
    int ip[2 + 16];
    double w[n / 2];
    fillSignal(x, 2 * n);
    memcpy(a, x, sizeof a);
    ip[0] = 0;
    cdft(2 * n, -1, a, ip, w);
    CHECK(ip[0] != 0);
    cdft(2 * n, 1, a, ip, w);
    double maxErr = 0;
    for (int j = 0; j < 2 * n; j++) {
        double e = fabs(a[j] / n - x[j]);
        if (e > maxErr) maxErr = e;
    }
    CHECK(maxErr < 1e-12);
}

/**
 * @brief rdft(n, 1) matches the documented real DFT layout (R[k] at a[2k], I[k] at a[2k+1],
 *        R[n/2] at a[1]).
 */
TEST(fft4g, rdft_matches_direct_dft)
{
    const int n = 64;
    double x[n], a[n];
    int ip[2 + 8];
    double w[n / 2];
    fillSignal(x, n);
    memcpy(a, x, sizeof a);
    ip[0] = 0;
    rdft(n, 1, a, ip, w);
    for (int k = 0; k <= n / 2; k++) {
        double re = 0, im = 0;
        for (int j = 0; j < n; j++) {
            re += x[j] * cos(2.0 * kPi * j * k / n);
            im += x[j] * sin(2.0 * kPi * j * k / n);
        }
        if (k == 0) {
            CHECK_CLOSE(a[0], re, 1e-9);
        } else if (k == n / 2) {
            CHECK_CLOSE(a[1], re, 1e-9);
        } else {
            CHECK_CLOSE(a[2 * k], re, 1e-9);
            CHECK_CLOSE(a[2 * k + 1], im, 1e-9);
        }
    }
}

/**
 * @brief A pure sine at bin 5 puts all its energy in bin 5 of the real DFT.
 */
TEST(fft4g, rdft_sine_peaks_at_its_bin)
{
    const int n = 128;
    double a[n];
    int ip[2 + 8];
    double w[n / 2];
    for (int j = 0; j < n; j++) a[j] = sin(2.0 * kPi * 5 * j / n);
    ip[0] = 0;
    rdft(n, 1, a, ip, w);
    for (int k = 1; k < n / 2; k++) {
        double mag = sqrt(a[2 * k] * a[2 * k] + a[2 * k + 1] * a[2 * k + 1]);
        if (k == 5) CHECK_CLOSE(mag, n / 2.0, 1e-9);
        else CHECK_CLOSE(mag, 0.0, 1e-9);
    }
    CHECK_CLOSE(a[0], 0.0, 1e-9);
    CHECK_CLOSE(a[1], 0.0, 1e-9);
}

/**
 * @brief rdft(n, 1) then rdft(n, -1) scaled by 2/n restores the input.
 */
TEST(fft4g, rdft_round_trip)
{
    const int n = 512;
    double x[n], a[n];
    int ip[2 + 16];
    double w[n / 2];
    fillSignal(x, n);
    memcpy(a, x, sizeof a);
    ip[0] = 0;
    rdft(n, 1, a, ip, w);
    rdft(n, -1, a, ip, w);
    double maxErr = 0;
    for (int j = 0; j < n; j++) {
        double e = fabs(a[j] * 2.0 / n - x[j]);
        if (e > maxErr) maxErr = e;
    }
    CHECK(maxErr < 1e-12);
}

/**
 * @brief ddct(n, -1) matches the documented DCT-II and ddct(n, 1) then scaling inverts it
 *        (a[0] halved, everything times 2/n).
 */
TEST(fft4g, ddct_matches_definition_and_inverts)
{
    const int n = 32;
    double x[n], a[n];
    int ip[2 + 8];
    double w[n * 5 / 4];
    fillSignal(x, n);
    memcpy(a, x, sizeof a);
    ip[0] = 0;
    ddct(n, -1, a, ip, w);
    for (int k = 0; k < n; k++) {
        double c = 0;
        for (int j = 0; j < n; j++) c += x[j] * cos(kPi * (j + 0.5) * k / n);
        CHECK_CLOSE(a[k], c, 1e-9);
    }
    a[0] *= 0.5;
    ddct(n, 1, a, ip, w);
    for (int j = 0; j < n; j++) CHECK_CLOSE(a[j] * 2.0 / n, x[j], 1e-12);
}
