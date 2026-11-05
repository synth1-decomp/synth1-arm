/**
 * @file test_common.cpp
 * @brief Unit tests for techpop/common.h: fpow(), the random generators and the critical section.
 */
#include "test.h"

#include "common.h"

TEST(common, fpow_matches_pow)
{
    CHECK_CLOSE(fpow(2.0, 10.0), 1024.0, 1e-9);
    CHECK_CLOSE(fpow(10.0, -1.0), 0.1, 1e-12);
    CHECK_CLOSE(fpow(2.0, 0.5), sqrt(2.0), 1e-12);
}

TEST(common, rand_is_deterministic_for_a_seed)
{
    CMyRand a, b;
    a.setSeed(1234);
    b.setSeed(1234);
    for (int i = 0; i < 100; i++) {
        CHECK_EQ(a.rand(), b.rand());
    }
}

TEST(common, critical_section_enter_and_leave)
{
    CMyCS cs;
    cs.enter();
    cs.leave();
    {
        CMyCSLock lock(&cs);
    }
    cs.enter();
    cs.leave();
    CHECK(true);
}

#include "engine.h"

TEST(engine, globals_initialize)
{
    S1G* g = t1::initEngine();
    REQUIRE(g != 0);
    CHECK(s1g == g);
    CHECK(t1::initEngine() == g);
}
