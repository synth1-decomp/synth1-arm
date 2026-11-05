/**
 * @file test_debug.cpp
 * @brief Unit tests for techpop/debug.h (edPrintf, dbgDraw, InitDebugWindow, dispClock2) and
 *        techpop/myasiodev.h (sound device stubs). On the Mac these produce no output, so the tests
 *        pin down the return values and that they are safe to call.
 */
#include "test.h"

#include "common.h"
#include "debug.h"
#include "myasiodev.h"

extern HINSTANCE ghInstance;

/**
 * @brief initSoundDevice()/freeSoundDevice() are stubs that return 0 and can be called repeatedly.
 */
TEST(myasiodev, stubs_return_zero)
{
    CHECK_EQ(initSoundDevice(), 0);
    CHECK_EQ(initSoundDevice(), 0);
    CHECK_EQ(freeSoundDevice(), 0);
    CHECK_EQ(freeSoundDevice(), 0);
}

/**
 * @brief edPrintf() formats into a local buffer and always returns 0.
 */
TEST(debug, ed_printf_returns_zero)
{
    CHECK_EQ(edPrintf("plain"), 0);
    CHECK_EQ(edPrintf("%d %s %.3f", 42, "abc", 1.5), 0);
    char big[3000];
    for (int i = 0; i < 2999; i++) big[i] = 'x';
    big[2999] = 0;
    CHECK_EQ(edPrintf("%s", big), 0);  // fits the 4096-byte buffer
}

/**
 * @brief dbgDraw() does nothing and returns 0.
 */
TEST(debug, dbg_draw_returns_zero)
{
    CHECK_EQ(dbgDraw(0.5f, -0.5f), 0);
    CHECK_EQ(dbgDraw(0.f, 0.f), 0);
}

/**
 * @brief InitDebugWindow() stores the instance handle in ghInstance.
 */
TEST(debug, init_debug_window_stores_handle)
{
    HINSTANCE saved = ghInstance;
    HINSTANCE h = (HINSTANCE)0x1234;
    InitDebugWindow(h);
    CHECK(ghInstance == h);
    InitDebugWindow(saved);
    CHECK(ghInstance == saved);
}

/**
 * @brief dispClock2() accumulates measurements and wraps its statistics (after more than 44100
 *        samples or 1024 blocks) without crashing; it has no observable output on the Mac.
 */
TEST(debug, disp_clock2_accumulates_and_wraps)
{
    // 1024-block wrap with tiny blocks, then the 44100-sample wrap with large blocks.
    for (int i = 0; i < 2100; i++) dispClock2(1, 100 + (i % 13));
    for (int i = 0; i < 100; i++) dispClock2(512, 512LL * (1000 + (i % 7) * 300));
    // an outlier block (more than twice the mean) is handled too
    dispClock2(256, 256LL * 100000);
    for (int i = 0; i < 200; i++) dispClock2(256, 256LL * 50);
    CHECK(true);
}
