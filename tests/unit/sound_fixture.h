/**
 * @file sound_fixture.h
 * @brief Helpers for the sound library tests (test_*.cpp): temp folders, file I/O,
 *        zip fixtures and isolation of the shared CSoundLibrary state in s1g.
 */
#pragma once

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <dirent.h>
#include <ftw.h>
#include <sys/stat.h>
#include <limits.h>
#include <string>
#include <vector>

#include "test.h"
#include "engine.h"
#include "stubs.h"
#include "synth1.h"

namespace t1fake {
// from tests/harness/os_fakes.h (not included here to keep this header free of its dependencies)
void resetPrefs();
void resetHome();
}

// Functions of sound.cpp with external linkage that no header declares.
BOOL readLine(void* fp, char* buf, int size);
void* openFile(char* path);
void closeFile(void* fp);
BOOL unzip(char* zipPath, char* outDir);

/**
 * @brief CHECK_STREQ for std::string or char* operands; keeps temporaries alive during the check
 *        (CHECK_STREQ itself would keep only a dangling c_str() of a temporary).
 */
#define CHECK_STR(a, b)                                                             \
    do { std::string t1sa_ = (a); std::string t1sb_ = (b);                                \
         CHECK_STREQ(t1sa_.c_str(), t1sb_.c_str()); } while (0)

namespace soundtest {

/**
 * @brief nftw callback that removes one entry.
 */
static int rmEntry(const char* p, const struct stat* st, int flag, struct FTW* ftw)
{
    (void)st; (void)flag; (void)ftw;
    remove(p);
    return 0;
}

/**
 * @brief Removes a file or folder tree.
 *
 * @param p Path to remove.
 */
inline void removeTree(const std::string& p)
{
    nftw(p.c_str(), rmEntry, 16, FTW_DEPTH | FTW_PHYS);
}

/**
 * @brief A temporary folder under /tmp (mkdtemp), removed with its contents on destruction.
 */
class TempDir {
public:
    TempDir()
    {
        char t[] = "/tmp/synth1test.XXXXXX";
        char* r = mkdtemp(t);
        // use the resolved path (/private/tmp/...): myMkDir() cannot create folders below a
        // symlinked component such as /tmp
        char real[PATH_MAX];
        m_path = (r && realpath(r, real)) ? real : (r ? r : "");
    }
    ~TempDir()
    {
        if (!m_path.empty()) removeTree(m_path);
    }
    const std::string& path() const { return m_path; }
    std::string sub(const std::string& name) const { return m_path + "/" + name; }

private:
    std::string m_path;
};

/**
 * @brief A writable char buffer holding a copy of a string (the engine takes char*).
 */
struct Buf {
    char b[2048];
    explicit Buf(const std::string& s)
    {
        strncpy(b, s.c_str(), sizeof(b) - 1);
        b[sizeof(b) - 1] = 0;
    }
    operator char*() { return b; }
};

inline bool writeFile(const std::string& path, const std::string& data)
{
    FILE* fp = fopen(path.c_str(), "wb");
    if (!fp) return false;
    bool ok = fwrite(data.data(), 1, data.size(), fp) == data.size();
    fclose(fp);
    return ok;
}

inline std::string readFile(const std::string& path)
{
    std::string s;
    FILE* fp = fopen(path.c_str(), "rb");
    if (!fp) return "<missing>";
    char buf[4096];
    size_t n;
    while ((n = fread(buf, 1, sizeof(buf), fp)) > 0) s.append(buf, n);
    fclose(fp);
    return s;
}

inline bool makeDir(const std::string& path)
{
    return mkdir(path.c_str(), 0755) == 0;
}

inline bool fileExists(const std::string& path)
{
    struct stat st;
    return stat(path.c_str(), &st) == 0 && S_ISREG(st.st_mode);
}

inline bool dirExists(const std::string& path)
{
    struct stat st;
    return stat(path.c_str(), &st) == 0 && S_ISDIR(st.st_mode);
}

inline bool copyFile(const std::string& src, const std::string& dst)
{
    std::string s = readFile(src);
    if (s == "<missing>") return false;
    return writeFile(dst, s);
}

/**
 * @brief Finds a committed fixture of tests/data/sound (the runner's working folder is tests/data).
 *
 * @param name  File name.
 * @return Path of the fixture ("" if not found).
 */
inline std::string fixture(const char* name)
{
    static const char* dirs[] = {"sound/", "data/sound/", "tests/data/sound/"};
    for (int i = 0; i < 3; i++) {
        std::string p = std::string(dirs[i]) + name;
        if (fileExists(p)) return p;
    }
    return "";
}

/**
 * @brief Prefix of the zip copies made by this process ("synth1test_<pid>_"), so the folders that
 *        extractZipBank() makes in the user temp folder are unique and can be cleaned up.
 */
inline std::string zipPrefix()
{
    char b[64];
    sprintf(b, "synth1test_%d_", (int)getpid());
    return b;
}

/**
 * @brief The folder extractZipBank() extracts into: <GetTempPath()>/synth1/zip.
 */
inline std::string zipTempRoot()
{
    char buf[1024];
    GetTempPath(sizeof(buf), buf);
    return std::string(buf) + "/synth1/zip";
}

/**
 * @brief Removes the extraction folders of this process's zip copies from the user temp folder.
 */
inline void cleanExtracted()
{
    std::string root = zipTempRoot();
    std::string prefix = zipPrefix();
    DIR* d = opendir(root.c_str());
    if (!d) return;
    std::vector<std::string> victims;
    struct dirent* e;
    while ((e = readdir(d)) != NULL) {
        if (strncmp(e->d_name, prefix.c_str(), prefix.size()) == 0) {
            victims.push_back(root + "/" + e->d_name);
        }
    }
    closedir(d);
    for (unsigned int i = 0; i < victims.size(); i++) removeTree(victims[i]);
}

/**
 * @brief Copies a committed zip fixture into a folder under a process-unique name.
 *
 * @param fixtureName   Fixture file name (e.g. "flat.zip").
 * @param dir           Destination folder.
 * @param stem          Name part after the prefix (the copy is "<prefix><stem>").
 * @return The file name of the copy (also the inner path of its root bank), "" on failure.
 */
inline std::string putZip(const char* fixtureName, const std::string& dir, const std::string& stem)
{
    std::string src = fixture(fixtureName);
    if (src.empty()) return "";
    std::string name = zipPrefix() + stem;
    if (!copyFile(src, dir + "/" + name)) return "";
    return name;
}

/**
 * @brief Isolates the sound library state shared through s1g.
 *
 * Pretends one CSoundLibrary instance exists (m_soundLibRefCount = 1) so that constructing
 * further instances does not run the first-instance setup (which reads the user's preferences
 * and may create folders in ~/Documents). Installs fresh zip bank containers, an empty zip
 * folder and sandbox mode (so changeBankFolder()/updateZipFolderPath() do not write the
 * preferences). Everything is restored on destruction; extracted zip folders of this process
 * are removed from the user temp folder.
 */
class LibState {
public:
    enum { SAVED_BANKS = 8 };

    LibState()
    {
        t1::initEngine();
        t1stub::reset();
        t1stub::sandBox = 1;
        m_refCount = s1g->m_soundLibRefCount;
        m_hash = s1g->m_zipFilesHash;
        m_lock = s1g->m_zipBankLock;
        m_checkTime = s1g->m_zipCheckTime;
        m_zipFolder = s1g->m_zipFolder;
        m_roots = s1g->m_pZipRootBanks;
        m_map = s1g->m_pZipBankMap;
        for (int i = 0; i < SAVED_BANKS; i++) m_bankPath[i] = s1g->m_banks[i].m_path;
        s1g->m_soundLibRefCount = 1;
        s1g->m_zipFilesHash = 0;
        s1g->m_zipBankLock = 0;
        s1g->m_zipFolder[0] = 0;
        s1g->m_pZipRootBanks = new std::vector<CBank*>;
        s1g->m_pZipBankMap = new BankMap;
    }

    /**
     * @brief Deletes the zip root banks (and their children) like the last ~CSoundLibrary.
     */
    void freeZipBanks()
    {
        for (unsigned int i = 0; i < s1g->m_pZipRootBanks->size(); i++) {
            delete s1g->m_pZipRootBanks->at(i);
        }
        s1g->m_pZipRootBanks->clear();
        s1g->m_pZipBankMap->clear();
    }

    ~LibState()
    {
        freeZipBanks();
        delete s1g->m_pZipRootBanks;
        delete s1g->m_pZipBankMap;
        s1g->m_pZipRootBanks = m_roots;
        s1g->m_pZipBankMap = m_map;
        s1g->m_soundLibRefCount = m_refCount;
        s1g->m_zipFilesHash = m_hash;
        s1g->m_zipBankLock = m_lock;
        s1g->m_zipCheckTime = m_checkTime;
        strcpy(s1g->m_zipFolder, m_zipFolder.c_str());
        for (int i = 0; i < SAVED_BANKS; i++) {
            CBank* b = &s1g->m_banks[i];
            if (m_bankPath[i].empty()) {
                b->m_path[0] = 0;
                b->m_name[0] = 0;
                b->m_readmePath[0] = 0;
                b->checkCache();
            } else {
                Buf p(m_bankPath[i]);
                b->setPath(p);
            }
        }
        cleanExtracted();
        t1stub::reset();
    }

    /**
     * @brief Points a numbered bank at a folder (CBank::setPath()).
     */
    static void setBank(int bankNo, const std::string& path)
    {
        Buf p(path);
        s1g->m_banks[bankNo].setPath(p);
    }

    /**
     * @brief Sets the zip folder (directly, without rescanning).
     */
    static void setZipFolder(const std::string& path)
    {
        strcpy(s1g->m_zipFolder, path.c_str());
    }

private:
    int m_refCount;
    int m_hash;
    int m_lock;
    LONGLONG m_checkTime;
    std::string m_zipFolder;
    std::vector<CBank*>* m_roots;
    BankMap* m_map;
    std::string m_bankPath[SAVED_BANKS];
};

/**
 * @brief Isolated sound library for tests that construct CSynthesizer1 or CSoundLibrary.
 *
 * Combines LibState and TempDir: banks 0-7 point into the temp dir (only bank 0 exists, empty)
 * and the zip folder is <tmp>/zip, so the first-instance library setup never touches the
 * user's folders or preferences. Keep it alive while synths exist.
 */
struct IsolatedLibrary {
    LibState lib;
    TempDir tmp;

    /**
     * @brief Points the library at a fresh temp folder.
     */
    IsolatedLibrary()
    {
        for (int i = 0; i < LibState::SAVED_BANKS; i++) {
            char name[16];
            sprintf(name, "bank%02d", i);
            if (i == 0) makeDir(tmp.sub(name));
            LibState::setBank(i, tmp.sub(name));
        }
        makeDir(tmp.sub("zip"));
        LibState::setZipFolder(tmp.sub("zip"));
    }
};

/**
 * @brief Makes the next CSoundLibrary the first instance, so its setup runs (bank folders and
 *        zip folder from the preferences or the defaults below Documents/Synth1Patches).
 *
 * Saves the shared library state in s1g (reference count, all BANK_FOLDER_NUM bank paths, zip
 * folder, zip containers, hash, lock and check time), then sets the reference count to 0 and
 * the zip containers to NULL (the first instance allocates its own). Also clears the fake
 * preferences and the fake home folder (tests/harness/os_fakes.h) and selects sandbox mode 0
 * (call t1stub::sandBox = 1 to test sandbox mode).
 *
 * Create the libraries with newLibrary(): the destructor deletes them (the last one frees the
 * zip containers it allocated) and then restores everything, clears the fakes again and removes
 * extracted zip folders of this process.
 */
class FirstInstanceState {
public:
    FirstInstanceState()
    {
        t1::initEngine();
        t1stub::reset();
        t1fake::resetPrefs();
        t1fake::resetHome();
        m_refCount = s1g->m_soundLibRefCount;
        m_hash = s1g->m_zipFilesHash;
        m_lock = s1g->m_zipBankLock;
        m_checkTime = s1g->m_zipCheckTime;
        m_zipFolder = s1g->m_zipFolder;
        m_roots = s1g->m_pZipRootBanks;
        m_map = s1g->m_pZipBankMap;
        for (int i = 0; i < BANK_FOLDER_NUM; i++) {
            m_bankPath[i] = s1g->m_banks[i].m_path;
            m_bankNo[i] = s1g->m_banks[i].m_bankNo;
        }
        s1g->m_soundLibRefCount = 0;
        s1g->m_zipFilesHash = 0;
        s1g->m_zipBankLock = 0;
        s1g->m_pZipRootBanks = NULL;
        s1g->m_pZipBankMap = NULL;
    }

    /**
     * @brief Constructs a CSoundLibrary owned by this fixture (the first one runs the setup).
     */
    CSoundLibrary* newLibrary()
    {
        CSoundLibrary* lib = new CSoundLibrary;
        m_libs.push_back(lib);
        return lib;
    }

    /**
     * @brief Deletes the libraries made by newLibrary() (newest first), like the end of a
     *        process: the last one frees the zip containers, and the next newLibrary() is a
     *        first instance again.
     */
    void deleteLibraries()
    {
        for (int i = (int)m_libs.size() - 1; i >= 0; i--) delete m_libs[i];
        m_libs.clear();
    }

    ~FirstInstanceState()
    {
        deleteLibraries();
        if (s1g->m_soundLibRefCount != 0 && s1g->m_pZipRootBanks) {
            // the libraries were not created through newLibrary(): free what the setup allocated
            for (unsigned int i = 0; i < s1g->m_pZipRootBanks->size(); i++) {
                delete s1g->m_pZipRootBanks->at(i);
            }
            delete s1g->m_pZipRootBanks;
            delete s1g->m_pZipBankMap;
        }
        s1g->m_pZipRootBanks = m_roots;
        s1g->m_pZipBankMap = m_map;
        s1g->m_soundLibRefCount = m_refCount;
        s1g->m_zipFilesHash = m_hash;
        s1g->m_zipBankLock = m_lock;
        s1g->m_zipCheckTime = m_checkTime;
        strcpy(s1g->m_zipFolder, m_zipFolder.c_str());
        for (int i = 0; i < BANK_FOLDER_NUM; i++) {
            CBank* b = &s1g->m_banks[i];
            b->m_bankNo = m_bankNo[i];
            if (m_bankPath[i].empty()) {
                b->m_path[0] = 0;
                b->m_name[0] = 0;
                b->m_readmePath[0] = 0;
                b->checkCache();
            } else {
                Buf p(m_bankPath[i]);
                b->setPath(p);
            }
        }
        cleanExtracted();
        t1fake::resetPrefs();
        t1fake::resetHome();
        t1stub::reset();
    }

private:
    FirstInstanceState(const FirstInstanceState&);
    FirstInstanceState& operator=(const FirstInstanceState&);

    std::vector<CSoundLibrary*> m_libs;
    int m_refCount;
    int m_hash;
    int m_lock;
    LONGLONG m_checkTime;
    std::string m_zipFolder;
    std::vector<CBank*>* m_roots;
    BankMap* m_map;
    std::string m_bankPath[BANK_FOLDER_NUM];
    int m_bankNo[BANK_FOLDER_NUM];
};

}  // namespace soundtest
