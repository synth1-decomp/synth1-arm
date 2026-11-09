#pragma once

/**
 * @file double_math.h
 * @brief Keeps the engine's math calls in double precision when built against libc++.
 *
 * The engine was written for libstdc++, where the global math functions from <math.h> only take
 * double, so log(someFloat) promotes its argument and calls log(double). libc++'s <math.h> adds
 * float overloads, so the same source silently calls logf() instead. That still compiles, but it
 * changes the results: the filter, EQ, compressor and envelope coefficients all lose precision, and
 * (int)(log(1.0 / 32768) / log(0.5f)) gives 14 rather than 15.
 *
 * These macros restore the original overload resolution. They are included from common.h, for the
 * modern project only (SYNTH1_MODERN_BUILD). <cmath> and the other standard headers have to come
 * first because they use these names themselves. A file that calls these functions has to include
 * this header, directly or through common.h, or it will quietly get the float overloads.
 */
#include <cmath>
#include <complex>
#include <algorithm>
#include <map>
#include <string>
#include <vector>

#define SYNTH1_DOUBLE_MATH1(name, x) (::name((double)(x)))
#define SYNTH1_DOUBLE_MATH2(name, x, y) (::name((double)(x), (double)(y)))

#define acos(x)     SYNTH1_DOUBLE_MATH1(acos, x)
#define asin(x)     SYNTH1_DOUBLE_MATH1(asin, x)
#define atan(x)     SYNTH1_DOUBLE_MATH1(atan, x)
#define ceil(x)     SYNTH1_DOUBLE_MATH1(ceil, x)
#define cos(x)      SYNTH1_DOUBLE_MATH1(cos, x)
#define cosh(x)     SYNTH1_DOUBLE_MATH1(cosh, x)
#define exp(x)      SYNTH1_DOUBLE_MATH1(exp, x)
#define fabs(x)     SYNTH1_DOUBLE_MATH1(fabs, x)
#define floor(x)    SYNTH1_DOUBLE_MATH1(floor, x)
#define log(x)      SYNTH1_DOUBLE_MATH1(log, x)
#define log10(x)    SYNTH1_DOUBLE_MATH1(log10, x)
#define sin(x)      SYNTH1_DOUBLE_MATH1(sin, x)
#define sinh(x)     SYNTH1_DOUBLE_MATH1(sinh, x)
#define sqrt(x)     SYNTH1_DOUBLE_MATH1(sqrt, x)
#define tan(x)      SYNTH1_DOUBLE_MATH1(tan, x)
#define tanh(x)     SYNTH1_DOUBLE_MATH1(tanh, x)
#define atan2(x, y) SYNTH1_DOUBLE_MATH2(atan2, x, y)
#define fmod(x, y)  SYNTH1_DOUBLE_MATH2(fmod, x, y)
#define pow(x, y)   SYNTH1_DOUBLE_MATH2(pow, x, y)
