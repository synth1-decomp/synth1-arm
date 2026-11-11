/**
 * @file SynWindow.m
 *
 * Windows and dialogs of the Cocoa GUI (view factory).
 *
 * - SynWindow / VolWindow / SoundSelectWindow: windows hosting the views of
 *   SynView.m; application defined events are forwarded to the view as Win32 messages.
 * - CCDlg: controller of the modal dialogs loaded from nibs (OptionWindow and
 *   SaveSoundWindow are their windows).
 * - OptionWindow: the option dialog (bank folders, zip folder, panel, master tune,
 *   MIDI CC map).
 * - SaveSoundWindow: the "write program" dialog.
 * - BankListItem / MidiListItem: table rows of the option dialog.
 */

#import <Cocoa/Cocoa.h>
#import "SynWindow.h"

#ifdef SYNTH1_MODERN_BUILD
// Current compilers reject implicit declarations; these match windef.h and techpop/common.h
int     SendMessage0(void *pWnd, void *hWnd, unsigned int msg, unsigned long wParam, unsigned long lParam);
void    ReleaseCapture(void);
int     MessageBox(void *hWnd, const char *lpText, const char *lpCaption, unsigned int uType);
int     isExistFile(const char *path, int bDir);
void    setIniInt(const char *key, int val);
#endif

#define WM_CREATE           0x0001

#define CCMAP_NUM           128
#define BANK_NUM            100
#define PROGRAM_NUM         128

static bool openFolder(char* path, const char* initPath);
static bool openFileDialog(char* path, BOOL bSave);
static void getExistFolder(char* dir);

/**
 * @brief Computes a to the power b via exp(log(a) * b).
 *
 * Same as fpow() in techpop/common.h: falls back to pow() if a is 0 or the result
 * is not a number.
 *
 * @param a                 Base.
 * @param b                 Exponent.
 * @return a raised to b.
 */
static inline double fpow(double a, double b)
{
    if(a == 0) return pow(a, b);
    double r = exp(log(a) * b);
    if(isnan(r)) r = pow(a, b);
    return r;
}


//==============================================================================
//  SynWindow
//==============================================================================
@implementation SynWindow

@synthesize view;

- (id)initWithContentRect:(NSRect)contentRect styleMask:(int)aStyle backing:(NSBackingStoreType)bufferingType defer:(BOOL)flag
{
    self = [super initWithContentRect:contentRect styleMask:aStyle backing:bufferingType defer:flag];
    if(self){
        [[NSNotificationCenter defaultCenter] addObserver:self selector:@selector(windowDidMove:) name:NSWindowDidMoveNotification object:self];
    }
    return self;
}

- (id)initView:(void*)synthWin
{
    NSRect rect = [[self contentView] bounds];
#ifdef SYNTH1_MODERN_BUILD
    SendMessage0(synthWin, self, WM_CREATE - 1, 0, 0);
#else
    SendMessage0(synthWin, [self windowRef], WM_CREATE - 1, 0, rect.size.width, rect.size.height);
#endif
}

/**
 * @brief Forwards application defined events to the view as Win32 messages, then dispatches normally.
 *
 * Application defined events carry Win32 messages for the view: subtype is the
 * message, data1 the wParam and data2 the lParam.
 *
 * @param theEvent          The event to dispatch.
 */
- (void)sendEvent:(NSEvent*)theEvent
{
    if([theEvent type] == NSApplicationDefined){
        [view sendMessage:[theEvent subtype] wParam:[theEvent data1] lParam:[theEvent data2]];
    }
    [super sendEvent:theEvent];
}

/**
 * @brief Stops observing NSWindowDidMoveNotification and closes the window.
 */
- (void)close
{
    [[NSNotificationCenter defaultCenter] removeObserver:self name:NSWindowDidMoveNotification object:self];
    [super close];
}

/**
 * @brief NSWindowDidMoveNotification handler: releases the mouse capture.
 *
 * @param notification      The notification (unused).
 */
- (void)windowDidMove:(NSNotification*)notification
{
    ReleaseCapture();
}

/**
 * @brief Releases the mouse capture before the window closes.
 *
 * @param sender            The object requesting the close.
 * @return The superclass result.
 */
- (BOOL)windowShouldClose:(id)sender
{
    ReleaseCapture();
    return [super windowShouldClose:sender];
}

@end


//==============================================================================
//  VolWindow
//==============================================================================
@implementation VolWindow

/**
 * @brief Forwards application defined events to the view as Win32 messages, then dispatches normally.
 *
 * @param theEvent          The event (subtype = message, data1 = wParam, data2 = lParam).
 */
- (void)sendEvent:(NSEvent*)theEvent
{
    if([theEvent type] == NSApplicationDefined){
        [view sendMessage:[theEvent subtype] wParam:[theEvent data1] lParam:[theEvent data2]];
    }
    [super sendEvent:theEvent];
}

- (void)setView:(VolView*)aView
{
    view = aView;
    [aView retain];
}

/**
 * @brief Deallocates the window (the retained view is not released).
 */
- (void)dealloc
{
    [super dealloc];
}

@end


//==============================================================================
//  SoundSelectWindow
//==============================================================================
@implementation SoundSelectWindow

/**
 * @brief Forwards application defined events to the view as Win32 messages, then dispatches normally.
 *
 * @param theEvent          The event (subtype = message, data1 = wParam, data2 = lParam).
 */
- (void)sendEvent:(NSEvent*)theEvent
{
    if([theEvent type] == NSApplicationDefined){
        [view sendMessage:[theEvent subtype] wParam:[theEvent data1] lParam:[theEvent data2]];
    }
    [super sendEvent:theEvent];
}

- (void)setView:(SoundSelectView*)aView
{
    view = aView;
    [aView retain];
    [[NSNotificationCenter defaultCenter] addObserver:self selector:@selector(windowDidResignKey:) name:NSWindowDidResignKeyNotification object:self];
}

/**
 * @brief Stops observing NSWindowDidResignKeyNotification and releases the view.
 */
- (void)dealloc
{
    [[NSNotificationCenter defaultCenter] removeObserver:self name:NSWindowDidResignKeyNotification object:self];
    [view release];
    [super dealloc];
}

/**
 * @brief NSWindowDidResignKeyNotification handler; does nothing.
 *
 * @param notification      The notification (unused).
 */
- (void)windowDidResignKey:(NSNotification*)notification
{
}

/**
 * @brief Reads the event location and otherwise ignores the event.
 *
 * @param theEvent          The mouse event.
 */
- (void)mouseMoved:(NSEvent*)theEvent
{
    [theEvent locationInWindow];
}

@end


//==============================================================================
//  CCDlg
//==============================================================================
@implementation CCDlg

- (void)doOK:(id)sender
{
    if([(OptionWindow*)[self window] doOK]){
        [[self window] close];
        [[NSApplication sharedApplication] stopModalWithCode:1];
    }
}

- (void)doCANCEL:(id)sender
{
    [[self window] close];
    [[NSApplication sharedApplication] stopModalWithCode:0];
}

@end


//==============================================================================
//  OptionWindow
//==============================================================================
@implementation OptionWindow

/**
 * @brief Initializes the window and creates the empty row arrays m_bank and m_midi.
 *
 * @param contentRect       Content rectangle in screen coordinates.
 * @param aStyle            NSWindow style mask.
 * @param bufferingType     Backing store type.
 * @param flag              YES to defer creation of the window device.
 * @return The initialized window.
 */
- (id)initWithContentRect:(NSRect)contentRect styleMask:(unsigned int)aStyle backing:(NSBackingStoreType)bufferingType defer:(BOOL)flag
{
    id ret = [super initWithContentRect:contentRect styleMask:aStyle backing:bufferingType defer:flag];
    m_bank = [[NSMutableArray alloc] init];
    m_midi = [[NSMutableArray alloc] init];
    return ret;
}

// NSTableView data source (m_list0: bank folders, m_list1: MIDI CC map)
/**
 * @brief NSTableView data source: returns the row count of a table.
 *
 * @param tableView         m_list0 (bank folders) or m_list1 (MIDI CC map).
 * @return Number of bank rows for m_list0, otherwise number of MIDI rows.
 */
- (int)numberOfRowsInTableView:(NSTableView*)tableView
{
    if(tableView == m_list0) return [m_bank count];
    return [m_midi count];
}

/**
 * @brief NSTableView data source: returns the text of one cell.
 *
 * Bank table columns "program" and "path" show the program count and folder; any
 * other column shows the bank number. MIDI table column "parameter" shows the
 * parameter name; any other column shows the CC.
 *
 * @param tableView         m_list0 or any other table (treated as m_list1).
 * @param tableColumn       Column, selected by its identifier.
 * @param row               Row index.
 * @return The cell string.
 */
- (id)tableView:(NSTableView*)tableView objectValueForTableColumn:(NSTableColumn*)tableColumn row:(int)row
{
    if(tableView == m_list0){
        id identifier = [tableColumn identifier];
        BankListItem* item = [m_bank objectAtIndex:row];
        if([identifier compare:@"program"] == NSOrderedSame) return [item program];
        if([identifier compare:@"path"] == NSOrderedSame) return [item path];
        return [item bank];
    }else{
        id identifier = [tableColumn identifier];
        MidiListItem* item = [m_midi objectAtIndex:row];
        if([identifier compare:@"parameter"] == NSOrderedSame) return [item parameter];
        return [item cc];
    }
}

/**
 * @brief Appends a row to the bank folder table data.
 *
 * @param bank              Bank number text.
 * @param program           Program count text.
 * @param path              Bank folder path.
 */
- (void)add:(const char*)bank banknum:(const char*)program bankpath:(const char*)path
{
    BankListItem* item = [[BankListItem alloc] initWithParam:bank program:program path:path];
    [m_bank addObject:item];
    [item release];
}

/**
 * @brief Appends a row to the MIDI CC map table data.
 *
 * @param parameter         Parameter name.
 * @param cc                CC number text, or "-".
 */
- (void)addcc:(const char*)parameter cc:(const char*)cc
{
    MidiListItem* item = [[MidiListItem alloc] initWithParam:parameter cc:cc];
    [m_midi addObject:item];
    [item release];
}

// Synthesizer1_getCCMapTable() is called without the synth pointer (it reads s1g), and
// g_midiAssignParamsNum is an `extern const int` here, so its load is hoisted out of the loop.
- (void)setParam:(void*)pSynth1
{
    m_pSynth1 = pSynth1;
    if(m_list0){
        [[NSNotificationCenter defaultCenter] addObserver:self selector:@selector(doSelectList1Item:) name:NSTableViewSelectionDidChangeNotification object:m_list1];
        [m_list0 setDataSource:self];
        for(int i=0;i<BANK_NUM;i++){
            char str[32];
            char num[32];
            sprintf(str, "%02d", i);
            SoundLibrary_getBankInfo(i, num, m_bankFolder[i]);
            [self add:str banknum:num bankpath:m_bankFolder[i]];
        }
        [m_list0 reloadData];
        int bankNo = Synthesizer1_getCurrentSoundBankNo(m_pSynth1);
        NSIndexSet* indexes = [[NSIndexSet alloc] initWithIndex:bankNo];
        [m_list0 selectRowIndexes:indexes byExtendingSelection:YES];
        [indexes release];
        [m_extzip setStringValue:[NSString stringWithUTF8String:Synthesizer1_getZipFolderPath(m_pSynth1)]];
        if(isSandBox()){
            [m_btn_browse0 setHidden:YES];
            [m_btn_browse1 setHidden:YES];
        }

        int scale = getIniInt("coordinatesscale", 150);
        if(scale < 80) scale = 80;
        if(scale > 250) scale = 250;
        int zoom = (scale - 80) / 10;
        [m_zoom selectItemAtIndex:zoom];

        char str[64];
        unsigned int color = getIniInt("panelcolor", 0x803c2828);
        sprintf(str, "#%02x%02x%02x", (unsigned char)color, (color >> 8) & 0xff, (color >> 16) & 0xff);
        [m_panel_text setStringValue:[NSString stringWithUTF8String:str]];
        [m_panel_check setIntValue:!(color >> 31)];
        [m_panel_text setEnabled:(color >> 31) == 0];

        color = getIniInt("textcolor", 0x80c8c8c8);
        sprintf(str, "#%02x%02x%02x", (unsigned char)color, (color >> 8) & 0xff, (color >> 16) & 0xff);
        [m_text_text setStringValue:[NSString stringWithUTF8String:str]];
        [m_text_check setIntValue:!(color >> 31)];
        [m_text_text setEnabled:(color >> 31) == 0];

        int slider = getIniInt("slidertype", 1);
        [m_slider_check setIntValue:slider == 1];
        int transparent = getIniInt("transparencywin", 1);
        [m_transparent_check setIntValue:transparent];
        int hostprog = getIniInt("hostselectprogram", 0);
        [m_program_check setIntValue:hostprog];
        int tune = getIniInt("mastertune", 0);
        [self setMasterTuneText:tune];
        [m_master_tuning setIntValue:tune];
    }
    if(m_list1){
        [m_list1 setDataSource:self];
        [m_bankselect_check setIntValue:Synthesizer1_getEnableBankSelect(m_pSynth1)];
        [m_programchange_check setIntValue:Synthesizer1_getEnableProgramChange(m_pSynth1)];
        int* pMap = Synthesizer1_getCCMapTable();
        for(int i=0;i<CCMAP_NUM;i++){
            m_CCMap[i] = pMap[i];
        }
        for(int i=0;i<g_midiAssignParamsNum;i++){
            char str[32];
            int cc;
            for(cc=0;cc<CCMAP_NUM;cc++){
                if(m_CCMap[cc] == g_midiAssignParams[i].id) break;
            }
            if(cc == CCMAP_NUM){
                strcpy(str, "-");
            }else{
                sprintf(str, "%d", cc);
            }
            [self addcc:g_midiAssignParams[i].name cc:str];
        }
        [m_list1 reloadData];
        NSIndexSet* indexes = [[NSIndexSet alloc] initWithIndex:0];
        [m_list1 selectRowIndexes:indexes byExtendingSelection:YES];
        [indexes release];
    }
}

/**
 * @brief Selection change of the MIDI table: shows the selected parameter's CC in the edit field.
 *
 * @param notification      The notification (unused).
 */
- (void)doSelectList1Item:(NSNotification*)notification
{
    [self dispedit:[m_list1 selectedRow]];
}

/**
 * @brief Stops observing the MIDI table's selection changes (the row arrays are not released).
 */
- (void)dealloc
{
    [[NSNotificationCenter defaultCenter] removeObserver:self name:NSTableViewSelectionDidChangeNotification object:m_list1];
    [super dealloc];
}

/**
 * @brief Shows the master tune as "n Cents" and the resulting A4 frequency ("A4=440.000Hz").
 *
 * @param cent              Master tune in cents (-100..100).
 */
- (void)setMasterTuneText:(int)cent
{
    [m_cent_text setStringValue:[NSString stringWithFormat:@"%d Cents", cent]];
    double freq = fpow(2.0, cent / 1200.0) * 440.0;
    [m_a4_text setStringValue:[NSString stringWithFormat:@"A4=%.3fHz", freq]];
}

/**
 * @brief Panel color check box action: enables the color field when checked.
 *
 * @param sender            The control that sent the action (unused).
 */
- (void)doCheckPanel:(id)sender
{
    if([m_panel_check intValue]){
        [m_panel_text setEnabled:YES];
    }else{
        [m_panel_text setEnabled:NO];
    }
}

/**
 * @brief Text color check box action: enables the color field when checked.
 *
 * @param sender            The control that sent the action (unused).
 */
- (void)doCheckText:(id)sender
{
    if([m_text_check intValue]){
        [m_text_text setEnabled:YES];
    }else{
        [m_text_text setEnabled:NO];
    }
}

// bank folder
/**
 * @brief Browse button of the bank table: picks a new folder for the selected bank.
 *
 * Updates the row's path and program count ("invalid folder" if the folder does
 * not exist) and m_bankFolder; the change is applied to the synthesizer by doOK.
 *
 * @param sender            The button.
 */
- (void)doBrowser0:(id)sender
{
    char path[1024];
    char str[32];
    int row = [m_list0 selectedRow];
    if(openFolder(path, [[[m_bank objectAtIndex:row] path] UTF8String])){
        BankListItem* item = [m_bank objectAtIndex:row];
        if(isExistFile(path, 1)){
            sprintf(str, "%d", Bank_getPatchNum(path));
        }else{
            sprintf(str, "invalid folder");
        }
        [item setProgram:str];
        [item setPath:path];
        strcpy(m_bankFolder[row], path);
        NSIndexSet* rows = [[NSIndexSet alloc] initWithIndex:row];
        NSIndexSet* columns = [[NSIndexSet alloc] initWithIndexesInRange:NSMakeRange(0, 2)];
        [m_list0 reloadData];
        [columns release];
        [rows release];
    }
}

/**
 * @brief Runs a modal folder chooser.
 *
 * Starts at the deepest existing folder of initPath when it is given.
 *
 * @param[out] path         Receives the chosen folder as a POSIX path (needs 1024 bytes).
 * @param initPath          Initial folder, or NULL / "" for the default.
 * @return true if a folder was chosen, false if the panel was cancelled.
 */
static bool openFolder(char* path, const char* initPath)
{
    NSOpenPanel* panel = [NSOpenPanel openPanel];
    [panel setCanChooseFiles:NO];
    [panel setCanChooseDirectories:YES];
    [panel setCanCreateDirectories:YES];
    [panel setAllowsMultipleSelection:NO];
    if(initPath && *initPath){
        // start at the deepest existing folder of initPath
        char dir[1024];
        strcpy(dir, initPath);
        getExistFolder(dir);
        [panel setDirectory:[NSString stringWithCString:dir encoding:NSUTF8StringEncoding]];
    }
    if([panel runModal] != NSOKButton) return false;
    NSArray* urls = [panel URLs];
    int count = [urls count];
    for(int i=0;i<count;i++){
        CFStringRef str = CFURLCopyFileSystemPath((CFURLRef)[urls objectAtIndex:i], kCFURLPOSIXPathStyle);
        strcpy(path, [(NSString*)str UTF8String]);
        CFRelease(str);
    }
    return true;
}

/**
 * @brief Cuts dir back to its deepest existing folder ("/" if there is none).
 *
 * @param[in,out] dir       UTF-8 POSIX path, shortened in place.
 */
static void getExistFolder(char* dir)
{
    BOOL bDir;
    int len = strlen(dir);
    while(1){
        NSString* str = [[[NSString alloc] initWithCString:dir encoding:NSUTF8StringEncoding] autorelease];
        if([[NSFileManager defaultManager] fileExistsAtPath:str isDirectory:&bDir] && bDir) break;
        for(len--;len>=0;len--){
            if(dir[len] == '/') break;
        }
        if(len <= 0){
            strcpy(dir, "/");
            break;
        }
        dir[len] = 0;
    }
}

// zip folder
/**
 * @brief Browse button of the zip folder: picks a folder and shows it in the zip folder field.
 *
 * @param sender            The control that sent the action (unused).
 */
- (void)doBrowser1:(id)sender
{
    char path[1024];
    if(openFolder(path, [[m_extzip stringValue] UTF8String])){
        [m_extzip setStringValue:[NSString stringWithCString:path encoding:NSUTF8StringEncoding]];
    }
}

/**
 * @brief "+" button: raises the master tune by 1 cent (clamped to -100..100).
 *
 * @param sender            The control that sent the action (unused).
 */
- (void)doCentPlus:(id)sender
{
    int cent = [m_master_tuning intValue] + 1;
    if(cent > 100) cent = 100;
    if(cent < -100) cent = -100;
    [m_master_tuning setIntValue:cent];
    [self setMasterTuneText:cent];
}

/**
 * @brief "-" button: lowers the master tune by 1 cent (clamped to -100..100).
 *
 * @param sender            The control that sent the action (unused).
 */
- (void)doCentMinus:(id)sender
{
    int cent = [m_master_tuning intValue] - 1;
    if(cent > 100) cent = 100;
    if(cent < -100) cent = -100;
    [m_master_tuning setIntValue:cent];
    [self setMasterTuneText:cent];
}

/**
 * @brief Reset button: sets the master tune to 0 cents.
 *
 * @param sender            The control that sent the action (unused).
 */
- (void)doReset:(id)sender
{
    [m_master_tuning setIntValue:0];
    [self setMasterTuneText:0];
}

/**
 * @brief Master tune slider action: updates the cent and A4 texts.
 *
 * @param sender            The control that sent the action (unused).
 */
- (void)doSlider:(id)sender
{
    [self setMasterTuneText:[m_master_tuning intValue]];
}

// load a CC map (.csv)
/**
 * @brief Load button: reads a CC map from a CSV file chosen by the user.
 *
 * Only the first line that is neither empty nor a '#' comment is parsed (lines
 * up to 1023 characters). If it is a valid map, it replaces m_CCMap and the MIDI
 * table is refreshed; the row selection ends on row 0.
 *
 * @param sender            The button.
 */
- (void)doLoad:(id)sender
{
    char path[1024];
    if(openFileDialog(path, NO)){
        FILE* fp = fopen(path, "rt");
        if(fp){
            while(fgets(path, sizeof(path), fp)){
                if(path[0] == '#' || path[0] == '\n') continue;
                fclose(fp);
                int map[CCMAP_NUM];
                if(Synthesizer1_getCCMapFromCSV(m_pSynth1, map, path)){
                    memcpy(m_CCMap, map, sizeof(m_CCMap));
                    int row = [m_list1 selectedRow];
                    [self dispmap:row];
                    [self dispedit:row];
                    [m_list1 reloadData];
                    for(int i=CCMAP_NUM-1;i>=0;i--){
                        NSIndexSet* indexes = [[NSIndexSet alloc] initWithIndex:i];
                        [m_list1 selectRowIndexes:indexes byExtendingSelection:NO];
                        [indexes release];
                    }
                }
                return;
            }
            fclose(fp);
        }
    }
}

/**
 * @brief Runs a modal save panel or single-file open panel.
 *
 * @param[out] path         Receives the chosen file as a POSIX path (needs 1024 bytes).
 * @param bSave             YES for a save panel, NO for an open panel.
 * @return true if a file was chosen, false if the panel was cancelled.
 */
static bool openFileDialog(char* path, BOOL bSave)
{
    if(bSave){
        NSSavePanel* panel = [NSSavePanel savePanel];
        [panel setCanCreateDirectories:YES];
        if([panel runModal] != NSOKButton) return false;
        CFStringRef str = CFURLCopyFileSystemPath((CFURLRef)[panel URL], kCFURLPOSIXPathStyle);
        strcpy(path, [(NSString*)str UTF8String]);
        CFRelease(str);
    }else{
        NSOpenPanel* panel = [NSOpenPanel openPanel];
        [panel setCanChooseFiles:YES];
        [panel setCanChooseDirectories:NO];
        [panel setCanCreateDirectories:YES];
        [panel setAllowsMultipleSelection:NO];
        if([panel runModal] != NSOKButton) return false;
        NSArray* urls = [panel URLs];
        int count = [urls count];
        for(int i=0;i<count;i++){
            CFStringRef str = CFURLCopyFileSystemPath((CFURLRef)[urls objectAtIndex:i], kCFURLPOSIXPathStyle);
            strcpy(path, [(NSString*)str UTF8String]);
            CFRelease(str);
        }
    }
    return true;
}

// save the CC map (.csv)
/**
 * @brief Save button: writes m_CCMap as one CSV line (plus newline) to a file chosen by the user.
 *
 * @param sender            The button.
 */
- (void)doSave:(id)sender
{
    char str[1024];
    if(openFileDialog(str, YES)){
        FILE* fp = fopen(str, "wt");
        if(fp){
            Synthesizer1_createCSVStringFromCCMap(m_pSynth1, m_CCMap, str);
            if(fputs(str, fp) != EOF){
                fputs("\n", fp);
            }
            fclose(fp);
        }
    }
}

// assign the CC in m_update_text to the selected parameter
// note: the "clear the old assignment" block updates list item `row` (not `i`); Synth1 1.13 does
// the same, so this is kept as is.
/**
 * @brief Update button: assigns the CC typed in m_update_text to the selected MIDI table row.
 *
 * "-" removes the parameter's assignment; text not starting with a digit is
 * ignored; numbers are clamped to 0..127. The parameter's previous CC entries in
 * m_CCMap are cleared before the new one is set.
 *
 * @param sender            The button.
 */
- (void)doUpdate:(id)sender
{
    char str[32];
    int row = [m_list1 selectedRow];
    if(row < 0) return;
    strcpy(str, [[m_update_text stringValue] UTF8String]);
    int cc = -1;
    if(strcmp(str, "-") != 0){
        if(str[0] < '0' || str[0] > '9') return;
        cc = atoi(str);
        if(cc > 127) cc = 127;
        if(cc < 1) cc = 0;
    }
    if(cc != -1){
        // the CC was assigned to another parameter: clear it there
        int id = m_CCMap[cc];
        if(id != -1){
            for(int i=0;i<g_midiAssignParamsNum;i++){
                if(id == g_midiAssignParams[i].id){
                    MidiListItem* item = [m_midi objectAtIndex:row];
                    [item setParameter:g_midiAssignParams[i].name];
                    [item setCC:"-"];
                    NSIndexSet* rows = [[NSIndexSet alloc] initWithIndex:row];
                    NSIndexSet* columns = [[NSIndexSet alloc] initWithIndexesInRange:NSMakeRange(0, 1)];
                    [m_list1 reloadData];
                    [columns release];
                    [rows release];
                    break;
                }
            }
        }
    }
    int id = g_midiAssignParams[row].id;
    for(int i=0;i<CCMAP_NUM;i++){
        if(m_CCMap[i] == id) m_CCMap[i] = -1;
    }
    char ccstr[32];
    if(cc == -1){
        strcpy(ccstr, "-");
    }else{
        m_CCMap[cc] = id;
        sprintf(ccstr, "%d", cc);
    }
    MidiListItem* item = [m_midi objectAtIndex:row];
    [item setParameter:g_midiAssignParams[row].name];
    [item setCC:ccstr];
    NSIndexSet* rows = [[NSIndexSet alloc] initWithIndex:row];
    NSIndexSet* columns = [[NSIndexSet alloc] initWithIndexesInRange:NSMakeRange(0, 1)];
    [m_list1 reloadData];
    [columns release];
    [rows release];
}

/**
 * @brief Shows the CC of parameter row in the edit field.
 *
 * Disables the Update button and clears the field for parameters that cannot be
 * assigned (cc == -2). Otherwise also refreshes the row's CC text from m_CCMap.
 *
 * @param row               MIDI table row (index into g_midiAssignParams); negative is ignored.
 */
- (void)dispedit:(int)row
{
    if(row < 0) return;
    if(g_midiAssignParams[row].cc == -2){
        [m_update setEnabled:NO];
        [m_update_text setStringValue:@""];
        return;
    }
    [m_update setEnabled:YES];
    int cc;
    for(cc=0;cc<CCMAP_NUM;cc++){
        if(m_CCMap[cc] == g_midiAssignParams[row].id) break;
    }
    char str[32];
    if(cc < CCMAP_NUM){
        sprintf(str, "%d", cc);
    }else{
        strcpy(str, "-");
    }
    [[m_midi objectAtIndex:row] setCC:str];
    [m_update_text setStringValue:[NSString stringWithCString:str encoding:NSUTF8StringEncoding]];
}

/**
 * @brief Reloads the MIDI table.
 *
 * @param row               Row that changed; negative skips the reload (otherwise unused).
 */
- (void)dispmap:(int)row
{
    if(row < 0) return;
    NSIndexSet* rows = [[NSIndexSet alloc] initWithIndex:row];
    NSIndexSet* columns = [[NSIndexSet alloc] initWithIndexesInRange:NSMakeRange(0, 2)];
    [m_list1 reloadData];
    [columns release];
    [rows release];
}

/**
 * @brief Parses "#rrggbb" (lowercase hex digits) into a COLORREF (0x00bbggrr).
 *
 * @param str               The text to parse.
 * @param[out] pColor       Receives the color; untouched on failure.
 * @return YES on success, NO if str is not a color.
 */
static inline BOOL parseColor(const char* str, unsigned int* pColor)
{
    if(str[0] != '#' || strlen(str) != 7) return NO;
    unsigned int color = 0;
    for(int i=5;i>=1;i-=2){
        unsigned char h, l;
        color <<= 8;
        char c = str[i];
        if(c >= '0' && c <= '9') h = c - '0';
        else if(c >= 'a' && c <= 'f') h = c - 'a' + 10;
        else return NO;
        c = str[i+1];
        if(c >= '0' && c <= '9') l = c - '0';
        else if(c >= 'a' && c <= 'f') l = c - 'a' + 10;
        else return NO;
        color |= l | (h << 4);
    }
    *pColor = color;
    return YES;
}

- (bool)doOK
{
    char str[32];
    unsigned int color;

    for(int i=0;i<BANK_NUM;i++){
        Synthesizer1_changeBankFolder(m_pSynth1, i, m_bankFolder[i]);
    }
    Synthesizer1_updateZipFolderPath(m_pSynth1, [[m_extzip stringValue] UTF8String]);
    setIniInt("coordinatesscale", [m_zoom indexOfSelectedItem] * 10 + 80);

    strcpy(str, [[[m_panel_text stringValue] lowercaseString] UTF8String]);
    if(parseColor(str, &color)){
        if([m_panel_check intValue]){
            setIniInt("panelcolor", color);
        }else{
            setIniInt("panelcolor", color | 0x80000000);
        }
    }
    strcpy(str, [[[m_text_text stringValue] lowercaseString] UTF8String]);
    if(parseColor(str, &color)){
        if([m_text_check intValue]){
            setIniInt("textcolor", color);
        }else{
            setIniInt("textcolor", color | 0x80000000);
        }
    }
    setIniInt("slidertype", [m_slider_check intValue]);
    setIniInt("transparencywin", [m_transparent_check intValue]);
    g_bTransparencyWin = [m_transparent_check intValue];
    setIniInt("mastertune", [m_master_tuning intValue]);
    Synthesizer1_setMasterTune(m_pSynth1, [m_master_tuning intValue]);
    setIniInt("hostselectprogram", [m_program_check intValue]);
    Synthesizer1_setEnableBankSelect(m_pSynth1, [m_bankselect_check intValue]);
    Synthesizer1_setEnableProgramChange(m_pSynth1, [m_programchange_check intValue]);
    Synthesizer1_setCCMap(m_pSynth1, m_CCMap);
    return true;
}

@end


//==============================================================================
//  BankListItem
//==============================================================================
@implementation BankListItem

- (id)initWithParam:(const char*)bank program:(const char*)program path:(const char*)path
{
    self = [super init];
    if(self){
        m_bank = [[NSString alloc] initWithCString:bank encoding:NSUTF8StringEncoding];
        m_program = [[NSString alloc] initWithCString:program encoding:NSUTF8StringEncoding];
        m_path = [[NSString alloc] initWithCString:path encoding:NSUTF8StringEncoding];
    }
    return self;
}

/**
 * @brief Releases the three strings.
 */
- (void)dealloc
{
    if(m_bank){
        [m_bank release];
        m_bank = nil;
    }
    if(m_program){
        [m_program release];
        m_program = nil;
    }
    if(m_path){
        [m_path release];
        m_path = nil;
    }
    [super dealloc];
}

- (NSString*)bank
{
    return m_bank;
}

- (NSString*)program
{
    return m_program;
}

- (NSString*)path
{
    return m_path;
}

- (void)setPath:(const char*)path
{
    if(m_path) [m_path release];
    m_path = [[NSString alloc] initWithCString:path encoding:NSUTF8StringEncoding];
}

- (void)setProgram:(const char*)program
{
    if(m_program) [m_program release];
    m_program = [[NSString alloc] initWithCString:program encoding:NSUTF8StringEncoding];
}

@end


//==============================================================================
//  MidiListItem
//==============================================================================
@implementation MidiListItem

- (id)initWithParam:(const char*)parameter cc:(const char*)cc
{
    self = [super init];
    if(self){
        m_parameter = [[NSString alloc] initWithCString:parameter encoding:NSUTF8StringEncoding];
        m_cc = [[NSString alloc] initWithCString:cc encoding:NSUTF8StringEncoding];
    }
    return self;
}

/**
 * @brief Releases the two strings.
 */
- (void)dealloc
{
    if(m_parameter){
        [m_parameter release];
        m_parameter = nil;
    }
    if(m_cc){
        [m_cc release];
        m_cc = nil;
    }
    [super dealloc];
}

- (NSString*)parameter
{
    return m_parameter;
}

- (NSString*)cc
{
    return m_cc;
}

- (void)setParameter:(const char*)parameter
{
    if(m_parameter) [m_parameter release];
    m_parameter = [[NSString alloc] initWithCString:parameter encoding:NSUTF8StringEncoding];
}

- (void)setCC:(const char*)cc
{
    if(m_cc) [m_cc release];
    m_cc = [[NSString alloc] initWithCString:cc encoding:NSUTF8StringEncoding];
}

@end


//==============================================================================
//  SaveSoundWindow
//==============================================================================
@implementation SaveSoundWindow

- (void)setParam:(void*)pSynth1
{
    char name[256];
    char str[256];

    m_pSynth1 = pSynth1;
    void* pSound = Synthesizer1_getCurrentSound(pSynth1);
    for(int i=0;i<BANK_NUM;i++){
        SoundLibrary_getBankInfo1(i, name);
        [m_bank addItemWithTitle:[NSString stringWithCString:name encoding:NSUTF8StringEncoding]];
    }
    [m_bank selectItemAtIndex:SoundData_getBankNo(pSound)];
    if(SoundData_getBankNo(pSound) != -1){
        for(int i=0;i<PROGRAM_NUM;){
            Synthesizer1_getProgramName(m_pSynth1, SoundData_getBankNo(pSound), NULL, i, name);
            i++;
            sprintf(str, "%d:%s", i, name);
            [m_program addItemWithTitle:[NSString stringWithCString:str encoding:NSUTF8StringEncoding]];
        }
        [m_program selectItemAtIndex:SoundData_getProgramNo(pSound)];
    }
    [m_color addItemWithTitle:@"default"];
    for(int i=0;i<DEFINECOLOR_NUM;i++){
        [m_color addItemWithTitle:[NSString stringWithCString:g_defineColors[i].name encoding:NSUTF8StringEncoding]];
    }
    [m_name setStringValue:[NSString stringWithCString:SoundData_getName(pSound) encoding:NSUTF8StringEncoding]];
    int sel = 0;
    for(int i=0;i<DEFINECOLOR_NUM;i++){
        if(g_defineColors[i].color == SoundData_getColor(pSound)){
            sel = i + 1;
            break;
        }
    }
    [m_color selectItemAtIndex:sel];
}

- (void)doBank:(id)sender
{
    char name[256];
    char str[256];
    int progNo = [m_program indexOfSelectedItem];
    int bankNo = [m_bank indexOfSelectedItem];
    [m_program removeAllItems];
    for(int i=0;i<PROGRAM_NUM;){
        Synthesizer1_getProgramName(m_pSynth1, bankNo, NULL, i, name);
        i++;
        sprintf(str, "%d:%s", i, name);
        [m_program addItemWithTitle:[NSString stringWithCString:str encoding:NSUTF8StringEncoding]];
    }
    [m_program selectItemAtIndex:progNo];
}

- (bool)doOK
{
    m_WPI.bankNo = [m_bank indexOfSelectedItem];
    m_WPI.progNo = [m_program indexOfSelectedItem];
    if(m_WPI.bankNo == -1 || m_WPI.progNo == -1){
        MessageBox(NULL, "Please select an internal bank number and the program number.", "Write Error", 0);
        return false;
    }
    strcpy(m_WPI.szName, [[m_name stringValue] UTF8String]);
    int sel = [m_color indexOfSelectedItem];
    m_WPI.color = (sel < 1) ? 0xffffffff : g_defineColors[sel - 1].color;
    if(g_pWPI){
        *g_pWPI = m_WPI;
    }
    return true;
}

@end
