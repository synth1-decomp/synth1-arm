/**
 * @file view_host.h
 * @brief Cocoa helpers for vst3_tests.cpp (view_host.mm).
 */
#ifndef VIEW_HOST_H
#define VIEW_HOST_H

#ifdef __cplusplus
extern "C" {
#endif

/** Opens an off-screen borderless window and returns its content view (NSView*). */
void* view_host_open(int width, int height);

/** Closes the window of view_host_open(). */
void view_host_close(void* parent);

/** Number of subviews of the parent view. */
int view_host_subview_count(void* parent);

/** Objective-C class name of a subview, or "". */
const char* view_host_subview_class(void* parent, int index);

/** Frame size of a subview (0 x 0 if there is none). */
void view_host_subview_size(void* parent, int index, double* width, double* height);

/**
 * Drags vertically with the mouse on the first subview, at panel coordinates (466 x 340, top left
 * origin), dy points up (> 0) or down, with the window's backing scale reported as 2 (Retina).
 */
void view_host_drag(void* parent, double x, double y, int dy);

#ifdef __cplusplus
}
#endif

#endif
