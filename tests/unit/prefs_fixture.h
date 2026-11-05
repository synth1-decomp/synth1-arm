/**
 * @file prefs_fixture.h
 * @brief Helpers for the tests of the settings code (the common.cpp getIni and setIni
 *        functions, the winemu_au.cpp PrivateProfile functions and their callers): seeding
 *        and inspecting the in-memory preference store of tests/harness/os_fakes.cpp.
 *
 * The engine keeps its settings in the preferences domain "jp.daichi.synth1": each ini section
 * ("global" for all of Synth1's settings) is one preference whose value is a dictionary of
 * string keys to string values (integers are stored as decimal strings).
 *
 * @note WritePrivateProfileString() builds a *new* section dictionary with NULL callbacks (it does
 * not retain its key and value, which are released right after CFPreferencesSetAppValue()). The
 * real CFPreferences serializes the value on the spot, so this is harmless in the product, but
 * the fake store keeps the dictionary object itself, whose entries are then dangling and are
 * looked up by pointer. PrefsScope therefore seeds an empty, properly retaining "global"
 * dictionary, so that every write goes through the copy-and-modify path
 * (CFDictionaryCreateMutableCopy() keeps the source's retaining callbacks) and stays valid.
 */
#pragma once

#include <CoreFoundation/CoreFoundation.h>
#include <string>

#include "os_fakes.h"

extern "C" {
// the fake CFPreferences entry points the engine is redirected to (os_fakes.cpp)
CFPropertyListRef t1_CFPreferencesCopyAppValue(CFStringRef key, CFStringRef applicationID);
void t1_CFPreferencesSetAppValue(CFStringRef key, CFPropertyListRef value, CFStringRef applicationID);
}

namespace prefstest {

/** @brief The engine's preference domain (common.cpp getIniPath()). */
static const char* const kDomain = "jp.daichi.synth1";

/** @brief The ini section all getIni and setIni settings live in. */
static const char* const kGlobal = "global";

/**
 * @brief Owns a CFString made from a C string.
 */
struct CFStr {
    CFStringRef s;
    explicit CFStr(const char* c) : s(CFStringCreateWithCString(NULL, c, kCFStringEncodingUTF8)) {}
    ~CFStr() { if (s) CFRelease(s); }
    operator CFStringRef() const { return s; }

private:
    CFStr(const CFStr&);
    CFStr& operator=(const CFStr&);
};

/**
 * @brief Converts a CFString to std::string ("<not a string>" for other types).
 */
inline std::string toStd(CFTypeRef v)
{
    if (!v || CFGetTypeID(v) != CFStringGetTypeID()) return "<not a string>";
    char buf[8192] = "";
    CFStringGetCString((CFStringRef)v, buf, sizeof buf, kCFStringEncodingUTF8);
    return buf;
}

/**
 * @brief Installs an empty, retaining section dictionary (replacing any section content).
 *
 * Does not count as an engine write: the fake's setCalls counter is restored.
 * @param section  Section name.
 * @param domain   Preference domain.
 */
inline void seedSection(const char* section = kGlobal, const char* domain = kDomain)
{
    int calls = t1fake::setCalls;
    CFMutableDictionaryRef d = CFDictionaryCreateMutable(NULL, 0, &kCFTypeDictionaryKeyCallBacks,
                                                         &kCFTypeDictionaryValueCallBacks);
    t1_CFPreferencesSetAppValue(CFStr(section), d, CFStr(domain));
    CFRelease(d);
    t1fake::setCalls = calls;
}

/**
 * @brief Stores key=value in a section, as if the user's preferences held it.
 *
 * Does not count as an engine write: the fake's setCalls counter is restored.
 * @param key      Key.
 * @param value    String value.
 * @param section  Section name.
 * @param domain   Preference domain.
 */
inline void seed(const char* key, const char* value, const char* section = kGlobal,
                 const char* domain = kDomain)
{
    int calls = t1fake::setCalls;
    CFStr sec(section), dom(domain);
    CFDictionaryRef old = (CFDictionaryRef)t1_CFPreferencesCopyAppValue(sec, dom);
    CFMutableDictionaryRef d;
    if (old) {
        d = CFDictionaryCreateMutableCopy(NULL, 0, old);
        CFRelease(old);
    } else {
        d = CFDictionaryCreateMutable(NULL, 0, &kCFTypeDictionaryKeyCallBacks,
                                      &kCFTypeDictionaryValueCallBacks);
    }
    CFDictionarySetValue(d, CFStr(key), CFStr(value));
    t1_CFPreferencesSetAppValue(sec, d, dom);
    CFRelease(d);
    t1fake::setCalls = calls;
}

/**
 * @brief Reads a stored key of a section.
 *
 * @param key      Key.
 * @param section  Section name.
 * @param domain   Preference domain.
 * @return The stored string, "<unset>" if the key (or the section) does not exist.
 */
inline std::string stored(const char* key, const char* section = kGlobal, const char* domain = kDomain)
{
    CFDictionaryRef d = (CFDictionaryRef)t1_CFPreferencesCopyAppValue(CFStr(section), CFStr(domain));
    if (!d) return "<unset>";
    std::string r = "<unset>";
    if (CFGetTypeID(d) == CFDictionaryGetTypeID()) {
        CFTypeRef v = CFDictionaryGetValue(d, CFStr(key));
        if (v) r = toStd(v);
    } else {
        r = "<section is not a dictionary>";
    }
    CFRelease(d);
    return r;
}

/**
 * @brief Number of keys in a section (-1 if the section does not exist).
 */
inline int keyCount(const char* section = kGlobal, const char* domain = kDomain)
{
    CFDictionaryRef d = (CFDictionaryRef)t1_CFPreferencesCopyAppValue(CFStr(section), CFStr(domain));
    if (!d) return -1;
    int n = (int)CFDictionaryGetCount(d);
    CFRelease(d);
    return n;
}

/**
 * @brief Clean preferences for one test: clears the fake store and seeds an empty "global"
 *        section (see the file note); clears the store again on destruction.
 */
struct PrefsScope {
    PrefsScope()
    {
        t1fake::resetPrefs();
        seedSection();
    }
    ~PrefsScope() { t1fake::resetPrefs(); }
};

}  // namespace prefstest
