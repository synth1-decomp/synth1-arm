#pragma once

/**
 * @file cdib.h
 * @brief CDIB, a 32-bit device independent bitmap on top of a CGBitmapContext.
 */

/**
 * @brief Device independent bitmap (32bit, bottom-up rows like a Windows DIB).
 *
 * On the Mac the "HBITMAP" is a CGBitmapContext.   sizeof == 32
 *
 * Pixels are DWORDs in the context's RGBA byte order (GetRValue() etc. read the
 * first, second and third byte). Coordinates count y from the top: row y is
 * stored at buffer row (m_height - 1 - y), as in a bottom-up Windows DIB.
 * The low-level copy functions do no clipping; the caller must keep the
 * rectangles inside both bitmaps.
 * @note A bitmap made with create() owns its context and pixel buffer; one
 *       attached with init() does not.
 */
class CDIB {
public:
    /**
     * @brief Creates an empty object with no bitmap attached.
     */
    CDIB();

    /**
     * @brief Releases the context if this object created it, and frees the pixel buffer.
     */
    ~CDIB();

    /**
     * @brief Allocates a width x height bitmap filled with opaque black (0xff000000).
     *
     * Creates an owned RGB context (premultiplied alpha last) on a malloc'ed buffer.
     * @param width     Width in pixels.
     * @param height    Height in pixels.
     */
    void create(int width, int height);

    /**
     * @brief Attaches an existing bitmap context without taking ownership.
     *
     * @param hBmp  A CGBitmapContext (32 bits per pixel).
     * @return Always TRUE.
     */
    BOOL init(HBITMAP hBmp);

    /**
     * @brief Reads one pixel.
     *
     * @param x     Column.
     * @param y     Row, counted from the top.
     * @return The pixel value, or 0 if (x, y) is outside the bitmap.
     */
    DWORD getPixel(int x, int y);

    /**
     * @brief Writes one pixel; does nothing if (x, y) is outside the bitmap.
     *
     * @param x     Column.
     * @param y     Row, counted from the top.
     * @param color Pixel value.
     */
    void setPixel(int x, int y, DWORD color);

    /**
     * @brief Copies a w x h block from pSrc at (sx, sy) to (x, y) in this bitmap.
     *
     * A plain row-by-row memory copy without clipping.
     * @param x     Destination left.
     * @param y     Destination top.
     * @param w     Width in pixels.
     * @param h     Height in pixels.
     * @param pSrc  Source bitmap.
     * @param sx    Source left.
     * @param sy    Source top.
     */
    void bitBlt(int x, int y, int w, int h, CDIB* pSrc, int sx, int sy);

    /**
     * @brief Gets the pixel buffer of the attached context.
     *
     * @return The start of the pixel buffer (the bottom row comes first).
     */
    DWORD* getBitsPointer();

    /**
     * @brief Scales the source rectangle (sx, sy, sw, sh) into (x, y, w, h).
     *
     * Uses halfDown() for an exact 1/2 reduction, doubleUp() for an exact 2x
     * enlargement and stretchAverage() otherwise. Does nothing if w or h is <= 0.
     * @param x     Destination left.
     * @param y     Destination top.
     * @param w     Destination width.
     * @param h     Destination height.
     * @param pSrc  Source bitmap.
     * @param sx    Source left.
     * @param sy    Source top.
     * @param sw    Source width.
     * @param sh    Source height.
     */
    void stretchBlt(int x, int y, int w, int h, CDIB* pSrc, int sx, int sy, int sw, int sh);

    /**
     * @brief Reduces the source by 1/2, each destination pixel the average of 2x2 source pixels.
     *
     * Parameters as for stretchBlt(); sw and sh are not used (the source size is 2w x 2h).
     * Output pixels are opaque.
     * @param x     Destination left.
     * @param y     Destination top.
     * @param w     Destination width.
     * @param h     Destination height.
     * @param pSrc  Source bitmap.
     * @param sx    Source left.
     * @param sy    Source top.
     * @param sw    Source width (not used).
     * @param sh    Source height (not used).
     */
    void halfDown(int x, int y, int w, int h, CDIB* pSrc, int sx, int sy, int sw, int sh);

    /**
     * @brief Enlarges the source by 2, each source pixel becoming a 2x2 block.
     *
     * Parameters as for stretchBlt(); the sw x sh source is written to 2sw x 2sh
     * at (x, y). Does nothing if w or h is <= 0. Pixels are copied unchanged.
     * @param x     Destination left.
     * @param y     Destination top.
     * @param w     Destination width (only checked for <= 0).
     * @param h     Destination height (only checked for <= 0).
     * @param pSrc  Source bitmap.
     * @param sx    Source left.
     * @param sy    Source top.
     * @param sw    Source width.
     * @param sh    Source height.
     */
    void doubleUp(int x, int y, int w, int h, CDIB* pSrc, int sx, int sy, int sw, int sh);

    /**
     * @brief Scales by any ratio using area averaging.
     *
     * Works on a grid of lcm(w, sw) x lcm(h, sh) cells: each destination pixel
     * is the average of the source pixels covering its cells. Parameters as for
     * stretchBlt(). Output pixels are opaque.
     * @param x     Destination left.
     * @param y     Destination top.
     * @param w     Destination width.
     * @param h     Destination height.
     * @param pSrc  Source bitmap.
     * @param sx    Source left.
     * @param sy    Source top.
     * @param sw    Source width.
     * @param sh    Source height.
     * @note Cost grows with the lcm, so coprime sizes are slow.
     */
    void stretchAverage(int x, int y, int w, int h, CDIB* pSrc, int sx, int sy, int sw, int sh);

    /**
     * @brief Draws part of a CGImage into this bitmap with CoreGraphics scaling.
     *
     * Draws the whole image directly when the source rectangle covers it, otherwise
     * a sub-image cut out at (sx, sy, sw, sh). The destination rectangle is in
     * CoreGraphics coordinates of this context.
     * @param x     Destination left.
     * @param y     Destination y (CoreGraphics coordinates).
     * @param w     Destination width.
     * @param h     Destination height.
     * @param hSrc  Source image (a CGImageRef passed as HBITMAP).
     * @param sx    Source left.
     * @param sy    Source top.
     * @param sw    Source width.
     * @param sh    Source height.
     */
    void StretchBlt(int x, int y, int w, int h, HBITMAP hSrc, int sx, int sy, int sw, int sh);

    /**
     * @brief Least common multiple.
     *
     * @param a     First value.
     * @param b     Second value.
     * @return lcm(a, b), or 0 if either is 0.
     */
    int lcm(int a, int b);

    /**
     * @brief Copies a w x h block from pSrc at (sx, sy) to (x, y), skipping pixels equal to transColor.
     *
     * @param x             Destination left.
     * @param y             Destination top.
     * @param pSrc          Source bitmap.
     * @param sx            Source left.
     * @param sy            Source top.
     * @param w             Width in pixels.
     * @param h             Height in pixels.
     * @param transColor    Pixel value that is treated as transparent (exact match).
     */
    void bitTransBlt(int x, int y, CDIB* pSrc, int sx, int sy, int w, int h, DWORD transColor);

    /**
     * @brief Greatest common divisor (by repeated subtraction).
     *
     * @param a     First value.
     * @param b     Second value.
     * @return gcd(a, b), or 0 if either is 0.
     * @note Expects positive arguments.
     */
    int gcd(int a, int b);

    /**
     * @brief Sets every pixel to color.
     *
     * @param color Pixel value (stored as is, including alpha).
     */
    void fillAll(DWORD color);

    /**
     * @brief Fills a rectangle (top/left inclusive, right/bottom exclusive) with color.
     *
     * Does nothing if the rectangle is empty vertically. No clipping is done.
     * @param rc        Rectangle to fill.
     * @param color     Pixel value.
     */
    void fillRect(RECT* rc, DWORD color);

    /**
     * @brief Copies all pixels of pSrc into this bitmap.
     *
     * @param pSrc  Source bitmap; must be at least as large as this one.
     */
    void copyFrom(CDIB* pSrc);

    /**
     * @brief Flips the bitmap upside down in place.
     *
     * @throws std::bad_alloc if the row buffer cannot be allocated.
     */
    void Reverse();

    HBITMAP m_hBmp;     // +0x00 (a CGBitmapContext)
    int m_width;        // +0x08
    int m_height;       // +0x0c
    void* m_pBits;      // +0x10 pixel buffer allocated by create()
    BOOL m_bCreated;    // +0x18 m_hBmp is owned
    int m_pitch;        // +0x1c bytes per row
};
