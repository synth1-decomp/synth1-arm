/**
 * @file Synth1CocoaViewFactory.mm
 * @brief Cocoa view of the Synth1 Audio Unit.
 *
 * The view factory (AUCocoaUIBase) creates a Synth1CocoaView, which hosts the
 * techpop GUI (CSynthWin, created by CreateSynWin()).  Every Cocoa event is
 * translated into the corresponding Win32 window message and handed to the
 * window procedure of the GUI with SendMessage0(); the GUI paints into the
 * CGContext of the view (WM_PAINT with the CGContextRef as lParam).
 *
 * Messages posted from other threads (PostMessage/SendMessage in winobj_au.m)
 * arrive here as MSMessageParam objects through -recieveEvent: ("recieve" (sic)
 * is also used by winobj_au.m and the other views).
 *
 * Event mapping
 * -------------
 * @verbatim
  Cocoa                       Win32 message       parameters
  -drawRect:                  WM_PAINT            wParam = RECT* (dirty rect, client coords)
                                                  lParam = CGContextRef (flipped, origin top left)
  -mouseDown:                 WM_LBUTTONDOWN      lParam = MAKELPARAM(x, y)
  -mouseUp:                   WM_LBUTTONUP        lParam = MAKELPARAM(x, y)
  -mouseDragged:              WM_MOUSEMOVE        lParam = MAKELPARAM(x, y)
  -mouseMoved:                WM_MOUSEMOVE        lParam = MAKELPARAM(x, y)
  -scrollWheel:               WM_MOUSEWHEEL       lParam = (deltaY * 1200) << 16
  -recieveEvent:              any                 MSMessageParam (message, wparam, lparam)
  @endverbatim
 * While a control has captured the mouse (SetCapture / GetCapture in
 * winobj_au.m) mouse messages are sent to the capturing window instead, with
 * the coordinates converted to its client area.
 *
 * Coordinates
 * -----------
 * The GUI was written for Windows, so all coordinates are client coordinates with the origin at the top left of
 * the view.  The view itself is not flipped; the y coordinate is converted with the height of the bounds, and the
 * CGContext is flipped before WM_PAINT is sent: y(win) = height - y(cocoa).
 *
 * Panel size
 * ----------
 * The panel is 466 x 340 pixels at 100%.  The user can zoom the panel in the option dialog (GetPanelScale()
 * returns the zoom in percent); the size of the view is decided here when the host asks for the view.
 *
 * The view factory bundle is loaded into the host process separately from the
 * AU component, so the engine globals (s1g) are taken over from the AU here.
 */
#import <Cocoa/Cocoa.h>
#import <AudioUnit/AudioUnit.h>
#import <AudioUnit/AUCocoaUIView.h>

#include <iostream>
#include "../windef.h"
#include "../SynView_au.h"
#include "../techpop/synth1.h"

// techpop GUI (synwin.cpp)
void* CreateSynWin(void* view, void* pSynth1, void* audioUnit);
void DestroySynWin(void* pSynWin);
int GetPanelScale();
extern "C" void CheckSSEFunc();
// windef.cpp
extern "C" bool SendMessage0(CCWindow* pWnd, HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);

// the view factory's own module handle (the AU defines its own in Synth1.cpp)
HINSTANCE hInst;
// bundle identifier of the view factory (the AU's own is in Synth1.cpp)
CFStringRef g_hBundleID = CFSTR("jp.daichi.synth1.audiounit.ui");

// panel size at 100%
#define PANEL_WIDTH     466
#define PANEL_HEIGHT    340

// private property of the AU: { CSynthesizer1*, S1G* }
#define kSynth1Property_Synth   64000

#define WM_MOUSEMOVE    0x0200
#define WM_LBUTTONDOWN  0x0201
#define WM_LBUTTONUP    0x0202
#define WM_MOUSEWHEEL   0x020A

//==============================================================================
//  Synth1CocoaView
//==============================================================================
/**
 * @brief NSView that hosts the techpop GUI and forwards Cocoa events to it as Win32 messages.
 *
 * @note The GUI (CSynthWin) is created when the view first enters a window and destroyed when it leaves it.
 */
@interface Synth1CocoaView : NSView
{
    void*       m_synwin;       // CSynthWin*
    void*       m_synth1;       // CSynthesizer1*
    AudioUnit   m_audiounit;
    NSTimer*    _idleTimer;
    float       m_delta;
    void*       m_pCapture;
}

/**
 * @brief Initializes the view and enables SSE in this bundle (CheckSSEFunc()).
 *
 * The GUI is created later, in -viewDidMoveToWindow.
 *
 * @param frameRect Frame of the view (the zoomed panel size).
 * @param inAU      The Audio Unit, passed on to the GUI.
 * @param pSynth1   CSynthesizer1* of the AU instance.
 * @return The view, or nil.
 */
- (id)initWithFrame:(NSRect)frameRect unit:(AudioUnit)inAU synth1:(void*)pSynth1;

/**
 * @brief Idle timer callback; does nothing (no timer is created in this file).
 *
 * @param timer The timer (unused).
 */
- (void)_timerFired:(NSTimer*)timer;

/**
 * @brief Invalidates and clears the idle timer.
 */
- (void)_destroy;

@end

@implementation Synth1CocoaView

- (id)initWithFrame:(NSRect)frameRect unit:(AudioUnit)inAU synth1:(void*)pSynth1
{
    self = [super initWithFrame:frameRect];
    if(self){
        CheckSSEFunc();
        m_synth1 = pSynth1;
        m_audiounit = inAU;
        m_synwin = NULL;
        m_delta = 0;
    }
    return self;
}

- (void)_timerFired:(NSTimer*)timer
{
}

- (void)_destroy
{
    [_idleTimer invalidate];
    _idleTimer = nil;
}

/**
 * @brief Creates the GUI (CreateSynWin()) the first time the view is put into a window.
 */
- (void)viewDidMoveToWindow
{
    if([self window] && m_synwin == NULL){
        m_synwin = CreateSynWin(self, m_synth1, m_audiounit);
    }
}

/**
 * @brief Prepares a new window, or destroys the GUI and the timer when the view leaves its window.
 *
 * For a new window, mouse-moved events are turned on and the view becomes first responder. For nil, the GUI is
 * destroyed with DestroySynWin() and the timer is stopped.
 *
 * @param newWindow The new window, or nil.
 */
- (void)viewWillMoveToWindow:(NSWindow*)newWindow
{
    if(newWindow){
        [newWindow setAcceptsMouseMovedEvents:YES];
        [newWindow makeFirstResponder:self];
        return;
    }
    if(m_synwin){
        void* pSynWin = m_synwin;
        m_synwin = NULL;
        DestroySynWin(pSynWin);
    }
    [self _destroy];
}

/**
 * @brief Stops the timer when the view is removed from its superview.
 *
 * @param newSuperview The new superview, or nil when the view is removed.
 */
- (void)viewWillMoveToSuperview:(NSView*)newSuperview
{
    if(newSuperview == nil){
        [self _destroy];
    }
}

/**
 * @brief Destroys the GUI if it still exists and stops the timer.
 */
- (void)dealloc
{
    if(m_synwin){
        void* pSynWin = m_synwin;
        m_synwin = NULL;
        DestroySynWin(pSynWin);
    }
    [self _destroy];
    [super dealloc];
}

/**
 * @brief Paints the GUI: sends WM_PAINT with the dirty rectangle (in client coordinates) and a flipped context.
 *
 * @param rect Dirty rectangle in view (Cocoa) coordinates.
 */
- (void)drawRect:(NSRect)rect
{
    [super drawRect:rect];

    // Cocoa coordinates (origin bottom left) -> Windows coordinates (origin top left)
    NSRect bounds = [self bounds];
    int top = rect.origin.y;
    int bottom = rect.origin.y + rect.size.height;
    int left = rect.origin.x;
    RECT rc;
    rc.left = left;
    rc.top = bounds.size.height - bottom;
    rc.right = rect.origin.x + rect.size.width;
    rc.bottom = bounds.size.height - top;

    CGContextRef context = (CGContextRef)[[NSGraphicsContext currentContext] CGContext];
    CGContextSaveGState(context);
    CGContextSetTextMatrix(context, CGAffineTransformMake(1, 0, 0, 1, 0, 0));
    CGContextScaleCTM(context, 1.0, -1.0);
    CGContextTranslateCTM(context, 0, -bounds.size.height);
    SendMessage0((CCWindow*)m_synwin, self, WM_PAINT, (WPARAM)&rc, (LPARAM)context);
    CGContextRestoreGState(context);
}

/**
 * @brief Takes the focus, cancels any mouse capture (SetCapture(NULL)) and sends WM_LBUTTONDOWN to the GUI.
 *
 * @param theEvent Mouse-down event (its location is used).
 */
- (void)mouseDown:(NSEvent*)theEvent
{
    [[self window] makeFirstResponder:self];
    NSPoint pt = [self convertPoint:[theEvent locationInWindow] fromView:nil];
    NSRect bounds = [self bounds];
    int x = pt.x;
    int y = bounds.size.height - pt.y;
    LPARAM lParam = MAKELPARAM(x, y);
    SetCapture(NULL);
    SendMessage0((CCWindow*)m_synwin, self, WM_LBUTTONDOWN, 0, lParam);
}

/**
 * @brief Sends WM_LBUTTONUP to the capturing window if there is one, otherwise to the GUI.
 *
 * @param theEvent Mouse-up event (its location is used).
 */
- (void)mouseUp:(NSEvent*)theEvent
{
    NSPoint pt = [self convertPoint:[theEvent locationInWindow] fromView:nil];
    NSRect bounds = [self bounds];
    int x = pt.x;
    int y = bounds.size.height - pt.y;
    LPARAM lParam = MAKELPARAM(x, y);
    int bControl = 0;
    HWND hCapture = GetCapture(&bControl);
    if(hCapture){
        POINT p;
        p.x = x;
        p.y = y;
        ClientToScreen(self, &p);
        ScreenToClient(hCapture, &p);
        SendMessage(hCapture, WM_LBUTTONUP, 0, MAKELPARAM(p.x, p.y));
    }else{
        SendMessage0((CCWindow*)m_synwin, self, WM_LBUTTONUP, 0, lParam);
    }
}

/**
 * @brief Sends WM_MOUSEMOVE to the capturing window if there is one, otherwise to the GUI.
 *
 * @param theEvent Mouse-dragged event (its location is used).
 */
- (void)mouseDragged:(NSEvent*)theEvent
{
    NSPoint pt = [self convertPoint:[theEvent locationInWindow] fromView:nil];
    NSRect bounds = [self bounds];
    int x = pt.x;
    int y = bounds.size.height - pt.y;
    LPARAM lParam = MAKELPARAM(x, y);
    int bControl = 0;
    HWND hCapture = GetCapture(&bControl);
    if(hCapture){
        POINT p;
        p.x = x;
        p.y = y;
        ClientToScreen(self, &p);
        ScreenToClient(hCapture, &p);
        SendMessage(hCapture, WM_MOUSEMOVE, 0, MAKELPARAM(p.x, p.y));
    }else{
        SendMessage0((CCWindow*)m_synwin, self, WM_MOUSEMOVE, 0, lParam);
    }
}

/**
 * @brief Same as -mouseDragged: (WM_MOUSEMOVE to the capturing window or the GUI).
 *
 * @param theEvent Mouse-moved event (its location is used).
 */
- (void)mouseMoved:(NSEvent*)theEvent
{
    NSPoint pt = [self convertPoint:[theEvent locationInWindow] fromView:nil];
    NSRect bounds = [self bounds];
    int x = pt.x;
    int y = bounds.size.height - pt.y;
    LPARAM lParam = MAKELPARAM(x, y);
    int bControl = 0;
    HWND hCapture = GetCapture(&bControl);
    if(hCapture){
        POINT p;
        p.x = x;
        p.y = y;
        ClientToScreen(self, &p);
        ScreenToClient(hCapture, &p);
        SendMessage(hCapture, WM_MOUSEMOVE, 0, MAKELPARAM(p.x, p.y));
    }else{
        SendMessage0((CCWindow*)m_synwin, self, WM_MOUSEMOVE, 0, lParam);
    }
}

/**
 * @brief Swallows key events: the GUI has no keyboard handling and the host should not get keys typed into the panel.
 *
 * @param theEvent Key event (ignored).
 */
- (void)keyDown:(NSEvent*)theEvent
{
}

/**
 * @brief Tells whether the view accepts the keyboard focus.
 *
 * @return YES.
 */
- (BOOL)acceptsFirstResponder
{
    return YES;
}

/**
 * @brief Tells whether the view agrees to become first responder.
 *
 * @return YES, always.
 */
- (BOOL)becomeFirstResponder
{
    return YES;
}

/**
 * @brief Lets the first click into an inactive plug-in window already operate the control.
 *
 * @param theEvent The click (unused).
 * @return YES.
 */
- (BOOL)acceptsFirstMouse:(NSEvent*)theEvent
{
    return YES;
}

/**
 * @brief Tells whether a click into the view may drag the window.
 *
 * @return NO, otherwise dragging a knob would move the host's window.
 */
- (BOOL)mouseDownCanMoveWindow
{
    return NO;
}

/**
 * @brief Delivers a message posted with PostMessage/SendMessage to the GUI (ignored if the GUI does not exist).
 *
 * @param param Message, wParam and lParam.
 */
- (void)recieveEvent:(MSMessageParam*)param
{
    if(m_synwin){
        SendMessage0((CCWindow*)m_synwin, self, [param message], [param wparam], [param lparam]);
    }
}

/**
 * @brief Sends WM_MOUSEWHEEL to the capturing window or the GUI.
 *
 * The delta (deltaY * 1200) goes into the high word of lParam (not wParam as in Win32), and the last deltaY is
 * kept in m_delta.
 *
 * @param theEvent Scroll-wheel event (deltaY is used).
 */
- (void)scrollWheel:(NSEvent*)theEvent
{
    [theEvent deltaX];
    float delta = [theEvent deltaY];
    [theEvent deltaZ];
    int bControl = 0;
    HWND hCapture = GetCapture(&bControl);
    if(hCapture){
        long wheel = delta * 1200.0;
        SendMessage(hCapture, WM_MOUSEWHEEL, 0, wheel << 16);
    }else{
        long wheel = delta * 1200.0;
        SendMessage0((CCWindow*)m_synwin, self, WM_MOUSEWHEEL, 0, wheel << 16);
    }
    m_delta = delta;
}

@end

//==============================================================================
//  Synth1CocoaViewFactory
//==============================================================================
/**
 * @brief Cocoa view factory of the AU (AUCocoaUIBase); the host instantiates it from the view bundle.
 */
@interface Synth1CocoaViewFactory : NSObject <AUCocoaUIBase>
{
}

/**
 * @brief Reports the version of the AUCocoaUIBase protocol this class implements.
 *
 * @return 0.
 */
- (unsigned)interfaceVersion;

/**
 * @brief Describes the Cocoa UI.
 *
 * @return "Synth1 Cocoa View".
 */
- (NSString*)description;

/**
 * @brief Creates a new autoreleased Synth1CocoaView for an AU instance.
 *
 * Reads { CSynthesizer1*, S1G* } from the AU's private property kSynth1Property_Synth (64000) and points s1g
 * of this bundle at the AU's globals, so the bank list, sound library and option settings are shared.
 *
 * @param inAU              The Audio Unit.
 * @param inPreferredSize   Ignored: the size is the panel size times the zoom setting of the option dialog.
 * @return The view (the host retains it).
 */
- (NSView*)uiViewForAudioUnit:(AudioUnit)inAU withSize:(NSSize)inPreferredSize;

@end

@implementation Synth1CocoaViewFactory

- (unsigned)interfaceVersion
{
    return 0;
}

- (NSString*)description
{
    return @"Synth1 Cocoa View";
}

// N.B.: this class is simply a view-factory,
// returning a new autoreleased view each time it's called.
- (NSView*)uiViewForAudioUnit:(AudioUnit)inAU withSize:(NSSize)inPreferredSize
{
    struct {
        void* pSynth1;      // CSynthesizer1*
        S1G* pS1G;
    } param;
    UInt32 size = sizeof(param);
    ComponentResult result = AudioUnitGetProperty(inAU, kSynth1Property_Synth, kAudioUnitScope_Global, 0, &param, &size);
    // MATCH-HACK: the assert's line number is compiled into the binary; keep it at 393.
    #line 393
    assert(result == noErr);
    s1g = param.pS1G;

    int scale = GetPanelScale();
    NSSize viewSize = NSMakeSize(PANEL_WIDTH * scale / 100, PANEL_HEIGHT * scale / 100);
    NSRect frame;
    frame.origin = NSZeroPoint;
    frame.size = viewSize;
    Synth1CocoaView* view = [[Synth1CocoaView alloc] initWithFrame:frame unit:inAU synth1:param.pSynth1];
    return [view autorelease];
}

@end
