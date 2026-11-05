/**
 * @file test_phatcont.cpp
 * @brief Unit tests for techpop/phatcont.h (CPhatCont): level to coefficient/gain mapping,
 *        impulse and DC response, release time and the silence tail.
 */
#include "test.h"

#include <math.h>
#include <string.h>
#include <vector>

#include "phatcont.h"

namespace {

/**
 * @brief Mono source that plays samples and then zeros, or reports silence after the data.
 */
class MonoSource : public CSignalDevice {
public:
    std::vector<float> data;
    int pos;
    int silentAfterEnd;
    MonoSource() : pos(0), silentAfterEnd(0) {}
    virtual int processSignal(int size, float* pBuf, int* pProcessed)
    {
        if (silentAfterEnd && pos >= (int)data.size()) {
            pos += size;
            *pProcessed = 1;
            return size;
        }
        for (int i = 0; i < size; i++, pos++) pBuf[i] = pos < (int)data.size() ? data[pos] : 0.0f;
        *pProcessed = 0;
        return size;
    }
};

}  // namespace

/** @brief Level 0 is off: coefficient 0, gain 1, release 2; the signal passes unchanged. */
TEST(phatcont, level_zero_is_passthrough)
{
    CPhatCont p;
    CHECK(p.m_coef == 0.0f);
    CHECK(p.m_gain == 1.0f);
    CHECK_EQ(p.m_releaseTime, 2);
    MonoSource src;
    src.data.assign(64, 0.0f);
    for (int i = 0; i < 64; i++) src.data[i] = (float)sin(i * 0.3);
    p.inputDeviceIs(&src);
    float b[64];
    int processed = 0;
    CHECK_EQ(p.processSignal(64, b, &processed), 64);
    int same = 1;
    for (int i = 0; i < 64; i++) if (b[i] != src.data[i]) same = 0;
    CHECK(same);
}

/** @brief coef = sqrt(level) (capped at 0.99), gain = 1 - 0.9 coef^2, release = log(1/32768)/log(coef) + 2. */
TEST(phatcont, level_mapping)
{
    CPhatCont p;
    p.setPhatLevel(0.25f);
    CHECK_CLOSE(p.m_coef, 0.5, 1e-7);
    CHECK_CLOSE(p.m_gain, 0.775, 1e-7);
    CHECK_EQ(p.m_releaseTime, 17);
    p.setPhatLevel(1.0f);
    CHECK_CLOSE(p.m_coef, 0.99, 1e-7);
    CHECK_CLOSE(p.m_gain, 1.0 - 0.9 * 0.99 * 0.99, 1e-6);
    CHECK_EQ(p.m_releaseTime, (int)(log(1.0 / 32768) / log((double)0.99f)) + 2);
    p.setPhatLevel(0.81f);
    CHECK_CLOSE(p.m_coef, 0.9, 1e-6);
}

/** @brief The impulse response is gain * coef^k and the DC gain is gain / (1 - coef). */
TEST(phatcont, impulse_and_dc_response)
{
    CPhatCont p;
    p.setPhatLevel(0.25f);
    MonoSource src;
    src.data.assign(16, 0.0f);
    src.data[0] = 1.0f;
    p.inputDeviceIs(&src);
    float b[16];
    int processed = 0;
    p.processSignal(16, b, &processed);
    for (int k = 0; k < 16; k++) CHECK_CLOSE(b[k], 0.775 * pow(0.5, k), 1e-7);

    CPhatCont q;
    q.setPhatLevel(0.25f);
    MonoSource dc;
    dc.data.assign(256, 1.0f);
    q.inputDeviceIs(&dc);
    float c[256];
    q.processSignal(256, c, &processed);
    CHECK_CLOSE(c[255], 0.775 / 0.5, 1e-5);
}

/** @brief Without an input device the block is flagged silent. */
TEST(phatcont, no_input_is_silent)
{
    CPhatCont p;
    p.setPhatLevel(0.5f);
    float b[4];
    int processed = 0;
    CHECK_EQ(p.processSignal(4, b, &processed), 4);
    CHECK_EQ(processed, 1);
}

/** @brief Silent input: the decaying tail continues for the release time, then the state is cleared and the buffer left alone. */
TEST(phatcont, silence_tail)
{
    CPhatCont p;
    p.setPhatLevel(0.25f);   // release 17 samples
    MonoSource src;
    src.data.assign(8, 1.0f);
    src.silentAfterEnd = 1;
    p.inputDeviceIs(&src);
    float b[8];
    int processed = 0;
    p.processSignal(8, b, &processed);
    float s = p.m_state;
    CHECK(s > 1.9f);   // 2 * (1 - 0.5^8)
    // first silent block: tail continues from the state
    for (int i = 0; i < 8; i++) b[i] = 9.0f;
    p.processSignal(8, b, &processed);
    CHECK_EQ(processed, 0);
    CHECK_CLOSE(b[0], s * 0.5 * 0.775, 1e-6);
    CHECK_CLOSE(b[7], s * pow(0.5, 8) * 0.775, 1e-7);
    // entry counts 8, 16 render; 24 > 17 stops
    p.processSignal(8, b, &processed);
    CHECK_EQ(processed, 0);
    p.processSignal(8, b, &processed);
    CHECK_EQ(processed, 0);
    for (int i = 0; i < 8; i++) b[i] = 9.0f;
    p.processSignal(8, b, &processed);
    CHECK_EQ(processed, 1);
    CHECK(b[0] == 9.0f);
    CHECK(p.m_state == 0.0f);
}

/** @brief Turning the level back to 0 clears the state. */
TEST(phatcont, level_zero_clears_state)
{
    CPhatCont p;
    p.setPhatLevel(0.5f);
    p.m_state = 0.75f;
    p.setPhatLevel(0.0f);
    CHECK(p.m_state == 0.0f);
    CHECK(p.m_coef == 0.0f);
}
