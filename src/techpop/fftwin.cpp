/**
 * @file fftwin.cpp
 * @brief FFT spectrum window (CFFTWin): power switch only.
 */
#include "fftwin.h"

void CFFTWin::powerOn()
{
    m_bPower = 1;
}

void CFFTWin::powerOff()
{
    m_bPower = 0;
    m_pBuf = 0;
}
