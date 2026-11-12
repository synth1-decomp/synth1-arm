/**
 * @file view_host.mm
 * @brief Cocoa helpers for vst3_tests.cpp: a host window to attach the plug-in's editor to, and
 *        a look at the views the editor puts into it.
 */
#import <Cocoa/Cocoa.h>
#import <objc/runtime.h>

#include "view_host.h"

void* view_host_open(int width, int height)
{
    [NSApplication sharedApplication];
    NSWindow* window = [[NSWindow alloc] initWithContentRect:NSMakeRect(-10000, -10000, width, height)
                                                   styleMask:NSWindowStyleMaskBorderless
                                                     backing:NSBackingStoreBuffered
                                                       defer:NO];
    [window setReleasedWhenClosed:NO];
    return (__bridge void*)[window contentView];
}

void view_host_close(void* parent)
{
    NSWindow* window = [(__bridge NSView*)parent window];
    [window close];
    [window release];
}

int view_host_subview_count(void* parent)
{
    return (int)[[(__bridge NSView*)parent subviews] count];
}

const char* view_host_subview_class(void* parent, int index)
{
    NSArray* views = [(__bridge NSView*)parent subviews];
    if (index < 0 || index >= (int)[views count]) return "";
    return class_getName([[views objectAtIndex:index] class]);
}

void view_host_subview_size(void* parent, int index, double* width, double* height)
{
    NSArray* views = [(__bridge NSView*)parent subviews];
    *width = *height = 0;
    if (index < 0 || index >= (int)[views count]) return;
    NSRect frame = [[views objectAtIndex:index] frame];
    *width = frame.size.width;
    *height = frame.size.height;
}
