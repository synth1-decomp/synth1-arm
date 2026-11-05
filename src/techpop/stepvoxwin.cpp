/**
 * @file stepvoxwin.cpp
 * CStepVoxWin: the step vox window is not available on the Mac.
 *
 * All members are empty stubs kept so the class and its vtable exist.
 */

#include "stepvoxwin.h"

LRESULT CStepVoxWin::myWndProc(HWND hWnd, int msg, WPARAM wParam, LPARAM lParam)
{
    return 0;
}

void CStepVoxWin::powerOn()
{
}

void CStepVoxWin::powerOff()
{
}
