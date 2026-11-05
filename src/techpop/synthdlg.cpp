/**
 * @file synthdlg.cpp
 * Sound select popup and option / save dialogs (Cocoa view factory only).
 *
 * Implements CSoundSelectWin (bank tree, program grid and readme editor) and the empty
 * option / save dialog procedures declared in synwin.h.
 */
#include <stdio.h>
#include <string.h>
#include <math.h>
#include <AudioToolbox/AudioToolbox.h>
#include "common.h"
#include "drawgui.h"
#include "sigdev.h"
#include "sound.h"
#include "synth1.h"
#include "synwin.h"

extern BOOL g_bTransparencyWin;

// Win32 message ids
#ifndef WM_CREATE
#define WM_CREATE       0x0001
#define WM_ACTIVATE     0x0006
#define WM_CLOSE        0x0010
#define WM_CHAR         0x0102
#define WM_COMMAND      0x0111
#define WM_MOUSEMOVE        0x0200
#define WM_LBUTTONDOWN      0x0201
#define WM_LBUTTONUP        0x0202
#define WM_RBUTTONDOWN      0x0204
#endif

//==================================================================
// CSoundSelectWin

int CSoundSelectWin::m_TVW = 250;

CSoundSelectWin::CSoundSelectWin()
{
}

void CSoundSelectWin::init(CSoundLibrary* pLib, int progNo, int bankNo, char* zipPath, int bProgMode, POINT* pt)
{
    m_pLib = pLib;
    m_curProg = progNo;
    m_hoverProg = progNo;
    CSoundLibrary::checkZipFile();
    m_pBank = m_pCurBank = CSoundLibrary::getBank(bankNo, zipPath);
    m_bValidBank = m_pBank != NULL;
    if (m_pBank && m_pBank->m_bankNo == -1 && m_pBank->m_patchNum == 0) m_bValidBank = 0;
    m_bInfoMode = 0;
    m_pt = *pt;
    m_bProgMode = bProgMode;
    m_winType = CCWIN_SOUNDSELECT;
    m_scale = g_bTransparencyWin ? 0.8f : 1.0f;
    m_bDragging = 0;
}

CSoundSelectWin::~CSoundSelectWin()
{
}

void CSoundSelectWin::calcWindowSize(int bankNo, int progNo, int* pW, int* pH)
{
    m_boxW = 180;
    m_boxH = 18;
    m_rcTree = CGRectMake(5, 40, m_TVW, 576);
    double x = m_TVW + 5.0 + 5.0;
    m_rcProg = CGRectMake(x, 40, 720, 576);
    m_rcBankName = CGRectMake(x, 5, 720, 30);
    *pW = (int)(x + 720.0 - 5.0 + 5.0 + 2.0);
    *pH = 623;
}

void CSoundSelectWin::createFont(float size)
{
}

void CSoundSelectWin::paint(CGContextRef hdc)
{
    if (m_pBank) drawBankNameInfo(hdc, m_pBank);
    if (m_bInfoMode == 0 && m_bValidBank) {
        for (int i = 0; i < 128; i++) {
            drawProgBox(hdc, i, m_pBank, i == m_hoverProg);
        }
    }
}

void CSoundSelectWin::drawBankNameInfo(CGContextRef hdc, CBank* pBank)
{
    char str[256];
    RECT rc;
    rc.left = (int)(m_rcBankName.origin.x + 90.0);
    rc.top = (int)m_rcBankName.origin.y;
    rc.right = (int)(m_rcBankName.origin.x + 90.0 + m_rcBankName.size.width);
    rc.bottom = (int)(m_rcBankName.origin.y + m_rcBankName.size.height);
    CGContextSaveGState(hdc);
    CGContextSetRGBFillColor(hdc, 1.0, 1.0, 1.0, 1.0);
    CGContextFillRect(hdc, CGRectMake(rc.left, rc.top, rc.right - rc.left, rc.bottom - rc.top));
    rc.left++;
    rc.top++;
    rc.right--;
    rc.bottom--;
    if(pBank->m_bankNo != -1) sprintf(str, "Bank%02d:%s", pBank->m_bankNo, pBank->m_name);
    else sprintf(str, "ExtBank:%s", pBank->m_name);
    CGContextSetTextDrawingMode(hdc, kCGTextFill);
    CGContextScaleCTM(hdc, 1.0, -1.0);
    CGContextTranslateCTM(hdc, rc.left, -rc.top);

    CFStringRef fontName = CFStringCreateWithCString(kCFAllocatorDefault, "Helvetica", kCFStringEncodingUTF8);
    CFStringRef keys[2] = { kCTFontAttributeName, kCTForegroundColorAttributeName };
    CTFontRef font = CTFontCreateWithName(fontName, 24.0, NULL);
    CGColorSpaceRef cs = CGColorSpaceCreateDeviceRGB();
    CGFloat rgba[4] = { 0, 0, 0, 1.0 };
    CGColorRef color = CGColorCreate(cs, rgba);
    CFTypeRef values[2] = { font, color };
    CFDictionaryRef attr = CFDictionaryCreate(kCFAllocatorDefault, (const void**)keys, (const void**)values, 2,
        &kCFTypeDictionaryKeyCallBacks, &kCFTypeDictionaryValueCallBacks);
    CFRelease(fontName);
    CFRelease(font);
    CGColorRelease(color);
    CGColorSpaceRelease(cs);
    CFStringRef s = CFStringCreateWithCString(kCFAllocatorDefault, str, kCFStringEncodingUTF8);
    CFAttributedStringRef as = CFAttributedStringCreate(kCFAllocatorDefault, s, attr);
    CTLineRef line = CTLineCreateWithAttributedString(as);
    CGContextSetTextPosition(hdc, 0, -24.0);
    CTLineDraw(line, hdc);
    CFRelease(line);
    CFRelease(s);
    CFRelease(as);
    CFRelease(attr);

    double r = m_rcBankName.origin.x + m_rcBankName.size.width;
    rc.right = (int)r;
    rc.left = (int)(r - 120.0);
    CGContextSetRGBFillColor(hdc, 1.0, 1.0, 1.0, 1.0);
    CGContextFillRect(hdc, CGRectMake(rc.left, rc.top, rc.right - rc.left, rc.bottom - rc.top));
    sprintf(str, "%3d Programs", pBank->m_patchNum);
    CGContextSetTextDrawingMode(hdc, kCGTextFill);
    CGContextScaleCTM(hdc, 1.0, -1.0);
    CGContextTranslateCTM(hdc, rc.left, -rc.top);
    CGContextSetRGBFillColor(hdc, 0, 0, 0, 1.0);
    CGContextShowTextAtPoint(hdc, 0, -24.0, str, strlen(str));
    CGContextRestoreGState(hdc);
}

/**
 * @brief Fills a rectangle given in CG coordinates with the current fill colour.
 *
 * @param hdc   Graphics context.
 * @param x     Left edge (CG coordinates).
 * @param y     Bottom edge (CG coordinates).
 * @param w     Width.
 * @param h     Height.
 */
static void FillRectD(CGContextRef hdc, double x, double y, double w, double h)
{
    CGContextFillRect(hdc, CGRectMake(x, y, w, h));
}

// Note: using getProgRect/FillRectD, the local declarations, and shrinking the box in
// place right after the frame fill are significant for reproducing the released binary.
void CSoundSelectWin::drawProgBox(CGContextRef hdc, int progNo, CBank* pBank, int bHilight)
{
    char str[256];
    COLORREF color;
    if (m_pBank == NULL) return;
    CSoundData sound;
    strcpy(sound.m_name, "initial sound");
    CPatch* pPatch = &pBank->m_patch[progNo];
    sprintf(str, "%d:%s", progNo + 1, GetEncodedString(pPatch->getName()));
    color = pPatch->getColor();
    CGRect rc;
    CGContextSaveGState(hdc);
    getProgRect(progNo, &rc);
    if (color == 0xffffffff) {
        color = bHilight ? 0xc8c8c8 : 0xffffff;
    } else if (bHilight == 0) {
        BYTE hue, lum, sat;
        RGBtoHLS(color, &hue, &lum, &sat);
        lum = 200;
        color = HLStoRGB(hue, lum, sat);
    }
    FillRectD(hdc, rc.origin.x, rc.origin.y, rc.size.width, rc.size.height);
    rc.origin.x += 1.0;
    rc.origin.y += 1.0;
    rc.size.width -= 2.0;
    rc.size.height -= 2.0;
    COLORREF textColor = 0;
    if (m_curProg == progNo && m_pCurBank == pBank) {
        textColor = color != 0xff0000 ? 0xff0000 : 0xffffff;
    }
    CGContextSetRGBFillColor(hdc, (color & 0xff) / 255.0, ((color >> 8) & 0xff) / 255.0, ((color >> 16) & 0xff) / 255.0, 1.0);
    {
        CGRect rc2 = rc;
        FillRectD(hdc, rc2.origin.x, rc2.origin.y, rc2.size.width, rc2.size.height);
        CGPoint pt = rc2.origin;
        CGContextSetTextDrawingMode(hdc, kCGTextFill);
        CGContextScaleCTM(hdc, 1.0, -1.0);
        CGContextTranslateCTM(hdc, pt.x, -pt.y);
        CFStringRef fontName = CFStringCreateWithCString(kCFAllocatorDefault, "Helvetica", kCFStringEncodingUTF8);
        CFStringRef keys[2] = { kCTFontAttributeName, kCTForegroundColorAttributeName };
        CTFontRef font = CTFontCreateWithName(fontName, 12.0, NULL);
        CGColorSpaceRef cs = CGColorSpaceCreateDeviceRGB();
        CGFloat rgba[4] = { (textColor & 0xff) / 255.0, ((textColor >> 8) & 0xff) / 255.0, (textColor >> 16) / 255.0, 1.0 };
        CGColorRef color2 = CGColorCreate(cs, rgba);
        CFTypeRef values[2] = { font, color2 };
        CFDictionaryRef attr = CFDictionaryCreate(kCFAllocatorDefault, (const void**)keys, (const void**)values, 2,
            &kCFTypeDictionaryKeyCallBacks, &kCFTypeDictionaryValueCallBacks);
        CFRelease(fontName);
        CFRelease(font);
        CGColorRelease(color2);
        CGColorSpaceRelease(cs);
        CFStringRef s = CFStringCreateWithCString(kCFAllocatorDefault, str, kCFStringEncodingUTF8);
        CFAttributedStringRef as = CFAttributedStringCreate(kCFAllocatorDefault, s, attr);
        CTLineRef line = CTLineCreateWithAttributedString(as);
        CGContextSetTextPosition(hdc, 0, -12.0);
        CTLineDraw(line, hdc);
        CFRelease(line);
        CFRelease(s);
        CFRelease(as);
        CFRelease(attr);
    }
    CGContextRestoreGState(hdc);
}

void CSoundSelectWin::getProgRect(int progNo, CGRect* rc)
{
    int col = progNo / 32;
    int row = progNo % 32;
    rc->origin.x = col * m_boxW + m_rcProg.origin.x;
    rc->origin.y = row * m_boxH + m_rcProg.origin.y;
    rc->size.width = m_boxW;
    rc->size.height = m_boxH;
}

void CSoundSelectWin::addBankTree(HWND hWnd, HWND hTV)
{
    char str[256];
    void* hItem = TreeView_InsertItem(hTV, NULL, "Internal", NULL);
    m_hInternal = hItem;
    for (int i = 0; i < 100; i++) {
        CBank* pBank = CSoundLibrary::getBank(i, NULL);
        pBank->checkCache();
        if (pBank->m_patchNum > 0 || m_pCurBank == pBank) {
            sprintf(str, "%02d:%s (%d)", i, pBank->m_name, pBank->m_patchNum);
            TreeView_InsertItem(hTV, hItem, str, pBank);
        }
    }
    hItem = TreeView_InsertItem(hTV, NULL, "Internal (All)", NULL);
    m_hInternalAll = hItem;
    for (int i = 0; i < 100; i++) {
        CBank* pBank = CSoundLibrary::getBank(i, NULL);
        int num = pBank->m_patchNum;
        sprintf(str, "%02d:%s (%d)", i, pBank->m_name, num);
        TreeView_InsertItem(hTV, hItem, str, pBank);
    }
    hItem = TreeView_InsertItem(hTV, NULL, "External (.zip)", NULL);
    m_hExternal = hItem;
    int num = CSoundLibrary::getZipNum();
    for (int i = 0; i < num; i++) {
        addZipBank2TV(hTV, hItem, CSoundLibrary::getZipRootBank(i));
    }
}

void CSoundSelectWin::addZipBank2TV(HWND hTV, void* hParent, CBank* pBank)
{
    char str[256];
    if (pBank->m_patchNum) sprintf(str, "%s (%d)", pBank->m_name, pBank->m_patchNum);
    else sprintf(str, "%s", pBank->m_name);
    void* hItem = TreeView_InsertItem(hTV, hParent, str, pBank);
    int num = pBank->getChildNum();
    for (int i = 0; i < num; i++) {
        addZipBank2TV(hTV, hItem, pBank->getChild(i));
    }
}

void* CSoundSelectWin::selectBank(HWND hTV, CBank* pBank, void* hItem)
{
    if (hItem == NULL) hItem = TreeView_GetRoot(hTV);
    while (hItem) {
        if ((CBank*)TreeView_GetItemParam(hTV, hItem) == pBank) {
            TreeView_SelectItem(hTV, hItem);
            return hItem;
        }
        void* hChild = TreeView_GetChild(hTV, hItem);
        if (hChild) {
            void* hFound = selectBank(hTV, pBank, hChild);
            if (hFound) return hFound;
        }
        hItem = TreeView_GetNextSibling(hTV, hItem);
    }
    return NULL;
}

int CSoundSelectWin::getProgNoFromPt(POINT* pt)
{
    int x = (int)(pt->x - m_rcProg.origin.x);
    if (x < 0) return -1;
    int y = (int)(pt->y - m_rcProg.origin.y);
    if (y < 0) return -1;
    if (x >= m_boxW * 4 || y >= m_boxH * 32) return -1;
    return (x / m_boxW) * 32 + y / m_boxH;
}

void CSoundSelectWin::moveChildWin(HWND hWnd)
{
    MoveWindowControl(m_hTree, (int)m_rcTree.origin.x, (int)m_rcTree.origin.y, (int)m_rcTree.size.width, (int)m_rcTree.size.height, 1);
    MoveWindowControl(m_hButton, (int)m_rcProg.origin.x, (int)m_rcBankName.origin.y, 80, (int)m_rcBankName.size.height, 1);
    if (m_bInfoMode == 1) {
        MoveWindowControl(m_hUpdateButton, (int)m_rcProg.origin.x, (int)(m_rcProg.origin.y + m_rcProg.size.height - 30.0), 160, 30, 1);
        MoveWindowControl(m_hEdit, (int)m_rcProg.origin.x, (int)m_rcProg.origin.y, (int)m_rcProg.size.width, (int)(m_rcProg.size.height - 35.0), 1);
    }
}

void CSoundSelectWin::initDisplayInfo()
{
    CBank* pBank = m_pBank;
    if (pBank && pBank->m_bExist) {
        int bankNo = pBank->m_bankNo;
        FILE* fp = fopen(pBank->m_readmePath, "rb");
        if (fp) {
            char* buf = new char[0x10000];
            int len = fread(buf, 1, 0xffff, fp);
            fclose(fp);
            if (len >= 0) buf[len] = '\0';
            Edit_SetText(m_hEdit, len >= 0 ? buf : "");
            delete[] buf;
        } else {
            bool bExtBank = (bankNo == -1);
            Edit_SetText(m_hEdit, bExtBank ? "readme.txt was not found." : "Please include your bank information in the readme.txt in your bank folder.");
        }
    } else {
        Edit_SetText(m_hEdit, "");
        Edit_SetReadOnly(m_hEdit, 1);
        EnableWindow(m_hUpdateButton, 0);
    }
}

LRESULT CSoundSelectWin::myWndProc(HWND hWnd, int msg, WPARAM wParam, LPARAM lParam)
{
    switch (msg) {
    case WM_CREATE:
        {
            m_bClosing = 0;
            InvisibleWindow(hWnd, 80);
            HWND hTV = CreateOutlineView(hWnd, 0, 0, 0, 0, 124);
            m_hTree = hTV;
            addBankTree(NULL, hTV);
            CSoundLibrary::lockZipBank();
            m_hButton = CreateButton(hWnd, 0, 0, 0, 0, "List/Info", 125);
            moveChildWin(hWnd);
            moveIntoMonitor(&m_pt);
            TreeView_Update(hTV);
            void* hItem = selectBank(hTV, m_pBank, NULL);
            SetFocusControl(hTV);
            if (hItem == NULL) return 0;
            PostMessage(hWnd, 0x8001, (WPARAM)hItem, 0);
        }
        return 0;
    case WM_ACTIVATE:
        if (wParam != 0) return 0;
        goto close;
    case WM_PAINT:
        if (lParam == 0) return 0;
        paint((CGContextRef)lParam);
        return 0;
    case WM_CLOSE:
        CSoundLibrary::unlockZipBank();
        return 0;
    case 0x4e:  // WM_NOTIFY: tree selection changed
        m_pBank = (CBank*)lParam;
        m_bValidBank = m_pBank != NULL;
        if (m_pBank && m_pBank->m_bankNo == -1 && m_pBank->m_patchNum == 0) m_bValidBank = 0;
        if (m_bInfoMode == 0) {
            InvalidateRect(hWnd, NULL, m_bValidBank == 0);
            return 0;
        }
        initDisplayInfo();
        {
            RECT rc;
            rc.left = (int)m_rcBankName.origin.x;
            rc.top = (int)m_rcBankName.origin.y;
            rc.right = (int)(m_rcBankName.origin.x + m_rcBankName.size.width);
            rc.bottom = (int)(m_rcBankName.origin.y + m_rcBankName.size.height);
            InvalidateRect(hWnd, &rc, 1);
        }
        return 0;
    case WM_CHAR:
        if (wParam == 0x1b) goto close;
        return defaultProc(hWnd, msg, wParam, lParam);
    case WM_MOUSEMOVE:
        {
            POINT pt;
            pt.x = (short)lParam;
            pt.y = (int)lParam >> 16;
            if (m_bDragging || m_bInfoMode || m_bValidBank == 0) return 0;
            int no = getProgNoFromPt(&pt);
            if (m_hoverProg == no) return 0;
            CGContextRef hdc = GetDC0(hWnd);
            if (m_hoverProg != -1) drawProgBox(hdc, m_hoverProg, m_pBank, 0);
            m_hoverProg = no;
            if (no != -1) drawProgBox(hdc, no, m_pBank, 1);
            ReleaseDC0(hWnd, hdc);
        }
        return 0;
    case WM_LBUTTONDOWN:
        {
            int x = (short)lParam;
            if (x >= m_rcTree.origin.x + m_rcTree.size.width && x <= m_rcProg.origin.x) {
                double y = (int)lParam >> 16;
                if (m_rcTree.origin.y <= y && y <= m_rcTree.origin.y + m_rcTree.size.height) {
                    m_bDragging = 1;
                    m_dragX = x;
                    SetCapture(hWnd);
                    return 0;
                }
            }
            if (m_bInfoMode == 0 && m_bValidBank && m_hoverProg != -1 && m_pBank) {
                m_pLib->setReqProgNo(m_hoverProg);
                m_pLib->setReqBankNo(m_pBank->m_bankNo, m_pBank->m_innerPath);
                PostMessageControl(m_hParent, 0x709, 300, 0);
            }
        }
        // fall through: a click outside the splitter closes the window
    case WM_RBUTTONDOWN:
    close:
        if (m_bClosing == 0) {
            m_bClosing = 1;
            SendMessageControl(m_hParent, 0x8002, (WPARAM)this, 0);
        }
        return 0;
    case WM_LBUTTONUP:
        {
            if (m_bDragging == 0) return 0;
            int x = (short)lParam;
            m_bDragging = 0;
            int d = x - m_dragX;
            ReleaseCapture();
            if (d) {
                int w = d + m_TVW;
                if (w < 50) w = 50;
                if (w > 600) w = 600;
                m_TVW = w;
                int width, height;
                calcWindowSize(0, 0, &width, &height);
                moveChildWin(hWnd);
                RECT rc;
                GetWindowRect(hWnd, &rc);
                MoveWindowMac(hWnd, rc.left, rc.top, width, height, 1);
            }
            InvalidateRect(hWnd, NULL, 1);
        }
        return 0;
    case WM_COMMAND:
        if ((wParam & 0xffff) == 125) {
            if (m_bInfoMode == 0) {
                m_bInfoMode = 1;
                m_hEdit = CreateEdit(hWnd, 0, 0, 0, 0, 126);
                Edit_SetReadOnly(m_hEdit, 1);
                m_hUpdateButton = CreateButton(hWnd, 0, 0, 0, 0, "Update readme.txt", 127);
                EnableWindow(m_hUpdateButton, 0);
                moveChildWin(hWnd);
                initDisplayInfo();
            } else {
                m_bInfoMode = 0;
                DestroyWindowControl(m_hEdit);
                DestroyWindowControl(m_hUpdateButton);
            }
            InvalidateRect(hWnd, NULL, 1);
            UpdateWindowMac(hWnd);
            return 0;
        }
        if ((wParam & 0xffff) == 127) {
            char* buf = new char[0x10000];
            int len = Edit_GetText(m_hEdit, buf, 0x10000);
            if (len == 0) {
                DeleteFile(m_pBank->m_readmePath);
            } else {
                FILE* fp = fopen(m_pBank->m_readmePath, "wb");
                if (fp) {
                    fwrite(buf, 1, len, fp);
                    fclose(fp);
                }
            }
            delete[] buf;
        }
        return 0;
    case 0x8001:
        POINT pt;
        if (m_bProgMode == 0) {
            RECT rc;
            if (!TreeView_GetItemRect(m_hTree, (void*)wParam, &rc, 1)) return 0;
            pt.x = (rc.left + rc.right) / 2;
            pt.y = (rc.top + rc.bottom) / 2;
            ClientToScreenControl(m_hTree, &pt);
            SetCursorPos(pt.x, pt.y);
        } else {
            CGRect rcProg;
            getProgRect(m_curProg, &rcProg);
            pt.x = (int)(rcProg.origin.x + rcProg.size.width / 2);
            pt.y = (int)(rcProg.origin.y + rcProg.size.height / 2);
            ClientToScreen(hWnd, &pt);
            SetCursorPos(pt.x, pt.y);
        }
        return 0;
    }
    return defaultProc(hWnd, msg, wParam, lParam);
}

//==================================================================
// Option / save dialogs (the Mac build has no dialog implementation)

BOOL COptionDlg::myDlgProc(HWND hDlg, UINT msg, WPARAM wParam, LPARAM lParam)
{
    return 0;
}

BOOL COptMidiDlg::myDlgProc(HWND hDlg, UINT msg, WPARAM wParam, LPARAM lParam)
{
    return 0;
}

BOOL COptGeneralDlg::myDlgProc(HWND hDlg, UINT msg, WPARAM wParam, LPARAM lParam)
{
    return 0;
}

void COptGeneralDlg::setMasterTuneText(HWND hDlg, int tune)
{
    char str[256];
    sprintf(str, "%d Cents", tune);
    sprintf(str, "A4=%.3fHz", fpow(2.0, tune / 1200.0) * 440.0);
}

int COptGeneralDlg::openFolderDlg(HWND hDlg, char* title, char* path)
{
    return 0;
}

BOOL CSoundSaveDlg::myDlgProc(HWND hDlg, UINT msg, WPARAM wParam, LPARAM lParam)
{
    return 0;
}

tagWriteProgramInfo* CSoundSaveDlg::getWPI()
{
    return (tagWriteProgramInfo*)m_wpi;
}

