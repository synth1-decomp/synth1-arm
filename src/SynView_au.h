/**
 * @file SynView_au.h
 *
 * Message carrier used to forward Win32-style messages (PostMessage/SendMessage)
 * to the Cocoa view on the main thread.  AU build only; the view factory uses
 * the declaration in SynView.h.
 */
#import <Cocoa/Cocoa.h>
#include "windef.h"

/**
 * @brief Win32 message (message, wParam, lParam) carried to a view's recieveEvent: on the main thread.
 *
 * Created by PostEventMac() / SendEvent() in winobj_au.m.
 */
@interface MSMessageParam : NSObject
{
    UINT    m_message;
    WPARAM  m_wparam;
    LPARAM  m_lparam;
}

/**
 * @brief Initializes the message holder.
 *
 * @param message           Win32 message number.
 * @param wparam            wParam of the message.
 * @param lparam            lParam of the message.
 * @return The initialized object, or nil.
 */
- (id)initWithParam:(UINT)message wparam:(WPARAM)wparam lparam:(LPARAM)lparam;

/**
 * @brief Gets the message number.
 *
 * @return The Win32 message number.
 */
- (UINT)message;

/**
 * @brief Gets the wParam.
 *
 * @return The wParam of the message.
 */
- (WPARAM)wparam;

/**
 * @brief Gets the lParam.
 *
 * @return The lParam of the message.
 */
- (LPARAM)lparam;
@end
