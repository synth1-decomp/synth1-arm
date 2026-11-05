#pragma once

/**
 * @file ccwindow.h
 * Window / dialog base classes (Win32 style wrappers; on the Mac the windows are
 * created by winobj_au.m).
 *
 * Also declares the C entry points of the Cocoa side that these classes call.
 */

#include "../windef.h"

#ifndef CCWINDOW_DECLARED
#define CCWINDOW_DECLARED

#define CCWINDOW_MAX    256

// m_winType
#define CCWIN_NONE      0
#define CCWIN_NSWINDOW      1
#define CCWIN_SOUNDSELECT   2
#define CCWIN_CUSTOM        3

// window styles (no meaning on the Mac)
#define WS_POPUP        0
#define WS_CHILD        0

// resource ids used by CCDialog::open()
#define IDD_SAVESOUND       165
#define IDD_OPTION      166

extern "C" {
/**
 * @brief Creates a borderless floating window with a view that forwards events to @p pWnd.
 *
 * @param x         Left edge in screen pixels.
 * @param y         Top edge in screen pixels (top-down; flipped to Cocoa coordinates).
 * @param w         Width in pixels.
 * @param h         Height in pixels.
 * @param scale     Passed through as the window's alpha value (CCWindow uses 1.0).
 * @param pWnd      Window object that receives the messages.
 * @return The created view, used as the window handle.
 */
HWND    CreateWindowNS(int x, int y, int w, int h, float scale, CCWindow* pWnd);

/**
 * @brief Creates the borderless floating sound (patch) select window.
 *
 * @param x         Left edge in screen pixels.
 * @param y         Top edge in screen pixels (top-down; flipped to Cocoa coordinates).
 * @param w         Width in pixels.
 * @param h         Height in pixels.
 * @param scale     Passed through as the window's alpha value.
 * @param pWnd      Window object (the CSoundSelectWin) that receives the messages.
 * @return The created view, used as the window handle.
 * @see CreateWindowNS()
 */
HWND    CreateSoundSelectWindowNS(int x, int y, int w, int h, float scale, CCWindow* pWnd);

/**
 * @brief Creates the main synth view (SynView) and adds it to the content view of @p hParent.
 *
 * @param y         First origin coordinate; used as the view's x origin (see the note).
 * @param x         Second origin coordinate; used as the view's y origin (see the note).
 * @param w         Width in pixels.
 * @param h         Height in pixels.
 * @param hParent   Parent NSWindow.
 * @param pWnd      Window object that receives the messages.
 * @return The created view, used as the window handle.
 * @note The implementation takes (x, y) in that order, so the first argument becomes the
 *       view's x origin.
 */
HWND    CreateCustumWindow(int y, int x, int w, int h, HWND hParent, CCWindow* pWnd);

/**
 * @brief Destroys a window created by CreateWindowNS() and closes its parent window.
 *
 * @param hWnd      View returned by CreateWindowNS().
 */
void    DestroyWindowNS(HWND hWnd);

/**
 * @brief Closes and releases a window created by CreateSoundSelectWindowNS().
 *
 * @param hWnd      View returned by CreateSoundSelectWindowNS(); released afterwards.
 */
void    DestroySoundSelectWindowNS(HWND hWnd);

/**
 * @brief Called for windows created by CreateCustumWindow(); does nothing on the Mac.
 *
 * @param hWnd      View returned by CreateCustumWindow() (unused).
 */
void    DestroyCustumWindow(HWND hWnd);

/**
 * @brief Brings the window that contains @p hWnd to the front.
 *
 * @param hWnd      View inside the window; NULL does nothing.
 * @param bShow     Nonzero to order the window front; zero does nothing (hiding is not
 *                  implemented).
 */
void    ShowWindowMac(HWND hWnd, int bShow);

/**
 * @brief Sets the frame size of a view.
 *
 * @param hWnd      View to resize.
 * @param w         New width in pixels.
 * @param h         New height in pixels.
 */
void    SizeView(HWND hWnd, int w, int h);

/**
 * @brief Gets the screen rectangle of the window that contains a view.
 *
 * @param hWnd      View inside the window.
 * @param[out] rc   Receives the window rectangle in screen pixels.
 */
void    GetWindowRect(HWND hWnd, RECT* rc);

/**
 * @brief Loads the nib named @p name and runs it as an application-modal dialog.
 *
 * @param hInst     Unused.
 * @param name      Nib name, e.g. "IDD_OPTION" or "IDD_SAVESOUND".
 * @param hParent   Passed to the dialog window as its parameter.
 * @return The modal response code of the dialog.
 */
int ModalDialogMac(HINSTANCE hInst, const char* name, HWND hParent);
}

/**
 * @brief Base class of all plugin windows: a Win32 style window object backed by a Cocoa view.
 *
 * Subclasses set m_winType and override myWndProc() to handle messages; create() fills in the
 * default geometry and creates the native window through the CreateXxxNS helpers.
 *
 * @note The native window is destroyed by Destroy() or the destructor. The destructor is not
 *       virtual.
 */
class CCWindow {
public:
    /**
     * @brief Creates an object with no native window (m_winType = CCWIN_NONE, m_scale = 1.0).
     */
    CCWindow();

    /**
     * @brief Destroys the native window, if any (calls Destroy()).
     */
    ~CCWindow();

    /**
     * @brief Destroys the native window with the helper that matches m_winType.
     *
     * Clears m_hWnd first; does nothing if there is no window.
     */
    void Destroy();

    /**
     * @brief Creates the native window from m_x, m_y, m_w, m_h according to m_winType.
     *
     * Stores the handle in m_hWnd. A sound select window is shown right away; CCWIN_NONE
     * creates nothing.
     */
    void createWinEx();

    /**
     * @brief One-time initialisation of the shared window and dialog tables.
     *
     * Only the first call has an effect: it stores the instance handle and clears
     * pccWindow and CCDialog::pccDialog.
     *
     * @param hInst     Instance handle to keep in CCWindow::hInst.
     * @return Always TRUE.
     */
    static BOOL init(HINSTANCE hInst);

    /**
     * @brief Default message handler; does nothing.
     *
     * @param hWnd      Window handle (unused).
     * @param msg       Message id (unused).
     * @param wParam    Message parameter (unused).
     * @param lParam    Message parameter (unused).
     * @return Always 0.
     */
    virtual LRESULT defaultProc(HWND hWnd, int msg, WPARAM wParam, LPARAM lParam){ return 0; }

    /**
     * @brief Window procedure called by the Cocoa view for each message.
     *
     * The base version forwards to defaultProc().
     *
     * @param hWnd      Window handle.
     * @param msg       Message id (WM_xxx).
     * @param wParam    Message parameter.
     * @param lParam    Message parameter.
     * @return Message result.
     */
    virtual LRESULT myWndProc(HWND hWnd, int msg, WPARAM wParam, LPARAM lParam){ return defaultProc(hWnd, msg, wParam, lParam); }

    /**
     * @brief Sets the default style (0), class name ("CCWindow"), position (0, 0) and size
     *        (100 x 100), and clears the parent, menu and parameter.
     */
    virtual void myWindowDefaultInfo();

    /**
     * @brief Creates a 100 x 100 window at (@p x, @p y).
     *
     * @param hParent   Parent window (used by CCWIN_CUSTOM windows).
     * @param x         Left position in pixels.
     * @param y         Top position in pixels.
     * @param lpParam   User parameter stored in m_lpParam.
     * @see create(HWND, int, int, int, int, LPVOID)
     */
    virtual void create(HWND hParent, int x, int y, LPVOID lpParam);

    /**
     * @brief Creates the native window.
     *
     * Calls myWindowClass() and myWindowDefaultInfo(), then overrides the geometry, parent
     * and parameter with the arguments and calls createWinEx().
     *
     * @param hParent   Parent window (used by CCWIN_CUSTOM windows).
     * @param x         Left position in pixels.
     * @param y         Top position in pixels.
     * @param w         Width in pixels.
     * @param h         Height in pixels.
     * @param lpParam   User parameter stored in m_lpParam.
     */
    virtual void create(HWND hParent, int x, int y, int w, int h, LPVOID lpParam);

    /**
     * @brief Resizes the view; only CCWIN_CUSTOM windows are resized.
     *
     * @param w     New width in pixels.
     * @param h     New height in pixels.
     */
    virtual void resize(int w, int h);

    /**
     * @brief Moves the window so that it lies entirely inside the visible screen area.
     *
     * @param pt    Unused; the current window position is used instead.
     */
    virtual void moveIntoMonitor(POINT* pt);

    /**
     * @brief Window class registration hook; does nothing.
     */
    virtual void myWindowClass();

    static HINSTANCE hInst;
    static int nUniqueNo;
    static CCWindow** pccWindow;
    static int bFirst;

    char _pad_0008[0x100];      // +0x008
    HWND m_hWnd;                // +0x108
    DWORD m_dwStyle;            // +0x110
    char m_szClassName[0x100];  // +0x114
    int m_winType;              // +0x214  CCWIN_xxx
    int m_x;                    // +0x218
    int m_y;                    // +0x21c
    int m_w;                    // +0x220
    int m_h;                    // +0x224
    HWND m_hParent;             // +0x228
    HMENU m_hMenu;              // +0x230
    LPVOID m_lpParam;           // +0x238
    float m_scale;              // +0x240
    void* m_pUser;              // +0x248
};

/**
 * @brief Popup window: CCWindow with the WS_POPUP style.
 */
class CPopupWin : public CCWindow {
public:
    /**
     * @brief Sets the CCWindow defaults with the WS_POPUP style.
     */
    virtual void myWindowDefaultInfo();
};

/**
 * @brief Child window: CCWindow with the WS_CHILD style.
 */
class CChildWin : public CCWindow {
public:
    /**
     * @brief Sets the CCWindow defaults with the WS_CHILD style.
     */
    virtual void myWindowDefaultInfo();
};

/**
 * @brief MDI child window; base of CMDIChild2.
 */
class CMDIChild : public CCWindow {
public:
    /**
     * @brief Sets the CCWindow defaults.
     */
    // myWindowDefaultInfo (ccwindow.cpp) is the key function, so the vtable and
    // typeinfo are emitted with ccwindow.cpp.  defaultProc is defined inline in
    // synwin.h only, so its out-of-line copy is emitted with synwin.cpp.
    virtual void myWindowDefaultInfo();

    /**
     * @brief Default message handler of MDI children; does nothing.
     *
     * @param hWnd      Window handle (unused).
     * @param msg       Message id (unused).
     * @param wParam    Message parameter (unused).
     * @param lParam    Message parameter (unused).
     * @return Always 0.
     */
    virtual LRESULT defaultProc(HWND hWnd, int msg, WPARAM wParam, LPARAM lParam);
};

/**
 * @brief MDI child window; base class of CSynthWin and CStepVoxWin.
 */
class CMDIChild2 : public CMDIChild {
public:
    /**
     * @brief Sets the CCWindow defaults.
     */
    virtual void myWindowDefaultInfo();
};

/**
 * @brief Base class of the modal dialogs; on the Mac the dialogs are nibs run by ModalDialogMac().
 */
class CCDialog {
public:
    /**
     * @brief Creates a dialog object with no dialog handle.
     */
    CCDialog();

    /**
     * @brief Removes every entry of pccDialog that refers to this dialog's handle.
     */
    ~CCDialog();

    /**
     * @brief Dialog procedure; the base version does nothing.
     *
     * @param hDlg      Dialog handle (unused).
     * @param msg       Message id (unused).
     * @param wParam    Message parameter (unused).
     * @param lParam    Message parameter (unused).
     * @return Always 0.
     */
    virtual BOOL myDlgProc(HWND hDlg, UINT msg, WPARAM wParam, LPARAM lParam){ return 0; }

    /**
     * @brief Runs the dialog modally.
     *
     * @param hInst         Instance handle (passed on, unused).
     * @param lpTemplate    Resource id cast to LPCSTR: IDD_OPTION or IDD_SAVESOUND.
     * @param hParent       Parameter handed to the dialog window.
     * @return The dialog's modal response, or 0 for any other template id.
     */
    virtual int open(HINSTANCE hInst, LPCSTR lpTemplate, HWND hParent);

    /**
     * @brief Records the instance and parent for a modeless dialog; no dialog is created.
     *
     * @param hInst         Stored in m_hInst.
     * @param lpTemplate    Unused.
     * @param hParent       Stored in m_hParent.
     * @return Always 0.
     */
    virtual int createDialog(HINSTANCE hInst, LPCSTR lpTemplate, HWND hParent);

    static CCDialog** pccDialog;

    HWND m_hDlg;            // +0x08
    HINSTANCE m_hInst;      // +0x10
    HWND m_hParent;         // +0x18
};

#endif
