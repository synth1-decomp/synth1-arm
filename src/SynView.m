/**
 * @file SynView.m
 *
 * Views of the Cocoa GUI (view factory).
 *
 * - SynView: the editor panel (CSynthWin) and other custom child windows.
 * - VolView: popup windows (value display, menus).
 * - SoundSelectView: the sound select popup with the bank tree (NSOutlineView).
 * - NSTreeItem: node of the bank tree.
 * - MSMessageParam: Win32 message carried to the main thread.
 *
 * Each view translates Cocoa events into Win32 messages for its techpop window
 * (CCWindow*), see also Synth1CocoaViewFactory.mm.  The TreeView_* / Edit_*
 * functions at the end emulate the Win32 common controls used by the GUI.
 */

#import <Cocoa/Cocoa.h>
#import "SynView.h"

// (this file does not include windef.h; the Win32 emulation calls not declared
// here, e.g. GetCapture(), are implicitly declared)
struct CCWindow;
BOOL    SendMessage0(struct CCWindow *pWnd, void *hWnd, unsigned int msg, unsigned long wParam, unsigned long lParam);
void    SetWindow(void *pWnd, void *hWnd);
#ifdef SYNTH1_MODERN_BUILD
// Current compilers reject implicit declarations, and int-returning ones would truncate pointers
void    *GetCapture(int *pbControl);
#endif

#define WM_CREATE           0x0001
#define WM_CLOSE            0x0010
#define WM_PAINT            0x000F
#define WM_NOTIFY           0x004E
#define WM_COMMAND          0x0111
#define WM_MOUSEMOVE        0x0200
#define WM_LBUTTONDOWN      0x0201
#define WM_LBUTTONUP        0x0202
#define WM_RBUTTONDOWN      0x0204
#define WM_MOUSEWHEEL       0x020A

/**
 * @brief Win32 RECT layout (top-left origin) passed as wParam of WM_PAINT.
 */
typedef struct {
    int left, top, right, bottom;
} WINRECT;

/**
 * @brief Packs x into the low and y into the high 16 bits of an lParam (Win32 MAKELPARAM).
 */
#define MAKELPARAM_XY(x, y) ((long)(int)(((unsigned short)((unsigned long)(x) & 0xffff)) | ((unsigned int)((unsigned short)((unsigned long)(y) & 0xffff))) << 16))


//==============================================================================
//  SynView
//==============================================================================
@implementation SynView

- (id)initWithParam:(NSRect)frameRect synth:(void*)synthWin
{
    self = [super initWithFrame:frameRect];
    if(self){
        m_synthWin = synthWin;
        SetWindow(synthWin, self);
    }
    return self;
}

/**
 * @brief Sends WM_CREATE to the techpop window when the view is added to a window.
 */
- (void)viewDidMoveToWindow
{
    [super viewDidMoveToWindow];
    if([self window]){
        SendMessage0(m_synthWin, self, WM_CREATE, 0, 0);
    }
}

/**
 * @brief Sends WM_CLOSE, detaches the techpop window and removes the view.
 */
- (void)removeFromSuperview
{
    SendMessage0(m_synthWin, self, WM_CLOSE, 0, 0);
    m_synthWin = NULL;
    [super removeFromSuperview];
}

/**
 * @brief Delivers a message posted with PostEventMac() / SendEvent() on the main thread.
 *
 * @param param             The message; ignored once the techpop window is detached.
 */
- (void)recieveEvent:(MSMessageParam*)param
{
    if(m_synthWin){
        SendMessage0(m_synthWin, self, [param message], [param wparam], [param lparam]);
    }
}

/**
 * @brief Sends WM_PAINT with the dirty rectangle to the techpop window.
 *
 * The context is flipped to a top-left origin while the message is handled;
 * wParam points to the dirty rectangle in that orientation (WINRECT).
 *
 * @param rect              Dirty rectangle in view coordinates.
 */
- (void)drawRect:(NSRect)rect
{
    NSRect bounds = [self bounds];
    WINRECT rc;
    rc.left = rect.origin.x;
    rc.top = bounds.size.height - (rect.origin.y + rect.size.height);
    rc.right = rect.origin.x + rect.size.width;
    rc.bottom = bounds.size.height - rect.origin.y;

    CGContextRef context = (CGContextRef)[[NSGraphicsContext currentContext] graphicsPort];
    CGContextSaveGState(context);
    bounds.origin = [self convertPointToBacking:bounds.origin];
    CGContextScaleCTM(context, 1.0, -1.0);
    CGContextTranslateCTM(context, 0, -bounds.size.height);
    SendMessage0(m_synthWin, self, WM_PAINT, (unsigned long)&rc, 0);
    CGContextRestoreGState(context);
}

/**
 * @brief Sends WM_LBUTTONDOWN with the top-left based mouse position.
 *
 * @param theEvent          The mouse event.
 */
- (void)mouseDown:(NSEvent*)theEvent
{
    NSPoint pt = [self convertPoint:[theEvent locationInWindow] fromView:nil];
    NSRect bounds = [self bounds];
    int x = pt.x;
    int y = bounds.size.height - pt.y;
    long lParam = MAKELPARAM_XY(x, y);
    SendMessage0(m_synthWin, self, WM_LBUTTONDOWN, 0, lParam);
}

/**
 * @brief Sends WM_MOUSEMOVE and WM_RBUTTONDOWN with the top-left based mouse position, then passes the event on.
 *
 * @param theEvent          The mouse event.
 */
- (void)rightMouseDown:(NSEvent*)theEvent
{
    NSPoint pt = [self convertPoint:[theEvent locationInWindow] fromView:nil];
    NSRect bounds = [self bounds];
    int x = pt.x;
    int y = bounds.size.height - pt.y;
    long lParam = MAKELPARAM_XY(x, y);
    SendMessage0(m_synthWin, self, WM_MOUSEMOVE, 0, lParam);
    SendMessage0(m_synthWin, self, WM_RBUTTONDOWN, 0, lParam);
    [super rightMouseDown:theEvent];
}

/**
 * @brief Sends WM_LBUTTONUP with the top-left based mouse position.
 *
 * @param theEvent          The mouse event.
 */
- (void)mouseUp:(NSEvent*)theEvent
{
    NSPoint pt = [self convertPoint:[theEvent locationInWindow] fromView:nil];
    NSRect bounds = [self bounds];
    int x = pt.x;
    int y = bounds.size.height - pt.y;
    long lParam = MAKELPARAM_XY(x, y);
    SendMessage0(m_synthWin, self, WM_LBUTTONUP, 0, lParam);
}

/**
 * @brief Sends WM_MOUSEMOVE with the top-left based mouse position.
 *
 * @param theEvent          The mouse event.
 */
- (void)mouseDragged:(NSEvent*)theEvent
{
    NSPoint pt = [self convertPoint:[theEvent locationInWindow] fromView:nil];
    NSRect bounds = [self bounds];
    int x = pt.x;
    int y = bounds.size.height - pt.y;
    long lParam = MAKELPARAM_XY(x, y);
    SendMessage0(m_synthWin, self, WM_MOUSEMOVE, 0, lParam);
}

/**
 * @brief Sends WM_MOUSEMOVE with the top-left based mouse position.
 *
 * @param theEvent          The mouse event.
 */
- (void)mouseMoved:(NSEvent*)theEvent
{
    NSPoint pt = [self convertPoint:[theEvent locationInWindow] fromView:nil];
    NSRect bounds = [self bounds];
    int x = pt.x;
    int y = bounds.size.height - pt.y;
    long lParam = MAKELPARAM_XY(x, y);
    SendMessage0(m_synthWin, self, WM_MOUSEMOVE, 0, lParam);
}

- (void)sendMessage:(unsigned int)msg wParam:(unsigned long)wParam lParam:(unsigned long)lParam
{
    SendMessage0(m_synthWin, self, msg, wParam, lParam);
}

- (void)setWin:(void*)synthWin
{
    m_synthWin = synthWin;
}

@end


//==============================================================================
//  VolView
//==============================================================================
@implementation VolView

- (id)initWithWnd:(NSRect)frameRect win:(void*)synthWin
{
    m_synthWin = synthWin;
    return [self initWithFrame:frameRect];
}

/**
 * @brief Initializes the view and registers it as the HWND of the techpop window set by initWithWnd:win:.
 *
 * @param frameRect         Frame in the superview's coordinates.
 * @return The initialized view, or nil.
 */
- (id)initWithFrame:(NSRect)frameRect
{
    self = [super initWithFrame:frameRect];
    if(self){
        [[self window] setAcceptsMouseMovedEvents:YES];
        SetWindow(m_synthWin, self);
    }
    return self;
}

/**
 * @brief Sends WM_CREATE to the techpop window when the view is added to a window.
 */
- (void)viewDidMoveToWindow
{
    [super viewDidMoveToWindow];
    if([self window]){
        SendMessage0(m_synthWin, self, WM_CREATE, 0, 0);
    }
}

/**
 * @brief Sends WM_CLOSE, detaches the techpop window and removes the view.
 */
- (void)removeFromSuperview
{
    SendMessage0(m_synthWin, self, WM_CLOSE, 0, 0);
    m_synthWin = NULL;
    [super removeFromSuperview];
}

/**
 * @brief Sends WM_PAINT with the dirty rectangle to the techpop window.
 *
 * The context is flipped to a top-left origin while the message is handled;
 * wParam points to the dirty rectangle in that orientation (WINRECT).
 *
 * @param rect              Dirty rectangle in view coordinates.
 */
- (void)drawRect:(NSRect)rect
{
    NSRect bounds = [self bounds];
    WINRECT rc;
    rc.left = rect.origin.x;
    rc.top = bounds.size.height - (rect.origin.y + rect.size.height);
    rc.right = rect.origin.x + rect.size.width;
    rc.bottom = bounds.size.height - rect.origin.y;

    CGContextRef context = (CGContextRef)[[NSGraphicsContext currentContext] graphicsPort];
    CGContextSaveGState(context);
    bounds.origin = [self convertPointToBacking:bounds.origin];
    CGContextScaleCTM(context, 1.0, -1.0);
    CGContextTranslateCTM(context, 0, -bounds.size.height);
    SendMessage0(m_synthWin, self, WM_PAINT, (unsigned long)&rc, 0);
    CGContextRestoreGState(context);
}

/**
 * @brief Sends WM_MOUSEMOVE and WM_LBUTTONDOWN with the top-left based mouse position.
 *
 * @param theEvent          The mouse event.
 */
- (void)mouseDown:(NSEvent*)theEvent
{
    NSPoint pt = [self convertPoint:[theEvent locationInWindow] fromView:nil];
    NSRect bounds = [self bounds];
    int x = pt.x;
    int y = bounds.size.height - pt.y;
    long lParam = MAKELPARAM_XY(x, y);
    SendMessage0(m_synthWin, self, WM_MOUSEMOVE, 0, lParam);
    SendMessage0(m_synthWin, self, WM_LBUTTONDOWN, 0, lParam);
}

/**
 * @brief Sends WM_MOUSEMOVE and WM_RBUTTONDOWN with the top-left based mouse position, then passes the event on.
 *
 * @param theEvent          The mouse event.
 */
- (void)rightMouseDown:(NSEvent*)theEvent
{
    NSPoint pt = [self convertPoint:[theEvent locationInWindow] fromView:nil];
    NSRect bounds = [self bounds];
    int x = pt.x;
    int y = bounds.size.height - pt.y;
    long lParam = MAKELPARAM_XY(x, y);
    SendMessage0(m_synthWin, self, WM_MOUSEMOVE, 0, lParam);
    SendMessage0(m_synthWin, self, WM_RBUTTONDOWN, 0, lParam);
    [super rightMouseDown:theEvent];
}

/**
 * @brief Sends WM_LBUTTONUP with the top-left based mouse position.
 *
 * @param theEvent          The mouse event.
 */
- (void)mouseUp:(NSEvent*)theEvent
{
    NSPoint pt = [self convertPoint:[theEvent locationInWindow] fromView:nil];
    NSRect bounds = [self bounds];
    int x = pt.x;
    int y = bounds.size.height - pt.y;
    long lParam = MAKELPARAM_XY(x, y);
    SendMessage0(m_synthWin, self, WM_LBUTTONUP, 0, lParam);
}

/**
 * @brief Sends WM_MOUSEMOVE with the top-left based mouse position.
 *
 * @param theEvent          The mouse event.
 */
- (void)mouseDragged:(NSEvent*)theEvent
{
    NSPoint pt = [self convertPoint:[theEvent locationInWindow] fromView:nil];
    NSRect bounds = [self bounds];
    int x = pt.x;
    int y = bounds.size.height - pt.y;
    long lParam = MAKELPARAM_XY(x, y);
    SendMessage0(m_synthWin, self, WM_MOUSEMOVE, 0, lParam);
}

/**
 * @brief Sends WM_MOUSEMOVE, unless another window holds the mouse capture.
 *
 * @param theEvent          The mouse event.
 */
- (void)mouseMoved:(NSEvent*)theEvent
{
    NSPoint pt = [self convertPoint:[theEvent locationInWindow] fromView:nil];
    NSRect bounds = [self bounds];
    int x = pt.x;
    int y = bounds.size.height - pt.y;
    long lParam = MAKELPARAM_XY(x, y);
    // while a window has captured the mouse, only it gets the moves
    void* hWnd = GetCapture(NULL);
    if(hWnd == NULL){
        SendMessage0(m_synthWin, self, WM_MOUSEMOVE, 0, lParam);
    }else if(hWnd == m_synthWin){
        SendMessage0(hWnd, self, WM_MOUSEMOVE, 0, lParam);
    }
}

- (void)sendMessage:(unsigned int)msg wParam:(unsigned long)wParam lParam:(unsigned long)lParam
{
    SendMessage0(m_synthWin, self, msg, wParam, lParam);
}

/**
 * @brief Sends WM_MOUSEWHEEL with the vertical delta times 120 in the high word of wParam.
 *
 * @param theEvent          The scroll event (also passed to the superclass first).
 */
- (void)scrollWheel:(NSEvent*)theEvent
{
    [super scrollWheel:theEvent];
    [theEvent deltaX];
    float delta = [theEvent deltaY];
    [theEvent deltaZ];
    int wheel = delta * 120.0f;
    SendMessage0(m_synthWin, self, WM_MOUSEWHEEL, wheel << 16, 0);
}

/**
 * @brief Delivers a message posted with PostEventMac() / SendEvent() on the main thread.
 *
 * @param param             The message; ignored once the techpop window is detached.
 */
- (void)recieveEvent:(MSMessageParam*)param
{
    if(m_synthWin){
        SendMessage0(m_synthWin, self, [param message], [param wparam], [param lparam]);
    }
}

@end


//==============================================================================
//  SoundSelectView
//==============================================================================
@implementation SoundSelectView

- (id)initWithWnd:(NSRect)frameRect win:(void*)synthWin
{
    m_synthWin = synthWin;
    return [self initWithFrame:frameRect];
}

- (void)setOutlineView:(NSOutlineView*)outline
{
    m_outline = outline;
}

/**
 * @brief Tells AppKit that the view accepts key focus.
 *
 * @return Always YES.
 */
- (BOOL)acceptsFirstResponder
{
    return YES;
}

/**
 * @brief Accepts becoming first responder.
 *
 * @return Always YES.
 */
- (BOOL)becomeFirstResponder
{
    return YES;
}

/**
 * @brief Initializes the view, creates the empty tree root and registers the view as the HWND of the techpop window set by initWithWnd:win:.
 *
 * @param frameRect         Frame in the superview's coordinates.
 * @return The initialized view, or nil.
 */
- (id)initWithFrame:(NSRect)frameRect
{
    self = [super initWithFrame:frameRect];
    if(self){
        m_root = [[NSTreeItem alloc] initWithParam:"" lparam:NULL parent:nil];
        [[self window] setAcceptsMouseMovedEvents:YES];
        SetWindow(m_synthWin, self);
    }
    return self;
}

/**
 * @brief Releases the tree root.
 */
- (void)dealloc
{
    [m_root release];
    [super dealloc];
}

/**
 * @brief Sends WM_CREATE when the view is added to a window and installs a tracking rectangle over the bounds.
 */
- (void)viewDidMoveToWindow
{
    [super viewDidMoveToWindow];
    if([self window]){
        SendMessage0(m_synthWin, self, WM_CREATE, 0, 0);
    }
    NSRect rect = [self bounds];
    mTrackRect = [self addTrackingRect:[self bounds] owner:self userData:nil assumeInside:NO];
}

/**
 * @brief Sends WM_CLOSE, detaches the techpop window and removes the view.
 */
- (void)removeFromSuperview
{
    SendMessage0(m_synthWin, self, WM_CLOSE, 0, 0);
    m_synthWin = NULL;
    [super removeFromSuperview];
}

/**
 * @brief Sends WM_PAINT with the dirty rectangle and the drawing context to the techpop window.
 *
 * The context is flipped to a top-left origin and its text matrix reset while the
 * message is handled; wParam points to the dirty rectangle in that orientation
 * (WINRECT), lParam is the CGContextRef.
 *
 * @param rect              Dirty rectangle in view coordinates.
 */
- (void)drawRect:(NSRect)rect
{
    NSRect bounds = [self bounds];
    WINRECT rc;
    rc.left = rect.origin.x;
    rc.top = bounds.size.height - (rect.origin.y + rect.size.height);
    rc.right = rect.origin.x + rect.size.width;
    rc.bottom = bounds.size.height - rect.origin.y;

    CGContextRef context = (CGContextRef)[[NSGraphicsContext currentContext] CGContext];
    CGContextSaveGState(context);
    bounds.origin = [self convertPointToBacking:bounds.origin];
    CGContextSetTextMatrix(context, CGAffineTransformMake(1, 0, 0, 1, 0, 0));
    CGContextScaleCTM(context, 1.0, -1.0);
    CGContextTranslateCTM(context, 0, -bounds.size.height);
    SendMessage0(m_synthWin, self, WM_PAINT, (unsigned long)&rc, (unsigned long)context);
    CGContextRestoreGState(context);
}

/**
 * @brief Sends WM_MOUSEMOVE and WM_LBUTTONDOWN with the top-left based mouse position, then passes the event on.
 *
 * @param theEvent          The mouse event.
 */
- (void)mouseDown:(NSEvent*)theEvent
{
    NSPoint pt = [self convertPoint:[theEvent locationInWindow] fromView:nil];
    NSRect bounds = [self bounds];
    int x = pt.x;
    int y = bounds.size.height - pt.y;
    long lParam = MAKELPARAM_XY(x, y);
    SendMessage0(m_synthWin, self, WM_MOUSEMOVE, 0, lParam);
    SendMessage0(m_synthWin, self, WM_LBUTTONDOWN, 0, lParam);
    [super mouseDown:theEvent];
}

/**
 * @brief Sends WM_MOUSEMOVE and WM_RBUTTONDOWN with the top-left based mouse position, then passes the event on.
 *
 * @param theEvent          The mouse event.
 */
- (void)rightMouseDown:(NSEvent*)theEvent
{
    NSPoint pt = [self convertPoint:[theEvent locationInWindow] fromView:nil];
    NSRect bounds = [self bounds];
    int x = pt.x;
    int y = bounds.size.height - pt.y;
    long lParam = MAKELPARAM_XY(x, y);
    SendMessage0(m_synthWin, self, WM_MOUSEMOVE, 0, lParam);
    SendMessage0(m_synthWin, self, WM_RBUTTONDOWN, 0, lParam);
    [super rightMouseDown:theEvent];
}

/**
 * @brief Sends WM_LBUTTONUP with the top-left based mouse position, then passes the event on.
 *
 * @param theEvent          The mouse event.
 */
- (void)mouseUp:(NSEvent*)theEvent
{
    NSPoint pt = [self convertPoint:[theEvent locationInWindow] fromView:nil];
    NSRect bounds = [self bounds];
    int x = pt.x;
    int y = bounds.size.height - pt.y;
    long lParam = MAKELPARAM_XY(x, y);
    SendMessage0(m_synthWin, self, WM_LBUTTONUP, 0, lParam);
    [super mouseUp:theEvent];
}

/**
 * @brief Sends WM_MOUSEMOVE with the top-left based mouse position, then passes the event on.
 *
 * @param theEvent          The mouse event.
 */
- (void)mouseDragged:(NSEvent*)theEvent
{
    NSPoint pt = [self convertPoint:[theEvent locationInWindow] fromView:nil];
    NSRect bounds = [self bounds];
    int x = pt.x;
    int y = bounds.size.height - pt.y;
    long lParam = MAKELPARAM_XY(x, y);
    SendMessage0(m_synthWin, self, WM_MOUSEMOVE, 0, lParam);
    [super mouseDragged:theEvent];
}

/**
 * @brief Sends WM_MOUSEMOVE unless another window holds the mouse capture, then passes the event on.
 *
 * @param theEvent          The mouse event.
 */
- (void)mouseMoved:(NSEvent*)theEvent
{
    NSPoint pt = [self convertPoint:[theEvent locationInWindow] fromView:nil];
    NSRect bounds = [self bounds];
    int x = pt.x;
    int y = bounds.size.height - pt.y;
    long lParam = MAKELPARAM_XY(x, y);
    void* hWnd = GetCapture(NULL);
    if(hWnd == NULL){
        SendMessage0(m_synthWin, self, WM_MOUSEMOVE, 0, lParam);
    }else if(hWnd == m_synthWin){
        SendMessage0(hWnd, self, WM_MOUSEMOVE, 0, lParam);
    }
    [super mouseMoved:theEvent];
}

- (void)sendMessage:(unsigned int)msg wParam:(unsigned long)wParam lParam:(unsigned long)lParam
{
    SendMessage0(m_synthWin, self, msg, wParam, lParam);
}

// NSOutlineView data source
/**
 * @brief NSOutlineView data source: counts the children of an item.
 *
 * @param outlineView       The outline view (unused; the view uses its own tree).
 * @param item              NSTreeItem to query, or nil for the root.
 * @return The number of children of @p item.
 */
- (int)outlineView:(NSOutlineView*)outlineView numberOfChildrenOfItem:(id)item
{
    if(item == nil) item = m_root;
    return [item numberOfChildren];
}

/**
 * @brief NSOutlineView data source: tells whether an item can be expanded.
 *
 * @param outlineView       The outline view (unused; the view uses its own tree).
 * @param item              NSTreeItem to query, or nil for the root.
 * @return YES for nil (the root) and for items with children, otherwise NO.
 */
- (BOOL)outlineView:(NSOutlineView*)outlineView isItemExpandable:(id)item
{
    if(item == nil) return YES;
    return [item numberOfChildren] != 0;
}

/**
 * @brief NSOutlineView data source: gets a child of an item.
 *
 * @param outlineView       The outline view (unused; the view uses its own tree).
 * @param index             0-based child index.
 * @param item              NSTreeItem to query, or nil for the root.
 * @return The child NSTreeItem at @p index.
 */
- (id)outlineView:(NSOutlineView*)outlineView child:(int)index ofItem:(id)item
{
    if(item == nil) item = m_root;
    return [item child:index];
}

/**
 * @brief NSOutlineView data source: gets the text shown for an item.
 *
 * @param outlineView       The outline view (unused; the view uses its own tree).
 * @param tableColumn       The column (unused; every column shows the title).
 * @param item              NSTreeItem to show.
 * @return The item's title.
 */
- (id)outlineView:(NSOutlineView*)outlineView objectValueForTableColumn:(NSTableColumn*)tableColumn byItem:(id)item
{
    return [item title];
}

- (void)addItem:(NSTreeItem*)item
{
    [m_root add:item];
}

- (NSTreeItem*)root
{
    return m_root;
}

- (void)pushButton:(id)sender
{
    SendMessage0(m_synthWin, self, WM_COMMAND, [sender tag], 0);
}

// NSOutlineView delegate
/**
 * @brief NSOutlineView delegate: sends WM_NOTIFY with the selected item's param as lParam.
 *
 * @param notification      The notification (unused).
 */
- (void)outlineViewSelectionDidChange:(NSNotification*)notification
{
    int row = [m_outline selectedRow];
    NSTreeItem* item = [m_outline itemAtRow:row];
    SendMessage0(m_synthWin, self, WM_NOTIFY, 0, [item param]);
}

/**
 * @brief Delivers a message posted with PostEventMac() / SendEvent() on the main thread.
 *
 * @param param             The message; ignored once the techpop window is detached.
 */
- (void)recieveEvent:(MSMessageParam*)param
{
    if(m_synthWin){
        SendMessage0(m_synthWin, self, [param message], [param wparam], [param lparam]);
    }
}

// a click on a bank opens / closes it (with option: including the sub banks)
- (void)outlineViewClicked:(id)sender
{
    id item = [sender itemAtRow:[sender clickedRow]];
    if(item){
        BOOL bChildren = ([[NSApp currentEvent] modifierFlags] & NSAlternateKeyMask) != 0;
        if([sender isItemExpanded:item]){
            [[sender animator] collapseItem:item collapseChildren:bChildren];
        }else{
            [[sender animator] expandItem:item expandChildren:bChildren];
        }
    }
}

@end


//==============================================================================
//  NSTreeItem
//==============================================================================
@implementation NSTreeItem

- (id)initWithParam:(const char*)title lparam:(void*)param parent:(NSTreeItem*)parent
{
    self = [super init];
    if(self){
        m_title = [[NSString alloc] initWithCString:title encoding:NSUTF8StringEncoding];
        m_param = (unsigned long)param;
        m_child = [[NSMutableArray alloc] init];
        m_parent = parent;
    }
    return self;
}

/**
 * @brief Releases the title and the child array.
 */
- (void)dealloc
{
    if(m_title){
        [m_title release];
        m_title = nil;
    }
    if(m_child){
        [m_child release];
        m_child = nil;
    }
    [super dealloc];
}

- (int)numberOfChildren
{
    if(m_child == nil) return 0;
    return [m_child count];
}

- (id)child:(int)index
{
    if(m_child){
        return [m_child objectAtIndex:index];
    }
    return nil;
}

- (NSString*)title
{
    return m_title;
}

- (void)add:(NSTreeItem*)item
{
    if(m_child){
        [m_child addObject:item];
    }
}

- (int)index:(NSTreeItem*)item
{
    return [m_child indexOfObject:item];
}

- (unsigned long)param
{
    return m_param;
}

- (NSTreeItem*)parent
{
    return m_parent;
}

@end


//==============================================================================
//  MSMessageParam
//==============================================================================
@implementation MSMessageParam

- (id)initWithParam:(unsigned int)message wparam:(unsigned long)wparam lparam:(unsigned long)lparam
{
    if((self = [super init])){
        m_message = message;
        m_wparam = wparam;
        m_lparam = lparam;
    }
    return self;
}

- (unsigned int)message
{
    return m_message;
}

- (unsigned long)wparam
{
    return m_wparam;
}

- (unsigned long)lparam
{
    return m_lparam;
}

@end


//==============================================================================
//  Win32 tree view / edit control emulation
//  (HWND of the tree view = the NSScrollView created by CreateOutlineView)
//==============================================================================

void* TreeView_GetRoot(void* hTV);

void* TreeView_InsertItem(void* hTV, void* hParentItem, const char* text, void* lParam)
{
    NSTreeItem* parent = hParentItem;
    if(parent == nil){
        parent = TreeView_GetRoot(hTV);
    }
    NSTreeItem* item = [[NSTreeItem alloc] initWithParam:text lparam:lParam parent:parent];
    if(parent){
        [parent add:item];
    }else{
        [(SoundSelectView*)[(NSView*)hTV superview] addItem:item];
    }
    [item release];
    return item;
}

void* TreeView_GetItemParam(void* hTV, void* hItem)
{
    return (void*)[(NSTreeItem*)hItem param];
}

static void TreeView_CollapseHierarchy(void* hTV, NSTreeItem* parent, NSTreeItem* item);

// selects the item and collapses all other branches
void TreeView_SelectItem(void* hTV, void* hItem)
{
    if([(NSTreeItem*)hItem parent]){
        TreeView_CollapseHierarchy(hTV, [(NSTreeItem*)hItem parent], hItem);
    }
    NSOutlineView* outline = [(NSScrollView*)hTV documentView];
    int row = [outline rowForItem:hItem];
    if(row != -1){
        NSIndexSet* indexes = [[NSIndexSet alloc] initWithIndex:row];
        [outline selectRowIndexes:indexes byExtendingSelection:NO];
        [indexes release];
        [outline scrollRowToVisible:row];
    }
}

void* TreeView_GetRoot(void* hTV)
{
    return [(SoundSelectView*)[(NSView*)hTV superview] root];
}

void* TreeView_GetChild(void* hTV, void* hItem)
{
    if([(NSTreeItem*)hItem numberOfChildren] > 0){
        return [(NSTreeItem*)hItem child:0];
    }
    return NULL;
}

void* TreeView_GetNextSibling(void* hTV, void* hItem)
{
    NSTreeItem* parent = [(NSTreeItem*)hItem parent];
    int index = [parent index:hItem];
    if(index != -1){
        if(index + 1 < [parent numberOfChildren]){
            return [parent child:index + 1];
        }
    }
    return NULL;
}

bool TreeView_GetItemRect(void* hTV, void* hItem, int* rc, int bTextOnly)
{
    NSOutlineView* outline = [(NSScrollView*)hTV documentView];
    int row = [outline rowForItem:hItem];
    if(row == -1) return false;
    NSRect cell = [outline frameOfCellAtColumn:0 row:row];
    NSRect r = [outline convertRect:cell toView:hTV];
    rc[0] = r.origin.x;
    rc[1] = r.origin.y;
    rc[2] = r.origin.x + r.size.width;
    rc[3] = r.origin.y + r.size.height;
    return true;
}

void TreeView_Update(void* hTV)
{
    [[(NSScrollView*)hTV documentView] reloadData];
}

void TreeView_Expand(void* hTV, void* hItem, BOOL bExpand)
{
    if(bExpand){
        [[(NSScrollView*)hTV documentView] expandItem:hItem];
    }else{
        [[(NSScrollView*)hTV documentView] collapseItem:hItem];
    }
}

/**
 * @brief Expands parent (and its ancestors) and collapses all children of parent except item.
 *
 * @param hTV               Tree view HWND (the NSScrollView created by CreateOutlineView()).
 * @param parent            Node whose other children are collapsed.
 * @param item              Child of parent to keep; nil does nothing.
 */
static void TreeView_CollapseHierarchy(void* hTV, NSTreeItem* parent, NSTreeItem* item)
{
    if(item == nil) return;
    for(int i=0;i<[parent numberOfChildren];i++){
        if([parent child:i] != item){
            NSTreeItem* child = [parent child:i];
            [[(NSScrollView*)hTV documentView] collapseItem:child];
        }
    }
    if([parent parent]){
        TreeView_CollapseHierarchy(hTV, [parent parent], parent);
    }
    [[(NSScrollView*)hTV documentView] expandItem:parent];
}

void TreeItem_Destroy(void* hItem)
{
}

int Edit_GetText(void* hEdit, char* text, int size)
{
    strncpy(text, [[(NSTextField*)hEdit stringValue] UTF8String], size);
    return strlen(text);
}

// the text may come from an old (Shift-JIS) patch file
void Edit_SetText(void* hEdit, const char* text)
{
    NSTextField* edit = (NSTextField*)hEdit;
    @try{
        [edit setStringValue:[NSString stringWithUTF8String:text]];
    }
    @catch(NSException* e){
        @try{
            [edit setStringValue:[NSString stringWithCString:text encoding:NSShiftJISStringEncoding]];
        }
        @catch(NSException* e){
            @try{
                [edit setStringValue:[NSString stringWithCString:text encoding:NSASCIIStringEncoding]];
            }
            @catch(NSException* e){
            }
        }
    }
}

void Edit_SetReadOnly(void* hEdit, BOOL bReadOnly)
{
    [(NSTextField*)hEdit setEditable:!bReadOnly];
}

void EnableWindow(void* hWnd, BOOL bEnable)
{
    [(NSView*)hWnd setHidden:!bEnable];
}

void* CreateCustumWindow(int x, int y, int w, int h, NSWindow* parent, void* synthWin)
{
    SynView* view = [[SynView alloc] initWithParam:NSMakeRect(x, y, w, h) synth:synthWin];
    [[parent contentView] addSubview:view];
    return view;
}

void DestroyCustumWindow(void* hWnd)
{
}

// returns the string as UTF-8 (patch names of old banks may be Shift-JIS or ASCII)
const char* GetEncodedString(const char* str)
{
    if(*str == 0) return str;
    NSString* s = [NSString stringWithUTF8String:str];
    if(s == nil || [s length] == 0) s = [NSString stringWithCString:str encoding:NSShiftJISStringEncoding];
    if(s == nil || [s length] == 0) s = [NSString stringWithCString:str encoding:NSASCIIStringEncoding];
    if(s == nil || [s length] == 0) s = @"not utf8";
    return [s UTF8String];
}
