#pragma once

/**
 * @file stepvoxwin.h
 * Step vox editor window (stub on the Mac).
 *
 * Declares CStepVoxWin, an MDI child window whose implementation is empty in this build.
 */

#include "ccwindow.h"

/**
 * @brief Editor window for the CStepVox test sequencer.
 *
 * @note On the Mac every member is a no-op; the window is never created.
 */
class CStepVoxWin : public CMDIChild2 {
public:
    /**
     * @brief Window procedure; does nothing.
     *
     * @param hWnd      Window handle (unused).
     * @param msg       Message id (unused).
     * @param wParam    Message parameter (unused).
     * @param lParam    Message parameter (unused).
     * @return Always 0.
     */
    virtual LRESULT myWndProc(HWND hWnd, int msg, WPARAM wParam, LPARAM lParam);

    /**
     * @brief Called when the step sequencer starts; does nothing.
     */
    void powerOn();

    /**
     * @brief Called when the step sequencer stops; does nothing.
     */
    void powerOff();
};
