/**
 * @file SynView_au.m
 *
 * AU side of the Cocoa view glue: implementation of MSMessageParam, the message
 * holder that PostEventMac() / SendEvent() pass to the view on the main thread.
 */

#import "SynView_au.h"

@implementation MSMessageParam

- (id)initWithParam:(UINT)message wparam:(WPARAM)wparam lparam:(LPARAM)lparam
{
    if((self = [super init])){
        m_message = message;
        m_wparam = wparam;
        m_lparam = lparam;
    }
    return self;
}

- (UINT)message
{
    return m_message;
}

- (WPARAM)wparam
{
    return m_wparam;
}

- (LPARAM)lparam
{
    return m_lparam;
}

@end
