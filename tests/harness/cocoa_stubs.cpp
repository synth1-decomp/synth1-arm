/**
 * @file cocoa_stubs.cpp
 * @brief Test doubles for the four Cocoa-side functions (winobj_au.m) the engine calls.
 *
 * The tests link the engine without the Objective-C layer. These stubs record the calls so
 * tests can check that the engine posts the expected notifications.
 */
#include "windef.h"
#include "stubs.h"

namespace t1stub {
int messageBoxCalls = 0;
int postEventCalls = 0;
int sendEventCalls = 0;
unsigned int lastMessage = 0;
unsigned long lastWParam = 0;
long lastLParam = 0;
int sandBox = 0;

void reset()
{
    messageBoxCalls = postEventCalls = sendEventCalls = 0;
    lastMessage = 0;
    lastWParam = 0;
    lastLParam = 0;
    sandBox = 0;
}
}  // namespace t1stub

extern "C" {

int MessageBox(HWND hWnd, LPCSTR lpText, LPCSTR lpCaption, UINT uType)
{
    (void)hWnd; (void)lpText; (void)lpCaption; (void)uType;
    t1stub::messageBoxCalls++;
    return 0;
}

void PostEventMac(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    (void)hWnd;
    t1stub::postEventCalls++;
    t1stub::lastMessage = msg;
    t1stub::lastWParam = (unsigned long)wParam;
    t1stub::lastLParam = (long)lParam;
}

void SendEvent(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    (void)hWnd;
    t1stub::sendEventCalls++;
    t1stub::lastMessage = msg;
    t1stub::lastWParam = (unsigned long)wParam;
    t1stub::lastLParam = (long)lParam;
}

int isSandBox(void)
{
    return t1stub::sandBox;
}

}  // extern "C"
