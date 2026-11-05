/**
 * @file test_prefs.cpp
 * @brief Unit tests for the settings store: the ini functions of techpop/common.cpp
 *        (getIniString, getIniInt, setIniString, setIniInt), the CFPreferences-based
 *        GetPrivateProfileString / GetPrivateProfileInt / WritePrivateProfileString of
 *        winemu_au.cpp, and the folder lookups GetFolderPath (common.cpp) and GetTempPath
 *        (winemu_au.cpp).
 *
 * In the test build these files call the fakes of tests/harness/os_fakes.cpp: the preferences
 * are an in-memory store and the user folders live in a fake home folder under /tmp. Every
 * test starts from a clean store (prefstest::PrefsScope) or a clean fake home.
 */
#include "prefs_fixture.h"

#include "test.h"

#include <string.h>
#include <sys/stat.h>
#include <set>
#include <string>

#include "common.h"
#include "os_fakes.h"
#include "sound_fixture.h"

using prefstest::PrefsScope;
using prefstest::kDomain;
using prefstest::kGlobal;
using prefstest::stored;
using prefstest::keyCount;

namespace {

/**
 * @brief Name of the domain the profile functions use when given no file name
 *        (kCFPreferencesCurrentApplication).
 */
std::string currentAppDomain()
{
    return prefstest::toStd(kCFPreferencesCurrentApplication);
}

}  // namespace

// ---------------------------------------------------------------------------
// common.cpp: getIni* / setIni*

/** @brief setIniString stores the value as a string under its key in the "global" section of
 *         the "jp.daichi.synth1" domain, with one set and one synchronize call. */
TEST(prefs, set_ini_string_domain_and_section)
{
    PrefsScope ps;
    CHECK_EQ(setIniString("someKey", "some value"), 1);
    CHECK_STR(stored("someKey"), "some value");
    CHECK_STR(stored("someKey", kGlobal, kDomain), "some value");
    CHECK_EQ(keyCount(), 1);
    CHECK_EQ(t1fake::setCalls, 1);
    CHECK_EQ(t1fake::syncCalls, 1);
    // nothing in other sections or domains
    CHECK_STR(stored("someKey", "other"), "<unset>");
    CHECK_STR(stored("someKey", kGlobal, "jp.daichi.synth1.audiounit"), "<unset>");
}

/** @brief setIniInt stores the decimal text of the value; getIniInt parses it back, negative
 *         values included. */
TEST(prefs, set_ini_int_round_trip)
{
    PrefsScope ps;
    setIniInt("n", -42);
    CHECK_STR(stored("n"), "-42");
    CHECK_EQ(getIniInt("n", 7), -42);
    setIniInt("n", 0x87c8c8);
    CHECK_STR(stored("n"), "8898760");
    CHECK_EQ(getIniInt("n", 7), 0x87c8c8);
    setIniInt("n", 0);
    CHECK_STR(stored("n"), "0");
    CHECK_EQ(getIniInt("n", 7), 0);
    CHECK_EQ(keyCount(), 1);
    CHECK_EQ(t1fake::setCalls, 3);
    CHECK_EQ(t1fake::syncCalls, 3);
}

/** @brief getIniInt returns the default for a missing key or section and atoi() of the stored
 *         text otherwise (0 for non-numeric text, the leading number of mixed text). */
TEST(prefs, get_ini_int_parsing)
{
    PrefsScope ps;
    CHECK_EQ(getIniInt("missing", 123), 123);
    prefstest::seed("text", "abc");
    prefstest::seed("mixed", "12xyz");
    prefstest::seed("spaced", "  7");
    prefstest::seed("neg", "-3");
    CHECK_EQ(getIniInt("text", 5), 0);
    CHECK_EQ(getIniInt("mixed", 5), 12);
    CHECK_EQ(getIniInt("spaced", 5), 7);
    CHECK_EQ(getIniInt("neg", 5), -3);
    CHECK_EQ(getIniInt("missing", -9), -9);
    t1fake::resetPrefs();  // no "global" section at all
    CHECK_EQ(getIniInt("neg", 5), 5);
    // reading never writes
    CHECK_EQ(t1fake::setCalls, 0);
    CHECK_EQ(t1fake::syncCalls, 0);
}

/** @brief getIniString returns the stored text, or the default when the key or section is
 *         missing; the return value is the length of the result. */
TEST(prefs, get_ini_string)
{
    PrefsScope ps;
    char buf[256];
    strcpy(buf, "garbage");
    CHECK_EQ(getIniString("missing", buf, sizeof buf, "default"), 7u);
    CHECK_STREQ(buf, "default");
    CHECK_EQ(getIniString("missing", buf, sizeof buf, ""), 0u);
    CHECK_STREQ(buf, "");
    setIniString("path", "/Users/someone/Synth1 Patches/bank 1");
    CHECK_EQ(getIniString("path", buf, sizeof buf, "default"), 36u);
    CHECK_STREQ(buf, "/Users/someone/Synth1 Patches/bank 1");
    t1fake::resetPrefs();
    CHECK_EQ(getIniString("path", buf, sizeof buf, "def"), 3u);
    CHECK_STREQ(buf, "def");
}

/** @brief Non-ASCII text survives the round trip as UTF-8. */
TEST(prefs, utf8_round_trip)
{
    PrefsScope ps;
    const char* v = "/Users/\xe5\xa4\xa7\xe5\x9c\xb0/\xc3\xa9t\xc3\xa9";
    setIniString("utf8", v);
    char buf[256];
    getIniString("utf8", buf, sizeof buf, "");
    CHECK_STREQ(buf, v);
    CHECK_STR(stored("utf8"), v);
}

/** @brief Writing an existing key replaces its value; other keys of the section are kept. */
TEST(prefs, overwrite_keeps_other_keys)
{
    PrefsScope ps;
    setIniString("a", "1");
    setIniString("b", "2");
    setIniInt("c", 3);
    setIniString("a", "one");
    CHECK_EQ(keyCount(), 3);
    CHECK_STR(stored("a"), "one");
    CHECK_STR(stored("b"), "2");
    CHECK_STR(stored("c"), "3");
    CHECK_EQ(getIniInt("c", 0), 3);
    // an empty string is a value, not a removal
    setIniString("b", "");
    CHECK_STR(stored("b"), "");
    char buf[16];
    getIniString("b", buf, sizeof buf, "def");
    CHECK_STREQ(buf, "");
    CHECK_EQ(keyCount(), 3);
}

/** @brief Values seeded in the store (as the user's preferences) are what getIni* read. */
TEST(prefs, reads_seeded_values)
{
    PrefsScope ps;
    prefstest::seed("mastertune", "-12");
    prefstest::seed("installfolder", "/Applications/Synth1");
    CHECK_EQ(getIniInt("mastertune", 0), -12);
    char buf[64];
    getIniString("installfolder", buf, sizeof buf, "c:\\");
    CHECK_STREQ(buf, "/Applications/Synth1");
}

// ---------------------------------------------------------------------------
// winemu_au.cpp: *PrivateProfile* on CFPreferences

/** @brief The "file name" is the preference domain and the "section" one preference of it:
 *         sections and domains are independent of each other. */
TEST(prefs, private_profile_sections_and_domains)
{
    PrefsScope ps;
    prefstest::seedSection("sec", "test.domain");
    prefstest::seedSection(kGlobal, "test.domain");
    CHECK_EQ(WritePrivateProfileString("sec", "k", "in sec", "test.domain"), 1);
    CHECK_EQ(WritePrivateProfileString(kGlobal, "k", "in global", "test.domain"), 1);
    CHECK_EQ(WritePrivateProfileString(kGlobal, "k", "jp", kDomain), 1);
    CHECK_STR(stored("k", "sec", "test.domain"), "in sec");
    CHECK_STR(stored("k", kGlobal, "test.domain"), "in global");
    CHECK_STR(stored("k"), "jp");
    char buf[64];
    CHECK_EQ(GetPrivateProfileString("sec", "k", "d", buf, sizeof buf, "test.domain"), 6u);
    CHECK_STREQ(buf, "in sec");
    GetPrivateProfileString(kGlobal, "k", "d", buf, sizeof buf, "test.domain");
    CHECK_STREQ(buf, "in global");
    getIniString("k", buf, sizeof buf, "d");
    CHECK_STREQ(buf, "jp");
    GetPrivateProfileString("nosuch", "k", "d", buf, sizeof buf, "test.domain");
    CHECK_STREQ(buf, "d");
    GetPrivateProfileString("sec", "k", "d", buf, sizeof buf, "no.such.domain");
    CHECK_STREQ(buf, "d");
    CHECK_EQ(t1fake::setCalls, 3);
    CHECK_EQ(t1fake::syncCalls, 3);
}

/** @brief GetPrivateProfileInt reads its own section and domain and falls back to the default. */
TEST(prefs, private_profile_int)
{
    PrefsScope ps;
    prefstest::seed("x", "77", "sec", "test.domain");
    prefstest::seed("x", "5");
    CHECK_EQ((int)GetPrivateProfileInt("sec", "x", -1, "test.domain"), 77);
    CHECK_EQ((int)GetPrivateProfileInt(kGlobal, "x", -1, kDomain), 5);
    CHECK_EQ((int)GetPrivateProfileInt("sec", "y", -1, "test.domain"), -1);
    CHECK_EQ((int)GetPrivateProfileInt("nosec", "x", -2, "test.domain"), -2);
}

/** @brief A NULL file name means kCFPreferencesCurrentApplication for reads and writes. */
TEST(prefs, private_profile_null_file_is_current_application)
{
    PrefsScope ps;
    std::string app = currentAppDomain();
    REQUIRE(!app.empty());
    prefstest::seedSection("s", app.c_str());
    WritePrivateProfileString("s", "k", "v", NULL);
    CHECK_STR(stored("k", "s", app.c_str()), "v");
    char buf[16];
    GetPrivateProfileString("s", "k", "d", buf, sizeof buf, NULL);
    CHECK_STREQ(buf, "v");
    prefstest::seed("n", "31", "s", app.c_str());
    CHECK_EQ((int)GetPrivateProfileInt("s", "n", 0, NULL), 31);
    // not visible in the engine's own domain
    CHECK_STR(stored("k", "s"), "<unset>");
}

/** @brief A NULL key name lists the keys of the section as "key1\0key2\0...\0\0"; the return
 *         value is the length of the first key. */
TEST(prefs, private_profile_enumerates_keys)
{
    PrefsScope ps;
    setIniString("alpha", "1");
    setIniString("beta", "2");
    setIniString("gamma", "3");
    char buf[256];
    memset(buf, 'x', sizeof buf);
    DWORD n = GetPrivateProfileString(kGlobal, NULL, "", buf, sizeof buf, kDomain);
    std::set<std::string> keys;
    const char* p = buf;
    int count = 0;
    while (*p && count < 10) {
        keys.insert(p);
        p += strlen(p) + 1;
        count++;
    }
    CHECK_EQ(count, 3);
    CHECK(keys.count("alpha") == 1);
    CHECK(keys.count("beta") == 1);
    CHECK(keys.count("gamma") == 1);
    CHECK_EQ(p - buf, 17);  // "alpha\0beta\0gamma\0" in some order, then the final \0
    CHECK_EQ(n, (DWORD)strlen(buf));
    // an empty section gives an empty list
    prefstest::seedSection("empty", kDomain);
    memset(buf, 'x', sizeof buf);
    CHECK_EQ(GetPrivateProfileString("empty", NULL, "", buf, sizeof buf, kDomain), 0u);
    CHECK_EQ(buf[0], 0);
    // a missing section leaves the default
    CHECK_EQ(GetPrivateProfileString("nosection", NULL, "dflt", buf, sizeof buf, kDomain), 4u);
    CHECK_STREQ(buf, "dflt");
}

/** @brief A NULL key and value delete the whole section (one set call with NULL, then a
 *         synchronize). */
TEST(prefs, private_profile_delete_section)
{
    PrefsScope ps;
    setIniString("a", "1");
    setIniString("b", "2");
    CHECK_EQ(keyCount(), 2);
    CHECK_EQ(WritePrivateProfileString(kGlobal, NULL, NULL, kDomain), 1);
    CHECK_EQ(keyCount(), -1);
    CHECK(!t1fake::hasPref(kDomain, kGlobal));
    CHECK_EQ(t1fake::setCalls, 3);
    CHECK_EQ(t1fake::syncCalls, 3);
    char buf[16];
    getIniString("a", buf, sizeof buf, "gone");
    CHECK_STREQ(buf, "gone");
    CHECK_EQ(getIniInt("b", -1), -1);
}

/** @brief A NULL value for an existing section changes nothing (no set call), but the domain is
 *         still synchronized. */
TEST(prefs, private_profile_null_value_is_ignored)
{
    PrefsScope ps;
    setIniString("a", "1");
    CHECK_EQ(WritePrivateProfileString(kGlobal, "a", NULL, kDomain), 1);
    CHECK_EQ(WritePrivateProfileString(kGlobal, "b", NULL, kDomain), 1);
    CHECK_STR(stored("a"), "1");
    CHECK_STR(stored("b"), "<unset>");
    CHECK_EQ(t1fake::setCalls, 1);
    CHECK_EQ(t1fake::syncCalls, 3);
}

/** @brief Writing to a section that does not exist yet creates it with one set call and
 *         synchronizes the domain.
 *  @note Only the creation is checked: the new section dictionary is built with NULL callbacks
 *        (see prefs_fixture.h), which the real CFPreferences serializes at once but the fake
 *        store keeps by reference, so its entries cannot be read back here. */
TEST(prefs, write_to_missing_section_creates_it)
{
    t1fake::resetPrefs();
    CHECK(!t1fake::hasPref(kDomain, kGlobal));
    CHECK_EQ(setIniString("first", "value"), 1);
    CHECK(t1fake::hasPref(kDomain, kGlobal));
    CHECK_EQ(t1fake::setCalls, 1);
    CHECK_EQ(t1fake::syncCalls, 1);
    // a NULL value creates nothing
    t1fake::resetPrefs();
    CHECK_EQ(WritePrivateProfileString("fresh", "k", NULL, kDomain), 1);
    CHECK(!t1fake::hasPref(kDomain, "fresh"));
    CHECK_EQ(t1fake::setCalls, 0);
    CHECK_EQ(t1fake::syncCalls, 1);
    t1fake::resetPrefs();
}

// ---------------------------------------------------------------------------
// folders

/** @brief GetFolderPath returns the POSIX path of an existing user folder and leaves the buffer
 *         unchanged when the folder does not exist (it is not created). */
TEST(folders, get_folder_path)
{
    t1fake::resetHome();
    std::string docs = t1fake::folderPath(kDocumentsFolderType);
    char buf[1024];
    strcpy(buf, "unchanged");
    GetFolderPath(kDocumentsFolderType, buf, sizeof buf);
    CHECK_STREQ(buf, "unchanged");
    CHECK(!soundtest::dirExists(docs));
    REQUIRE(soundtest::makeDir(docs));
    GetFolderPath(kDocumentsFolderType, buf, sizeof buf);
    CHECK_STR(buf, docs);
    // a different folder type resolves to its own folder
    std::string prefs = t1fake::folderPath(kPreferencesFolderType);
    strcpy(buf, "unchanged");
    GetFolderPath(kPreferencesFolderType, buf, sizeof buf);
    CHECK_STREQ(buf, "unchanged");
    std::string cmd = "mkdir -p '" + prefs + "'";
    REQUIRE(system(cmd.c_str()) == 0);
    GetFolderPath(kPreferencesFolderType, buf, sizeof buf);
    CHECK_STR(buf, prefs);
    t1fake::resetHome();
}

/** @brief GetTempPath returns the user temporary folder, creating it if needed, and returns 0. */
TEST(folders, get_temp_path_creates_folder)
{
    t1fake::resetHome();
    std::string tmp = t1fake::folderPath(kTemporaryFolderType);
    CHECK(!soundtest::dirExists(tmp));
    char buf[1024];
    strcpy(buf, "x");
    CHECK_EQ(GetTempPath(sizeof buf, buf), 0u);
    CHECK_STR(buf, tmp);
    CHECK(soundtest::dirExists(tmp));
    // again, with the folder present
    strcpy(buf, "x");
    GetTempPath(sizeof buf, buf);
    CHECK_STR(buf, tmp);
    t1fake::resetHome();
}
