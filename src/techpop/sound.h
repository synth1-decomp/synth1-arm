#pragma once

/**
 * @file sound.h
 * @brief Patches, banks and the sound library.
 *
 * Patches (.sy1 text files), banks (folders of 128 patches, or zip archives
 * extracted to the temp folder) and the sound library shared by all plugin
 * instances (lives in S1G).
 *
 * A .sy1 file is text: the patch name on the first line, then "color=<name>",
 * "ver=<n>" and one "<param id>,<value>" line per parameter.
 */

#include <string>
#include <vector>
#include <map>
#include "common.h"

#define BANK_PATCH_NUM      128     // patches per bank
#define BANK_FOLDER_NUM     100     // numbered bank folders (soundbank00..99)

// ---------------------------------------------------------------------------
// CSoundData : one patch (parameter id/value list).  sizeof == 0xb60
// (CSoundLibrary embeds one at +0x808, CSynthesizer1 one at +0xc3a0).
// The same definition appears in synth1.h, where its members are documented;
// whichever header is included first provides it (synth1.h includes this one).
#ifndef CSOUNDDATA_DEFINED
#define CSOUNDDATA_DEFINED
#define SOUNDDATA_PARAMS_MAX    100
class CSoundData {
public:
    CSoundData();
    virtual ~CSoundData(){}
    void init();
    void enumParam(int idx, int* pID, int* pVal);
    void setParamVal(int id, int val);
    int getParamVal(int id);
    int getParamsNum();

    char m_name[32];                        // +0x008  ("not defined")
    unsigned int m_color;                   // +0x028
    int m_paramID[SOUNDDATA_PARAMS_MAX];    // +0x02c  (-1 = empty)
    int m_paramVal[SOUNDDATA_PARAMS_MAX];   // +0x1bc
    int m_paramNum;                         // +0x34c
    int m_bankNo;                           // +0x350
    char m_path[0x800];                     // +0x354
    int m_progNo;                           // +0xb54
    int m_version;                          // +0xb58  (113 == 0x71 for V1.13)
};
#endif
// bytes of CSoundData copied by CSoundLibrary::loadProgram (m_name .. m_version)
#define SOUNDDATA_BODY_SIZE 0xb54

/**
 * @brief Where and under which name/color CSoundLibrary::saveProgram() stores the current patch.
 */
typedef struct tagWriteProgramInfo {
    int bankNo;         // +0x00
    int progNo;         // +0x04
    char name[32];      // +0x08
    COLORREF color;     // +0x28
} WriteProgramInfo;

class CBank;

// ---------------------------------------------------------------------------
#define PATCH_NO_INFO   0   // m_ts value: info not loaded yet
#define PATCH_NO_FILE   1   // m_ts value: no patch file

/**
 * @brief One patch slot of a bank (lazily loaded name/color).  sizeof == 0x48
 *
 * The name and color are read from the patch file on demand. m_ts doubles as
 * the state: (0,0) = info not loaded, (1,1) = no patch file.
 * @note m_pBank and m_progNo are set by the owning CBank's constructor.
 */
class CPatch {
public:
    /**
     * @brief Creates a slot whose info is not loaded yet.
     */
    CPatch();
    virtual ~CPatch();

    /**
     * @brief Marks the name/color as not loaded, so the next getName()/getColor() rereads the file.
     */
    void invalidateInfo();

    /**
     * @brief Compares a file time with the stored time stamp.
     *
     * @param ts    Last write time of the patch file.
     * @return TRUE if ts equals m_ts.
     */
    BOOL isSameTS(FILETIME ts);

    /**
     * @brief Marks the slot as having no patch file.
     */
    void setNoFile();

    /**
     * @brief Returns the patch name, loading it from the file if needed.
     *
     * @return The name (points into the object), or the literal "initial sound"
     *         if the slot has no patch file.
     */
    char* getName();

    /**
     * @brief Reads the name (first line, up to 31 characters) and color ("color=" line) from the patch file.
     *
     * A missing or unknown color gives 0xffffffff (default color). If the file
     * cannot be opened or has fewer than two lines, the slot is marked as having
     * no file.
     * @note m_ts is not updated on success, so the info is still considered
     *       "not loaded" and the file is reread on each getName()/getColor().
     */
    void loadPatchInfo();

    /**
     * @brief Returns the patch color, loading it from the file if needed.
     *
     * @return The color, or 0xffffffff if there is no patch file or no color.
     */
    COLORREF getColor();

    /**
     * @brief Checks whether the name/color have been loaded.
     *
     * @return TRUE if the name/color have not been loaded (m_ts == (0,0)).
     */
    BOOL isInfoNotLoaded(){ return m_ts.dwLowDateTime == PATCH_NO_INFO && m_ts.dwHighDateTime == PATCH_NO_INFO; }

    /**
     * @brief Checks whether the slot has a patch file.
     *
     * @return TRUE if the slot has no patch file (m_ts == (1,1)).
     */
    BOOL isNoFile(){ return m_ts.dwLowDateTime == PATCH_NO_FILE && m_ts.dwHighDateTime == PATCH_NO_FILE; }

    int m_progNo;       // +0x08
    CBank* m_pBank;     // +0x10
    char m_name[32];    // +0x18
    COLORREF m_color;   // +0x38
    FILETIME m_ts;      // +0x3c  (0,0 = not loaded, 1,1 = no file)
};

/**
 * @brief Zip banks by inner path (zip file name plus sub folders); the map does not own the banks.
 */
typedef std::map<std::string, CBank*> BankMap;

// ---------------------------------------------------------------------------
/**
 * @brief A bank: a folder of up to 128 patches (001.sy1 .. 128.sy1), possibly with
 * child banks (sub folders of an extracted zip).  sizeof == 0x3940 (14656)
 *
 * The 100 numbered banks live in S1G (m_bankNo 0..99). Zip banks (m_bankNo -1)
 * are created with new and form a tree through m_children.
 * @note A bank deletes its children unless m_bNotOwnChildren is set.
 */
class CBank {
public:
    /**
     * @brief Creates an empty bank (no path) and links its 128 patch slots to it.
     */
    CBank();

    /**
     * @brief Deletes the child banks unless m_bNotOwnChildren is set.
     */
    virtual ~CBank();

    /**
     * @brief Builds the path of a patch file in this bank.
     *
     * @param progNo    Program number 0..127 (file "NNN.sy1" with NNN = progNo + 1),
     *                  or -1 for the wildcard "???.sy1".
     * @param[out] path Receives the path; set to "" if the bank has no folder.
     * @return FALSE if the bank has no folder, TRUE otherwise.
     */
    BOOL getProgPath(int progNo, char* path);

    /**
     * @brief Gets the number of child banks.
     *
     * @return The number of child banks.
     */
    int getChildNum();

    /**
     * @brief Gets a child bank by index.
     *
     * @param idx   Index 0..getChildNum()-1 (no range check).
     * @return Child bank idx (still owned by this bank unless m_bNotOwnChildren is set).
     */
    CBank* getChild(int idx);

    /**
     * @brief Sets the bank folder and rescans it.
     *
     * Also sets the readme path (folder/readme.txt) and m_name (the folder name),
     * invalidates all patch infos and calls checkCache().
     * @param path  Folder path.
     */
    void setPath(char* path);

    /**
     * @brief Gets the bank folder path.
     *
     * @return The bank folder path (m_path, points into the object; "" if none).
     */
    char* getPath() { return m_path; }

    /**
     * @brief Scans the bank folder for patch files.
     *
     * Sets m_bExist and m_patchNum (number of NNN.sy1 files, NNN 001..128).
     * Slots without a file are marked as having no file; slots whose file time
     * differs from the stored one are invalidated.
     */
    void checkCache();

    /**
     * @brief Extracts a zip bank into the temp folder, unless already done.
     *
     * The target is <temp>/synth1/zip/<zip name without "zip">NNNNN, where NNNNN
     * is hash in hex (e.g. "bank.0012f"). A file "extractDone" in the target marks
     * a completed extraction and makes later calls return at once.
     * @param zipPath       Path of the zip file.
     * @param[out] outPath  Receives the extraction folder (at least 1024 bytes).
     * @param hash          Value that makes the folder name unique (the zip file size).
     * @return TRUE if the extracted folder is available.
     */
    static BOOL extractZipBank(char* zipPath, char* outPath, unsigned int hash);

    /**
     * @brief Makes this bank the root of a zip archive.
     *
     * Extracts the zip, uses the extracted folder as the bank folder, sets m_name
     * and m_innerPath to the zip file name and creates the child banks
     * (constructZipBank()).
     * @param zipPath   Path of the zip file.
     * @param timeStamp Not used.
     * @param hash      Passed to extractZipBank().
     * @param pMap      Map that receives the child banks.
     * @return FALSE if the extraction failed.
     * @throws std::bad_alloc if allocating the child banks or map entries fails.
     */
    BOOL setZipPath(char* zipPath, unsigned int timeStamp, unsigned int hash, BankMap* pMap);

    /**
     * @brief Creates child banks for the sub folders of path, recursively.
     *
     * Each child gets m_bankNo -1, its folder and an inner path below this
     * bank's, and is added to m_children and to pMap. The children are then
     * sorted by inner path.
     * @param path  Folder to scan (this bank's folder).
     * @param pMap  Map that receives the child banks, keyed by inner path.
     * @throws std::bad_alloc if allocating a bank or map entry fails.
     */
    void constructZipBank(char* path, BankMap* pMap);

    /**
     * @brief Sets m_innerPath to parent + "/" + the name of this bank's folder.
     *
     * @param parent    Inner path of the parent bank.
     */
    void makeInnerPath(char* parent);

    /**
     * @brief Sort predicate: orders banks by inner path (strcmp).
     *
     * @param a     First bank.
     * @param b     Second bank.
     * @return true if a sorts before b.
     */
    static bool bankComp(CBank* a, CBank* b);

    /**
     * @brief Appends a child bank; this bank takes ownership.
     *
     * @param pBank Bank to append (allocated with new).
     * @throws std::bad_alloc if the vector cannot grow.
     */
    void addChildBank(CBank* pBank);

    /**
     * @brief Gets the program number from a patch file name ("001.sy1" -> 0).
     *
     * @param name  File name.
     * @return Program number (first three digits minus 1), or -1 if the name does
     *         not start with three digits.
     */
    static int getProgNoFromFilename(char* name);

    int m_bankNo;                   // +0x0008  (-1 for zip banks)
    int m_patchNum;                 // +0x000c
    char m_name[0x100];             // +0x0010
    char m_path[0x400];             // +0x0110
    char m_readmePath[0x400];       // +0x0510
    char m_zipPath[0x400];          // +0x0910
    char m_innerPath[0x800];        // +0x0d10  (zip name / sub folders, map key)
    BOOL m_bNotOwnChildren;         // +0x1510
    CPatch m_patch[BANK_PATCH_NUM]; // +0x1518
    LONGLONG m_checkTime;           // +0x3918
    BOOL m_bExist;                  // +0x3920
    std::vector<CBank*> m_children; // +0x3928
};

// ---------------------------------------------------------------------------
/**
 * @brief Sound library: patch loading/saving for one plugin instance; the bank
 * data itself is shared through S1G.  sizeof == 0x1368
 *
 * An instance holds the requested bank/program and the loaded patch (m_sound).
 * The static functions work on the shared banks: the 100 numbered folder banks
 * and the zip banks found in the zip folder.
 * @note The first instance creates the shared bank data and the last one frees
 *       the zip banks (reference counted under S1G's m_csLib).
 */
class CSoundLibrary {
public:
    /**
     * @brief Registers an instance; the first one sets up the shared banks.
     *
     * The first instance reads the folders of banks 0..99 from the settings
     * ("bankfolderN"; default ~/Documents/Synth1Patches/soundbankNN, always the
     * default when sandboxed) and the zip folder ("zipfolder"; default
     * ~/Documents/Synth1Patches/zipbank), creates soundbank00 and the zip folder
     * if they are the defaults and missing, and scans the zip banks.
     * @note m_reqProgNo, m_reqBankNo and m_reqZipPath are not initialized here.
     * @throws std::bad_alloc if allocating the zip bank containers fails.
     */
    CSoundLibrary();

    /**
     * @brief Unregisters an instance; the last one deletes the zip banks and their containers.
     */
    ~CSoundLibrary();

    /**
     * @brief Rebuilds the zip banks if the contents of the zip folder changed.
     *
     * Does nothing while the zip banks are locked (lockZipBank()) or if the zip
     * folder hash (getZipFilesHash()) is unchanged. Otherwise deletes all zip
     * banks, extracts and loads each *.zip as a root bank, sorts the roots by
     * path, and replaces any root that holds no patches and exactly one sub
     * folder by that sub folder (which takes the root's name and readme).
     * Runs under S1G's m_csZip.
     * @note The map entry of a replaced root is not removed and keeps pointing to
     *       the deleted bank.
     * @throws std::bad_alloc if allocating banks or map entries fails.
     */
    static void checkZipFile();

    /**
     * @brief Sets the folder of a numbered bank and stores it in the settings (unless sandboxed).
     *
     * @param bankNo    Bank number 0..99.
     * @param path      New folder.
     * @return Always TRUE.
     */
    BOOL changeBankFolder(int bankNo, char* path);

    /**
     * @brief Returns a bank by number, or a zip bank by inner path.
     *
     * @param bankNo    0..99 for a numbered bank (no range check), -1 for a zip bank.
     * @param zipPath   Inner path of the zip bank (used only when bankNo is -1).
     * @return The bank, or NULL if bankNo is -1 and zipPath is NULL or not found.
     * @throws std::bad_alloc if the key string cannot be allocated.
     */
    static CBank* getBank(int bankNo, char* zipPath);

    /**
     * @brief Changes the zip folder, stores it in the settings (unless sandboxed) and rescans.
     *
     * Does nothing if path equals the current zip folder.
     * @param path  New zip folder.
     */
    static void updateZipFolderPath(char* path);

    /**
     * @brief Increments the zip bank lock count; checkZipFile() does nothing while it is nonzero.
     *
     * Runs under S1G's m_csZipLock.
     */
    static void lockZipBank();

    /**
     * @brief Decrements the zip bank lock count.
     *
     * Runs under S1G's m_csZipLock.
     */
    static void unlockZipBank();

    /**
     * @brief Gets the zip folder path.
     *
     * @return The zip folder path (shared buffer S1G::m_zipFolder).
     */
    static char* getZipFolderPath();

    /**
     * @brief Computes a hash of the zip files in a folder.
     *
     * Sums a string hash (h = h * 31 + c) of "path:size:mtime" over the *.zip files.
     * @param folder    Folder to scan.
     * @return The hash, or 0 if the folder cannot be opened.
     */
    static int getZipFilesHash(char* folder);

    /**
     * @brief Sort predicate: orders zip root banks by folder path (strcmp).
     *
     * @param a     First bank.
     * @param b     Second bank.
     * @return true if a sorts before b.
     */
    static bool rootBankComp(CBank* a, CBank* b);

    /**
     * @brief Gets the number of zip root banks.
     *
     * @return The number of zip root banks.
     */
    static int getZipNum();

    /**
     * @brief Gets a zip root bank by index.
     *
     * @param idx   Index 0..getZipNum()-1.
     * @return Zip root bank idx.
     * @throws std::out_of_range if idx is not a valid index.
     */
    static CBank* getZipRootBank(int idx);

    /**
     * @brief Gets the name and color of a patch.
     *
     * @param bankNo        Bank number 0..99, or -1 for a zip bank.
     * @param zipPath       Inner path of the zip bank (when bankNo is -1).
     * @param progNo        Program number 0..127.
     * @param[out] name     Receives the name ("not found" if the bank does not exist,
     *                      "initial sound" if the patch file does not exist); may be NULL.
     * @param[out] pColor   Receives the color (0xffffffff = default); may be NULL.
     * @param bCheckCache   Nonzero to rescan a numbered bank's folder first.
     * @return FALSE if the bank does not exist, TRUE otherwise.
     * @throws std::bad_alloc if the zip bank lookup cannot allocate its key.
     */
    static BOOL getProgramNameColor(int bankNo, char* zipPath, int progNo, char* name, unsigned int* pColor, int bCheckCache);

    /**
     * @brief Sets the requested program number (clamped later by isReqPatchLoaded()).
     *
     * @param progNo    Requested program number.
     */
    void setReqProgNo(int progNo);

    /**
     * @brief Sets the requested bank.
     *
     * @param bankNo    Bank number 0..99, or -1 for a zip bank.
     * @param zipPath   Inner path of the zip bank; NULL clears it.
     */
    void setReqBankNo(int bankNo, char* zipPath);

    /**
     * @brief Clamps the request and tells whether m_sound already holds the requested patch.
     *
     * The requested program is clamped to 0..127 and a numbered bank to 0..99
     * (both written back).
     * @return TRUE if m_sound has the requested bank (and zip path) and program.
     */
    BOOL isReqPatchLoaded();

    /**
     * @brief Loads the requested patch into m_sound.
     *
     * m_sound starts as a copy of pInitSound (name, color, parameters, version),
     * then takes the requested bank/zip path/program. If the patch file can be
     * read, its name, color, version (100 if no "ver=" line) and parameters are
     * applied; parameters missing from the file keep pInitSound's values.
     * @param pInitSound    Default sound.
     * @return Pointer to m_sound.
     */
    CSoundData* loadProgram(CSoundData* pInitSound);

    /**
     * @brief Writes m_sound to a patch file of a numbered bank.
     *
     * Writes the name from pInfo, "color=", "ver=" and the parameters of m_sound,
     * creating the bank folder if needed. On success m_sound takes the bank,
     * program, name and color from pInfo and the slot's cached info is
     * invalidated. Runs under S1G's m_csZip.
     * @param pInfo     Target bank (0..99), program, name and color.
     * @return 1 on success, 0 if the bank has no folder, -1 for a zip bank
     *         (bankNo -1) or a file error.
     */
    int saveProgram(WriteProgramInfo* pInfo);

    int m_reqProgNo;            // +0x000
    int m_reqBankNo;            // +0x004
    char m_reqZipPath[0x800];   // +0x008
    CSoundData m_sound;         // +0x808
};

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Rescans a numbered bank and reports its patch count and folder.
 *
 * @param bankNo        Bank number 0..99.
 * @param[out] num      Receives the patch count as text, or "invalid folder" if the folder does not exist.
 * @param[out] path     Receives the bank folder.
 */
void SoundLibrary_getBankInfo(int bankNo, char* num, char* path);

/**
 * @brief Formats a one-line description of a numbered bank: "NN:name (count)".
 *
 * Uses the cached patch count (no rescan).
 * @param bankNo        Bank number 0..99 (-1 is not supported).
 * @param[out] str      Receives the text.
 */
void SoundLibrary_getBankInfo1(int bankNo, char* str);

/**
 * @brief Counts the patch files (001.sy1 .. 128.sy1) in a folder.
 *
 * @param path  Folder to scan.
 * @return Number of patch files found.
 */
int Bank_getPatchNum(char* path);
#ifdef __cplusplus
}
#endif
