/**
 * @file test_main.cpp
 * @brief Test runner: runs the registered tests and reports the results.
 *
 * Usage: synth1_tests [filter ...]. With filters, only the tests whose "suite.name" contains
 * one of the filters run. Exit status 0 means every check passed.
 */
#include "test.h"

#include <signal.h>
#include <stdlib.h>
#include <unistd.h>

namespace t1 {

static TestCase* g_head = 0;
static TestCase* g_tail = 0;
static int g_passed = 0;
static int g_failed = 0;
static int g_testFailed = 0;

int registerTest(TestCase* tc)
{
    if (g_tail) g_tail->next = tc; else g_head = tc;
    g_tail = tc;
    return 0;
}

void fail(const char* file, int line, const char* msg)
{
    g_failed++;
    g_testFailed = 1;
    printf("    FAIL %s:%d: %s\n", file, line, msg);
}

void pass()
{
    g_passed++;
}

}  // namespace t1

static char g_current[256] = "";

/**
 * @brief SIGALRM handler: reports the test that exceeded the time limit and exits.
 *
 * @param sig  Signal number (unused).
 */
static void onTimeout(int sig)
{
    (void)sig;
    printf("FAIL %s: timed out after 60 s\n", g_current);
    fflush(stdout);
    _exit(2);
}

int main(int argc, char** argv)
{
    signal(SIGALRM, onTimeout);
    int tests = 0, failedTests = 0;
    for (t1::TestCase* tc = t1::g_head; tc; tc = tc->next) {
        char full[256];
        snprintf(full, sizeof full, "%s.%s", tc->suite, tc->name);
        bool run = argc < 2;
        for (int i = 1; i < argc && !run; i++) run = strstr(full, argv[i]) != 0;
        if (!run) continue;
        tests++;
        t1::g_testFailed = 0;
        snprintf(g_current, sizeof g_current, "%s", full);
        alarm(60);
        try {
            tc->fn();
        } catch (t1::Abort&) {
        } catch (...) {
            t1::fail(__FILE__, __LINE__, "unexpected exception");
        }
        alarm(0);
        printf("%s %s\n", t1::g_testFailed ? "FAIL" : "ok  ", full);
        fflush(stdout);
        if (t1::g_testFailed) failedTests++;
    }
    printf("\n%d tests, %d failed; %d checks passed, %d failed\n",
           tests, failedTests, t1::g_passed, t1::g_failed);
    return (failedTests || tests == 0) ? 1 : 0;
}
