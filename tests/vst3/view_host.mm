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

static CGFloat retina_scale(id self, SEL _cmd) { return 2; }

static NSEvent* mouse_event(NSEventType type, NSWindow* window, NSPoint location)
{
    return [NSEvent mouseEventWithType:type location:location modifierFlags:0 timestamp:0
                          windowNumber:[window windowNumber] context:nil eventNumber:0 clickCount:1 pressure:1];
}

void view_host_drag(void* parent, double x, double y, int dy)
{
    NSView* view = [[(__bridge NSView*)parent subviews] objectAtIndex:0];
    NSWindow* window = [view window];
    NSRect b = [view bounds];
    // panel coordinates (466 x 340, top left origin) to window coordinates
    NSPoint p = [view convertPoint:NSMakePoint(x * b.size.width / 466, b.size.height * (1 - y / 340)) toView:nil];
    // as on a Retina display, whatever screen the tests run on
    Method m = class_getInstanceMethod([NSWindow class], @selector(backingScaleFactor));
    IMP original = method_setImplementation(m, (IMP)retina_scale);
    [view mouseDown:mouse_event(NSEventTypeLeftMouseDown, window, p)];
    for (int i = 0; i < abs(dy); i++) {
        p.y += dy > 0 ? 1 : -1;     // Cocoa's y points up
        [view mouseDragged:mouse_event(NSEventTypeLeftMouseDragged, window, p)];
    }
    [view mouseUp:mouse_event(NSEventTypeLeftMouseUp, window, p)];
    method_setImplementation(m, original);
}
