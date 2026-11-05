/**
 * @file engine.h
 * @brief Shared setup of the engine's process-wide globals for the tests.
 */
#pragma once

#include "synth1.h"

namespace t1 {

/**
 * @brief Makes s1g point at a test-owned S1G instance (constructed once), like the AU does.
 *
 * Call it at the start of any test that uses engine code depending on s1g (wavetables, the
 * frequency tables, the sound library, CSynthesizer1, ...). Calling it again is harmless.
 *
 * @return The global engine state (also stored in s1g).
 */
S1G* initEngine();

}  // namespace t1
