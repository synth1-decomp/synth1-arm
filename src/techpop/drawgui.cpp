/**
 * @file drawgui.cpp
 * @brief GUI drawing routines (ported from the Win32 GDI version to CoreGraphics).
 *
 * Also holds the image cache used by DrawLED2().
 */

#include <stdio.h>
#include <string.h>
#include <math.h>
#include <string>
#include <map>
#include "common.h"
#include "debug.h"
#include "cdib.h"
#include "drawgui.h"

// bitmap resources
#define IDB_LEDBUTTON_ON    0x82
#define IDB_LEDBUTTON_OFF   0x83
#define IDB_PUSHBUTTON_OFF  0x84
#define IDB_PUSHBUTTON_ON   0x85
#define IDB_LED_RED_OFF     0x86
#define IDB_LED_RED_ON      0x87
#define IDB_LED_GREEN_OFF   0x88
#define IDB_LED_GREEN_ON    0x89
#define IDB_LED_YELLOW_OFF  0x8a
#define IDB_LED_YELLOW_ON   0x8b
#define IDB_LED2_RED        0xaa
#define IDB_LED2_GREEN      0xab

#ifndef SRCCOPY
#define SRCCOPY             0x00CC0020
#endif

#define LIMIT(v)    ((v) < 0 ? 0 : ((v) > 255 ? 255 : (v)))
#define LIMIT2(v)   ((v) > 255 ? 255 : ((v) < 0 ? 0 : (v)))

#define DIBCONTEXT(pDib)    ((CGContextRef)(pDib)->m_hBmp)

// HLS color model
#define HLSMAX      240
#define RGBMAX      255
#define UNDEFINED   (HLSMAX*2/3)

extern HINSTANCE hInst;

BOOL g_bTransparencyWin;

static const float SYNTHETIC_ITALIC_SKEW = tanf(14 * (float)M_PI / 180);

// select a 1 pixel pen of the color and begin a new path
/**
 * @brief Begins a new path with a 1-pixel opaque stroke of the given color.
 *
 * @param hdc       Target context.
 * @param color     Stroke color.
 */
static inline void SelectPen(HDC hdc, COLORREF color)
{
    CGContextBeginPath(hdc);
    CGContextSetLineWidth(hdc, 1.0);
    CGContextSetRGBStrokeColor(hdc, GetRValue(color)/255.0, GetGValue(color)/255.0, GetBValue(color)/255.0, 1.0);
}

/**
 * @brief Sets the opaque stroke color of a context.
 *
 * @param hdc       Target context.
 * @param color     Stroke color.
 */
static inline void SetStrokeColor(HDC hdc, COLORREF color)
{
    CGContextSetRGBStrokeColor(hdc, GetRValue(color)/255.0, GetGValue(color)/255.0, GetBValue(color)/255.0, 1.0);
}

/**
 * @brief Sets the opaque fill color of a context.
 *
 * @param hdc       Target context.
 * @param color     Fill color.
 */
static inline void SelectBrush(HDC hdc, COLORREF color)
{
    CGContextSetRGBFillColor(hdc, GetRValue(color)/255.0, GetGValue(color)/255.0, GetBValue(color)/255.0, 1.0);
}

/**
 * @brief Converts a RECT to a CGRect.
 *
 * @param rc    Rectangle (left, top, right, bottom).
 * @return The same rectangle as origin and size.
 */
static inline CGRect MakeCGRect(RECT* rc)
{
    return CGRectMake(rc->left, rc->top, rc->right - rc->left, rc->bottom - rc->top);
}

BOOL InvisibleWindow(HWND hWnd, int bInvisible)
{
    return FALSE;
}

// brightness of the color in percent
COLORREF CalcColor(COLORREF color, int percent)
{
    int r = GetRValue(color);
    int g = GetGValue(color);
    int b = GetBValue(color);
    return RGB(MIN(255, MAX(0, r * percent / 100)), MIN(255, MAX(0, g * percent / 100)), MIN(255, MAX(0, b * percent / 100)));
}

// rotary knob (perspective projection of a cylinder)
void DrawRotery(HDC hdc, int cx, int cy, int r, int depth, int vx, int vy, int vz, COLORREF color)
{
    int eyeDist = -vz;
    int pos[4][3] = {
        {cx-r, cy-r, -depth},
        {cx+r, cy+r, -depth},
        {cx-r, cy-r, 0},
        {cx+r, cy+r, 0},
    };
    POINT pt[4];
    for(int i=0;i<4;i++){
        pt[i].x = (int)((pos[i][0] - vx) / ((double)(pos[i][2] - vz) / eyeDist)) + vx;
        pt[i].y = (int)((pos[i][1] - vy) / ((double)(pos[i][2] - vz) / eyeDist)) + vy;
    }

    BYTE h, l, s;
    RGBtoHLS(color, &h, &l, &s);
    int lum = l + (HLSMAX - l) / 4;
    if(lum < 0) lum = 0;
    if(lum > HLSMAX) lum = HLSMAX;
    COLORREF sideColor = HLStoRGB(h, lum, s);

    SelectBrush(hdc, sideColor);
    CGContextFillEllipseInRect(hdc, CGRectMake(pt[2].x, pt[2].y, pt[3].x - pt[2].x, pt[3].y - pt[2].y));
    SelectBrush(hdc, color);
    CGContextFillEllipseInRect(hdc, CGRectMake(pt[0].x, pt[0].y, pt[1].x - pt[0].x, pt[1].y - pt[0].y));
}

void RightDownRect(RECT* rc, int x, int y)
{
    rc->left += x;
    rc->top += y;
    rc->right += x;
    rc->bottom += y;
}

void MySetRect(RECT* rc, int x, int y, int w, int h)
{
    rc->left = x;
    rc->right = x + w;
    rc->top = y;
    rc->bottom = y + h;
}

void DrawLEDButton(HDC hdc, RECT* rc, int bOn, COLORREF color)
{
    DrawButton(hdc, IDB_LEDBUTTON_OFF, IDB_LEDBUTTON_ON, rc, bOn, color);
}

void DrawPushButton(HDC hdc, RECT* rc, int bOn, COLORREF color)
{
    DrawButton(hdc, IDB_PUSHBUTTON_OFF, IDB_PUSHBUTTON_ON, rc, bOn, color);
}

void DrawButton(HDC hdc, int idOff, int idOn, RECT* rc, int bOn, COLORREF color)
{
    DrawBitmap(hdc, bOn ? idOn : idOff, rc);
    DrawButtonShadow(hdc, rc, color, bOn);
}

void DrawBitmap(HDC hdc, int id, RECT* rc)
{
    HBITMAP hBmp = LoadBitmap(hInst, id);
    BITMAP bm;
    GetObject(hBmp, sizeof(BITMAP), &bm);
    CGImageRelease(hBmp);
}

void DrawButtonShadow(HDC hdc, RECT* rc, COLORREF color, int bPushed)
{
    COLORREF c1, c2, c3;
    if(bPushed){
        c1 = CalcColor(color, 100);
        c2 = CalcColor(color, 100);
        c3 = CalcColor(color, 100);
    }else{
        c1 = CalcColor(color, 50);
        c2 = CalcColor(color, 75);
        c3 = CalcColor(color, 88);
    }

    SelectPen(hdc, c1);
    MoveTo0(hdc, rc->left+1, rc->bottom);
    LineTo0(hdc, rc->right, rc->bottom);
    LineTo0(hdc, rc->right, rc->top);
    CGContextStrokePath(hdc);

    SelectPen(hdc, c2);
    MoveTo0(hdc, rc->left+2, rc->bottom+1);
    LineTo0(hdc, rc->right+1, rc->bottom+1);
    LineTo0(hdc, rc->right+1, rc->top+1);
    MoveTo0(hdc, rc->left-1, rc->top+1);
    LineTo0(hdc, rc->left-1, rc->bottom-2);
    MoveTo0(hdc, rc->left+2, rc->top-1);
    LineTo0(hdc, rc->right-1, rc->top-1);
    CGContextStrokePath(hdc);

    SelectPen(hdc, c3);
    MoveTo0(hdc, rc->left+3, rc->bottom+2);
    LineTo0(hdc, rc->right+2, rc->bottom+2);
    LineTo0(hdc, rc->right+2, rc->top+2);
    CGContextStrokePath(hdc);
}

void DrawTextButton(HDC hdc, RECT* rc, char* str, int bOn, int reserved, COLORREF color, COLORREF textColor, int ox, int oy)
{
    if(bOn){
        COLORREF c = CalcColor(color, 120);
        COLORREF frameColor = CalcColor(c, 50);
        SelectBrush(hdc, c);
        CGContextFillRect(hdc, MakeCGRect(rc));
        CGContextSetRGBStrokeColor(hdc, GetRValue(frameColor)/255.0, GetGValue(frameColor)/255.0, GetBValue(frameColor)/255.0, 1.0);
        CGContextStrokeRect(hdc, MakeCGRect(rc));
        color = c;
    }else{
        DrawPanel(hdc, rc, color, 2);
    }
    RECT r = *rc;
    r.left += ox;
    r.top += oy;
    r.left += 2;
    r.top += 2;
    r.right -= 2;
    r.bottom -= (r.bottom - r.top > 15) ? 4 : 1;
    DrawLetter(hdc, &r, str, color, textColor, TRUE, TRUE, TRUE);
}

void DrawPanel(HDC hdc, RECT* rc, COLORREF color, int depth)
{
    BYTE h, l, s;
    RGBtoHLS(color, &h, &l, &s);
    COLORREF base = CalcColor(color, 100);
    COLORREF hi1 = CalcColor(color, 200);
    COLORREF hi2 = CalcColor(color, 150);
    COLORREF hi3 = CalcColor(color, 125);
    COLORREF sh1 = CalcColor(color, 50);
    COLORREF sh2 = CalcColor(color, 75);
    COLORREF sh3 = CalcColor(color, 88);

    SelectBrush(hdc, base);
    CGContextFillRect(hdc, MakeCGRect(rc));

    // highlight
    if(depth >= 1){
        SelectPen(hdc, hi1);
        MoveTo0(hdc, rc->left, rc->bottom-1);
        LineTo0(hdc, rc->left, rc->top);
        LineTo0(hdc, rc->right, rc->top);
        CGContextStrokePath(hdc);
    }
    if(depth >= 2){
        SelectPen(hdc, hi2);
        MoveTo0(hdc, rc->left+1, rc->bottom-2);
        LineTo0(hdc, rc->left+1, rc->top+1);
        LineTo0(hdc, rc->right-1, rc->top+1);
        CGContextStrokePath(hdc);
    }
    if(depth >= 3){
        SelectPen(hdc, hi3);
        MoveTo0(hdc, rc->left+2, rc->bottom-3);
        LineTo0(hdc, rc->left+2, rc->top+2);
        LineTo0(hdc, rc->right-2, rc->top+2);
        CGContextStrokePath(hdc);
    }

    // shadow
    if(depth >= 1){
        SelectPen(hdc, sh1);
        MoveTo0(hdc, rc->left, rc->bottom-1);
        LineTo0(hdc, rc->right-1, rc->bottom-1);
        LineTo0(hdc, rc->right-1, rc->top);
        CGContextStrokePath(hdc);
    }
    if(depth >= 2){
        SelectPen(hdc, sh2);
        MoveTo0(hdc, rc->left+1, rc->bottom-2);
        LineTo0(hdc, rc->right-2, rc->bottom-2);
        LineTo0(hdc, rc->right-2, rc->top+1);
        CGContextStrokePath(hdc);
    }
    if(depth >= 3){
        SelectPen(hdc, sh3);
        MoveTo0(hdc, rc->left+2, rc->bottom-3);
        LineTo0(hdc, rc->right-3, rc->bottom-3);
        LineTo0(hdc, rc->right-3, rc->top+2);
        CGContextStrokePath(hdc);
    }
}

// draw a monochrome bitmap: white pixels in color, the others in bkColor
void DrawTransparentBmp(CDIB* pDib, RECT* rc, int id, COLORREF color, COLORREF bkColor)
{
    CDIB dib;
    BITMAP bm;
    HBITMAP hBmp = LoadBitmap(hInst, id);
    if(hBmp){
        GetObject(hBmp, sizeof(BITMAP), &bm);
        dib.create(bm.bmWidth, bm.bmHeight);
        HDC hMemDC = CreateCompatibleDC(NULL);
        COLORREF c1 = color | 0xff000000;
        COLORREF c2 = bkColor | 0xff000000;
        RECT rc2;
        MySetRect(&rc2, 0, 0, bm.bmWidth, bm.bmHeight);
        DrawBitmapSub(&dib, hBmp, bm.bmWidth, bm.bmHeight, &rc2);
        DeleteDC(hMemDC);
        CGImageRelease(hBmp);
        dib.Reverse();
        for(int y=0;y<bm.bmHeight;y++){
            for(int x=0;x<bm.bmWidth;x++){
                if((dib.getPixel(x, y) & 0xffffff) == 0xffffff){
                    dib.setPixel(x, y, c1);
                }else{
                    dib.setPixel(x, y, c2);
                }
            }
        }
        pDib->stretchBlt(rc->left, rc->top, rc->right - rc->left, rc->bottom - rc->top, &dib, 0, 0, dib.m_width, dib.m_height);
    }
}

void DrawBitmapSub(CDIB* pDib, HBITMAP hBmp, int w, int h, RECT* rc)
{
    pDib->StretchBlt(rc->left, rc->top, rc->right - rc->left, rc->bottom - rc->top, hBmp, 0, 0, w, h);
}

// draw a monochrome bitmap: white pixels are transparent, the others in color
void DrawTransparentBmpTrans(CDIB* pDib, RECT* rc, int id, COLORREF color)
{
    CDIB dib;
    BITMAP bm;
    HBITMAP hBmp = LoadBitmap(hInst, id);
    if(hBmp == NULL) return;
    GetObject(hBmp, sizeof(BITMAP), &bm);
    dib.create(bm.bmWidth, bm.bmHeight);
    HDC hMemDC = CreateCompatibleDC(NULL);
    COLORREF c = color | 0xff000000;
    dib.StretchBlt(0, 0, bm.bmWidth, bm.bmHeight, hBmp, 0, 0, bm.bmWidth, bm.bmHeight);
    DeleteDC(hMemDC);
    CGImageRelease(hBmp);
    dib.Reverse();
    for(int y=0;y<bm.bmHeight;y++){
        for(int x=0;x<bm.bmWidth;x++){
            if((dib.getPixel(x, y) & 0xffffff) == 0xffffff){
                dib.setPixel(x, y, 0);
            }else{
                dib.setPixel(x, y, c);
            }
        }
    }
    CDIB work;
    work.create(bm.bmWidth, bm.bmHeight);
    work.stretchBlt(0, 0, work.m_width, work.m_height, pDib, rc->left, rc->top, rc->right - rc->left, rc->bottom - rc->top);
    work.bitTransBlt(0, 0, &dib, 0, 0, dib.m_width, dib.m_width, 0);
    pDib->stretchBlt(rc->left, rc->top, rc->right - rc->left, rc->bottom - rc->top, &work, 0, 0, work.m_width, work.m_height);
}

void DrawLetter(HDC hdc, RECT* rc, char* str, COLORREF bkColor, COLORREF textColor, int bCenter, int bTransparent, int style)
{
    CDIB dib;
    if(dib.init((HBITMAP)hdc) == TRUE){
        DrawLetterDIB(&dib, rc, str, bkColor, textColor, bCenter, bTransparent, style);
    }
}

// draw a text with CoreText (2x oversampled, then reduced)
void DrawLetterDIB(CDIB* pDib, RECT* rc, char* str, COLORREF bkColor, COLORREF textColor, int bCenter, int bTransparent, int style)
{
    if(str == NULL) return;
    int len = strlen(str);
    if(len == 0) return;

    int h = rc->bottom - rc->top;
    int h2 = h * 2;
    float fontSize = h2 * 0.9;
    CGContextSelectFont(DIBCONTEXT(pDib), "Lucida Grande", fontSize, kCGEncodingMacRoman);
    CGContextSetTextDrawingMode(DIBCONTEXT(pDib), kCGTextInvisible);
    CGPoint pt0 = CGContextGetTextPosition(DIBCONTEXT(pDib));
    CGContextShowText(DIBCONTEXT(pDib), str, len);
    CGPoint pt1 = CGContextGetTextPosition(DIBCONTEXT(pDib));
    int textWidth = (int)(pt1.x - pt0.x);
    int w2 = textWidth + textWidth % 2;
    w2 = MIN(w2, (pDib->m_width - rc->left) * 2);

    int ox = 0;
    if(bCenter){
        ox = ((rc->right - rc->left) - w2/2) / 2;
    }

    CDIB dib;
    dib.create(w2, h2);
    if(bTransparent){
        dib.stretchBlt(0, 0, w2, h2, pDib, rc->left + ox, rc->top, w2/2, h);
    }else{
        SelectBrush(DIBCONTEXT(&dib), bkColor);
        CGContextFillRect(DIBCONTEXT(&dib), CGRectMake(0, 0, w2, h2));
    }

    CGContextSaveGState(DIBCONTEXT(&dib));
    {
        int height = CGBitmapContextGetHeight(DIBCONTEXT(&dib));
        CGContextScaleCTM(DIBCONTEXT(&dib), 1.0, -1.0);
        CGContextTranslateCTM(DIBCONTEXT(&dib), 0, -height);
        if(style & 2){
            CGAffineTransform matrix = CGAffineTransformMake(1.0, 0, SYNTHETIC_ITALIC_SKEW, 1.0, 0, 0);
            CGContextSetTextMatrix(DIBCONTEXT(&dib), matrix);
        }
        CGContextSetTextDrawingMode(DIBCONTEXT(&dib), kCGTextFill);

        CFStringRef fontName = CFStringCreateWithCString(kCFAllocatorDefault, "Lucida Grande", kCFStringEncodingUTF8);
        CFStringRef keys[] = { kCTFontAttributeName, kCTForegroundColorAttributeName };
        CTFontRef font = CTFontCreateWithName(fontName, fontSize, NULL);
        CGColorSpaceRef colorSpace = CGColorSpaceCreateDeviceRGB();
        CGFloat components[] = { GetRValue(textColor)/255.0, GetGValue(textColor)/255.0, GetBValue(textColor)/255.0, 1.0 };
        CGColorRef color = CGColorCreate(colorSpace, components);
        CFTypeRef values[] = { font, color };
        CFDictionaryRef attr = CFDictionaryCreate(kCFAllocatorDefault, (const void**)keys, (const void**)values, 2,
            &kCFTypeDictionaryKeyCallBacks, &kCFTypeDictionaryValueCallBacks);
        CFRelease(fontName);
        CFRelease(font);
        CGColorRelease(color);
        CGColorSpaceRelease(colorSpace);

        CFStringRef string = CFStringCreateWithCString(kCFAllocatorDefault, str, kCFStringEncodingUTF8);
        CFAttributedStringRef attrString = CFAttributedStringCreate(kCFAllocatorDefault, string, attr);
        CTLineRef line = CTLineCreateWithAttributedString(attrString);
        CGContextSetTextPosition(DIBCONTEXT(&dib), 0, fontSize * 0.25);
        CTLineDraw(line, DIBCONTEXT(&dib));
        CFRelease(line);
        CFRelease(string);
        CFRelease(attrString);
        CFRelease(attr);
    }
    CGContextRestoreGState(DIBCONTEXT(&dib));

    pDib->stretchBlt(rc->left + ox, rc->top, w2/2, h, &dib, 0, 0, w2, h2);
}

void DrawLED(HDC hdc, RECT* rc, int type, int bOn, COLORREF color)
{
    int id;
    if(type == 0){
        id = bOn ? IDB_LED_RED_ON : IDB_LED_RED_OFF;
    }else if(type == 1){
        id = bOn ? IDB_LED_GREEN_ON : IDB_LED_GREEN_OFF;
    }else{
        id = bOn ? IDB_LED_YELLOW_ON : IDB_LED_YELLOW_OFF;
    }
    HBITMAP hBmp = LoadBitmap(hInst, id);
    HDC hMemDC = CreateCompatibleDC(hdc);
    SetPixel(hMemDC, 0, 0, color);
    SetPixel(hMemDC, 0, 7, color);
    SetPixel(hMemDC, 7, 0, color);
    SetPixel(hMemDC, 7, 7, color);
    StretchBlt(hdc, rc->left, rc->top, rc->right - rc->left, rc->bottom - rc->top, hMemDC, 0, 0, 8, 8, SRCCOPY);
    CGImageRelease(hBmp);
    DeleteDC(hMemDC);
}

void DrawToggleButtonShadow(HDC hdc, RECT* rc, COLORREF color, int bOn)
{
    CGContextSetRGBStrokeColor(hdc, GetRValue(color)/255.0, GetGValue(color)/255.0, GetBValue(color)/255.0, 1.0);
    CGContextStrokeRect(hdc, CGRectMake(rc->left-1, rc->top-1, rc->right - rc->left + 2, rc->bottom - rc->top + 2));
    CGContextStrokeRect(hdc, CGRectMake(rc->left-2, rc->top-2, rc->right - rc->left + 4, rc->bottom - rc->top + 4));
    CGContextStrokeRect(hdc, CGRectMake(rc->left-3, rc->top-3, rc->right - rc->left + 6, rc->bottom - rc->top + 6));
    if(bOn){
        COLORREF c = CalcColor(color, 150);
        CGContextSetRGBStrokeColor(hdc, GetRValue(c)/255.0, GetGValue(color)/255.0, GetBValue(color)/255.0, 1.0);
        CGContextStrokeRect(hdc, CGRectMake(rc->left-1, rc->top-1, rc->right - rc->left + 2, rc->bottom - rc->top + 2));

        CGContextBeginPath(hdc);
        CGContextSetLineWidth(hdc, 1.0);
        c = CalcColor(color, 110);
        CGContextSetRGBStrokeColor(hdc, GetRValue(c)/255.0, GetGValue(color)/255.0, GetBValue(color)/255.0, 1.0);
        MoveTo0(hdc, rc->left, rc->top-2);
        LineTo0(hdc, rc->right, rc->top-2);
        MoveTo0(hdc, rc->right+1, rc->top);
        LineTo0(hdc, rc->right+1, rc->bottom);
        MoveTo0(hdc, rc->left, rc->bottom+1);
        LineTo0(hdc, rc->right, rc->bottom+1);
        MoveTo0(hdc, rc->left-2, rc->top);
        LineTo0(hdc, rc->left-2, rc->bottom);
        CGContextStrokePath(hdc);

        CGContextBeginPath(hdc);
        CGContextSetLineWidth(hdc, 1.0);
        c = CalcColor(color, 105);
        CGContextSetRGBStrokeColor(hdc, GetRValue(c)/255.0, GetGValue(color)/255.0, GetBValue(color)/255.0, 1.0);
        MoveTo0(hdc, rc->left+2, rc->top-3);
        LineTo0(hdc, rc->right-2, rc->top-3);
        MoveTo0(hdc, rc->right+2, rc->top+2);
        LineTo0(hdc, rc->right+2, rc->bottom-2);
        MoveTo0(hdc, rc->left+2, rc->bottom+2);
        LineTo0(hdc, rc->right-2, rc->bottom+2);
        MoveTo0(hdc, rc->left-3, rc->top+2);
        LineTo0(hdc, rc->left-3, rc->bottom-2);
        CGContextStrokePath(hdc);
    }else{
        DrawButtonShadow(hdc, rc, color, FALSE);
    }
}

void DrawFrame(HDC hdc, RECT* rc, COLORREF color, int bSunken)
{
    COLORREF light = CalcColor(color, 125);
    COLORREF dark = CalcColor(color, 88);

    CGContextBeginPath(hdc);
    CGContextSetLineWidth(hdc, 1.0);
    if(bSunken) SetStrokeColor(hdc, light);
    else SetStrokeColor(hdc, dark);
    MoveTo0(hdc, rc->left-1, rc->bottom);
    LineTo0(hdc, rc->right, rc->bottom);
    LineTo0(hdc, rc->right, rc->top-2);
    CGContextStrokePath(hdc);

    CGContextBeginPath(hdc);
    CGContextSetLineWidth(hdc, 1.0);
    if(bSunken) SetStrokeColor(hdc, dark);
    else SetStrokeColor(hdc, light);
    MoveTo0(hdc, rc->left-1, rc->bottom);
    LineTo0(hdc, rc->left-1, rc->top-1);
    LineTo0(hdc, rc->right+1, rc->top-1);
    CGContextStrokePath(hdc);
}

void RGBtoHLS(COLORREF rgb, BYTE* pH, BYTE* pL, BYTE* pS)
{
    WORD R, G, B;
    WORD cMax, cMin;
    WORD Rdelta, Gdelta, Bdelta;

    R = GetRValue(rgb);
    G = GetGValue(rgb);
    B = GetBValue(rgb);

    cMax = MAX(MAX(R, G), B);
    cMin = MIN(MIN(R, G), B);

    *pL = (((cMax + cMin) * HLSMAX) + RGBMAX) / (2 * RGBMAX);

    if(cMax == cMin){
        *pS = 0;
        *pH = UNDEFINED;
    }else{
        if(*pL <= (HLSMAX/2)){
            *pS = (((cMax - cMin) * HLSMAX) + ((cMax + cMin) / 2)) / (cMax + cMin);
        }else{
            *pS = (((cMax - cMin) * HLSMAX) + ((2 * RGBMAX - cMax - cMin) / 2)) / (2 * RGBMAX - cMax - cMin);
        }

        Rdelta = (((cMax - R) * (HLSMAX/6)) + ((cMax - cMin) / 2)) / (cMax - cMin);
        Gdelta = (((cMax - G) * (HLSMAX/6)) + ((cMax - cMin) / 2)) / (cMax - cMin);
        Bdelta = (((cMax - B) * (HLSMAX/6)) + ((cMax - cMin) / 2)) / (cMax - cMin);

        if(R == cMax){
            *pH = Bdelta - Gdelta;
        }else if(G == cMax){
            *pH = (HLSMAX/3) + Rdelta - Bdelta;
        }else{
            *pH = ((2 * HLSMAX)/3) + Gdelta - Rdelta;
        }

        if(*pH < 0) *pH += HLSMAX;
        if(*pH > HLSMAX) *pH -= HLSMAX;
    }
}

COLORREF HLStoRGB(WORD hue, WORD lum, WORD sat)
{
    WORD R, G, B;
    WORD Magic1, Magic2;

    if(sat == 0){
        R = G = B = (lum * RGBMAX) / HLSMAX;
    }else{
        if(lum <= (HLSMAX/2)){
            Magic2 = (lum * (HLSMAX + sat) + (HLSMAX/2)) / HLSMAX;
        }else{
            Magic2 = lum + sat - ((lum * sat) + (HLSMAX/2)) / HLSMAX;
        }
        Magic1 = 2 * lum - Magic2;

        R = (HueToRGB(Magic1, Magic2, hue + (HLSMAX/3)) * RGBMAX + (HLSMAX/2)) / HLSMAX;
        G = (HueToRGB(Magic1, Magic2, hue) * RGBMAX + (HLSMAX/2)) / HLSMAX;
        B = (HueToRGB(Magic1, Magic2, hue - (HLSMAX/3)) * RGBMAX + (HLSMAX/2)) / HLSMAX;
    }
    return RGB(R, G, B);
}

WORD HueToRGB(WORD n1, WORD n2, WORD hue)
{
    // range check: note values passed add/subtract thirds of range
    if(hue < 0) hue += HLSMAX;
    if(hue > HLSMAX) hue -= HLSMAX;

    // return r,g, or b value from this tridrant
    if(hue < (HLSMAX/6)) return (n1 + (((n2 - n1) * hue + (HLSMAX/12)) / (HLSMAX/6)));
    if(hue < (HLSMAX/2)) return n2;
    if(hue < ((HLSMAX*2)/3)) return (n1 + (((n2 - n1) * (((HLSMAX*2)/3) - hue) + (HLSMAX/12)) / (HLSMAX/6)));
    return n1;
}

// cache of the generated bitmaps
/**
 * @brief Cache of generated LED images keyed by a description string.
 *
 * Used by DrawLED2(). Freeing is deferred: freeBitmapCache() only sets
 * m_bFreeRequest and the next get() empties the cache.
 * @note The cache owns the images and releases them in free(); it is not locked,
 *       so it must only be used from the drawing thread.
 */
class CachedBmps {
public:
    /**
     * @brief Creates an empty cache with no pending free request.
     */
    CachedBmps(){
        m_bFreeRequest = FALSE;
    }

    /**
     * @brief Looks up an image, first handling a pending free request.
     *
     * @param key   Cache key.
     * @return The cached image (still owned by the cache), or NULL if not cached
     *         or if a pending free request emptied the cache.
     * @throws std::bad_alloc if the key string cannot be allocated.
     */
    CGImageRef get(char* key){
        CGImageRef hImage = NULL;
        if(m_bFreeRequest){
            m_bFreeRequest = FALSE;
            free();
        }else{
            std::map<std::string, CGImageRef>::iterator it = m_bmps.find(key);
            if(it != m_bmps.end()) hImage = it->second;
        }
        return hImage;
    }

    /**
     * @brief Adds an image; the cache takes ownership.
     *
     * @param key       Cache key.
     * @param hImage    Image to store.
     * @throws std::bad_alloc if the map node or key string cannot be allocated.
     */
    void add(char* key, CGImageRef hImage){
        m_bmps.insert(std::make_pair(std::string(key), hImage));
    }

    /**
     * @brief Releases all cached images and empties the cache.
     */
    void free(){
        edPrintf("bmpキャッシュ フリー\n");
        for(std::map<std::string, CGImageRef>::iterator it = m_bmps.begin(); it != m_bmps.end(); it++){
            CGImageRelease(it->second);
        }
        m_bmps.clear();
    }

    std::map<std::string, CGImageRef> m_bmps;   // +0x00
    BOOL m_bFreeRequest;                        // +0x30
};

static CachedBmps g_cachedBmps;

void startupGraphics()
{
}

void shutdownGraphics()
{
}

// the cache is freed on the next access (from the drawing thread)
void freeBitmapCache()
{
    g_cachedBmps.m_bFreeRequest = TRUE;
}

// LED with cached bitmap
void DrawLED2(HDC hdc, RECT* rc, int type, int bOn, COLORREF color, int style)
{
    int w = rc->right - rc->left;
    int h = rc->bottom - rc->top;
    char key[32];
    sprintf(key, "%d:%d:%d:%d:%d:%d", w, h, type, bOn, color, style);
    CGImageRef hImage = g_cachedBmps.get(key);
    if(hImage == NULL){
        BYTE* pBits = new BYTE[w*h*4];
        CGColorSpaceRef colorSpace = CGColorSpaceCreateDeviceRGB();
        CGContextRef ctx = CGBitmapContextCreate(pBits, w, h, 8, w*4, colorSpace, kCGImageAlphaPremultipliedLast);
        CGColorSpaceRelease(colorSpace);
        CGContextSetShouldAntialias(ctx, true);
        SelectBrush(ctx, color);
        CGContextFillRect(ctx, CGRectMake(0, 0, w, h));
        if(style){
            int id = type ? IDB_LED2_GREEN : IDB_LED2_RED;
            HBITMAP hBmp = LoadBitmap(hInst, id);
            if(hBmp){
                int bw = CGImageGetWidth(hBmp);
                int bh = (int)CGImageGetHeight(hBmp) / 2;
                CGImageRef hPart = CGImageCreateWithImageInRect(hBmp, CGRectMake(0, bOn ? bh : 0, bw, bh));
                if(hPart){
                    CGContextDrawImage(ctx, CGRectMake(0, 0, bw, bh), hPart);
                    CGImageRelease(hPart);
                }
                CGImageRelease(hBmp);
            }
        }else{
            COLORREF ledColor = RGB(type == 0 ? 255 : 0, type == 1 ? 255 : 0, 0);
            float margin = w * 0.1f;
            if(!bOn){
                int r = (int)((ledColor & 0xff) * 0.35f);
                int g = (int)(((ledColor >> 8) & 0xff) * 0.35f);
                r = MIN(255, MAX(0, r));
                g = MIN(255, MAX(0, g));
                ledColor = RGB(r, g, 0);
            }
            float x0 = margin * 0.5f - 0.5f;
            float lw = w - margin;
            float lh = h - margin;
            SelectBrush(ctx, ledColor);
            CGRect rcLed = CGRectMake(x0, x0, lw, lh);
            CGContextFillEllipseInRect(ctx, rcLed);
            CGContextSetRGBStrokeColor(ctx, 0, 0, 0, 1.0);
            CGContextStrokeEllipseInRect(ctx, rcLed);
        }
        hImage = CGBitmapContextCreateImage(ctx);
        CGContextRelease(ctx);
        delete[] pBits;
        g_cachedBmps.add(key, hImage);
    }
    CGImageRef hPart = CGImageCreateWithImageInRect(hImage, CGRectMake(0, 0, w, h));
    if(hPart){
        CGContextDrawImage(hdc, CGRectMake(rc->left, rc->top, w, h), hPart);
        CGImageRelease(hPart);
    }
}
