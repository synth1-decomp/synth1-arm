/**
 * @file sound.cpp
 * @brief Patch files (.sy1), banks (patch folders and extracted zip archives) and
 * the sound library.
 *
 * Implements CSoundData, CPatch, CBank and CSoundLibrary, plus the text file and
 * zip extraction helpers they use.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <dirent.h>
#include <sys/stat.h>
#include <algorithm>
#include <CoreServices/CoreServices.h>
#include "common.h"
#include "debug.h"
#include "unzip.h"
#include "sound.h"
#include "synth1.h"


// ---------------------------------------------------------------------------
// text file helpers

/**
 * @brief Opens a text file for reading.
 *
 * @param path  File path.
 * @return An opaque file handle (a FILE*), or NULL on failure.
 */
void* openFile(char* path)
{
    return fopen(path, "rt");
}

/**
 * @brief Closes a file opened with openFile().
 *
 * @param fp    File handle.
 */
void closeFile(void* fp)
{
    fclose((FILE*)fp);
}

// read one line and strip the trailing CR/LF
/**
 * @brief Reads one line from a file opened with openFile() and strips the line end.
 *
 * Clears a CR or LF in the last and in the second-to-last position, so both
 * "\n" and "\r\n" endings are removed. A line longer than size - 1 characters
 * is returned in pieces.
 * @param fp        File handle.
 * @param[out] buf  Receives the line.
 * @param size      Size of buf in bytes.
 * @return FALSE at end of file or on error, TRUE otherwise.
 * @note For a one-character line the byte before buf is examined (and cleared if
 *       it is a CR or LF).
 */
BOOL readLine(void* fp, char* buf, int size)
{
    if (fgets(buf, size, (FILE*)fp) == NULL) {
        return FALSE;
    }
    int len = strlen(buf);
    if (len > 0) {
        if (buf[len - 1] == '\r' || buf[len - 1] == '\n') {
            buf[len - 1] = 0;
        }
        if (buf[len - 2] == '\r' || buf[len - 2] == '\n') {
            buf[len - 2] = 0;
        }
    }
    return TRUE;
}

// extract all files of a zip archive into outDir
/**
 * @brief Extracts all entries of a zip archive into a folder.
 *
 * Directory entries are created as folders; file entries are written with their
 * parent folders created as needed. On failure a message box is shown.
 * @param zipPath   Path of the zip file.
 * @param outDir    Destination folder.
 * @return TRUE if every entry was extracted, FALSE on the first error.
 */
// Note: the goto/while/break structure and where name, fp and buf are declared are
// significant for reproducing the released binary.
BOOL unzip(char* zipPath, char* outDir)
{
    char path[1024];
    unz_file_info info;
    unzFile uf = unzOpen(zipPath);
    if (uf == NULL) goto failed;
    while (1) {
        char name[512];
        if (unzGetCurrentFileInfo(uf, &info, name, sizeof(name), NULL, 0, NULL, 0) != UNZ_OK) goto error;
        if (*getLastChar(name) == '/') {
            // directory
            connectPath(path, outDir, name);
            myMkDir(path);
        } else {
            if (unzOpenCurrentFile(uf) != UNZ_OK) goto error;
            connectPath(path, outDir, name);
            cutFileName(path);
            myMkDir(path);
            connectPath(path, outDir, name);
            FILE* fp = fopen(path, "wb");
            if (fp != NULL) {
                char buf[0x2000];
                unsigned int size;
                while ((size = unzReadCurrentFile(uf, buf, sizeof(buf))) != 0) {
                    unsigned int written = fwrite(buf, 1, size, fp);
                    if (written != size) {
                        fclose(fp);
                        fp = NULL;
                        break;
                    }
                }
                if (fp) {
                    fclose(fp);
                }
            }
            unzCloseCurrentFile(uf);
            if (fp == NULL) goto error;
        }
        if (unzGoToNextFile(uf) == UNZ_END_OF_LIST_OF_FILE) break;
    }
    unzClose(uf);
    return TRUE;
error:
    unzClose(uf);
failed:
    sprintf(path, "Can't extract zip.\n[%s]\n->[%s]\n", zipPath, outDir);
    MessageBox(NULL, path, "Synth1", MB_OKCANCEL);
    edPrintf("解凍失敗 [%s]  [%s]\n", zipPath, outDir);
    return FALSE;
}

// ---------------------------------------------------------------------------
// CSoundData

CSoundData::CSoundData()
{
    init();
}

void CSoundData::init()
{
    for (int i = 0; i < SOUNDDATA_PARAMS_MAX; i++) {
        m_paramID[i] = -1;
        m_paramVal[i] = 0;
    }
    m_paramNum = 0;
    strcpy(m_name, "not defined");
}

void CSoundData::enumParam(int idx, int* pID, int* pVal)
{
    *pID = m_paramID[idx];
    *pVal = m_paramVal[idx];
}

void CSoundData::setParamVal(int id, int val)
{
    for (int i = 0; i < sizeof(m_paramID) / sizeof(m_paramID[0]); i++) {
        if (m_paramID[i] == id) {
            m_paramVal[i] = val;
            return;
        }
        if (m_paramID[i] == -1) {
            m_paramID[i] = id;
            m_paramVal[i] = val;
            m_paramNum++;
            return;
        }
    }
}

int CSoundData::getParamVal(int id)
{
    for (int i = 0; i < m_paramNum; i++) {
        if (m_paramID[i] == id) {
            return m_paramVal[i];
        }
    }
    return 0;
}

int CSoundData::getParamsNum()
{
    return m_paramNum;
}

// ---------------------------------------------------------------------------
// CPatch

CPatch::CPatch()
{
    invalidateInfo();
}

void CPatch::invalidateInfo()
{
    m_ts.dwLowDateTime = PATCH_NO_INFO;
    m_ts.dwHighDateTime = PATCH_NO_INFO;
}

CPatch::~CPatch()
{
}

BOOL CPatch::isSameTS(FILETIME ts)
{
    return m_ts.dwLowDateTime == ts.dwLowDateTime && m_ts.dwHighDateTime == ts.dwHighDateTime;
}

void CPatch::setNoFile()
{
    m_ts.dwLowDateTime = PATCH_NO_FILE;
    m_ts.dwHighDateTime = PATCH_NO_FILE;
}

char* CPatch::getName()
{
    if (isInfoNotLoaded()) {
        loadPatchInfo();
    }
    if (isNoFile()) {
        return "initial sound";
    }
    return m_name;
}

COLORREF CPatch::getColor()
{
    if (isInfoNotLoaded()) {
        loadPatchInfo();
    }
    if (isNoFile()) {
        return 0xffffffff;
    }
    return m_color;
}

// read the name and color lines of the patch file
void CPatch::loadPatchInfo()
{
    char buf[260];
    void* fp = INVALID_HANDLE_VALUE;
    if (!m_pBank->getProgPath(m_progNo, buf)) goto error;
    fp = openFile(buf);
    if (fp == NULL) goto error;
    if (!readLine(fp, m_name, 32)) goto error;
    if (!readLine(fp, buf, 255)) goto error;
    if (strncmp(buf, "color=", 6) == 0) {
        m_color = getColorFromString(buf + 6);
    } else {
        m_color = 0xffffffff;
    }
    closeFile(fp);
    return;
error:
    setNoFile();
    if (fp != INVALID_HANDLE_VALUE) {
        closeFile(fp);
    }
}

// ---------------------------------------------------------------------------
// CBank

CBank::CBank()
{
    m_path[0] = 0;
    m_readmePath[0] = 0;
    m_zipPath[0] = 0;
    m_bNotOwnChildren = FALSE;
    m_bExist = FALSE;
    m_checkTime = 0;
    m_name[0] = 0;
    m_bankNo = 0;
    m_patchNum = 0;
    for (int i = 0; i < BANK_PATCH_NUM; i++) {
        m_patch[i].m_progNo = i;
        m_patch[i].m_pBank = this;
    }
    m_innerPath[0] = 0;
}

CBank::~CBank()
{
    if (!m_bNotOwnChildren) {
        for (unsigned int i = 0; i < m_children.size(); i++) {
            if (m_children[i]) {
                delete m_children[i];
            }
        }
    }
}

int CBank::getChildNum()
{
    return m_children.size();
}

CBank* CBank::getChild(int idx)
{
    return m_children[idx];
}

void CBank::setPath(char* path)
{
    strcpy(m_path, path);
    connectPath(m_readmePath, path, "readme.txt");
    getFileName(m_name, path, sizeof(m_name));
    m_checkTime = 0;
    for (int i = 0; i < BANK_PATCH_NUM; i++) {
        m_patch[i].invalidateInfo();
    }
    checkCache();
}

// extract a zip bank to <temp>/synth1/zip/<name>.<hash>
BOOL CBank::extractZipBank(char* zipPath, char* outPath, unsigned int hash)
{
    char buf[1024];
    GetTempPath(1024, outPath);
    addPath(outPath, "synth1");
    addPath(outPath, "zip");
    getFileName(buf, zipPath, sizeof(buf));
    buf[strlen(buf) - 3] = 0;
    addPath(outPath, buf);
    sprintf(buf, "%05x", hash);
    strcat(outPath, buf);
    connectPath(buf, outPath, "extractDone");
    if (isExistFile(buf, FALSE)) {
        return TRUE;
    }
    if (!myMkDir(outPath)) {
        return FALSE;
    }
    BOOL ret = unzip(zipPath, outPath);
    if (!ret) {
        return FALSE;
    }
    FILE* fp = fopen(buf, "wt");
    if (fp) {
        fclose(fp);
    }
    return ret;
}

BOOL CBank::setZipPath(char* zipPath, unsigned int timeStamp, unsigned int hash, BankMap* pMap)
{
    char path[1024];
    strcpy(m_zipPath, zipPath);
    if (!extractZipBank(m_zipPath, path, hash)) {
        return FALSE;
    }
    setPath(path);
    getFileName(m_name, m_zipPath, sizeof(m_name));
    getFileName(m_innerPath, m_zipPath, sizeof(m_innerPath));
    constructZipBank(path, pMap);
    return TRUE;
}

void CBank::makeInnerPath(char* parent)
{
    char name[1024];
    getFileName(name, m_path, sizeof(name));
    connectPath(m_innerPath, parent, name);
}

bool CBank::bankComp(CBank* a, CBank* b)
{
    return strcmp(a->m_innerPath, b->m_innerPath) < 0;
}

// create child banks for the sub folders of path (recursively)
void CBank::constructZipBank(char* path, BankMap* pMap)
{
    char findPath[1040];
    char subPath[1040];
    WIN32_FIND_DATA fd;

    connectPath(findPath, path, "*.*");
    HANDLE hFind = FindFirstFile(findPath, &fd);
    if (hFind != INVALID_HANDLE_VALUE) {
        do {
            if ((fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) &&
                strcmp(fd.cFileName, ".") != 0 && strcmp(fd.cFileName, "..") != 0) {
                CBank* pBank = new CBank;
                pBank->m_bankNo = -1;
                connectPath(subPath, path, fd.cFileName);
                pBank->setPath(subPath);
                pBank->makeInnerPath(m_innerPath);
                addChildBank(pBank);
                pMap->insert(std::make_pair(std::string(pBank->m_innerPath), pBank));
                pBank->constructZipBank(subPath, pMap);
            }
        } while (FindNextFile(hFind, &fd));
        FindClose(hFind);
    }
    std::sort(m_children.begin(), m_children.end(), bankComp);
}

void CBank::addChildBank(CBank* pBank)
{
    m_children.push_back(pBank);
}

// "001.sy1" -> 0
int CBank::getProgNoFromFilename(char* name)
{
    if (name[0] >= '0' && name[0] <= '9' &&
        name[1] >= '0' && name[1] <= '9' &&
        name[2] >= '0' && name[2] <= '9') {
        return (name[0] - '0') * 100 + (name[1] - '0') * 10 + (name[2] - '0') - 1;
    }
    return -1;
}

// progNo == -1 gives the wildcard path of all patches
BOOL CBank::getProgPath(int progNo, char* path)
{
    if (strlen(m_path) == 0) {
        *path = 0;
        return FALSE;
    }
    if (progNo == -1) {
        connectPath(path, m_path, "???.sy1");
    } else {
        char name[10];
        sprintf(name, "%03d.sy1", progNo + 1);
        connectPath(path, m_path, name);
    }
    return TRUE;
}

// scan the bank folder for patch files
void CBank::checkCache()
{
    char path[256];
    int bFound[BANK_PATCH_NUM];
    WIN32_FIND_DATA fd;

    if (m_checkTime == -1) {
        m_checkTime = 0;
    }
    m_patchNum = 0;
    memset(bFound, 0, sizeof(bFound));
    m_bExist = isExistFile(m_path, TRUE);
    if (m_bExist && getProgPath(-1, path)) {
        HANDLE hFind = FindFirstFile(path, &fd);
        if (hFind != INVALID_HANDLE_VALUE) {
            do {
                char* name = fd.cFileName;
                if (strcmp(&name[3], ".sy1") == 0 &&
                    name[0] >= '0' && name[0] <= '9' &&
                    name[1] >= '0' && name[1] <= '9' &&
                    name[2] >= '0' && name[2] <= '9') {
                    int no = (name[0] - '0') * 100 + (name[1] - '0') * 10 + (name[2] - '0') - 1;
                    if (no >= 0 && no < BANK_PATCH_NUM) {
                        bFound[no] = TRUE;
                        if (!m_patch[no].isSameTS(fd.ftLastWriteTime)) {
                            m_patch[no].invalidateInfo();
                        }
                        m_patchNum++;
                    }
                }
            } while (FindNextFile(hFind, &fd));
            FindClose(hFind);
        }
    }
    for (int i = 0; i < BANK_PATCH_NUM; i++) {
        if (!bFound[i]) {
            m_patch[i].setNoFile();
        }
    }
}

// ---------------------------------------------------------------------------
// CSoundLibrary

CSoundLibrary::CSoundLibrary()
{
    CMyCSLock lock(&s1g->m_csLib);
    s1g->m_soundLibRefCount++;

    FSRef ref;
    char key[256];
    char folder[256];
    char defPath[1024];
    char docPath[1024];
    CFURLRef url;
    CFStringRef str;
    strcpy(docPath, "/");
    if (FSFindFolder(kUserDomain, kDocumentsFolderType, kCreateFolder, &ref) == noErr) {
        url = CFURLCreateFromFSRef(NULL, &ref);
        if (url) {
            str = CFURLCopyFileSystemPath(url, kCFURLPOSIXPathStyle);
            if (str) {
                CFStringGetCString(str, docPath, sizeof(docPath), kCFStringEncodingUTF8);
                CFRelease(str);
            }
            CFRelease(url);
        }
    }

    if (s1g->m_soundLibRefCount == 1) {
        for (int i = 0; i < BANK_FOLDER_NUM; i++) {
            CBank* pBank = &s1g->m_banks[i];
            pBank->m_bankNo = i;
            sprintf(key, "bankfolder%d", i);
            if (docPath[strlen(docPath) - 1] != '/') {
                sprintf(defPath, "%s/Synth1Patches/soundbank%02d", docPath, i);
            } else {
                sprintf(defPath, "%sSynth1Patches/soundbank%02d", docPath, i);
            }
            if (isSandBox()) {
                strcpy(folder, defPath);
            } else {
                getIniString(key, folder, sizeof(folder), defPath);
            }
            if (i == 0 && strcmp(folder, defPath) == 0 && !isExistFile(defPath, TRUE)) {
                myMkDir(defPath);
            }
            pBank->setPath(folder);
        }
        if (docPath[strlen(docPath) - 1] != '/') {
            sprintf(defPath, "%s/Synth1Patches/zipbank", docPath);
        } else {
            sprintf(defPath, "%sSynth1Patches/zipbank", docPath);
        }
        if (isSandBox()) {
            strcpy(s1g->m_zipFolder, defPath);
        } else {
            getIniString("zipfolder", s1g->m_zipFolder, sizeof(s1g->m_zipFolder), defPath);
        }
        if (strcmp(s1g->m_zipFolder, defPath) == 0 && !isExistFile(defPath, TRUE)) {
            myMkDir(defPath);
        }
        s1g->m_zipCheckTime = 0;
        s1g->m_pZipRootBanks = new std::vector<CBank*>;
        s1g->m_pZipBankMap = new BankMap;
        checkZipFile();
    }
}

CSoundLibrary::~CSoundLibrary()
{
    CMyCSLock lock(&s1g->m_csLib);
    if (--s1g->m_soundLibRefCount == 0) {
        for (unsigned int i = 0; i < s1g->m_pZipRootBanks->size(); i++) {
            if (s1g->m_pZipRootBanks->at(i)) {
                delete s1g->m_pZipRootBanks->at(i);
            }
        }
        delete s1g->m_pZipRootBanks;
        delete s1g->m_pZipBankMap;
        s1g->m_zipFilesHash = 0;
    }
}

BOOL CSoundLibrary::changeBankFolder(int bankNo, char* path)
{
    char key[256];
    getBank(bankNo, NULL)->setPath(path);
    sprintf(key, "bankfolder%d", bankNo);
    if (!isSandBox()) {
        setIniString(key, path);
    }
    return TRUE;
}

// bankNo == -1: zip bank given by its inner path
CBank* CSoundLibrary::getBank(int bankNo, char* zipPath)
{
    CBank* pBank = NULL;
    if (bankNo == -1) {
        if (zipPath) {
            BankMap::iterator it = s1g->m_pZipBankMap->find(std::string(zipPath));
            if (it != s1g->m_pZipBankMap->end()) {
                pBank = it->second;
            }
        }
    } else {
        pBank = &s1g->m_banks[bankNo];
    }
    return pBank;
}

void CSoundLibrary::updateZipFolderPath(char* path)
{
    if (strcmp(path, s1g->m_zipFolder) == 0) {
        return;
    }
    strcpy(s1g->m_zipFolder, path);
    if (!isSandBox()) {
        setIniString("zipfolder", path);
    }
    s1g->m_zipCheckTime = 0;
    checkZipFile();
}

void SoundLibrary_getBankInfo(int bankNo, char* num, char* path)
{
    CBank* pBank = CSoundLibrary::getBank(bankNo, NULL);
    pBank->checkCache();
    if (!pBank->m_bExist) {
        strcpy(num, "invalid folder");
    } else {
        sprintf(num, "%d", pBank->m_patchNum);
    }
    strcpy(path, pBank->m_path);
}

void SoundLibrary_getBankInfo1(int bankNo, char* str)
{
    CBank* pBank = (bankNo == -1) ? NULL : &s1g->m_banks[bankNo];
    sprintf(str, "%02d:%s (%d)", bankNo, pBank->m_name, pBank->m_patchNum);
}

int Bank_getPatchNum(char* path)
{
    CBank bank;
    bank.setPath(path);
    return bank.m_patchNum;
}

void CSoundLibrary::lockZipBank()
{
    CMyCSLock lock(&s1g->m_csZipLock);
    s1g->m_zipBankLock++;
}

void CSoundLibrary::unlockZipBank()
{
    CMyCSLock lock(&s1g->m_csZipLock);
    s1g->m_zipBankLock--;
}

char* CSoundLibrary::getZipFolderPath()
{
    return s1g->m_zipFolder;
}

// hash of the names, sizes and dates of the zip files in folder
int CSoundLibrary::getZipFilesHash(char* folder)
{
    char path[1280];
    struct stat st;
    int hash = 0;
    DIR* dir = opendir(folder);
    if (dir == NULL) {
        return 0;
    }
    struct dirent* ent;
    while ((ent = readdir(dir)) != NULL) {
        if (ent->d_type & DT_DIR) {
            continue;
        }
        if (strcmp(".zip", ent->d_name + strlen(ent->d_name) - 4) != 0) {
            continue;
        }
        strcpy(path, folder);
        if (path[strlen(path) - 1] != '/') {
            strcat(path, "/");
        }
        strcat(path, ent->d_name);
        if (stat(path, &st) != 0) {
            continue;
        }
        unsigned long long size = st.st_size;
        unsigned long long time = st.st_mtime;
        sprintf(path + strlen(path), ":%llu:%llu", size, time);
        int h = 0;
        char* p = path;
        char c;
        while ((c = *p++) != 0) {
            h = h * 31 + c;
        }
        hash += h;
    }
    closedir(dir);
    return hash;
}

// (getPath() is called here rather than reading the member directly on purpose: it is
// significant for reproducing the released binary)
bool CSoundLibrary::rootBankComp(CBank* a, CBank* b)
{
    return strcmp(a->getPath(), b->getPath()) < 0;
}

int CSoundLibrary::getZipNum()
{
    return s1g->m_pZipRootBanks->size();
}

CBank* CSoundLibrary::getZipRootBank(int idx)
{
    return s1g->m_pZipRootBanks->at(idx);
}

// rebuild the zip banks if the zip folder changed
// Note: where the locals are declared, building the map entry with the pair constructor
// (not make_pair), reading the child count before the if, and the `name` local are
// significant for reproducing the released binary.
void CSoundLibrary::checkZipFile()
{
    if (s1g->m_zipBankLock > 0) {
        return;
    }
    CMyCSLock lock(&s1g->m_csZip);
    int hash = getZipFilesHash(s1g->m_zipFolder);
    if (s1g->m_zipFilesHash == hash) {
        return;
    }
    s1g->m_zipFilesHash = hash;
    if (s1g->m_zipCheckTime == -1) {
        s1g->m_zipCheckTime = 0;
    }
    for (unsigned int i = 0; i < s1g->m_pZipRootBanks->size(); i++) {
        if (s1g->m_pZipRootBanks->at(i)) {
            delete s1g->m_pZipRootBanks->at(i);
        }
    }
    s1g->m_pZipRootBanks->clear();
    s1g->m_pZipBankMap->clear();
    char findPath[1040];
    char path[1024];
    WIN32_FIND_DATA fd;
    unsigned int timeStamp;
    HANDLE hFind;
    if (!isExistFile(s1g->m_zipFolder, TRUE)) {
        return;
    }
    connectPath(findPath, s1g->m_zipFolder, "*.zip");
    hFind = FindFirstFile(findPath, &fd);
    if (hFind != INVALID_HANDLE_VALUE) {
        do {
            if (!(fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) && endWith(fd.cFileName, ".zip")) {
                CBank* pBank = new CBank;
                pBank->m_bankNo = -1;
                connectPath(path, s1g->m_zipFolder, fd.cFileName);
                BOOL bOK = pBank->setZipPath(path, timeStamp, fd.nFileSizeLow, s1g->m_pZipBankMap);
                if (bOK) {
                    s1g->m_pZipRootBanks->push_back(pBank);
                    s1g->m_pZipBankMap->insert(std::pair<std::string, CBank*>(pBank->m_innerPath, pBank));
                }
            }
        } while (FindNextFile(hFind, &fd));
        FindClose(hFind);
    }
    std::sort(s1g->m_pZipRootBanks->begin(), s1g->m_pZipRootBanks->end(), rootBankComp);

    // a zip that only holds one folder: use that folder as the root bank
    BOOL bChanged;
    do {
        bChanged = FALSE;
        int num = getZipNum();
        for (int i = 0; i < num; i++) {
            CBank* pRoot = getZipRootBank(i);
            int childNum = pRoot->getChildNum();
            if (pRoot->m_patchNum == 0 && childNum == 1) {
                CBank* pChild = pRoot->getChild(0);
                char* name = pRoot->m_name;
                strcpy(pChild->m_name, name);
                s1g->m_pZipRootBanks->at(i) = pChild;
                if (isExistFile(pRoot->m_readmePath, FALSE)) {
                    strcpy(pChild->m_readmePath, pRoot->m_readmePath);
                }
                pRoot->m_bNotOwnChildren = TRUE;
                bChanged = TRUE;
                delete pRoot;
            }
        }
    } while (bChanged);
}

BOOL CSoundLibrary::getProgramNameColor(int bankNo, char* zipPath, int progNo, char* name, unsigned int* pColor, int bCheckCache)
{
    CBank* pBank = getBank(bankNo, zipPath);
    if (pBank == NULL) {
        if (name) {
            strcpy(name, "not found");
        }
        return FALSE;
    }
    if (bankNo != -1 && bCheckCache) {
        pBank->checkCache();
    }
    if (name) {
        strcpy(name, pBank->m_patch[progNo].getName());
    }
    if (pColor) {
        *pColor = pBank->m_patch[progNo].getColor();
    }
    return TRUE;
}

void CSoundLibrary::setReqProgNo(int progNo)
{
    m_reqProgNo = progNo;
}

void CSoundLibrary::setReqBankNo(int bankNo, char* zipPath)
{
    m_reqBankNo = bankNo;
    if (zipPath) {
        strcpy(m_reqZipPath, zipPath);
    } else {
        m_reqZipPath[0] = 0;
    }
}

BOOL CSoundLibrary::isReqPatchLoaded()
{
    int prog = m_reqProgNo;
    int bank = m_reqBankNo;
    if (prog < 0) prog = 0;
    if (prog > BANK_PATCH_NUM - 1) prog = BANK_PATCH_NUM - 1;
    m_reqProgNo = prog;
    if (bank == -1) {
        return prog == m_sound.m_progNo && m_sound.m_bankNo == -1 && strcmp(m_reqZipPath, m_sound.m_path) == 0;
    }
    if (bank < 0) bank = 0;
    if (bank > BANK_FOLDER_NUM - 1) bank = BANK_FOLDER_NUM - 1;
    m_reqBankNo = bank;
    return prog == m_sound.m_progNo && bank == m_sound.m_bankNo;
}

// load the requested patch (pInitSound gives the defaults)
CSoundData* CSoundLibrary::loadProgram(CSoundData* pInitSound)
{
    char path[256];
    char line[256];

    m_sound.init();
    memcpy(m_sound.m_name, pInitSound->m_name, SOUNDDATA_BODY_SIZE);
    m_sound.m_bankNo = m_reqBankNo;
    strcpy(m_sound.m_path, m_reqZipPath);
    m_sound.m_progNo = m_reqProgNo;
    CBank* pBank = getBank(m_reqBankNo, m_reqZipPath);
    if (pBank && pBank->getProgPath(m_reqProgNo, path)) {
        void* fp = openFile(path);
        if (fp) {
            if (readLine(fp, m_sound.m_name, 32)) {
                m_sound.m_version = 100;
                while (readLine(fp, line, 255)) {
                    char* p;
                    for (p = line; *p; p++) {
                        if (*p == ',') break;
                    }
                    if (*p == ',') {
                        *p = 0;
                        int id = atoi(line);
                        int val = atoi(p + 1);
                        m_sound.setParamVal(id, val);
                    } else if (strncmp(line, "color=", 6) == 0) {
                        m_sound.m_color = getColorFromString(line + 6);
                    } else if (strncmp(line, "ver=", 4) == 0) {
                        m_sound.m_version = atoi(line + 4);
                    }
                }
            }
            closeFile(fp);
        }
    }
    return &m_sound;
}

// (the declaration points of fp, the arrays and dir are significant for reproducing the
// released binary)
int CSoundLibrary::saveProgram(WriteProgramInfo* pInfo)
{
    CMyCSLock lock(&s1g->m_csZip);
    FILE* fp;
    if (pInfo->bankNo == -1) {
        return -1;
    }
    char path[256];
    char line[256];
    char color[256];
    if (!s1g->m_banks[pInfo->bankNo].getProgPath(pInfo->progNo, path)) {
        return 0;
    }
    char dir[1024];
    strcpy(dir, path);
    cutFileName(dir);
    myMkDir(dir);
    fp = fopen(path, "wt");
    if (fp == NULL) {
        return -1;
    }
    if (fputs(pInfo->name, fp) != EOF) {
        if (fputs("\n", fp) != EOF) {
            getColorStringFromVal(color, pInfo->color);
            sprintf(line, "color=%s\n", color);
            if (fputs(line, fp) != EOF) {
                sprintf(line, "ver=%d\n", m_sound.m_version);
                if (fputs(line, fp) != EOF) {
                    for (int i = 0; i < m_sound.m_paramNum; i++) {
                        sprintf(line, "%d,%d\n", m_sound.m_paramID[i], m_sound.m_paramVal[i]);
                        if (fputs(line, fp) == EOF) goto error;
                    }
                    fclose(fp);
                    m_sound.m_bankNo = pInfo->bankNo;
                    m_sound.m_progNo = pInfo->progNo;
                    strcpy(m_sound.m_name, pInfo->name);
                    m_sound.m_color = pInfo->color;
                    s1g->m_banks[pInfo->bankNo].m_patch[pInfo->progNo].invalidateInfo();
                    return 1;
                }
            }
        }
    }
error:
    fclose(fp);
    return -1;
}
