/**
 * @file guiparts.cpp
 * @brief GUI widgets of the editor window (view factory only).
 *
 * Implementation of the widgets declared in guiparts.h, the widget collection that renders them into
 * the panel bitmap and routes mouse input, the value popups, and small Win32 rectangle helpers.
 */

#include <stdio.h>
#include <string.h>
#include "common.h"
#include "guiparts.h"
#include "synth1.h"
#include "synwin.h"

// popup sub windows shared by all CLCDs
static CLCDSubWin g_LCDSubWin;
static CVolSubWin g_VolSubWin;

//---------------------------------------------------------------------------
// CGUICollection

CGUICollection::CGUICollection()
{
    startupGraphics();
    initResouce();
}

void CGUICollection::initResouce()
{
    m_num = 0;
    m_pLastParts = NULL;
    m_sliderType = 0;
    m_pDragParts = NULL;
    m_pTempDib = NULL;
    m_hdcMem = NULL;
    m_pDib = NULL;
    m_pDisplay = new CDisplayArea;
    m_pCapture = NULL;
}

CGUICollection::~CGUICollection()
{
    destroyResouce();
    shutdownGraphics();
}

void CGUICollection::destroyResouce()
{
    if(m_pDisplay){
        delete m_pDisplay;
        m_pDisplay = NULL;
    }
    if(m_hdcMem){
        DeleteDC(m_hdcMem);
        m_hdcMem = NULL;
    }
    if(m_pDib){
        delete m_pDib;
        m_pDib = NULL;
    }
    if(m_pTempDib){
        delete m_pTempDib;
        m_pTempDib = NULL;
    }
    for(int i = 0; i < m_num; i++){
        if(m_pParts[i]) delete m_pParts[i];
    }
    m_num = 0;
}

void CGUICollection::changedSliderType()
{
    m_sliderType = getIniInt("slidertype", 1);
}

// work DIB, grown on demand
CDIB* CGUICollection::getTempDIB(int w, int h)
{
    if(m_pTempDib){
        if(m_pTempDib->m_width < w || m_pTempDib->m_height < h){
            w = MAX(w, m_pTempDib->m_width);
            h = MAX(h, m_pTempDib->m_height);
            delete m_pTempDib;
            m_pTempDib = new CDIB;
            m_pTempDib->create(w, h);
        }
    }else{
        m_pTempDib = new CDIB;
        m_pTempDib->create(w, h);
    }
    return m_pTempDib;
}

void CGUICollection::init(HWND hWnd, int w, int h, COLORREF color, int scale, void* pSynth)
{
    destroyResouce();
    initResouce();
    m_pSynth = pSynth;
    m_hWnd = hWnd;
    m_scalePercent = scale;
    m_pDib = new CDIB;
    m_pDib->create(w, h);
    HDC hdc = GetDCControl(m_hWnd);
    m_hdcMem = CreateCompatibleDC(hdc);
    m_fontSize = 16.0f;
    ReleaseDCControl(m_hWnd, hdc);
    m_scale = scale / 100.0f;
    m_pDisplay->init(this, m_hdcMem, m_pDib, 0xffffff);
    RECT rc;
    MySetRect(&rc, 0, 0, w, h);
    DrawPanel(((HDC)m_pDib->m_hBmp), &rc, color, 3);
}

void CGUICollection::add(CGuiParts* pParts)
{
    m_pParts[m_num] = pParts;
    pParts->m_pGui = this;
    m_num++;
    pParts->setCoordinatesScale(m_scalePercent);
}

void CGUICollection::incrimentLEDGroup(int from, int to)
{
    int i;
    int on = to;
    for(i = from; i <= to; i++){
        CLED* pLED = (CLED*)getGui(i);
        if(pLED && pLED->m_bOn){
            pLED->setState(0);
            on = i;
            break;
        }
    }
    if(i < to) from = on + 1;
    CLED* pLED = (CLED*)getGui(from);
    if(pLED) pLED->setState(1);
}

CGuiParts* CGUICollection::getGui(int id)
{
    for(int i = 0; i < m_num; i++){
        if(m_pParts[i]->m_id == id) return m_pParts[i];
    }
    return NULL;
}

void CGUICollection::decrimentLEDGroup(int from, int to)
{
    int i;
    int on = from;
    for(i = from; i <= to; i++){
        CLED* pLED = (CLED*)getGui(i);
        if(pLED && pLED->m_bOn){
            pLED->setState(0);
            on = i;
            break;
        }
    }
    if(on > from) to = on - 1;
    CLED* pLED = (CLED*)getGui(to);
    if(pLED) pLED->setState(1);
}

int CGUICollection::getOnLEDData(int from, int to)
{
    for(int i = from; i <= to; i++){
        CLED* pLED = (CLED*)getGui(i);
        if(pLED && pLED->m_bOn) return pLED->m_data;
    }
    return 0;
}

void CGUICollection::setOnLEDData(int data, int from, int to)
{
    for(int i = from; i <= to; i++){
        CLED* pLED = (CLED*)getGui(i);
        if(pLED){
            if(pLED->m_data == data) pLED->setState(TRUE);
            else pLED->setState(FALSE);
        }
    }
}

void CGUICollection::drawAll()
{
    for(int i = 0; i < m_num; i++){
        m_pParts[i]->drawMe(((HDC)m_pDib->m_hBmp), m_pDib, TRUE);
    }
    if(((HDC)m_pDib->m_hBmp)) CGContextFlush(((HDC)m_pDib->m_hBmp));
}

int CGUICollection::processMouseMessage(int msg, LPARAM lParam)
{
    if(m_pDragParts) return processDragInput(msg, lParam);

    CGuiParts* pParts = m_pCapture;
    if(pParts){
        if(msg == WM_RBUTTONUP || (msg == WM_LBUTTONDOWN && !pParts->PtInRt((short)LOWORD(lParam), (short)HIWORD(lParam)))){
            pParts = m_pCapture;
            m_pCapture = NULL;
            InvalidateRectControl(m_hWnd, &pParts->m_rc, TRUE);
            pParts = NULL;
        }else{
            pParts = m_pCapture;
        }
    }
    if(pParts == NULL){
        for(int i = 0; i < m_num; i++){
            if(m_pParts[i]->PtInRt((short)LOWORD(lParam), (short)HIWORD(lParam)) && m_pParts[i]->m_id){
                pParts = m_pParts[i];
                break;
            }
        }
    }
    if(m_pLastParts != pParts && m_pLastParts) m_pLastParts->mouseLeave();
    int ret = 0;
    if(pParts){
        ret = 1;
        switch(msg){
        case WM_MOUSEMOVE:
            pParts->mouseMove((short)LOWORD(lParam), (short)HIWORD(lParam));
            break;
        case WM_LBUTTONDOWN:
            if(((CSynthesizer1*)m_pSynth)->m_bMidiLearning){
                if(CSynthWin::isMidiLearnEndParts(pParts->m_id)){
                    ((CSynthesizer1*)m_pSynth)->endMidiLearning();
                    break;
                }
                int id = pParts->m_id - 1000;
                if(CSynthesizer1::isMidiCCAssignable(id)){
                    ((CSynthesizer1*)m_pSynth)->m_midiLearnParamID = id;
                }
            }
            pParts->mouseDown((short)LOWORD(lParam), (short)HIWORD(lParam));
            break;
        case WM_LBUTTONUP:
            pParts->mouseUp((short)LOWORD(lParam), (short)HIWORD(lParam));
            break;
        case WM_MOUSEWHEEL:
            pParts->mouseWheel((int)lParam);
            break;
        }
    }
    m_pLastParts = pParts;
    return ret;
}

void CGUICollection::setValue(int id, int val)
{
    CGuiParts* pParts = getGui(id);
    if(pParts) pParts->setValue(val);
}

void CGUICollection::changeWindowHandle(HWND hWnd)
{
    for(int i = 0; i < m_num; i++){
        if(m_pParts[i]->m_hWnd == m_hWnd) m_pParts[i]->m_hWnd = hWnd;
    }
    m_hWnd = hWnd;
}

void CGUICollection::startDragInputPart(CGuiParts* pParts, int x, int y, int min, int max, int val, int bDisplay)
{
    char str[256];
    m_pDragParts = pParts;
    m_dragX = x;
    m_dragY = y;
    m_dragMin = min;
    m_dragMax = max;
    m_dragVal = val;
    m_bDragDisplay = bDisplay;
    SetCaptureControl(m_hWnd);
    if(bDisplay){
        m_pDisplay->setDisplayLocation((pParts->m_rc.left + pParts->m_rc.right) / 2, pParts->m_rc.bottom + 1);
        if(!CSynthWin::convertVal(str, pParts->m_id, val, m_pSynth)){
            sprintf(str, "%d", val);
        }
        m_pDisplay->setText(str);
        m_pDisplay->setEnable(TRUE);
        m_pDisplay->drawUpdate();
    }
}

int CGUICollection::processDragInput(int msg, LPARAM lParam)
{
    if(msg == WM_LBUTTONUP){
        stopDragInputPart(m_pDragParts);
        return 0;
    }
    int y = (int)lParam >> 16;
    int val = (m_dragY - y) * (m_dragMax - m_dragMin) / 128 + m_dragVal;
    val = MIN(val, m_dragMax);
    val = MAX(val, m_dragMin);
    if(m_bDragDisplay){
        char str[256];
        if(!CSynthWin::convertVal(str, m_pDragParts->m_id, val, m_pSynth)){
            sprintf(str, "%d", val);
        }
        m_pDisplay->setText(str);
    }
    m_pDragParts->setVol(val);
    return 1;
}

void CGUICollection::stopDragInputPart(CGuiParts* pParts)
{
    ReleaseCaptureControl();
    m_pDragParts = NULL;
    m_pDisplay->setEnable(FALSE);
    m_pDisplay->drawUpdate();
}

//---------------------------------------------------------------------------
// Win32 rectangle helpers on top of QuickDraw style Rects

BOOL IsRectEmpty(const RECT* rc)
{
    if(rc->left == rc->right && rc->top == rc->bottom) return TRUE;
    return FALSE;
}

BOOL SectRect(Rect* a, Rect* b, Rect* d)
{
    if(a->right < b->left || b->right < a->left || a->bottom < b->top || b->bottom < a->top) return FALSE;
    d->left = (a->left > b->left) ? a->left : b->left;
    d->right = (a->right < b->right) ? a->right : b->right;
    d->top = (a->top > b->top) ? a->top : b->top;
    d->bottom = (a->bottom < b->bottom) ? a->bottom : b->bottom;
    return TRUE;
}

BOOL IntersectRect(const RECT* a, const RECT* b, RECT* d)
{
    if(IsRectEmpty(a) || IsRectEmpty(b)) return FALSE;
    Rect ra, rb, rd;
    ra.left = a->left;
    ra.top = a->top;
    ra.right = a->right;
    ra.bottom = a->bottom;
    rb.left = b->left;
    rb.top = b->top;
    rb.right = b->right;
    rb.bottom = b->bottom;
    Boolean ret = SectRect(&ra, &rb, &rd);
    d->left = rd.left;
    d->top = rd.top;
    d->right = rd.right;
    d->bottom = rd.bottom;
    return ret;
}

void CGUICollection::paint(HDC hdc, RECT* rc, int ox, int oy)
{
    if(m_pDib){
        CGImageRef img = CGBitmapContextCreateImage(((HDC)m_pDib->m_hBmp));
        if(img){
            CGImageRef part = CGImageCreateWithImageInRect(img,
                CGRectMake(ox, (int)CGBitmapContextGetHeight(((HDC)m_pDib->m_hBmp)) - (rc->bottom - rc->top) - oy, rc->right - rc->left, rc->bottom - rc->top));
            if(part){
                CGContextDrawImage(hdc, CGRectMake(rc->left, rc->top, rc->right - rc->left, rc->bottom - rc->top), part);
                CGImageRelease(part);
            }
            CGImageRelease(img);
        }
    }
    RECT rcSect;
    if(m_pCapture && IntersectRect(rc, &m_pCapture->m_rc, &rcSect)){
        // draw the captured part on top
        CGContextSaveGState(hdc);
        CGContextBeginPath(hdc);
        CGContextMoveToPoint(hdc, rcSect.left, rcSect.top);
        CGContextAddLineToPoint(hdc, rcSect.right, rcSect.top);
        CGContextAddLineToPoint(hdc, rcSect.right, rcSect.bottom);
        CGContextAddLineToPoint(hdc, rcSect.left, rcSect.bottom);
        CGContextClosePath(hdc);
        CGContextClip(hdc);
        m_pCapture->drawMe(hdc, NULL, TRUE);
        CGContextRestoreGState(hdc);
    }
    if(m_pDisplay->isEnabled()){
        RECT* prc;
        m_pDisplay->paint(hdc, prc, ox - rc->left, oy - rc->top);
    }
}

//---------------------------------------------------------------------------
// CGuiParts

void CGuiParts::init(HWND hWnd, int id, int x, int y, int w, int h, char* text, COLORREF bkColor, COLORREF textColor)
{
    m_hWnd = hWnd;
    m_id = id;
    m_rc.left = x;
    m_rc.top = y;
    m_rc.right = w + x;
    m_rc.bottom = y + h;
    if(text) strcpy(m_text, text);
    else m_text[0] = 0;
    m_bkColor = bkColor;
    m_textColor = textColor;
    m_color = bkColor;
    m_pUser = NULL;
}

void CGuiParts::drawMeOwn()
{
    if(m_hWnd == NULL) return;
    if(drawMe(((HDC)m_pGui->m_pDib->m_hBmp), m_pGui->m_pDib, FALSE)){
        if(((HDC)m_pGui->m_pDib->m_hBmp)) CGContextFlush(((HDC)m_pGui->m_pDib->m_hBmp));
        InvalidateRectControl(m_hWnd, &m_rc, FALSE);
    }
}

BOOL CGuiParts::PtInRt(int x, int y)
{
    POINT pt;
    pt.x = x;
    pt.y = y;
    return PtInRect0(&m_rc, pt);
}

void CGuiParts::setCoordinatesScale(int percent)
{
    int w = (m_rc.right - m_rc.left) * percent / 100;
    int h = (m_rc.bottom - m_rc.top) * percent / 100;
    m_rc.left = m_rc.left * percent / 100;
    m_rc.top = m_rc.top * percent / 100;
    m_rc.right = m_rc.left + w;
    m_rc.bottom = m_rc.top + h;
    w = (m_rcText.right - m_rcText.left) * percent / 100;
    h = (m_rcText.bottom - m_rcText.top) * percent / 100;
    m_rcText.left = m_rcText.left * percent / 100;
    m_rcText.top = m_rcText.top * percent / 100;
    m_rcText.right = m_rcText.left + w;
    m_rcText.bottom = m_rcText.top + h;
}

//---------------------------------------------------------------------------
// CPushButton

CPushButton::CPushButton(HWND hWnd, int id, int x, int y, int w, int h, char* text, COLORREF bkColor, COLORREF textColor, COLORREF color)
{
    init(hWnd, id, x, y, w, h, text, bkColor, textColor);
    m_color = color;
    m_bPushed = 0;
    m_mode = 0;
    m_bToggle = 0;
}

void CPushButton::setLCDMode(int mode, CGuiParts* pParts)
{
    m_mode = mode;
    m_pTarget = pParts;
}

void CPushButton::setLEDGroup(int from, int to, int bToggle)
{
    m_mode = 1;
    m_ledFrom = from;
    m_ledTo = to;
    m_bToggle = (to - from == 1 && bToggle);
    for(int i = from; i <= to; i++){
        CLED* pLED = (CLED*)m_pGui->getGui(i);
        if(pLED) pLED->setParentGui(this);
    }
}

BOOL CPushButton::drawMe(HDC hdc, CDIB* pDib, int bAll)
{
    if(m_rc.right == m_rc.left) return FALSE;
    DrawTextButton(hdc, &m_rc, m_text, m_bPushed, 1, m_color, m_textColor, 0, 0);
    DrawButtonShadow(hdc, &m_rc, m_bkColor, m_bPushed);
    return TRUE;
}

void CPushButton::mouseLeave()
{
    if(m_bPushed){
        m_bPushed = 0;
        drawMeOwn();
    }
}

void CPushButton::mouseUp(int x, int y)
{
    mouseLeave();
}

void CPushButton::mouseDown(int x, int y)
{
    m_bPushed = 1;
    drawMeOwn();
    if(m_mode == 1){
        m_pGui->incrimentLEDGroup(m_ledFrom, m_ledTo);
        int data = m_pGui->getOnLEDData(m_ledFrom, m_ledTo);
        PostMessageControl(m_hWnd, WM_GUIPARTS_NOTIFY, m_id, data);
    }else if(m_mode == 2){
        m_pTarget->incIndex();
    }else if(m_mode == 3){
        m_pTarget->decIndex();
    }else{
        PostMessageControl(m_hWnd, WM_GUIPARTS_NOTIFY, m_id, 0);
    }
}

void CPushButton::setValue(int val)
{
    if(m_mode == 1) m_pGui->setOnLEDData(val, m_ledFrom, m_ledTo);
}

// an LED of the group was clicked
void CPushButton::notifyClicked(int id)
{
    if(m_mode != 1) return;
    CLED* pLED = (CLED*)m_pGui->getGui(id);
    if(pLED == NULL) return;
    if(m_bToggle && pLED->m_bOn){
        // two state group: clicking the lit LED selects the other one
        pLED = (CLED*)m_pGui->getGui(id == m_ledFrom ? m_ledTo : m_ledFrom);
        if(pLED == NULL) return;
    }
    int data = pLED->m_data;
    m_pGui->setOnLEDData(data, m_ledFrom, m_ledTo);
    PostMessageControl(m_hWnd, WM_GUIPARTS_NOTIFY, m_id, data);
}

//---------------------------------------------------------------------------
// CDisplayArea (value popup shown while dragging a control)

void CDisplayArea::init(CGUICollection* pGui, HDC hdc, CDIB* pDib, COLORREF color)
{
    m_pGui = pGui;
    m_hdc = hdc;
    m_pDib = pDib;
    m_color = 0xffffff;
    m_text[0] = 0;
}

void CDisplayArea::setDisplayLocation(int x, int y)
{
    m_x = x;
    m_y = y;
    m_text[0] = 0;
}

void CDisplayArea::setText(char* text)
{
    if(strcmp(m_text, text) == 0) return;
    strcpy(m_text, text);

    // measure the text
    RECT rc;
    MySetRect(&rc, 0, 0, 0, 0);
    HDC hdc = ((HDC)m_pDib->m_hBmp);
    CGContextSaveGState(hdc);
    CGContextSelectFont(hdc, "Helvetica", 16.0, kCGEncodingMacRoman);
    CGContextSetTextDrawingMode(hdc, kCGTextInvisible);
    CGPoint pt0 = CGContextGetTextPosition(hdc);
    CGContextShowText(hdc, m_text, strlen(m_text));
    CGPoint pt1 = CGContextGetTextPosition(hdc);
    rc.right = (int)(pt1.x - pt0.x);
    rc.bottom = 16;
    CGContextRestoreGState(hdc);
    rc.right += 6;
    rc.bottom += 2;

    RECT rcOld = m_rc;
    MySetRect(&m_rc, m_x - rc.right / 2, m_y + 3, rc.right, rc.bottom);
    if(m_bEnable){
        rc.left = MIN(rcOld.left, m_rc.left);
        rc.top = MIN(rcOld.top, m_rc.top);
        rc.right = MAX(rcOld.right, m_rc.right);
        rc.bottom = MAX(rcOld.bottom, m_rc.bottom);
        InvalidateRectControl(m_pGui->m_hWnd, &rc, TRUE);
    }
}

void CDisplayArea::setEnable(int bEnable)
{
    m_bEnable = bEnable;
}

void CDisplayArea::drawUpdate()
{
    InvalidateRectControl(m_pGui->m_hWnd, &m_rc, TRUE);
}

int CDisplayArea::isEnabled()
{
    return m_bEnable;
}

void CDisplayArea::paint(HDC hdc, RECT* prc, int ox, int oy)
{
    if(!m_bEnable){
        // erase the last popup
        CGImageRef img = CGBitmapContextCreateImage(((HDC)m_pDib->m_hBmp));
        if(img){
            CGImageRef part = CGImageCreateWithImageInRect(img,
                CGRectMake(m_rcPrev.left, CGBitmapContextGetHeight(((HDC)m_pDib->m_hBmp)) - m_rcPrev.bottom,
                    m_rcPrev.right - m_rcPrev.left, m_rcPrev.bottom - m_rcPrev.top));
            if(part){
                CGContextDrawImage(hdc, CGRectMake(m_rcPrev.left, m_rcPrev.top,
                    m_rcPrev.right - m_rcPrev.left, m_rcPrev.bottom - m_rcPrev.top), part);
                CGImageRelease(part);
            }
            CGImageRelease(img);
        }
        if(!m_bEnable) return;
    }

    RECT rcWork;
    MySetRect(&rcWork, 0, 0, 0, 0);
    RECT rc = m_rc;
    int w = rc.right - rc.left;
    int h = rc.bottom - rc.top;
    MySetRect(&rcWork, 0, 0, w, h);
    m_pGui->getTempDIB(rcWork.right, rcWork.bottom);
    CDIB dib;
    dib.create(rcWork.right, rcWork.bottom);
    CGContextScaleCTM(((HDC)dib.m_hBmp), 1.0, 1.0);
    CGContextTranslateCTM(((HDC)dib.m_hBmp), 0, 0);
    // darkened background
    for(int x = rcWork.left; x < rcWork.right; x++){
        for(int y = rcWork.top; y < rcWork.bottom; y++){
            DWORD c = m_pDib->getPixel(x + rc.left, y + rc.top);
            DWORD c2 = 0xff000000 + (((c & 0xff) * 5 >> 3) | (((c >> 8) & 0xff) * 160 / 256 << 8)) + (((c >> 16) & 0xff) * 160 / 256 << 16);
            dib.setPixel(x, y, c2);
        }
    }
    // keep the corners
    DWORD c;
    c = m_pDib->getPixel(rc.left, rc.top);
    dib.setPixel(0, 0, c | 0xff000000);
    c = m_pDib->getPixel(rc.left, rc.bottom - 1);
    dib.setPixel(0, rcWork.bottom - 1, c | 0xff000000);
    c = m_pDib->getPixel(rc.right - 1, rc.top);
    dib.setPixel(rcWork.right - 1, 0, c | 0xff000000);
    c = m_pDib->getPixel(rc.right - 1, rc.bottom - 1);
    dib.setPixel(rcWork.right - 1, rcWork.bottom - 1, c | 0xff000000);

    HDC hdcMem = CreateCompatibleDC(hdc);
    CGContextSaveGState(((HDC)dib.m_hBmp));
    CGContextSelectFont(((HDC)dib.m_hBmp), "Helvetica", 16.0, kCGEncodingMacRoman);
    CGContextSetTextDrawingMode(((HDC)dib.m_hBmp), kCGTextFill);
    CGContextScaleCTM(((HDC)dib.m_hBmp), 1.0, -1.0);
    CGContextTranslateCTM(((HDC)dib.m_hBmp), 3.0, 2 - rcWork.bottom);
    CGContextSetRGBFillColor(((HDC)dib.m_hBmp), GetRValue(m_color) / 255.0, GetGValue(m_color) / 255.0, GetBValue(m_color) / 255.0, 1.0);
    CGContextShowTextAtPoint(((HDC)dib.m_hBmp), 0, 0, m_text, strlen(m_text));
    CGContextRestoreGState(((HDC)dib.m_hBmp));

    if(w < m_rcPrev.right - m_rcPrev.left || h < m_rcPrev.bottom - m_rcPrev.top){
        // the popup shrank: restore the background
        CGImageRef img = CGBitmapContextCreateImage(((HDC)m_pDib->m_hBmp));
        if(img){
            CGRect r = CGRectMake(0, 0, CGBitmapContextGetWidth(((HDC)m_pDib->m_hBmp)),
                CGBitmapContextGetHeight(((HDC)m_pDib->m_hBmp)));
            CGContextDrawImage(hdc, r, img);
            CGImageRelease(img);
        }
    }
    CGImageRef img = CGBitmapContextCreateImage(((HDC)dib.m_hBmp));
    if(img){
        CGRect r = CGRectMake(rc.left + ox, rc.top + oy, rcWork.right, rcWork.bottom);
        CGContextDrawImage(hdc, r, img);
        CGImageRelease(img);
    }
    DeleteDC(hdcMem);
    m_rcPrev = rc;
}

//---------------------------------------------------------------------------
// CButtonGroup

CButtonGroup::CButtonGroup(HWND hWnd, int id)
{
    init(hWnd, id, 0, 0, 0, 0, NULL, 0, 0);
    m_num = 0;
}

void CButtonGroup::setButtonVal(int id, int val)
{
    CToggleButton* pButton = (CToggleButton*)m_pGui->getGui(id);
    if(pButton){
        m_pButtons[m_num] = pButton;
        pButton->m_pGroup = this;
        m_vals[m_num] = val;
        m_num++;
    }
}

void CButtonGroup::setValue(int val)
{
    for(int i = 0; i < m_num; i++){
        CToggleButton* pButton = (CToggleButton*)m_pButtons[i];
        if(m_vals[i] == val && !pButton->m_bOn) pButton->setValue(1);
        if(m_vals[i] != val && pButton->m_bOn) pButton->setValue(0);
    }
}

void CButtonGroup::clickedButton(void* pButton)
{
    if(((CToggleButton*)pButton)->m_bOn) return;
    int i;
    for(i = 0; i < m_num; i++){
        if(m_pButtons[i] == pButton) break;
    }
    int val = m_vals[i];
    setValue(val);
    SendMessageControl(m_hWnd, WM_GUIPARTS_NOTIFY, m_id, val);
}

//---------------------------------------------------------------------------
// CBMPPushButton

CBMPPushButton::CBMPPushButton(HWND hWnd, int id, int x, int y, int w, int h, char* text, int tx, int ty, int tw, int th, COLORREF bkColor, COLORREF textColor)
    : CPushButton(hWnd, id, x, y, w, h, text, bkColor, textColor, 0)
{
    MySetRect(&m_rcText, x + tx, y + ty, tw, th);
}

BOOL CBMPPushButton::drawMe(HDC hdc, CDIB* pDib, int bAll)
{
    DrawPushButton(hdc, &m_rc, m_bPushed, m_bkColor);
    DrawButtonShadow(hdc, &m_rc, m_bkColor, m_bPushed);
    if(bAll){
        DrawLetter(hdc, &m_rcText, m_text, m_bkColor, m_textColor, 0, 0, 0);
    }
    return TRUE;
}

//---------------------------------------------------------------------------
// CToggleButton

CToggleButton::CToggleButton(HWND hWnd, int id, int x, int y, int w, int h, int bOn, char* text, COLORREF bkColor, COLORREF textColor, COLORREF color, COLORREF onColor, int style)
{
    init(hWnd, id, x, y, w, h, text, bkColor, textColor);
    m_color = color;
    m_onColor = onColor;
    m_bOn = bOn;
    m_bDrawnOn = !bOn;
    m_pGroup = NULL;
    m_style = style;
}

static void line(HDC hdc, int x1, int y1, int x2, int y2, COLORREF color);

BOOL CToggleButton::drawMe(HDC hdc, CDIB* pDib, int bAll)
{
    if(m_bDrawnOn == m_bOn) return FALSE;
    if(m_style){
        RECT rc = m_rc;
        CGContextSetRGBStrokeColor(hdc, 0, 0, 0, 1.0);
        Rectangle(hdc, rc.left, rc.top, rc.right, rc.bottom);
        SetPixel(hdc, rc.left, rc.top, m_bkColor);
        SetPixel(hdc, rc.right - 1, rc.top, m_bkColor);
        SetPixel(hdc, rc.left, rc.bottom - 1, m_bkColor);
        SetPixel(hdc, rc.right - 1, rc.bottom - 1, m_bkColor);
        rc.left++;
        rc.top++;
        rc.right--;
        rc.bottom--;
        line(hdc, rc.left, rc.top, rc.right, rc.top, 0xc8c8c8);
        line(hdc, rc.right - 1, rc.top, rc.right - 1, rc.bottom, 0xa0a0a0);
        RECT rc2 = rc;
        rc2.left++;
        rc2.top++;
        rc2.right--;
        rc2.bottom--;
        line(hdc, rc2.right, rc2.bottom, rc2.left - 1, rc2.bottom, 0x969696);
        line(hdc, rc2.left - 1, rc2.bottom, rc2.left - 1, rc2.top - 1, 0xb4b4b4);
        // gradation
        int h = rc2.bottom - (rc2.top - 1);
        rc = rc2;
        for(int y = rc.top, n = 0; y < rc.bottom; y++, n += 160){
            int c = n / h;
            c = (c > 40) ? 200 : c + 160;
            if(m_bOn){
                CGContextSetRGBStrokeColor(hdc, (BYTE)(c + 50) / 255.0, (BYTE)(c + 50) / 255.0, (BYTE)c / 255.0, 1.0);
            }else{
                CGContextSetRGBStrokeColor(hdc, (BYTE)c / 255.0, (BYTE)c / 255.0, (BYTE)c / 255.0, 1.0);
            }
            MoveTo0(hdc, rc.left, y);
            LineTo0(hdc, rc.right, y);
        }
        DrawLetter(hdc, &rc, m_text, m_bkColor, m_textColor, 1, 1, 0);
    }else{
        DrawTextButton(hdc, &m_rc, m_text, m_bOn, 1, m_bOn ? m_onColor : m_color, m_textColor, 0, 0);
        DrawToggleButtonShadow(hdc, &m_rc, m_bkColor, m_bOn);
    }
    m_bDrawnOn = m_bOn;
    return TRUE;
}

/**
 * @brief Strokes a 1 pixel wide line.
 *
 * @param hdc    Graphics context.
 * @param x1     Start x (pixels).
 * @param y1     Start y (pixels).
 * @param x2     End x (pixels).
 * @param y2     End y (pixels).
 * @param color  Line color.
 */
static void line(HDC hdc, int x1, int y1, int x2, int y2, COLORREF color)
{
    CGContextBeginPath(hdc);
    CGContextSetLineWidth(hdc, 1.0);
    CGContextSetRGBStrokeColor(hdc, GetRValue(color) / 255.0, GetGValue(color) / 255.0, GetBValue(color) / 255.0, 1.0);
    CGContextMoveToPoint(hdc, x1, y1);
    CGContextAddLineToPoint(hdc, x2, y2);
    CGContextStrokePath(hdc);
}

void CToggleButton::mouseDown(int x, int y)
{
    if(m_pGroup){
        m_pGroup->clickedButton(this);
        return;
    }
    m_bOn = !m_bOn;
    drawMeOwn();
    SendMessageControl(m_hWnd, WM_GUIPARTS_NOTIFY, m_id, m_bOn);
}

void CToggleButton::setValue(int val)
{
    m_bOn = (val != 0);
    drawMeOwn();
}

//---------------------------------------------------------------------------
// CLEDToggleButton

CLEDToggleButton::CLEDToggleButton(HWND hWnd, int id, int x, int y, int w, int h, int bOn, char* text, COLORREF bkColor, COLORREF textColor, COLORREF color, int ledType, int ledSize)
{
    init(hWnd, id, x, y, w, h, text, bkColor, textColor);
    m_color = color;
    m_bOn = bOn;
    m_bDrawnOn = !bOn;
    m_bPushed = 0;
    m_bDrawnPushed = 1;
    m_ledType = ledType;
    m_ledSize = ledSize;
}

BOOL CLEDToggleButton::drawMe(HDC hdc, CDIB* pDib, int bAll)
{
    if(m_bDrawnOn == m_bOn && m_bDrawnPushed == m_bPushed) return FALSE;
    DrawTextButton(hdc, &m_rc, m_text, m_bPushed, 1, m_color, m_textColor, m_ledSize, 0);
    DrawButtonShadow(hdc, &m_rc, m_bkColor, m_bPushed);
    RECT rc;
    MySetRect(&rc, m_rc.left + 1, (m_rc.top + m_rc.bottom) / 2 - m_ledSize / 2 - 1, m_ledSize, m_ledSize);
    DrawLED(hdc, &rc, m_ledType, m_bOn, m_color);
    m_bDrawnPushed = m_bPushed;
    m_bDrawnOn = m_bOn;
    return TRUE;
}

void CLEDToggleButton::mouseDown(int x, int y)
{
    m_bOn = !m_bOn;
    m_bPushed = 1;
    drawMeOwn();
    PostMessageControl(m_hWnd, WM_GUIPARTS_NOTIFY, m_id, m_bOn);
}

void CLEDToggleButton::mouseUp(int x, int y)
{
    mouseLeave();
}

void CLEDToggleButton::mouseLeave()
{
    if(m_bPushed){
        m_bPushed = 0;
        drawMeOwn();
    }
}

void CLEDToggleButton::setValue(int val)
{
    m_bOn = (val != 0);
    drawMeOwn();
}

void CLEDToggleButton::setCoordinatesScale(int percent)
{
    CGuiParts::setCoordinatesScale(percent);
    m_ledSize = MIN(10, percent * m_ledSize / 100);
}

//---------------------------------------------------------------------------
// CBMPToggleButton

CBMPToggleButton::CBMPToggleButton(HWND hWnd, int id, int x, int y, int w, int h, int bOn, char* text, int tx, int ty, int tw, int th, COLORREF bkColor, COLORREF textColor)
    : CToggleButton(hWnd, id, x, y, w, h, bOn, text, bkColor, textColor, 0, 0, 0)
{
    MySetRect(&m_rcText, x + tx, y + ty, tw, th);
}

BOOL CBMPToggleButton::drawMe(HDC hdc, CDIB* pDib, int bAll)
{
    BOOL bDraw = FALSE;
    if(m_bDrawnOn != m_bOn){
        DrawLEDButton(hdc, &m_rc, m_bOn, m_bkColor);
        DrawToggleButtonShadow(hdc, &m_rc, m_bkColor, m_bOn);
        m_bDrawnOn = m_bOn;
        bDraw = TRUE;
    }
    if(bAll){
        DrawLetter(hdc, &m_rcText, m_text, m_bkColor, m_textColor, 0, 0, 0);
    }
    return bDraw;
}

//---------------------------------------------------------------------------
// CLED

CLED::CLED(HWND hWnd, int id, int data, int x, int y, int w, int h, int type, int bOn, char* text, int bmpId, int tx, int ty, int tw, int th, COLORREF bkColor, COLORREF textColor)
{
    init(hWnd, id, x, y, w, h, text, bkColor, textColor);
    m_bOn = bOn;
    m_bDrawnOn = !bOn;
    m_data = data;
    m_type = type;
    m_bmpId = bmpId;
    m_pParent = NULL;
    MySetRect(&m_rcText, x + tx, y + ty, tw, th);
    if(text && m_rc.right > m_rc.left) m_textW = m_rcText.left - m_rc.right + strlen(text) * th * 3 / 5;
    else m_textW = 0;
}

CLED::CLED(HWND hWnd, int id, int data, int x, int y, int w, int h, int type, int bOn, char* text, int bmpId, int tx, int ty, int tw, int th, COLORREF bkColor, COLORREF textColor, int textLen)
{
    init(hWnd, id, x, y, w, h, text, bkColor, textColor);
    m_bOn = bOn;
    m_bDrawnOn = !bOn;
    m_data = data;
    m_type = type;
    m_bmpId = bmpId;
    m_pParent = NULL;
    MySetRect(&m_rcText, x + tx, y + ty, tw, th);
    if(m_rc.right > m_rc.left) m_textW = th * textLen * 3 / 5;
    else m_textW = 0;
}

BOOL CLED::drawMe(HDC hdc, CDIB* pDib, int bAll)
{
    BOOL ret = FALSE;
    if(m_bDrawnOn != m_bOn){
        if(m_rc.right > m_rc.left){
            DrawLED2(hdc, &m_rc, m_type, m_bOn, m_bkColor, 0);
        }
        m_bDrawnOn = m_bOn;
        ret = TRUE;
    }
    if(bAll){
        if(m_bmpId){
            DrawTransparentBmp(pDib, &m_rcText, m_bmpId, m_bkColor, m_textColor);
        }else{
            DrawLetter(hdc, &m_rcText, m_text, m_bkColor, m_textColor, 0, 0, 0);
        }
    }
    return ret;
}

void CLED::setCoordinatesScale(int percent)
{
    CGuiParts::setCoordinatesScale(percent);
    m_textW = percent * m_textW / 100;
}

// the label next to the LED is clickable too
BOOL CLED::PtInRt(int x, int y)
{
    RECT rc = m_rc;
    rc.bottom = m_rcText.bottom;
    if(m_textW > 0) rc.right += m_textW;
    else rc.left += m_textW;
    POINT pt;
    pt.x = x;
    pt.y = y;
    return PtInRect0(&rc, pt);
}

void CLED::mouseDown(int x, int y)
{
    if(m_pParent) m_pParent->notifyClicked(m_id);
}

void CLED::setState(int bOn)
{
    m_bOn = bOn;
    drawMeOwn();
}

void CLED::setParentGui(CGuiParts* pParent)
{
    m_pParent = pParent;
}

//---------------------------------------------------------------------------
// CLabel

CLabel::CLabel(HWND hWnd, int id, int x, int y, int w, int h, char* text, COLORREF bkColor, COLORREF textColor, int style)
{
    init(hWnd, id, x, y, w, h, text, bkColor, textColor);
    m_style = style;
}

BOOL CLabel::drawMe(HDC hdc, CDIB* pDib, int bAll)
{
    CGContextSetRGBFillColor(hdc, GetRValue(m_bkColor) / 255.0, GetGValue(m_bkColor) / 255.0, GetBValue(m_bkColor) / 255.0, 1.0);
    CGContextFillRect(hdc, CGRectMake(m_rc.left - 1, m_rc.top, m_rc.right - m_rc.left, m_rc.bottom - m_rc.top));
    RECT rc = m_rc;
    rc.top++;
    DrawLetter(hdc, &rc, m_text, m_bkColor, m_textColor, 0, 0, m_style);
    return TRUE;
}

void CLabel::setText(char* text)
{
    char str[256];
    if(text){
        strcpy(str, text);
        str[49] = 0;
        strcpy(m_text, str);
    }
    drawMeOwn();
}

//---------------------------------------------------------------------------
// CLedMeter

CLedMeter::CLedMeter(HWND hWnd, int id, int x, int y, int w, int h, char* text, COLORREF bkColor, COLORREF textColor, int num)
{
    init(hWnd, id, x, y, w, h, text, bkColor, textColor);
    m_num = num;
    m_value = 0;
}

void CLedMeter::setValue(int val)
{
    m_value = val;
    drawMeOwn();
}

BOOL CLedMeter::drawMe(HDC hdc, CDIB* pDib, int bAll)
{
    RECT rc = m_rc;
    int i;
    if(bAll){
        for(i = 0; i < m_num; i++){
            rc.left = m_rc.left + (m_rc.right - m_rc.left) * i / m_num;
            rc.right = rc.left + 3;
            DrawFrame(hdc, &rc, m_bkColor, 1);
        }
    }
    CGContextSetRGBFillColor(hdc, 1.0, 64 / 255.0, 64 / 255.0, 1.0);
    for(i = 0; i < m_value; i++){
        rc.left = m_rc.left + (m_rc.right - m_rc.left) * i / m_num;
        rc.right = rc.left + 3;
        CGContextFillRect(hdc, CGRectMake(rc.left, rc.top, rc.right - rc.left + 1, rc.bottom - rc.top + 1));
    }
    CGContextSetRGBFillColor(hdc, 96 / 255.0, 96 / 255.0, 96 / 255.0, 1.0);
    for(; i < m_num; i++){
        rc.left = m_rc.left + (m_rc.right - m_rc.left) * i / m_num;
        rc.right = rc.left + 3;
        CGContextFillRect(hdc, CGRectMake(rc.left, rc.top, rc.right - rc.left + 1, rc.bottom - rc.top + 1));
    }
    return TRUE;
}

//---------------------------------------------------------------------------
// C3DFrame

C3DFrame::C3DFrame(HWND hWnd, int id, int x, int y, int w, int h, COLORREF color)
{
    init(hWnd, id, x, y, w, h, NULL, color, 0);
}

BOOL C3DFrame::drawMe(HDC hdc, CDIB* pDib, int bAll)
{
    if(bAll) DrawFrame(hdc, &m_rc, m_bkColor, 1);
    return FALSE;
}

//---------------------------------------------------------------------------
// CLine

CLine::CLine(HWND hWnd, int id, int x, int y, int w, int h, COLORREF color)
{
    init(hWnd, id, x, y, w, h, NULL, 0, color);
}

void CLine::setCoordinatesScale(int percent)
{
    m_rc.left = m_rc.left * percent / 100;
    m_rc.top = m_rc.top * percent / 100;
    m_rc.right = m_rc.right * percent / 100;
    m_rc.bottom = m_rc.bottom * percent / 100;
    int w = (m_rcText.right - m_rcText.left) * percent / 100;
    int h = (m_rcText.bottom - m_rcText.top) * percent / 100;
    m_rcText.left = m_rcText.left * percent / 100;
    m_rcText.top = m_rcText.top * percent / 100;
    m_rcText.right = m_rcText.left + w;
    m_rcText.bottom = m_rcText.top + h;
}

BOOL CLine::drawMe(HDC hdc, CDIB* pDib, int bAll)
{
    if(bAll){
        CGContextBeginPath(hdc);
        CGContextSetLineWidth(hdc, 1.0);
        CGContextSetRGBStrokeColor(hdc, GetRValue(m_textColor) / 255.0, GetGValue(m_textColor) / 255.0, GetBValue(m_textColor) / 255.0, 1.0);
        MoveTo0(hdc, m_rc.left, m_rc.top);
        LineTo0(hdc, m_rc.right, m_rc.bottom);
        MoveTo0(hdc, m_rc.right, m_rc.bottom);
        LineTo0(hdc, m_rc.left, m_rc.top);
        CGContextStrokePath(hdc);
    }
    return FALSE;
}

//---------------------------------------------------------------------------
// CLCD

CLCD::CLCD(HWND hWnd, int id, int x, int y, int w, int h, char* text, COLORREF bkColor, COLORREF textColor, int clickMode, int type)
    : CLabel(hWnd, id, x, y, w, h, text, bkColor, textColor, 0)
{
    m_bInitBg = 1;
    m_clickMode = clickMode;
    m_type = type;
}

BOOL CLCD::drawMe(HDC hdc, CDIB* pDib, int bAll)
{
    char str[32];
    RECT rc;
    if(m_bInitBg){
        // background with a soft glow
        m_dibBg.create(m_rc.right - m_rc.left, m_rc.bottom - m_rc.top);
        int cx = (m_rc.right - m_rc.left) / 2;
        int cy = (m_rc.bottom - m_rc.top) / 2;
        for(int x = 0; x < m_rc.right - m_rc.left; x++){
            int dx2 = (cx - x) * (cx - x) + 100;
            for(int y = 0; y < m_rc.bottom - m_rc.top; y++){
                int dy = cy - y;
                double d = sqrt((double)(dy * dy + dx2));
                int percent = (int)(fpow(0.97, d) * 60.0);
                m_dibBg.setPixel(x, y, 0xff000000 + RGB(GetRValue(CalcColor(m_textColor, percent)), GetGValue(CalcColor(m_textColor, percent)), GetBValue(CalcColor(m_textColor, percent))));
            }
        }
        HDC hdcMem = CreateCompatibleDC(hdc);
        MySetRect(&rc, 0, 0, m_rc.right - m_rc.left, m_rc.bottom - m_rc.top);
        DrawFrame((HDC)m_dibBg.m_hBmp, &m_rc, m_bkColor, 1);
        DeleteDC(hdcMem);
    }
    if(!m_bInitBg){
        if(m_type == 0){
            if(strcmp(m_text, m_lastText) == 0) return FALSE;
        }else if(m_type == 1){
            if(m_value == m_lastValue) return FALSE;
        }else if(m_type == 2 || m_type == 3){
            if(m_index == m_lastIndex) return FALSE;
        }
    }
    m_bInitBg = 0;

    int w = m_rc.right - m_rc.left;
    int h = m_rc.bottom - m_rc.top;
    CDIB* pTmp = m_pGui->getTempDIB(w, h);
    pTmp->bitBlt(0, 0, w, h, &m_dibBg, 0, 0);
    MySetRect(&rc, 0, 0, w, h);
    RightDownRect(&rc, 1, 1);
    rc.bottom -= 2;
    rc.right -= 2;
    switch(m_type){
    case 0:
        DrawLetterDIB(pTmp, &rc, m_text, m_bkColor, m_textColor, 0, 1, m_style);
        strcpy(m_lastText, m_text);
        break;
    case 1:
        sprintf(str, m_format, m_value);
        DrawLetterDIB(pTmp, &rc, str, m_bkColor, m_textColor, 0, 1, m_style);
        m_lastValue = m_value;
        break;
    case 2:
    case 3:
        if(m_type == 2){
            DrawLetterDIB(pTmp, &rc, m_texts[m_index], m_bkColor, m_textColor, 0, 1, m_style);
        }else{
            RightDownRect(&rc, 1, 2);
            int right = rc.right - 3;
            int bottom = rc.bottom - 3;
            rc.right = right - (right - rc.left) % 2;
            rc.bottom = bottom - (bottom - rc.top) % 2;
            DrawTransparentBmpTrans(pTmp, &rc, m_bmps[m_index], m_textColor);
        }
        m_lastIndex = m_index;
        break;
    }
    pDib->bitBlt(m_rc.left, m_rc.top, w, h, pTmp, 0, 0);
    return TRUE;
}

void CLCD::setVol(int vol)
{
    setValue(vol);
    if(m_type == 2 || m_type == 3){
        SendMessageControl(m_hWnd, WM_GUIPARTS_NOTIFY, m_id, m_vals[m_index]);
    }else if(m_type == 1){
        SendMessageControl(m_hWnd, WM_GUIPARTS_NOTIFY, m_id, m_value);
    }
}

void CLCD::mouseDown(int x, int y)
{
    switch(m_clickMode){
    case 0:
        SendMessageControl(m_hWnd, WM_GUIPARTS_NOTIFY, m_id, MAKELONG(x, y));
        break;
    case 1:
        if(m_type == 1){
            if(m_pGui->m_sliderType != 1){
                m_pGui->startDragInputPart(this, x, y, m_min, m_max, m_value, FALSE);
                return;
            }
            // slider popup
            g_VolSubWin.init(m_min, m_max, m_value, m_id);
            m_pGui->m_pCapture = &g_VolSubWin;
            RECT rc;
            int top = m_rc.top;
            int px = m_rc.right;
            int py = top - 60;
            GetClientRectControl(m_hWnd, &rc);
            if(px < 0) px = 0;
            if(py < 0) py = 0;
            if(px + 18 > rc.right) px = rc.right - 18;
            if(py + 150 > rc.bottom) py = rc.bottom - 150;
            g_VolSubWin.create(m_hWnd, px, py, 18, 150, this);
        }
        break;
    case 2:
        {
            // list popup
            int cols = m_num / m_rows + (m_num % m_rows != 0);
            g_LCDSubWin.init(this, 2, m_rows);
            m_pGui->m_pCapture = &g_LCDSubWin;
            float scale = sqrtf(m_pGui->m_scale);
            float itemW, itemH;
            if(m_type == 2){
                float size = scale * m_pGui->m_fontSize - 1.0f;
                itemW = size * 0.5f * m_maxLen * 1.1f;
                g_LCDSubWin.setFontSize(size);
                itemH = size + 1.0f;
            }else{
                itemW = (m_rc.right - m_rc.left) * scale;
                itemH = scale * (m_rc.bottom - m_rc.top);
            }
            int ih = (int)itemH;
            int w = cols * (int)itemW + 7;
            int h = m_rows * ih + 7;
            RECT rc;
            int px = m_rc.right;
            int py = (m_rc.top + m_rc.bottom) / 2 - h / 2;
            GetClientRectControl(m_hWnd, &rc);
            if(px < 0) px = 0;
            if(py < 0) py = 0;
            if(px + w > rc.right) px = rc.right - w;
            if(py + h > rc.bottom) py = rc.bottom - h;
            g_LCDSubWin.create(m_hWnd, px, py, w, h, (void*)(long)h);
        }
        break;
    }
}

//---------------------------------------------------------------------------

void CLCD::setValsAndTexts(int* vals, char** texts, int num, int rows)
{
    m_vals = vals;
    m_texts = texts;
    m_num = num;
    m_index = 0;
    m_rows = rows;
    m_maxLen = 0;
    for(int i = 0; i < num; i++){
        int len = strlen(texts[i]);
        if(m_maxLen < len) m_maxLen = len;
    }
    m_maxLen++;
}

void CLCD::setValsAndBmp(int* vals, int* bmps, int num, int rows)
{
    m_vals = vals;
    m_bmps = bmps;
    m_num = num;
    m_index = 0;
    m_rows = rows;
}

void CLCD::setNumericRange(int min, int max, int val, char* format)
{
    m_min = min;
    m_max = max;
    m_value = val;
    strcpy(m_format, format);
}

void CLCD::setValue(int val)
{
    if(m_type == 2 || m_type == 3){
        int old = m_index;
        for(int i = 0; i < m_num; i++){
            if(m_vals[i] == val){
                m_index = i;
                if(old != i) drawMeOwn();
                return;
            }
        }
    }else if(m_type == 1){
        m_value = MIN(m_max, MAX(m_min, val));
        drawMeOwn();
    }
}

void CLCD::incIndex()
{
    if(m_type == 2 || m_type == 3){
        m_index = (m_index + 1 < m_num) ? m_index + 1 : 0;
        drawMeOwn();
        SendMessageControl(m_hWnd, WM_GUIPARTS_NOTIFY, m_id, m_vals[m_index]);
    }else if(m_type == 1){
        int old = m_value;
        m_value = MIN(m_max, MAX(m_min, old + 1));
        if(m_value != old){
            drawMeOwn();
            SendMessageControl(m_hWnd, WM_GUIPARTS_NOTIFY, m_id, m_value);
        }
    }
}

void CLCD::decIndex()
{
    if(m_type == 2 || m_type == 3){
        if(m_index-- <= 0) m_index = m_num - 1;
        drawMeOwn();
        SendMessageControl(m_hWnd, WM_GUIPARTS_NOTIFY, m_id, m_vals[m_index]);
    }else if(m_type == 1){
        int old = m_value;
        m_value = MIN(m_max, MAX(m_min, old - 1));
        if(m_value != old){
            drawMeOwn();
            SendMessageControl(m_hWnd, WM_GUIPARTS_NOTIFY, m_id, m_value);
        }
    }
}

//---------------------------------------------------------------------------
// CLCDSubWin

CLCDSubWin::CLCDSubWin()
{
    m_pLCD = NULL;
    m_index = 0;
    m_rows = 0;
    m_marginX = 0;
    m_marginY = 0;
    m_fontSize = 16.0f;
    m_alpha = 1.0f;
}

void CLCDSubWin::init(CLCD* pLCD, int margin, int rows)
{
    m_pLCD = pLCD;
    m_index = pLCD->m_index;
    m_rows = rows;
    m_marginX = margin;
    m_marginY = margin;
    m_fontSize = 16.0f;
    m_alpha = g_bTransparencyWin ? 0.75f : 1.0f;
}

void CLCDSubWin::setFontSize(float size)
{
    m_fontSize = size;
}

void CLCDSubWin::create(HWND hWnd, int x, int y, int w, int h, void* lpParam)
{
    CGuiParts::init(hWnd, 0, x, y, w, h, NULL, 0, RGB(0, 0xd2, 0xb9));
    InvalidateRectControl(hWnd, &m_rc, TRUE);
    RECT rc = m_rc;
    m_cols = m_pLCD->m_num / m_rows + (m_pLCD->m_num % m_rows != 0);
    m_itemW = (rc.right - rc.left - 1 - m_marginX * 2) / m_cols;
    m_itemH = (rc.bottom - rc.top - 1 - m_marginY * 2) / m_rows;
    getItemRect(m_index, &rc);
    POINT pt;
    pt.x = (rc.left + rc.right) / 2;
    pt.y = (rc.top + rc.bottom) / 2;
    ClientToScreenControl(hWnd, &pt);
    SetCursorPos(pt.x, pt.y);
}

void CLCDSubWin::getItemRect(int index, RECT* rc)
{
    int col = index / m_rows;
    int row = index % m_rows;
    rc->left = m_itemW * col + m_marginX + m_rc.left;
    rc->top = m_itemH * row + m_marginY + m_rc.top;
    rc->right = (col + 1) * m_itemW + m_marginX + m_rc.left;
    rc->bottom = (row + 1) * m_itemH + m_marginY + m_rc.top;
}

CLCDSubWin::~CLCDSubWin()
{
}

void CLCDSubWin::createFont()
{
}

void CLCDSubWin::paint(HDC hdc)
{
    for(int i = 0; i < m_pLCD->m_num; i++){
        drawItemBox(hdc, i, i == m_index);
    }
}

void CLCDSubWin::drawItemBox(HDC hdc, int index, int bSelected)
{
    RECT rc;
    getItemRect(index, &rc);
    if(bSelected){
        CGContextSetRGBFillColor(hdc, 200 / 255.0, 200 / 255.0, 200 / 255.0, 1.0);
    }else{
        CGContextSetRGBFillColor(hdc, 1.0, 1.0, 1.0, 1.0);
    }
    RECT rcIn;
    rcIn.left = rc.left + 1;
    rcIn.top = rc.top + 1;
    rcIn.right = rc.right - 1;
    rcIn.bottom = rc.bottom - 1;
    CGContextFillRect(hdc, CGRectMake(rcIn.left, rcIn.top, rc.right - rc.left - 1, rc.bottom - rc.top - 1));
    CGContextBeginPath(hdc);
    CGContextSetLineWidth(hdc, 0.5);
    CGContextSetRGBFillColor(hdc, 0, 0, 0, 1.0);
    CGContextStrokeRect(hdc, CGRectMake(rc.left + 0.5, rc.top + 0.5, rc.right - rc.left - 1.0, rc.bottom - rc.top - 1.0));
    CGContextStrokePath(hdc);
    rc = rcIn;

    if(m_pLCD->m_type == 2){
        char* text = m_pLCD->m_texts[index];
        int cur = m_pLCD->m_index;
        CGContextSaveGState(hdc);
        CGContextSelectFont(hdc, "Helvetica", 16.0, kCGEncodingMacRoman);
        CGContextSetTextDrawingMode(hdc, kCGTextFill);
        CGContextScaleCTM(hdc, 1.0, -1.0);
        CGContextTranslateCTM(hdc, rc.left, 2.0f - rc.bottom);
        CGContextSetRGBFillColor(hdc, 0, 0, (cur == index) ? 1.0 : 0, 1.0);
        CGContextShowTextAtPoint(hdc, 0, 0, text, strlen(text));
        CGContextRestoreGState(hdc);
    }else if(m_pLCD->m_type == 3){
        RightDownRect(&rc, 2, 2);
        int right = rc.right - 4;
        int bottom = rc.bottom - 4;
        rc.right = right - (right - rc.left) % 2;
        rc.bottom = bottom - (bottom - rc.top) % 2;
        COLORREF bkColor = bSelected ? 0xc8c8c8 : 0xffffff;
        COLORREF color = (m_pLCD->m_index == index) ? 0xff0000 : 0;
        int w = rc.right - rc.left;
        int h = rc.bottom - rc.top;
        CDIB* pDib = m_pLCD->m_pGui->getTempDIB(w, h);
        RECT rcBmp;
        MySetRect(&rcBmp, 0, 0, w, h);
        DrawTransparentBmp(pDib, &rcBmp, m_pLCD->m_bmps[index], bkColor, color);
        HDC hdcMem = CreateCompatibleDC(hdc);
        BitBlt(hdc, rc.left, rc.top, w, h, (HDC)pDib->m_hBmp, 0, pDib->m_height - h, 0xcc0020);
        DeleteDC(hdcMem);
    }
}

int CLCDSubWin::getItemFromPt(POINT* pt)
{
    int x = pt->x - m_rc.left - m_marginX;
    if(x < 0) return -1;
    int y = pt->y - m_rc.top - m_marginY;
    if(y < 0) return -1;
    if(x >= m_cols * m_itemW) return -1;
    if(y >= m_itemH * m_rows) return -1;
    return x / m_itemW * m_rows + y / m_itemH;
}

BOOL CLCDSubWin::drawMe(HDC hdc, CDIB* pDib, int bAll)
{
    CGContextSaveGState(hdc);
    CGContextSetAlpha(hdc, m_alpha);
    paint(hdc);
    CGContextRestoreGState(hdc);
    return TRUE;
}

void CLCDSubWin::mouseUp(int x, int y)
{
}

void CLCDSubWin::mouseDown(int x, int y)
{
    RECT rc = m_rc;
    POINT pt;
    pt.x = x;
    pt.y = y;
    if(PtInRect0(&rc, pt)){
        if(m_index != -1){
            m_pLCD->setVol(m_pLCD->m_vals[m_index]);
        }
        // back to the LCD
        POINT ptLCD;
        ptLCD.x = (m_pLCD->m_rc.left + m_pLCD->m_rc.right) / 2;
        ptLCD.y = (m_pLCD->m_rc.top + m_pLCD->m_rc.bottom) / 2;
        ClientToScreenControl(m_pLCD->m_hWnd, &ptLCD);
        SetCursorPos(ptLCD.x, ptLCD.y);
        InvalidateRectControl(m_hWnd, &m_rc, TRUE);
        m_pLCD->m_pGui->m_pCapture = NULL;
    }
}

void CLCDSubWin::mouseMove(int x, int y)
{
    POINT pt;
    pt.x = x;
    pt.y = y;
    int index = getItemFromPt(&pt);
    if(m_index != index){
        m_index = (index < m_pLCD->m_num) ? index : -1;
        InvalidateRectControl(m_hWnd, &m_rc, TRUE);
    }
}

void CLCDSubWin::mouseLeave()
{
}

//---------------------------------------------------------------------------
// CSpin

CSpin::CSpin(HWND hWnd, int id, int x, int y, int w, int h, char* text, int bVertical, int tx, int ty, int tw, int th, COLORREF bkColor, COLORREF textColor, COLORREF color)
{
    init(hWnd, id, x, y, w, h, text, bkColor, textColor);
    MySetRect(&m_rcText, x + tx, y + ty, tw, th);
    m_pushed = 0;
    m_color = color;
    m_pLCD = NULL;
    m_bVertical = bVertical;
}

BOOL CSpin::drawMe(HDC hdc, CDIB* pDib, int bAll)
{
    RECT rcDown = m_rc;
    RECT rcUp = m_rc;
    if(m_bVertical == 0){
        int mid = (m_rc.left + m_rc.right) / 2;
        rcUp.left = mid;
        rcDown.right = mid;
    }else{
        int mid = (m_rc.top + m_rc.bottom) / 2;
        rcUp.bottom = mid;
        rcDown.top = mid;
    }
    DrawButtonShadow(hdc, &rcUp, m_bkColor, m_pushed == 1);
    DrawButtonShadow(hdc, &rcDown, m_bkColor, m_pushed == 2);
    DrawTextButton(hdc, &rcUp, NULL, m_pushed == 1, 1, m_color, m_textColor, 0, 0);
    DrawTextButton(hdc, &rcDown, NULL, m_pushed == 2, 1, m_color, m_textColor, 0, 0);
    if(bAll){
        DrawLetter(hdc, &m_rcText, m_text, m_bkColor, m_textColor, 0, 0, 0);
    }
    return TRUE;
}

void CSpin::mouseLeave()
{
    if(m_pushed){
        m_pushed = 0;
        drawMeOwn();
    }
}

void CSpin::mouseUp(int x, int y)
{
    mouseLeave();
}

void CSpin::mouseDown(int x, int y)
{
    int old = m_value;
    int bUp, delta, pushed;
    if(m_bVertical == 0){
        if(x > (m_rc.left + m_rc.right) / 2){
            bUp = 1;
            if(m_pLCD) m_pLCD->incIndex();
            delta = 1;
            pushed = 1;
        }else{
            pushed = 2;
            delta = -1;
            bUp = 0;
        }
    }else{
        bUp = (y < (m_rc.top + m_rc.bottom) / 2);
        delta = bUp ? 1 : -1;
        pushed = 2 - bUp;
    }
    m_value += delta;
    m_pushed = pushed;
    drawMeOwn();
    if(m_pLCD == NULL){
        m_value = MIN(m_max, MAX(m_value, m_min));
        if(m_value != old){
            SendMessageControl(m_hWnd, WM_GUIPARTS_NOTIFY, m_id, m_value);
        }
        return;
    }
    if(bUp) m_pLCD->incIndex();
    else m_pLCD->decIndex();
}

void CSpin::setLCD(CLCD* pLCD)
{
    m_pLCD = pLCD;
}

void CSpin::setRange(int min, int max, int val)
{
    m_max = max;
    m_min = min;
    m_value = val;
}

void CSpin::setValue(int val)
{
    m_value = val;
    if(m_pLCD) m_pLCD->setValue(val);
}

//---------------------------------------------------------------------------
// CPatternMatrix

CPatternMatrix::CPatternMatrix(HWND hWnd, int id, int x, int y, int w, int h, int cols, int rows, char** texts, char* text, int tx, int ty, int tw, int th, COLORREF bkColor, COLORREF textColor)
{
    init(hWnd, id, x, y, w, h, text, bkColor, textColor);
    m_texts = texts;
    MySetRect(&m_rcText, x + tx, y + ty, tw, th);
    m_cols = cols;
    m_rows = rows;
    for(int i = 0; i < m_cols * m_rows; i++){
        m_states[i] = 0;
    }
}

BOOL CPatternMatrix::drawMe(HDC hdc, CDIB* pDib, int bAll)
{
    for(int i = 0; i < m_cols; i++){
        for(int j = 0; j < m_rows; j++){
            RECT rc;
            rc.left = (m_rc.right - m_rc.left) * i / m_cols + m_rc.left;
            rc.right = (m_rc.right - m_rc.left) * (i + 1) / m_cols + m_rc.left;
            rc.top = (m_rc.bottom - m_rc.top) * j / m_rows + m_rc.top;
            rc.bottom = (m_rc.bottom - m_rc.top) * (j + 1) / m_rows + m_rc.top;
            DrawTextButton(hdc, &rc, m_texts[m_cols * j + i], m_states[m_cols * j + i], 1, m_bkColor, m_textColor, 0, 0);
            DrawButtonShadow(hdc, &rc, m_bkColor, 1);
        }
    }
    return TRUE;
}

//---------------------------------------------------------------------------
// CVolSubWin

CVolSubWin::CVolSubWin()
{
    m_max = 0;
    m_min = 0;
    m_value = 0;
    m_startValue = 0;
    m_targetId = 0;
    m_alpha = 1.0f;
}

void CVolSubWin::init(int min, int max, int val, int id)
{
    m_min = min;
    m_max = max;
    m_startValue = val;
    m_value = val;
    m_targetId = id;
    m_alpha = g_bTransparencyWin ? 0.75f : 1.0f;
}

void CVolSubWin::create(HWND hWnd, int x, int y, int w, int h, void* lpParam)
{
    CGuiParts::init(hWnd, m_targetId, x, y, w, h, NULL, 0, RGB(0, 0xd2, 0xb9));
    InvalidateRectControl(hWnd, &m_rc, TRUE);
    m_bFirst = 1;
    m_pLCD = (CLCD*)lpParam;
    // put the cursor on the knob
    POINT pt;
    pt.x = m_rc.right - 2;
    pt.y = (m_value - m_max) * (m_rc.top - m_rc.bottom) / (m_max - m_min) + m_rc.top;
    ClientToScreenControl(m_hWnd, &pt);
    SetCursorPos(pt.x, pt.y);
    m_bDragging = 1;
}

void CVolSubWin::paint(HDC hdc)
{
    char str[256];
    int left = m_rc.left;
    int top = m_rc.top;
    int right = m_rc.right;
    int bottom = m_rc.bottom;
    int bar = (m_value - m_min) * (bottom - top) / (m_max - m_min);
    CGContextSetRGBFillColor(hdc, 200 / 255.0, 200 / 255.0, 200 / 255.0, 1.0);
    CGContextFillRect(hdc, CGRectMake(left, bottom - bar, right - left, bar));
    int h = bottom - top;
    int rest = h - bar;
    CGContextSetRGBFillColor(hdc, 60 / 255.0, 60 / 255.0, 60 / 255.0, 1.0);
    CGContextFillRect(hdc, CGRectMake(left, top, right - left, rest));
    SetTextColor(hdc, 0);
    if(CSynthWin::convertVal(str, m_targetId, m_value, m_pLCD->m_pGui->m_pSynth)){
        int len = strlen(str);
        CGContextSaveGState(hdc);
        CGContextSelectFont(hdc, "Helvetica", 16.0, kCGEncodingMacRoman);
        CGContextSetTextDrawingMode(hdc, kCGTextFill);
        CGContextScaleCTM(hdc, 1.0, -1.0);
        CGContextTranslateCTM(hdc, (right - left) * 0.5 + left, -(h * 0.5 + top));
        CGContextRotateCTM(hdc, 4.71225);
        CGContextSetRGBFillColor(hdc, 0, 0, 0, 1.0);
        double x = 8.0 - h * 0.5;
        CGContextShowTextAtPoint(hdc, x + 1.0, -5.0, str, len);
        CGContextSetRGBFillColor(hdc, 0, 210 / 255.0, 185 / 255.0, 1.0);
        CGContextShowTextAtPoint(hdc, x, -6.0, str, len);
        CGContextRestoreGState(hdc);
    }else{
        sprintf(str, "%d", m_value);
        int len = strlen(str);
        CGContextSaveGState(hdc);
        CGContextSetTextDrawingMode(hdc, kCGTextInvisible);
        CGContextSelectFont(hdc, "Helvetica", 16.0, kCGEncodingMacRoman);
        CGPoint pt0 = CGContextGetTextPosition(hdc);
        CGContextShowText(hdc, str, len);
        CGPoint pt1 = CGContextGetTextPosition(hdc);
        float textW = pt1.x - pt0.x;
        CGContextSetTextDrawingMode(hdc, kCGTextFill);
        CGContextScaleCTM(hdc, 1.0, -1.0);
        CGContextTranslateCTM(hdc, (right - left) * 0.5 + left, -(h * 0.5 + top));
        CGContextRotateCTM(hdc, 4.71225);
        CGContextSetRGBFillColor(hdc, 0, 0, 0, 1.0);
        double x = -textW * 0.5;
        CGContextShowTextAtPoint(hdc, x + 1.0, -5.0, str, len);
        CGContextSetRGBFillColor(hdc, 0, 210 / 255.0, 185 / 255.0, 1.0);
        CGContextShowTextAtPoint(hdc, x, -6.0, str, len);
        CGContextRestoreGState(hdc);
    }
    CGContextBeginPath(hdc);
    CGContextSetLineWidth(hdc, 0.5);
    CGContextSetRGBFillColor(hdc, 0, 0, 0, 1.0);
    CGContextStrokeRect(hdc, CGRectMake(left + 0.5, top + 0.5, (right - left) - 1.0, h - 1.0));
    CGContextStrokePath(hdc);
}

BOOL CVolSubWin::drawMe(HDC hdc, CDIB* pDib, int bAll)
{
    if(m_bFirst) m_bFirst = 0;
    CGContextSaveGState(hdc);
    CGContextSetAlpha(hdc, m_alpha);
    paint(hdc);
    CGContextRestoreGState(hdc);
    return TRUE;
}

void CVolSubWin::mouseUp(int x, int y)
{
    m_bDragging = 0;
}

void CVolSubWin::mouseDown(int x, int y)
{
    RECT rc = m_rc;
    POINT pt;
    pt.x = x;
    pt.y = y;
    if(PtInRect0(&rc, pt)){
        m_bDragging = 1;
        mouseMove(x, y);
    }else{
        m_bDragging = 0;
    }
}

void CVolSubWin::mouseMove(int x, int y)
{
    if(m_bFirst){
        // ignore the move caused by SetCursorPos()
        m_bFirst = 0;
        return;
    }
    if(m_bDragging){
        int bottom = m_rc.bottom, top = m_rc.top;
        m_value = MIN(m_max, m_min + MAX((bottom - top - (short)(y - top)) * (m_max - m_min) / (bottom - top), 0));
        if(m_value != m_startValue){
            InvalidateRectControl(m_hWnd, &m_rc, TRUE);
            m_pLCD->setVol(m_value);
            m_startValue = m_value;
        }
    }
}

void CVolSubWin::mouseLeave()
{
}

void CVolSubWin::mouseWheel(int delta)
{
    m_value = MIN(m_max, MAX(m_min, (delta >> 16) / 120 + m_value));
    InvalidateRectControl(m_hWnd, &m_rc, TRUE);
    m_pLCD->setVol(m_value);
}

//---------------------------------------------------------------------------
// CRoteryV

CRoteryV::CRoteryV(HWND hWnd, int id, int x, int y, int w, int h, int max, int val, char* text, int tx, int ty, int tw, int th, COLORREF bkColor, COLORREF textColor, COLORREF color)
{
    init(hWnd, id, x, y, w, h, text, bkColor, textColor);
    MySetRect(&m_rcText, x + tx, y + ty, tw, th);
    m_knobColor = color;
    m_max = max;
    m_value = val;
    m_bInit = 1;
    m_pBg = NULL;
    m_lastValue = 0;
    m_sliderPlace = 0;
}

// the knob is drawn at double size and shrunk
BOOL CRoteryV::drawMe(HDC hdc, CDIB* pDib, int bAll)
{
    RECT rc;
    if(m_bInit){
        // share the background with a knob of the same size
        for(int i = 0; i < m_pGui->m_num; i++){
            CDIB* pBg = (CDIB*)m_pGui->m_pParts[i]->m_pUser;
            if(pBg && pBg->m_width == (m_rc.right - m_rc.left) * 2 + 8
                && pBg->m_height == (m_rc.bottom - m_rc.top) * 2 + 8){
                m_pBg = pBg;
                break;
            }
        }
        if(m_pBg == NULL){
            CDIB* pBg = new CDIB;
            m_pUser = pBg;
            m_pBg = pBg;
            pBg->create((m_rc.right - m_rc.left) * 2 + 8, (m_rc.bottom - m_rc.top) * 2 + 8);
            HDC hdcMem = CreateCompatibleDC(hdc);
            CGContextSetRGBFillColor((HDC)m_pBg->m_hBmp, GetRValue(m_bkColor) / 255.0, GetGValue(m_bkColor) / 255.0, GetBValue(m_bkColor) / 255.0, 1.0);
            CGContextFillRect((HDC)m_pBg->m_hBmp, CGRectMake(0, 0, m_pBg->m_width, m_pBg->m_height));
            int w = (m_rc.right - m_rc.left) * 2 + 8;
            MySetRect(&rc, (int)(w * (1.0 - 0.85) * 0.5), (int)(((m_rc.bottom - m_rc.top) * 2 + 8) * (1.0 - 0.85) * 0.5), (int)(w * 0.85), (int)(((m_rc.bottom - m_rc.top) * 2 + 8) * 0.85));
            COLORREF c = CalcColor(m_bkColor, 75);
            CGContextSetRGBFillColor((HDC)m_pBg->m_hBmp, GetRValue(c) / 255.0, GetGValue(c) / 255.0, GetBValue(c) / 255.0, 1.0);
            CGContextFillEllipseInRect((HDC)m_pBg->m_hBmp, CGRectMake(rc.left - 1, rc.top - 1, rc.right - rc.left, rc.bottom - rc.top));
            c = CalcColor(m_bkColor, 75);
            CGContextSetRGBFillColor((HDC)m_pBg->m_hBmp, GetRValue(c) / 255.0, GetGValue(c) / 255.0, GetBValue(c) / 255.0, 1.0);
            CGContextFillEllipseInRect((HDC)m_pBg->m_hBmp, CGRectMake(rc.left + 2, rc.top + 2, rc.right - rc.left, rc.bottom - rc.top));
            c = CalcColor(m_bkColor, 50);
            CGContextSetRGBFillColor((HDC)m_pBg->m_hBmp, GetRValue(c) / 255.0, GetGValue(c) / 255.0, GetBValue(c) / 255.0, 1.0);
            CGContextFillEllipseInRect((HDC)m_pBg->m_hBmp, CGRectMake(rc.left + 1, rc.top + 1, rc.right - rc.left, rc.bottom - rc.top));
            c = CalcColor(m_knobColor, 100);
            CGContextSetRGBFillColor((HDC)m_pBg->m_hBmp, GetRValue(m_knobColor) / 255.0, GetGValue(m_knobColor) / 255.0, GetBValue(m_knobColor) / 255.0, 1.0);
            CGContextFillEllipseInRect((HDC)m_pBg->m_hBmp, CGRectMake(rc.left, rc.top, rc.right - rc.left, rc.bottom - rc.top));
            // scale
            int cx = (rc.left + rc.right) / 2;
            int cy = (rc.top + rc.bottom) / 2;
            double r1 = (rc.right - rc.left) / 2 + 1;
            double r2 = r1 * 1.15;
            c = m_textColor;
            CGContextBeginPath((HDC)m_pBg->m_hBmp);
            CGContextSetLineWidth((HDC)m_pBg->m_hBmp, 1.0);
            CGContextSetRGBStrokeColor((HDC)m_pBg->m_hBmp, GetRValue(c) / 255.0, GetGValue(c) / 255.0, GetBValue(c) / 255.0, 1.0);
            for(int i = 0; i <= 10; i++){
                double a = (i * 1.5 / 10.0 + 0.75) * 3.141592653589793;
                int x1 = (int)(cos(a) * r1 + 0.5 + cx);
                int y1 = (int)(sin(a) * r1 + 0.5 + cy);
                int x2 = (int)(cos(a) * r2 + 0.5 + cx);
                int y2 = (int)(r2 * sin(a) + 0.5 + cy);
                MoveTo0((HDC)m_pBg->m_hBmp, x1, y1);
                LineTo0((HDC)m_pBg->m_hBmp, x2, y2);
            }
            CGContextStrokePath((HDC)m_pBg->m_hBmp);
            DeleteDC(hdcMem);
        }
    }
    if(bAll){
        DrawLetter(hdc, &m_rcText, m_text, m_bkColor, m_textColor, 0, 0, 0);
    }
    if(m_lastValue == m_value && !m_bInit) return FALSE;

    m_lastValue = m_value;
    int bw = m_pBg->m_width;
    int bh = m_pBg->m_height;
    CDIB* pTmp = m_pGui->getTempDIB(bw, bh);
    pTmp->bitBlt(0, 0, bw, bh, m_pBg, 0, 0);
    HDC hdcMem = CreateCompatibleDC(hdc);
    int w = (m_rc.right - m_rc.left) * 2 + 8;
    MySetRect(&rc, (int)(w * (1.0 - 0.85) * 0.5), (int)(((m_rc.bottom - m_rc.top) * 2 + 8) * (1.0 - 0.85) * 0.5), (int)(w * 0.85), (int)(((m_rc.bottom - m_rc.top) * 2 + 8) * 0.85));
    int cx = (rc.left + rc.right) / 2;
    int cy = (rc.top + rc.bottom) / 2;
    double r = (rc.right - rc.left) / 2;
    // pointer
    CGContextBeginPath((HDC)pTmp->m_hBmp);
    CGContextSetLineWidth((HDC)pTmp->m_hBmp, 4.0);
    CGContextSetRGBStrokeColor((HDC)pTmp->m_hBmp, 1.0, 0, 0, 1.0);
    double a = ((double)m_value * 1.5 / m_max + 0.75) * 3.141592653589793;
    double r2 = r * 0.8;
    int x2 = (int)(cos(a) * r2 + 0.5 + cx);
    int y2 = (int)(r2 * sin(a) + 0.5 + cy);
    int y1 = (int)(r * 0.0 * sin(a) + 0.5 + cy);
    MoveTo0((HDC)pTmp->m_hBmp, cx, y1);
    LineTo0((HDC)pTmp->m_hBmp, x2, y2);
    CGContextStrokePath((HDC)pTmp->m_hBmp);
    DeleteDC(hdcMem);
    int kw = m_rc.right - m_rc.left;
    if(m_bInit){
        int w4 = kw + 4;
        int h4 = m_rc.bottom - m_rc.top + 4;
        pDib->stretchBlt(m_rc.left - 2, m_rc.top - 2, w4, h4, pTmp, 0, 0, w4 * 2, h4 * 2);
    }else{
        pDib->stretchBlt(m_rc.left, m_rc.top, kw, m_rc.bottom - m_rc.top,
            pTmp, 4, 4, kw * 2, (m_rc.bottom - m_rc.top) * 2);
    }
    m_bInit = 0;
    return TRUE;
}

void CRoteryV::setSliderPlace(int place)
{
    m_sliderPlace = place;
}

void CRoteryV::mouseDown(int x, int y)
{
    if(m_pGui->m_sliderType == 1){
        // slider popup
        g_VolSubWin.init(0, m_max, m_value, m_id);
        m_pGui->m_pCapture = &g_VolSubWin;
        RECT rc;
        int px = m_sliderPlace ? m_rc.left - 18 : m_rc.right;
        int py = m_rc.top - 60;
        GetClientRectControl(m_hWnd, &rc);
        if(px < 0) px = 0;
        if(py < 0) py = 0;
        if(px + 18 > rc.right) px = rc.right - 18;
        if(py + 150 > rc.bottom) py = rc.bottom - 150;
        g_VolSubWin.create(m_hWnd, px, py, 18, 150, this);
    }else{
        m_pGui->startDragInputPart(this, x, y, 0, m_max, m_value, TRUE);
    }
}

void CRoteryV::setVol(int vol)
{
    m_value = vol;
    drawMeOwn();
    SendMessageControl(m_hWnd, WM_GUIPARTS_NOTIFY, m_id, m_value);
}

void CRoteryV::setValue(int val)
{
    m_value = MIN(m_max, MAX(val, 0));
    drawMeOwn();
}

CRoteryV::~CRoteryV()
{
    if(m_pUser) delete (CDIB*)m_pUser;
}

//---------------------------------------------------------------------------
// CLEDMatrix

CLEDMatrix::CLEDMatrix(HWND hWnd, int id, int x, int y, int w, int h, int cols, int rows, char* text, COLORREF bkColor, COLORREF textColor)
{
    init(hWnd, id, x, y, w, h, text, bkColor, textColor);
    m_cols = cols;
    m_rows = rows;
    int n = rows * cols;
    m_colors = new int[n];
    m_drawnColors = new int[n];
    for(int i = 0; i < n; i++){
        m_colors[i] = 0;
        m_drawnColors[i] = -1;
    }
}

CLEDMatrix::~CLEDMatrix()
{
    if(m_colors) delete m_colors;
    if(m_drawnColors) delete m_drawnColors;
}

BOOL CLEDMatrix::PtInRt(int x, int y)
{
    return FALSE;
}

void CLEDMatrix::setValue(int val)
{
    drawMeOwn();
}

void CLEDMatrix::setColors(int* colors)
{
    int n = m_cols * m_rows;
    for(int i = 0; i < n; i++){
        m_colors[i] = colors[i];
    }
    drawMeOwn();
}

BOOL CLEDMatrix::drawMe(HDC hdc, CDIB* pDib, int bAll)
{
    RECT rc = m_rc;
    int w = (m_rc.right - m_rc.left) / m_cols;
    int h = (m_rc.bottom - m_rc.top) / m_rows;
    int n = m_rows * m_cols;
    if(bAll){
        for(int i = 0; i < n; i++){
            rc.left = m_rc.left + (m_rc.right - m_rc.left) * (i % m_cols) / m_cols;
            rc.right = rc.left + w;
            rc.top = (i / m_cols) * (m_rc.bottom - m_rc.top) / m_rows + m_rc.top;
            rc.bottom = rc.top + h;
            DrawFrame((HDC)pDib->m_hBmp, &rc, m_bkColor, 1);
        }
    }
    for(int i = 0; i < n; i++){
        if(m_colors[i] != m_drawnColors[i]){
            m_drawnColors[i] = m_colors[i];
            rc.left = (m_rc.right - m_rc.left) * (i % m_cols) / m_cols + m_rc.left;
            rc.right = rc.left + w;
            rc.top = (i / m_cols) * (m_rc.bottom - m_rc.top) / m_rows + m_rc.top;
            rc.bottom = rc.top + h;
            CGContextSetRGBFillColor((HDC)pDib->m_hBmp, GetRValue(m_colors[i]) / 255.0, GetGValue(m_colors[i]) / 255.0, GetBValue(m_colors[i]) / 255.0, 1.0);
            CGContextFillRect((HDC)pDib->m_hBmp, CGRectMake(rc.left + 1, rc.top + 1, rc.right - rc.left - 2, rc.bottom - rc.top - 2));
        }
    }
    return TRUE;
}
