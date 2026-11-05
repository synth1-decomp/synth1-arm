/**
 * @file test_soundlib_setup.cpp
 * @brief Unit tests for the first-instance setup of CSoundLibrary (techpop/sound.cpp): the
 *        default bank and zip folders below Documents/Synth1Patches, the "bankfolder<N>" and
 *        "zipfolder" preferences, sandbox mode, and the persistence of changeBankFolder() and
 *        updateZipFolderPath() outside the sandbox.
 *
 * soundtest::FirstInstanceState makes the next CSoundLibrary the first instance (reference
 * count 0) and restores the shared library state afterwards. Documents is the fake home's
 * (tests/harness/os_fakes.h) and the preferences are the fake in-memory store, both cleared
 * at the start and the end of every test.
 */
#include "prefs_fixture.h"

#include "test.h"

#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>
#include <string>

#include "common.h"
#include "engine.h"
#include "stubs.h"
#include "synth1.h"
#include "sound_fixture.h"

using prefstest::PrefsScope;
using prefstest::stored;
using soundtest::Buf;
using soundtest::FirstInstanceState;
using soundtest::TempDir;
using soundtest::dirExists;

namespace {

/**
 * @brief The fake Documents folder.
 */
std::string docs()
{
    return t1fake::folderPath(kDocumentsFolderType);
}

/**
 * @brief Default path of numbered bank i: <Documents>/Synth1Patches/soundbank<ii>.
 */
std::string defaultBank(int i)
{
    char b[32];
    sprintf(b, "/Synth1Patches/soundbank%02d", i);
    return docs() + b;
}

/**
 * @brief Default zip folder: <Documents>/Synth1Patches/zipbank.
 */
std::string defaultZip()
{
    return docs() + "/Synth1Patches/zipbank";
}

}  // namespace

/** @brief Without preferences the first instance points bank i at
 *         Documents/Synth1Patches/soundbank<ii> and the zip folder at .../zipbank, creating
 *         Documents, soundbank00 and zipbank (only those) and allocating the zip containers. */
TEST(soundlib_setup, defaults_below_documents)
{
    FirstInstanceState st;
    PrefsScope ps;
    CHECK(!dirExists(docs()));
    CSoundLibrary* lib = st.newLibrary();
    REQUIRE(lib != NULL);
    CHECK_EQ(s1g->m_soundLibRefCount, 1);
    CHECK(dirExists(docs()));
    CHECK(dirExists(defaultBank(0)));
    CHECK(!dirExists(defaultBank(1)));
    CHECK(!dirExists(defaultBank(99)));
    CHECK(dirExists(defaultZip()));
    for (int i = 0; i < BANK_FOLDER_NUM; i++) {
        if (s1g->m_banks[i].m_path != defaultBank(i) || s1g->m_banks[i].m_bankNo != i) {
            CHECK_STR(s1g->m_banks[i].m_path, defaultBank(i));
            CHECK_EQ(s1g->m_banks[i].m_bankNo, i);
            break;
        }
    }
    CHECK_STR(s1g->m_banks[42].m_path, defaultBank(42));
    CHECK_STREQ(s1g->m_banks[7].m_name, "soundbank07");
    CHECK(s1g->m_banks[0].m_bExist);
    CHECK(!s1g->m_banks[1].m_bExist);
    CHECK_STR(s1g->m_zipFolder, defaultZip());
    CHECK(s1g->m_pZipRootBanks != NULL);
    CHECK(s1g->m_pZipBankMap != NULL);
    CHECK_EQ(CSoundLibrary::getZipNum(), 0);
    // the setup only reads the preferences
    CHECK_EQ(t1fake::setCalls, 0);
    CHECK_EQ(t1fake::syncCalls, 0);
}

/** @brief Later instances skip the setup: they only bump the reference count and leave the bank
 *         paths, the zip folder and the zip containers alone. */
TEST(soundlib_setup, second_instance_skips_setup)
{
    FirstInstanceState st;
    PrefsScope ps;
    st.newLibrary();
    std::vector<CBank*>* roots = s1g->m_pZipRootBanks;
    TempDir tmp;
    soundtest::LibState::setBank(0, tmp.path());
    soundtest::LibState::setZipFolder(tmp.path());
    prefstest::seed("bankfolder1", "/nowhere/else");
    st.newLibrary();
    CHECK_EQ(s1g->m_soundLibRefCount, 2);
    CHECK_STR(s1g->m_banks[0].m_path, tmp.path());
    CHECK_STR(s1g->m_banks[1].m_path, defaultBank(1));
    CHECK_STR(s1g->m_zipFolder, tmp.path());
    CHECK(s1g->m_pZipRootBanks == roots);
}

/** @brief An existing default bank 0 folder is used as it is (its content is kept). */
TEST(soundlib_setup, existing_default_folder_is_kept)
{
    FirstInstanceState st;
    PrefsScope ps;
    std::string cmd = "mkdir -p '" + defaultBank(0) + "' '" + defaultZip() + "'";
    REQUIRE(system(cmd.c_str()) == 0);
    REQUIRE(soundtest::writeFile(defaultBank(0) + "/keep.txt", "hello"));
    REQUIRE(soundtest::writeFile(defaultZip() + "/keep.txt", "zip"));
    st.newLibrary();
    CHECK_STR(soundtest::readFile(defaultBank(0) + "/keep.txt"), "hello");
    CHECK_STR(soundtest::readFile(defaultZip() + "/keep.txt"), "zip");
    CHECK_STR(s1g->m_banks[0].m_path, defaultBank(0));
    CHECK(s1g->m_banks[0].m_bExist);
}

/** @brief Outside the sandbox, stored "bankfolder<N>" and "zipfolder" preferences choose the
 *         folders; custom folders are not created, nor are the unused defaults, and the zip
 *         folder from the preferences is scanned. */
TEST(soundlib_setup, folders_from_preferences)
{
    FirstInstanceState st;
    PrefsScope ps;
    TempDir tmp;
    REQUIRE(!tmp.path().empty());
    std::string custom0 = tmp.sub("custom0");        // missing: stays missing
    std::string custom7 = tmp.sub("custom7");
    std::string zips = tmp.sub("zips");
    REQUIRE(soundtest::makeDir(custom7));
    REQUIRE(soundtest::makeDir(zips));
    REQUIRE(!soundtest::putZip("flat.zip", zips, "setup.zip").empty());
    prefstest::seed("bankfolder0", custom0.c_str());
    prefstest::seed("bankfolder7", custom7.c_str());
    prefstest::seed("bankfolder99", "/Volumes/Patches/last");
    prefstest::seed("zipfolder", zips.c_str());
    t1stub::sandBox = 0;
    st.newLibrary();
    CHECK_STR(s1g->m_banks[0].m_path, custom0);
    CHECK(!dirExists(custom0));
    CHECK(!s1g->m_banks[0].m_bExist);
    CHECK_STR(s1g->m_banks[7].m_path, custom7);
    CHECK(s1g->m_banks[7].m_bExist);
    CHECK_STREQ(s1g->m_banks[7].m_name, "custom7");
    CHECK_STR(s1g->m_banks[99].m_path, "/Volumes/Patches/last");
    CHECK_STR(s1g->m_banks[1].m_path, defaultBank(1));
    CHECK_STR(s1g->m_banks[98].m_path, defaultBank(98));
    CHECK_STR(s1g->m_zipFolder, zips);
    CHECK_EQ(CSoundLibrary::getZipNum(), 1);
    CHECK(!dirExists(defaultBank(0)));
    CHECK(!dirExists(defaultZip()));
    CHECK_EQ(t1fake::setCalls, 0);
}

/** @brief A stored path equal to the default is treated like the default: the missing bank 0
 *         and zip folders are created. */
TEST(soundlib_setup, stored_default_paths_are_created)
{
    FirstInstanceState st;
    PrefsScope ps;
    prefstest::seed("bankfolder0", defaultBank(0).c_str());
    prefstest::seed("bankfolder1", defaultBank(1).c_str());
    prefstest::seed("zipfolder", defaultZip().c_str());
    t1stub::sandBox = 0;
    st.newLibrary();
    CHECK(dirExists(defaultBank(0)));
    CHECK(!dirExists(defaultBank(1)));  // only bank 0 is ever created
    CHECK(dirExists(defaultZip()));
    CHECK_STR(s1g->m_banks[0].m_path, defaultBank(0));
    CHECK_STR(s1g->m_zipFolder, defaultZip());
}

/** @brief In the sandbox the preferences are ignored: every bank and the zip folder use the
 *         defaults below Documents, which are created as without preferences. */
TEST(soundlib_setup, sandbox_ignores_preferences)
{
    FirstInstanceState st;
    PrefsScope ps;
    TempDir tmp;
    std::string zips = tmp.sub("zips");
    REQUIRE(soundtest::makeDir(zips));
    REQUIRE(!soundtest::putZip("flat.zip", zips, "sandbox.zip").empty());
    prefstest::seed("bankfolder0", tmp.sub("custom0").c_str());
    prefstest::seed("bankfolder5", tmp.sub("custom5").c_str());
    prefstest::seed("zipfolder", zips.c_str());
    t1stub::sandBox = 1;
    st.newLibrary();
    CHECK_STR(s1g->m_banks[0].m_path, defaultBank(0));
    CHECK_STR(s1g->m_banks[5].m_path, defaultBank(5));
    CHECK_STR(s1g->m_zipFolder, defaultZip());
    CHECK(dirExists(defaultBank(0)));
    CHECK(dirExists(defaultZip()));
    CHECK_EQ(CSoundLibrary::getZipNum(), 0);
    CHECK_EQ(t1fake::setCalls, 0);
    // the stored values are untouched
    CHECK_STR(stored("bankfolder0"), tmp.sub("custom0"));
    CHECK_STR(stored("zipfolder"), zips);
}

/** @brief Outside the sandbox, changeBankFolder() and updateZipFolderPath() store
 *         "bankfolder<N>" and "zipfolder", and the next first instance (a new session) picks the
 *         stored folders up. */
TEST(soundlib_setup, changed_folders_persist_across_sessions)
{
    FirstInstanceState st;
    PrefsScope ps;
    TempDir tmp;
    std::string b2 = tmp.sub("b2");
    std::string zips = tmp.sub("zips");
    REQUIRE(soundtest::makeDir(b2));
    REQUIRE(soundtest::makeDir(zips));
    REQUIRE(!soundtest::putZip("flat.zip", zips, "persist.zip").empty());
    t1stub::sandBox = 0;
    CSoundLibrary* lib = st.newLibrary();
    CHECK_EQ(lib->changeBankFolder(2, Buf(b2)), 1);
    CSoundLibrary::updateZipFolderPath(Buf(zips));
    CHECK_STR(stored("bankfolder2"), b2);
    CHECK_STR(stored("zipfolder"), zips);
    CHECK_EQ(CSoundLibrary::getZipNum(), 1);
    CHECK_EQ(t1fake::setCalls, 2);
    CHECK_EQ(t1fake::syncCalls, 2);
    // "restart": the last instance goes away, the shared state is wiped, a new first instance
    st.deleteLibraries();
    CHECK_EQ(s1g->m_soundLibRefCount, 0);
    soundtest::LibState::setBank(2, "/somewhere/stale");
    s1g->m_zipFolder[0] = 0;
    st.newLibrary();
    CHECK_STR(s1g->m_banks[2].m_path, b2);
    CHECK(s1g->m_banks[2].m_bExist);
    CHECK_STR(s1g->m_banks[3].m_path, defaultBank(3));
    CHECK_STR(s1g->m_zipFolder, zips);
    CHECK_EQ(CSoundLibrary::getZipNum(), 1);
}

/** @brief In the sandbox changeBankFolder() and updateZipFolderPath() only change the running
 *         state; a new session starts from the defaults again. */
TEST(soundlib_setup, sandbox_changes_do_not_persist)
{
    FirstInstanceState st;
    PrefsScope ps;
    TempDir tmp;
    std::string b2 = tmp.sub("b2");
    REQUIRE(soundtest::makeDir(b2));
    t1stub::sandBox = 1;
    CSoundLibrary* lib = st.newLibrary();
    lib->changeBankFolder(2, Buf(b2));
    CSoundLibrary::updateZipFolderPath(Buf(tmp.path()));
    CHECK_STR(s1g->m_banks[2].m_path, b2);
    CHECK_STR(s1g->m_zipFolder, tmp.path());
    CHECK_EQ(t1fake::setCalls, 0);
    CHECK_EQ(t1fake::syncCalls, 0);
    st.deleteLibraries();
    st.newLibrary();
    CHECK_STR(s1g->m_banks[2].m_path, defaultBank(2));
    CHECK_STR(s1g->m_zipFolder, defaultZip());
}

/** @brief A CSynthesizer1 created with no other library instance runs the same setup through
 *         its embedded CSoundLibrary and loads the initial sound from the empty default bank. */
TEST(soundlib_setup, first_synth_sets_up_library)
{
    FirstInstanceState st;
    PrefsScope ps;
    t1stub::sandBox = 1;
    CSynthesizer1* s = new CSynthesizer1();
    CHECK_EQ(s1g->m_soundLibRefCount, 1);
    CHECK(dirExists(defaultBank(0)));
    CHECK(dirExists(defaultZip()));
    CHECK_STR(s1g->m_banks[0].m_path, defaultBank(0));
    CHECK_STREQ(s->getCurrentSound()->m_name, "initial sound");
    delete s;
    CHECK_EQ(s1g->m_soundLibRefCount, 0);
}

namespace {

/**
 * @brief Makes the fake home folder read-only, so FSFindFolder cannot create Documents
 *        (restores the permissions on destruction).
 */
struct ReadOnlyHome {
    ReadOnlyHome() { chmod(t1fake::home().c_str(), 0555); }
    ~ReadOnlyHome() { chmod(t1fake::home().c_str(), 0755); }
};

}  // namespace

/** @brief When the Documents folder cannot be found the setup falls back to "/" as the
 *         documents path: the defaults become /Synth1Patches/soundbank<ii> and
 *         /Synth1Patches/zipbank (no doubled slash).
 *  @note Creating /Synth1Patches fails for a normal user, so nothing is written; the test
 *        does nothing if "/" is writable (e.g. when run as root). */
TEST(soundlib_setup, documents_not_found_uses_root)
{
    if (access("/", W_OK) == 0 || dirExists("/Synth1Patches")) {
        printf("     (skipped: / is writable or /Synth1Patches exists)\n");
        return;
    }
    FirstInstanceState st;
    PrefsScope ps;
    ReadOnlyHome ro;
    t1stub::sandBox = 1;
    st.newLibrary();
    CHECK(!dirExists(docs()));
    CHECK_STR(s1g->m_banks[0].m_path, "/Synth1Patches/soundbank00");
    CHECK_STR(s1g->m_banks[99].m_path, "/Synth1Patches/soundbank99");
    CHECK_STR(s1g->m_zipFolder, "/Synth1Patches/zipbank");
    CHECK(!s1g->m_banks[0].m_bExist);
    CHECK(!dirExists("/Synth1Patches"));
    CHECK_EQ(CSoundLibrary::getZipNum(), 0);
}
