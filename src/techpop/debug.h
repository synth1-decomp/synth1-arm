#pragma once

/**
 * @file debug.h
 * @brief Debug routines.
 *
 * On the Mac build the debug window does not exist: edPrintf only formats,
 * dbgDraw does nothing and dispClock2 computes its statistics without showing them.
 */

#ifdef __cplusplus
extern "C"{
#endif

/**
 * @brief Stores the module instance handle used by the debug window.
 *
 * @param hInst     Module instance handle (kept in the global ghInstance).
 */
void InitDebugWindow(HINSTANCE hInst);

/**
 * @brief Prints a printf-style formatted string to the debug window.
 *
 * On the Mac the text is formatted into a 4096-byte local buffer and discarded.
 * @param first     printf format string.
 * @param ...       Arguments for the format string.
 * @return Always 0.
 * @note The result must fit in 4096 bytes (vsprintf, no length check).
 */
int edPrintf(const char *first,...);            // print a string to the debug window

/**
 * @brief Accumulates a processing-time measurement and periodically summarizes it.
 *
 * Computes the clocks per sample of this block, and once more than 44100 samples
 * or 1024 blocks have been accumulated, computes the average block size, average
 * clocks per sample, the average excluding outliers (more than twice the mean),
 * the worst value and the mean relative deviation, then resets the accumulators.
 * The formatted text is not displayed on the Mac.
 * @param nSample   Number of samples processed in the measured block.
 * @param clock     CPU clocks spent on the block.
 * @note Keeps its state in function-static variables; not thread safe.
 */
void dispClock2(int nSample,LONGLONG clock);    // show clocks per sample

/**
 * @brief Plots a stereo signal value on the debug window.
 *
 * Does nothing on the Mac.
 * @param l     Left sample value.
 * @param r     Right sample value.
 * @return Always 0.
 */
int dbgDraw(float l,float r);                   // plot signal values

#ifdef __cplusplus
}
#endif
