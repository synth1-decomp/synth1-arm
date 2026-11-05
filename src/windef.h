/**
 * @file windef.h
 * @brief Minimal Win32 emulation for the Mac port of Synth1.
 *
 * The techpop sources were written against the Win32 API; on the Mac these
 * types are mapped onto CoreGraphics / CoreFoundation objects and the
 * functions are implemented in winemu_au.cpp (C++), winobj_au.m (Cocoa),
 * SynView.m (tree view / edit control emulation) and windef.cpp.
 *
 * Coordinates follow the Win32 convention unless noted otherwise: client
 * coordinates have their origin at the top left of the view, and screen
 * coordinates have their origin at the top left of the main display, with y
 * growing downwards.
 *
 * Type facts recovered from mangled names / ObjC type encodings:
 * @verbatim
   HDC      = CGContextRef            BitBlt(CGContext*, ...)
   HBITMAP  = CGImageRef              GetObject(CGImage*, int, void*)
   HWND     = void* (an NSView*)      CCWindow::myWndProc(void*, int, unsigned long, unsigned long)
   UINT msg = int, WPARAM = LPARAM = unsigned long ("Q" in -[MSMessageParam initWithParam:wparam:lparam:])
   HANDLE   = void*                   FindNextFile(void*, _WIN32_FIND_DATA*)
   @endverbatim
 */
#ifndef __WINDEF_H__
#define __WINDEF_H__

#include <ApplicationServices/ApplicationServices.h>

#if defined(__cplusplus)
// C++ and Objective-C++: BOOL is int.  In .mm files this macro also overrides
// Objective-C's BOOL (include Cocoa before windef.h); the view factory's
// methods are typed 'i' (-[Synth1CocoaView acceptsFirstResponder]).
#define BOOL    int
#elif !defined(__OBJC__)
typedef int             BOOL;
#endif
// (in plain Objective-C, BOOL stays signed char; the C-linkage functions below use int)
typedef unsigned char   BYTE;
typedef unsigned short  WORD;
typedef unsigned int    DWORD;
typedef unsigned int    UINT;
typedef int             LONG;
typedef short           SHORT;
typedef char            CHAR;
typedef char            TCHAR;
typedef float           FLOAT;
typedef unsigned long   WPARAM;
typedef unsigned long   LPARAM;
typedef long            LRESULT;
typedef unsigned int    COLORREF;
typedef void            *LPVOID;
typedef char            *LPSTR;
typedef const char      *LPCSTR;
typedef char            *LPTSTR;
typedef const char      *LPCTSTR;
typedef BYTE            *LPBYTE;
typedef DWORD           *LPDWORD;

typedef void            *HANDLE;
typedef void            *HWND;
typedef void            *HINSTANCE;
typedef void            *HMODULE;
typedef void            *HMENU;
typedef void            *HFONT;
typedef void            *HBRUSH;
typedef void            *HPEN;
typedef void            *HGDIOBJ;
typedef CGContextRef    HDC;
typedef CGImageRef      HBITMAP;

#ifndef TRUE
#define TRUE    1
#endif
#ifndef FALSE
#define FALSE   0
#endif
#ifndef NULL
#define NULL    0
#endif

#define MAX_PATH    1024

#define INVALID_HANDLE_VALUE    ((HANDLE)(long)-1)

#define FILE_ATTRIBUTE_READONLY     0x00000001
#define FILE_ATTRIBUTE_HIDDEN       0x00000002
#define FILE_ATTRIBUTE_DIRECTORY    0x00000010

#define HEAP_ZERO_MEMORY    0x00000008

#define RGB(r,g,b)      ((COLORREF)(((BYTE)(r)|((WORD)((BYTE)(g))<<8))|(((DWORD)(BYTE)(b))<<16)))
#define GetRValue(rgb)  ((BYTE)(rgb))
#define GetGValue(rgb)  ((BYTE)(((WORD)(rgb)) >> 8))
#define GetBValue(rgb)  ((BYTE)((rgb)>>16))

#define LOWORD(l)       ((WORD)((unsigned long)(l) & 0xffff))
#define HIWORD(l)       ((WORD)((unsigned long)(l) >> 16))
#define LOBYTE(w)       ((BYTE)((unsigned long)(w) & 0xff))
#define HIBYTE(w)       ((BYTE)((unsigned long)(w) >> 8))
#define MAKELONG(a, b)  ((LONG)(((WORD)((unsigned long)(a) & 0xffff)) | ((DWORD)((WORD)((unsigned long)(b) & 0xffff))) << 16))
#define MAKEWPARAM(l, h)    ((WPARAM)MAKELONG(l, h))
#define MAKELPARAM(l, h)    ((LPARAM)MAKELONG(l, h))

// virtual keys (GetKeyState)
#define VK_SHIFT    0x10
#define VK_CONTROL  0x11
#define VK_MENU     0x12
#define VK_PAUSE    0x13

// window messages
#define WM_CREATE       0x0001
#define WM_DESTROY      0x0002
#define WM_ACTIVATE     0x0006
#define WM_CLOSE        0x0010
#define WM_PAINT        0x000F
#define WM_CHAR         0x0102
#define WM_COMMAND      0x0111
#define WM_MOUSEMOVE    0x0200
#define WM_LBUTTONDOWN  0x0201
#define WM_LBUTTONUP    0x0202
#define WM_RBUTTONDOWN  0x0204
#define WM_MOUSEWHEEL   0x020A
#define WM_CAPTURECHANGED   0x0215
#define WM_USER         0x0400
#define WM_APP          0x8000

// MessageBox
#define MB_OK           0x00000000
#define MB_OKCANCEL     0x00000001
#define MB_YESNO        0x00000004
#define MB_ICONHAND     0x00000010
#define MB_ICONQUESTION 0x00000020
#define MB_ICONEXCLAMATION  0x00000030
#define MB_ICONINFORMATION  0x00000040
#define IDOK            1
#define IDCANCEL        2
#define IDYES           6
#define IDNO            7

/**
 * @brief Point in Win32 coordinates.
 */
typedef struct tagPOINT {
    LONG x;
    LONG y;
} POINT, *LPPOINT;

/**
 * @brief Rectangle in Win32 coordinates (right and bottom are exclusive).
 */
typedef struct tagRECT {
    LONG left;
    LONG top;
    LONG right;
    LONG bottom;
} RECT, *LPRECT;

/**
 * @brief Width and height.
 */
typedef struct tagSIZE {
    LONG cx;
    LONG cy;
} SIZE;

/**
 * @brief Bitmap description filled by GetObject().
 */
typedef struct tagBITMAP {
    LONG    bmType;         // +0x00
    LONG    bmWidth;        // +0x04
    LONG    bmHeight;       // +0x08
    LONG    bmWidthBytes;   // +0x0c
    WORD    bmPlanes;       // +0x10
    WORD    bmBitsPixel;    // +0x12
    LPVOID  bmBits;         // +0x18
} BITMAP;                   // sizeof == 0x20

/**
 * @brief Paint information used by BeginPaint() / EndPaint().
 */
typedef struct tagPAINTSTRUCT {
    HDC     hdc;        // the only field the Mac BeginPaint fills
    RECT    rcPaint;    // +0x08  synwin.cpp WM_PAINT passes the update rect in here
} PAINTSTRUCT;

/**
 * @brief 64-bit file time split into two halves (never filled on the Mac).
 */
typedef struct _FILETIME {
    DWORD dwLowDateTime;
    DWORD dwHighDateTime;
} FILETIME;

/**
 * @brief Directory entry returned by FindFirstFile() / FindNextFile().
 */
typedef struct _WIN32_FIND_DATA {
    DWORD       dwFileAttributes;       // +0x00
    FILETIME    ftCreationTime;         // +0x04
    FILETIME    ftLastAccessTime;       // +0x0c
    FILETIME    ftLastWriteTime;        // +0x14
    DWORD       nFileSizeHigh;          // +0x1c
    DWORD       nFileSizeLow;           // +0x20
    DWORD       dwReserved0;            // +0x24
    DWORD       dwReserved1;            // +0x28
    CHAR        cFileName[MAX_PATH];    // +0x2c
    CHAR        cAlternateFileName[14]; // +0x42c
    DWORD       dwFileType;             // +0x43c (_MAC fields of the Win32 SDK)
    DWORD       dwCreatorType;          // +0x440
} WIN32_FIND_DATA, *LPWIN32_FIND_DATA;  // sizeof == 0x444

#ifdef __cplusplus
class CCWindow;
extern "C" {
#else
typedef struct CCWindow CCWindow;
#endif

// winemu_au.cpp
/**
 * @brief Same as PostMessage(); used for messages to child controls.
 *
 * @param hWnd      Target view; NULL is ignored.
 * @param msg       Message number (WM_*).
 * @param wParam    First message parameter.
 * @param lParam    Second message parameter.
 * @return Always TRUE.
 * @see PostMessage()
 */
int     PostMessageControl(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);

/**
 * @brief Queues a window message for asynchronous delivery on the main thread.
 *
 * Wraps the message in an MSMessageParam and hands it to -recieveEvent: of the
 * view through PostEventMac(); returns without waiting.
 *
 * @param hWnd      Target view (NSView* that implements -recieveEvent:); NULL is ignored.
 * @param msg       Message number (WM_*).
 * @param wParam    First message parameter.
 * @param lParam    Second message parameter.
 * @return Always TRUE.
 */
int     PostMessage(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);

/**
 * @brief Same as SendMessage(); used for messages to child controls.
 *
 * @param hWnd      Target view; NULL is ignored.
 * @param msg       Message number (WM_*).
 * @param wParam    First message parameter.
 * @param lParam    Second message parameter.
 * @return Always TRUE.
 * @see SendMessage()
 */
int     SendMessageControl(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);

/**
 * @brief Delivers a window message on the main thread and waits for it to be handled.
 *
 * Wraps the message in an MSMessageParam and hands it to -recieveEvent: of the
 * view through SendEvent().
 *
 * @param hWnd      Target view (NSView* that implements -recieveEvent:); NULL is ignored.
 * @param msg       Message number (WM_*).
 * @param wParam    First message parameter.
 * @param lParam    Second message parameter.
 * @return Always TRUE (the result of the window procedure is not returned).
 * @note Calling it from the main thread runs the handler immediately.
 */
int     SendMessage(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);

/**
 * @brief Debug helper: appends samples as text to "/data_<c>.txt".
 *
 * @param buf   Samples to write, one "%f" line each.
 * @param n     Number of samples.
 * @param c     Character inserted into the file name.
 */
void    DebugOutput(float *buf, int n, char c);

/**
 * @brief Stores the view handle in a techpop window object.
 *
 * Writes @p hWnd into CCWindow::m_hWnd (offset 0x108) of @p p.
 *
 * @param[out] p    CCWindow* whose window handle is set.
 * @param hWnd      View (NSView*) that hosts the window.
 */
void    SetWindow(void *p, HWND hWnd);

/**
 * @brief Deletes a file with unlink(); errors are ignored.
 *
 * @param lpFileName    POSIX path of the file.
 */
void    DeleteFile(LPCSTR lpFileName);

/**
 * @brief Moves the mouse cursor on the main display.
 *
 * @param x,y   Position in global display coordinates (origin top left of the main display).
 */
void    SetCursorPos(int x, int y);

/**
 * @brief Reports whether a modifier key is held down.
 *
 * Reads the keyboard state with GetKeys(). The virtual keys are mapped to Mac
 * modifiers: VK_SHIFT = Shift, VK_CONTROL = Control, VK_MENU = Command and
 * VK_PAUSE = Option.
 *
 * @param nVirtKey  VK_SHIFT, VK_CONTROL, VK_MENU or VK_PAUSE; other keys report "up".
 * @return 0x8000 (negative) if the key is down, otherwise 0.
 */
SHORT   GetKeyState(int nVirtKey);

/**
 * @brief Starts painting a view: gets a drawing context for it.
 *
 * @param hWnd              View to paint.
 * @param[out] lpPaint      PAINTSTRUCT*; only its hdc field is set.
 * @return The context from GetDC(), or NULL if the view has no window.
 */
HDC     BeginPaint(HWND hWnd, void *lpPaint);   // (PAINTSTRUCT*) view factory only

/**
 * @brief Finishes painting started with BeginPaint(); releases the context.
 *
 * @param hWnd      View that was painted.
 * @param lpPaint   PAINTSTRUCT* filled by BeginPaint().
 * @return Always TRUE.
 */
int     EndPaint(HWND hWnd, void *lpPaint);     // (PAINTSTRUCT*) view factory only

// winobj_au.m, view factory only (SYNTH1_VIEWFACTORY).  CreateWindowNS,
// CreateSoundSelectWindowNS, CreateCustumWindow, Destroy*NS, ShowWindowMac,
// SizeView, GetWindowRect and ModalDialogMac are declared in techpop/ccwindow.h.
#define SYNWIN_WINAPI_DECLARED  // (replaces the stub declarations in techpop/synwin.h)

/**
 * @brief Same as GetDC(); used for child controls.
 *
 * @param hWnd  View (NSView*) to draw into.
 * @return The context, or NULL. Release it with ReleaseDCControl().
 * @see GetDC()
 */
HDC     GetDCControl(HWND hWnd);

/**
 * @brief Returns a drawing context for a view, set up with Win32 coordinates.
 *
 * Creates a graphics context for the view's window, saves its state and flips
 * it so that (0, 0) is the top left of the view and y grows downwards.
 *
 * @param hWnd  View (NSView*) to draw into.
 * @return The context, or NULL if @p hWnd is NULL or not in a window. Release
 *         it with ReleaseDC().
 */
HDC     GetDC(HWND hWnd);

/**
 * @brief Identical to GetDC().
 *
 * @param hWnd  View (NSView*) to draw into.
 * @return The context, or NULL. Release it with ReleaseDC0().
 * @see GetDC()
 */
HDC     GetDC0(HWND hWnd);

/**
 * @brief Same as ReleaseDC(); used for child controls.
 *
 * @param hWnd  View the context belongs to (unused).
 * @param hdc   Context to release; NULL is ignored.
 * @see ReleaseDC()
 */
void    ReleaseDCControl(HWND hWnd, HDC hdc);

/**
 * @brief Releases a context obtained with GetDC().
 *
 * Restores the graphics state saved by GetDC() and flushes the context.
 *
 * @param hWnd  View the context belongs to (unused).
 * @param hdc   Context to release; NULL is ignored.
 */
void    ReleaseDC(HWND hWnd, HDC hdc);

/**
 * @brief Identical to ReleaseDC(); pairs with GetDC0().
 *
 * @param hWnd  View the context belongs to (unused).
 * @param hdc   Context to release; NULL is ignored.
 */
void    ReleaseDC0(HWND hWnd, HDC hdc);

/**
 * @brief Prepares an existing context (e.g. the current one in -drawRect:) like GetDC().
 *
 * Saves the state of @p hdc and, if @p hWnd is given, flips it so that (0, 0)
 * is the top left of the view.
 *
 * @param hWnd  View whose bounds define the flip; may be NULL.
 * @param hdc   Context to prepare; NULL is ignored. Undo with ReleaseDC1().
 */
void    GetDC1(HWND hWnd, HDC hdc);

/**
 * @brief Restores and flushes a context prepared with GetDC1().
 *
 * @param hWnd  View the context belongs to (unused).
 * @param hdc   Context to release; NULL is ignored.
 */
void    ReleaseDC1(HWND hWnd, HDC hdc);

/**
 * @brief Makes the view the first responder of its window (keyboard focus).
 *
 * @param hWnd  View that gets the focus.
 */
void    SetFocus0(HWND hWnd);

/**
 * @brief Same as SetFocus0(); used for child controls.
 *
 * @param hWnd  View that gets the focus.
 */
void    SetFocusControl(HWND hWnd);

/**
 * @brief Resizes the content area of the view's window.
 *
 * @param hWnd  View whose window is resized.
 * @param w     New content width in points.
 * @param h     New content height in points.
 */
void    SetWindowSize(HWND hWnd, int w, int h);

/**
 * @brief Same as ScreenToClient(); used for child controls.
 *
 * @param hWnd          View that defines the client area.
 * @param[in,out] pt    Point in screen coordinates; replaced by client coordinates.
 * @see ScreenToClient()
 */
void    ScreenToClientControl(HWND hWnd, POINT *pt);

/**
 * @brief Converts a point from screen coordinates to client coordinates of a view.
 *
 * @param hWnd          View that defines the client area.
 * @param[in,out] pt    Point in screen coordinates (origin top left of the main
 *                      display); replaced by client coordinates (origin top left of the view).
 */
void    ScreenToClient(HWND hWnd, POINT *pt);

/**
 * @brief Same as ClientToScreen(); used for child controls.
 *
 * @param hWnd          View that defines the client area.
 * @param[in,out] pt    Point in client coordinates; replaced by screen coordinates.
 * @see ClientToScreen()
 */
void    ClientToScreenControl(HWND hWnd, POINT *pt);

/**
 * @brief Converts a point from client coordinates of a view to screen coordinates.
 *
 * @param hWnd          View that defines the client area.
 * @param[in,out] pt    Point in client coordinates (origin top left of the view);
 *                      replaced by screen coordinates (origin top left of the main display).
 */
void    ClientToScreen(HWND hWnd, POINT *pt);

/**
 * @brief Gets the screen rectangle of a view (see GetWindowRect() in techpop/ccwindow.h).
 *
 * @param hWnd      View.
 * @param[out] rc   Rectangle in screen coordinates (origin top left of the main display).
 * @return Always TRUE.
 */
int     GetWindowRectControl(HWND hWnd, RECT *rc);

/**
 * @brief Same as GetClientRect(); used for child controls.
 *
 * @param hWnd      View.
 * @param[out] rc   The view's bounds.
 * @see GetClientRect()
 */
void    GetClientRectControl(HWND hWnd, RECT *rc);

/**
 * @brief Gets the client rectangle of a view.
 *
 * @param hWnd      View.
 * @param[out] rc   The view's bounds (normally left = top = 0, right = width, bottom = height).
 */
void    GetClientRect(HWND hWnd, RECT *rc);

/**
 * @brief Same as InvalidateRect(); used for child controls.
 *
 * @param hWnd      View to invalidate.
 * @param rc        Rectangle in client coordinates, or NULL for the whole view.
 * @param bErase    Ignored.
 * @see InvalidateRect()
 */
void    InvalidateRectControl(HWND hWnd, RECT *rc, int bErase);

/**
 * @brief Marks part of a view for redrawing.
 *
 * @param hWnd      View to invalidate.
 * @param rc        Rectangle in client coordinates (origin top left), or NULL
 *                  to invalidate the whole view.
 * @param bErase    Ignored.
 */
void    InvalidateRect(HWND hWnd, RECT *rc, bool bErase);

/**
 * @brief Creates a borderless floating window with a view bound to a techpop window.
 *
 * The window (VolWindow) floats above normal windows, accepts mouse-moved
 * events, is shown and made key. Its view (VolView) forwards events to @p pWnd.
 *
 * @param x,y       Top left corner in screen coordinates (origin top left of the main display).
 * @param w,h       Size in points.
 * @param alpha     Window opacity (0..1).
 * @param pWnd      CCWindow* that receives the view's messages.
 * @return The view (HWND of the new window).
 */
HWND    CreateWindow(int x, int y, int w, int h, float alpha, void *pWnd);

/**
 * @brief Creates the borderless floating window of the sound (bank) selector.
 *
 * Like CreateWindow(), but with a SoundSelectWindow / SoundSelectView.
 *
 * @param x,y       Top left corner in screen coordinates.
 * @param w,h       Size in points.
 * @param alpha     Window opacity (0..1).
 * @param pWnd      CCWindow* that receives the view's messages.
 * @param hParent   Ignored.
 * @param style     Ignored.
 * @return The SoundSelectView.
 */
HWND    CreateSoundSelectWindow(int x, int y, int w, int h, float alpha, void *pWnd, HWND hParent, int style);

/**
 * @brief Creates the bank tree view inside a sound selector view.
 *
 * Builds an NSOutlineView with one read-only "Bank" column (300 points wide),
 * wraps it in a scroll view and adds that to @p hParent. The parent
 * (a SoundSelectView) becomes data source, delegate and target; clicks call
 * its -outlineViewClicked:. The scroll view is made first responder.
 *
 * @param hParent   SoundSelectView that hosts the tree.
 * @param x,y       Origin of the frame in the parent's (Cocoa) coordinates.
 * @param w,h       Size in points.
 * @param id        Ignored.
 * @return The NSScrollView, which serves as the tree view handle for the TreeView_* functions.
 */
HWND    CreateOutlineView(HWND hParent, int x, int y, int w, int h, int id);

/**
 * @brief Creates a push button in a view.
 *
 * Clicking the button calls -pushButton: of @p hParent; the button's tag is @p id.
 *
 * @param hParent   Parent view (also the button's target).
 * @param x,y       Origin of the frame in the parent's (Cocoa) coordinates.
 * @param w,h       Size in points.
 * @param text      Title (UTF-8).
 * @param id        Control ID stored as the button tag.
 * @return The NSButton (owned by the parent view).
 */
HWND    CreateButton(HWND hParent, int x, int y, int w, int h, const char *text, int id);

/**
 * @brief Creates a text field in a view.
 *
 * @param hParent   Parent view.
 * @param x,y       Origin of the frame in the parent's (Cocoa) coordinates.
 * @param w,h       Size in points.
 * @param id        Ignored.
 * @return The NSTextField (owned by the parent view).
 */
HWND    CreateEdit(HWND hParent, int x, int y, int w, int h, int id);

/**
 * @brief Removes a child control from its superview.
 *
 * Also clears the mouse capture if it pointed to the control. Nothing is
 * removed if the control is not in a window.
 *
 * @param hWnd  Control to remove; may be NULL.
 */
void    DestroyWindowControl(HWND hWnd);

/**
 * @brief Removes a view from its window and closes the window.
 *
 * Same as DestroyWindowWithParentCloseFlag(@p hWnd, TRUE).
 *
 * @param hWnd  View to remove.
 * @return FALSE if @p hWnd is NULL, otherwise TRUE.
 */
int     DestroyWindow(HWND hWnd);

/**
 * @brief Removes a view from its window and optionally closes the window.
 *
 * Clears the mouse capture if it pointed to @p hWnd.
 *
 * @param hWnd          View to remove.
 * @param bCloseParent  Nonzero to also close the view's window.
 * @return FALSE if @p hWnd is NULL, otherwise TRUE.
 */
int     DestroyWindowWithParentCloseFlag(HWND hWnd, int bCloseParent);

/**
 * @brief Stub for UpdateWindow(); does not repaint anything.
 *
 * @param hWnd  View.
 * @return Nonzero if @p hWnd is not NULL, otherwise 0.
 */
int     UpdateWindowMac(HWND hWnd);

/**
 * @brief Sets the opacity of a view and its window.
 *
 * Also gives the window a clear background, makes it non-opaque and turns on
 * its shadow.
 *
 * @param hWnd      View.
 * @param alpha     Opacity (0..1).
 */
void    SetAlpha(HWND hWnd, float alpha);

/**
 * @brief Same as SetCapture() without a return value.
 *
 * @param hWnd  View that captures the mouse; NULL just releases the capture.
 * @see SetCapture()
 */
void    SetCaptureControl(HWND hWnd);

/**
 * @brief Routes subsequent mouse messages to a view (mouse capture).
 *
 * Releases the current capture first (see ReleaseCapture()).
 *
 * @param hWnd  View that captures the mouse; NULL just releases the capture.
 * @return Always NULL (the previous capture is not returned).
 */
HWND    SetCapture(HWND hWnd);

/**
 * @brief Returns the view that currently has the mouse capture.
 *
 * @param[out] pbControl    If not NULL, set to FALSE.
 * @return The capturing view, or NULL.
 */
HWND    GetCapture(int *pbControl);

/**
 * @brief Same as ReleaseCapture().
 *
 * @see ReleaseCapture()
 */
void    ReleaseCaptureControl();    // declared with an empty () (unprototyped in Objective-C) on

/**
 * @brief Ends the mouse capture.
 *
 * Sends WM_CAPTURECHANGED to the capturing view (synchronously, on the main
 * thread) and clears the capture. Does nothing if there is no capture.
 */
void    ReleaseCapture();           // purpose: this fixes where ReleaseCapture ends up in the binary

/**
 * @brief Same as MoveWindowMac(); used for child controls.
 *
 * @param hWnd      View to move.
 * @param x,y       Top left corner relative to the top left of the superview.
 * @param w,h       New size in points.
 * @param bRepaint  Ignored.
 * @return Always FALSE.
 * @see MoveWindowMac()
 */
int     MoveWindowControl(HWND hWnd, int x, int y, int w, int h, int bRepaint);

/**
 * @brief Moves and resizes a child view.
 *
 * Sets the frame from Win32-style coordinates and resets the bounds to
 * (0, 0, @p w, @p h).
 *
 * @param hWnd      View to move.
 * @param x,y       Top left corner relative to the top left of the superview.
 * @param w,h       New size in points.
 * @param bRepaint  Ignored.
 * @return Always FALSE.
 */
int     MoveWindowMac(HWND hWnd, int x, int y, int w, int h, int bRepaint);

/**
 * @brief Sets the mouse cursor shape.
 *
 * @param type  0 arrow, 1 pointing hand, 2 resize left/right, 3 resize up/down;
 *              other values do nothing.
 */
void    SetCursorWin(int type);

/**
 * @brief Starts watching the window of a view so that a mouse capture can be cancelled.
 *
 * Posts message WM_APP + 2 to @p hWnd whenever its window starts to move, is
 * minimized, is closed or loses key status.
 *
 * @param hWnd  View to notify (must be in its window already).
 * @return Opaque observer object; free it with exitCapture().
 */
void    *initCapture(HWND hWnd);

/**
 * @brief Stops and releases an observer created by initCapture().
 *
 * @param p     Observer returned by initCapture().
 */
void    exitCapture(void *p);

/**
 * @brief Tells whether the application is running a modal window (e.g. a dialog from ModalDialogMac()).
 *
 * @return Nonzero if a modal window is running, otherwise 0.
 */
int     IsRunningModal(void);
// SynView.m (view factory only)
/**
 * @brief Adds an item to the bank tree.
 *
 * @param hTV           Tree view handle (the scroll view from CreateOutlineView()).
 * @param hParentItem   Parent item, or NULL to insert under the root item (or at
 *                      the top level if there is no root yet).
 * @param text          Item label (UTF-8).
 * @param lParam        User value returned by TreeView_GetItemParam().
 * @return The new item (owned by its parent or the sound selector view).
 */
void    *TreeView_InsertItem(HWND hTV, void *hParentItem, const char *text, void *lParam);

/**
 * @brief Gets the root item of the tree (held by the sound selector view).
 *
 * @param hTV   Tree view handle (the scroll view from CreateOutlineView()).
 * @return The root item, or NULL if there is none.
 */
void    *TreeView_GetRoot(HWND hTV);

/**
 * @brief Gets the user value of an item.
 *
 * @param hTV   Tree view handle (unused).
 * @param hItem Item.
 * @return The value given to TreeView_InsertItem() for @p hItem.
 */
void    *TreeView_GetItemParam(HWND hTV, void *hItem);

/**
 * @brief Selects an item and scrolls it into view.
 *
 * Expands the item's ancestors and collapses all other branches along the way.
 *
 * @param hTV       Tree view handle.
 * @param hItem     Item to select.
 */
void    TreeView_SelectItem(HWND hTV, void *hItem);

/**
 * @brief Declared for the tree view emulation but not implemented as a public function.
 *
 * SynView.m has only a file-local helper of this name.
 *
 * @param hTV   Tree view handle.
 * @param hItem Item.
 */
void    TreeView_CollapseHierarchy(HWND hTV, void *hItem);

/**
 * @brief Gets the first child of an item.
 *
 * @param hTV   Tree view handle (unused).
 * @param hItem Parent item.
 * @return The first child, or NULL if @p hItem has none.
 */
void    *TreeView_GetChild(HWND hTV, void *hItem);

/**
 * @brief Gets the next sibling of an item.
 *
 * @param hTV   Tree view handle (unused).
 * @param hItem Item.
 * @return The next sibling, or NULL if @p hItem is the last child.
 */
void    *TreeView_GetNextSibling(HWND hTV, void *hItem);

/**
 * @brief Gets the rectangle of an item's cell.
 *
 * @param hTV           Tree view handle.
 * @param hItem         Item.
 * @param[out] rc       Cell rectangle in the scroll view's coordinates.
 * @param bTextOnly     Ignored.
 * @return false if the item is not a visible row (e.g. inside a collapsed branch), else true.
 */
bool    TreeView_GetItemRect(HWND hTV, void *hItem, RECT *rc, int bTextOnly);

/**
 * @brief Reloads all rows of the tree view.
 *
 * @param hTV   Tree view handle.
 */
void    TreeView_Update(HWND hTV);

/**
 * @brief Expands or collapses an item.
 *
 * @param hTV   Tree view handle.
 * @param hItem Item.
 * @param flag  Nonzero to expand, 0 to collapse.
 */
void    TreeView_Expand(HWND hTV, void *hItem, int flag);

/**
 * @brief Does nothing (items are released with their parent).
 *
 * @param hItem Item (unused).
 */
void    TreeItem_Destroy(void *hItem);

/**
 * @brief Copies the text of a text field.
 *
 * @param hEdit         Text field from CreateEdit().
 * @param[out] text     Buffer for the UTF-8 text (strncpy(), so it is not
 *                      terminated if the text fills @p size bytes).
 * @param size          Size of @p text in bytes.
 * @return strlen() of @p text.
 */
int     Edit_GetText(HWND hEdit, char *text, int size);

/**
 * @brief Sets the text of a text field.
 *
 * The text is decoded as UTF-8; if that fails, as Shift-JIS (old patch files)
 * and then as ASCII. If all fail the field is left unchanged.
 *
 * @param hEdit     Text field from CreateEdit().
 * @param text      New text.
 */
void    Edit_SetText(HWND hEdit, const char *text);

/**
 * @brief Makes a text field read-only or editable.
 *
 * @param hEdit Text field from CreateEdit().
 * @param b     Nonzero for read-only, 0 for editable.
 */
void    Edit_SetReadOnly(HWND hEdit, int b);

/**
 * @brief Shows or hides a view (there is no disabled state).
 *
 * @param hWnd  View.
 * @param b     Nonzero to show the view, 0 to hide it.
 */
void    EnableWindow(HWND hWnd, int b);

/**
 * @brief Does nothing on the Mac (see techpop/ccwindow.h).
 *
 * @param hWnd  View (unused).
 */
void    DestroyCustumWindow(HWND hWnd);

/**
 * @brief Returns a string as UTF-8, converting old Shift-JIS or ASCII patch names.
 *
 * @param str   String in UTF-8, Shift-JIS or ASCII.
 * @return @p str itself if it is empty; otherwise a UTF-8 copy, or "not utf8"
 *         if no encoding fits. The copy is autoreleased: it is valid only until
 *         the current autorelease pool is drained.
 */
const char *GetEncodedString(const char *str);

// winobj_au.m
/**
 * @brief Performs -recieveEvent: of a view asynchronously on the main thread.
 *
 * @param hWnd      Target object (implements -recieveEvent:); NULL is ignored.
 * @param msg       Message number.
 * @param wParam    First message parameter.
 * @param lParam    Second message parameter.
 */
void    PostEventMac(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);

/**
 * @brief Like PostEventMac(), but waits until the main thread has handled the message.
 *
 * @param hWnd      Target object (implements -recieveEvent:); NULL is ignored.
 * @param msg       Message number.
 * @param wParam    First message parameter.
 * @param lParam    Second message parameter.
 */
void    SendEvent(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);

/**
 * @brief Shows a modal alert and waits for the user.
 *
 * With MB_YESNO the alert has OK and Cancel buttons; otherwise only OK. The
 * icon flags are ignored (always a note alert).
 *
 * @param hWnd          Ignored.
 * @param lpText        Message text (UTF-8).
 * @param lpCaption     Alert title (UTF-8).
 * @param uType         MB_* flags; only MB_YESNO is checked.
 * @return 0 if OK was pressed, nonzero otherwise. This differs from Win32,
 *         which returns IDOK / IDYES etc.
 */
int     MessageBox(HWND hWnd, LPCSTR lpText, LPCSTR lpCaption, UINT uType);

/**
 * @brief Moves the view's window.
 *
 * @param hWnd  View whose window is moved.
 * @param x,y   New bottom-left corner of the window in Cocoa screen coordinates
 *              (origin bottom left of the main display).
 */
void    setWindowPos(HWND hWnd, int x, int y);

/**
 * @brief Gets the usable area (without menu bar and Dock) of the screen that shows a view.
 *
 * @param hWnd      View whose window selects the screen.
 * @param[out] rc   Visible frame in screen coordinates (origin top left of the main display).
 */
void    getVisibleScreenRect(HWND hWnd, RECT *rc);

/**
 * @brief Tells whether the plug-in runs inside the GarageBand sandbox.
 *
 * Checks if the user's Documents directory lies in the GarageBand container
 * (path contains "com.apple.garageband"). The result is cached.
 *
 * @return Nonzero when sandboxed.
 */
int     isSandBox(void);

#ifdef __cplusplus
}

// winemu_au.cpp (C++ linkage)
/**
 * @brief Allocates memory aligned to a given boundary.
 *
 * Over-allocates with malloc() and stores the original pointer just before the
 * returned block.
 *
 * @param size          Number of bytes.
 * @param alignment     Alignment in bytes (e.g. 16).
 * @return Aligned pointer, or NULL if malloc() fails. Free it with _aligned_free().
 */
void    *_aligned_malloc(size_t size, size_t alignment);

/**
 * @brief Frees memory from _aligned_malloc().
 *
 * @param p     Block to free; must not be NULL.
 */
void    _aligned_free(void *p);

/**
 * @brief Gets the time since system start-up (from mach_absolute_time()).
 *
 * @return Milliseconds since system start-up.
 */
unsigned long   GetTickCount();

/**
 * @brief Reads a string setting from the preferences (Win32 INI file emulation).
 *
 * The "file" is a CFPreferences application ID; each section is stored as a
 * dictionary of strings under the key @p lpAppName. The default is copied
 * first and overwritten if the key exists.
 *
 * @param lpAppName             Section name.
 * @param lpKeyName             Key name, or NULL to get all key names of the
 *                              section as "key1\0key2\0\0".
 * @param lpDefault             Default value.
 * @param[out] lpReturnedString Buffer for the result (UTF-8).
 * @param nSize                 Size of the buffer in bytes.
 * @param lpFileName            Preferences application ID, or NULL for the
 *                              current application (the host).
 * @return strlen() of the buffer (for the key list, the length of the first name only).
 */
DWORD   GetPrivateProfileString(LPCSTR lpAppName, LPCSTR lpKeyName, LPCSTR lpDefault, LPSTR lpReturnedString, DWORD nSize, LPCSTR lpFileName);

/**
 * @brief Writes a string setting to the preferences and synchronizes them.
 *
 * @param lpAppName     Section name.
 * @param lpKeyName     Key name. If both @p lpKeyName and @p lpString are
 *                      NULL the whole section is deleted.
 * @param lpString      Value (UTF-8). If NULL (with a key), nothing is written;
 *                      the key is not deleted.
 * @param lpFileName    Preferences application ID, or NULL for the current application.
 * @return Always TRUE.
 */
BOOL    WritePrivateProfileString(LPCSTR lpAppName, LPCSTR lpKeyName, LPCSTR lpString, LPCSTR lpFileName);

/**
 * @brief Reads an integer setting from the preferences.
 *
 * The stored string (up to 255 characters) is converted with atoi().
 *
 * @param lpAppName     Section name.
 * @param lpKeyName     Key name.
 * @param nDefault      Value returned if the setting does not exist.
 * @param lpFileName    Preferences application ID, or NULL for the current application.
 * @return The setting, or @p nDefault.
 */
UINT    GetPrivateProfileInt(LPCSTR lpAppName, LPCSTR lpKeyName, int nDefault, LPCSTR lpFileName);

/**
 * @brief Creates a heap, emulated by a CFAllocator.
 *
 * The allocator is created from an empty context; all arguments are ignored.
 *
 * @param flOptions         Ignored.
 * @param dwInitialSize     Ignored.
 * @param dwMaximumSize     Ignored.
 * @return The CFAllocatorRef as a HANDLE. Destroy it with HeapDestroy().
 */
HANDLE  HeapCreate(DWORD flOptions, int dwInitialSize, int dwMaximumSize);

/**
 * @brief Releases a heap from HeapCreate().
 *
 * @param hHeap Heap (CFAllocatorRef); NULL is ignored.
 * @return Always TRUE.
 */
BOOL    HeapDestroy(HANDLE hHeap);

/**
 * @brief Allocates memory from a heap created with HeapCreate().
 *
 * @param hHeap     Heap (CFAllocatorRef).
 * @param dwFlags   HEAP_ZERO_MEMORY to clear the block.
 * @param dwBytes   Number of bytes.
 * @return The block, or NULL.
 */
LPVOID  HeapAlloc(HANDLE hHeap, DWORD dwFlags, int dwBytes);

/**
 * @brief Frees a block from HeapAlloc().
 *
 * @param hHeap     Heap (CFAllocatorRef) the block came from.
 * @param dwFlags   Ignored.
 * @param lpMem     Block to free.
 * @return Always TRUE.
 */
BOOL    HeapFree(HANDLE hHeap, DWORD dwFlags, LPVOID lpMem);

/**
 * @brief Gets the user's temporary folder (created if needed).
 *
 * @param nBufferLength     Size of @p lpBuffer in bytes.
 * @param[out] lpBuffer     POSIX path (UTF-8), without a trailing slash; empty on failure.
 * @return Always 0 (unlike Win32, the length is not returned).
 */
DWORD   GetTempPath(int nBufferLength, LPSTR lpBuffer);

/**
 * @brief Steps back one character (single-byte characters only).
 *
 * @param lpszStart     Start of the string.
 * @param lpszCurrent   Current position.
 * @return The byte before @p lpszCurrent, but not before @p lpszStart.
 */
LPSTR   CharPrev(LPCSTR lpszStart, LPCSTR lpszCurrent);

/**
 * @brief Steps forward one character (single-byte characters only).
 *
 * @param lpsz  Current position.
 * @return @p lpsz + 1.
 */
LPSTR   CharNext(LPCSTR lpsz);

/**
 * @brief Meant to convert a string to lower case.
 *
 * @param lpsz  String.
 * @return @p lpsz.
 * @note The result of tolower() is discarded, so the string is not changed.
 */
LPSTR   CharLower(LPSTR lpsz);

/**
 * @brief Blocks the calling thread for a while.
 *
 * Waits on a Multiprocessing Services event that is never signaled.
 *
 * @param dwMilliseconds    Time to wait in milliseconds.
 */
void    Sleep(int dwMilliseconds);

/**
 * @brief Copies a rectangle from one context to another.
 *
 * Takes an image snapshot of the source (which must be a bitmap context) and
 * draws the requested part of it into the destination.
 *
 * @param hdcDest           Destination context.
 * @param nXDest,nYDest     Top left of the destination rectangle.
 * @param nWidth,nHeight    Size of the rectangle.
 * @param hdcSrc            Source bitmap context.
 * @param nXSrc,nYSrc       Top left of the source rectangle.
 * @param dwRop             Ignored (always a plain copy).
 * @return Always FALSE, even on success.
 */
BOOL    BitBlt(HDC hdcDest, int nXDest, int nYDest, int nWidth, int nHeight, HDC hdcSrc, int nXSrc, int nYSrc, DWORD dwRop);

/**
 * @brief Not implemented: does nothing.
 *
 * @param hdcDest                       Destination context (unused).
 * @param nXOriginDest,nYOriginDest     Destination origin (unused).
 * @param nWidthDest,nHeightDest       Destination size (unused).
 * @param hdcSrc                        Source context (unused).
 * @param nXOriginSrc,nYOriginSrc       Source origin (unused).
 * @param nWidthSrc,nHeightSrc         Source size (unused).
 * @param dwRop                         Raster operation (unused).
 * @return Always FALSE.
 */
BOOL    StretchBlt(HDC hdcDest, int nXOriginDest, int nYOriginDest, int nWidthDest, int nHeightDest, HDC hdcSrc, int nXOriginSrc, int nYOriginSrc, int nWidthSrc, int nHeightSrc, DWORD dwRop);

/**
 * @brief Not implemented.
 *
 * @param hdc   Context (unused).
 * @return Always NULL.
 */
HDC     CreateCompatibleDC(HDC hdc);

/**
 * @brief Not implemented: does nothing.
 *
 * @param hdc   Context (unused).
 */
void    DeleteDC(HDC hdc);

/**
 * @brief Tests whether a point lies in a rectangle.
 *
 * @param lprc  Rectangle.
 * @param pt    Point.
 * @return TRUE if @p pt is inside @p lprc. Unlike Win32 PtInRect, the right and
 *         bottom edges count as inside.
 */
BOOL    PtInRect0(RECT *lprc, POINT pt);

/**
 * @brief Adds a rectangle to the current path of the context.
 *
 * Nothing is drawn; the caller strokes or fills the path.
 *
 * @param hdc       Context.
 * @param left,top  Top left corner.
 * @param right     Passed to CGRectMake() as the width.
 * @param bottom    Passed to CGRectMake() as the height.
 */
void    Rectangle(HDC hdc, int left, int top, int right, int bottom);

/**
 * @brief Not implemented: does nothing.
 *
 * @param hdc       Context (unused).
 * @param x,y       Pixel position (unused).
 * @param crColor   Color (unused).
 */
void    SetPixel(HDC hdc, int x, int y, COLORREF crColor);

/**
 * @brief Not implemented.
 *
 * @param hdc   Context (unused).
 * @param x,y   Pixel position (unused).
 * @return Always 0 (black).
 */
COLORREF    GetPixel0(HDC hdc, int x, int y);

/**
 * @brief Starts a new subpath of the context's path.
 *
 * @param hdc   Context.
 * @param x,y   Start point.
 */
void    MoveTo0(HDC hdc, int x, int y);

/**
 * @brief Adds a line from the current point to the context's path (not stroked).
 *
 * @param hdc   Context.
 * @param x,y   End point.
 */
void    LineTo0(HDC hdc, int x, int y);

/**
 * @brief Draws text with the context's current font and fill color.
 *
 * @param hdc           Context.
 * @param x,y           Text position (baseline origin).
 * @param lpString      Text (bytes in the context's font encoding).
 * @param c             Number of bytes to draw.
 */
void    TextOut(HDC hdc, int x, int y, LPCSTR lpString, int c);

/**
 * @brief Sets the fill color of the context (used by TextOut()); alpha is 1.
 *
 * @param hdc       Context.
 * @param crColor   Color as a COLORREF (RGB()).
 */
void    SetTextColor(HDC hdc, COLORREF crColor);

/**
 * @brief Not implemented: does nothing.
 *
 * @param hdc       Context (unused).
 * @param lpchText  Text (unused).
 * @param cchText   Text length (unused).
 * @param lprc      Rectangle (unused).
 * @param format    Format flags (unused).
 */
void    DrawText0(HDC hdc, LPCSTR lpchText, int cchText, RECT *lprc, int format);

/**
 * @brief Gets the attributes of a file or folder.
 *
 * @param lpFileName    POSIX path (UTF-8).
 * @return FILE_ATTRIBUTE_DIRECTORY and/or FILE_ATTRIBUTE_HIDDEN, 0 for a plain
 *         file, or 0xFFFFFFFF if the item does not exist.
 */
DWORD   GetFileAttributes(LPCSTR lpFileName);

/**
 * @brief Starts enumerating a folder and returns the first entry.
 *
 * The last path component (e.g. "*.*") is dropped; every entry of the
 * remaining folder is enumerated without filtering. Only the directory
 * attribute, the size and the name are filled; the times are left 0.
 *
 * @param lpFileName            Search pattern (POSIX path).
 * @param[out] lpFindFileData   First entry; cleared first.
 * @return Search handle for FindNextFile() / FindClose(), or
 *         INVALID_HANDLE_VALUE if the folder cannot be read or is empty.
 */
HANDLE  FindFirstFile(LPCSTR lpFileName, WIN32_FIND_DATA *lpFindFileData);

/**
 * @brief Gets the next entry of a search started with FindFirstFile().
 *
 * @param hFindFile             Search handle (must not be INVALID_HANDLE_VALUE).
 * @param[out] lpFindFileData   Next entry; cleared first.
 * @return TRUE if an entry was returned, FALSE at the end.
 */
BOOL    FindNextFile(HANDLE hFindFile, WIN32_FIND_DATA *lpFindFileData);

/**
 * @brief Ends a search started with FindFirstFile().
 *
 * @param hFindFile Search handle; NULL is ignored.
 * @return FALSE if @p hFindFile is NULL, otherwise TRUE.
 */
BOOL    FindClose(HANDLE hFindFile);

/**
 * @brief Creates a folder.
 *
 * @param path  POSIX path of the new folder (UTF-8); its parent must exist.
 * @return noErr on success, the File Manager error, or -1 for an invalid path
 *         (including "/").
 */
OSErr   mkdir(LPCSTR path);

/**
 * @brief Debug helper: appends a string to "/data.txt".
 *
 * @param str   Text to append.
 * @param c     Ignored.
 */
void    DebugOutput1(LPCSTR str, char c);

/**
 * @brief Debug helper: same as DebugOutput1(@p str, 0).
 *
 * @param str   Text to append.
 */
void    DebugOutput2(LPCSTR str);

/**
 * @brief Creates a 32-bit RGBA image of the given size.
 *
 * @param width     Width in pixels.
 * @param height    Height in pixels.
 * @return The image (release with CGImageRelease()).
 * @note The pixel buffer is freed before returning while the image still
 *       refers to it, so only the size and format of the result are reliable.
 */
HBITMAP CreateDIB(int width, int height);

/**
 * @brief Fills a BITMAP structure with the size and format of an image.
 *
 * bmType is not touched and bmBits is set to NULL; bmPlanes receives the
 * number of components per pixel.
 *
 * @param hbm               Image.
 * @param cbBuffer          Must be sizeof(BITMAP).
 * @param[out] lpvObject    BITMAP to fill.
 * @return sizeof(BITMAP), or 0 if @p cbBuffer is wrong.
 */
int     GetObject(HBITMAP hbm, int cbBuffer, LPVOID lpvObject);

/**
 * @brief Loads a bitmap resource of the GUI.
 *
 * The resource ID is mapped to a PNG file in the view bundle
 * (jp.daichi.synth1.audiounit.ui).
 *
 * @param hInstance     Ignored.
 * @param id            Win32 resource ID (0x82..0xab).
 * @return The image (release with CGImageRelease()), or NULL if the file was
 *         not found (a message is printed).
 */
HBITMAP LoadBitmap(HINSTANCE hInstance, int id);

#endif // __cplusplus

#endif // __WINDEF_H__
