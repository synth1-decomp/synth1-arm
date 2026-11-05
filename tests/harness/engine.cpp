/**
 * @file engine.cpp
 * @brief Shared setup of the engine's process-wide globals for the tests.
 */
#include "engine.h"

namespace t1 {

S1G* initEngine()
{
    static S1G* area = 0;
    if (!area) area = new S1G();
    s1g = area;
    return s1g;
}

}  // namespace t1
