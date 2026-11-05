#pragma once

/**
 * @file fftwin.h
 * @brief FFT spectrum window (CFFTWin); only the power switch is implemented.
 */

/**
 * @brief FFT spectrum window.  On the Mac only the power switch survived;
 *        the drawing code is Windows-only.
 */
class CFFTWin {
public:
    /**
     * @brief Sets the power flag.
     */
    void powerOn();

    /**
     * @brief Clears the power flag and the buffer pointer (the buffer is not freed).
     */
    void powerOff();

protected:
    char _pad_0000[0x288];          // +0x000
    int m_bPower;                   // +0x288
    char _pad_028c[0x410 - 0x28c];  // +0x28c
    void* m_pBuf;                   // +0x410
};
