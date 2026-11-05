/**
 * @file SynWindow.h
 *
 * Windows and dialogs of the Cocoa GUI (view factory only, SynWindow.m).
 *
 * Declares the borderless popup windows that host the views of SynView.h, the
 * controller of the modal nib dialogs and the two dialog windows (options and
 * "write program"), plus the row objects of the option dialog's tables.
 */
#import <Cocoa/Cocoa.h>
#import "SynView.h"
#include "synth1if.h"

/**
 * @brief Window hosting a SynView; forwards application defined events to it as Win32 messages.
 *
 * Releases the GUI's mouse capture when the window is moved or about to close.
 */
@interface SynWindow : NSWindow
{
    SynView*    view;
}
@property (assign) SynView* view;

/**
 * @brief Initializes the window and starts observing its NSWindowDidMoveNotification.
 *
 * @param contentRect       Content rectangle in screen coordinates.
 * @param aStyle            NSWindow style mask.
 * @param bufferingType     Backing store type.
 * @param flag              YES to defer creation of the window device.
 * @return The initialized window, or nil.
 */
- (id)initWithContentRect:(NSRect)contentRect styleMask:(int)aStyle backing:(NSBackingStoreType)bufferingType defer:(BOOL)flag;

/**
 * @brief Sends message 0 (WM_CREATE - 1) with the content size to the techpop window.
 *
 * Calls SendMessage0() with the window's windowRef as HWND and the content view's
 * width and height as trailing arguments (one argument more than SendMessage0() takes).
 *
 * @param synthWin          Techpop window (CCWindow*) that receives the message.
 * @return Undefined: the method has no return statement.
 */
- (id)initView:(void*)synthWin;
@end

/**
 * @brief Borderless floating window hosting a VolView (value display and popup menus).
 *
 * Forwards application defined events to the view as Win32 messages.
 * @note The view is retained by setView: and never released.
 */
@interface VolWindow : NSWindow
{
    VolView*    view;
}

/**
 * @brief Sets and retains the view that receives the forwarded messages.
 *
 * @param aView             The hosted view.
 */
- (void)setView:(VolView*)aView;
@end

/**
 * @brief Borderless floating window hosting the SoundSelectView (sound select popup).
 *
 * Forwards application defined events to the view as Win32 messages.
 */
@interface SoundSelectWindow : NSWindow
{
    SoundSelectView*    view;
}

/**
 * @brief Sets and retains the view and starts observing NSWindowDidResignKeyNotification.
 *
 * The view is released in dealloc.
 *
 * @param aView             The hosted view.
 */
- (void)setView:(SoundSelectView*)aView;
@end

/**
 * @brief Window controller of the modal dialogs loaded from a nib (CCDialog).
 *
 * Its window is an OptionWindow or a SaveSoundWindow; the OK and Cancel buttons
 * end the modal session started by ModalDialogMac().
 */
@interface CCDlg : NSWindowController
{
}

/**
 * @brief OK button action: applies the dialog and, if that succeeds, closes it.
 *
 * Calls doOK on the window; when it returns true, closes the window and stops the
 * modal session with code 1.
 *
 * @param sender            The button.
 */
- (void)doOK:(id)sender;

/**
 * @brief Cancel button action: closes the window and stops the modal session with code 0.
 *
 * @param sender            The button.
 */
- (void)doCANCEL:(id)sender;
@end

/**
 * @brief Window of the option dialog.
 *
 * Edits the 100 bank folders, the zip folder, the GUI zoom, the panel and text
 * colors, slider type, window transparency, host program selection, master tune
 * (-100..100 cents) and the MIDI CC map. The values are written back by doOK.
 * Acts as data source of both table views (m_list0: bank folders, m_list1: MIDI CC map).
 */
@interface OptionWindow : NSWindow
{
    NSTableView*    m_list0;
    NSTableView*    m_list1;
    NSTextField*    m_extzip;
    NSPopUpButton*  m_zoom;
    NSButton*       m_panel_check;
    NSTextField*    m_panel_text;
    NSButton*       m_text_check;
    NSTextField*    m_text_text;
    NSSlider*       m_master_tuning;
    NSTextField*    m_cent_text;
    NSTextField*    m_a4_text;
    NSButton*       m_slider_check;
    NSButton*       m_transparent_check;
    NSButton*       m_program_check;
    NSButton*       m_update;
    NSTextField*    m_update_text;
    NSButton*       m_bankselect_check;
    NSButton*       m_programchange_check;
    NSTextField*    m_cc_text;
    NSButton*       m_btn_browse0;
    NSButton*       m_btn_browse1;
    void*           m_pSynth1;
    char            m_bankFolder[100][256];
    NSMutableArray* m_bank;
    NSMutableArray* m_midi;
    int             m_CCMap[128];
}

/**
 * @brief Fills the dialog controls from the synthesizer and the ini settings.
 *
 * If the bank list exists: loads the 100 bank folders and their program counts,
 * selects the current bank, shows the zip folder (browse buttons hidden in a
 * sandbox), the zoom (ini "coordinatesscale" clamped to 80..250 %), the panel and
 * text colors as "#rrggbb" (bit 31 set = use default), slider type, transparency,
 * host program selection and master tune.
 * If the MIDI list exists: copies the CC map, lists every assignable parameter
 * with its CC ("-" if none) and selects the first row.
 *
 * @param pSynth1           Synthesizer instance passed to the Synthesizer1_* calls.
 */
- (void)setParam:(void*)pSynth1;

/**
 * @brief Writes the dialog values back to the synthesizer and the ini settings.
 *
 * Applies the bank folders, zip folder, zoom, colors (invalid "#rrggbb" text is
 * ignored), slider type, transparency (also g_bTransparencyWin), master tune, host
 * program selection, bank select / program change enable and the CC map.
 *
 * @return Always true.
 */
- (bool)doOK;
@end

/**
 * @brief Row of the option dialog's bank folder table (bank number, program count, folder path).
 *
 * @note Owns its three strings.
 */
@interface BankListItem : NSObject
{
    NSString*   m_bank;
    NSString*   m_program;
    NSString*   m_path;
}

/**
 * @brief Initializes the row from three UTF-8 strings.
 *
 * @param bank              Bank number text ("00".."99").
 * @param program           Program count text (or "invalid folder").
 * @param path              Bank folder path.
 * @return The initialized item, or nil.
 */
- (id)initWithParam:(const char*)bank program:(const char*)program path:(const char*)path;

/**
 * @brief Gets the bank number text.
 *
 * @return The bank number text ("00".."99"); not retained for the caller.
 */
- (NSString*)bank;

/**
 * @brief Gets the program count text.
 *
 * @return The program count text (or "invalid folder").
 */
- (NSString*)program;

/**
 * @brief Gets the bank folder path.
 *
 * @return The bank folder path.
 */
- (NSString*)path;

/**
 * @brief Sets the bank folder path (UTF-8), releasing the old one.
 *
 * @param path              New bank folder path (UTF-8).
 */
- (void)setPath:(const char*)path;

/**
 * @brief Sets the program count text (UTF-8), releasing the old one.
 *
 * @param program           New program count text (UTF-8).
 */
- (void)setProgram:(const char*)program;
@end

/**
 * @brief Row of the option dialog's MIDI CC map table (parameter name, CC number or "-").
 *
 * @note Owns its two strings.
 */
@interface MidiListItem : NSObject
{
    NSString*   m_parameter;
    NSString*   m_cc;
}

/**
 * @brief Initializes the row from two UTF-8 strings.
 *
 * @param parameter         Parameter name.
 * @param cc                CC number text, or "-" if unassigned.
 * @return The initialized item, or nil.
 */
- (id)initWithParam:(const char*)parameter cc:(const char*)cc;

/**
 * @brief Gets the parameter name.
 *
 * @return The parameter name.
 */
- (NSString*)parameter;

/**
 * @brief Gets the CC number text.
 *
 * @return The CC number text, or "-" if unassigned.
 */
- (NSString*)cc;

/**
 * @brief Sets the parameter name (UTF-8), releasing the old one.
 *
 * @param parameter         New parameter name (UTF-8).
 */
- (void)setParameter:(const char*)parameter;

/**
 * @brief Sets the CC number text (UTF-8), releasing the old one.
 *
 * @param cc                New CC number text (UTF-8), or "-" if unassigned.
 */
- (void)setCC:(const char*)cc;
@end

/**
 * @brief Window of the "write program" dialog.
 *
 * Lets the user pick the target bank and program, the name and the color tag of
 * the current sound; doOK stores the choice in g_pWPI.
 */
@interface SaveSoundWindow : NSWindow
{
    void*           m_pSynth1;
    NSPopUpButton*  m_bank;
    NSPopUpButton*  m_program;
    NSPopUpButton*  m_color;
    NSTextField*    m_name;
    WriteProgramInfo    m_WPI;
}

/**
 * @brief Fills the dialog from the current sound.
 *
 * Lists the 100 banks and, if the sound belongs to a bank, that bank's 128
 * programs as "n:name" (1-based); selects the sound's bank, program and color
 * ("default" when the color is not one of g_defineColors) and shows its name.
 *
 * @param pSynth1           Synthesizer instance passed to the Synthesizer1_* calls.
 */
- (void)setParam:(void*)pSynth1;

/**
 * @brief Bank popup action: reloads the program popup with the selected bank's programs.
 *
 * Keeps the previously selected program index.
 *
 * @param sender            The bank popup.
 */
- (void)doBank:(id)sender;

/**
 * @brief Stores bank, program, name and color of the dialog in *g_pWPI.
 *
 * Shows an error message box if no bank or program is selected. Color is
 * 0xffffffff for "default", otherwise the g_defineColors entry.
 *
 * @return true if the values were accepted, false if bank or program is missing.
 */
- (bool)doOK;
@end
