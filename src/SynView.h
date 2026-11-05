/**
 * @file SynView.h
 *
 * Views of the Cocoa GUI (view factory only, SynView.m).  Each view forwards
 * Cocoa events as Win32 messages to its techpop window (CCWindow*) through
 * SendMessage0().
 *
 * Mouse coordinates are converted to a top-left origin and packed into lParam
 * like Win32 (x in the low 16 bits, y in the high 16 bits).
 */
#import <Cocoa/Cocoa.h>

/**
 * @brief Win32 message carried to the main thread (PostMessage / SendMessage in winobj_au.m).
 *
 * Immutable holder of message, wParam and lParam; passed to a view's recieveEvent:
 * through performSelectorOnMainThread.
 */
@interface MSMessageParam : NSObject
{
    unsigned int    m_message;
    unsigned long   m_wparam;
    unsigned long   m_lparam;
}

/**
 * @brief Initializes the message holder.
 *
 * @param message           Win32 message number.
 * @param wparam            wParam of the message.
 * @param lparam            lParam of the message.
 * @return The initialized object, or nil.
 */
- (id)initWithParam:(unsigned int)message wparam:(unsigned long)wparam lparam:(unsigned long)lparam;

/**
 * @brief Gets the message number.
 *
 * @return The Win32 message number.
 */
- (unsigned int)message;

/**
 * @brief Gets the wParam.
 *
 * @return The wParam of the message.
 */
- (unsigned long)wparam;

/**
 * @brief Gets the lParam.
 *
 * @return The lParam of the message.
 */
- (unsigned long)lparam;
@end

/**
 * @brief View of the main panel (CSynthWin) and of other custom child windows.
 *
 * Sends WM_CREATE when added to a window, WM_CLOSE when removed, WM_PAINT for
 * drawing (in a flipped, top-left origin context) and the mouse messages.
 */
@interface SynView : NSView
{
    void*   m_synthWin;     // CCWindow*
}

/**
 * @brief Initializes the view and registers it as the HWND of the techpop window (SetWindow()).
 *
 * @param frameRect         Frame in the superview's coordinates.
 * @param synthWin          Techpop window (CCWindow*) that receives the messages.
 * @return The initialized view, or nil.
 */
- (id)initWithParam:(NSRect)frameRect synth:(void*)synthWin;

/**
 * @brief Sends a Win32 message to the techpop window (SendMessage0()).
 *
 * @param msg               Message number.
 * @param wParam            wParam of the message.
 * @param lParam            lParam of the message.
 */
- (void)sendMessage:(unsigned int)msg wParam:(unsigned long)wParam lParam:(unsigned long)lParam;

/**
 * @brief Sets the techpop window (CCWindow*) that receives the messages.
 *
 * @param synthWin          Techpop window (CCWindow*), or NULL to detach the view.
 */
- (void)setWin:(void*)synthWin;
@end

/**
 * @brief View of the popup windows (volume / value windows, popup menus).
 *
 * Like SynView, plus mouse wheel messages; mouse moves go only to the window
 * holding the mouse capture, if any.
 */
@interface VolView : NSView
{
    void*   m_synthWin;     // CCWindow*
}

/**
 * @brief Initializes the view for a techpop window and registers it as its HWND.
 *
 * @param frameRect         Frame in the superview's coordinates.
 * @param synthWin          Techpop window (CCWindow*) that receives the messages.
 * @return The initialized view, or nil.
 */
- (id)initWithWnd:(NSRect)frameRect win:(void*)synthWin;

/**
 * @brief Sends a Win32 message to the techpop window (SendMessage0()).
 *
 * @param msg               Message number.
 * @param wParam            wParam of the message.
 * @param lParam            lParam of the message.
 */
- (void)sendMessage:(unsigned int)msg wParam:(unsigned long)wParam lParam:(unsigned long)lParam;
@end

@class NSTreeItem;

/**
 * @brief View of the sound (program) select popup with the bank tree.
 *
 * Hosts an NSOutlineView (created by CreateOutlineView()) and is its data source,
 * delegate and target; the tree data is a hierarchy of NSTreeItem below m_root.
 * Selecting an item sends WM_NOTIFY with the item's param as lParam; buttons send
 * WM_COMMAND with their tag as wParam.
 * @note Owns m_root.
 */
@interface SoundSelectView : NSView
{
    NSOutlineView*  m_outline;
    void*           m_synthWin;     // CCWindow*
    NSTreeItem*     m_root;
    NSTrackingRectTag   mTrackRect;
}

/**
 * @brief Initializes the view for a techpop window, creates the empty tree root and registers the view as its HWND.
 *
 * @param frameRect         Frame in the superview's coordinates.
 * @param synthWin          Techpop window (CCWindow*) that receives the messages.
 * @return The initialized view, or nil.
 */
- (id)initWithWnd:(NSRect)frameRect win:(void*)synthWin;

/**
 * @brief Sets the outline view whose selection is reported (not retained).
 *
 * @param outline           The outline view (created by CreateOutlineView()).
 */
- (void)setOutlineView:(NSOutlineView*)outline;

/**
 * @brief Sends a Win32 message to the techpop window (SendMessage0()).
 *
 * @param msg               Message number.
 * @param wParam            wParam of the message.
 * @param lParam            lParam of the message.
 */
- (void)sendMessage:(unsigned int)msg wParam:(unsigned long)wParam lParam:(unsigned long)lParam;

/**
 * @brief Adds a top level item to the tree (the root retains it).
 *
 * @param item              Item to add below the root.
 */
- (void)addItem:(NSTreeItem*)item;

/**
 * @brief Gets the invisible root item of the tree.
 *
 * @return The root item (owned by the view).
 */
- (NSTreeItem*)root;

/**
 * @brief Button action: sends WM_COMMAND with the button's tag as wParam.
 *
 * @param sender            The button (created by CreateButton()).
 */
- (void)pushButton:(id)sender;

/**
 * @brief Outline view action: expands or collapses the clicked item.
 *
 * With the option key held, the item's children are expanded / collapsed too.
 *
 * @param sender            The outline view.
 */
- (void)outlineViewClicked:(id)sender;
@end

/**
 * @brief One node of the bank tree shown by SoundSelectView.
 *
 * Holds a title, an opaque parameter (the Win32 TreeView lParam) and its children.
 * @note Owns the title and the child array (children are retained by the array);
 *       the parent is not retained.
 */
@interface NSTreeItem : NSObject
{
    NSString*       m_title;
    unsigned long   m_param;
    NSMutableArray* m_child;
    NSTreeItem*     m_parent;
}

/**
 * @brief Initializes the node with an empty child list.
 *
 * @param title             UTF-8 text shown in the tree.
 * @param param             Opaque value returned by param.
 * @param parent            Parent node, or nil for a root (not retained).
 * @return The initialized node, or nil.
 */
- (id)initWithParam:(const char*)title lparam:(void*)param parent:(NSTreeItem*)parent;

/**
 * @brief Counts the children of the node.
 *
 * @return The number of children, or 0 if there is no child array.
 */
- (int)numberOfChildren;

/**
 * @brief Gets a child of the node.
 *
 * @param index             0-based child index (must be in range).
 * @return The child at @p index, or nil if there is no child array.
 */
- (id)child:(int)index;

/**
 * @brief Gets the title.
 *
 * @return The title shown in the tree.
 */
- (NSString*)title;

/**
 * @brief Appends a child (retained by the child array).
 *
 * @param item              Child node to append; its parent is not changed.
 */
- (void)add:(NSTreeItem*)item;

/**
 * @brief Finds the index of a child.
 *
 * @param item              Child node to look up.
 * @return The 0-based index, or NSNotFound truncated to int if @p item is not a child.
 */
- (int)index:(NSTreeItem*)item;

/**
 * @brief Gets the opaque parameter given at initialization.
 *
 * @return The parameter (the Win32 TreeView lParam).
 */
- (unsigned long)param;

/**
 * @brief Gets the parent node.
 *
 * @return The parent node, or nil for a root.
 */
- (NSTreeItem*)parent;
@end
