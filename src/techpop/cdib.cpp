/**
 * @file cdib.cpp
 * @brief CDIB: 32bit device independent bitmap.
 *
 * Pixel access, block copies and scaling on the pixel buffer of a CGBitmapContext.
 */

#include <stdlib.h>
#include <string.h>
#include "common.h"
#include "cdib.h"

#define DIBCONTEXT(h)   ((CGContextRef)(h))
#define DIBBITS(h)      ((BYTE*)CGBitmapContextGetData(DIBCONTEXT(h)))

// greatest common divisor
/**
 * @brief Greatest common divisor by repeated subtraction.
 *
 * @param a     First value (positive).
 * @param b     Second value (positive).
 * @return gcd(a, b), or 0 if either value is 0.
 */
static inline int calcGcd(int a, int b)
{
    if(a == 0 || b == 0) return 0;
    while(a != b){
        if(a > b) a -= b;
        else b -= a;
    }
    return a;
}

// least common multiple
/**
 * @brief Least common multiple.
 *
 * @param a     First value (positive).
 * @param b     Second value (positive).
 * @return lcm(a, b), or 0 if either value is 0.
 */
static inline int calcLcm(int a, int b)
{
    if(a == 0 || b == 0) return 0;
    return a / calcGcd(a, b) * b;
}

CDIB::CDIB()
{
    m_hBmp = NULL;
    m_bCreated = FALSE;
    m_pBits = NULL;
}

CDIB::~CDIB()
{
    if(m_hBmp && m_bCreated){
        CGContextRelease(DIBCONTEXT(m_hBmp));
    }
    if(m_pBits){
        free(m_pBits);
        m_pBits = NULL;
    }
}

void CDIB::create(int width, int height)
{
    m_pBits = malloc(width*height*4);
    DWORD* p = (DWORD*)m_pBits;
    for(int i=0;i<width*height;i++) p[i] = 0xff000000;
    CGColorSpaceRef colorSpace = CGColorSpaceCreateDeviceRGB();
    m_hBmp = (HBITMAP)CGBitmapContextCreate(m_pBits, width, height, 8, width*4, colorSpace, kCGImageAlphaPremultipliedLast);
    CGColorSpaceRelease(colorSpace);
    m_pitch = width*4;
    m_width = width;
    m_height = height;
    m_bCreated = TRUE;
}

BOOL CDIB::init(HBITMAP hBmp)
{
    m_pBits = NULL;
    m_hBmp = hBmp;
    m_bCreated = FALSE;
    m_width = CGBitmapContextGetWidth(DIBCONTEXT(m_hBmp));
    m_height = CGBitmapContextGetHeight(DIBCONTEXT(m_hBmp));
    m_pitch = m_width*4;
    return TRUE;
}

DWORD CDIB::getPixel(int x, int y)
{
    if(x >= m_width || y >= m_height || x < 0 || y < 0) return 0;
    return *(DWORD*)(DIBBITS(m_hBmp) + ((m_height-1-y)*m_pitch + x*4));
}

void CDIB::setPixel(int x, int y, DWORD color)
{
    if(x >= m_width || y >= m_height || x < 0 || y < 0) return;
    *(DWORD*)(DIBBITS(m_hBmp) + ((m_height-1-y)*m_pitch + x*4)) = color;
}

void CDIB::bitBlt(int x, int y, int w, int h, CDIB* pSrc, int sx, int sy)
{
    DWORD* src = (DWORD*)DIBBITS(pSrc->m_hBmp);
    DWORD* dst = (DWORD*)DIBBITS(m_hBmp);
    int srcW = pSrc->m_width;
    int srcH = pSrc->m_height;
    for(int j=0;j<h;j++){
        int s = (srcH-1-(sy+j))*srcW + sx;
        int d = (m_height-1-(y+j))*m_width + x;
        memcpy(&dst[d], &src[s], w*4);
    }
}

DWORD* CDIB::getBitsPointer()
{
    return (DWORD*)CGBitmapContextGetData(DIBCONTEXT(m_hBmp));
}

void CDIB::stretchBlt(int x, int y, int w, int h, CDIB* pSrc, int sx, int sy, int sw, int sh)
{
    if(w <= 0 || h <= 0) return;
    if(w*2 == sw && h*2 == sh){
        halfDown(x, y, w, h, pSrc, sx, sy, sw, sh);
    }else if(sw*2 == w && sh*2 == h){
        doubleUp(x, y, w, h, pSrc, sx, sy, sw, sh);
    }else{
        stretchAverage(x, y, w, h, pSrc, sx, sy, sw, sh);
    }
}

// 1/2 reduction (average of 2x2 pixels)
void CDIB::halfDown(int x, int y, int w, int h, CDIB* pSrc, int sx, int sy, int sw, int sh)
{
    int srcH = pSrc->m_height;
    int srcW = pSrc->m_width;
    DWORD* src = pSrc->getBitsPointer();
    DWORD* dst = getBitsPointer();
    for(int j=0;j<h;j++){
        int sn = (srcH-1-(sy+j*2))*srcW + sx;
        int dn = (m_height-1-(y+j))*m_width + x;
        DWORD* s = &src[sn];
        DWORD* d = &dst[dn];
        for(int i=0;i<w;i++){
            DWORD c0 = s[i*2];
            DWORD c1 = s[i*2+1];
            DWORD c2 = s[i*2-srcW];
            DWORD c3 = s[i*2-srcW+1];
            int r = (GetRValue(c0) + GetRValue(c1) + GetRValue(c2) + GetRValue(c3)) >> 2;
            int g = (GetGValue(c0) + GetGValue(c1) + GetGValue(c2) + GetGValue(c3)) >> 2;
            int b = (GetBValue(c0) + GetBValue(c1) + GetBValue(c2) + GetBValue(c3)) >> 2;
            d[i] = RGB(r,g,b) | 0xff000000;
        }
    }
}

// x2 enlargement
void CDIB::doubleUp(int x, int y, int w, int h, CDIB* pSrc, int sx, int sy, int sw, int sh)
{
    if(w <= 0 || h <= 0) return;
    int srcH = pSrc->m_height;
    int srcW = pSrc->m_width;
    DWORD* src = pSrc->getBitsPointer();
    DWORD* dst = getBitsPointer();
    for(int j=0;j<sh;j++){
        int sn = (srcH-1-(sy+j))*srcW + sx;
        int dn = (m_height-1-(y+j*2))*m_width;
        int k = dn + x;
        for(int i=0;i<sw;i++){
            DWORD c = src[sn + i];
            dst[k] = c;
            dst[k+1] = c;
            dst[k - m_width] = c;
            dst[k - m_width+1] = c;
            k += 2;
        }
    }
}

// add one source line (x0..x1 in lcm units) to the b, g, r sums and advance to the next line
/**
 * @brief Adds one row of lcm-grid cells to the color sums used by CDIB::stretchAverage().
 *
 * The source row is sy + *py / sdy; each cell xx in [x0, x1) maps to source
 * column sx + xx / sdx.
 * @param src           Source pixel buffer.
 * @param srcW          Source width in pixels.
 * @param srcH          Source height in pixels.
 * @param sx            Source rectangle left.
 * @param sy            Source rectangle top.
 * @param[in,out] py    Current grid row; incremented by one.
 * @param sdy           Grid rows per source pixel.
 * @param x0            First grid column (inclusive).
 * @param x1            Last grid column (exclusive).
 * @param sdx           Grid columns per source pixel.
 * @param[in,out] pr    Red sum.
 * @param[in,out] pg    Green sum.
 * @param[in,out] pb    Blue sum.
 */
static inline void addLine(DWORD* src, int srcW, int srcH, int sx, int sy, int* py, int sdy, int x0, int x1, int sdx, int* pr, int* pg, int* pb)
{
    DWORD* s = src + sx + (srcH-1-(sy + *py/sdy))*srcW;
    for(int xx=x0;xx<x1;xx++){
        DWORD c = s[xx/sdx];
        *pr += GetRValue(c);
        *pg += GetGValue(c);
        *pb += GetBValue(c);
    }
    (*py)++;
}

// store one pixel (bottom-up rows)
/**
 * @brief Writes one pixel of a CDIB without range checking.
 *
 * @param pDib  Target bitmap.
 * @param x     Column.
 * @param y     Row, counted from the top.
 * @param c     Pixel value.
 */
static inline void putPixel(CDIB* pDib, int x, int y, DWORD c)
{
    BYTE* p = (BYTE*)pDib->getBitsPointer();
    *(DWORD*)(p + ((pDib->m_height-1-y)*pDib->m_pitch + x*4)) = c;
}

// generic stretch with area averaging
// (each destination pixel covers dx*dy cells of the lcm grid, each source pixel sdx*sdy)
void CDIB::stretchAverage(int x, int y, int w, int h, CDIB* pSrc, int sx, int sy, int sw, int sh)
{
    int srcH = pSrc->m_height;
    int srcW = pSrc->m_width;
    DWORD* src = pSrc->getBitsPointer();
    int lx = calcLcm(w, sw);
    int ly = calcLcm(h, sh);
    int dx = lx / w;
    int sdx = lx / sw;
    int dy = ly / h;
    int sdy = ly / sh;
    int n = dx*dy;
    for(int j=0;j<h;j++){
        int y1 = (j+1)*dy;
        for(int i=0;i<w;i++){
            int x1 = (i+1)*dx;
            int yy = j*dy;
            int b = 0, g = 0, r = 0;
            while(yy < y1){
                addLine(src, srcW, srcH, sx, sy, &yy, sdy, i*dx, x1, sdx, &r, &g, &b);
            }
            putPixel(this, x+i, y+j, RGB(r/n, g/n, b/n) | 0xff000000);
        }
    }
}

void CDIB::StretchBlt(int x, int y, int w, int h, HBITMAP hSrc, int sx, int sy, int sw, int sh)
{
    if(sx == 0 && sy == 0 && CGImageGetWidth(hSrc) == sw && CGImageGetHeight(hSrc) == sh){
        CGRect rc = CGRectMake(x, y, w, h);
        CGContextDrawImage(DIBCONTEXT(m_hBmp), rc, hSrc);
    }else{
        CGRect rcSrc = CGRectMake(sx, sy, sw, sh);
        CGImageRef hImage = CGImageCreateWithImageInRect(hSrc, rcSrc);
        if(hImage){
            CGRect rc = CGRectMake(x, y, w, h);
            CGContextDrawImage(DIBCONTEXT(m_hBmp), rc, hImage);
            CGImageRelease(hImage);
        }
    }
}

// least common multiple
int CDIB::lcm(int a, int b)
{
    return calcLcm(a, b);
}

// transparent blt (pixels of transColor are not copied)
void CDIB::bitTransBlt(int x, int y, CDIB* pSrc, int sx, int sy, int w, int h, DWORD transColor)
{
    int srcH = pSrc->m_height;
    int srcW = pSrc->m_width;
    DWORD* src = pSrc->getBitsPointer();
    DWORD* dst = getBitsPointer();
    int ey = sy + h;
    int srcPitch = srcW*4;
    // note: both row expressions are spelled this way on purpose (dst row from y+j-sy,
    // src row as -1-j+srcH); this is significant for reproducing the released binary
    for(int j=sy;j<ey;j++){
        int dOff = ((m_height-1-(y+j-sy))*m_width + x - sx)*4;
        for(int i=sx;i<sx+w;i++){
            DWORD c = *(DWORD*)((BYTE*)src + ((-1-j+srcH)*srcPitch + i*4));
            if(c != transColor) *(DWORD*)((BYTE*)dst + (dOff + i*4)) = c;
        }
    }
}

// greatest common divisor
int CDIB::gcd(int a, int b)
{
    return calcGcd(a, b);
}

void CDIB::fillAll(DWORD color)
{
    DWORD* p = getBitsPointer();
    DWORD* end = p + m_height*m_width;
    while(p < end) *p++ = color;
}

void CDIB::fillRect(RECT* rc, DWORD color)
{
    BYTE* bits = (BYTE*)getBitsPointer();
    if(rc->bottom <= rc->top) return;
    int offset = (m_height-rc->bottom)*m_pitch;
    DWORD* pStart = (DWORD*)(bits + (offset + rc->left*4));
    DWORD* pEnd = (DWORD*)(bits + (offset + rc->right*4));
    for(int y=rc->bottom-1;y>=rc->top;y--){
        for(DWORD* p=pStart;p<pEnd;p++) *p = color;
        pStart += m_width;
        pEnd += m_width;
    }
}

void CDIB::copyFrom(CDIB* pSrc)
{
    DWORD* src = pSrc->getBitsPointer();
    memcpy(getBitsPointer(), src, m_width*m_height*4);
}

// upside down
void CDIB::Reverse()
{
    DWORD* bits = getBitsPointer();
    DWORD* work = new DWORD[m_width];
    for(int i=0;i<m_height/2;i++){
        DWORD* p1 = &bits[i*m_width];
        DWORD* p2 = &bits[(m_height-1-i)*m_width];
        memcpy(work, p1, m_width*sizeof(DWORD));
        memcpy(p1, p2, m_width*sizeof(DWORD));
        memcpy(p2, work, m_width*sizeof(DWORD));
    }
    delete[] work;
}
