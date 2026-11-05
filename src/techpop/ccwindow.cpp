/**
 * @file ccwindow.cpp
 * CCWindow / CCDialog: Win32 style window wrappers for the Mac GUI
 *
 * Defines the static window/dialog tables and forwards window creation and destruction to
 * the Cocoa helpers in winobj_au.m and SynView.m according to CCWindow::m_winType.
 */

#include <string.h>
#include "common.h"
#include "ccwindow.h"

HINSTANCE CCWindow::hInst;
int CCWindow::nUniqueNo;
CCWindow** CCWindow::pccWindow = new CCWindow*[CCWINDOW_MAX];
int CCWindow::bFirst = TRUE;
CCDialog** CCDialog::pccDialog = new CCDialog*[CCWINDOW_MAX];
BOOL g_bMouseWheelMoved;

CCWindow::CCWindow()
{
    m_hWnd = NULL;
    m_winType = CCWIN_NONE;
    m_scale = 1.0f;
    m_pUser = NULL;
}

CCWindow::~CCWindow()
{
    Destroy();
}

void CCWindow::Destroy()
{
    if(m_hWnd){
        HWND hWnd = m_hWnd;
        m_hWnd = NULL;
        switch(m_winType){
        case CCWIN_NSWINDOW:
            DestroyWindowNS(hWnd);
            break;
        case CCWIN_SOUNDSELECT:
            DestroySoundSelectWindowNS(hWnd);
            break;
        case CCWIN_CUSTOM:
            DestroyCustumWindow(hWnd);
            break;
        }
    }
}

void CCWindow::myWindowDefaultInfo()
{
    m_dwStyle = 0;
    strcpy(m_szClassName, "CCWindow");
    m_x = 0;
    m_y = 0;
    m_w = 100;
    m_h = 100;
    m_hParent = m_hMenu = m_lpParam = NULL;
}

void CCWindow::create(HWND hParent, int x, int y, LPVOID lpParam)
{
    create(hParent, x, y, 100, 100, lpParam);
}

void CCWindow::create(HWND hParent, int x, int y, int w, int h, LPVOID lpParam)
{
    myWindowClass();
    myWindowDefaultInfo();
    m_x = x;
    m_y = y;
    m_w = w;
    m_h = h;
    m_hParent = hParent;
    m_lpParam = lpParam;
    createWinEx();
}

void CCWindow::createWinEx()
{
    switch(m_winType){
    case CCWIN_NSWINDOW:
        m_hWnd = CreateWindowNS(m_x, m_y, m_w, m_h, m_scale, this);
        break;
    case CCWIN_SOUNDSELECT:
        m_hWnd = CreateSoundSelectWindowNS(m_x, m_y, m_w, m_h, m_scale, this);
        ShowWindowMac(m_hWnd, TRUE);
        break;
    case CCWIN_CUSTOM:
        m_hWnd = CreateCustumWindow(m_y, m_x, m_w, m_h, m_hParent, this);
        break;
    }
}

void CCWindow::resize(int w, int h)
{
    if(m_winType == CCWIN_CUSTOM){
        SizeView(m_hWnd, w, h);
    }
}

// keep the window inside the visible screen area
void CCWindow::moveIntoMonitor(POINT* pt)
{
    RECT rc;
    GetWindowRect(m_hWnd, &rc);
    int x = rc.left;
    int y = rc.top;
    int w = rc.right - rc.left;
    int h = rc.bottom - rc.top;
    getVisibleScreenRect(m_hWnd, &rc);
    x = MIN(x, rc.right - w);
    x = MAX(rc.left, x);
    y = MIN(y, rc.bottom - h);
    y = MAX(rc.top, y);
    CGRect bounds = CGDisplayBounds(CGMainDisplayID());
    y = (int)(bounds.size.height - (y + h));    // flip to Cocoa coordinates
    setWindowPos(m_hWnd, x, y);
}

BOOL CCWindow::init(HINSTANCE hInstance)
{
    if(bFirst){
        bFirst = FALSE;
        hInst = hInstance;
        for(int i = 0; i < CCWINDOW_MAX; i++){
            pccWindow[i] = NULL;
        }
        for(int i = 0; i < CCWINDOW_MAX; i++){
            CCDialog::pccDialog[i] = NULL;
        }
    }
    return TRUE;
}

void CCWindow::myWindowClass()
{
}

CCDialog::CCDialog()
{
    m_hDlg = NULL;
}

CCDialog::~CCDialog()
{
    HWND hDlg = m_hDlg;
    if(hDlg){
        for(int i = 0; i < CCWINDOW_MAX; i++){
            if(pccDialog[i] && pccDialog[i]->m_hDlg == hDlg){
                pccDialog[i] = NULL;
            }
        }
    }
}

int CCDialog::open(HINSTANCE hInst, LPCSTR lpTemplate, HWND hParent)
{
    const char* name;
    if(lpTemplate == (LPCSTR)IDD_OPTION){
        name = "IDD_OPTION";
    }else if(lpTemplate == (LPCSTR)IDD_SAVESOUND){
        name = "IDD_SAVESOUND";
    }else{
        return 0;
    }
    return ModalDialogMac(hInst, name, hParent);
}

int CCDialog::createDialog(HINSTANCE hInst, LPCSTR lpTemplate, HWND hParent)
{
    m_hInst = hInst;
    m_hParent = hParent;
    return 0;
}

void CPopupWin::myWindowDefaultInfo()
{
    CCWindow::myWindowDefaultInfo();
    m_dwStyle = WS_POPUP;
}

void CChildWin::myWindowDefaultInfo()
{
    CCWindow::myWindowDefaultInfo();
    m_dwStyle = WS_CHILD;
}

void CMDIChild::myWindowDefaultInfo()
{
    CCWindow::myWindowDefaultInfo();
}

void CMDIChild2::myWindowDefaultInfo()
{
    CCWindow::myWindowDefaultInfo();
}
