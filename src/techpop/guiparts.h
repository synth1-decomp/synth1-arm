#pragma once

/**
 * @file guiparts.h
 *
 * GUI widgets of the editor window (view factory only).
 *
 * Declares the widget base class CGuiParts, the concrete widgets (buttons, LEDs, labels, LCD displays,
 * knobs, spin buttons, meters, matrices), the popup windows used by LCDs and knobs, and the
 * CGUICollection container that owns the widgets, renders them into one off-screen panel bitmap and
 * routes mouse messages to them. Widgets report user changes with WM_GUIPARTS_NOTIFY messages.
 */

#include "../windef.h"
#include "cdib.h"
#include "drawgui.h"

class CGUICollection;
class CLCD;
class CButtonGroup;

// Win32 helpers implemented in winobj_au.m (C linkage)
// (also declared and documented in windef.h)
extern "C" {
void    GetClientRectControl(HWND hWnd, RECT* rc);
void    ClientToScreenControl(HWND hWnd, POINT* pt);
void    InvalidateRectControl(HWND hWnd, RECT* rc, int bErase);
void    SetCaptureControl(HWND hWnd);
void    ReleaseCaptureControl();
HDC GetDCControl(HWND hWnd);
void    ReleaseDCControl(HWND hWnd, HDC hdc);
}

// CSynthWin (synwin.h) owns a CGUICollection, so synwin.h includes this
// header; guiparts.cpp includes synwin.h for CSynthWin::convertVal etc.
class CSynthWin;

// messages sent by the widgets
/**
 * @brief Notification sent or posted to the widget's window when the user changes a widget.
 *
 * wParam is the widget id, lParam the new value (its meaning depends on the widget).
 */
#define WM_GUIPARTS_NOTIFY  0x709

// mouse messages
#define WM_MOUSEMOVE        0x200
#define WM_LBUTTONDOWN      0x201
#define WM_LBUTTONUP        0x202
#define WM_RBUTTONUP        0x204
#define WM_MOUSEWHEEL       0x20a

// ---------------------------------------------------------------------------
/**
 * @brief Base class of all editor widgets.
 *
 * A widget has an id, a bounding rectangle in panel coordinates (m_rc), an optional label rectangle
 * (m_rcText), a text and colors. It draws itself into the panel bitmap of its owning CGUICollection
 * and receives mouse events from it. The virtual handlers do nothing by default.
 *
 * @note Widgets are owned by the CGUICollection they were added to, which deletes them.
 */
class CGuiParts {
public:
    virtual ~CGuiParts(){}

    /**
     * @brief Draws the widget.
     *
     * @param hdc   Graphics context to draw into (normally the panel bitmap's context).
     * @param pDib  Panel bitmap behind @p hdc (NULL when a popup is drawn directly into a view).
     * @param bAll  Nonzero for a full redraw (also draws static parts such as labels); zero to draw
     *              only what changed.
     * @return TRUE if anything was drawn and the area must be refreshed on screen. The base version
     *         draws nothing and returns FALSE.
     */
    virtual BOOL drawMe(HDC hdc, CDIB* pDib, int bAll){ return FALSE; }

    /**
     * @brief Hit test.
     *
     * @param x  Horizontal position in panel coordinates (pixels).
     * @param y  Vertical position in panel coordinates (pixels).
     * @return Nonzero if the point lies inside m_rc.
     */
    virtual BOOL PtInRt(int x, int y);

    /**
     * @brief Called when the mouse moves off the widget (another widget or nothing is hovered).
     *
     * Does nothing by default.
     */
    virtual void mouseLeave(){}

    /**
     * @brief Called when the mouse moves over the widget.
     *
     * Does nothing by default.
     *
     * @param x  Horizontal position in panel coordinates (pixels).
     * @param y  Vertical position in panel coordinates (pixels).
     */
    virtual void mouseMove(int x, int y){}

    /**
     * @brief Called when the left button is pressed on the widget.
     *
     * Does nothing by default.
     *
     * @param x  Horizontal position in panel coordinates (pixels).
     * @param y  Vertical position in panel coordinates (pixels).
     */
    virtual void mouseDown(int x, int y){}

    /**
     * @brief Called when the left button is released over the widget.
     *
     * Does nothing by default.
     *
     * @param x  Horizontal position in panel coordinates (pixels).
     * @param y  Vertical position in panel coordinates (pixels).
     */
    virtual void mouseUp(int x, int y){}

    /**
     * @brief Called when the mouse wheel turns over the widget.
     *
     * Does nothing by default.
     *
     * @param delta  Raw message parameter (wheel delta in the high 16 bits, 120 per notch).
     */
    virtual void mouseWheel(int delta){}

    /**
     * @brief Sets the widget's value from the program side (no notification is sent).
     *
     * Does nothing by default.
     *
     * @param val  New value (meaning depends on the widget).
     */
    virtual void setValue(int val){}

    /**
     * @brief Steps the widget to its next value.
     *
     * Does nothing by default.
     */
    virtual void incIndex(){}

    /**
     * @brief Steps the widget to its previous value.
     *
     * Does nothing by default.
     */
    virtual void decIndex(){}

    /**
     * @brief Scales the widget's geometry.
     *
     * Multiplies the position and size of m_rc and m_rcText by @p percent / 100 (integer arithmetic).
     * Called once by CGUICollection::add().
     *
     * @param percent  Scale in percent (100 = unscaled).
     */
    virtual void setCoordinatesScale(int percent);

    /**
     * @brief Sets the value as the result of a user drag (popup slider or vertical drag).
     *
     * Implementations notify the window. Does nothing by default.
     *
     * @param vol  New value.
     */
    virtual void setVol(int vol){}

    /**
     * @brief Called by a child LED (see CLED::setParentGui()) when it is clicked.
     *
     * Does nothing by default.
     *
     * @param id  Id of the clicked LED.
     */
    virtual void notifyClicked(int id){}

    /**
     * @brief Initialises the common widget fields.
     *
     * Sets the window, id and bounding rectangle, copies the text, sets both m_bkColor and m_color to
     * @p bkColor, and clears m_pUser. m_rcText is not touched.
     *
     * @param hWnd       Window (view) that receives notifications and invalidations.
     * @param id         Widget id (0 means the widget never receives mouse events from the collection).
     * @param x          Left edge in panel coordinates (pixels).
     * @param y          Top edge in panel coordinates (pixels).
     * @param w          Width in pixels.
     * @param h          Height in pixels.
     * @param text       Text/label (copied without length check into m_text, 52 bytes); NULL for none.
     * @param bkColor    Background color.
     * @param textColor  Text color.
     */
    void init(HWND hWnd, int id, int x, int y, int w, int h, char* text, COLORREF bkColor, COLORREF textColor);

    /**
     * @brief Redraws the changed parts of the widget and refreshes it on screen.
     *
     * Calls drawMe() with bAll = FALSE into the collection's panel bitmap; if it drew anything, flushes
     * the bitmap context and invalidates m_rc in the window. Does nothing if m_hWnd is NULL.
     */
    void drawMeOwn();

    HWND m_hWnd;            // +0x08
    int m_id;               // +0x10
    RECT m_rc;              // +0x14
    RECT m_rcText;          // +0x24
    char m_text[0x34];      // +0x34
    COLORREF m_color;       // +0x68
    COLORREF m_textColor;   // +0x6c
    COLORREF m_bkColor;     // +0x70
    CGUICollection* m_pGui; // +0x78
    void* m_pUser;          // +0x80
};

// ---------------------------------------------------------------------------
// popup value display shown while dragging
/**
 * @brief Small popup that shows the current value while a control is being dragged.
 *
 * Drawn on top of the panel by CGUICollection::paint() as white Helvetica 16 text on a darkened copy
 * of the panel background, centred horizontally below an anchor point.
 */
class CDisplayArea {
public:
    /**
     * @brief Creates a hidden popup.
     */
    CDisplayArea(){ m_bEnable = 0; }

    /**
     * @brief Attaches the popup to a collection.
     *
     * @param pGui   Owning collection (its window is invalidated on changes).
     * @param hdc    Memory DC of the collection (stored only).
     * @param pDib   Panel bitmap; used to measure text and as the background source.
     * @param color  Ignored; the text color is always white (0xffffff).
     */
    void init(CGUICollection* pGui, HDC hdc, CDIB* pDib, COLORREF color);

    /**
     * @brief Sets the anchor point and clears the text.
     *
     * @param x  Horizontal centre of the popup in panel coordinates.
     * @param y  Vertical anchor; the popup's top edge is placed 3 pixels below it.
     */
    void setDisplayLocation(int x, int y);

    /**
     * @brief Sets the displayed text and recomputes the popup rectangle.
     *
     * Does nothing if the text is unchanged. Measures the text in Helvetica 16, sets m_rc to the text
     * width + 6 by 18 pixels centred on the anchor and, if the popup is shown, invalidates the union
     * of the old and new rectangles.
     *
     * @param text  NUL terminated text (copied without length check into a 256 byte buffer).
     */
    void setText(char* text);

    /**
     * @brief Shows or hides the popup.
     *
     * Call drawUpdate() to refresh the screen.
     *
     * @param bEnable  Nonzero to show, 0 to hide.
     */
    void setEnable(int bEnable);

    /**
     * @brief Invalidates the popup rectangle in the collection's window.
     */
    void drawUpdate();

    /**
     * @brief Tells whether the popup is shown.
     *
     * @return Nonzero if the popup is shown, otherwise 0.
     */
    int isEnabled();

    /**
     * @brief Draws the popup into a view context.
     *
     * When hidden, restores the panel area last covered by the popup and returns. When shown, renders
     * the darkened background (each channel scaled to 5/8, corner pixels kept) and the text, draws it
     * at m_rc offset by (@p ox, @p oy), and remembers the area in m_rcPrev. If the popup shrank, the
     * whole panel is redrawn first to erase the old popup.
     *
     * @param hdc  View graphics context.
     * @param rc   Unused (the caller passes an uninitialised pointer).
     * @param ox   Horizontal offset added to the popup position.
     * @param oy   Vertical offset added to the popup position.
     */
    void paint(HDC hdc, RECT* rc, int ox, int oy);

    CGUICollection* m_pGui; // +0x00
    HDC m_hdc;              // +0x08
    CDIB* m_pDib;           // +0x10
    COLORREF m_color;       // +0x18
    int m_x;                // +0x1c
    int m_y;                // +0x20
    RECT m_rcPrev;          // +0x24  last drawn area
    int m_bEnable;          // +0x34
    char m_text[0x100];     // +0x38
    RECT m_rc;              // +0x138
};                          // sizeof == 0x148

// ---------------------------------------------------------------------------
/**
 * @brief Maximum number of widgets in a CGUICollection.
 */
#define GUIPARTS_MAX    250

/**
 * @brief Owner and renderer of all widgets of the editor panel.
 *
 * Holds up to GUIPARTS_MAX widgets, renders them into an off-screen panel bitmap (m_pDib) that is
 * copied to the view in paint(), routes mouse messages to the widget under the cursor, and manages
 * the popup that currently captures the mouse (list or slider popup), the vertical drag input mode
 * and the value display popup.
 *
 * @note Widgets passed to add() are owned by the collection and deleted by destroyResouce().
 */
class CGUICollection {
public:
    /**
     * @brief Starts up the graphics helpers and initialises an empty collection.
     *
     * @throws std::bad_alloc  If the value display popup cannot be allocated.
     */
    CGUICollection();

    /**
     * @brief Deletes all widgets and resources and shuts down the graphics helpers.
     */
    ~CGUICollection();

    /**
     * @brief Resets the collection to an empty state and allocates a new value display popup.
     *
     * Frees nothing; call destroyResouce() first if resources exist.
     *
     * @throws std::bad_alloc  If the popup cannot be allocated.
     */
    void initResouce();

    /**
     * @brief Deletes the value display popup, the memory DC, the panel and work bitmaps and all widgets.
     */
    void destroyResouce();

    /**
     * @brief Re-reads the "slidertype" setting (default 1).
     *
     * 1 = knobs and LCDs open a popup slider, other values use vertical dragging.
     */
    void changedSliderType();

    /**
     * @brief Returns the shared work bitmap, growing it if needed.
     *
     * If the current work bitmap is smaller than @p w x @p h in either dimension it is recreated at
     * the larger of the old and requested sizes (its contents are lost).
     *
     * @param w  Minimum width in pixels.
     * @param h  Minimum height in pixels.
     * @return The work bitmap (owned by the collection), at least @p w x @p h.
     * @throws std::bad_alloc  If a new bitmap cannot be allocated.
     */
    CDIB* getTempDIB(int w, int h);

    /**
     * @brief (Re)initialises the collection for a window.
     *
     * Destroys all existing widgets and resources, creates the panel bitmap, a memory DC and the value
     * display popup, and paints the panel background.
     *
     * @param hWnd    View that shows the panel.
     * @param w       Panel width in pixels.
     * @param h       Panel height in pixels.
     * @param color   Panel background color.
     * @param scale   GUI scale in percent (applied to widgets as they are added).
     * @param pSynth  The CSynthesizer1 instance (used for value text and MIDI learn).
     * @throws std::bad_alloc  If an allocation fails.
     */
    void init(HWND hWnd, int w, int h, COLORREF color, int scale, void* pSynth);

    /**
     * @brief Adds a widget and takes ownership of it.
     *
     * Sets the widget's m_pGui and scales its geometry by the GUI scale. There is no check against
     * GUIPARTS_MAX.
     *
     * @param pParts  Widget allocated with new; deleted by destroyResouce().
     */
    void add(CGuiParts* pParts);

    /**
     * @brief Moves the lit LED of an LED group to the next LED.
     *
     * Turns off the first lit CLED with an id in [@p from, @p to] and lights the following one,
     * wrapping to @p from after the last one or when none is lit.
     *
     * @param from  First LED id of the group.
     * @param to    Last LED id of the group (inclusive).
     */
    void incrimentLEDGroup(int from, int to);

    /**
     * @brief Finds a widget by id.
     *
     * @param id  Widget id.
     * @return The first widget with that id, or NULL if there is none.
     */
    CGuiParts* getGui(int id);

    /**
     * @brief Moves the lit LED of an LED group to the previous LED.
     *
     * Turns off the first lit CLED with an id in [@p from, @p to] and lights the preceding one,
     * wrapping to @p to before the first one or when none is lit.
     *
     * @param from  First LED id of the group.
     * @param to    Last LED id of the group (inclusive).
     */
    void decrimentLEDGroup(int from, int to);

    /**
     * @brief Returns the value of the lit LED of an LED group.
     *
     * @param from  First LED id of the group.
     * @param to    Last LED id of the group (inclusive).
     * @return m_data of the first lit CLED with an id in [@p from, @p to], or 0 if none is lit.
     */
    int getOnLEDData(int from, int to);

    /**
     * @brief Lights the LEDs of a group whose value equals @p data and turns off the others.
     *
     * @param data  LED value (CLED::m_data) to select.
     * @param from  First LED id of the group.
     * @param to    Last LED id of the group (inclusive).
     */
    void setOnLEDData(int data, int from, int to);

    /**
     * @brief Fully redraws every widget into the panel bitmap and flushes it.
     *
     * The view is not invalidated.
     */
    void drawAll();

    /**
     * @brief Routes a mouse message to the widgets.
     *
     * During a vertical drag the message goes to processDragInput(). If a popup has captured the mouse,
     * a right button release or a left click outside it closes the popup and the message is routed
     * normally; otherwise the popup receives it. Normal routing sends the message to the first widget
     * with a nonzero id under the cursor; the previously hovered widget gets mouseLeave(). While MIDI
     * learn is active a left click selects the clicked parameter (widget id - 1000) as the learn
     * target, or ends learning (without forwarding the click) when the widget is a learn end control.
     *
     * @param msg     WM_MOUSEMOVE, WM_LBUTTONDOWN, WM_LBUTTONUP, WM_RBUTTONUP or WM_MOUSEWHEEL.
     * @param lParam  Cursor position (x in the low 16 bits, y in the high 16 bits, signed, panel
     *                coordinates); for WM_MOUSEWHEEL it is passed unchanged to mouseWheel().
     * @return 1 if a widget or popup received the message, otherwise 0 (processDragInput()'s result
     *         during a drag).
     */
    int processMouseMessage(int msg, LPARAM lParam);

    /**
     * @brief Handles a mouse message during a vertical drag started by startDragInputPart().
     *
     * On WM_LBUTTONUP the drag ends. Otherwise the new value is the start value plus the vertical
     * distance from the start point, scaled so that 128 pixels cover the whole range (up increases),
     * clamped to the range; it updates the value display popup and is passed to the widget's setVol().
     *
     * @param msg     Mouse message.
     * @param lParam  Cursor position (y in the high 16 bits).
     * @return 0 when the drag ended, otherwise 1.
     */
    int processDragInput(int msg, LPARAM lParam);

    /**
     * @brief Sets the value of a widget by id (calls its setValue(); no notification).
     *
     * @param id   Widget id; nothing happens if no widget has this id.
     * @param val  New value.
     */
    void setValue(int id, int val);

    /**
     * @brief Replaces the collection's window, also in every widget that used the old window.
     *
     * @param hWnd  New window (view).
     */
    void changeWindowHandle(HWND hWnd);

    /**
     * @brief Starts a vertical drag input for a widget.
     *
     * Captures the mouse and remembers the start point, range and value; subsequent mouse messages are
     * handled by processDragInput(). If @p bDisplay is set, the value display popup is shown below the
     * widget with the text from CSynthWin::convertVal() (or the plain number).
     *
     * @param pParts    Widget that receives the new values through setVol().
     * @param x         Start position, panel coordinates.
     * @param y         Start position, panel coordinates.
     * @param min       Minimum value.
     * @param max       Maximum value.
     * @param val       Current value.
     * @param bDisplay  Nonzero to show the value display popup.
     */
    void startDragInputPart(CGuiParts* pParts, int x, int y, int min, int max, int val, int bDisplay);

    /**
     * @brief Ends the vertical drag input: releases the mouse capture and hides the value display popup.
     *
     * @param pParts  Unused.
     */
    void stopDragInputPart(CGuiParts* pParts);

    /**
     * @brief Copies part of the panel to a view and draws the popups on top.
     *
     * Copies the panel bitmap area starting at (@p ox, @p oy) into @p rc, then draws the capturing
     * popup (clipped to @p rc) and the value display popup if shown.
     *
     * @param hdc  View graphics context.
     * @param rc   Destination rectangle in view coordinates.
     * @param ox   Left edge of the source area in panel coordinates.
     * @param oy   Top edge of the source area in panel coordinates.
     */
    void paint(HDC hdc, RECT* rc, int ox, int oy);

    CGuiParts* m_pParts[GUIPARTS_MAX];  // +0x000
    int m_num;                          // +0x7d0
    CGuiParts* m_pLastParts;            // +0x7d8
    HWND m_hWnd;                        // +0x7e0
    CDIB* m_pDib;                       // +0x7e8
    HDC m_hdcMem;                       // +0x7f0
    CDIB* m_pTempDib;                   // +0x7f8
    float m_scale;                      // +0x800
    float m_fontSize;                   // +0x804
    int m_scalePercent;                 // +0x808
    void* m_pSynth;                     // +0x810  CSynthesizer1*
    CDisplayArea* m_pDisplay;           // +0x818
    int m_sliderType;                   // +0x820
    CGuiParts* m_pCapture;              // +0x828
    CGuiParts* m_pDragParts;            // +0x830
    int m_dragX;                        // +0x838
    int m_dragY;                        // +0x83c
    int m_dragMin;                      // +0x840
    int m_dragMax;                      // +0x844
    int m_dragVal;                      // +0x848
    int m_bDragDisplay;                 // +0x84c
};

// ---------------------------------------------------------------------------
/**
 * @brief Momentary text push button.
 *
 * Depending on its mode a press posts WM_GUIPARTS_NOTIFY (mode 0, lParam 0), steps an LED group
 * (mode 1, see setLEDGroup()) or steps a target widget up (mode 2) or down (mode 3).
 */
class CPushButton : public CGuiParts {
public:
    /**
     * @brief Creates a push button in mode 0.
     *
     * @param hWnd       Window receiving notifications.
     * @param id         Widget id.
     * @param x          Left edge (pixels).
     * @param y          Top edge (pixels).
     * @param w          Width (pixels).
     * @param h          Height (pixels).
     * @param text       Button caption.
     * @param bkColor    Panel background color (used for the shadow).
     * @param textColor  Caption color.
     * @param color      Button face color.
     */
    CPushButton(HWND hWnd, int id, int x, int y, int w, int h, char* text, COLORREF bkColor, COLORREF textColor, COLORREF color);
    virtual ~CPushButton(){}

    /**
     * @brief Draws the button face and shadow (pushed or released).
     *
     * @param hdc   Graphics context to draw into (the panel bitmap's context).
     * @param pDib  Panel bitmap behind @p hdc.
     * @param bAll  Nonzero for a full redraw (also draws static parts such as labels).
     * @return FALSE without drawing if the button has zero width, otherwise TRUE.
     */
    virtual BOOL drawMe(HDC hdc, CDIB* pDib, int bAll);

    /**
     * @brief Releases the button if it is pushed.
     */
    virtual void mouseLeave();

    /**
     * @brief Pushes the button and performs its action.
     *
     * Mode 1: lights the next LED of the group and posts WM_GUIPARTS_NOTIFY with that LED's value.
     * Mode 2/3: calls incIndex()/decIndex() on the target. Otherwise posts WM_GUIPARTS_NOTIFY with 0.
     *
     * @param x  Horizontal position in panel coordinates (unused).
     * @param y  Vertical position in panel coordinates (unused).
     */
    virtual void mouseDown(int x, int y);

    /**
     * @brief Releases the button (same as mouseLeave()).
     *
     * @param x  Horizontal position in panel coordinates (unused).
     * @param y  Vertical position in panel coordinates (unused).
     */
    virtual void mouseUp(int x, int y);

    /**
     * @brief In LED group mode lights the LED whose value equals @p val; ignored in other modes.
     *
     * @param val  LED value (CLED::m_data) to select.
     */
    virtual void setValue(int val);

    /**
     * @brief Handles a click on one of the group's LEDs (LED group mode only).
     *
     * Lights the clicked LED and posts WM_GUIPARTS_NOTIFY with its value. In a two LED toggle group,
     * clicking the lit LED selects the other one instead.
     *
     * @param id  Id of the clicked LED.
     */
    virtual void notifyClicked(int id);

    /**
     * @brief Sets the button mode and the target widget.
     *
     * @param mode    2 to call @p pParts->incIndex() on press, 3 to call decIndex(); 0 for a plain button.
     * @param pParts  Target widget (usually a CLCD).
     */
    void setLCDMode(int mode, CGuiParts* pParts);

    /**
     * @brief Makes the button step through an LED group (mode 1).
     *
     * Registers the button as parent of the CLEDs with ids @p from..@p to so that clicks on them are
     * reported through notifyClicked(). The LEDs must already be in the collection.
     *
     * @param from     First LED id.
     * @param to       Last LED id (inclusive).
     * @param bToggle  Nonzero to make a two LED group toggle when the lit LED is clicked (ignored for
     *                 groups of other sizes).
     */
    void setLEDGroup(int from, int to, int bToggle);

    int m_bPushed;          // +0x88
    int m_mode;             // +0x8c  1:LED group 2:inc 3:dec
    int m_ledFrom;          // +0x90
    int m_ledTo;            // +0x94
    CGuiParts* m_pTarget;   // +0x98
    int m_bToggle;          // +0xa0
};

/**
 * @brief Invisible widget that makes a set of CToggleButtons act as radio buttons.
 *
 * Each member button is associated with a value; exactly the buttons with the selected value are on.
 * Selecting a button sends WM_GUIPARTS_NOTIFY with the group's id and the button's value.
 */
class CButtonGroup : public CGuiParts {
public:
    /**
     * @brief Creates an empty, zero sized group with the given window and id.
     *
     * @param hWnd  Window receiving notifications.
     * @param id    Group id (sent as wParam of WM_GUIPARTS_NOTIFY).
     */
    CButtonGroup(HWND hWnd, int id);
    virtual ~CButtonGroup(){}

    /**
     * @brief Turns on the member buttons whose value equals @p val and turns off the others (no notification).
     *
     * @param val  Value to select.
     */
    virtual void setValue(int val);

    /**
     * @brief Adds a toggle button to the group.
     *
     * @param id   Id of a CToggleButton already in the collection; ignored if not found.
     * @param val  Value selected by this button. At most 100 buttons (not checked).
     */
    void setButtonVal(int id, int val);

    /**
     * @brief Called by a member button when it is clicked.
     *
     * Ignored if the button is already on; otherwise selects its value and sends WM_GUIPARTS_NOTIFY
     * with the group's id and that value.
     *
     * @param pButton  The clicked CToggleButton.
     */
    void clickedButton(void* pButton);

    CGuiParts* m_pButtons[100]; // +0x088
    int m_vals[100];            // +0x3a8
    int m_num;                  // +0x538
};

/**
 * @brief Push button drawn with the bitmap style button face and a separate label.
 */
class CBMPPushButton : public CPushButton {
public:
    /**
     * @brief Creates the button.
     *
     * @param hWnd       Window receiving notifications.
     * @param id         Widget id.
     * @param x          Left edge (pixels).
     * @param y          Top edge (pixels).
     * @param w          Width (pixels).
     * @param h          Height (pixels).
     * @param text       Label text.
     * @param tx         Label left edge relative to @p x.
     * @param ty         Label top edge relative to @p y.
     * @param tw         Label width.
     * @param th         Label height.
     * @param bkColor    Background color.
     * @param textColor  Label color.
     */
    CBMPPushButton(HWND hWnd, int id, int x, int y, int w, int h, char* text, int tx, int ty, int tw, int th, COLORREF bkColor, COLORREF textColor);
    virtual ~CBMPPushButton(){}

    /**
     * @brief Draws the button face and shadow, and the label when @p bAll is set.
     *
     * @param hdc   Graphics context to draw into (the panel bitmap's context).
     * @param pDib  Panel bitmap behind @p hdc.
     * @param bAll  Nonzero for a full redraw (also draws static parts such as labels).
     * @return TRUE.
     */
    virtual BOOL drawMe(HDC hdc, CDIB* pDib, int bAll);
};

/**
 * @brief On/off button that toggles on each click.
 *
 * A click sends WM_GUIPARTS_NOTIFY with the new state (0/1), or, if the button belongs to a
 * CButtonGroup, lets the group handle the click.
 */
class CToggleButton : public CGuiParts {
public:
    /**
     * @brief Creates the button.
     *
     * @param hWnd       Window receiving notifications.
     * @param id         Widget id.
     * @param x          Left edge (pixels).
     * @param y          Top edge (pixels).
     * @param w          Width (pixels).
     * @param h          Height (pixels).
     * @param bOn        Initial state (nonzero = on).
     * @param text       Caption.
     * @param bkColor    Panel background color.
     * @param textColor  Caption color.
     * @param color      Face color when off (style 0).
     * @param onColor    Face color when on (style 0).
     * @param style      0: plain text button; nonzero: framed button with a gray gradient (yellowish
     *                   when on).
     */
    CToggleButton(HWND hWnd, int id, int x, int y, int w, int h, int bOn, char* text, COLORREF bkColor, COLORREF textColor, COLORREF color, COLORREF onColor, int style);
    virtual ~CToggleButton(){}

    /**
     * @brief Draws the button if its state changed since the last draw.
     *
     * @param hdc   Graphics context to draw into (the panel bitmap's context).
     * @param pDib  Panel bitmap behind @p hdc.
     * @param bAll  Nonzero for a full redraw (also draws static parts such as labels).
     * @return TRUE if drawn, otherwise FALSE.
     */
    virtual BOOL drawMe(HDC hdc, CDIB* pDib, int bAll);

    /**
     * @brief Toggles the state, redraws and sends WM_GUIPARTS_NOTIFY with the new state.
     *
     * In a group, calls CButtonGroup::clickedButton() instead.
     *
     * @param x  Horizontal position in panel coordinates (unused).
     * @param y  Vertical position in panel coordinates (unused).
     */
    virtual void mouseDown(int x, int y);

    /**
     * @brief Sets the state and redraws, without notification.
     *
     * @param val  Nonzero = on.
     */
    virtual void setValue(int val);

    int m_bOn;              // +0x88
    int m_bDrawnOn;         // +0x8c
    int m_style;            // +0x90
    COLORREF m_onColor;     // +0x94
    CButtonGroup* m_pGroup; // +0x98
};

/**
 * @brief Toggle button with an LED at its left edge showing the state.
 *
 * A click toggles the state and posts WM_GUIPARTS_NOTIFY with the new state (0/1); the button face
 * looks pushed while the mouse button is held.
 */
class CLEDToggleButton : public CGuiParts {
public:
    /**
     * @brief Creates the button.
     *
     * @param hWnd       Window receiving notifications.
     * @param id         Widget id.
     * @param x          Left edge (pixels).
     * @param y          Top edge (pixels).
     * @param w          Width (pixels).
     * @param h          Height (pixels).
     * @param bOn        Initial state (nonzero = on).
     * @param text       Caption.
     * @param bkColor    Panel background color.
     * @param textColor  Caption color.
     * @param color      Button face color.
     * @param ledType    LED style passed to DrawLED().
     * @param ledSize    LED size in pixels (before scaling).
     */
    CLEDToggleButton(HWND hWnd, int id, int x, int y, int w, int h, int bOn, char* text, COLORREF bkColor, COLORREF textColor, COLORREF color, int ledType, int ledSize);

    /**
     * @brief Draws the button and LED if the state or pushed state changed.
     *
     * @param hdc   Graphics context to draw into (the panel bitmap's context).
     * @param pDib  Panel bitmap behind @p hdc.
     * @param bAll  Nonzero for a full redraw (also draws static parts such as labels).
     * @return TRUE if drawn, otherwise FALSE.
     */
    virtual BOOL drawMe(HDC hdc, CDIB* pDib, int bAll);

    /**
     * @brief Releases the pushed look.
     */
    virtual void mouseLeave();

    /**
     * @brief Toggles the state, shows the button pushed and posts WM_GUIPARTS_NOTIFY with the new state.
     *
     * @param x  Horizontal position in panel coordinates (unused).
     * @param y  Vertical position in panel coordinates (unused).
     */
    virtual void mouseDown(int x, int y);

    /**
     * @brief Releases the pushed look (same as mouseLeave()).
     *
     * @param x  Horizontal position in panel coordinates (unused).
     * @param y  Vertical position in panel coordinates (unused).
     */
    virtual void mouseUp(int x, int y);

    /**
     * @brief Sets the state and redraws, without notification.
     *
     * @param val  Nonzero = on.
     */
    virtual void setValue(int val);

    /**
     * @brief Scales the geometry and the LED size.
     *
     * The LED size is capped at 10 pixels.
     *
     * @param percent  Scale in percent (100 = unscaled).
     */
    virtual void setCoordinatesScale(int percent);

    int _reserved88;        // +0x88
    int m_bPushed;          // +0x8c
    int m_bOn;              // +0x90
    int m_bDrawnPushed;     // +0x94
    int m_bDrawnOn;         // +0x98
    int m_ledType;          // +0x9c
    int m_ledSize;          // +0xa0
};

/**
 * @brief Toggle button drawn as an LED style button face with a separate label.
 */
class CBMPToggleButton : public CToggleButton {
public:
    /**
     * @brief Creates the button (plain style, no group).
     *
     * @param hWnd       Window receiving notifications.
     * @param id         Widget id.
     * @param x          Left edge (pixels).
     * @param y          Top edge (pixels).
     * @param w          Width (pixels).
     * @param h          Height (pixels).
     * @param bOn        Initial state (nonzero = on).
     * @param text       Label text.
     * @param tx         Label left edge relative to @p x.
     * @param ty         Label top edge relative to @p y.
     * @param tw         Label width.
     * @param th         Label height.
     * @param bkColor    Background color.
     * @param textColor  Label color.
     */
    CBMPToggleButton(HWND hWnd, int id, int x, int y, int w, int h, int bOn, char* text, int tx, int ty, int tw, int th, COLORREF bkColor, COLORREF textColor);

    /**
     * @brief Draws the button if its state changed, and the label when @p bAll is set.
     *
     * @param hdc   Graphics context to draw into (the panel bitmap's context).
     * @param pDib  Panel bitmap behind @p hdc.
     * @param bAll  Nonzero for a full redraw (also draws static parts such as labels).
     * @return TRUE only if the button was drawn.
     */
    virtual BOOL drawMe(HDC hdc, CDIB* pDib, int bAll);
};

/**
 * @brief Single LED with a text or bitmap label, used as one choice of an LED group.
 *
 * The LED does nothing on its own: a click is forwarded to its parent widget (see setParentGui(),
 * normally a CPushButton in LED group mode), which decides which LED is lit. The label next to the
 * LED is part of the clickable area.
 */
class CLED : public CGuiParts {
public:
    /**
     * @brief Creates the LED; the clickable label width is estimated from the text length.
     *
     * @param hWnd       Window.
     * @param id         Widget id.
     * @param data       Value the LED stands for (reported by the parent when selected).
     * @param x          Left edge (pixels).
     * @param y          Top edge (pixels).
     * @param w          LED width (pixels); 0 for a label without LED.
     * @param h          LED height (pixels).
     * @param type       LED style passed to DrawLED2().
     * @param bOn        Initial state (nonzero = lit).
     * @param text       Label text.
     * @param bmpId      Label bitmap resource id, or 0 to draw @p text.
     * @param tx         Label left edge relative to @p x.
     * @param ty         Label top edge relative to @p y.
     * @param tw         Label width.
     * @param th         Label height.
     * @param bkColor    Background color.
     * @param textColor  Label color.
     */
    CLED(HWND hWnd, int id, int data, int x, int y, int w, int h, int type, int bOn, char* text, int bmpId, int tx, int ty, int tw, int th, COLORREF bkColor, COLORREF textColor);

    /**
     * @brief Creates the LED with an explicit label length for the clickable area.
     *
     * Same as the other constructor, but the clickable label width is @p textLen characters of 3/5
     * of @p th pixels each.
     *
     * @param hWnd       Window.
     * @param id         Widget id.
     * @param data       Value the LED stands for (reported by the parent when selected).
     * @param x          Left edge (pixels).
     * @param y          Top edge (pixels).
     * @param w          LED width (pixels); 0 for a label without LED.
     * @param h          LED height (pixels).
     * @param type       LED style passed to DrawLED2().
     * @param bOn        Initial state (nonzero = lit).
     * @param text       Label text.
     * @param bmpId      Label bitmap resource id, or 0 to draw @p text.
     * @param tx         Label left edge relative to @p x.
     * @param ty         Label top edge relative to @p y.
     * @param tw         Label width.
     * @param th         Label height.
     * @param bkColor    Background color.
     * @param textColor  Label color.
     * @param textLen    Label length in characters.
     */
    CLED(HWND hWnd, int id, int data, int x, int y, int w, int h, int type, int bOn, char* text, int bmpId, int tx, int ty, int tw, int th, COLORREF bkColor, COLORREF textColor, int textLen);

    /**
     * @brief Draws the LED if its state changed (and it has a width), and the label when @p bAll is set.
     *
     * @param hdc   Graphics context to draw into (the panel bitmap's context).
     * @param pDib  Panel bitmap behind @p hdc.
     * @param bAll  Nonzero for a full redraw (also draws static parts such as labels).
     * @return TRUE only if the LED was drawn.
     */
    virtual BOOL drawMe(HDC hdc, CDIB* pDib, int bAll);

    /**
     * @brief Hit test covering the LED and its label.
     *
     * The LED rectangle is extended down to the label's bottom edge and right by the clickable label
     * width (left instead if that width is negative).
     *
     * @param x  Horizontal position in panel coordinates (pixels).
     * @param y  Vertical position in panel coordinates (pixels).
     * @return Nonzero if (@p x, @p y) lies inside the extended rectangle.
     */
    virtual BOOL PtInRt(int x, int y);

    /**
     * @brief Reports the click to the parent widget through notifyClicked(m_id), if a parent is set.
     *
     * @param x  Horizontal position in panel coordinates (unused).
     * @param y  Vertical position in panel coordinates (unused).
     */
    virtual void mouseDown(int x, int y);

    /**
     * @brief Scales the geometry and the clickable label width.
     *
     * @param percent  Scale in percent (100 = unscaled).
     */
    virtual void setCoordinatesScale(int percent);

    /**
     * @brief Lights or turns off the LED and redraws it.
     *
     * @param bOn  Nonzero to light, 0 to turn off.
     */
    void setState(int bOn);

    /**
     * @brief Sets the widget that receives this LED's clicks.
     *
     * @param pParent  Parent widget (its notifyClicked() is called); NULL for none.
     */
    void setParentGui(CGuiParts* pParent);

    int m_bOn;              // +0x88
    int m_bDrawnOn;         // +0x8c
    int m_type;             // +0x90
    int m_data;             // +0x94
    int m_bmpId;            // +0x98  label bitmap (0: text)
    int m_textW;            // +0x9c  extra clickable width (label)
    CGuiParts* m_pParent;   // +0xa0
};

/**
 * @brief Static text on a filled background.
 */
class CLabel : public CGuiParts {
public:
    /**
     * @brief Creates the label.
     *
     * @param hWnd       Window.
     * @param id         Widget id.
     * @param x          Left edge (pixels).
     * @param y          Top edge (pixels).
     * @param w          Width (pixels).
     * @param h          Height (pixels).
     * @param text       Text.
     * @param bkColor    Background color.
     * @param textColor  Text color.
     * @param style      Text style passed to DrawLetter().
     */
    CLabel(HWND hWnd, int id, int x, int y, int w, int h, char* text, COLORREF bkColor, COLORREF textColor, int style);
    virtual ~CLabel(){}

    /**
     * @brief Fills the background and draws the text.
     *
     * @param hdc   Graphics context to draw into (the panel bitmap's context).
     * @param pDib  Panel bitmap behind @p hdc.
     * @param bAll  Nonzero for a full redraw (also draws static parts such as labels).
     * @return TRUE.
     */
    virtual BOOL drawMe(HDC hdc, CDIB* pDib, int bAll);

    /**
     * @brief Replaces the text (truncated to 49 characters) and redraws.
     *
     * @param text  New text (at most 255 characters, not checked); NULL keeps the current text and
     *              only redraws.
     */
    void setText(char* text);

    int m_style;            // +0x88
};

/**
 * @brief Horizontal bar of LED segments showing a level.
 */
class CLedMeter : public CGuiParts {
public:
    /**
     * @brief Creates the meter with all segments off.
     *
     * @param hWnd       Window.
     * @param id         Widget id.
     * @param x          Left edge (pixels).
     * @param y          Top edge (pixels).
     * @param w          Width (pixels).
     * @param h          Height (pixels).
     * @param text       Text (not drawn).
     * @param bkColor    Background color (segment frames).
     * @param textColor  Text color (unused).
     * @param num        Number of segments.
     */
    CLedMeter(HWND hWnd, int id, int x, int y, int w, int h, char* text, COLORREF bkColor, COLORREF textColor, int num);
    virtual ~CLedMeter(){}

    /**
     * @brief Draws the meter.
     *
     * Draws the segment frames when @p bAll is set, then the lit segments in red and the others in gray.
     *
     * @param hdc   Graphics context to draw into (the panel bitmap's context).
     * @param pDib  Panel bitmap behind @p hdc.
     * @param bAll  Nonzero for a full redraw (also draws static parts such as labels).
     * @return TRUE.
     */
    virtual BOOL drawMe(HDC hdc, CDIB* pDib, int bAll);

    /**
     * @brief Sets the number of lit segments and redraws.
     *
     * @param val  Lit segments, 0..number of segments.
     */
    virtual void setValue(int val);

    int m_num;          // +0x88
    int m_value;        // +0x8c
};

/**
 * @brief Decorative sunken 3D frame.
 */
class C3DFrame : public CGuiParts {
public:
    /**
     * @brief Creates a sunken frame.
     *
     * @param hWnd   Window.
     * @param id     Widget id.
     * @param x      Left edge (pixels).
     * @param y      Top edge (pixels).
     * @param w      Width (pixels).
     * @param h      Height (pixels).
     * @param color  Base color; the frame is drawn in shades of it.
     */
    C3DFrame(HWND hWnd, int id, int x, int y, int w, int h, COLORREF color);

    /**
     * @brief Draws the frame when @p bAll is set.
     *
     * @param hdc   Graphics context to draw into (the panel bitmap's context).
     * @param pDib  Panel bitmap behind @p hdc.
     * @param bAll  Nonzero for a full redraw (also draws static parts such as labels).
     * @return Always FALSE.
     */
    virtual BOOL drawMe(HDC hdc, CDIB* pDib, int bAll);
};

/**
 * @brief Decorative straight line.
 */
class CLine : public CGuiParts {
public:
    /**
     * @brief Creates a line from (@p x, @p y) to (@p x + @p w, @p y + @p h).
     *
     * @param hWnd   Window.
     * @param id     Widget id.
     * @param x      Start x (pixels).
     * @param y      Start y (pixels).
     * @param w      Horizontal extent (pixels).
     * @param h      Vertical extent (pixels).
     * @param color  Line color.
     */
    CLine(HWND hWnd, int id, int x, int y, int w, int h, COLORREF color);

    /**
     * @brief Draws the line when @p bAll is set.
     *
     * @param hdc   Graphics context to draw into (the panel bitmap's context).
     * @param pDib  Panel bitmap behind @p hdc.
     * @param bAll  Nonzero for a full redraw (also draws static parts such as labels).
     * @return Always FALSE.
     */
    virtual BOOL drawMe(HDC hdc, CDIB* pDib, int bAll);

    /**
     * @brief Scales both end points of the line (and m_rcText) by @p percent / 100.
     *
     * @param percent  Scale in percent (100 = unscaled).
     */
    virtual void setCoordinatesScale(int percent);
};                  // sizeof == 0x88

// ---------------------------------------------------------------------------
// Win32 rectangle helpers on top of QuickDraw style Rects
#ifndef INTERSECTRECT_DEFINED
#define INTERSECTRECT_DEFINED
// defined in guiparts.cpp

/**
 * @brief Tests whether a rectangle is empty.
 *
 * @param rc  Rectangle.
 * @return TRUE only if both the width and the height are zero.
 * @note Unlike the Win32 function, a rectangle with only one zero (or a negative) dimension is not
 *       considered empty.
 */
BOOL IsRectEmpty(const RECT* rc);

/**
 * @brief Intersects two QuickDraw rectangles.
 *
 * @param a       First rectangle.
 * @param b       Second rectangle.
 * @param[out] d  Receives the intersection; not written when the rectangles are disjoint.
 * @return FALSE if the rectangles are disjoint; TRUE otherwise (also when they only share an edge,
 *         giving an empty intersection).
 */
BOOL SectRect(Rect* a, Rect* b, Rect* d);

/**
 * @brief Win32 style rectangle intersection implemented with SectRect().
 *
 * @param a       First rectangle.
 * @param b       Second rectangle.
 * @param[out] d  Receives the intersection. Not written if either rectangle is empty (see
 *                IsRectEmpty()); receives undefined values if the rectangles are disjoint.
 * @return TRUE if the rectangles intersect, otherwise FALSE.
 */
BOOL IntersectRect(const RECT* a, const RECT* b, RECT* d);
#endif

// ---------------------------------------------------------------------------
/**
 * @brief LCD style display that shows a text, a number, or one entry of a text or bitmap list.
 *
 * Display types (m_type): 0 = fixed text (m_text), 1 = number in [m_min, m_max] formatted with
 * m_format, 2 = entry of a text list, 3 = entry of a bitmap list. Lists pair each entry with a value
 * (m_vals). Click modes (m_clickMode): 0 = send WM_GUIPARTS_NOTIFY with the click position,
 * 1 = edit a number with the popup slider or vertical drag, 2 = choose a list entry from a popup list.
 * User changes send WM_GUIPARTS_NOTIFY with the number or the selected entry's value.
 */
class CLCD : public CLabel {
public:
    /**
     * @brief Creates the display.
     *
     * @param hWnd       Window receiving notifications.
     * @param id         Widget id.
     * @param x          Left edge (pixels).
     * @param y          Top edge (pixels).
     * @param w          Width (pixels).
     * @param h          Height (pixels).
     * @param text       Text for type 0.
     * @param bkColor    Background color.
     * @param textColor  Text color (also tints the glowing background).
     * @param clickMode  0: notify click, 1: value popup/drag (type 1 only), 2: popup list.
     * @param type       0: text, 1: number, 2: text list, 3: bitmap list.
     */
    CLCD(HWND hWnd, int id, int x, int y, int w, int h, char* text, COLORREF bkColor, COLORREF textColor, int clickMode, int type);

    /**
     * @brief Draws the display into the panel bitmap @p pDib.
     *
     * The first call renders the glowing background into m_dibBg. Later calls draw only when the
     * displayed text, number or list entry changed.
     *
     * @param hdc   Graphics context to draw into (the panel bitmap's context).
     * @param pDib  Panel bitmap the display is drawn into.
     * @param bAll  Unused; the display draws itself only when its content changed.
     * @return TRUE if drawn, FALSE if nothing changed.
     */
    virtual BOOL drawMe(HDC hdc, CDIB* pDib, int bAll);

    /**
     * @brief Handles a click according to the click mode.
     *
     * Mode 0 sends WM_GUIPARTS_NOTIFY with MAKELONG(@p x, @p y). Mode 1 (type 1) opens the shared
     * popup slider to the right of the display, or starts a vertical drag when the slider type is not
     * 1. Mode 2 opens the shared popup list to the right of the display, sized for the entries.
     *
     * @param x  Horizontal click position in panel coordinates.
     * @param y  Vertical click position in panel coordinates.
     */
    virtual void mouseDown(int x, int y);

    /**
     * @brief Sets the displayed value without notification.
     *
     * Types 2/3 select the entry whose value equals @p val (unchanged if none matches); type 1 sets the
     * number clamped to [m_min, m_max]. Ignored for type 0.
     *
     * @param val  Number (type 1) or entry value (types 2/3).
     */
    virtual void setValue(int val);

    /**
     * @brief Selects the next list entry (wrapping) or increments the number (clamped).
     *
     * Redraws and sends WM_GUIPARTS_NOTIFY (for numbers only if the value changed).
     */
    virtual void incIndex();

    /**
     * @brief Selects the previous list entry (wrapping) or decrements the number (clamped).
     *
     * Redraws and sends WM_GUIPARTS_NOTIFY (for numbers only if the value changed).
     */
    virtual void decIndex();

    /**
     * @brief Sets the value like setValue() and sends WM_GUIPARTS_NOTIFY with the resulting number or entry value.
     *
     * Nothing is sent for type 0.
     *
     * @param vol  New number (type 1) or entry value (types 2/3).
     */
    virtual void setVol(int vol);

    /**
     * @brief Sets the text list (type 2) and selects its first entry.
     *
     * The arrays are not copied and must stay valid. Also computes the longest text length (used to
     * size the popup list).
     *
     * @param vals   Value of each entry.
     * @param texts  Text of each entry.
     * @param num    Number of entries.
     * @param rows   Number of rows of the popup list (entries are laid out column by column).
     */
    void setValsAndTexts(int* vals, char** texts, int num, int rows);

    /**
     * @brief Sets the bitmap list (type 3) and selects its first entry.
     *
     * The arrays are not copied and must stay valid.
     *
     * @param vals  Value of each entry.
     * @param bmps  Bitmap resource id of each entry.
     * @param num   Number of entries.
     * @param rows  Number of rows of the popup list.
     */
    void setValsAndBmp(int* vals, int* bmps, int num, int rows);

    /**
     * @brief Sets the range, value and format of a numeric display (type 1).
     *
     * @param min     Minimum value.
     * @param max     Maximum value.
     * @param val     Current value (not clamped here).
     * @param format  printf format with one int conversion (copied, at most 31 characters, not checked).
     */
    void setNumericRange(int min, int max, int val, char* format);

    int m_bInitBg;          // +0x8c
    CDIB m_dibBg;           // +0x90
    int m_clickMode;        // +0xb0  0:notify 1:value 2:list
    int m_type;             // +0xb4  0:text 1:numeric 2:texts 3:bitmaps
    int* m_vals;            // +0xb8
    char** m_texts;         // +0xc0
    int m_rows;             // +0xc8
    int m_maxLen;           // +0xcc
    char m_lastText[0x38];  // +0xd0
    int* m_bmps;            // +0x108
    int m_num;              // +0x110
    int m_index;            // +0x114
    int m_lastIndex;        // +0x118
    char m_format[0x20];    // +0x11c
    int m_value;            // +0x13c
    int m_lastValue;        // +0x140
    int m_min;              // +0x144
    int m_max;              // +0x148
};

// popup list of a CLCD
/**
 * @brief Popup list for choosing an entry of a CLCD text or bitmap list.
 *
 * One shared instance exists; it is installed as the collection's capturing widget while open. The
 * entries are laid out in a grid, column by column, with the hovered entry highlighted and the
 * current entry drawn in blue (text) or red (bitmap). Clicking inside applies the hovered entry and
 * closes the popup.
 */
class CLCDSubWin : public CGuiParts {
public:
    /**
     * @brief Creates an unattached popup.
     */
    CLCDSubWin();
    virtual ~CLCDSubWin();

    /**
     * @brief Draws all entries with the popup transparency.
     *
     * @param hdc   Graphics context to draw into.
     * @param pDib  Panel bitmap behind @p hdc (unused).
     * @param bAll  Unused; the popup is always drawn completely.
     * @return TRUE.
     */
    virtual BOOL drawMe(HDC hdc, CDIB* pDib, int bAll);

    /**
     * @brief Does nothing.
     */
    virtual void mouseLeave();

    /**
     * @brief Updates the hovered entry and invalidates the popup when it changes.
     *
     * The hovered entry becomes -1 when the point is outside the grid or past the last entry.
     *
     * @param x  Horizontal position in panel coordinates (pixels).
     * @param y  Vertical position in panel coordinates (pixels).
     */
    virtual void mouseMove(int x, int y);

    /**
     * @brief Handles a click inside the popup.
     *
     * Applies the hovered entry's value through CLCD::setVol() (which notifies), moves the cursor back
     * to the centre of the LCD and closes the popup. Clicks outside are handled by the collection.
     *
     * @param x  Horizontal position in panel coordinates (pixels).
     * @param y  Vertical position in panel coordinates (pixels).
     */
    virtual void mouseDown(int x, int y);

    /**
     * @brief Does nothing.
     *
     * @param x  Horizontal position in panel coordinates (unused).
     * @param y  Vertical position in panel coordinates (unused).
     */
    virtual void mouseUp(int x, int y);

    /**
     * @brief Prepares the popup for an LCD.
     *
     * Starts with the LCD's current entry highlighted and sets the transparency (0.75 alpha when
     * transparent popups are enabled, otherwise opaque).
     *
     * @param pLCD    LCD whose list is shown.
     * @param margin  Margin around the grid in pixels.
     * @param rows    Number of rows.
     */
    void init(CLCD* pLCD, int margin, int rows);

    /**
     * @brief Stores the font size.
     *
     * The entries are currently always drawn in 16 point Helvetica.
     *
     * @param size  Font size in points.
     */
    void setFontSize(float size);

    /**
     * @brief Opens the popup at the given rectangle.
     *
     * Sets the popup geometry, computes the grid cell size, invalidates the area and moves the mouse
     * cursor to the centre of the highlighted entry.
     *
     * @param hWnd     Window of the panel.
     * @param x        Left edge in panel coordinates.
     * @param y        Top edge in panel coordinates.
     * @param w        Width in pixels.
     * @param h        Height in pixels.
     * @param lpParam  Unused.
     */
    void create(HWND hWnd, int x, int y, int w, int h, void* lpParam);

    /**
     * @brief Computes the rectangle of an entry.
     *
     * @param index    Entry index (column = index / rows, row = index % rows).
     * @param[out] rc  Receives the cell rectangle in panel coordinates.
     */
    void getItemRect(int index, RECT* rc);

    /**
     * @brief Does nothing.
     */
    void createFont();

    /**
     * @brief Draws every entry box.
     *
     * @param hdc  Graphics context.
     */
    void paint(HDC hdc);

    /**
     * @brief Draws one entry box.
     *
     * @param hdc        Graphics context.
     * @param index      Entry index.
     * @param bSelected  Nonzero to draw the hovered (gray) background.
     */
    void drawItemBox(HDC hdc, int index, int bSelected);

    /**
     * @brief Finds the grid cell under a point.
     *
     * @param pt  Point in panel coordinates.
     * @return Cell index, or -1 if the point lies outside the grid. The index may be past the last
     *         entry for empty cells of the last column.
     */
    int getItemFromPt(POINT* pt);

    CLCD* m_pLCD;           // +0x88
    int m_index;            // +0x90
    int m_rows;             // +0x94
    int m_cols;             // +0x98
    int m_marginX;          // +0x9c
    int m_marginY;          // +0xa0
    int m_itemW;            // +0xa4
    int m_itemH;            // +0xa8
    float m_fontSize;       // +0xac
    float m_alpha;          // +0xb0
};

// popup vertical slider of a CLCD
/**
 * @brief Popup vertical slider used to edit a knob (CRoteryV) or numeric CLCD.
 *
 * One shared instance exists; it is installed as the collection's capturing widget while open. The
 * bar shows the value (bottom = minimum, top = maximum) with the value text rotated along it. Each
 * change is passed to the owner's setVol(), which notifies the window.
 */
class CVolSubWin : public CGuiParts {
public:
    /**
     * @brief Creates an unattached slider.
     */
    CVolSubWin();

    /**
     * @brief Draws the slider with the popup transparency.
     *
     * @param hdc   Graphics context to draw into.
     * @param pDib  Panel bitmap behind @p hdc (unused).
     * @param bAll  Unused; the popup is always drawn completely.
     * @return TRUE.
     */
    virtual BOOL drawMe(HDC hdc, CDIB* pDib, int bAll);

    /**
     * @brief Does nothing.
     */
    virtual void mouseLeave();

    /**
     * @brief While dragging, sets the value from the vertical position and passes changes to the owner.
     *
     * The first move after create() is ignored (it is caused by the cursor being repositioned).
     *
     * @param x  Horizontal position in panel coordinates (unused).
     * @param y  Vertical position in panel coordinates (pixels).
     */
    virtual void mouseMove(int x, int y);

    /**
     * @brief Starts dragging (and applies the position) when the click is inside the slider, otherwise stops dragging.
     *
     * @param x  Horizontal position in panel coordinates (pixels).
     * @param y  Vertical position in panel coordinates (pixels).
     */
    virtual void mouseDown(int x, int y);

    /**
     * @brief Stops dragging.
     *
     * @param x  Horizontal position in panel coordinates (unused).
     * @param y  Vertical position in panel coordinates (unused).
     */
    virtual void mouseUp(int x, int y);

    /**
     * @brief Changes the value by one step per wheel notch (clamped) and passes it to the owner.
     *
     * @param delta  Raw message parameter; the high 16 bits hold the wheel delta (120 per notch).
     */
    virtual void mouseWheel(int delta);

    /**
     * @brief Sets range, value and target before opening.
     *
     * @param min  Minimum value.
     * @param max  Maximum value.
     * @param val  Current value.
     * @param id   Id of the edited widget (used to format the value text with CSynthWin::convertVal()).
     */
    void init(int min, int max, int val, int id);

    /**
     * @brief Opens the slider at the given rectangle and starts dragging.
     *
     * Moves the mouse cursor to the slider's right edge at the height of the current value.
     *
     * @param hWnd     Window of the panel.
     * @param x        Left edge in panel coordinates.
     * @param y        Top edge in panel coordinates.
     * @param w        Width in pixels.
     * @param h        Height in pixels.
     * @param lpParam  Owner widget whose setVol() receives the values (stored as a CLCD*, but a
     *                 CRoteryV is passed as well; only virtual calls and base members are used).
     */
    void create(HWND hWnd, int x, int y, int w, int h, void* lpParam);

    /**
     * @brief Draws the bar, the rotated value text and the border.
     *
     * @param hdc  Graphics context.
     */
    void paint(HDC hdc);

    int m_max;              // +0x88
    int m_min;              // +0x8c
    int m_value;            // +0x90
    int m_startValue;       // +0x94
    int m_targetId;         // +0x98
    int m_bDragging;        // +0x9c
    CLCD* m_pLCD;           // +0xa0
    int m_bFirst;           // +0xa8
    float m_alpha;          // +0xac
};

#define CSPIN_DECLARED

/**
 * @brief Up/down spin button pair, optionally driving a CLCD.
 *
 * Without an LCD it keeps its own value in [m_min, m_max] and sends WM_GUIPARTS_NOTIFY when it
 * changes; with an LCD it calls the LCD's incIndex()/decIndex().
 */
class CSpin : public CGuiParts {
public:
    /**
     * @brief Creates the spin buttons.
     *
     * @param hWnd       Window receiving notifications.
     * @param id         Widget id.
     * @param x          Left edge (pixels).
     * @param y          Top edge (pixels).
     * @param w          Width (pixels).
     * @param h          Height (pixels).
     * @param text       Label text.
     * @param bVertical  0: left half = down, right half = up; nonzero: top half = up, bottom half = down.
     * @param tx         Label left edge relative to @p x.
     * @param ty         Label top edge relative to @p y.
     * @param tw         Label width.
     * @param th         Label height.
     * @param bkColor    Background color.
     * @param textColor  Label color.
     * @param color      Button face color.
     */
    CSpin(HWND hWnd, int id, int x, int y, int w, int h, char* text, int bVertical, int tx, int ty, int tw, int th, COLORREF bkColor, COLORREF textColor, COLORREF color);

    /**
     * @brief Draws both buttons, and the label when @p bAll is set.
     *
     * @param hdc   Graphics context to draw into (the panel bitmap's context).
     * @param pDib  Panel bitmap behind @p hdc.
     * @param bAll  Nonzero for a full redraw (also draws static parts such as labels).
     * @return TRUE.
     */
    virtual BOOL drawMe(HDC hdc, CDIB* pDib, int bAll);

    /**
     * @brief Releases the pushed button.
     */
    virtual void mouseLeave();

    /**
     * @brief Pushes the up or down half and steps the value.
     *
     * Without an LCD the value changes by one (clamped) and WM_GUIPARTS_NOTIFY is sent if it changed.
     * With an LCD the LCD is stepped instead.
     *
     * @param x  Horizontal position in panel coordinates (pixels).
     * @param y  Vertical position in panel coordinates (pixels).
     * @note In horizontal mode an up click with an LCD calls CLCD::incIndex() twice.
     */
    virtual void mouseDown(int x, int y);

    /**
     * @brief Releases the pushed button (same as mouseLeave()).
     *
     * @param x  Horizontal position in panel coordinates (unused).
     * @param y  Vertical position in panel coordinates (unused).
     */
    virtual void mouseUp(int x, int y);

    /**
     * @brief Sets the value (not clamped) and forwards it to the LCD's setValue() if one is set.
     *
     * No notification is sent.
     *
     * @param val  New value.
     */
    virtual void setValue(int val);

    /**
     * @brief Sets the LCD driven by the buttons.
     *
     * @param pLCD  LCD to step, or NULL for none.
     */
    void setLCD(CLCD* pLCD);

    /**
     * @brief Sets the range and current value used without an LCD.
     *
     * @param min  Minimum value.
     * @param max  Maximum value.
     * @param val  Current value (not clamped).
     */
    void setRange(int min, int max, int val);

    int m_pushed;           // +0x88  1:up 2:down
    int m_value;            // +0x8c
    int m_max;              // +0x90
    int m_min;              // +0x94
    int m_bVertical;        // +0x98
    CLCD* m_pLCD;           // +0xa0
};

/**
 * @brief Maximum number of cells of a CPatternMatrix.
 */
#define PATTERNMATRIX_MAX   64

/**
 * @brief Grid of text buttons (display only, no mouse handling).
 */
class CPatternMatrix : public CGuiParts {
public:
    /**
     * @brief Creates the grid with all cells released.
     *
     * @param hWnd       Window.
     * @param id         Widget id.
     * @param x          Left edge (pixels).
     * @param y          Top edge (pixels).
     * @param w          Width (pixels).
     * @param h          Height (pixels).
     * @param cols       Number of columns.
     * @param rows       Number of rows (cols * rows <= PATTERNMATRIX_MAX).
     * @param texts      Cell captions, row by row (not copied).
     * @param text       Label text.
     * @param tx         Label left edge relative to @p x.
     * @param ty         Label top edge relative to @p y.
     * @param tw         Label width.
     * @param th         Label height.
     * @param bkColor    Background and button color.
     * @param textColor  Caption color.
     */
    CPatternMatrix(HWND hWnd, int id, int x, int y, int w, int h, int cols, int rows, char** texts, char* text, int tx, int ty, int tw, int th, COLORREF bkColor, COLORREF textColor);

    /**
     * @brief Draws every cell as a button (pushed according to m_states).
     *
     * @param hdc   Graphics context to draw into (the panel bitmap's context).
     * @param pDib  Panel bitmap behind @p hdc.
     * @param bAll  Nonzero for a full redraw (also draws static parts such as labels).
     * @return TRUE.
     */
    virtual BOOL drawMe(HDC hdc, CDIB* pDib, int bAll);

    char** m_texts;                     // +0x88
    int m_cols;                         // +0x90
    int m_rows;                         // +0x94
    int m_states[PATTERNMATRIX_MAX];    // +0x98
};

// knob
#define CROTERYV_DECLARED

/**
 * @brief Rotary knob with a value of 0..m_max.
 *
 * The pointer sweeps 270 degrees over a scale of 11 ticks. The knob is rendered at double size into
 * a work bitmap and scaled down; the background is shared by all knobs of the same size. A click
 * opens the shared popup slider or starts a vertical drag (depending on the slider type); user
 * changes send WM_GUIPARTS_NOTIFY with the new value.
 */
class CRoteryV : public CGuiParts {
public:
    /**
     * @brief Creates the knob.
     *
     * @param hWnd       Window receiving notifications.
     * @param id         Widget id.
     * @param x          Left edge (pixels).
     * @param y          Top edge (pixels).
     * @param w          Width (pixels).
     * @param h          Height (pixels).
     * @param max        Maximum value.
     * @param val        Initial value.
     * @param text       Label text.
     * @param tx         Label left edge relative to @p x.
     * @param ty         Label top edge relative to @p y.
     * @param tw         Label width.
     * @param th         Label height.
     * @param bkColor    Background color.
     * @param textColor  Label and scale color.
     * @param color      Knob color.
     */
    CRoteryV(HWND hWnd, int id, int x, int y, int w, int h, int max, int val, char* text, int tx, int ty, int tw, int th, COLORREF bkColor, COLORREF textColor, COLORREF color);

    /**
     * @brief Deletes the knob background if this knob created it.
     *
     * Knobs sharing that background must not be drawn afterwards.
     */
    virtual ~CRoteryV();

    /**
     * @brief Draws the knob.
     *
     * The first call finds or creates the shared background; the label is drawn when @p bAll is set.
     * The knob itself is redrawn only on the first call or when the value changed.
     *
     * @param hdc   Graphics context to draw into (the panel bitmap's context).
     * @param pDib  Panel bitmap behind @p hdc.
     * @param bAll  Nonzero for a full redraw (also draws static parts such as labels).
     * @return TRUE if the knob was drawn, otherwise FALSE.
     * @throws std::bad_alloc  If the background bitmap cannot be allocated.
     */
    virtual BOOL drawMe(HDC hdc, CDIB* pDib, int bAll);

    /**
     * @brief Opens the shared popup slider or starts a vertical drag.
     *
     * With slider type 1 the popup slider opens right or left of the knob (see setSliderPlace()); otherwise a vertical drag starts with the value display popup.
     *
     * @param x  Horizontal click position in panel coordinates (drag start).
     * @param y  Vertical click position in panel coordinates (drag start).
     */
    virtual void mouseDown(int x, int y);

    /**
     * @brief Sets the value without notification, clamped to 0..m_max, and redraws.
     *
     * @param val  New value.
     */
    virtual void setValue(int val);

    /**
     * @brief Sets the value from a user drag (not clamped), redraws and sends WM_GUIPARTS_NOTIFY with it.
     *
     * @param vol  New value.
     */
    virtual void setVol(int vol);

    /**
     * @brief Sets where the popup slider opens.
     *
     * @param place  0 = right of the knob, nonzero = left of it.
     */
    void setSliderPlace(int place);

    int m_max;              // +0x88
    int m_value;            // +0x8c
    COLORREF m_knobColor;   // +0x90
    int m_bInit;            // +0x94
    CDIB* m_pBg;            // +0x98  knob background (shared by same sized knobs)
    int m_lastValue;        // +0xa0
    int m_sliderPlace;      // +0xa4
};

#define CLEDMATRIX_DECLARED

/**
 * @brief Grid of colored cells (display only; never receives mouse events).
 */
class CLEDMatrix : public CGuiParts {
public:
    /**
     * @brief Creates an empty matrix without cell arrays.
     */
    CLEDMatrix(){ m_colors = NULL; m_drawnColors = NULL; }

    /**
     * @brief Creates a matrix with all cells black.
     *
     * @param hWnd       Window.
     * @param id         Widget id.
     * @param x          Left edge (pixels).
     * @param y          Top edge (pixels).
     * @param w          Width (pixels).
     * @param h          Height (pixels).
     * @param cols       Number of columns.
     * @param rows       Number of rows.
     * @param text       Text (not drawn).
     * @param bkColor    Background color (cell frames).
     * @param textColor  Text color (unused).
     * @throws std::bad_alloc  If the cell arrays cannot be allocated.
     */
    CLEDMatrix(HWND hWnd, int id, int x, int y, int w, int h, int cols, int rows, char* text, COLORREF bkColor, COLORREF textColor);

    /**
     * @brief Frees the cell arrays (with scalar delete).
     */
    virtual ~CLEDMatrix();

    /**
     * @brief Draws the cell frames when @p bAll is set and fills the cells whose color changed.
     *
     * @param hdc   Graphics context to draw into.
     * @param pDib  Panel bitmap to draw into (must not be NULL).
     * @param bAll  Nonzero to also draw the cell frames.
     * @return TRUE.
     */
    virtual BOOL drawMe(HDC hdc, CDIB* pDib, int bAll);

    /**
     * @brief Hit test that never hits, so the matrix never receives mouse events.
     *
     * @param x  Horizontal position in panel coordinates (unused).
     * @param y  Vertical position in panel coordinates (unused).
     * @return Always FALSE.
     */
    virtual BOOL PtInRt(int x, int y);

    /**
     * @brief Redraws the changed cells.
     *
     * @param val  Ignored.
     */
    virtual void setValue(int val);

    /**
     * @brief Sets the cell colors and redraws.
     *
     * @param colors  cols * rows COLORREF values, row by row.
     */
    void setColors(int* colors);

    int m_cols;         // +0x88
    int m_rows;         // +0x8c
    int* m_colors;      // +0x90
    int* m_drawnColors; // +0x98
};
