/**
 * @file windef.cpp
 * @brief SendMessage0(), the bridge from the Cocoa views to the window procedure of a techpop window.
 */
#include <stdio.h>
#include "windef.h"
#include "techpop/ccwindow.h"

/**
 * @brief Delivers a Win32-style message directly to a techpop window.
 *
 * Calls CCWindow::myWndProc() synchronously on the calling thread. The Cocoa
 * views use it to forward their events (paint, mouse, posted messages).
 *
 * @param pWnd      Target techpop window object; may be NULL.
 * @param hWnd      Handle (NSView*) passed through to the window procedure.
 * @param msg       Message number (WM_*).
 * @param wParam    First message parameter.
 * @param lParam    Second message parameter.
 * @return The result of the window procedure, or 0 (after printing an error
 *         message) if @p pWnd is NULL.
 */
extern "C" int SendMessage0(CCWindow* pWnd, HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    if(pWnd){
        return pWnd->myWndProc(hWnd, msg, wParam, lParam);
    }
    printf("SendMessage0() error.\n");
    return 0;
}
