/**
 * @file test.h
 * @brief Minimal C++98 unit test harness for the Synth1 engine tests.
 *
 * Tests register themselves with TEST(suite, name) and use the CHECK macros. A failing CHECK
 * reports file/line and the test continues; REQUIRE stops the current test. The runner
 * (test_main.cpp) runs all tests, or those whose "suite.name" contains one of the
 * command-line arguments, and exits non-zero if any check failed.
 */
#pragma once

#include <math.h>
#include <stdio.h>
#include <string.h>

namespace t1 {

/**
 * @brief One registered test case.
 */
struct TestCase {
    const char* suite;
    const char* name;
    void (*fn)();
    TestCase* next;
};

/**
 * @brief Adds a test case to the global list.
 *
 * @param tc  Statically allocated test case to register.
 * @return Always 0 (used to run the registration at static-initialization time).
 */
int registerTest(TestCase* tc);

/**
 * @brief Records a failed check and prints its location and message.
 *
 * @param file  Source file of the check.
 * @param line  Line of the check.
 * @param msg   Description of the failure.
 */
void fail(const char* file, int line, const char* msg);

/**
 * @brief Counts a passed check.
 */
void pass();

/**
 * @brief Thrown by REQUIRE to abort the current test.
 */
struct Abort {};

}  // namespace t1

#define T1_CAT2(a, b) a##b
#define T1_CAT(a, b) T1_CAT2(a, b)

/**
 * @brief Defines and registers a test function.
 */
#define TEST(suite, name)                                                              \
    static void T1_CAT(t1_fn_, T1_CAT(suite, T1_CAT(_, name)))();                     \
    static t1::TestCase T1_CAT(t1_tc_, T1_CAT(suite, T1_CAT(_, name))) =              \
        {#suite, #name, &T1_CAT(t1_fn_, T1_CAT(suite, T1_CAT(_, name))), 0};          \
    static int T1_CAT(t1_reg_, T1_CAT(suite, T1_CAT(_, name))) =                      \
        t1::registerTest(&T1_CAT(t1_tc_, T1_CAT(suite, T1_CAT(_, name))));            \
    static void T1_CAT(t1_fn_, T1_CAT(suite, T1_CAT(_, name)))()

/**
 * @brief Checks that a condition is true.
 */
#define CHECK(cond)                                                                    \
    do { if (cond) t1::pass(); else t1::fail(__FILE__, __LINE__, "CHECK(" #cond ")"); } while (0)

/**
 * @brief Checks that a condition is true and aborts the test if it is not.
 */
#define REQUIRE(cond)                                                                  \
    do { if (cond) t1::pass(); else { t1::fail(__FILE__, __LINE__, "REQUIRE(" #cond ")"); \
         throw t1::Abort(); } } while (0)

/**
 * @brief Checks that two integer-like values are equal (printed as long long).
 */
#define CHECK_EQ(a, b)                                                                 \
    do { long long t1a_ = (long long)(a), t1b_ = (long long)(b);                       \
         if (t1a_ == t1b_) t1::pass(); else { char t1m_[512];                          \
         snprintf(t1m_, sizeof t1m_, "CHECK_EQ(%s, %s): %lld != %lld", #a, #b, t1a_, t1b_); \
         t1::fail(__FILE__, __LINE__, t1m_); } } while (0)

/**
 * @brief Checks that two floating-point values differ by at most tol.
 */
#define CHECK_CLOSE(a, b, tol)                                                         \
    do { double t1a_ = (double)(a), t1b_ = (double)(b), t1t_ = (double)(tol);          \
         if (fabs(t1a_ - t1b_) <= t1t_) t1::pass(); else { char t1m_[512];             \
         snprintf(t1m_, sizeof t1m_, "CHECK_CLOSE(%s, %s, %s): %.9g vs %.9g (diff %.3g)", \
                  #a, #b, #tol, t1a_, t1b_, fabs(t1a_ - t1b_));                         \
         t1::fail(__FILE__, __LINE__, t1m_); } } while (0)

/**
 * @brief Checks that two C strings are equal.
 *
 * Both arguments are copied before comparing, so temporaries (e.g. std::string::c_str() of a
 * temporary) are safe to pass.
 */
#define CHECK_STREQ(a, b)                                                              \
    do { char t1a_[1024], t1b_[1024]; const char* t1pa_ = (a); const char* t1pb_ = (b); \
         int t1na_ = t1pa_ == 0, t1nb_ = t1pb_ == 0;                                   \
         snprintf(t1a_, sizeof t1a_, "%s", t1pa_ ? t1pa_ : "(null)");                  \
         snprintf(t1b_, sizeof t1b_, "%s", t1pb_ ? t1pb_ : "(null)");                  \
         if (!t1na_ && !t1nb_ && strcmp(t1a_, t1b_) == 0) t1::pass(); else { char t1m_[2304]; \
         snprintf(t1m_, sizeof t1m_, "CHECK_STREQ(%s, %s): \"%s\" != \"%s\"", #a, #b, t1a_, t1b_); \
         t1::fail(__FILE__, __LINE__, t1m_); } } while (0)

/**
 * @brief Checks that two floating-point values are exactly equal (compared as double).
 */
#define CHECK_FEQ(a, b) CHECK_CLOSE(a, b, 0.0)
