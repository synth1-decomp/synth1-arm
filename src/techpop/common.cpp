/**
 * @file common.cpp
 * @brief Small utilities: settings (ini) access, colors, path handling.
 *
 * Also defines the global random table g_rnd and the named color table.
 */

#include <Carbon/Carbon.h>
#include <stdio.h>
#include <string.h>
#include "common.h"

CRandomTable g_rnd;

DefineColor g_defineColors[] = {
    {"red",     RGB(255,0,0)},
    {"green",   RGB(0,255,0)},
    {"blue",    RGB(0,0,255)},
    {"yellow",  RGB(255,255,0)},
    {"magenta", RGB(255,0,255)},
    {"cyan",    RGB(0,255,255)},
};
#define DEFINE_COLORS_NUM   (sizeof(g_defineColors)/sizeof(DefineColor))

// Get the path of a user folder (FSFindFolder folder type)
void GetFolderPath(unsigned int folderType, char* path, int size)
{
    FSRef ref;
    if(FSFindFolder(kUserDomain, folderType, kDontCreateFolder, &ref) == noErr){
        CFURLRef url = CFURLCreateFromFSRef(NULL, &ref);
        CFStringRef str = CFURLCopyFileSystemPath(url, kCFURLPOSIXPathStyle);
        if(str){
            CFStringGetCString(str, path, size, kCFStringEncodingUTF8);
            CFRelease(str);
        }
        CFRelease(url);
    }
}

// settings file (preferences domain on Mac)
/**
 * @brief Returns the "ini file" name passed to the profile functions.
 *
 * On the Mac this is the preferences domain "jp.daichi.synth1", set on the first call.
 * @return Pointer to a static buffer holding the name.
 */
static char* getIniPath()
{
    static char g_iniPath[MAX_PATH];
    static bool bFirst;
    if(!bFirst){
        bFirst = true;
        strcpy(g_iniPath, "jp.daichi.synth1");
    }
    return g_iniPath;
}

DWORD getIniString(const char* key, char* ret, int size, const char* def)
{
    return GetPrivateProfileString("global", key, def, ret, size, getIniPath());
}

int getIniInt(const char* key, int def)
{
    return GetPrivateProfileInt("global", key, def, getIniPath());
}

BOOL setIniString(const char* key, const char* val)
{
    return WritePrivateProfileString("global", key, val, getIniPath());
}

void setIniInt(const char* key, int val)
{
    char szWork[16];
    sprintf(szWork, "%d", val);
    WritePrivateProfileString("global", key, szWork, getIniPath());
}

COLORREF getColorFromString(char* str)
{
    for(int i=0;i<DEFINE_COLORS_NUM;i++){
        if(strcmp(g_defineColors[i].name, str) == 0) return g_defineColors[i].color;
    }
    return 0xffffffff;
}

void getColorStringFromVal(char* str, COLORREF color)
{
    if(color != 0xffffffff){
        int i;
        for(i=0;i<DEFINE_COLORS_NUM;i++){
            if(g_defineColors[i].color == color){
                strcpy(str, g_defineColors[i].name);
                return;
            }
        }
        if(i != DEFINE_COLORS_NUM) return;
    }
    strcpy(str, "default");
}

char* getLastChar(char* str)
{
    return CharPrev(str, str + strlen(str));
}

// dst = dir + "/" + name
void connectPath(char* dst, char* dir, char* name)
{
    CFStringRef dirStr = CFStringCreateWithCString(kCFAllocatorDefault, dir, kCFStringEncodingUTF8);
    if(dirStr){
        CFURLRef dirUrl = CFURLCreateWithFileSystemPath(NULL, dirStr, kCFURLPOSIXPathStyle, true);
        if(dirUrl){
            CFStringRef nameStr = CFStringCreateWithCString(kCFAllocatorDefault, name, kCFStringEncodingUTF8);
            if(nameStr){
                CFURLRef url = CFURLCreateCopyAppendingPathComponent(kCFAllocatorDefault, dirUrl, nameStr, false);
                if(url){
                    CFStringRef path = CFURLCopyFileSystemPath(url, kCFURLPOSIXPathStyle);
                    if(path){
                        CFStringGetCString(path, dst, MAX_PATH, kCFStringEncodingUTF8);
                        CFRelease(path);
                    }
                    CFRelease(url);
                }
                CFRelease(nameStr);
            }
            CFRelease(dirUrl);
        }
        CFRelease(dirStr);
    }
}

// path = path + "/" + name
void addPath(char* path, const char* name)
{
    CFStringRef dirStr = CFStringCreateWithCString(kCFAllocatorDefault, path, kCFStringEncodingUTF8);
    if(dirStr){
        CFURLRef dirUrl = CFURLCreateWithFileSystemPath(NULL, dirStr, kCFURLPOSIXPathStyle, true);
        if(dirUrl){
            CFStringRef nameStr = CFStringCreateWithCString(kCFAllocatorDefault, name, kCFStringEncodingUTF8);
            if(nameStr){
                CFURLRef url = CFURLCreateCopyAppendingPathComponent(kCFAllocatorDefault, dirUrl, nameStr, false);
                if(url){
                    CFStringRef newPath = CFURLCopyFileSystemPath(url, kCFURLPOSIXPathStyle);
                    if(newPath){
                        CFStringGetCString(newPath, path, MAX_PATH, kCFStringEncodingUTF8);
                        CFRelease(newPath);
                    }
                    CFRelease(url);
                }
                CFRelease(nameStr);
            }
            CFRelease(dirUrl);
        }
        CFRelease(dirStr);
    }
}

// extract the file name part of a path
void getFileName(char* dst, char* path, int size)
{
    char* p = path;
    char* top = p;
    while(*p){
        if(*p == '\\' || *p == '/' || *p == ':'){
            if(*p != ':' && *(p+1) == 0) break;
            top = p+1;
        }
        p = CharNext(p);
    }
    int len = MIN(size-1, p - top);
    for(int i=0;i<len;i++) dst[i] = top[i];
    dst[len] = 0;
}

// cut the file name part of a path (leaves the trailing separator)
void cutFileName(char* path)
{
    char* top = path;
    while(*path){
        if(*path == '\\' || *path == '/' || *path == ':'){
            top = path+1;
        }
        path = CharNext(path);
    }
    *top = 0;
}

BOOL isExistFile(const char* path, BOOL bDir)
{
    DWORD attr = GetFileAttributes(path);
    if(attr != 0xffffffff){
        if(bDir && (attr & FILE_ATTRIBUTE_DIRECTORY)) return TRUE;
        if(!bDir && !(attr & FILE_ATTRIBUTE_DIRECTORY)) return TRUE;
    }
    return FALSE;
}

// make a directory, creating the intermediate directories too
BOOL myMkDir(char* path)
{
    int len = strlen(path);
    if(len >= MAX_PATH) return FALSE;
    if(isExistFile(path, TRUE)) return TRUE;

    char szWork[MAX_PATH];
    strcpy(szWork, path);
    for(int i=0;i<len;i++){
        szWork[i] = path[i];
        if(path[i] == '/' || path[i] == '\\' || i == len-1){
            szWork[i+1] = 0;
            DWORD attr = GetFileAttributes(szWork);
            if(attr != 0xffffffff){
                if(attr & FILE_ATTRIBUTE_DIRECTORY) continue;
                return FALSE;
            }
            if(mkdir(szWork) != 0) return FALSE;
        }
    }
    return TRUE;
}

// does str end with "end" ? (case insensitive)
BOOL endWith(char* str, char* end)
{
    int len = strlen(str);
    int endLen = strlen(end);
    if(len < endLen) return FALSE;
    char szWork[256];
    char szEnd[256];
    strcpy(szWork, str + len - endLen);
    strcpy(szEnd, end);
    CharLower(szWork);
    CharLower(szEnd);
    return strcmp(szWork, szEnd) == 0;
}
