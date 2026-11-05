#pragma once

/**
 * @file drawgui.h
 * @brief GUI drawing helpers (panels, buttons, LEDs, text) on a CGContext "HDC".
 *
 * Ported from Win32 GDI to CoreGraphics. Colors are COLORREF values; rectangles
 * are RECTs in the context's coordinates. Some routines that relied on GDI
 * bitmap blits (DrawBitmap, DrawLED) draw nothing on the Mac.
 */

class CDIB;

/**
 * @brief "Transparent window" setting ("transparencywin"); nonzero makes some GUI parts semi-transparent.
 */
extern BOOL g_bTransparencyWin;

/**
 * @brief Makes a window semi-transparent (Windows layered window).
 *
 * Not implemented on the Mac.
 * @param hWnd          Window.
 * @param bInvisible    Requested opacity setting (ignored).
 * @return Always FALSE.
 */
BOOL InvisibleWindow(HWND hWnd, int bInvisible);

/**
 * @brief Scales the brightness of a color.
 *
 * @param color     Base color.
 * @param percent   Scale factor in percent (100 = unchanged); each channel is clamped to 0..255.
 * @return The scaled color.
 */
COLORREF CalcColor(COLORREF color, int percent);

/**
 * @brief Draws a rotary knob as a perspective-projected cylinder.
 *
 * The square of radius r around (cx, cy) is projected at depth 0 (base) and
 * -depth (top face) as seen from the viewpoint (vx, vy, vz). The base ellipse is
 * filled with a lighter shade of color (luminance raised by a quarter of the way
 * to maximum), then the top face ellipse with color.
 * @param hdc       Target context.
 * @param cx        Knob center x.
 * @param cy        Knob center y.
 * @param r         Knob radius in pixels.
 * @param depth     Knob height (distance between base and top face).
 * @param vx        Viewpoint x.
 * @param vy        Viewpoint y.
 * @param vz        Viewpoint z (negative; its magnitude is the eye distance).
 * @param color     Top face color.
 */
void DrawRotery(HDC hdc, int cx, int cy, int r, int depth, int vx, int vy, int vz, COLORREF color);

/**
 * @brief Converts an RGB color to hue/luminance/saturation (each 0..240).
 *
 * For grays the saturation is 0 and the hue is 160 (undefined).
 * @param rgb       Color to convert.
 * @param[out] pH   Hue, 0..240.
 * @param[out] pL   Luminance, 0..240.
 * @param[out] pS   Saturation, 0..240.
 */
void RGBtoHLS(COLORREF rgb, BYTE* pH, BYTE* pL, BYTE* pS);

/**
 * @brief Converts hue/luminance/saturation (each 0..240) to an RGB color.
 *
 * @param hue   Hue, 0..240.
 * @param lum   Luminance, 0..240.
 * @param sat   Saturation, 0..240 (0 gives a gray).
 * @return The RGB color.
 */
COLORREF HLStoRGB(WORD hue, WORD lum, WORD sat);

/**
 * @brief Helper of HLStoRGB(): computes one RGB channel (0..240 scale) for a hue.
 *
 * @param n1    Lower magic value.
 * @param n2    Upper magic value.
 * @param hue   Hue for this channel, wrapped into 0..240 when above 240.
 * @return The channel value on the 0..240 scale.
 * @note hue is unsigned, so a "negative" hue (hue - 80 for blue) wraps around
 *       instead of being brought back into range by the < 0 check.
 */
WORD HueToRGB(WORD n1, WORD n2, WORD hue);

/**
 * @brief Moves a rectangle by (x, y).
 *
 * @param[in,out] rc    Rectangle to offset.
 * @param x             Horizontal offset.
 * @param y             Vertical offset.
 */
void RightDownRect(RECT* rc, int x, int y);

/**
 * @brief Sets a rectangle from position and size.
 *
 * @param[out] rc   Rectangle to set (right = x + w, bottom = y + h).
 * @param x         Left.
 * @param y         Top.
 * @param w         Width.
 * @param h         Height.
 */
void MySetRect(RECT* rc, int x, int y, int w, int h);

/**
 * @brief Draws an LED button (LED button bitmaps plus shadow).
 *
 * @param hdc       Target context.
 * @param rc        Button rectangle.
 * @param bOn       Nonzero for the "on" (pushed) state.
 * @param color     Button color used for the shadow.
 * @see DrawButton()
 */
void DrawLEDButton(HDC hdc, RECT* rc, int bOn, COLORREF color);

/**
 * @brief Draws a bitmap button and its shadow.
 *
 * Calls DrawBitmap() with idOn or idOff, then DrawButtonShadow().
 * @param hdc       Target context.
 * @param idOff     Bitmap resource id for the off state.
 * @param idOn      Bitmap resource id for the on state.
 * @param rc        Button rectangle.
 * @param bOn       Nonzero for the "on" (pushed) state.
 * @param color     Button color used for the shadow.
 */
void DrawButton(HDC hdc, int idOff, int idOn, RECT* rc, int bOn, COLORREF color);

/**
 * @brief Draws a push button (push button bitmaps plus shadow).
 *
 * @param hdc       Target context.
 * @param rc        Button rectangle.
 * @param bOn       Nonzero for the pushed state.
 * @param color     Button color used for the shadow.
 * @see DrawButton()
 */
void DrawPushButton(HDC hdc, RECT* rc, int bOn, COLORREF color);

/**
 * @brief Draws a bitmap resource into a rectangle.
 *
 * On the Mac this only loads and releases the bitmap; nothing is drawn.
 * @param hdc   Target context.
 * @param id    Bitmap resource id.
 * @param rc    Destination rectangle.
 */
void DrawBitmap(HDC hdc, int id, RECT* rc);

/**
 * @brief Draws a three-line shadow along the bottom and right edges of a button.
 *
 * Also draws short edge lines left of and above the button. When not pushed the
 * lines are 50%, 75% and 88% of color; when pushed all three use color itself.
 * @param hdc       Target context.
 * @param rc        Button rectangle (the shadow is drawn outside it).
 * @param color     Button color.
 * @param bPushed   Nonzero for the pushed state.
 */
void DrawButtonShadow(HDC hdc, RECT* rc, COLORREF color, int bPushed);

/**
 * @brief Draws a button with a centered text label.
 *
 * On: fills rc with 120% of color and frames it with half that brightness.
 * Off: draws a raised panel (DrawPanel(), depth 2). The label is then drawn
 * transparently and centered, inset by 2 pixels (bottom by 4 pixels when the
 * rectangle is taller than 15 pixels, else 1).
 * @param hdc       Target context.
 * @param rc        Button rectangle.
 * @param str       Label text; may be NULL for no label.
 * @param bOn       Nonzero for the on state.
 * @param reserved  Not used.
 * @param color     Button color.
 * @param textColor Label color.
 * @param ox        Extra left inset of the label in pixels.
 * @param oy        Extra top inset of the label in pixels.
 */
void DrawTextButton(HDC hdc, RECT* rc, char* str, int bOn, int reserved, COLORREF color, COLORREF textColor, int ox, int oy);

/**
 * @brief Fills a rectangle and gives it a raised bevel.
 *
 * Draws up to three highlight lines on the top/left edges (200%, 150%, 125% of
 * color) and up to three shadow lines on the bottom/right edges (50%, 75%, 88%),
 * all inside rc.
 * @param hdc       Target context.
 * @param rc        Panel rectangle.
 * @param color     Panel color.
 * @param depth     Bevel width in lines, 0..3 (0 = flat fill).
 */
void DrawPanel(HDC hdc, RECT* rc, COLORREF color, int depth);

/**
 * @brief Draws text into a rectangle of a bitmap context.
 *
 * Wraps hdc in a CDIB and calls DrawLetterDIB().
 * @param hdc           Target context (must be a CGBitmapContext).
 * @param rc            Text rectangle.
 * @param str           Text (UTF-8).
 * @param bkColor       Background color when not transparent.
 * @param textColor     Text color.
 * @param bCenter       Nonzero to center horizontally.
 * @param bTransparent  Nonzero to keep the existing background.
 * @param style         Bit 1 (value 2): italic.
 * @see DrawLetterDIB()
 */
void DrawLetter(HDC hdc, RECT* rc, char* str, COLORREF bkColor, COLORREF textColor, int bCenter, int bTransparent, int style);

/**
 * @brief Draws a monochrome bitmap resource scaled into a rectangle, recolored.
 *
 * White pixels become color and all other pixels bkColor.
 * @param pDib      Target bitmap.
 * @param rc        Destination rectangle.
 * @param id        Bitmap resource id.
 * @param color     Color for the white pixels.
 * @param bkColor   Color for the other pixels.
 */
void DrawTransparentBmp(CDIB* pDib, RECT* rc, int id, COLORREF color, COLORREF bkColor);

/**
 * @brief Draws a w x h image scaled into a rectangle of a bitmap.
 *
 * @param pDib  Target bitmap.
 * @param hBmp  Source image.
 * @param w     Source width.
 * @param h     Source height.
 * @param rc    Destination rectangle.
 */
void DrawBitmapSub(CDIB* pDib, HBITMAP hBmp, int w, int h, RECT* rc);

/**
 * @brief Draws a monochrome bitmap resource scaled into a rectangle, white pixels transparent.
 *
 * Non-white pixels are drawn in color over the existing contents of rc.
 * @param pDib      Target bitmap.
 * @param rc        Destination rectangle.
 * @param id        Bitmap resource id.
 * @param color     Color for the non-white pixels.
 * @note The overlay step copies a width x width block, so only square bitmaps
 *       are handled fully.
 */
void DrawTransparentBmpTrans(CDIB* pDib, RECT* rc, int id, COLORREF color);

/**
 * @brief Draws one line of text into a rectangle of a CDIB.
 *
 * The text is rendered with CoreText in Lucida Grande at twice the size
 * (font size 0.9 x 2 x rectangle height) into a work bitmap, then reduced by
 * half into pDib. The width is limited to the right edge of pDib. Does nothing
 * for a NULL or empty string.
 * @param pDib          Target bitmap.
 * @param rc            Text rectangle; its height sets the font size.
 * @param str           Text (UTF-8).
 * @param bkColor       Background color when not transparent.
 * @param textColor     Text color.
 * @param bCenter       Nonzero to center horizontally in rc.
 * @param bTransparent  Nonzero to keep the existing background, zero to fill it with bkColor.
 * @param style         Bit 1 (value 2): synthetic italic (14 degree skew).
 */
void DrawLetterDIB(CDIB* pDib, RECT* rc, char* str, COLORREF bkColor, COLORREF textColor, int bCenter, int bTransparent, int style);

/**
 * @brief Draws a bitmap LED (red, green or yellow).
 *
 * On the Mac nothing is drawn (the GDI blit it relies on is not implemented).
 * @param hdc       Target context.
 * @param rc        LED rectangle.
 * @param type      0 = red, 1 = green, other = yellow.
 * @param bOn       Nonzero for the lit state.
 * @param color     Background color for the corner pixels.
 * @see DrawLED2()
 */
void DrawLED(HDC hdc, RECT* rc, int type, int bOn, COLORREF color);

/**
 * @brief Draws the frame of a toggle button.
 *
 * Strokes three rectangles around rc in color. When on, adds lighter edge
 * lines; when off, adds the normal button shadow (DrawButtonShadow()).
 * @param hdc       Target context.
 * @param rc        Button rectangle (the frame is drawn outside it).
 * @param color     Button color.
 * @param bOn       Nonzero for the on state.
 * @note In the on state only the red channel of the lighter lines is brightened;
 *       green and blue keep the values of color.
 */
void DrawToggleButtonShadow(HDC hdc, RECT* rc, COLORREF color, int bOn);

/**
 * @brief Draws a one-pixel 3D frame just outside a rectangle.
 *
 * Uses 125% and 88% of color for the light and dark edges.
 * @param hdc       Target context.
 * @param rc        Framed rectangle.
 * @param color     Base color.
 * @param bSunken   Nonzero for a sunken look (dark top/left, light bottom/right),
 *                  zero for a raised one.
 */
void DrawFrame(HDC hdc, RECT* rc, COLORREF color, int bSunken);

/**
 * @brief Initializes the graphics subsystem; does nothing on the Mac.
 */
void startupGraphics();

/**
 * @brief Shuts down the graphics subsystem; does nothing on the Mac.
 */
void shutdownGraphics();

/**
 * @brief Requests that the LED bitmap cache used by DrawLED2() be freed.
 *
 * Only sets a flag; the cache is emptied on its next access, from the drawing
 * thread.
 */
void freeBitmapCache();

/**
 * @brief Draws an LED, using a cache of rendered images.
 *
 * Images are cached by size, type, state, color and style. With style 0 a red
 * or green ellipse with a black outline is drawn on a color background (dimmed
 * to 35% when off); otherwise half of the led_red/led_green resource image is
 * used (the lower half when on, the upper half when off).
 * @param hdc       Target context.
 * @param rc        LED rectangle.
 * @param type      0 = red, nonzero = green (with style 0, any other value gives a dark LED).
 * @param bOn       Nonzero for the lit state.
 * @param color     Background color.
 * @param style     0 = drawn ellipse, nonzero = resource image.
 */
void DrawLED2(HDC hdc, RECT* rc, int type, int bOn, COLORREF color, int style);
