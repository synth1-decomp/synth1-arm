#pragma once

/**
 * @file common.h
 * @brief Basic types, macros and small utilities shared by all techpop sources.
 *
 * Signal types, float helpers, a mutex wrapper, MSVC-compatible random numbers,
 * the named color table and the settings/path/color helpers of common.cpp.
 */

#include "../windef.h"

typedef long long LONGLONG;

// SSE alignment (16 bytes)
#define CACHE_ALIGN16   __attribute__((aligned(16)))

// signal value type: 32bit float
typedef float SIGREAL;

/**
 * @brief A stereo signal value (left/right SIGREAL pair).
 */
typedef struct tagSIG_LR{
    SIGREAL l;
    SIGREAL r;
} SIG_LR;

// generic macros
#ifndef MIN
#define MIN(a,b)    ((a)<(b)?(a):(b))
#endif
#ifndef MAX
#define MAX(a,b)    ((a)>(b)?(a):(b))
#endif
#define PI      3.141592653589793238462643383279f

#ifdef __cplusplus
#include <math.h>
#ifdef SYNTH1_MODERN_BUILD
#include "../compat/double_math.h"
#endif

#ifndef FTOI_DEFINED
#define FTOI_DEFINED

/**
 * @brief Converts a float to int using the current FPU rounding mode (x87 fistp).
 *
 * With the default rounding mode this rounds to nearest (ties to even), unlike a
 * C cast, which truncates.
 * @param f     Value to convert.
 * @return The rounded integer.
 */
inline int ftoi(float f)
{
#if defined(__aarch64__)
    // fistp stores INT_MIN for NaN and for values that don't fit; a NEON conversion would saturate
    float r = __builtin_rintf(f);
    return (r >= -2147483648.0f && r < 2147483648.0f) ? (int)r : (-2147483647 - 1);
#else
    int i;
    __asm__ __volatile__ ("fistpl %0" : "=m" (i) : "t" (f) : "st");
    return i;
#endif
}
#endif

#ifndef FPOW_DEFINED
#define FPOW_DEFINED

/**
 * @brief Computes a to the power b as exp(log(a) * b).
 *
 * Uses pow() directly when a is 0, and falls back to pow() when the exp/log
 * result is NaN (e.g. for a negative base).
 * @param a     Base.
 * @param b     Exponent.
 * @return a raised to the power b.
 */
inline double fpow(double a, double b)
{
    if (a == 0) return pow(a, b);
    double r = exp(log(a) * b);
    if (isnan(r)) r = pow(a, b);
    return r;
}
#endif

#include <pthread.h>

// ---------------------------------------------------------------------------
/**
 * @brief Critical section built on a pthread mutex.  S1G holds three of them.  sizeof == 0x48
 *
 * The mutex is created with default attributes (not recursive).
 * @note The destructor does not destroy the mutex.
 */
class CMyCS {
public:
    /**
     * @brief Initializes the mutex with default attributes.
     */
    CMyCS() : m_mutex() { pthread_mutex_init(&m_mutex, NULL); }
    virtual ~CMyCS(){}

    /**
     * @brief Locks the mutex, blocking until it is available.
     */
    void enter(){ pthread_mutex_lock(&m_mutex); }

    /**
     * @brief Unlocks the mutex.
     */
    void leave(){ pthread_mutex_unlock(&m_mutex); }

    pthread_mutex_t m_mutex;    // +0x08
};

/**
 * @brief Scoped lock of a CMyCS: enters it on construction and leaves it on destruction.
 */
class CMyCSLock {
public:
    /**
     * @brief Enters pCS and remembers it for the destructor.
     *
     * @param pCS   Critical section to lock; must outlive this object.
     */
    CMyCSLock(CMyCS* pCS){ m_pCS = pCS; m_pCS->enter(); }

    /**
     * @brief Leaves the critical section entered by the constructor.
     */
    ~CMyCSLock(){ m_pCS->leave(); }

    CMyCS* m_pCS;
};
#endif // __cplusplus

// ---- random numbers ----
#define MYRAND_MAX  0x7fff

/**
 * @brief Linear congruential generator, same sequence as the MSVC CRT rand().
 *
 * seed = seed * 214013 + 2531011; the result is bits 16..30 of the seed.
 */
class CMyRand {
public:
    /**
     * @brief Creates a generator with seed 1 (the CRT default).
     */
    CMyRand(){ m_seed = 1; }

    /**
     * @brief Sets the seed.
     *
     * @param seed  New seed; the next rand() continues from it.
     */
    void setSeed(unsigned int seed){ m_seed = seed; }

    /**
     * @brief Advances the generator.
     *
     * @return The next pseudo-random value, 0..MYRAND_MAX (0..32767).
     */
    int rand(){
        m_seed = m_seed * 214013 + 2531011;
        return (m_seed >> 16) & MYRAND_MAX;
    }
    unsigned int m_seed;
};

#define RND_TABLE_SIZE  512

/**
 * @brief Table of 512 precomputed random values in [0,1] plus generators.
 *
 * sizeof == 0x808.  The single instance is g_rnd (common.cpp).
 */
class CRandomTable {
public:
    /**
     * @brief Fills m_table from a fresh CMyRand (seed 1), each value rand() / 32767.0.
     */
    CRandomTable(){
        CMyRand rnd;
        for(int i=0;i<RND_TABLE_SIZE;i++){
            m_table[i] = (float)(rnd.rand() / 32767.0);
        }
    }
    float m_table[RND_TABLE_SIZE];  // +0x000
    CMyRand m_rand0;                // +0x800
    CMyRand m_rand;                 // +0x804
};
extern CRandomTable g_rnd;

// ---- named colors ----
/**
 * @brief One entry of the named color table (name as used in .sy1 "color=" lines, and its value).
 */
typedef struct tagDefineColor {
    const char* name;
    COLORREF color;
} DefineColor;
extern DefineColor g_defineColors[];

// ---- common.cpp ----
#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Reads a string setting from the "global" section of the settings store.
 *
 * The settings live in the "jp.daichi.synth1" preferences domain.
 * @param key       Setting name.
 * @param[out] ret  Receives the value (or def if the key does not exist).
 * @param size      Size of ret in bytes.
 * @param def       Default value.
 * @return Number of characters copied to ret (GetPrivateProfileString semantics).
 */
DWORD getIniString(const char* key, char* ret, int size, const char* def);

/**
 * @brief Reads an integer setting from the "global" section of the settings store.
 *
 * @param key   Setting name.
 * @param def   Default value.
 * @return The stored value, or def if the key does not exist.
 */
int getIniInt(const char* key, int def);

/**
 * @brief Writes a string setting to the "global" section of the settings store.
 *
 * @param key   Setting name.
 * @param val   Value to store.
 * @return TRUE on success.
 */
BOOL setIniString(const char* key, const char* val);

/**
 * @brief Writes an integer setting (stored as a decimal string) to the "global" section.
 *
 * @param key   Setting name.
 * @param val   Value to store.
 */
void setIniInt(const char* key, int val);

/**
 * @brief Checks whether a file or directory exists.
 *
 * @param path  Path to test.
 * @param bDir  TRUE to test for a directory, FALSE to test for a regular file.
 * @return TRUE if path exists and is of the requested kind.
 */
BOOL isExistFile(const char* path, BOOL bDir);
#ifdef __cplusplus
}
#endif

/**
 * @brief Gets the POSIX path of a folder in the user domain.
 *
 * The folder is not created. path is left unchanged if the folder cannot be found.
 * @param folderType    FSFindFolder folder type (e.g. kDocumentsFolderType).
 * @param[out] path     Receives the UTF-8 path.
 * @param size          Size of path in bytes.
 */
void GetFolderPath(unsigned int folderType, char* path, int size);

/**
 * @brief Converts a color name ("red", "green", ...) to its value via g_defineColors.
 *
 * @param str   Color name (exact, case-sensitive match).
 * @return The color, or 0xffffffff (default color) if the name is unknown.
 */
COLORREF getColorFromString(char* str);

/**
 * @brief Converts a color value to its name in g_defineColors.
 *
 * @param[out] str  Receives the name, or "default" if the color is 0xffffffff or not in the table.
 * @param color     Color value.
 */
void getColorStringFromVal(char* str, COLORREF color);

/**
 * @brief Returns a pointer to the last character of a string (multibyte aware, via CharPrev).
 *
 * @param str   String to examine.
 * @return Pointer to the last character, or str itself if it is empty.
 */
char* getLastChar(char* str);

/**
 * @brief Joins a directory and a name into a path (dst = dir + "/" + name).
 *
 * Uses CFURL path handling, so a separator is added only when needed.
 * dst is left unchanged if a conversion fails.
 * @param[out] dst  Receives the joined path (at least MAX_PATH bytes).
 * @param dir       Directory path.
 * @param name      File or folder name to append.
 */
void connectPath(char* dst, char* dir, char* name);

/**
 * @brief Appends a path component in place (path = path + "/" + name).
 *
 * @param[in,out] path  Path to extend (at least MAX_PATH bytes).
 * @param name          Component to append.
 */
void addPath(char* path, const char* name);

/**
 * @brief Extracts the last component of a path.
 *
 * '/', '\\' and ':' are separators. A trailing '/' or '\\' is ignored, so
 * "a/b/" gives "b".
 * @param[out] dst  Receives the name (truncated to size - 1 characters).
 * @param path      Path to examine.
 * @param size      Size of dst in bytes.
 */
void getFileName(char* dst, char* path, int size);

/**
 * @brief Removes the last component of a path, keeping the trailing separator.
 *
 * @param[in,out] path  Path to shorten; becomes "" if it has no separator.
 */
void cutFileName(char* path);

/**
 * @brief Creates a directory, including any missing parent directories.
 *
 * @param path  Directory to create (shorter than MAX_PATH).
 * @return TRUE if the directory exists afterwards; FALSE if path is too long,
 *         a component exists as a file, or mkdir fails.
 */
BOOL myMkDir(char* path);

/**
 * @brief Tests whether a string ends with a suffix, ignoring case.
 *
 * @param str   String to test (the compared tail must be shorter than 256 bytes).
 * @param end   Suffix (shorter than 256 bytes).
 * @return TRUE if str ends with end.
 */
BOOL endWith(char* str, char* end);
