/**
 * @file os_fakes.cpp
 * @brief In-memory preferences and a fake home folder for the engine tests (see os_fakes.h).
 */
#include "os_fakes.h"

#include <CoreServices/CoreServices.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

namespace t1fake {

int setCalls = 0;
int syncCalls = 0;

/**
 * @brief The preference store: domain -> (key -> value).
 *
 * @return The store (created on first use).
 */
static CFMutableDictionaryRef store()
{
    static CFMutableDictionaryRef s = 0;
    if (!s) {
        s = CFDictionaryCreateMutable(NULL, 0, &kCFTypeDictionaryKeyCallBacks,
                                      &kCFTypeDictionaryValueCallBacks);
    }
    return s;
}

/**
 * @brief Gets the key -> value dictionary of a preference domain.
 *
 * @param appID   Preference domain.
 * @param create  Create the dictionary if the domain has none yet.
 * @return The domain's dictionary, or NULL if it does not exist and create is false.
 */
static CFMutableDictionaryRef domain(CFStringRef appID, bool create)
{
    CFMutableDictionaryRef d = (CFMutableDictionaryRef)CFDictionaryGetValue(store(), appID);
    if (!d && create) {
        d = CFDictionaryCreateMutable(NULL, 0, &kCFTypeDictionaryKeyCallBacks,
                                      &kCFTypeDictionaryValueCallBacks);
        CFDictionarySetValue(store(), appID, d);
        CFRelease(d);
    }
    return d;
}

void resetPrefs()
{
    CFDictionaryRemoveAllValues(store());
    setCalls = 0;
    syncCalls = 0;
}

void setPrefString(const char* appID, const char* key, const char* value)
{
    CFStringRef a = CFStringCreateWithCString(NULL, appID, kCFStringEncodingUTF8);
    CFStringRef k = CFStringCreateWithCString(NULL, key, kCFStringEncodingUTF8);
    if (value) {
        CFStringRef v = CFStringCreateWithCString(NULL, value, kCFStringEncodingUTF8);
        CFDictionarySetValue(domain(a, true), k, v);
        CFRelease(v);
    } else if (CFMutableDictionaryRef d = domain(a, false)) {
        CFDictionaryRemoveValue(d, k);
    }
    CFRelease(k);
    CFRelease(a);
}

std::string getPrefString(const char* appID, const char* key)
{
    std::string result;
    CFStringRef a = CFStringCreateWithCString(NULL, appID, kCFStringEncodingUTF8);
    CFStringRef k = CFStringCreateWithCString(NULL, key, kCFStringEncodingUTF8);
    CFMutableDictionaryRef d = domain(a, false);
    CFTypeRef v = d ? CFDictionaryGetValue(d, k) : 0;
    if (v) {
        char buf[4096] = "";
        if (CFGetTypeID(v) == CFStringGetTypeID()) {
            CFStringGetCString((CFStringRef)v, buf, sizeof buf, kCFStringEncodingUTF8);
        } else if (CFGetTypeID(v) == CFNumberGetTypeID()) {
            long long n = 0;
            CFNumberGetValue((CFNumberRef)v, kCFNumberLongLongType, &n);
            snprintf(buf, sizeof buf, "%lld", n);
        } else if (CFGetTypeID(v) == CFBooleanGetTypeID()) {
            snprintf(buf, sizeof buf, "%d", CFBooleanGetValue((CFBooleanRef)v) ? 1 : 0);
        }
        result = buf;
    }
    CFRelease(k);
    CFRelease(a);
    return result;
}

bool hasPref(const char* appID, const char* key)
{
    CFStringRef a = CFStringCreateWithCString(NULL, appID, kCFStringEncodingUTF8);
    CFStringRef k = CFStringCreateWithCString(NULL, key, kCFStringEncodingUTF8);
    CFMutableDictionaryRef d = domain(a, false);
    bool has = d && CFDictionaryContainsKey(d, k);
    CFRelease(k);
    CFRelease(a);
    return has;
}

static std::string g_home;

/**
 * @brief Removes the fake home folder at exit.
 */
static void removeHome()
{
    if (g_home.empty()) return;
    std::string cmd = "rm -rf '" + g_home + "'";
    if (system(cmd.c_str()) != 0) fprintf(stderr, "warning: could not remove %s\n", g_home.c_str());
}

std::string home()
{
    if (g_home.empty()) {
        char t[] = "/tmp/synth1test-home.XXXXXX";
        char* r = mkdtemp(t);
        char real[PATH_MAX];
        g_home = (r && realpath(r, real)) ? real : "/tmp";
        atexit(removeHome);
    }
    return g_home;
}

std::string folderPath(OSType folderType)
{
    switch (folderType) {
    case kDocumentsFolderType: return home() + "/Documents";
    case kTemporaryFolderType: return home() + "/Temp";
    case kPreferencesFolderType: return home() + "/Library/Preferences";
    case kApplicationSupportFolderType: return home() + "/Library/Application Support";
    default: {
        char b[32];
        snprintf(b, sizeof b, "/Folder-%08x", (unsigned)folderType);
        return home() + b;
    }
    }
}

void resetHome()
{
    std::string cmd = "rm -rf '" + home() + "'/* '" + home() + "'/.[!.]*";
    if (system(cmd.c_str()) != 0) fprintf(stderr, "warning: could not clear %s\n", home().c_str());
}

}  // namespace t1fake

extern "C" {

/**
 * @brief Fake CFPreferencesCopyAppValue: reads the in-memory store.
 *
 * @param key            Preference key.
 * @param applicationID  Preference domain.
 * @return A retained copy of the value (caller releases), or NULL if not set.
 */
CFPropertyListRef t1_CFPreferencesCopyAppValue(CFStringRef key, CFStringRef applicationID)
{
    CFMutableDictionaryRef d = t1fake::domain(applicationID, false);
    CFPropertyListRef v = d ? CFDictionaryGetValue(d, key) : 0;
    if (v) CFRetain(v);
    return v;
}

/**
 * @brief Fake CFPreferencesSetAppValue: writes a deep copy of the value into the in-memory store.
 *
 * @param key            Preference key.
 * @param value          New value; NULL removes the key.
 * @param applicationID  Preference domain.
 */
void t1_CFPreferencesSetAppValue(CFStringRef key, CFPropertyListRef value, CFStringRef applicationID)
{
    t1fake::setCalls++;
    if (value) {
        // like the real CFPreferences, store a deep copy: callers may free (or never retain)
        // what they pass in right after the call
        CFPropertyListRef copy = CFPropertyListCreateDeepCopy(NULL, value, kCFPropertyListImmutable);
        CFDictionarySetValue(t1fake::domain(applicationID, true), key, copy ? copy : value);
        if (copy) CFRelease(copy);
    } else if (CFMutableDictionaryRef d = t1fake::domain(applicationID, false)) {
        CFDictionaryRemoveValue(d, key);
    }
}

/**
 * @brief Fake CFPreferencesAppSynchronize: counts the call.
 *
 * @param applicationID  Preference domain (unused).
 * @return Always true.
 */
Boolean t1_CFPreferencesAppSynchronize(CFStringRef applicationID)
{
    (void)applicationID;
    t1fake::syncCalls++;
    return true;
}

/**
 * @brief Fake FSFindFolder: resolves folders inside the fake home folder.
 *
 * @param vRefNum       Domain (ignored; everything maps into the fake home).
 * @param folderType    Folder type, e.g. kDocumentsFolderType.
 * @param createFolder  Create the folder (and its parents) if it is missing.
 * @param[out] foundRef FSRef of the folder.
 * @return noErr, or fnfErr if the folder does not exist and createFolder is false.
 */
OSErr t1_FSFindFolder(FSVolumeRefNum vRefNum, OSType folderType, Boolean createFolder, FSRef* foundRef)
{
    (void)vRefNum;
    std::string path = t1fake::folderPath(folderType);
    struct stat st;
    if (stat(path.c_str(), &st) != 0) {
        if (!createFolder) return fnfErr;
        std::string cmd = "mkdir -p '" + path + "' 2>/dev/null";
        if (system(cmd.c_str()) != 0) return fnfErr;
    }
    return FSPathMakeRef((const UInt8*)path.c_str(), foundRef, NULL) == noErr ? noErr : fnfErr;
}

}  // extern "C"
