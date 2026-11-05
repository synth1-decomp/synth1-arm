/**
 * @file test_common2.cpp
 * @brief More unit tests for techpop/common.h: CMyRand sequence characterization, CRandomTable
 *        (including the global g_rnd table), ftoi() rounding and the named color table helpers.
 */
#include "test.h"

#include <stdlib.h>
#include <string.h>

#include "common.h"

/**
 * @brief The default seed is 1 and the first values match the MSVC CRT rand() sequence.
 */
TEST(common2, rand_default_seed_matches_msvc)
{
    CMyRand r;
    CHECK_EQ(r.m_seed, 1);
    const int expect[] = {41, 18467, 6334, 26500, 19169, 15724, 11478, 29358, 26962, 24464};
    for (int i = 0; i < 10; i++) CHECK_EQ(r.rand(), expect[i]);
}

/**
 * @brief rand() applies seed = seed * 214013 + 2531011 (mod 2^32) and returns bits 16..30.
 */
TEST(common2, rand_follows_lcg_formula)
{
    CMyRand r;
    r.setSeed(0xDEADBEEFu);
    unsigned int s = 0xDEADBEEFu;
    int bad = 0;
    for (int i = 0; i < 1000; i++) {
        s = s * 214013u + 2531011u;
        int v = r.rand();
        if (v != (int)((s >> 16) & 0x7fff) || r.m_seed != s) bad++;
    }
    CHECK_EQ(bad, 0);
}

/**
 * @brief Characterization values for seed 0 and a seed whose update wraps around 2^32.
 */
TEST(common2, rand_characterization)
{
    CMyRand r;
    r.setSeed(0);
    CHECK_EQ(r.rand(), 38);      // 2531011 >> 16
    CHECK_EQ(r.m_seed, 2531011u);
    r.setSeed(0xFFFFFFFFu);
    CHECK_EQ(r.rand(), ((0xFFFFFFFFu * 214013u + 2531011u) >> 16) & 0x7fff);
}

/**
 * @brief setSeed() restarts the sequence; generators are independent of each other.
 */
TEST(common2, rand_set_seed_restarts)
{
    CMyRand a, b;
    int first[5];
    a.setSeed(777);
    for (int i = 0; i < 5; i++) first[i] = a.rand();
    b.rand();  // advancing b does not affect a
    a.setSeed(777);
    for (int i = 0; i < 5; i++) CHECK_EQ(a.rand(), first[i]);
}

/**
 * @brief rand() stays in 0..MYRAND_MAX and is roughly uniform (each eighth of the range gets
 *        12.5 % +- 1 % of 80000 draws, and both ends are reached).
 */
TEST(common2, rand_range_and_distribution)
{
    CMyRand r;
    r.setSeed(12345);
    int bins[8] = {0};
    int lo = 1 << 30, hi = -1;
    const int N = 80000;
    for (int i = 0; i < N; i++) {
        int v = r.rand();
        if (v < lo) lo = v;
        if (v > hi) hi = v;
        if (v >= 0 && v <= MYRAND_MAX) bins[v >> 12]++;
    }
    CHECK(lo >= 0);
    CHECK(hi <= MYRAND_MAX);
    CHECK(lo < 10);
    CHECK(hi > MYRAND_MAX - 10);
    for (int b = 0; b < 8; b++) CHECK(abs(bins[b] - N / 8) < N / 100);
}

/**
 * @brief A CRandomTable holds rand()/32767 of a seed-1 generator, so it equals the global g_rnd
 *        table and every entry is within [0, 1].
 */
TEST(common2, random_table_contents)
{
    CRandomTable t;
    CMyRand r;
    int bad = 0, outOfRange = 0, diffGlobal = 0;
    for (int i = 0; i < RND_TABLE_SIZE; i++) {
        float expect = (float)(r.rand() / 32767.0);
        if (t.m_table[i] != expect) bad++;
        if (t.m_table[i] < 0.f || t.m_table[i] > 1.f) outOfRange++;
        if (g_rnd.m_table[i] != t.m_table[i]) diffGlobal++;
    }
    CHECK_EQ(bad, 0);
    CHECK_EQ(outOfRange, 0);
    CHECK_EQ(diffGlobal, 0);
    CHECK_CLOSE(t.m_table[0], 41.0 / 32767.0, 1e-7);
    CHECK_CLOSE(t.m_table[1], 18467.0 / 32767.0, 1e-7);
    CHECK_EQ(sizeof(CRandomTable), 0x808);
}

/**
 * @brief The table's own generators start at seed 1, independent of the generator used to fill
 *        the table.
 */
TEST(common2, random_table_generators_start_fresh)
{
    CRandomTable t;
    CHECK_EQ(t.m_rand0.m_seed, 1);
    CHECK_EQ(t.m_rand.m_seed, 1);
    CHECK_EQ(t.m_rand.rand(), 41);
}

/**
 * @brief The table mean is close to 0.5 (sanity of the distribution).
 */
TEST(common2, random_table_mean)
{
    double sum = 0;
    for (int i = 0; i < RND_TABLE_SIZE; i++) sum += g_rnd.m_table[i];
    CHECK_CLOSE(sum / RND_TABLE_SIZE, 0.5, 0.05);
}

/**
 * @brief ftoi() rounds to nearest with ties to even (default FPU mode), unlike a C cast.
 */
TEST(common2, ftoi_rounds_to_nearest_even)
{
    CHECK_EQ(ftoi(0.4f), 0);
    CHECK_EQ(ftoi(0.6f), 1);
    CHECK_EQ(ftoi(0.5f), 0);
    CHECK_EQ(ftoi(1.5f), 2);
    CHECK_EQ(ftoi(2.5f), 2);
    CHECK_EQ(ftoi(-0.6f), -1);
    CHECK_EQ(ftoi(-1.5f), -2);
    CHECK_EQ(ftoi(-2.5f), -2);
    CHECK_EQ(ftoi(1234567.f), 1234567);
    CHECK_EQ(ftoi(2.7f), 3);
    CHECK_EQ((int)2.7f, 2);
}

/**
 * @brief MIN/MAX and PI behave as expected.
 */
TEST(common2, macros)
{
    CHECK_EQ(MIN(3, 5), 3);
    CHECK_EQ(MAX(3, 5), 5);
    CHECK_EQ(MIN(-1, -2), -2);
    CHECK_CLOSE(PI, 3.14159265f, 1e-6);
}

/**
 * @brief The named color table holds the six documented colors as RGB() values.
 */
TEST(common2, color_table_entries)
{
    CHECK_STREQ(g_defineColors[0].name, "red");
    CHECK_EQ(g_defineColors[0].color, 0x0000FF);
    CHECK_STREQ(g_defineColors[1].name, "green");
    CHECK_EQ(g_defineColors[1].color, 0x00FF00);
    CHECK_STREQ(g_defineColors[2].name, "blue");
    CHECK_EQ(g_defineColors[2].color, 0xFF0000);
    CHECK_STREQ(g_defineColors[3].name, "yellow");
    CHECK_EQ(g_defineColors[3].color, RGB(255, 255, 0));
    CHECK_STREQ(g_defineColors[4].name, "magenta");
    CHECK_EQ(g_defineColors[4].color, RGB(255, 0, 255));
    CHECK_STREQ(g_defineColors[5].name, "cyan");
    CHECK_EQ(g_defineColors[5].color, RGB(0, 255, 255));
}

/**
 * @brief getColorFromString() maps known names to their colors and anything else (including
 *        different case) to 0xffffffff.
 */
TEST(common2, color_from_string)
{
    char name[32];
    strcpy(name, "red");
    CHECK_EQ(getColorFromString(name), RGB(255, 0, 0));
    strcpy(name, "cyan");
    CHECK_EQ(getColorFromString(name), RGB(0, 255, 255));
    strcpy(name, "Red");
    CHECK_EQ(getColorFromString(name), 0xffffffffu);
    strcpy(name, "default");
    CHECK_EQ(getColorFromString(name), 0xffffffffu);
    strcpy(name, "");
    CHECK_EQ(getColorFromString(name), 0xffffffffu);
    strcpy(name, "redx");
    CHECK_EQ(getColorFromString(name), 0xffffffffu);
}

/**
 * @brief getColorStringFromVal() maps table colors to their names, and 0xffffffff or unknown
 *        colors to "default".
 */
TEST(common2, color_string_from_val)
{
    char s[32];
    getColorStringFromVal(s, RGB(0, 0, 255));
    CHECK_STREQ(s, "blue");
    getColorStringFromVal(s, RGB(255, 0, 255));
    CHECK_STREQ(s, "magenta");
    getColorStringFromVal(s, 0xffffffff);
    CHECK_STREQ(s, "default");
    getColorStringFromVal(s, RGB(1, 2, 3));
    CHECK_STREQ(s, "default");
    getColorStringFromVal(s, 0);
    CHECK_STREQ(s, "default");
}

/**
 * @brief Name -> color -> name round trips for every table entry.
 */
TEST(common2, color_round_trip)
{
    const char* names[] = {"red", "green", "blue", "yellow", "magenta", "cyan"};
    for (int i = 0; i < 6; i++) {
        char in[32], out[32];
        strcpy(in, names[i]);
        COLORREF c = getColorFromString(in);
        CHECK(c != 0xffffffff);
        getColorStringFromVal(out, c);
        CHECK_STREQ(out, names[i]);
    }
}
