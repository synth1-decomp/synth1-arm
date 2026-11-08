/**
 * @file winobj_au.m
 *
 * Win32 emulation functions that need Cocoa / Carbon.
 *
 * The same file is compiled into the AU and into the Cocoa view factory.  The
 * view factory defines SYNTH1_VIEWFACTORY, which adds the window / DC / capture
 * emulation used by the techpop GUI.  Both builds get PostEventMac() /
 * SendEvent(), MessageBox(), setWindowPos(), getVisibleScreenRect() and
 * isSandBox().  An HWND is an NSView*, an HDC a CGContextRef; screen coordinates
 * use a top-left origin on the main display.  The functions are declared in
 * windef.h, techpop/ccwindow.h and synth1if.h.
 */

#import <Cocoa/Cocoa.h>
#import <Carbon/Carbon.h>
#include "windef.h"
#ifdef SYNTH1_VIEWFACTORY
#import "SynWindow.h"
#else
#import "SynView_au.h"
#endif

#ifdef SYNTH1_MODERN_BUILD
// Still exported by HIToolbox, but no longer declared in the SDK's headers
extern OSStatus CreateStandardAlert(AlertType alertType, CFStringRef error, CFStringRef explanation,
                                    const AlertStdCFStringAlertParamRec *param, DialogRef *outAlert);
extern OSStatus RunStandardAlert(DialogRef inAlert, ModalFilterUPP filterProc, DialogItemIndex *outItemHit);
extern OSStatus SetUserFocusWindow(WindowRef inWindow);
// Defined below, after its first use
int GetWindowRect(HWND hWnd, RECT* rc);
#endif

#ifdef SYNTH1_VIEWFACTORY

static HWND g_hCaptureWnd;  // (separate from the global of the same name in winemu_au.cpp)

//------------------------------------------------------------------------------
// device contexts

HDC GetDCControl(HWND hWnd)
{
    return GetDC(hWnd);
}

HDC GetDC(HWND hWnd)
{
    if(hWnd){
        NSWindow* window = [(NSView*)hWnd window];
        if(window){
            CGContextRef context = (CGContextRef)[[NSGraphicsContext graphicsContextWithWindow:window] graphicsPort];
            if(context){
                CGContextSaveGState(context);
                NSRect rect = [(NSView*)hWnd bounds];
                rect.origin = [(NSView*)hWnd convertPointToBacking:rect.origin];
                CGContextScaleCTM(context, 1.0, -1.0);
                CGContextTranslateCTM(context, rect.origin.x, -(rect.origin.y + rect.size.height));
                return context;
            }
        }
    }
    return NULL;
}

HDC GetDC0(HWND hWnd)
{
    if(hWnd){
        NSWindow* window = [(NSView*)hWnd window];
        if(window){
            CGContextRef context = (CGContextRef)[[NSGraphicsContext graphicsContextWithWindow:window] graphicsPort];
            if(context){
                CGContextSaveGState(context);
                NSRect rect = [(NSView*)hWnd bounds];
                rect.origin = [(NSView*)hWnd convertPointToBacking:rect.origin];
                CGContextScaleCTM(context, 1.0, -1.0);
                CGContextTranslateCTM(context, rect.origin.x, -(rect.origin.y + rect.size.height));
                return context;
            }
        }
    }
    return NULL;
}

void ReleaseDCControl(HWND hWnd, HDC hdc)
{
    if(hdc){
        CGContextRestoreGState(hdc);
        CGContextFlush(hdc);
    }
}

void ReleaseDC(HWND hWnd, HDC hdc)
{
    if(hdc){
        CGContextRestoreGState(hdc);
        CGContextFlush(hdc);
    }
}

void ReleaseDC0(HWND hWnd, HDC hdc)
{
    if(hdc){
        CGContextRestoreGState(hdc);
        CGContextFlush(hdc);
    }
}

// prepares a context that is already current (drawRect:) like GetDC
void GetDC1(HWND hWnd, HDC hdc)
{
    if(hdc){
        CGContextSaveGState(hdc);
        if(hWnd){
            NSRect rect = [(NSView*)hWnd bounds];
            rect.origin = [(NSView*)hWnd convertPointToBacking:rect.origin];
            CGContextScaleCTM(hdc, 1.0, -1.0);
            CGContextTranslateCTM(hdc, rect.origin.x, -(rect.origin.y + rect.size.height));
        }
    }
}

void ReleaseDC1(HWND hWnd, HDC hdc)
{
    if(hdc){
        CGContextRestoreGState(hdc);
        CGContextFlush(hdc);
    }
}

void SetFocus0(HWND hWnd)
{
    [[(NSView*)hWnd window] makeFirstResponder:(NSView*)hWnd];
}

void SetFocusControl(HWND hWnd)
{
    [[(NSView*)hWnd window] makeFirstResponder:(NSView*)hWnd];
}

#endif // SYNTH1_VIEWFACTORY

void PostEventMac(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    if(hWnd){
        MSMessageParam* param = [[MSMessageParam alloc] initWithParam:msg wparam:wParam lparam:lParam];
        [(id)hWnd performSelectorOnMainThread:@selector(recieveEvent:) withObject:param waitUntilDone:NO];
        [param release];
    }
}

void SendEvent(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    if(hWnd){
        MSMessageParam* param = [[MSMessageParam alloc] initWithParam:msg wparam:wParam lparam:lParam];
        [(id)hWnd performSelectorOnMainThread:@selector(recieveEvent:) withObject:param waitUntilDone:YES];
        [param release];
    }
}

#ifdef SYNTH1_VIEWFACTORY

//------------------------------------------------------------------------------
// windows

void SetWindowSize(HWND hWnd, int w, int h)
{
    [[(NSView*)hWnd window] setContentSize:NSMakeSize(w, h)];
}

void ScreenToClientControl(HWND hWnd, POINT* pt)
{
    ScreenToClient(hWnd, pt);
}

// screen coordinates: origin at the top left of the main display
void ScreenToClient(HWND hWnd, POINT* pt)
{
    CGRect display = CGDisplayBounds(CGMainDisplayID());
    NSWindow* window = [(NSView*)hWnd window];
    NSPoint p = NSMakePoint(pt->x, display.size.height - pt->y);
    p = [(NSView*)hWnd convertPointFromBase:[window convertScreenToBase:p]];
    pt->x = p.x;
    if(![(NSView*)hWnd isFlipped]){
        p.y = [(NSView*)hWnd bounds].size.height - p.y;
    }
    pt->y = p.y;
}

void ClientToScreenControl(HWND hWnd, POINT* pt)
{
    ClientToScreen(hWnd, pt);
}

void ClientToScreen(HWND hWnd, POINT* pt)
{
    CGRect display = CGDisplayBounds(CGMainDisplayID());
    NSWindow* window = [(NSView*)hWnd window];
    NSPoint p;
    p.x = pt->x;
    if([(NSView*)hWnd isFlipped]){
        p.y = pt->y;
    }else{
        p.y = [(NSView*)hWnd bounds].size.height - pt->y;
    }
    p = [(NSView*)hWnd convertPoint:p toView:nil];
    NSRect r = [window convertRectToScreen:NSMakeRect(p.x, p.y, 0, 0)];
    pt->x = r.origin.x;
    pt->y = display.size.height - r.origin.y;
}

int GetWindowRectControl(HWND hWnd, RECT* rc)
{
    GetWindowRect(hWnd, rc);
    return TRUE;
}

int GetWindowRect(HWND hWnd, RECT* rc)
{
    CGRect display = CGDisplayBounds(CGMainDisplayID());
    NSWindow* window = [(NSView*)hWnd window];
    NSRect r = [(NSView*)hWnd bounds];
    if(![(NSView*)hWnd isFlipped]){
        r.origin.y += r.size.height;
    }
    r.origin = [(NSView*)hWnd convertPoint:r.origin toView:nil];
    NSRect sr = [window convertRectToScreen:NSMakeRect(r.origin.x, r.origin.y, 0, 0)];
    r.origin.x = sr.origin.x;
    r.origin.y = sr.origin.y;
    rc->left = r.origin.x;
    rc->top = display.size.height - r.origin.y;
    rc->right = rc->left + r.size.width;
    rc->bottom = rc->top + r.size.height;
    return TRUE;
}

void GetClientRectControl(HWND hWnd, RECT* rc)
{
    GetClientRect(hWnd, rc);
}

void GetClientRect(HWND hWnd, RECT* rc)
{
    NSRect r = [(NSView*)hWnd bounds];
    rc->left = r.origin.x;
    rc->top = r.origin.y;
    rc->right = r.origin.x + r.size.width;
    rc->bottom = r.origin.y + r.size.height;
}

void InvalidateRectControl(HWND hWnd, RECT* rc, int bErase)
{
    InvalidateRect(hWnd, rc, FALSE);
}

void InvalidateRect(HWND hWnd, RECT* rc, bool bErase)
{
    if(rc){
        NSRect bounds = [(NSView*)hWnd bounds];
        [(NSView*)hWnd setNeedsDisplayInRect:NSMakeRect(rc->left, bounds.size.height - rc->bottom, rc->right - rc->left, rc->bottom - rc->top)];
    }else{
        [(NSView*)hWnd setNeedsDisplay:YES];
    }
}

int ShowWindowMac(HWND hWnd, int bShow)
{
    if(hWnd == NULL) return FALSE;
    if(bShow){
        [[(NSView*)hWnd window] orderFrontRegardless];
    }
    return TRUE;
}

HWND CreateWindow(int x, int y, int w, int h, float alpha, void* pWnd)
{
    CGRect display = CGDisplayBounds(CGMainDisplayID());
    VolWindow* window = [[VolWindow alloc] initWithContentRect:NSMakeRect(x, display.size.height - (y + h), w, h) styleMask:NSBorderlessWindowMask backing:NSBackingStoreBuffered defer:NO];
    [window setLevel:NSFloatingWindowLevel];
    [window setAlphaValue:alpha];
    [window setAcceptsMouseMovedEvents:YES];
    VolView* view = [[VolView alloc] initWithWnd:NSMakeRect(0, 0, w, h) win:pWnd];
    [window setView:view];
    [[window contentView] addSubview:view];
    [window makeFirstResponder:view];
    [window makeKeyAndOrderFront:window];
    SetUserFocusWindow([window windowRef]);
    return view;
}

HWND CreateWindowNS(int x, int y, int w, int h, float alpha, void* pWnd)
{
    return CreateWindow(x, y, w, h, alpha, pWnd);
}

HWND CreateSoundSelectWindow(int x, int y, int w, int h, float alpha, void* pWnd, HWND hParent, int style)
{
    CGRect display = CGDisplayBounds(CGMainDisplayID());
    SoundSelectWindow* window = [[SoundSelectWindow alloc] initWithContentRect:NSMakeRect(x, display.size.height - (y + h), w, h) styleMask:NSBorderlessWindowMask backing:NSBackingStoreBuffered defer:NO];
    [window setLevel:NSFloatingWindowLevel];
    [window setAlphaValue:alpha];
    SoundSelectView* view = [[SoundSelectView alloc] initWithWnd:NSMakeRect(0, 0, w, h) win:pWnd];
    [window setView:view];
    [[window contentView] addSubview:view];
    [window makeFirstResponder:view];
    [window makeKeyAndOrderFront:window];
    [window setAcceptsMouseMovedEvents:YES];
    SetUserFocusWindow([window windowRef]);
    return view;
}

HWND CreateSoundSelectWindowNS(int x, int y, int w, int h, float alpha, void* pWnd)
{
    HWND hParent;
    int style;
    return CreateSoundSelectWindow(x, y, w, h, alpha, pWnd, hParent, style);
}

HWND CreateOutlineView(HWND hParent, int x, int y, int w, int h, int nID)
{
    NSOutlineView* outline = [[NSOutlineView alloc] initWithFrame:NSMakeRect(x, y, w, h)];
    [outline setFocusRingType:NSFocusRingTypeExterior];
    [(SoundSelectView*)hParent setOutlineView:outline];
    [outline setDataSource:(id)hParent];

    NSTableColumn* column = [[NSTableColumn alloc] initWithIdentifier:@"BANK"];
    [column setHeaderCell:[[NSCell alloc] initTextCell:@"Bank"]];
    [column setEditable:NO];
    [column setMinWidth:300];
    [column setMaxWidth:300];
    [outline addTableColumn:column];
    [outline setOutlineTableColumn:column];
    [column release];

    [outline setAutoresizesOutlineColumn:YES];
    [outline shouldCollapseAutoExpandedItemsForDeposited:YES];
    [outline setAutosaveExpandedItems:YES];
    [outline setIndentationMarkerFollowsCell:YES];
    [outline setAutosaveExpandedItems:YES];
    [outline setRowHeight:18];
    [outline setIndentationPerLevel:18];
    [outline setSelectionHighlightStyle:NSTableViewSelectionHighlightStyleSourceList];
    [outline setGridStyleMask:NSTableViewSolidHorizontalGridLineMask];

    NSScrollView* scroll = [[NSScrollView alloc] initWithFrame:NSMakeRect(x, y, w, h)];
    [scroll setFocusRingType:NSFocusRingTypeExterior];
    [scroll setDocumentView:outline];
    [outline release];
    [scroll setAutoresizesSubviews:YES];
    [scroll setHasHorizontalScroller:YES];
    [scroll setHasVerticalScroller:YES];
    [scroll setDocumentCursor:[NSCursor arrowCursor]];
    [scroll setBorderType:NSBezelBorder];
    [(NSView*)hParent addSubview:scroll];
    [scroll release];
    [[(NSView*)hParent window] makeFirstResponder:scroll];

    [outline setDelegate:(id)hParent];
    [outline setTarget:(id)hParent];
    [outline setAction:@selector(outlineViewClicked:)];
    return scroll;
}

HWND CreateButton(HWND hParent, int x, int y, int w, int h, const char* text, int nID)
{
    NSButton* button = [[NSButton alloc] initWithFrame:NSMakeRect(x, y, w, h)];
    [button setTitle:[[[NSString alloc] initWithCString:text encoding:NSUTF8StringEncoding] autorelease]];
    [button setBezelStyle:NSRoundedBezelStyle];
    [button setTag:nID];
    [button setTarget:(id)hParent];
    [button setAction:@selector(pushButton:)];
    [(NSView*)hParent addSubview:button];
    [button release];
    return button;
}

HWND CreateEdit(HWND hParent, int x, int y, int w, int h, int nID)
{
    NSTextField* edit = [[NSTextField alloc] initWithFrame:NSMakeRect(x, y, w, h)];
    [edit setFocusRingType:NSFocusRingTypeDefault];
    [(NSView*)hParent addSubview:edit];
    [edit release];
    return edit;
}

void DestroyWindowControl(HWND hWnd)
{
    if(g_hCaptureWnd == hWnd) g_hCaptureWnd = NULL;
    if(hWnd && [(NSView*)hWnd window]){
        [(NSView*)hWnd removeFromSuperview];
    }
}

int DestroyWindow(HWND hWnd)
{
    return DestroyWindowWithParentCloseFlag(hWnd, TRUE);
}

int DestroyWindowWithParentCloseFlag(HWND hWnd, int bCloseParent)
{
    if(g_hCaptureWnd == hWnd) g_hCaptureWnd = NULL;
    if(hWnd == NULL) return FALSE;
    NSWindow* window = [(NSView*)hWnd window];
    if(window){
        [(NSView*)hWnd removeFromSuperview];
        if(bCloseParent){
            [window close];
        }
    }
    return TRUE;
}

int DestroyWindowNS(HWND hWnd)
{
    return DestroyWindowWithParentCloseFlag(hWnd, TRUE);
}

void DestroySoundSelectWindowNS(HWND hWnd)
{
    DestroyWindowWithParentCloseFlag(hWnd, TRUE);
    [(NSView*)hWnd release];
}

int UpdateWindowMac(HWND hWnd)
{
    return hWnd != NULL;
}

void SetAlpha(HWND hWnd, float alpha)
{
    [(NSView*)hWnd setAlphaValue:alpha];
    NSWindow* window = [(NSView*)hWnd window];
    [window setAlphaValue:alpha];
    [[window contentView] setAlphaValue:alpha];
    [window setBackgroundColor:[NSColor clearColor]];
    [window setOpaque:NO];
    [window setHasShadow:YES];
}

//------------------------------------------------------------------------------
// mouse capture

void SetCaptureControl(HWND hWnd)
{
    ReleaseCapture();
    g_hCaptureWnd = hWnd;
}

HWND SetCapture(HWND hWnd)
{
    ReleaseCapture();
    g_hCaptureWnd = hWnd;
    return NULL;
}

HWND GetCapture(int* pbControl)
{
    if(pbControl) *pbControl = FALSE;
    return g_hCaptureWnd;
}

void ReleaseCaptureControl(void)
{
    HWND hWnd = g_hCaptureWnd;
    if(hWnd){
        MSMessageParam* param = [[MSMessageParam alloc] initWithParam:WM_CAPTURECHANGED wparam:0 lparam:0];
        [(id)hWnd performSelectorOnMainThread:@selector(recieveEvent:) withObject:param waitUntilDone:YES];
        [param release];
        g_hCaptureWnd = NULL;
    }
}

void ReleaseCapture(void)
{
    HWND hWnd = g_hCaptureWnd;
    if(hWnd){
        MSMessageParam* param = [[MSMessageParam alloc] initWithParam:WM_CAPTURECHANGED wparam:0 lparam:0];
        [(id)hWnd performSelectorOnMainThread:@selector(recieveEvent:) withObject:param waitUntilDone:YES];
        [param release];
        g_hCaptureWnd = NULL;
    }
}

int MoveWindowControl(HWND hWnd, int x, int y, int w, int h, int bRepaint)
{
    MoveWindowMac(hWnd, x, y, w, h, bRepaint);
    return FALSE;
}

// (x, y) relative to the top left of the parent view
int MoveWindowMac(HWND hWnd, int x, int y, int w, int h, int bRepaint)
{
    NSRect bounds = [[(NSView*)hWnd superview] bounds];
    [(NSView*)hWnd setFrame:NSMakeRect(x, bounds.size.height - (y + h), w, h)];
    [(NSView*)hWnd setBounds:NSMakeRect(0, 0, w, h)];
    return FALSE;
}

void SizeView(HWND hWnd, int w, int h)
{
    [(NSView*)hWnd setFrameSize:NSMakeSize(w, h)];
}

int ModalDialogMac(HINSTANCE hInst, const char* name, HWND hParent)
{
    CCDlg* dlg = [[CCDlg alloc] initWithWindowNibName:[NSString stringWithUTF8String:name]];
    [dlg loadWindow];
    NSWindow* window = [dlg window];
    [(OptionWindow*)window setParam:hParent];
    [window center];
    [window makeKeyAndOrderFront:window];
    int ret = [[NSApplication sharedApplication] runModalForWindow:window];
    [dlg release];
    return ret;
}

#endif // SYNTH1_VIEWFACTORY

// Each button layout is a complete block of its own (the shared tail is merged only
// by the machine tail merger, from RunStandardAlert on).
int MessageBox(HWND hWnd, LPCSTR lpText, LPCSTR lpCaption, UINT uType)
{
    AlertStdCFStringAlertParamRec param;

    param.movable = true;
    param.flags = 0;
    param.version = kStdCFStringAlertVersionOne;
    param.position = kWindowCenterMainScreen;
    if(uType & MB_YESNO){
        param.defaultText = (CFStringRef)kAlertDefaultOKText;
        param.cancelText = (CFStringRef)kAlertDefaultCancelText;
        param.otherText = NULL;
        param.helpButton = false;
        param.defaultButton = kAlertStdAlertOKButton;
        param.cancelButton = kAlertStdAlertCancelButton;
        CFStringRef caption = CFStringCreateWithCString(kCFAllocatorDefault, lpCaption, kCFStringEncodingUTF8);
        CFStringRef text = CFStringCreateWithCString(kCFAllocatorDefault, lpText, kCFStringEncodingUTF8);
        DialogRef alert;
        CreateStandardAlert(kAlertNoteAlert, caption, text, &param, &alert);
        DialogItemIndex itemHit;
        RunStandardAlert(alert, NULL, &itemHit);
        CFRelease(text);
        CFRelease(caption);
        return itemHit != kAlertStdAlertOKButton;
    }else{
        param.defaultText = (CFStringRef)kAlertDefaultOKText;
        param.cancelText = NULL;
        param.otherText = NULL;
        param.helpButton = false;
        param.defaultButton = kAlertStdAlertOKButton;
        param.cancelButton = 0;
        CFStringRef caption = CFStringCreateWithCString(kCFAllocatorDefault, lpCaption, kCFStringEncodingUTF8);
        CFStringRef text = CFStringCreateWithCString(kCFAllocatorDefault, lpText, kCFStringEncodingUTF8);
        DialogRef alert;
        CreateStandardAlert(kAlertNoteAlert, caption, text, &param, &alert);
        DialogItemIndex itemHit;
        RunStandardAlert(alert, NULL, &itemHit);
        CFRelease(text);
        CFRelease(caption);
        return itemHit != kAlertStdAlertOKButton;
    }
}

#ifdef SYNTH1_VIEWFACTORY

void SetCursorWin(int type)
{
    switch(type){
    case 0: [[NSCursor arrowCursor] set]; break;
    case 1: [[NSCursor pointingHandCursor] set]; break;
    case 2: [[NSCursor resizeLeftRightCursor] set]; break;
    case 3: [[NSCursor resizeUpDownCursor] set]; break;
    }
}

//------------------------------------------------------------------------------
// Synth1MouseCapture: releases the GUI's mouse capture (message 0x8002) when
// the window is moved, minimized, closed or loses the key status.

/**
 * @brief Posts message WM_APP + 2 to a view when its window moves, miniaturizes, closes or resigns key.
 *
 * The GUI handles that message by releasing its mouse capture. Created by
 * initCapture() and released by exitCapture().
 * @note The target view is not retained.
 */
@interface Synth1MouseCapture : NSObject
{
    void*   m_hTarget;      // HWND (NSView*)
}

/**
 * @brief Initializes the object and observes the window notifications of the target's window.
 *
 * @param hTarget           HWND (NSView*) that receives the messages; must already be in a window.
 * @return The initialized object, or nil.
 */
- (id)initWithTarget:(void*)hTarget;
@end

@implementation Synth1MouseCapture

/**
 * @brief Window notification handler: posts WM_APP + 2 to the target.
 *
 * @param notification      The notification (unused).
 */
- (void)windowWillMove:(NSNotification*)notification
{
    PostEventMac(m_hTarget, WM_APP + 2, 0, 0);
}

/**
 * @brief Window notification handler: posts WM_APP + 2 to the target.
 *
 * @param notification      The notification (unused).
 */
- (void)windowWillMiniaturize:(NSNotification*)notification
{
    PostEventMac(m_hTarget, WM_APP + 2, 0, 0);
}

/**
 * @brief Window notification handler: posts WM_APP + 2 to the target.
 *
 * @param notification      The notification (unused).
 */
- (void)windowWillClose:(NSNotification*)notification
{
    PostEventMac(m_hTarget, WM_APP + 2, 0, 0);
}

/**
 * @brief Window notification handler: posts WM_APP + 2 to the target.
 *
 * @param notification      The notification (unused).
 */
- (void)windowDidResignKey:(NSNotification*)notification
{
    PostEventMac(m_hTarget, WM_APP + 2, 0, 0);
}

- (id)initWithTarget:(void*)hTarget
{
    self = [super init];
    if(self){
        m_hTarget = hTarget;
        NSNotificationCenter* center = [NSNotificationCenter defaultCenter];
        [center addObserver:self selector:@selector(windowWillMove:) name:NSWindowWillMoveNotification object:[(NSView*)m_hTarget window]];
        [center addObserver:self selector:@selector(windowWillMiniaturize:) name:NSWindowWillMiniaturizeNotification object:[(NSView*)m_hTarget window]];
        [center addObserver:self selector:@selector(windowWillClose:) name:NSWindowWillCloseNotification object:[(NSView*)m_hTarget window]];
        [center addObserver:self selector:@selector(windowDidResignKey:) name:NSWindowDidResignKeyNotification object:[(NSView*)m_hTarget window]];
    }
    return self;
}

/**
 * @brief Stops observing the target window's notifications.
 */
- (void)dealloc
{
    NSNotificationCenter* center = [NSNotificationCenter defaultCenter];
    [center removeObserver:self name:NSWindowDidResignKeyNotification object:[(NSView*)m_hTarget window]];
    [center removeObserver:self name:NSWindowWillCloseNotification object:[(NSView*)m_hTarget window]];
    [center removeObserver:self name:NSWindowWillMiniaturizeNotification object:[(NSView*)m_hTarget window]];
    [center removeObserver:self name:NSWindowWillMoveNotification object:[(NSView*)m_hTarget window]];
    [super dealloc];
}

@end

void* initCapture(HWND hWnd)
{
    return [[Synth1MouseCapture alloc] initWithTarget:hWnd];
}

void exitCapture(void* p)
{
    [(Synth1MouseCapture*)p release];
}

int IsRunningModal(void)
{
    return [[NSApplication sharedApplication] modalWindow] != nil;
}

#endif // SYNTH1_VIEWFACTORY

void setWindowPos(HWND hWnd, int x, int y)
{
    [[(NSView*)hWnd window] setFrameOrigin:NSMakePoint(x, y)];
}

void getVisibleScreenRect(HWND hWnd, RECT* rc)
{
    NSRect visible = [[[(NSView*)hWnd window] screen] visibleFrame];
    CGRect bounds = CGDisplayBounds(CGMainDisplayID());
    rc->left = visible.origin.x;
    rc->top = bounds.size.height - (visible.origin.y + visible.size.height);
    rc->right = visible.origin.x + visible.size.width;
    rc->bottom = bounds.size.height - visible.origin.y;
}

int isSandBox(void)
{
    static int bRtn = -1;
    if(bRtn == -1){
        NSArray* paths = NSSearchPathForDirectoriesInDomains(NSDocumentDirectory, NSUserDomainMask, YES);
        NSString* path = [paths objectAtIndex:0];
        bRtn = [path rangeOfString:@"com.apple.garageband"].location != NSNotFound;
    }
    return bRtn;
}
