#pragma once

/**
 * @file synwin.h
 * Main synth editor window (CSynthWin), the sound select popup
 * (CSoundSelectWin) and the option / save dialogs.  View factory only.
 *
 * Also declares the C++ entry points used by the Cocoa view factory to create and
 * destroy the editor (CreateSynWin(), DestroySynWin(), GetPanelScale()).
 */

#include "../windef.h"
#include "ccwindow.h"
#include "guiparts.h"

class CSynthesizer1;
class CSoundLibrary;
class CBank;
struct tagWriteProgramInfo;


//------------------------------------------------------------------
// Sound (program) select popup.  sizeof == 0x330
/**
 * @brief Sound (program) select popup.
 *
 * A borderless window with a bank tree on the left (internal banks, all internal banks and
 * external .zip banks) and a 4 x 32 grid of the 128 programs of the selected bank on the
 * right. In info mode the grid is replaced by an editor for the bank's readme.txt. Clicking a
 * program asks the library to load it and notifies the owner window; any click outside the
 * splitter, Esc or deactivation closes the popup.
 *
 * @note It is a member of CSynthWin, which destroys the native window when it receives
 *       message 0x8002.
 */
class CSoundSelectWin : public CPopupWin {
public:
    /**
     * @brief Creates an uninitialised popup; init() must be called before create().
     */
    CSoundSelectWin();

    /**
     * @brief Destroys the popup object; does nothing (the native window is destroyed by CCWindow).
     */
    ~CSoundSelectWin();

    /**
     * @brief Prepares the popup before its window is created.
     *
     * Rescans the .zip banks, looks up the bank to show, and sets the window type to
     * CCWIN_SOUNDSELECT with alpha 0.8 when window transparency is enabled (1.0 otherwise).
     *
     * @param pLib      Sound library that receives the program request.
     * @param progNo    Current program number; 0..127.
     * @param bankNo    Current bank number, or -1 for an external bank.
     * @param zipPath   Path of the external bank (used when @p bankNo is -1).
     * @param bProgMode Nonzero if opened from the program name (the cursor is then moved
     *                  to the current program), zero if opened from the bank name (cursor
     *                  moved to the bank in the tree).
     * @param pt        Screen position the popup was opened from.
     */
    void init(CSoundLibrary* pLib, int progNo, int bankNo, char* zipPath, int bProgMode, POINT* pt);

    /**
     * @brief Lays out the tree, program grid and bank name areas and returns the window size.
     *
     * The layout depends only on the tree width m_TVW; the grid boxes are 180 x 18 pixels.
     *
     * @param bankNo    Unused.
     * @param progNo    Unused.
     * @param[out] pW   Window width in pixels.
     * @param[out] pH   Window height in pixels (always 623).
     */
    void calcWindowSize(int bankNo, int progNo, int* pW, int* pH);

    /**
     * @brief Font setup hook; does nothing on the Mac.
     *
     * @param size  Font size (unused).
     */
    void createFont(float size);

    /**
     * @brief Draws the bank name and, in list mode with a valid bank, all 128 program boxes.
     *
     * @param hdc   Graphics context of the view.
     */
    void paint(CGContextRef hdc);

    /**
     * @brief Draws the bank title ("BankNN:name" or "ExtBank:name") and the program count.
     *
     * @param hdc   Graphics context of the view.
     * @param pBank Bank whose name and patch count are shown.
     */
    void drawBankNameInfo(CGContextRef hdc, CBank* pBank);

    /**
     * @brief Draws one program box with its number and name.
     *
     * The box is filled with the patch colour (lightened unless highlighted; white or grey if
     * the patch has no colour). The text is red for the current program of the current bank
     * (white if the box itself is red), black otherwise.
     *
     * @param hdc       Graphics context of the view.
     * @param progNo    Program number; 0..127.
     * @param pBank     Bank that holds the program.
     * @param bHilight  Nonzero to draw the box highlighted (mouse hover).
     */
    void drawProgBox(CGContextRef hdc, int progNo, CBank* pBank, int bHilight);

    /**
     * @brief Computes the rectangle of a program box in the grid (32 rows per column).
     *
     * @param progNo    Program number; 0..127.
     * @param[out] rc   Box rectangle in view pixels.
     */
    void getProgRect(int progNo, CGRect* rc);

    /**
     * @brief Fills the bank tree view.
     *
     * Adds an "Internal" node with the non-empty banks 00..99 (plus the current bank), an
     * "Internal (All)" node with all 100 banks and an "External (.zip)" node with every .zip
     * bank and its sub-banks. Each item's parameter is its CBank pointer.
     *
     * @param hWnd  Unused.
     * @param hTV   Tree (outline) view to fill.
     */
    void addBankTree(HWND hWnd, HWND hTV);

    /**
     * @brief Adds a .zip bank and, recursively, its child banks to the tree.
     *
     * @param hTV       Tree view.
     * @param hParent   Parent tree item.
     * @param pBank     Bank to add; shown as "name (count)" when it has patches.
     */
    void addZipBank2TV(HWND hTV, void* hParent, CBank* pBank);

    /**
     * @brief Searches the tree depth-first for the item of a bank and selects it.
     *
     * @param hTV       Tree view.
     * @param pBank     Bank to find.
     * @param hItem     First item to search from, or NULL to start at the root.
     * @return The selected item, or NULL if the bank is not in the tree.
     */
    void* selectBank(HWND hTV, CBank* pBank, void* hItem);

    /**
     * @brief Finds the program number under a point.
     *
     * @param pt    Point in view pixels.
     * @return Program number 0..127, or -1 if the point is outside the program grid.
     */
    int getProgNoFromPt(POINT* pt);

    /**
     * @brief Moves the child controls (tree, List/Info button and, in info mode, the
     *        readme editor and its update button) to the current layout.
     *
     * @param hWnd  Unused.
     */
    void moveChildWin(HWND hWnd);

    /**
     * @brief Loads the shown bank's readme.txt (up to 64 KB) into the info editor.
     *
     * Shows a hint text if the file does not exist; for a missing bank the editor is
     * cleared and made read-only and the update button disabled.
     */
    void initDisplayInfo();

    /**
     * @brief Window procedure of the popup.
     *
     * Handles creation (tree and button), painting, hover highlighting, program selection
     * (posts 0x709 with id 300 to the owner), dragging the splitter to resize the tree
     * (50..600 pixels), the List/Info toggle (command 125), saving the readme (command 127;
     * an empty text deletes the file), tree selection changes (0x4e) and closing (sends
     * 0x8002 to the owner once). Message 0x8001 moves the mouse cursor onto the selected
     * bank or program.
     *
     * @param hWnd      Window handle.
     * @param msg       Message id.
     * @param wParam    Message parameter.
     * @param lParam    Message parameter (mouse position packed as x | y << 16 for mouse
     *                  messages; the CGContextRef for WM_PAINT; the CBank pointer for 0x4e).
     * @return 0 for handled messages, otherwise the result of defaultProc().
     */
    virtual LRESULT myWndProc(HWND hWnd, int msg, WPARAM wParam, LPARAM lParam);

    static int m_TVW;       // tree view width

    int m_bClosing;         // +0x250
    POINT m_pt;             // +0x254  opening position
    int m_bProgMode;        // +0x25c
    int m_hoverProg;        // +0x260
    int m_curProg;          // +0x264
    CBank* m_pBank;         // +0x268  bank shown
    CBank* m_pCurBank;      // +0x270  bank of the current program
    int m_bInfoMode;        // +0x278
    int m_bValidBank;       // +0x27c
    int m_boxW;             // +0x280
    int m_boxH;             // +0x284
    CGRect m_rcBankName;    // +0x288
    CGRect m_rcTree;        // +0x2a8
    CGRect m_rcProg;        // +0x2c8
    void* m_hInternal;      // +0x2e8
    void* m_hInternalAll;   // +0x2f0
    void* m_hExternal;      // +0x2f8
    CSoundLibrary* m_pLib;  // +0x300
    HWND m_hButton;         // +0x308
    HWND m_hTree;           // +0x310
    HWND m_hEdit;           // +0x318
    HWND m_hUpdateButton;   // +0x320
    int m_bDragging;        // +0x328
    int m_dragX;            // +0x32c
};

//------------------------------------------------------------------
// Option dialogs (Windows only; the Mac build keeps empty procedures)
/**
 * @brief "General" page of the option dialog.
 *
 * @note The Mac build shows the options through a nib (see CCDialog::open()); these
 *       procedures are empty.
 */
class COptGeneralDlg : public CCDialog {
public:
    /**
     * @brief Dialog procedure; does nothing.
     *
     * @param hDlg      Dialog handle (unused).
     * @param msg       Message id (unused).
     * @param wParam    Message parameter (unused).
     * @param lParam    Message parameter (unused).
     * @return Always 0.
     */
    virtual BOOL myDlgProc(HWND hDlg, UINT msg, WPARAM wParam, LPARAM lParam);

    /**
     * @brief Formats the master tune text ("A4=xxx.xxxHz") but does not display it.
     *
     * @param hDlg  Unused.
     * @param tune  Master tune in cents.
     */
    void setMasterTuneText(HWND hDlg, int tune);

    /**
     * @brief Folder chooser; not implemented.
     *
     * @param hDlg      Owner dialog (unused).
     * @param title     Chooser title (unused).
     * @param path      Folder path buffer (unused; not written).
     * @return Always 0.
     */
    int openFolderDlg(HWND hDlg, char* title, char* path);
    char _pad_0020[0x19008];    // +0x20  (sizeof == 0x19028)
};

/**
 * @brief "MIDI" page of the option dialog (empty on the Mac).
 */
class COptMidiDlg : public CCDialog {
public:
    /**
     * @brief Dialog procedure; does nothing.
     *
     * @param hDlg      Dialog handle (unused).
     * @param msg       Message id (unused).
     * @param wParam    Message parameter (unused).
     * @param lParam    Message parameter (unused).
     * @return Always 0.
     */
    virtual BOOL myDlgProc(HWND hDlg, UINT msg, WPARAM wParam, LPARAM lParam);
    char _pad_0020[0x208];      // +0x20
};

// sizeof == 103024
/**
 * @brief Option dialog with the general and MIDI pages; opened by the "opt" button.
 *
 * Opened with CCDialog::open() and IDD_OPTION, which runs the IDD_OPTION nib modally.
 */
class COptionDlg : public CCDialog {
public:
    /**
     * @brief Dialog procedure; does nothing.
     *
     * @param hDlg      Dialog handle (unused).
     * @param msg       Message id (unused).
     * @param wParam    Message parameter (unused).
     * @param lParam    Message parameter (unused).
     * @return Always 0.
     */
    virtual BOOL myDlgProc(HWND hDlg, UINT msg, WPARAM wParam, LPARAM lParam);
    COptGeneralDlg m_general;   // +0x20
    COptMidiDlg m_midi;         // +0x19048
};

// sizeof == 88
/**
 * @brief Program write (save) dialog; opened by the "write" button.
 *
 * Opened with CCDialog::open() and IDD_SAVESOUND; the nib fills the WriteProgramInfo
 * returned by getWPI() (made available to it through g_pWPI).
 */
class CSoundSaveDlg : public CCDialog {
public:
    /**
     * @brief Dialog procedure; does nothing.
     *
     * @param hDlg      Dialog handle (unused).
     * @param msg       Message id (unused).
     * @param wParam    Message parameter (unused).
     * @param lParam    Message parameter (unused).
     * @return Always 0.
     */
    virtual BOOL myDlgProc(HWND hDlg, UINT msg, WPARAM wParam, LPARAM lParam);

    /**
     * @brief Gives access to the WriteProgramInfo stored in the dialog.
     *
     * @return Pointer to m_wpi (bank, program, name, colour) as a tagWriteProgramInfo.
     */
    tagWriteProgramInfo* getWPI();
    char _pad_0020[8];      // +0x20
    char m_wpi[0x30];       // +0x28  WriteProgramInfo
};

//------------------------------------------------------------------
// Main editor window.  sizeof == 3600
// Only visible here (see ccwindow.h): its out-of-line copy is emitted with synwin.cpp,
// and every CMDIChild vtable refers to it.
inline LRESULT CMDIChild::defaultProc(HWND hWnd, int msg, WPARAM wParam, LPARAM lParam)
{
    return 0;
}

/**
 * @brief Main synth editor panel shown in the Audio Unit's Cocoa view.
 *
 * Builds the panel from GUI parts (knobs, LCDs, LEDs, buttons, labels) in m_guis, keeps
 * them in sync with the synthesizer's sound and common parameters, forwards user edits to
 * CSynthesizer1 and notifies the host of parameter changes. GUI part ids are the sound
 * parameter id + 1000 (0..98) or the common parameter id + 1000 (251..254); smaller ids
 * are buttons and LEDs.
 *
 * Custom messages handled by myWndProc():
 * - 0x709  a GUI part changed (wParam = part id, lParam = value)
 * - 0x720  a sound parameter changed in the engine (wParam = id, lParam = value)
 * - 0x721  the program changed (lParam = CSoundData*; wParam 0 also posts 0x722)
 * - 0x722  tell the host that the whole state changed
 * - 0x723  redraw the voice status LEDs
 * - 0x8002 close the sound select popup
 * - 0x8003 redraw every parameter flagged in CSynthesizer1::m_bParamChanged
 *
 * @note Created by CreateSynWin() and deleted by DestroySynWin().
 */
class CSynthWin : public CMDIChild2 {
public:
    /**
     * @brief Creates an empty object; unused (the AU constructor is used instead).
     */
    CSynthWin();

    /**
     * @brief Creates the editor object for an Audio Unit.
     *
     * The window type is CCWIN_CUSTOM; the view itself is created by the caller and passed
     * to onCreate().
     *
     * @param pAU   AudioUnit handle used for parameter notifications.
     */
    CSynthWin(void* pAU);

    /**
     * @brief Stops the capture started in onCreate().
     */
    virtual ~CSynthWin();

    /**
     * @brief Attaches the editor to its view and synthesizer and builds the panel.
     *
     * Registers the view with the synthesizer, reads the transparency setting, builds and
     * draws the panel with the current values and starts the capture helper (m_pUser).
     *
     * @param hWnd      View that hosts the panel.
     * @param pSynth    Synthesizer being edited.
     */
    void onCreate(HWND hWnd, CSynthesizer1* pSynth);

    /**
     * @brief Builds (or rebuilds) the whole panel.
     *
     * Reads the panel scale (percent, default 150), panel colour and text colour from the
     * settings, sets the remaining colours, computes the panel size (466 x 340 units scaled),
     * reinitialises m_guis and adds the title labels, the voice status LED matrix and every
     * section.
     *
     * @param hWnd  View that hosts the panel.
     */
    void constructPanel(HWND hWnd);

    /**
     * @brief Shows all values of the current sound plus the master volume.
     */
    void drawAllParams();

    /**
     * @brief Adds the controls of the "Wheel / MIDI" section (pitch bend range and the two
     *        MIDI controller to parameter assignments).
     *
     * The construct functions place their controls in panel units at 100% scale; m_guis
     * scales them by m_coordScale.
     *
     * @param hWnd  View that hosts the panel.
     * @param x     Left edge of the section in panel units.
     * @param y     Top edge of the section in panel units.
     * @param w     LED size in panel units (used by sections with LEDs).
     * @param h     Knob size in panel units.
     * @param col1  Unused (the m_colXxx members are used instead).
     * @param col2  Unused.
     * @param col3  Unused.
     */
    void constructPanelWheelAsign(HWND hWnd, int x, int y, int w, int h, COLORREF col1, COLORREF col2, COLORREF col3);

    /**
     * @brief Adds the controls of the program area: volume knob, program spin, bank and program
     *        name displays and the write / opt buttons.
     *
     * @param hWnd  View that hosts the panel.
     * @param x     Left edge of the section in panel units.
     * @param y     Top edge of the section in panel units.
     * @param w     LED size in panel units (used by sections with LEDs).
     * @param h     Knob size in panel units.
     * @param col1  Unused (the m_colXxx members are used instead).
     * @param col2  Unused.
     * @param col3  Unused.
     * @see constructPanelWheelAsign()
     */
    void constructPanelProgramArea(HWND hWnd, int x, int y, int w, int h, COLORREF col1, COLORREF col2, COLORREF col3);

    /**
     * @brief Adds the controls of the "Voice" section: poly/mono/legato, polyphony, portamento and
     *        unison controls.
     *
     * @param hWnd  View that hosts the panel.
     * @param x     Left edge of the section in panel units.
     * @param y     Top edge of the section in panel units.
     * @param w     LED size in panel units (used by sections with LEDs).
     * @param h     Knob size in panel units.
     * @param col1  Unused (the m_colXxx members are used instead).
     * @param col2  Unused.
     * @param col3  Unused.
     * @see constructPanelWheelAsign()
     */
    void constructPanelPlayMode(HWND hWnd, int x, int y, int w, int h, COLORREF col1, COLORREF col2, COLORREF col3);

    /**
     * @brief Adds the controls of the "Oscillators" section.
     *
     * @param hWnd  View that hosts the panel.
     * @param x     Left edge of the section in panel units.
     * @param y     Top edge of the section in panel units.
     * @param w     LED size in panel units (used by sections with LEDs).
     * @param h     Knob size in panel units.
     * @param col1  Unused (the m_colXxx members are used instead).
     * @param col2  Unused.
     * @param col3  Unused.
     * @see constructPanelWheelAsign()
     */
    void constructPanelVCO(HWND hWnd, int x, int y, int w, int h, COLORREF col1, COLORREF col2, COLORREF col3);

    /**
     * @brief Adds the controls of the "Filter" section.
     *
     * @param hWnd  View that hosts the panel.
     * @param x     Left edge of the section in panel units.
     * @param y     Top edge of the section in panel units.
     * @param w     LED size in panel units (used by sections with LEDs).
     * @param h     Knob size in panel units.
     * @param col1  Unused (the m_colXxx members are used instead).
     * @param col2  Unused.
     * @param col3  Unused.
     * @see constructPanelWheelAsign()
     */
    void constructPanelVCF(HWND hWnd, int x, int y, int w, int h, COLORREF col1, COLORREF col2, COLORREF col3);

    /**
     * @brief Adds the controls of the "Amplifier" section.
     *
     * @param hWnd  View that hosts the panel.
     * @param x     Left edge of the section in panel units.
     * @param y     Top edge of the section in panel units.
     * @param w     LED size in panel units (used by sections with LEDs).
     * @param h     Knob size in panel units.
     * @param col1  Unused (the m_colXxx members are used instead).
     * @param col2  Unused.
     * @param col3  Unused.
     * @see constructPanelWheelAsign()
     */
    void constructPanelVCA(HWND hWnd, int x, int y, int w, int h, COLORREF col1, COLORREF col2, COLORREF col3);

    /**
     * @brief Adds the controls of the "LFO" section (both LFOs).
     *
     * @param hWnd  View that hosts the panel.
     * @param x     Left edge of the section in panel units.
     * @param y     Top edge of the section in panel units.
     * @param w     LED size in panel units (used by sections with LEDs).
     * @param h     Knob size in panel units.
     * @param col1  Face colour of the on/off toggle button(s) when off.
     * @param col2  Face colour of the on/off toggle button(s) when on.
     * @param col3  Caption colour of the on/off toggle button(s).
     * @see constructPanelWheelAsign()
     */
    void constructPanelLFO(HWND hWnd, int x, int y, int w, int h, COLORREF col1, COLORREF col2, COLORREF col3);

    /**
     * @brief Adds the controls of the "Arpeggiator" section.
     *
     * @param hWnd  View that hosts the panel.
     * @param x     Left edge of the section in panel units.
     * @param y     Top edge of the section in panel units.
     * @param w     LED size in panel units (used by sections with LEDs).
     * @param h     Knob size in panel units.
     * @param col1  Face colour of the on/off toggle button(s) when off.
     * @param col2  Face colour of the on/off toggle button(s) when on.
     * @param col3  Caption colour of the on/off toggle button(s).
     * @see constructPanelWheelAsign()
     */
    void constructPanelArp(HWND hWnd, int x, int y, int w, int h, COLORREF col1, COLORREF col2, COLORREF col3);

    /**
     * @brief Adds the controls of the "Effect" section.
     *
     * @param hWnd  View that hosts the panel.
     * @param x     Left edge of the section in panel units.
     * @param y     Top edge of the section in panel units.
     * @param w     LED size in panel units (used by sections with LEDs).
     * @param h     Knob size in panel units.
     * @param col1  Face colour of the on/off toggle button(s) when off.
     * @param col2  Face colour of the on/off toggle button(s) when on.
     * @param col3  Caption colour of the on/off toggle button(s).
     * @see constructPanelWheelAsign()
     */
    void constructPanelEFXS(HWND hWnd, int x, int y, int w, int h, COLORREF col1, COLORREF col2, COLORREF col3);

    /**
     * @brief Adds the controls of the "Equalizer / Pan" section.
     *
     * @param hWnd  View that hosts the panel.
     * @param x     Left edge of the section in panel units.
     * @param y     Top edge of the section in panel units.
     * @param w     LED size in panel units (used by sections with LEDs).
     * @param h     Knob size in panel units.
     * @param col1  Unused (the m_colXxx members are used instead).
     * @param col2  Unused.
     * @param col3  Unused.
     * @see constructPanelWheelAsign()
     */
    void constructPanelEqPan(HWND hWnd, int x, int y, int w, int h, COLORREF col1, COLORREF col2, COLORREF col3);

    /**
     * @brief Adds the controls of the "Tempo Delay" section.
     *
     * @param hWnd  View that hosts the panel.
     * @param x     Left edge of the section in panel units.
     * @param y     Top edge of the section in panel units.
     * @param w     LED size in panel units (used by sections with LEDs).
     * @param h     Knob size in panel units.
     * @param col1  Face colour of the on/off toggle button(s) when off.
     * @param col2  Face colour of the on/off toggle button(s) when on.
     * @param col3  Caption colour of the on/off toggle button(s).
     * @see constructPanelWheelAsign()
     */
    void constructPanelDelay(HWND hWnd, int x, int y, int w, int h, COLORREF col1, COLORREF col2, COLORREF col3);

    /**
     * @brief Adds the controls of the "Chorus / Flanger" section.
     *
     * @param hWnd  View that hosts the panel.
     * @param x     Left edge of the section in panel units.
     * @param y     Top edge of the section in panel units.
     * @param w     LED size in panel units (used by sections with LEDs).
     * @param h     Knob size in panel units.
     * @param col1  Face colour of the on/off toggle button(s) when off.
     * @param col2  Face colour of the on/off toggle button(s) when on.
     * @param col3  Caption colour of the on/off toggle button(s).
     * @see constructPanelWheelAsign()
     */
    void constructPanelChorus(HWND hWnd, int x, int y, int w, int h, COLORREF col1, COLORREF col2, COLORREF col3);

    /**
     * @brief Shows every parameter of a newly selected sound and its bank and program names.
     *
     * @param wParam    Unused.
     * @param lParam    The CSoundData* of the new sound.
     */
    void onUser_PROGRAM_CHANGED(WPARAM wParam, LPARAM lParam);

    /**
     * @brief Updates the display of a common (non-sound) parameter.
     *
     * @param id    251 = bank name, 252 = program number and name, 254 = master volume knob;
     *              other ids are ignored.
     */
    void drawCommonPartByParamID(int id);

    /**
     * @brief Moves the editor to another view, or detaches it.
     *
     * Updates the synthesizer, the GUI parts and m_hWnd. Passing NULL also ends MIDI learn.
     *
     * @param hWnd  New view, or NULL.
     */
    void changeWindowHandle(HWND hWnd);

    /**
     * @brief Paints the part of the panel inside @p rc.
     *
     * @param hWnd  Unused.
     * @param hdc   Graphics context of the view.
     * @param rc    Area to repaint in view pixels.
     */
    void paint(HWND hWnd, CGContextRef hdc, RECT* rc);

    /**
     * @brief Passes a mouse message to the GUI parts.
     *
     * @param msg       Mouse message id (WM_xxx).
     * @param lParam    Mouse position packed as x | y << 16, in view pixels.
     */
    void onMouseMessage(int msg, LPARAM lParam);

    /**
     * @brief Handles a value change or click from a GUI part.
     *
     * - 1000..1098: sets the sound parameter (id - 1000) and updates the related LEDs.
     * - 3 / 5: opens the sound select popup from the program / bank name, centred on the panel.
     * - 15 ("opt"): with Shift held starts MIDI learn; otherwise runs the option dialog and
     *   rebuilds the panel if the scale or colours changed.
     * - 94 ("write"): runs the save dialog and writes the program; shows an error if the
     *   bank folder is not set or the file cannot be written.
     * - 300: loads the program requested by the popup and moves the cursor back to the
     *   program or bank name.
     * - 1252 / 1254: sets the program number / master volume.
     *
     * @param wParam    GUI part id.
     * @param lParam    New value (for 3 / 5 the click position packed as x | y << 16).
     */
    void onUser_GUIPARTS(WPARAM wParam, LPARAM lParam);

    /**
     * @brief Lights the "in tune" LED next to a pitch control.
     *
     * For osc2 pitch (1002) the LED is on at the values that map to whole octaves; for osc2
     * fine tune (1003) and osc1 detune (1072) it is on within the centre dead zone 62..66.
     *
     * @param id    GUI part id: 1002, 1003 or 1072; others are ignored.
     * @param val   Parameter value; 0..127.
     */
    void setPitchTuneLED(int id, int val);

    /**
     * @brief Turns the oscillator phase LEDs on for a value of 1 or more, off for 0.
     *
     * @param id    GUI part id; only 1091 is handled.
     * @param val   Parameter value.
     */
    void setOscPhaseLED(int id, int val);

    /**
     * @brief Colours the 32 voice LEDs from the voice states (0..3 = panel colour, white, grey,
     *        black).
     */
    void drawVoiceStatus();

    /**
     * @brief Shows a changed sound parameter.
     *
     * Sets GUI part id + 1000, updates the pitch / phase LEDs, and redraws the bank or
     * program name for ids 251 / 252.
     *
     * @param wParam    Parameter id.
     * @param lParam    New value.
     */
    void onUser_SOUNDPARAM_CHANGED(WPARAM wParam, LPARAM lParam);

    /**
     * @brief Redraws the voice status LEDs (calls drawVoiceStatus()).
     *
     * @param wParam    Unused.
     * @param lParam    Unused.
     */
    void onUser_REDRAW_STATUS(WPARAM wParam, LPARAM lParam);

    /**
     * @brief Window procedure of the editor view.
     *
     * Handles creation, painting, mouse input (a click closes an open sound select popup
     * first), closing (ends MIDI learn) and the custom messages listed in the class
     * description. A GUI part change of a sound parameter or the master volume is also
     * reported to the host as a begin / change / end gesture.
     *
     * @param hWnd      View handle.
     * @param msg       Message id.
     * @param wParam    Message parameter.
     * @param lParam    Message parameter.
     * @return 0 for handled messages, otherwise the result of defaultProc().
     */
    virtual LRESULT myWndProc(HWND hWnd, int msg, WPARAM wParam, LPARAM lParam);

    /**
     * @brief Shows the current value of a sound parameter.
     *
     * @param id    Sound parameter id; 0..98.
     */
    void drawPartByParamID(int id);

    /**
     * @brief Formats a parameter value as display text (e.g. Hz, msec, dB, %, note lengths).
     *
     * Used by the GUI parts for value tooltips and LCDs.
     *
     * @param[out] str  Buffer for the text; set to "" if there is no special format.
     * @param id        GUI part id (parameter id + 1000).
     * @param val       Parameter value; 0..127 for most parameters.
     * @param pSynth    The CSynthesizer1 (checked for LFO tempo sync).
     * @return 1 if @p str was filled, 0 if the parameter has no special format (the
     *         caller then shows the raw value).
     */
    static int convertVal(char* str, int id, int val, void* pSynth);

    /**
     * @brief Tells whether a GUI part ends MIDI learn instead of being learned.
     *
     * @param id    GUI part id.
     * @return 1 for the program and bank names, the opt and write buttons, the polyphony
     *         controls and the program and volume controls (ids 3, 5, 15, 93, 94, 1094,
     *         1252, 1254); otherwise 0.
     */
    static int isMidiLearnEndParts(int id);

    COLORREF m_colPanel;            // +0x250
    COLORREF m_colText;             // +0x254
    COLORREF m_colLabelText;        // +0x258
    COLORREF m_colLCD;              // +0x25c
    COLORREF m_colVerText;          // +0x260
    COLORREF m_colVerBk;            // +0x264
    COLORREF m_colKnob;             // +0x268
    COLORREF m_colKnobBk;           // +0x26c
    CGUICollection m_guis;          // +0x270
    int m_coordScale;               // +0xac0  percent
    int m_bProgSelect;              // +0xac4
    void* m_pAU;                    // +0xac8  AudioUnit
    CSoundSelectWin m_soundSelWin;  // +0xad0
    CSynthesizer1* m_pSynth;        // +0xe00
    int m_panelW;                   // +0xe08
    int m_panelH;                   // +0xe0c
};

/**
 * @brief Creates the editor for an Audio Unit view.
 *
 * @param hWnd      View that hosts the panel.
 * @param pSynth    The CSynthesizer1 to edit; its notify window is set to @p hWnd.
 * @param pAU       AudioUnit handle used for parameter notifications.
 * @return The new editor; free it with DestroySynWin().
 */
CSynthWin* CreateSynWin(HWND hWnd, void* pSynth, void* pAU);

/**
 * @brief Closes and deletes an editor created by CreateSynWin().
 *
 * Ends MIDI learn, detaches the view from the synthesizer and the GUI parts and clears the
 * synthesizer's notify window before deleting the object.
 *
 * @param pWin  The CSynthWin to delete.
 */
void DestroySynWin(void* pWin);

/**
 * @brief Reads the panel scale from the settings ("coordinatesscale").
 *
 * @return Panel scale in percent (default 150).
 */
int GetPanelScale();
