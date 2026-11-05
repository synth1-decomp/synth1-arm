/**
 * @file winemu_au.cpp
 * @brief Win32 API emulation for the Audio Unit build.
 *
 * Plain C++ replacements for the Win32 calls used by the techpop sources:
 * aligned allocation, tick count, private profile strings (stored in
 * CFPreferences), heaps (CFAllocator), file enumeration (File Manager
 * iterators), a few GDI drawing calls on CGContext, bitmap loading from the
 * view bundle, and the Post/SendMessage wrappers around winobj_au.m. The
 * functions are declared in windef.h and documented there.
 */

#include <iostream>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <unistd.h>
#include <mach/mach_time.h>
#include <Carbon/Carbon.h>
#include "windef.h"

BOOL g_bCaptureWnd;
HWND g_hCaptureWnd;

void* _aligned_malloc(size_t size, size_t alignment)
{
    char* p = (char*)malloc(size + alignment + sizeof(void*));
    if(p == NULL) return NULL;
    size_t a = alignment + (size_t)p + sizeof(void*);
    char* ap = (char*)(a - a % alignment);
    ((void**)ap)[-1] = p;
    return ap;
}

void _aligned_free(void* p)
{
    free(((void**)p)[-1]);
}

unsigned long GetTickCount()
{
    uint64_t t = mach_absolute_time();
    Nanoseconds ns = AbsoluteToNanoseconds(*(AbsoluteTime*)&t);
    return *(uint64_t*)&ns / 1000000;
}

DWORD GetPrivateProfileString(LPCSTR lpAppName, LPCSTR lpKeyName, LPCSTR lpDefault, LPSTR lpReturnedString, DWORD nSize, LPCSTR lpFileName)
{
    strncpy(lpReturnedString, lpDefault, nSize);

    CFStringRef appID;
    if(lpFileName){
        appID = CFStringCreateWithCString(kCFAllocatorDefault, lpFileName, kCFStringEncodingUTF8);
    }else{
        appID = kCFPreferencesCurrentApplication;
    }
    if(appID){
        CFStringRef section = CFStringCreateWithCString(kCFAllocatorDefault, lpAppName, kCFStringEncodingUTF8);
        if(section){
            CFDictionaryRef dict = (CFDictionaryRef)CFPreferencesCopyAppValue(section, appID);
            if(dict){
                if(lpKeyName){
                    CFStringRef key = CFStringCreateWithCString(kCFAllocatorDefault, lpKeyName, kCFStringEncodingUTF8);
                    if(key){
                        CFStringRef val = (CFStringRef)CFDictionaryGetValue(dict, key);
                        if(val){
                            CFStringGetCString(val, lpReturnedString, nSize, kCFStringEncodingUTF8);
                        }
                        CFRelease(key);
                    }
                }else{
                    // enumerate all keys of the section ("key1\0key2\0\0")
                    CFIndex count = CFDictionaryGetCount(dict);
                    const void** keys = (const void**)malloc(count * sizeof(void*));
                    CFDictionaryGetKeysAndValues(dict, keys, NULL);
                    char* p = lpReturnedString;
                    for(CFIndex i=0;i<count;i++){
                        CFStringGetCString((CFStringRef)keys[i], p, lpReturnedString + nSize - p, kCFStringEncodingUTF8);
                        p += strlen(p) + 1;
                    }
                    free(keys);
                    *p = 0;
                }
                CFRelease(dict);
            }
            CFRelease(section);
        }
        if(kCFPreferencesCurrentApplication != appID){
            CFRelease(appID);
        }
    }
    return strlen(lpReturnedString);
}

BOOL WritePrivateProfileString(LPCSTR lpAppName, LPCSTR lpKeyName, LPCSTR lpString, LPCSTR lpFileName)
{
    CFStringRef appID;
    if(lpFileName){
        appID = CFStringCreateWithCString(kCFAllocatorDefault, lpFileName, kCFStringEncodingUTF8);
    }else{
        appID = kCFPreferencesCurrentApplication;
    }
    if(appID == NULL) return TRUE;

    CFStringRef section = CFStringCreateWithCString(kCFAllocatorDefault, lpAppName, kCFStringEncodingUTF8);
    if(lpKeyName == NULL && lpString == NULL){
        // delete the section
        if(section){
            CFPreferencesSetAppValue(section, NULL, appID);
            CFRelease(section);
        }
    }else if(section){
        CFDictionaryRef dict = (CFDictionaryRef)CFPreferencesCopyAppValue(section, appID);
        if(dict){
            CFStringRef key = CFStringCreateWithCString(kCFAllocatorDefault, lpKeyName, kCFStringEncodingUTF8);
            if(key){
                CFStringRef val;
                if(lpString && (val = CFStringCreateWithCString(kCFAllocatorDefault, lpString, kCFStringEncodingUTF8)) != NULL){
                    CFMutableDictionaryRef mdict = CFDictionaryCreateMutableCopy(kCFAllocatorDefault, 0, dict);
                    if(CFDictionaryGetValue(mdict, key)){
                        CFDictionaryReplaceValue(mdict, key, val);
                    }else{
                        CFDictionaryAddValue(mdict, key, val);
                    }
                    CFPreferencesSetAppValue(section, mdict, appID);
                    CFRelease(mdict);
                    CFRelease(val);
                }
                CFRelease(key);
            }
        }else{
            CFMutableDictionaryRef mdict = CFDictionaryCreateMutable(kCFAllocatorDefault, 0, NULL, NULL);
            dict = mdict;
            CFStringRef key = CFStringCreateWithCString(kCFAllocatorDefault, lpKeyName, kCFStringEncodingUTF8);
            if(key){
                CFStringRef val;
                if(lpString && (val = CFStringCreateWithCString(kCFAllocatorDefault, lpString, kCFStringEncodingUTF8)) != NULL){
                    CFDictionaryAddValue(mdict, key, val);
                    CFPreferencesSetAppValue(section, mdict, appID);
                    CFRelease(val);
                }
                CFRelease(key);
            }
        }
        CFRelease(dict);
        CFRelease(section);
    }
    CFPreferencesAppSynchronize(appID);
    if(kCFPreferencesCurrentApplication != appID){
        CFRelease(appID);
    }
    return TRUE;
}

UINT GetPrivateProfileInt(LPCSTR lpAppName, LPCSTR lpKeyName, int nDefault, LPCSTR lpFileName)
{
    char buf[256];
    CFStringRef appID;
    if(lpFileName){
        appID = CFStringCreateWithCString(kCFAllocatorDefault, lpFileName, kCFStringEncodingUTF8);
    }else{
        appID = kCFPreferencesCurrentApplication;
    }
    if(appID){
        CFStringRef section = CFStringCreateWithCString(kCFAllocatorDefault, lpAppName, kCFStringEncodingUTF8);
        if(section){
            CFDictionaryRef dict = (CFDictionaryRef)CFPreferencesCopyAppValue(section, appID);
            if(dict){
                CFStringRef key = CFStringCreateWithCString(kCFAllocatorDefault, lpKeyName, kCFStringEncodingUTF8);
                if(key){
                    CFStringRef val = (CFStringRef)CFDictionaryGetValue(dict, key);
                    if(val){
                        CFStringGetCString(val, buf, sizeof(buf), kCFStringEncodingUTF8);
                        nDefault = atoi(buf);
                    }
                    CFRelease(key);
                }
                CFRelease(dict);
            }
            CFRelease(section);
        }
        if(kCFPreferencesCurrentApplication != appID){
            CFRelease(appID);
        }
    }
    return nDefault;
}

HANDLE HeapCreate(DWORD flOptions, int dwInitialSize, int dwMaximumSize)
{
    CFAllocatorContext context = {0};
    return (HANDLE)CFAllocatorCreate(kCFAllocatorDefault, &context);
}

BOOL HeapDestroy(HANDLE hHeap)
{
    if(hHeap){
        CFRelease((CFAllocatorRef)hHeap);
    }
    return TRUE;
}

LPVOID HeapAlloc(HANDLE hHeap, DWORD dwFlags, int dwBytes)
{
    void* p = CFAllocatorAllocate((CFAllocatorRef)hHeap, dwBytes, 0);
    if((dwFlags & HEAP_ZERO_MEMORY) && p){
        memset(p, 0, dwBytes);
    }
    return p;
}

BOOL HeapFree(HANDLE hHeap, DWORD dwFlags, LPVOID lpMem)
{
    CFAllocatorDeallocate((CFAllocatorRef)hHeap, lpMem);
    return TRUE;
}

DWORD GetTempPath(int nBufferLength, LPSTR lpBuffer)
{
    FSRef ref;
    *lpBuffer = 0;
    if(FSFindFolder(kUserDomain, kTemporaryFolderType, kCreateFolder, &ref) == noErr){
        CFURLRef url = CFURLCreateFromFSRef(NULL, &ref);
        if(url){
            CFStringRef path = CFURLCopyFileSystemPath(url, kCFURLPOSIXPathStyle);
            if(path){
                CFStringGetCString(path, lpBuffer, nBufferLength, kCFStringEncodingUTF8);
                CFRelease(path);
            }
            CFRelease(url);
        }
    }
    return 0;
}

LPSTR CharPrev(LPCSTR lpszStart, LPCSTR lpszCurrent)
{
    lpszCurrent--;
    if(lpszCurrent < lpszStart) lpszCurrent = lpszStart;
    return (LPSTR)lpszCurrent;
}

LPSTR CharNext(LPCSTR lpsz)
{
    return (LPSTR)(lpsz + 1);
}

LPSTR CharLower(LPSTR lpsz)
{
    char* p = lpsz;
    while(*p){
        tolower(*p++);
    }
    return lpsz;
}

void Sleep(int dwMilliseconds)
{
    MPEventID event;
    MPEventFlags flags;
    MPCreateEvent(&event);
    MPWaitForEvent(event, &flags, dwMilliseconds);
    MPDeleteEvent(event);
}

BOOL BitBlt(HDC hdcDest, int nXDest, int nYDest, int nWidth, int nHeight, HDC hdcSrc, int nXSrc, int nYSrc, DWORD dwRop)
{
    if(hdcDest == NULL || hdcSrc == NULL) return FALSE;
    CGImageRef image = CGBitmapContextCreateImage(hdcSrc);
    if(image == NULL) return FALSE;
    if(nXSrc == 0 && nYSrc == 0 && CGImageGetWidth(image) == nWidth && CGImageGetHeight(image) == nHeight){
        CGContextDrawImage(hdcDest, CGRectMake(nXDest, nYDest, nWidth, nHeight), image);
    }else{
        CGImageRef sub = CGImageCreateWithImageInRect(image, CGRectMake(nXSrc, nYSrc, nWidth, nHeight));
        if(sub){
            CGContextDrawImage(hdcDest, CGRectMake(nXDest, nYDest, nWidth, nHeight), sub);
            CGImageRelease(sub);
        }
    }
    CGImageRelease(image);
    return FALSE;
}

BOOL StretchBlt(HDC hdcDest, int nXOriginDest, int nYOriginDest, int nWidthDest, int nHeightDest, HDC hdcSrc, int nXOriginSrc, int nYOriginSrc, int nWidthSrc, int nHeightSrc, DWORD dwRop)
{
    return FALSE;
}

HDC CreateCompatibleDC(HDC hdc)
{
    return NULL;
}

void DeleteDC(HDC hdc)
{
}

BOOL PtInRect0(RECT* lprc, POINT pt)
{
    if(lprc->left <= pt.x && pt.x <= lprc->right && lprc->top <= pt.y && pt.y <= lprc->bottom) return TRUE;
    return FALSE;
}

BOOL PostMessageControl(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    PostEventMac(hWnd, msg, wParam, lParam);
    return TRUE;
}

BOOL PostMessage(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    PostEventMac(hWnd, msg, wParam, lParam);
    return TRUE;
}

BOOL SendMessageControl(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    SendEvent(hWnd, msg, wParam, lParam);
    return TRUE;
}

BOOL SendMessage(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    SendEvent(hWnd, msg, wParam, lParam);
    return TRUE;
}

void Rectangle(HDC hdc, int left, int top, int right, int bottom)
{
    CGContextAddRect(hdc, CGRectMake(left, top, right, bottom));
}

void SetPixel(HDC hdc, int x, int y, COLORREF crColor)
{
}

COLORREF GetPixel0(HDC hdc, int x, int y)
{
    return 0;
}

void MoveTo0(HDC hdc, int x, int y)
{
    CGContextMoveToPoint(hdc, x, y);
}

void LineTo0(HDC hdc, int x, int y)
{
    CGContextAddLineToPoint(hdc, x, y);
}

void TextOut(HDC hdc, int x, int y, LPCSTR lpString, int c)
{
    CGContextSaveGState(hdc);
    CGContextSetTextDrawingMode(hdc, kCGTextFill);
    CGContextShowTextAtPoint(hdc, x, y, lpString, c);
    CGContextRestoreGState(hdc);
}

void SetTextColor(HDC hdc, COLORREF crColor)
{
    CGContextSetRGBFillColor(hdc, GetRValue(crColor) / 255.0, GetGValue(crColor) / 255.0, GetBValue(crColor) / 255.0, 1.0);
}

void DrawText0(HDC hdc, LPCSTR lpchText, int cchText, RECT* lprc, int format)
{
}

DWORD GetFileAttributes(LPCSTR lpFileName)
{
    CFStringRef path = CFStringCreateWithCString(kCFAllocatorDefault, lpFileName, kCFStringEncodingUTF8);
    DWORD attr = (DWORD)-1;
    if(path){
        CFURLRef url = CFURLCreateWithFileSystemPath(NULL, path, kCFURLPOSIXPathStyle, false);
        CFRelease(path);
        if(url){
            LSItemInfoRecord info;
            OSStatus err = LSCopyItemInfoForURL(url, kLSRequestAllInfo, &info);
            CFRelease(url);
            if(err == noErr){
                attr = ((info.flags & kLSItemInfoIsContainer) ? FILE_ATTRIBUTE_DIRECTORY : 0)
                     + ((info.flags & kLSItemInfoIsInvisible) ? FILE_ATTRIBUTE_HIDDEN : 0);
            }
        }
    }
    return attr;
}

HANDLE FindFirstFile(LPCSTR lpFileName, WIN32_FIND_DATA* lpFindFileData)
{
    memset(lpFindFileData, 0, sizeof(WIN32_FIND_DATA));
    FSIterator iterator = (FSIterator)INVALID_HANDLE_VALUE;
    CFStringRef path = CFStringCreateWithCString(kCFAllocatorDefault, lpFileName, kCFStringEncodingUTF8);
    if(path == NULL) return INVALID_HANDLE_VALUE;
    CFURLRef url = CFURLCreateWithFileSystemPath(kCFAllocatorDefault, path, kCFURLPOSIXPathStyle, false);
    CFRelease(path);
    if(url == NULL) return INVALID_HANDLE_VALUE;

    CFURLRef dirUrl = CFURLCreateCopyDeletingLastPathComponent(kCFAllocatorDefault, url);
    if(dirUrl){
        FSRef dirRef;
        if(CFURLGetFSRef(dirUrl, &dirRef)){
            if(FSOpenIterator(&dirRef, kFSIterateFlat, &iterator) == noErr){
                ItemCount count;
                FSCatalogInfo info = {0};
                FSRef ref;
                HFSUniStr255 name;
                if(FSGetCatalogInfoBulk(iterator, 1, &count, NULL, kFSCatInfoNodeFlags | kFSCatInfoContentMod | kFSCatInfoDataSizes, &info, &ref, NULL, &name) == noErr){
                    if(info.nodeFlags & kFSNodeIsDirectoryMask){
                        lpFindFileData->dwFileAttributes |= FILE_ATTRIBUTE_DIRECTORY;
                    }
                    lpFindFileData->nFileSizeLow = (DWORD)info.dataLogicalSize;
                    lpFindFileData->nFileSizeHigh = (DWORD)(info.dataLogicalSize >> 32);
                    CFStringRef fname = CFStringCreateWithCharacters(kCFAllocatorDefault, name.unicode, name.length);
                    if(fname){
                        CFStringGetCString(fname, lpFindFileData->cFileName, MAX_PATH + 1, kCFStringEncodingUTF8);
                        CFRelease(fname);
                    }
                }else{
                    FSCloseIterator(iterator);
                    iterator = (FSIterator)INVALID_HANDLE_VALUE;
                }
            }else{
                iterator = (FSIterator)INVALID_HANDLE_VALUE;
            }
        }
        CFRelease(dirUrl);
    }
    CFRelease(url);
    return (HANDLE)iterator;
}

BOOL FindNextFile(HANDLE hFindFile, WIN32_FIND_DATA* lpFindFileData)
{
    memset(lpFindFileData, 0, sizeof(WIN32_FIND_DATA));
    if(hFindFile){
        ItemCount count;
        FSCatalogInfo info = {0};
        FSRef ref;
        HFSUniStr255 name;
        if(FSGetCatalogInfoBulk((FSIterator)hFindFile, 1, &count, NULL, kFSCatInfoNodeFlags | kFSCatInfoContentMod | kFSCatInfoDataSizes, &info, &ref, NULL, &name) == noErr){
            if(info.nodeFlags & kFSNodeIsDirectoryMask){
                lpFindFileData->dwFileAttributes |= FILE_ATTRIBUTE_DIRECTORY;
            }
            lpFindFileData->nFileSizeLow = (DWORD)info.dataLogicalSize;
            lpFindFileData->nFileSizeHigh = (DWORD)(info.dataLogicalSize >> 32);
            CFStringRef fname = CFStringCreateWithCharacters(kCFAllocatorDefault, name.unicode, name.length);
            if(fname){
                CFStringGetCString(fname, lpFindFileData->cFileName, MAX_PATH + 1, kCFStringEncodingUTF8);
                CFRelease(fname);
            }
            return TRUE;
        }
    }
    return FALSE;
}

BOOL FindClose(HANDLE hFindFile)
{
    if(hFindFile){
        FSCloseIterator((FSIterator)hFindFile);
        return TRUE;
    }
    return FALSE;
}

OSErr mkdir(LPCSTR path)
{
    OSErr err;
    CFStringRef str = CFStringCreateWithCString(kCFAllocatorDefault, path, kCFStringEncodingUTF8);
    if(str == NULL){
        err = -1;
    }else{
        CFURLRef url = CFURLCreateWithFileSystemPath(kCFAllocatorDefault, str, kCFURLPOSIXPathStyle, false);
        if(url == NULL){
            err = -1;
        }else{
            CFURLRef parentUrl = CFURLCreateCopyDeletingLastPathComponent(NULL, url);
            if(parentUrl == NULL){
                err = -1;
            }else{
                FSRef parentRef;
                err = -1;
                if(CFURLGetFSRef(parentUrl, &parentRef)){
                    CFStringRef name = CFURLCopyLastPathComponent(url);
                    if(name){
                        int len = CFStringGetLength(name);
                        UniChar* uname = new UniChar[len];
                        CFStringGetCharacters(name, CFRangeMake(0, len), uname);
                        if(len == 1 && uname[0] == '/'){
                            err = -1;
                        }else{
                            err = FSCreateDirectoryUnicode(&parentRef, len, uname, kFSCatInfoNone, NULL, NULL, NULL, NULL);
                        }
                        delete[] uname;
                        CFRelease(name);
                    }
                }
                CFRelease(parentUrl);
            }
            CFRelease(url);
        }
        CFRelease(str);
    }
    return err;
}

void DebugOutput(float* buf, int n, char c)
{
    char fname[1024];
    char str[32];
    sprintf(fname, "/data_%c.txt", c);
    FILE* fp = fopen(fname, "ab");
    if(fp){
        for(int i=0;i<n;i++){
            sprintf(str, "%f\n", buf[i]);
            fwrite(str, 1, strlen(str), fp);
        }
        fclose(fp);
    }
}

void DebugOutput1(LPCSTR str, char c)
{
    FILE* fp = fopen("/data.txt", "ab");
    if(fp){
        fwrite(str, 1, strlen(str), fp);
        fclose(fp);
    }
}

void DebugOutput2(LPCSTR str)
{
    DebugOutput1(str, 0);
}

HBITMAP CreateDIB(int width, int height)
{
    int rowBytes = width * 4;
    size_t size = rowBytes * height;
    void* data = malloc(size);
    CGDataProviderRef provider = CGDataProviderCreateWithData(NULL, data, size, NULL);
    CGColorSpaceRef colorSpace = CGColorSpaceCreateDeviceRGB();
    CGImageRef image = CGImageCreate(width, height, 8, 32, rowBytes, colorSpace, kCGImageAlphaLast, provider, NULL, false, kCGRenderingIntentDefault);
    CGColorSpaceRelease(colorSpace);
    CGDataProviderRelease(provider);
    free(data);
    return image;
}

int GetObject(HBITMAP hbm, int cbBuffer, LPVOID lpvObject)
{
    if(cbBuffer != sizeof(BITMAP)) return 0;
    BITMAP* bm = (BITMAP*)lpvObject;
    bm->bmWidth = CGImageGetWidth(hbm);
    bm->bmHeight = CGImageGetHeight(hbm);
    bm->bmWidthBytes = CGImageGetBytesPerRow(hbm);
    bm->bmBitsPixel = CGImageGetBitsPerPixel(hbm);
    bm->bmPlanes = (short)bm->bmBitsPixel / CGImageGetBitsPerComponent(hbm);
    bm->bmBits = NULL;
    return sizeof(BITMAP);
}

/**
 * @brief Maps the Win32 bitmap resource IDs used by the GUI to PNG files in the view bundle.
 *
 * @see LoadBitmap()
 */
static const struct {
    int id;
    const char* name;
} resource_table[] = {
    { 0x82, "SQUARE_LEDSW_RED.png" },
    { 0x83, "SQUARE_LEDSW_OFF.png" },
    { 0x84, "SQUARE_PUSHSW_UP.png" },
    { 0x85, "SQUARE_PUSHSW_DOWN.png" },
    { 0x86, "LED_RED_OFF.png" },
    { 0x87, "LED_RED_ON.png" },
    { 0x88, "LED_GREEN_OFF.png" },
    { 0x89, "LED_GREEN_ON.png" },
    { 0x8a, "LED_BLUE_OFF.png" },
    { 0x8b, "LED_BLUE_ON.png" },
    { 0x93, "W_sine.png" },
    { 0x95, "W_triangle.png" },
    { 0x96, "W_pulse.png" },
    { 0x97, "W_downsaw.png" },
    { 0x98, "W_random.png" },
    { 0x99, "W_randomsmooth.png" },
    { 0xaa, "led_red.png" },
    { 0xab, "led_green.png" },
};

HBITMAP LoadBitmap(HINSTANCE hInstance, int id)
{
    const char* name = "";
    for(int i=0;i<sizeof(resource_table)/sizeof(resource_table[0]);i++){
        if(resource_table[i].id == id){
            name = resource_table[i].name;
            break;
        }
    }
    CFStringRef fname = CFStringCreateWithFormat(kCFAllocatorDefault, NULL, CFSTR("%s"), name);
    CFBundleRef bundle = CFBundleGetBundleWithIdentifier(CFSTR("jp.daichi.synth1.audiounit.ui"));
    CFURLRef url = CFBundleCopyResourceURL(bundle, fname, NULL, NULL);
    if(url == NULL){
        printf("image file not found:%s,%x\n", name, bundle);
        return NULL;
    }
    CGDataProviderRef provider = CGDataProviderCreateWithURL(url);
    CGImageRef image = CGImageCreateWithPNGDataProvider(provider, NULL, false, kCGRenderingIntentDefault);
    CGDataProviderRelease(provider);
    CFRelease(url);
    CFRelease(fname);
    return image;
}

#ifdef SYNTH1_VIEWFACTORY
HDC BeginPaint(HWND hWnd, void* lpPaint)
{
    return ((PAINTSTRUCT*)lpPaint)->hdc = GetDC(hWnd);
}

int EndPaint(HWND hWnd, void* lpPaint)
{
    ReleaseDC(hWnd, ((PAINTSTRUCT*)lpPaint)->hdc);
    return TRUE;
}
#endif

void SetWindow(void* p, HWND hWnd)
{
    *(HWND*)((char*)p + 0x108) = hWnd;
}

void DeleteFile(LPCSTR lpFileName)
{
    unlink(lpFileName);
}

void SetCursorPos(int x, int y)
{
    CGPoint pt = CGPointMake(x, y);
    CGDisplayMoveCursorToPoint(CGMainDisplayID(), pt);
}

SHORT GetKeyState(int nVirtKey)
{
    KeyMap keys;
    GetKeys(keys);
    // keys[1] holds key codes 32..63 (big endian bit order inside each byte group)
    switch(nVirtKey){
    case VK_PAUSE:      if(keys[1].bigEndianValue & 0x04000000) return (SHORT)0x8000; break;    // kVK_Option (58)
    case VK_SHIFT:      if(keys[1].bigEndianValue & 0x01000000) return (SHORT)0x8000; break;    // kVK_Shift (56)
    case VK_CONTROL:    if(keys[1].bigEndianValue & 0x08000000) return (SHORT)0x8000; break;    // kVK_Control (59)
    case VK_MENU:       if(keys[1].bigEndianValue & 0x00800000) return (SHORT)0x8000; break;    // kVK_Command (55)
    }
    return 0;
}
