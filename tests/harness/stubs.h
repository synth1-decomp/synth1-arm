/**
 * @file stubs.h
 * @brief Call records of the Cocoa-side test doubles in cocoa_stubs.cpp.
 */
#pragma once

namespace t1stub {

/**
 * @brief Number of MessageBox() calls.
 */
extern int messageBoxCalls;

/**
 * @brief Number of PostEventMac() calls.
 */
extern int postEventCalls;

/**
 * @brief Number of SendEvent() calls.
 */
extern int sendEventCalls;

/**
 * @brief Message number of the last PostEventMac()/SendEvent() call.
 */
extern unsigned int lastMessage;

/**
 * @brief wParam of the last PostEventMac()/SendEvent() call.
 */
extern unsigned long lastWParam;

/**
 * @brief lParam of the last PostEventMac()/SendEvent() call.
 */
extern long lastLParam;

/**
 * @brief Value returned by isSandBox().
 */
extern int sandBox;

/**
 * @brief Resets all call records.
 */
void reset();

}  // namespace t1stub
