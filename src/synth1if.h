/**
 * @file synth1if.h
 * @brief C interface of the techpop engine for the Objective-C dialogs (SynWindow.m).
 *
 * The functions are defined in techpop/synth1.cpp, sound.cpp and common.cpp
 * (extern "C"). Engine objects are passed as void*: pSynth is a CSynthesizer1*, pSound a
 * CSoundData*.
 */
#ifndef __SYNTH1IF_H__
#define __SYNTH1IF_H__

// synth1.cpp
/**
 * @brief Returns the bank number of the current patch.
 *
 * @param pSynth  The engine.
 * @return Bank number, or -1 for a zip bank.
 */
int     Synthesizer1_getCurrentSoundBankNo(void* pSynth);

/**
 * @brief Returns the folder that is scanned for zip banks.
 *
 * @param pSynth  Unused (the path is process-wide).
 * @return Pointer to the path held in s1g.
 */
char*   Synthesizer1_getZipFolderPath(void* pSynth);

/**
 * @brief Returns whether MIDI bank select is enabled.
 *
 * @param pSynth  The engine.
 * @return Nonzero if enabled.
 */
int     Synthesizer1_getEnableBankSelect(void* pSynth);

/**
 * @brief Returns whether MIDI program change is enabled.
 *
 * @param pSynth  The engine.
 * @return Nonzero if enabled.
 */
int     Synthesizer1_getEnableProgramChange(void* pSynth);

/**
 * @brief Returns the current patch.
 *
 * @param pSynth  The engine.
 * @return The current CSoundData.
 */
void*   Synthesizer1_getCurrentSound(void* pSynth);

/**
 * @brief Enables or disables MIDI bank select and saves the setting to the ini file.
 *
 * @param pSynth  The engine.
 * @param b       Nonzero to enable.
 */
void    Synthesizer1_setEnableBankSelect(void* pSynth, BOOL b);

/**
 * @brief Enables or disables MIDI program change and saves the setting to the ini file.
 *
 * @param pSynth  The engine.
 * @param b       Nonzero to enable.
 */
void    Synthesizer1_setEnableProgramChange(void* pSynth, BOOL b);

/**
 * @brief Sets the process-wide CC map and saves it to the ini file.
 *
 * @param pSynth  The engine.
 * @param pMap    128 entries (CC number -> parameter id, -1 for none).
 */
void    Synthesizer1_setCCMap(void* pSynth, int* pMap);

/**
 * @brief Sets the folder of a numbered bank and saves it to the ini file (not in the
 *        GarageBand sandbox).
 *
 * @param pSynth  The engine.
 * @param no      Bank number, 0..99.
 * @param path    Folder path.
 */
void    Synthesizer1_changeBankFolder(void* pSynth, int no, char* path);

/**
 * @brief Sets the zip bank folder and rescans it if the path changed (saved to the ini
 *        file outside the GarageBand sandbox).
 *
 * @param pSynth  Unused.
 * @param path    Folder path.
 */
void    Synthesizer1_updateZipFolderPath(void* pSynth, const char* path);

/**
 * @brief Sets the master tuning and rebuilds the note frequency table.
 *
 * @param pSynth  Unused.
 * @param tune    Offset from A4 = 440 Hz in cents.
 */
void    Synthesizer1_setMasterTune(void* pSynth, int tune);

/**
 * @brief Formats a CC map as 128 comma-separated parameter ids.
 *
 * @param pSynth    Unused.
 * @param pMap      128 entries.
 * @param[out] str  Destination; must hold the whole string (0x400 bytes is enough).
 */
void    Synthesizer1_createCSVStringFromCCMap(void* pSynth, int* pMap, char* str);

/**
 * @brief Parses a CC map from comma/space separated text.
 *
 * @param pSynth        Unused.
 * @param[out] pMap     128 entries (-1 where no valid value was read).
 * @param[in,out] str   Text to parse; modified by strtok().
 * @return TRUE if 128 values were read; otherwise shows a message box and returns FALSE.
 */
int     Synthesizer1_getCCMapFromCSV(void* pSynth, int* pMap, char* str);

/**
 * @brief Gets the name of a program in a bank.
 *
 * @param pSynth     The engine.
 * @param bankNo     Bank number, or -1 for a zip bank given by bankPath.
 * @param bankPath   Zip bank path, may be NULL.
 * @param progNo     Program number, 0..127.
 * @param[out] name  Receives the name, or "not found".
 */
void    Synthesizer1_getProgramName(void* pSynth, int bankNo, char* bankPath, int progNo, char* name);

/**
 * @brief Returns the process-wide CC map (128 entries, CC number -> parameter id).
 *
 * @return Pointer to s1g->m_ccMap.
 * @note The definition takes an unused engine pointer; it is declared here without it.
 */
int*    Synthesizer1_getCCMapTable(void);

/**
 * @brief Gets the bank number of a patch.
 *
 * @param pSound  The patch (CSoundData*).
 * @return Bank number, or -1 for a zip bank.
 */
int     SoundData_getBankNo(void* pSound);

/**
 * @brief Gets the program number of a patch within its bank.
 *
 * @param pSound  The patch (CSoundData*).
 * @return Program number.
 */
int     SoundData_getProgramNo(void* pSound);

/**
 * @brief Gets the name of a patch.
 *
 * @param pSound  The patch (CSoundData*).
 * @return Pointer to the patch's name buffer (32 chars, not a copy).
 */
char*   SoundData_getName(void* pSound);

/**
 * @brief Gets the colour of a patch.
 *
 * @param pSound  The patch (CSoundData*).
 * @return The patch colour value (m_color).
 */
unsigned int SoundData_getColor(void* pSound);

// sound.cpp
/**
 * @brief Gets the patch count and folder of a numbered bank (refreshing its cache).
 *
 * @param bankNo     Bank number, 0..99.
 * @param[out] num   Patch count as text, or "invalid folder" if the folder does not exist.
 * @param[out] path  Bank folder path.
 */
void    SoundLibrary_getBankInfo(int bankNo, char* num, char* path);

/**
 * @brief Formats a bank's menu label as "NN:name (count)".
 *
 * @param bankNo     Bank number, 0..99 (-1 is not supported).
 * @param[out] str   Destination.
 */
void    SoundLibrary_getBankInfo1(int bankNo, char* str);

/**
 * @brief Counts the patches in a bank folder.
 *
 * @param path  Bank folder path.
 * @return Number of patches found.
 */
int     Bank_getPatchNum(char* path);

// common.cpp
/**
 * @brief Reads an integer from the [global] section of the ini file.
 *
 * @param key  Key name.
 * @param def  Default value.
 * @return The stored value, or def if the key is missing.
 */
int     getIniInt(const char* key, int def);

// winobj_au.m
/**
 * @brief Returns whether the plugin runs inside the GarageBand sandbox (the user's
 *        Documents path contains "com.apple.garageband"). The result is cached.
 *
 * @return Nonzero inside the sandbox.
 */
int     isSandBox(void);

/**
 * @brief Parameter table entry for MIDI assignment (C view of MidiAssignParam, synmidi.cpp).
 */
typedef struct {
    int         id;             // +0x00
    int         min;            // +0x04
    int         max;            // +0x08
    int         cc;             // +0x0c  default CC (-1 none, -2 not assignable)
    const char* name;           // +0x10
    int         bAssignable;    // +0x18
    const char* shortName;      // +0x20
} MIDIASSIGNPARAM;
extern MIDIASSIGNPARAM g_midiAssignParams[];
extern const int g_midiAssignParamsNum;

/**
 * @brief Named patch colour (table in common.cpp).
 */
typedef struct {
    const char*     name;
    unsigned int    color;
} DEFINECOLOR;
extern DEFINECOLOR g_defineColors[];
#define DEFINECOLOR_NUM 6

/**
 * @brief Destination and name of a patch save; written by the save dialog (synwin.cpp).
 */
typedef struct tagWriteProgramInfo {
    int     bankNo;
    int     progNo;
    char    szName[32];
    unsigned int color;
} WriteProgramInfo;
extern WriteProgramInfo* g_pWPI;

extern BOOL g_bTransparencyWin;     // drawgui.cpp

#endif
