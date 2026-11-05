/**
 * @file test_soundlib.cpp
 * @brief Unit tests for techpop/sound.cpp, part 2: CSoundLibrary (bank lookup, program and bank
 *        selection, loadProgram()/saveProgram() and the .sy1 file format) and the zip banks
 *        (unzip(), CBank::extractZipBank(), checkZipFile(), getZipFilesHash(), getZipRootBank()).
 *
 * Every test runs under soundtest::LibState, which pretends a first CSoundLibrary instance already exists
 * so the constructor does not read the user's preferences or touch ~/Documents, and enables the
 * sandbox stub so no preferences are written. Patch folders and zip folders are mkdtemp folders.
 * The zip fixtures (tests/data/sound/*.zip, made by tests/data/sound/mkzips.py) are copied under
 * process-unique names; extractZipBank() always extracts into the user temp folder
 * (GetTempPath()/synth1/zip), and those folders are removed by LibState.
 */
#include "sound_fixture.h"

#include <stdexcept>
#include <signal.h>
#include <sys/resource.h>

#include "zlib.h"

using soundtest::Buf;
using soundtest::LibState;
using soundtest::TempDir;
using soundtest::writeFile;

namespace {

/**
 * @brief Fills a CSoundData to use as the loadProgram() default sound.
 */
void makeInitSound(CSoundData& s)
{
    s.init();
    strcpy(s.m_name, "init sound");
    s.m_color = RGB(255, 0, 0);
    s.m_version = 113;
    s.m_bankNo = 55;
    s.m_progNo = 66;
    memset(s.m_path, 0, sizeof(s.m_path));
    strcpy(s.m_path, "init path");
    s.setParamVal(0, 10);
    s.setParamVal(1, 20);
    s.setParamVal(2, 30);
}

/**
 * @brief Fills a CSoundData with no parameters, colour 0 and version 1 (all fields defined).
 */
void plainInit(CSoundData& s)
{
    s.init();
    s.m_color = 0;
    s.m_version = 1;
    s.m_bankNo = 0;
    s.m_progNo = 0;
    memset(s.m_path, 0, sizeof(s.m_path));
}

/**
 * @brief Gives a library instance a defined request and loaded-sound location.
 */
void resetRequest(CSoundLibrary& lib)
{
    lib.setReqBankNo(0, NULL);
    lib.setReqProgNo(0);
    lib.m_sound.m_bankNo = -99;
    lib.m_sound.m_progNo = -99;
    lib.m_sound.m_path[0] = 0;
    lib.m_sound.m_version = 0;
    lib.m_sound.m_color = 0;
}

/**
 * @brief Writes a patch file as 001.sy1 into bank 0's folder and loads it.
 */
CSoundData* loadText(CSoundLibrary& lib, const TempDir& tmp, const std::string& text, CSoundData& init)
{
    std::string dir = tmp.sub("bank0");
    soundtest::makeDir(dir);
    writeFile(dir + "/001.sy1", text);
    LibState::setBank(0, dir);
    lib.setReqBankNo(0, NULL);
    lib.setReqProgNo(0);
    return lib.loadProgram(&init);
}

/**
 * @brief Recomputes CSoundLibrary::getZipFilesHash()'s contribution of one file.
 */
int hashOf(const std::string& folder, const std::string& name)
{
    struct stat st;
    std::string p = folder + "/" + name;
    if (stat(p.c_str(), &st) != 0) return 0;
    char buf[1400];
    sprintf(buf, "%s:%llu:%llu", p.c_str(), (unsigned long long)st.st_size,
            (unsigned long long)st.st_mtime);
    int h = 0;
    for (const char* q = buf; *q; q++) h = h * 31 + *q;
    return h;
}

/**
 * @brief Size of a file in bytes (the hash extractZipBank() gets from checkZipFile()).
 */
unsigned int fileSize(const std::string& p)
{
    struct stat st;
    return stat(p.c_str(), &st) == 0 ? (unsigned int)st.st_size : 0;
}

/**
 * @brief The folder extractZipBank() uses for a zip file name and hash.
 */
std::string extractDirFor(const std::string& zipName, unsigned int hash)
{
    char h[16];
    sprintf(h, "%05x", hash);
    return soundtest::zipTempRoot() + "/" + zipName.substr(0, zipName.size() - 3) + h;
}

/**
 * @brief Appends a little-endian 16-bit value.
 */
void put16(std::string& z, unsigned int v)
{
    z += (char)(v & 0xff);
    z += (char)((v >> 8) & 0xff);
}

/**
 * @brief Appends a little-endian 32-bit value.
 */
void put32(std::string& z, unsigned int v)
{
    put16(z, v & 0xffff);
    put16(z, v >> 16);
}

/**
 * @brief Builds a zip archive of stored (uncompressed) entries.
 *
 * @param names     Entry names.
 * @param data      Entry contents.
 * @param n         Number of entries.
 * @param method    Compression method written to the headers (0 = stored; others are not
 *                  supported by minizip).
 * @param prefix    Bytes placed before the archive (minizip never finds an end record at offset 0).
 * @return The archive bytes.
 */
std::string buildZip(const std::string* names, const std::string* data, int n, int method,
                     const std::string& prefix)
{
    std::string z = prefix, cd;
    for (int i = 0; i < n; i++) {
        unsigned int crc = crc32(0L, (const Bytef*)data[i].data(), (uInt)data[i].size());
        unsigned int off = (unsigned int)(z.size() - prefix.size());
        put32(z, 0x04034b50);
        put16(z, 20); put16(z, 0); put16(z, method); put16(z, 0); put16(z, 0x21);
        put32(z, crc); put32(z, (unsigned int)data[i].size()); put32(z, (unsigned int)data[i].size());
        put16(z, (unsigned int)names[i].size()); put16(z, 0);
        z += names[i];
        z += data[i];
        put32(cd, 0x02014b50);
        put16(cd, 20); put16(cd, 20); put16(cd, 0); put16(cd, method); put16(cd, 0); put16(cd, 0x21);
        put32(cd, crc); put32(cd, (unsigned int)data[i].size()); put32(cd, (unsigned int)data[i].size());
        put16(cd, (unsigned int)names[i].size()); put16(cd, 0); put16(cd, 0); put16(cd, 0); put16(cd, 0);
        put32(cd, 0); put32(cd, off);
        cd += names[i];
    }
    unsigned int cdOff = (unsigned int)(z.size() - prefix.size());
    z += cd;
    put32(z, 0x06054b50);
    put16(z, 0); put16(z, 0); put16(z, n); put16(z, n);
    put32(z, (unsigned int)cd.size()); put32(z, cdOff); put16(z, 0);
    return z;
}

}  // namespace

// ---------------------------------------------------------------------------
// instances and bank lookup

/** @brief Further instances only bump the reference count; the shared state is left alone. */
TEST(soundlib, instances_are_reference_counted)
{
    LibState st;
    std::vector<CBank*>* roots = s1g->m_pZipRootBanks;
    {
        CSoundLibrary a;
        CHECK_EQ(s1g->m_soundLibRefCount, 2);
        CSoundLibrary* b = new CSoundLibrary;
        CHECK_EQ(s1g->m_soundLibRefCount, 3);
        delete b;
        CHECK_EQ(s1g->m_soundLibRefCount, 2);
    }
    CHECK_EQ(s1g->m_soundLibRefCount, 1);
    CHECK(s1g->m_pZipRootBanks == roots);
}

/** @brief The last instance deletes the zip banks and containers and resets the zip hash. */
TEST(soundlib, last_instance_frees_zip_banks)
{
    LibState st;
    TempDir tmp;
    REQUIRE(!tmp.path().empty());
    REQUIRE(!soundtest::putZip("flat.zip", tmp.path(), "last.zip").empty());
    LibState::setZipFolder(tmp.path());
    CSoundLibrary::checkZipFile();
    REQUIRE(CSoundLibrary::getZipNum() == 1);
    CHECK(s1g->m_zipFilesHash != 0);
    CSoundLibrary* lib = new CSoundLibrary;
    s1g->m_soundLibRefCount = 1;  // make it the last instance
    delete lib;
    CHECK_EQ(s1g->m_soundLibRefCount, 0);
    CHECK_EQ(s1g->m_zipFilesHash, 0);
    // the containers are gone; give LibState fresh ones to clean up
    s1g->m_pZipRootBanks = new std::vector<CBank*>;
    s1g->m_pZipBankMap = new BankMap;
    s1g->m_soundLibRefCount = 1;
}

/** @brief getBank returns the numbered banks and looks zip banks up by inner path. */
TEST(soundlib, get_bank)
{
    LibState st;
    CHECK(CSoundLibrary::getBank(0, NULL) == &s1g->m_banks[0]);
    CHECK(CSoundLibrary::getBank(99, NULL) == &s1g->m_banks[99]);
    CHECK(CSoundLibrary::getBank(5, Buf("ignored")) == &s1g->m_banks[5]);
    CHECK(CSoundLibrary::getBank(-1, NULL) == NULL);
    CHECK(CSoundLibrary::getBank(-1, Buf("nothing.zip")) == NULL);
    CBank* zb = new CBank;
    zb->m_bankNo = -1;
    strcpy(zb->m_innerPath, "x.zip/sub");
    s1g->m_pZipBankMap->insert(std::make_pair(std::string("x.zip/sub"), zb));
    CHECK(CSoundLibrary::getBank(-1, Buf("x.zip/sub")) == zb);
    CHECK(CSoundLibrary::getBank(-1, Buf("x.zip")) == NULL);
    s1g->m_pZipBankMap->clear();
    delete zb;
}

/** @brief changeBankFolder (sandboxed) points a numbered bank at a new folder and rescans it. */
TEST(soundlib, change_bank_folder)
{
    LibState st;
    TempDir tmp;
    REQUIRE(!tmp.path().empty());
    REQUIRE(soundtest::makeDir(tmp.sub("Keys")));
    REQUIRE(writeFile(tmp.sub("Keys/001.sy1"), "K\ncolor=red\n"));
    REQUIRE(writeFile(tmp.sub("Keys/002.sy1"), "L\ncolor=red\n"));
    CSoundLibrary lib;
    CHECK(lib.changeBankFolder(1, Buf(tmp.sub("Keys"))));
    CHECK_STR(s1g->m_banks[1].getPath(), tmp.sub("Keys"));
    CHECK_STREQ(s1g->m_banks[1].m_name, "Keys");
    CHECK_EQ(s1g->m_banks[1].m_patchNum, 2);
    CHECK(s1g->m_banks[1].m_bExist);
}

/** @brief getProgramNameColor reports names/colours, "initial sound" and "not found". */
TEST(soundlib, get_program_name_color)
{
    LibState st;
    TempDir tmp;
    REQUIRE(!tmp.path().empty());
    REQUIRE(writeFile(tmp.sub("001.sy1"), "Piano\ncolor=yellow\nver=113\n"));
    LibState::setBank(0, tmp.path());
    char name[64];
    unsigned int color = 0;
    CHECK(CSoundLibrary::getProgramNameColor(0, NULL, 0, name, &color, 0));
    CHECK_STREQ(name, "Piano");
    CHECK_EQ(color, RGB(255, 255, 0));
    CHECK(CSoundLibrary::getProgramNameColor(0, NULL, 1, name, &color, 0));
    CHECK_STREQ(name, "initial sound");
    CHECK_EQ(color, 0xffffffffu);
    // NULL outputs are allowed
    CHECK(CSoundLibrary::getProgramNameColor(0, NULL, 0, NULL, &color, 0));
    CHECK(CSoundLibrary::getProgramNameColor(0, NULL, 0, name, NULL, 0));

    // a new file is seen only with bCheckCache
    REQUIRE(writeFile(tmp.sub("002.sy1"), "Organ\ncolor=green\n"));
    CSoundLibrary::getProgramNameColor(0, NULL, 1, name, &color, 0);
    CHECK_STREQ(name, "initial sound");
    CSoundLibrary::getProgramNameColor(0, NULL, 1, name, &color, 1);
    CHECK_STREQ(name, "Organ");
    CHECK_EQ(color, RGB(0, 255, 0));

    strcpy(name, "x");
    color = 123;
    CHECK(!CSoundLibrary::getProgramNameColor(-1, Buf("missing.zip"), 0, name, &color, 1));
    CHECK_STREQ(name, "not found");
    CHECK_EQ(color, 123u);
    CHECK(!CSoundLibrary::getProgramNameColor(-1, NULL, 0, NULL, NULL, 0));
}

// ---------------------------------------------------------------------------
// bank and program selection

/** @brief isReqPatchLoaded clamps the request (program 0..127, bank 0..99) and writes it back. */
TEST(sound_select, request_is_clamped)
{
    LibState st;
    CSoundLibrary lib;
    resetRequest(lib);
    lib.setReqProgNo(-5);
    lib.setReqBankNo(150, NULL);
    CHECK(!lib.isReqPatchLoaded());
    CHECK_EQ(lib.m_reqProgNo, 0);
    CHECK_EQ(lib.m_reqBankNo, 99);
    lib.setReqProgNo(500);
    lib.setReqBankNo(-3, NULL);
    lib.isReqPatchLoaded();
    CHECK_EQ(lib.m_reqProgNo, 127);
    CHECK_EQ(lib.m_reqBankNo, 0);
    lib.setReqProgNo(64);
    lib.setReqBankNo(-1, Buf("a.zip"));
    lib.isReqPatchLoaded();
    CHECK_EQ(lib.m_reqProgNo, 64);
    CHECK_EQ(lib.m_reqBankNo, -1);  // zip banks are not clamped
    CHECK_STREQ(lib.m_reqZipPath, "a.zip");
    lib.setReqBankNo(3, NULL);
    CHECK_STREQ(lib.m_reqZipPath, "");
}

/** @brief After loadProgram the request counts as loaded until the bank or program changes. */
TEST(sound_select, loaded_tracking)
{
    LibState st;
    TempDir tmp;
    REQUIRE(!tmp.path().empty());
    LibState::setBank(2, tmp.path());
    CSoundLibrary lib;
    resetRequest(lib);
    CSoundData init;
    makeInitSound(init);
    lib.setReqBankNo(2, NULL);
    lib.setReqProgNo(7);
    CHECK(!lib.isReqPatchLoaded());
    lib.loadProgram(&init);
    CHECK(lib.isReqPatchLoaded());
    lib.setReqProgNo(8);
    CHECK(!lib.isReqPatchLoaded());
    lib.setReqProgNo(7);
    CHECK(lib.isReqPatchLoaded());
    lib.setReqBankNo(3, NULL);
    CHECK(!lib.isReqPatchLoaded());
    lib.setReqBankNo(2, NULL);
    CHECK(lib.isReqPatchLoaded());

    // zip requests also compare the inner path
    lib.setReqBankNo(-1, Buf("z.zip/a"));
    CHECK(!lib.isReqPatchLoaded());
    lib.loadProgram(&init);  // bank not found: defaults, but the location is recorded
    CHECK(lib.isReqPatchLoaded());
    lib.setReqBankNo(-1, Buf("z.zip/b"));
    CHECK(!lib.isReqPatchLoaded());
}

// ---------------------------------------------------------------------------
// loadProgram and the .sy1 format

/** @brief A complete file sets name, colour, version and parameters; other init values remain. */
TEST(sound_patchfile, load_full_file)
{
    LibState st;
    TempDir tmp;
    REQUIRE(!tmp.path().empty());
    CSoundLibrary lib;
    resetRequest(lib);
    CSoundData init;
    makeInitSound(init);
    CSoundData* s = loadText(lib, tmp, "Fat Bass\ncolor=blue\nver=112\n1,99\n5,-12\n17,0\n", init);
    CHECK(s == &lib.m_sound);
    CHECK_STREQ(s->m_name, "Fat Bass");
    CHECK_EQ(s->m_color, RGB(0, 0, 255));
    CHECK_EQ(s->m_version, 112);
    CHECK_EQ(s->m_bankNo, 0);
    CHECK_EQ(s->m_progNo, 0);
    CHECK_STREQ(s->m_path, "");
    CHECK_EQ(s->getParamsNum(), 5);
    CHECK_EQ(s->getParamVal(0), 10);  // from init
    CHECK_EQ(s->getParamVal(1), 99);  // overridden
    CHECK_EQ(s->getParamVal(2), 30);
    CHECK_EQ(s->getParamVal(5), -12);
    CHECK_EQ(s->getParamVal(17), 0);
    int id, val;
    s->enumParam(3, &id, &val);  // new ids are appended after the init ones
    CHECK_EQ(id, 5);
    CHECK_EQ(val, -12);
    // init is not modified
    CHECK_STREQ(init.m_name, "init sound");
    CHECK_EQ(init.getParamVal(1), 20);
}

/** @brief A file without "ver=" gets version 100; without "color=" keeps the init colour. */
TEST(sound_patchfile, missing_version_and_color)
{
    LibState st;
    TempDir tmp;
    REQUIRE(!tmp.path().empty());
    CSoundLibrary lib;
    resetRequest(lib);
    CSoundData init;
    makeInitSound(init);
    CSoundData* s = loadText(lib, tmp, "Old Patch\n3,33\n", init);
    CHECK_STREQ(s->m_name, "Old Patch");
    CHECK_EQ(s->m_version, 100);
    CHECK_EQ(s->m_color, RGB(255, 0, 0));
    CHECK_EQ(s->getParamVal(3), 33);
}

/** @brief Unknown colour names give the default colour; colour/version lines may come anywhere. */
TEST(sound_patchfile, color_and_version_anywhere)
{
    LibState st;
    TempDir tmp;
    REQUIRE(!tmp.path().empty());
    CSoundLibrary lib;
    resetRequest(lib);
    CSoundData init;
    makeInitSound(init);
    CSoundData* s = loadText(lib, tmp, "P\n1,1\nver=105\ncolor=green\n2,2\ncolor=nope\nver=7\n", init);
    CHECK_EQ(s->m_color, 0xffffffffu);  // the last colour line wins
    CHECK_EQ(s->m_version, 7);
    CHECK_EQ(s->getParamVal(1), 1);
    CHECK_EQ(s->getParamVal(2), 2);
}

/** @brief CRLF line ends are handled like LF. */
TEST(sound_patchfile, crlf)
{
    LibState st;
    TempDir tmp;
    REQUIRE(!tmp.path().empty());
    CSoundLibrary lib;
    resetRequest(lib);
    CSoundData init;
    makeInitSound(init);
    CSoundData* s = loadText(lib, tmp, "Win Patch\r\ncolor=cyan\r\nver=113\r\n4,44\r\n", init);
    CHECK_STREQ(s->m_name, "Win Patch");
    CHECK_EQ(s->m_color, RGB(0, 255, 255));
    CHECK_EQ(s->m_version, 113);
    CHECK_EQ(s->getParamVal(4), 44);
}

/**
 * @brief Parameter lines are parsed with atoi: spaces and trailing junk are tolerated, a
 *        non-numeric id is 0, duplicates keep the last value, other lines are ignored.
 */
TEST(sound_patchfile, lenient_parameter_parsing)
{
    LibState st;
    TempDir tmp;
    REQUIRE(!tmp.path().empty());
    CSoundLibrary lib;
    resetRequest(lib);
    CSoundData init;
    plainInit(init);
    CSoundData* s = loadText(lib, tmp,
                             "Lenient\n"
                             " 12 , -7\n"
                             "13,5abc\n"
                             "x,3\n"
                             "14,\n"
                             "15,8,9\n"
                             "12,70\n"
                             "something else\n"
                             "color = red\n"
                             "VER=200\n",
                             init);
    CHECK_STREQ(s->m_name, "Lenient");
    CHECK_EQ(s->getParamVal(12), 70);
    CHECK_EQ(s->getParamVal(13), 5);
    CHECK_EQ(s->getParamVal(0), 3);
    CHECK_EQ(s->getParamVal(14), 0);
    CHECK_EQ(s->getParamVal(15), 8);
    CHECK_EQ(s->getParamsNum(), 5);  // 12, 13, 0, 14, 15
    CHECK_EQ(s->m_color, 0u);        // "color = red" is not a colour line
    CHECK_EQ(s->m_version, 100);     // "VER=" is case sensitive
}

/** @brief A line with a comma is a parameter even if it starts with "color=". */
TEST(sound_patchfile, comma_wins_over_keywords)
{
    LibState st;
    TempDir tmp;
    REQUIRE(!tmp.path().empty());
    CSoundLibrary lib;
    resetRequest(lib);
    CSoundData init;
    plainInit(init);
    CSoundData* s = loadText(lib, tmp, "K\ncolor=red,4\nver=9,5\n", init);
    CHECK_EQ(s->m_color, 0u);
    CHECK_EQ(s->m_version, 100);
    CHECK_EQ(s->getParamsNum(), 1);
    CHECK_EQ(s->getParamVal(0), 5);  // both lines are id 0; the last value wins
}

/** @brief A name longer than 31 characters is cut; the rest of the line is parsed as a line. */
TEST(sound_patchfile, long_name_spills_into_next_line)
{
    LibState st;
    TempDir tmp;
    REQUIRE(!tmp.path().empty());
    CSoundLibrary lib;
    resetRequest(lib);
    CSoundData init;
    plainInit(init);
    CSoundData* s = loadText(lib, tmp, "ABCDEFGHIJKLMNOPQRSTUVWXYZ01234,56789\nver=113\n", init);
    CHECK_STREQ(s->m_name, "ABCDEFGHIJKLMNOPQRSTUVWXYZ01234");
    CHECK_EQ(s->getParamsNum(), 1);
    CHECK_EQ(s->getParamVal(0), 56789);  // ",56789" became parameter 0
    CHECK_EQ(s->m_version, 113);
}

/** @brief Lines longer than 254 characters are read in pieces, each parsed on its own. */
TEST(sound_patchfile, long_line_is_split)
{
    LibState st;
    TempDir tmp;
    REQUIRE(!tmp.path().empty());
    CSoundLibrary lib;
    resetRequest(lib);
    CSoundData init;
    plainInit(init);
    std::string line = "7,5" + std::string(251, ' ') + "8,6\n";
    CSoundData* s = loadText(lib, tmp, "Long\n" + line, init);
    CHECK_EQ(s->getParamsNum(), 2);
    CHECK_EQ(s->getParamVal(7), 5);
    CHECK_EQ(s->getParamVal(8), 6);
}

/** @brief More than 100 distinct ids: the extra ones are dropped. */
TEST(sound_patchfile, too_many_parameters)
{
    LibState st;
    TempDir tmp;
    REQUIRE(!tmp.path().empty());
    CSoundLibrary lib;
    resetRequest(lib);
    CSoundData init;
    plainInit(init);
    std::string text = "Many\nver=113\n";
    char line[32];
    for (int i = 0; i < 120; i++) {
        sprintf(line, "%d,%d\n", i, i * 2);
        text += line;
    }
    CSoundData* s = loadText(lib, tmp, text, init);
    CHECK_EQ(s->getParamsNum(), SOUNDDATA_PARAMS_MAX);
    CHECK_EQ(s->getParamVal(99), 198);
    CHECK_EQ(s->getParamVal(100), 0);
    CHECK_EQ(s->getParamVal(119), 0);
}

/** @brief A name-only file sets the name and version 100 and keeps the init parameters. */
TEST(sound_patchfile, name_only_file)
{
    LibState st;
    TempDir tmp;
    REQUIRE(!tmp.path().empty());
    CSoundLibrary lib;
    resetRequest(lib);
    CSoundData init;
    makeInitSound(init);
    CSoundData* s = loadText(lib, tmp, "Just A Name\n", init);
    CHECK_STREQ(s->m_name, "Just A Name");
    CHECK_EQ(s->m_version, 100);
    CHECK_EQ(s->m_color, RGB(255, 0, 0));
    CHECK_EQ(s->getParamsNum(), 3);
}

/** @brief An empty file leaves the init name and version (the version is not set to 100). */
TEST(sound_patchfile, empty_file)
{
    LibState st;
    TempDir tmp;
    REQUIRE(!tmp.path().empty());
    CSoundLibrary lib;
    resetRequest(lib);
    CSoundData init;
    makeInitSound(init);
    CSoundData* s = loadText(lib, tmp, "", init);
    CHECK_STREQ(s->m_name, "init sound");
    CHECK_EQ(s->m_version, 113);
    CHECK_EQ(s->getParamVal(2), 30);
}

/** @brief Missing file, bank without folder or unknown zip bank: a copy of the init sound. */
TEST(sound_patchfile, missing_patch_gives_init_copy)
{
    LibState st;
    TempDir tmp;
    REQUIRE(!tmp.path().empty());
    CSoundLibrary lib;
    resetRequest(lib);
    CSoundData init;
    makeInitSound(init);
    LibState::setBank(1, tmp.path());
    lib.setReqBankNo(1, NULL);
    lib.setReqProgNo(9);
    CSoundData* s = lib.loadProgram(&init);
    CHECK_STREQ(s->m_name, "init sound");
    CHECK_EQ(s->m_color, RGB(255, 0, 0));
    CHECK_EQ(s->m_version, 113);
    CHECK_EQ(s->getParamsNum(), 3);
    CHECK_EQ(s->getParamVal(1), 20);
    CHECK_EQ(s->m_bankNo, 1);
    CHECK_EQ(s->m_progNo, 9);
    CHECK_STREQ(s->m_path, "");

    // bank whose folder is unset
    s1g->m_banks[6].m_path[0] = 0;
    lib.setReqBankNo(6, NULL);
    s = lib.loadProgram(&init);
    CHECK_STREQ(s->m_name, "init sound");
    CHECK_EQ(s->m_bankNo, 6);

    lib.setReqBankNo(-1, Buf("missing.zip/sub"));
    lib.setReqProgNo(3);
    s = lib.loadProgram(&init);
    CHECK_STREQ(s->m_name, "init sound");
    CHECK_EQ(s->m_bankNo, -1);
    CHECK_EQ(s->m_progNo, 3);
    CHECK_STREQ(s->m_path, "missing.zip/sub");
}

/** @brief Loading the next patch starts again from the init sound (no leftovers). */
TEST(sound_patchfile, loads_do_not_accumulate)
{
    LibState st;
    TempDir tmp;
    REQUIRE(!tmp.path().empty());
    REQUIRE(writeFile(tmp.sub("001.sy1"), "One\ncolor=red\nver=113\n50,1\n"));
    REQUIRE(writeFile(tmp.sub("002.sy1"), "Two\ncolor=blue\nver=113\n51,2\n"));
    LibState::setBank(0, tmp.path());
    CSoundLibrary lib;
    resetRequest(lib);
    CSoundData init;
    makeInitSound(init);
    lib.setReqProgNo(0);
    lib.loadProgram(&init);
    CHECK_EQ(lib.m_sound.getParamVal(50), 1);
    lib.setReqProgNo(1);
    CSoundData* s = lib.loadProgram(&init);
    CHECK_STREQ(s->m_name, "Two");
    CHECK_EQ(s->getParamVal(50), 0);
    CHECK_EQ(s->getParamVal(51), 2);
    CHECK_EQ(s->getParamsNum(), 4);
}

// ---------------------------------------------------------------------------
// saveProgram

/** @brief saveProgram writes the exact text format and updates m_sound and the slot. */
TEST(sound_patchfile, save_writes_exact_format)
{
    LibState st;
    TempDir tmp;
    REQUIRE(!tmp.path().empty());
    LibState::setBank(3, tmp.path());
    CSoundLibrary lib;
    resetRequest(lib);
    lib.m_sound.init();
    lib.m_sound.m_version = 113;
    lib.m_sound.setParamVal(2, 64);
    lib.m_sound.setParamVal(0, -3);
    lib.m_sound.setParamVal(45, 127);
    WriteProgramInfo info;
    info.bankNo = 3;
    info.progNo = 11;
    strcpy(info.name, "Saved Lead");
    info.color = RGB(255, 255, 0);
    CHECK_EQ(lib.saveProgram(&info), 1);
    CHECK_STR(soundtest::readFile(tmp.sub("012.sy1")),
                "Saved Lead\ncolor=yellow\nver=113\n2,64\n0,-3\n45,127\n");
    CHECK_EQ(lib.m_sound.m_bankNo, 3);
    CHECK_EQ(lib.m_sound.m_progNo, 11);
    CHECK_STREQ(lib.m_sound.m_name, "Saved Lead");
    CHECK_EQ(lib.m_sound.m_color, RGB(255, 255, 0));
    CHECK(s1g->m_banks[3].m_patch[11].isInfoNotLoaded());
    char name[64];
    unsigned int color;
    CHECK(CSoundLibrary::getProgramNameColor(3, NULL, 11, name, &color, 1));
    CHECK_STREQ(name, "Saved Lead");
    CHECK_EQ(color, RGB(255, 255, 0));
}

/** @brief Colours without a name are saved as "default"; an empty patch has only the header. */
TEST(sound_patchfile, save_default_color_and_no_params)
{
    LibState st;
    TempDir tmp;
    REQUIRE(!tmp.path().empty());
    LibState::setBank(3, tmp.path());
    CSoundLibrary lib;
    resetRequest(lib);
    lib.m_sound.init();
    lib.m_sound.m_version = 100;
    WriteProgramInfo info;
    info.bankNo = 3;
    info.progNo = 127;
    strcpy(info.name, "Empty");
    info.color = RGB(10, 20, 30);
    CHECK_EQ(lib.saveProgram(&info), 1);
    CHECK_STR(soundtest::readFile(tmp.sub("128.sy1")), "Empty\ncolor=default\nver=100\n");
    CHECK_EQ(lib.m_sound.m_color, RGB(10, 20, 30));  // m_sound keeps the given value
}

/** @brief Save then load gives back the same name, colour, version and parameters. */
TEST(sound_patchfile, save_load_round_trip)
{
    LibState st;
    TempDir tmp;
    REQUIRE(!tmp.path().empty());
    LibState::setBank(4, tmp.path());
    CSoundLibrary lib;
    resetRequest(lib);
    lib.m_sound.init();
    lib.m_sound.m_version = 113;
    for (int i = 0; i < SOUNDDATA_PARAMS_MAX; i++) {
        lib.m_sound.setParamVal((i * 37) % 251, i * 13 - 600);
    }
    CSoundData saved = lib.m_sound;
    WriteProgramInfo info;
    info.bankNo = 4;
    info.progNo = 63;
    strcpy(info.name, "Round Trip 30 characters long!");
    info.color = RGB(0, 255, 255);
    REQUIRE(lib.saveProgram(&info) == 1);

    CSoundLibrary lib2;
    resetRequest(lib2);
    CSoundData init;
    plainInit(init);
    lib2.setReqBankNo(4, NULL);
    lib2.setReqProgNo(63);
    CSoundData* s = lib2.loadProgram(&init);
    CHECK_STREQ(s->m_name, "Round Trip 30 characters long!");
    CHECK_EQ(s->m_color, RGB(0, 255, 255));
    CHECK_EQ(s->m_version, 113);
    CHECK_EQ(s->getParamsNum(), saved.getParamsNum());
    bool same = true;
    for (int i = 0; i < SOUNDDATA_PARAMS_MAX; i++) {
        int id1, v1, id2, v2;
        saved.enumParam(i, &id1, &v1);
        s->enumParam(i, &id2, &v2);
        if (id1 != id2 || v1 != v2) same = false;
    }
    CHECK(same);

    // saving the loaded patch again reproduces the file byte for byte
    std::string first = soundtest::readFile(tmp.sub("064.sy1"));
    info.progNo = 64;
    REQUIRE(lib2.saveProgram(&info) == 1);
    CHECK(soundtest::readFile(tmp.sub("065.sy1")) == first);
}

/** @brief The bank folder (and missing parents) is created on save. */
TEST(sound_patchfile, save_creates_folder)
{
    LibState st;
    TempDir tmp;
    REQUIRE(!tmp.path().empty());
    std::string dir = tmp.sub("new/deeper/bank");
    LibState::setBank(5, dir);
    CHECK(!s1g->m_banks[5].m_bExist);
    CSoundLibrary lib;
    resetRequest(lib);
    lib.m_sound.init();
    lib.m_sound.m_version = 113;
    lib.m_sound.setParamVal(1, 1);
    WriteProgramInfo info;
    info.bankNo = 5;
    info.progNo = 0;
    strcpy(info.name, "Fresh");
    info.color = RGB(255, 0, 0);
    CHECK_EQ(lib.saveProgram(&info), 1);
    CHECK(soundtest::dirExists(dir));
    CHECK_STR(soundtest::readFile(dir + "/001.sy1"), "Fresh\ncolor=red\nver=113\n1,1\n");
}

/** @brief Saving fails with -1 for zip banks and file errors, and 0 for a bank without folder. */
TEST(sound_patchfile, save_errors)
{
    LibState st;
    TempDir tmp;
    REQUIRE(!tmp.path().empty());
    CSoundLibrary lib;
    resetRequest(lib);
    lib.m_sound.init();
    strcpy(lib.m_sound.m_name, "Unchanged");
    lib.m_sound.m_version = 113;
    WriteProgramInfo info;
    info.bankNo = -1;
    info.progNo = 0;
    strcpy(info.name, "X");
    info.color = 0xffffffff;
    CHECK_EQ(lib.saveProgram(&info), -1);

    s1g->m_banks[6].m_path[0] = 0;
    info.bankNo = 6;
    CHECK_EQ(lib.saveProgram(&info), 0);

    REQUIRE(writeFile(tmp.sub("plainfile"), "x"));
    strcpy(s1g->m_banks[7].m_path, tmp.sub("plainfile/bank").c_str());
    info.bankNo = 7;
    CHECK_EQ(lib.saveProgram(&info), -1);
    CHECK_STREQ(lib.m_sound.m_name, "Unchanged");
    CHECK_EQ(lib.m_sound.m_bankNo, -99);
}

// ---------------------------------------------------------------------------
// zip support

/** @brief unzip extracts files and folders (explicit and implicit) with their contents. */
TEST(sound_zip, unzip_extracts_tree)
{
    LibState st;
    TempDir tmp;
    REQUIRE(!tmp.path().empty());
    std::string zip = soundtest::fixture("nested.zip");
    REQUIRE(!zip.empty());
    std::string out = tmp.sub("out");
    REQUIRE(soundtest::makeDir(out));
    CHECK(unzip(Buf(zip), Buf(out)));
    CHECK_EQ(t1stub::messageBoxCalls, 0);
    CHECK_STR(soundtest::readFile(out + "/005.sy1"), "Top Five\ncolor=yellow\nver=113\n7,70\n");
    CHECK_STR(soundtest::readFile(out + "/b/001.sy1"), "B One\ncolor=magenta\nver=113\n1,11\n");
    CHECK_STR(soundtest::readFile(out + "/b/deep/002.sy1"), "Deep Two\ncolor=blue\nver=113\n2,23\n");
    CHECK_STR(soundtest::readFile(out + "/a/002.sy1"), "A Two\ncolor=green\nver=113\n4,44\n");
    CHECK(soundtest::dirExists(out + "/a"));
    CHECK(soundtest::dirExists(out + "/b/deep"));
}

/** @brief unzip fails (with a message box) for a missing, non-zip or empty archive. */
TEST(sound_zip, unzip_failures)
{
    LibState st;
    TempDir tmp;
    REQUIRE(!tmp.path().empty());
    std::string out = tmp.sub("out");
    REQUIRE(soundtest::makeDir(out));
    CHECK(!unzip(Buf(tmp.sub("nothing.zip")), Buf(out)));
    CHECK_EQ(t1stub::messageBoxCalls, 1);
    std::string bad = soundtest::fixture("corrupt.zip");
    REQUIRE(!bad.empty());
    CHECK(!unzip(Buf(bad), Buf(out)));
    CHECK_EQ(t1stub::messageBoxCalls, 2);
    std::string empty = soundtest::fixture("empty.zip");
    REQUIRE(!empty.empty());
    CHECK(!unzip(Buf(empty), Buf(out)));  // no first entry to read
    CHECK_EQ(t1stub::messageBoxCalls, 3);
}

/**
 * @brief extractZipBank extracts into <temp>/synth1/zip/<name without "zip"><hash in %05x>,
 *        marks it with "extractDone" and later returns at once without extracting again.
 */
TEST(sound_zip, extract_zip_bank)
{
    LibState st;
    TempDir tmp;
    REQUIRE(!tmp.path().empty());
    std::string name = soundtest::putZip("flat.zip", tmp.path(), "ex.zip");
    REQUIRE(!name.empty());
    char out[1024];
    CHECK(CBank::extractZipBank(Buf(tmp.sub(name)), out, 0x1f));
    std::string expect = extractDirFor(name, 0x1f);
    CHECK_STR(out, expect);
    CHECK(soundtest::fileExists(expect + "/extractDone"));
    CHECK_STR(soundtest::readFile(expect + "/001.sy1"), "Flat One\ncolor=red\nver=113\n0,1\n1,64\n");
    CHECK(soundtest::fileExists(expect + "/readme.txt"));

    REQUIRE(remove((expect + "/002.sy1").c_str()) == 0);
    char out2[1024];
    CHECK(CBank::extractZipBank(Buf(tmp.sub(name)), out2, 0x1f));
    CHECK_STREQ(out2, out);
    CHECK(!soundtest::fileExists(expect + "/002.sy1"));  // not extracted again

    // another hash is another folder
    CHECK(CBank::extractZipBank(Buf(tmp.sub(name)), out2, 0x12345));
    CHECK_STR(out2, extractDirFor(name, 0x12345));
    CHECK(soundtest::fileExists(std::string(out2) + "/002.sy1"));
    CHECK_EQ(t1stub::messageBoxCalls, 0);
}

/** @brief A broken zip is not marked done, so the extraction is retried next time. */
TEST(sound_zip, extract_zip_bank_failure)
{
    LibState st;
    TempDir tmp;
    REQUIRE(!tmp.path().empty());
    std::string name = soundtest::putZip("corrupt.zip", tmp.path(), "bad.zip");
    REQUIRE(!name.empty());
    char out[1024];
    CHECK(!CBank::extractZipBank(Buf(tmp.sub(name)), out, 7));
    CHECK_EQ(t1stub::messageBoxCalls, 1);
    CHECK(!soundtest::fileExists(std::string(out) + "/extractDone"));
    CHECK(!CBank::extractZipBank(Buf(tmp.sub(name)), out, 7));
    CHECK_EQ(t1stub::messageBoxCalls, 2);
}

/** @brief getZipFilesHash sums h*31+c over "path:size:mtime" of the *.zip files only. */
TEST(sound_zip, zip_files_hash)
{
    LibState st;
    TempDir tmp;
    REQUIRE(!tmp.path().empty());
    CHECK_EQ(CSoundLibrary::getZipFilesHash(Buf(tmp.sub("missing"))), 0);
    CHECK_EQ(CSoundLibrary::getZipFilesHash(Buf(tmp.path())), 0);
    REQUIRE(writeFile(tmp.sub("a.zip"), "12345"));
    int ha = hashOf(tmp.path(), "a.zip");
    CHECK(ha != 0);
    CHECK_EQ(CSoundLibrary::getZipFilesHash(Buf(tmp.path())), ha);
    CHECK_EQ(CSoundLibrary::getZipFilesHash(Buf(tmp.path() + "/")), ha);  // trailing slash
    // ignored: other extensions, upper case ".ZIP", folders named *.zip
    REQUIRE(writeFile(tmp.sub("notes.txt"), "x"));
    REQUIRE(writeFile(tmp.sub("B.ZIP"), "x"));
    REQUIRE(soundtest::makeDir(tmp.sub("dir.zip")));
    CHECK_EQ(CSoundLibrary::getZipFilesHash(Buf(tmp.path())), ha);
    REQUIRE(writeFile(tmp.sub("c.zip"), "abc"));
    int hc = hashOf(tmp.path(), "c.zip");
    CHECK_EQ(CSoundLibrary::getZipFilesHash(Buf(tmp.path())), (int)((unsigned)ha + (unsigned)hc));
    // the size is part of the hash
    REQUIRE(writeFile(tmp.sub("c.zip"), "abcd"));
    CHECK(CSoundLibrary::getZipFilesHash(Buf(tmp.path())) != (int)((unsigned)ha + (unsigned)hc));
}

/** @brief A missing or empty zip folder gives no zip banks. */
TEST(sound_zip, check_zip_file_no_zips)
{
    LibState st;
    TempDir tmp;
    REQUIRE(!tmp.path().empty());
    LibState::setZipFolder(tmp.sub("missing"));
    CSoundLibrary::checkZipFile();
    CHECK_EQ(CSoundLibrary::getZipNum(), 0);
    LibState::setZipFolder(tmp.path());
    CSoundLibrary::checkZipFile();
    CHECK_EQ(CSoundLibrary::getZipNum(), 0);
    CHECK_EQ(s1g->m_zipFilesHash, 0);
    CHECK_STR(CSoundLibrary::getZipFolderPath(), tmp.path());
}

/** @brief A zip with patches at the top becomes one root bank named after the zip file. */
TEST(sound_zip, flat_zip_root_bank)
{
    LibState st;
    TempDir tmp;
    REQUIRE(!tmp.path().empty());
    std::string name = soundtest::putZip("flat.zip", tmp.path(), "flat.zip");
    REQUIRE(!name.empty());
    LibState::setZipFolder(tmp.path());
    CSoundLibrary::checkZipFile();
    CHECK_EQ(s1g->m_zipFilesHash, hashOf(tmp.path(), name));
    REQUIRE(CSoundLibrary::getZipNum() == 1);
    CBank* root = CSoundLibrary::getZipRootBank(0);
    CHECK_EQ(root->m_bankNo, -1);
    CHECK_STR(root->m_name, name);
    CHECK_STR(root->m_innerPath, name);
    CHECK_STR(root->m_zipPath, tmp.sub(name));
    std::string dir = extractDirFor(name, fileSize(tmp.sub(name)));
    CHECK_STR(root->getPath(), dir);
    CHECK_STR(root->m_readmePath, (dir + "/readme.txt"));
    CHECK(root->m_bExist);
    CHECK_EQ(root->m_patchNum, 3);
    CHECK_EQ(root->getChildNum(), 0);
    CHECK(CSoundLibrary::getBank(-1, Buf(name)) == root);
    CHECK_EQ(s1g->m_pZipBankMap->size(), 1);

    char pname[64];
    unsigned int color;
    CHECK(CSoundLibrary::getProgramNameColor(-1, Buf(name), 9, pname, &color, 1));
    CHECK_STREQ(pname, "Flat Ten");
    CHECK_EQ(color, 0xffffffffu);
    CHECK(CSoundLibrary::getProgramNameColor(-1, Buf(name), 1, pname, &color, 0));
    CHECK_STREQ(pname, "Flat Two");
    CHECK_EQ(color, RGB(0, 0, 255));
    CHECK(CSoundLibrary::getProgramNameColor(-1, Buf(name), 2, pname, &color, 0));
    CHECK_STREQ(pname, "initial sound");
}

/** @brief Patches of a zip bank load through loadProgram; saving to a zip bank is refused. */
TEST(sound_zip, load_and_save_zip_program)
{
    LibState st;
    TempDir tmp;
    REQUIRE(!tmp.path().empty());
    std::string name = soundtest::putZip("flat.zip", tmp.path(), "load.zip");
    REQUIRE(!name.empty());
    LibState::setZipFolder(tmp.path());
    CSoundLibrary::checkZipFile();
    REQUIRE(CSoundLibrary::getZipNum() == 1);
    CSoundLibrary lib;
    resetRequest(lib);
    CSoundData init;
    makeInitSound(init);
    lib.setReqBankNo(-1, Buf(name));
    lib.setReqProgNo(0);
    CSoundData* s = lib.loadProgram(&init);
    CHECK_STREQ(s->m_name, "Flat One");
    CHECK_EQ(s->m_color, RGB(255, 0, 0));
    CHECK_EQ(s->m_version, 113);
    CHECK_EQ(s->getParamVal(0), 1);
    CHECK_EQ(s->getParamVal(1), 64);
    CHECK_EQ(s->getParamVal(2), 30);
    CHECK_EQ(s->m_bankNo, -1);
    CHECK_STR(s->m_path, name);
    CHECK(lib.isReqPatchLoaded());

    lib.setReqProgNo(9);  // CRLF file without "ver="
    s = lib.loadProgram(&init);
    CHECK_STREQ(s->m_name, "Flat Ten");
    CHECK_EQ(s->m_version, 100);
    CHECK_EQ(s->m_color, 0xffffffffu);
    CHECK_EQ(s->getParamVal(5), 7);

    WriteProgramInfo info;
    info.bankNo = -1;
    info.progNo = 0;
    strcpy(info.name, "Nope");
    info.color = 0xffffffff;
    CHECK_EQ(lib.saveProgram(&info), -1);
}

/** @brief A zip holding only one folder (plus a readme) is replaced by that folder's bank. */
TEST(sound_zip, single_folder_zip_is_unwrapped)
{
    LibState st;
    TempDir tmp;
    REQUIRE(!tmp.path().empty());
    std::string name = soundtest::putZip("single.zip", tmp.path(), "single.zip");
    REQUIRE(!name.empty());
    LibState::setZipFolder(tmp.path());
    CSoundLibrary::checkZipFile();
    REQUIRE(CSoundLibrary::getZipNum() == 1);
    CBank* root = CSoundLibrary::getZipRootBank(0);
    std::string dir = extractDirFor(name, fileSize(tmp.sub(name)));
    CHECK_STR(root->m_name, name);  // takes the zip's name
    CHECK_STR(root->m_innerPath, (name + "/Inner Bank"));
    CHECK_STR(root->getPath(), (dir + "/Inner Bank"));
    CHECK_STR(root->m_readmePath, (dir + "/readme.txt"));  // the zip's readme
    CHECK_EQ(root->m_patchNum, 2);
    CHECK_EQ(root->m_bankNo, -1);
    CHECK(CSoundLibrary::getBank(-1, Buf(name + "/Inner Bank")) == root);
    char pname[64];
    unsigned int color;
    CHECK(CSoundLibrary::getProgramNameColor(-1, Buf(name + "/Inner Bank"), 2, pname, &color, 0));
    CHECK_STREQ(pname, "Inner Three");
    CHECK_EQ(color, RGB(0, 255, 255));
    // note: the map entry for the zip name itself still points to the deleted root
    // (documented), so it is not looked up here
}

/** @brief Folders nested in a single folder are unwrapped repeatedly. */
TEST(sound_zip, double_wrapped_zip_is_unwrapped_twice)
{
    LibState st;
    TempDir tmp;
    REQUIRE(!tmp.path().empty());
    std::string name = soundtest::putZip("double.zip", tmp.path(), "double.zip");
    REQUIRE(!name.empty());
    LibState::setZipFolder(tmp.path());
    CSoundLibrary::checkZipFile();
    REQUIRE(CSoundLibrary::getZipNum() == 1);
    CBank* root = CSoundLibrary::getZipRootBank(0);
    CHECK_STR(root->m_name, name);
    CHECK_STR(root->m_innerPath, (name + "/outer/inner"));
    CHECK_EQ(root->m_patchNum, 2);
    CHECK_EQ(root->getChildNum(), 0);
    CHECK(CSoundLibrary::getBank(-1, Buf(name + "/outer/inner")) == root);
}

/** @brief Sub folders of a zip become sorted child banks reachable by inner path. */
TEST(sound_zip, nested_zip_child_banks)
{
    LibState st;
    TempDir tmp;
    REQUIRE(!tmp.path().empty());
    std::string name = soundtest::putZip("nested.zip", tmp.path(), "nested.zip");
    REQUIRE(!name.empty());
    LibState::setZipFolder(tmp.path());
    CSoundLibrary::checkZipFile();
    REQUIRE(CSoundLibrary::getZipNum() == 1);
    CBank* root = CSoundLibrary::getZipRootBank(0);
    CHECK_STR(root->m_innerPath, name);
    CHECK_EQ(root->m_patchNum, 1);
    REQUIRE(root->getChildNum() == 2);
    CBank* a = root->getChild(0);
    CBank* b = root->getChild(1);
    CHECK_STR(a->m_innerPath, (name + "/a"));
    CHECK_STR(b->m_innerPath, (name + "/b"));
    CHECK_EQ(a->m_patchNum, 1);
    CHECK_EQ(b->m_patchNum, 1);
    REQUIRE(b->getChildNum() == 1);
    CBank* deep = b->getChild(0);
    CHECK_STR(deep->m_innerPath, (name + "/b/deep"));
    CHECK_STREQ(deep->m_name, "deep");
    CHECK_EQ(deep->m_patchNum, 2);
    CHECK_EQ(s1g->m_pZipBankMap->size(), 4);
    CHECK(CSoundLibrary::getBank(-1, Buf(name)) == root);
    CHECK(CSoundLibrary::getBank(-1, Buf(name + "/a")) == a);
    CHECK(CSoundLibrary::getBank(-1, Buf(name + "/b/deep")) == deep);
    char pname[64];
    CHECK(CSoundLibrary::getProgramNameColor(-1, Buf(name + "/b/deep"), 1, pname, NULL, 0));
    CHECK_STREQ(pname, "Deep Two");
    CHECK(CSoundLibrary::getProgramNameColor(-1, Buf(name), 4, pname, NULL, 0));
    CHECK_STREQ(pname, "Top Five");
}

/** @brief Several zips give root banks sorted by extraction path; broken zips are skipped. */
TEST(sound_zip, several_zips_sorted_and_broken_skipped)
{
    LibState st;
    TempDir tmp;
    REQUIRE(!tmp.path().empty());
    std::string n1 = soundtest::putZip("flat.zip", tmp.path(), "c.zip");
    std::string n2 = soundtest::putZip("nested.zip", tmp.path(), "a.zip");
    std::string n3 = soundtest::putZip("flat.zip", tmp.path(), "b.zip");
    std::string n4 = soundtest::putZip("corrupt.zip", tmp.path(), "d.zip");
    std::string n5 = soundtest::putZip("empty.zip", tmp.path(), "e.zip");
    REQUIRE(!n1.empty() && !n2.empty() && !n3.empty() && !n4.empty() && !n5.empty());
    REQUIRE(writeFile(tmp.sub("readme.txt"), "not a zip"));
    LibState::setZipFolder(tmp.path());
    CSoundLibrary::checkZipFile();
    CHECK_EQ(t1stub::messageBoxCalls, 2);
    REQUIRE(CSoundLibrary::getZipNum() == 3);
    CHECK_STR(CSoundLibrary::getZipRootBank(0)->m_name, n2);
    CHECK_STR(CSoundLibrary::getZipRootBank(1)->m_name, n3);
    CHECK_STR(CSoundLibrary::getZipRootBank(2)->m_name, n1);
    for (int i = 0; i < 2; i++) {
        CHECK(CSoundLibrary::rootBankComp(CSoundLibrary::getZipRootBank(i),
                                          CSoundLibrary::getZipRootBank(i + 1)));
    }
    CHECK(CSoundLibrary::getBank(-1, Buf(n4)) == NULL);
    bool threw = false;
    try {
        CSoundLibrary::getZipRootBank(3);
    } catch (std::out_of_range&) {
        threw = true;
    }
    CHECK(threw);
}

/** @brief The banks are rebuilt only when the zip folder hash changes. */
TEST(sound_zip, rebuild_only_on_change)
{
    LibState st;
    TempDir tmp;
    REQUIRE(!tmp.path().empty());
    std::string n1 = soundtest::putZip("flat.zip", tmp.path(), "r1.zip");
    REQUIRE(!n1.empty());
    LibState::setZipFolder(tmp.path());
    CSoundLibrary::checkZipFile();
    REQUIRE(CSoundLibrary::getZipNum() == 1);
    CBank* first = CSoundLibrary::getZipRootBank(0);
    first->m_patchNum = 77;  // marker: survives only if nothing is rebuilt
    CSoundLibrary::checkZipFile();
    CHECK(CSoundLibrary::getZipRootBank(0) == first);
    CHECK_EQ(CSoundLibrary::getZipRootBank(0)->m_patchNum, 77);

    std::string n2 = soundtest::putZip("nested.zip", tmp.path(), "r2.zip");
    REQUIRE(!n2.empty());
    CSoundLibrary::checkZipFile();
    REQUIRE(CSoundLibrary::getZipNum() == 2);
    CHECK_EQ(CSoundLibrary::getZipRootBank(0)->m_patchNum, 3);  // rebuilt

    // removing every zip clears the banks
    REQUIRE(remove(tmp.sub(n1).c_str()) == 0);
    REQUIRE(remove(tmp.sub(n2).c_str()) == 0);
    CSoundLibrary::checkZipFile();
    CHECK_EQ(CSoundLibrary::getZipNum(), 0);
    CHECK_EQ(s1g->m_pZipBankMap->size(), 0);
}

/** @brief While locked, checkZipFile does nothing; locks nest. */
TEST(sound_zip, lock_blocks_rescan)
{
    LibState st;
    TempDir tmp;
    REQUIRE(!tmp.path().empty());
    LibState::setZipFolder(tmp.path());
    CSoundLibrary::lockZipBank();
    CSoundLibrary::lockZipBank();
    CHECK_EQ(s1g->m_zipBankLock, 2);
    REQUIRE(!soundtest::putZip("flat.zip", tmp.path(), "lock.zip").empty());
    CSoundLibrary::checkZipFile();
    CHECK_EQ(CSoundLibrary::getZipNum(), 0);
    CSoundLibrary::unlockZipBank();
    CSoundLibrary::checkZipFile();
    CHECK_EQ(CSoundLibrary::getZipNum(), 0);
    CSoundLibrary::unlockZipBank();
    CHECK_EQ(s1g->m_zipBankLock, 0);
    CSoundLibrary::checkZipFile();
    CHECK_EQ(CSoundLibrary::getZipNum(), 1);
}

/**
 * @brief Upper-case ".ZIP" files are ignored: not hashed, and not loaded either, because
 *        endWith() is documented as case insensitive but the CharLower() emulation it relies on
 *        does not change the string (characterization).
 */
TEST(sound_zip, upper_case_extension)
{
    LibState st;
    TempDir tmp;
    REQUIRE(!tmp.path().empty());
    std::string up = soundtest::putZip("flat.zip", tmp.path(), "UP.ZIP");
    REQUIRE(!up.empty());
    LibState::setZipFolder(tmp.path());
    CSoundLibrary::checkZipFile();
    CHECK_EQ(CSoundLibrary::getZipNum(), 0);
    REQUIRE(!soundtest::putZip("double.zip", tmp.path(), "low.zip").empty());
    CSoundLibrary::checkZipFile();
    REQUIRE(CSoundLibrary::getZipNum() == 1);
    CHECK_STR(CSoundLibrary::getZipRootBank(0)->m_name, soundtest::zipPrefix() + "low.zip");
    CHECK(CSoundLibrary::getBank(-1, Buf(up)) == NULL);
    CHECK(!endWith(Buf("BANK.ZIP"), Buf(".zip")));
    CHECK(endWith(Buf("bank.zip"), Buf(".zip")));
}

/** @brief updateZipFolderPath switches folders and rescans; the same path is a no-op. */
TEST(sound_zip, update_zip_folder_path)
{
    LibState st;
    TempDir tmp;
    REQUIRE(!tmp.path().empty());
    std::string f1 = tmp.sub("one");
    std::string f2 = tmp.sub("two");
    REQUIRE(soundtest::makeDir(f1));
    REQUIRE(soundtest::makeDir(f2));
    REQUIRE(!soundtest::putZip("flat.zip", f1, "u1.zip").empty());
    REQUIRE(!soundtest::putZip("flat.zip", f2, "u2.zip").empty());
    REQUIRE(!soundtest::putZip("nested.zip", f2, "u3.zip").empty());
    CSoundLibrary::updateZipFolderPath(Buf(f1));
    CHECK_STR(CSoundLibrary::getZipFolderPath(), f1);
    CHECK_EQ(CSoundLibrary::getZipNum(), 1);
    CBank* b = CSoundLibrary::getZipRootBank(0);
    s1g->m_zipFilesHash = 1;  // would force a rebuild if a scan ran
    CSoundLibrary::updateZipFolderPath(Buf(f1));
    CHECK(CSoundLibrary::getZipRootBank(0) == b);
    CHECK_EQ(s1g->m_zipFilesHash, 1);
    s1g->m_zipCheckTime = 5;
    CSoundLibrary::updateZipFolderPath(Buf(f2));
    CHECK_STR(CSoundLibrary::getZipFolderPath(), f2);
    CHECK_EQ(s1g->m_zipCheckTime, 0);
    CHECK_EQ(CSoundLibrary::getZipNum(), 2);
}

// ---------------------------------------------------------------------------
// zip error paths

/** @brief The zip builder's archives extract with unzip() (control for the error tests below). */
TEST(sound_zip, built_zip_extracts)
{
    LibState st;
    TempDir tmp;
    REQUIRE(!tmp.path().empty());
    std::string names[2] = {"001.sy1", "sub/002.sy1"};
    std::string data[2] = {"One\ncolor=red\n", "Two\ncolor=blue\n"};
    REQUIRE(writeFile(tmp.sub("ok.zip"), buildZip(names, data, 2, 0, "")));
    std::string out = tmp.sub("out");
    REQUIRE(soundtest::makeDir(out));
    CHECK(unzip(Buf(tmp.sub("ok.zip")), Buf(out)));
    CHECK_EQ(t1stub::messageBoxCalls, 0);
    CHECK_STR(soundtest::readFile(out + "/001.sy1"), data[0]);
    CHECK_STR(soundtest::readFile(out + "/sub/002.sy1"), data[1]);
}

/** @brief An archive that opens but has no entries fails at the first entry info (message box). */
TEST(sound_zip, unzip_no_entries_fails)
{
    LibState st;
    TempDir tmp;
    REQUIRE(!tmp.path().empty());
    REQUIRE(writeFile(tmp.sub("none.zip"), buildZip(NULL, NULL, 0, 0, "junk")));
    std::string out = tmp.sub("out");
    REQUIRE(soundtest::makeDir(out));
    CHECK(!unzip(Buf(tmp.sub("none.zip")), Buf(out)));
    CHECK_EQ(t1stub::messageBoxCalls, 1);
}

/** @brief An entry with an unsupported compression method cannot be opened: unzip fails. */
TEST(sound_zip, unzip_unsupported_method_fails)
{
    LibState st;
    TempDir tmp;
    REQUIRE(!tmp.path().empty());
    std::string names[1] = {"001.sy1"};
    std::string data[1] = {"One\n"};
    REQUIRE(writeFile(tmp.sub("m.zip"), buildZip(names, data, 1, 99, "")));
    std::string out = tmp.sub("out");
    REQUIRE(soundtest::makeDir(out));
    CHECK(!unzip(Buf(tmp.sub("m.zip")), Buf(out)));
    CHECK_EQ(t1stub::messageBoxCalls, 1);
    CHECK(!soundtest::fileExists(out + "/001.sy1"));
}

/** @brief An entry whose output path cannot be opened for writing (a folder is there) fails. */
TEST(sound_zip, unzip_unwritable_output_fails)
{
    LibState st;
    TempDir tmp;
    REQUIRE(!tmp.path().empty());
    std::string names[1] = {"001.sy1"};
    std::string data[1] = {"One\n"};
    REQUIRE(writeFile(tmp.sub("w.zip"), buildZip(names, data, 1, 0, "")));
    std::string out = tmp.sub("out");
    REQUIRE(soundtest::makeDir(out));
    REQUIRE(soundtest::makeDir(out + "/001.sy1"));
    CHECK(!unzip(Buf(tmp.sub("w.zip")), Buf(out)));
    CHECK_EQ(t1stub::messageBoxCalls, 1);
}

/**
 * @brief A short write while extracting (here: the file size limit RLIMIT_FSIZE, with SIGXFSZ
 *        ignored, lowered for the duration of the call) makes unzip fail.
 */
TEST(sound_zip, unzip_short_write_fails)
{
    LibState st;
    TempDir tmp;
    REQUIRE(!tmp.path().empty());
    std::string names[1] = {"big.sy1"};
    std::string data[1] = {std::string(20000, 'x')};
    REQUIRE(writeFile(tmp.sub("big.zip"), buildZip(names, data, 1, 0, "")));
    std::string out = tmp.sub("out");
    REQUIRE(soundtest::makeDir(out));
    struct rlimit saved;
    REQUIRE(getrlimit(RLIMIT_FSIZE, &saved) == 0);
    void (*oldHandler)(int) = signal(SIGXFSZ, SIG_IGN);
    struct rlimit lim = saved;
    lim.rlim_cur = 1000;
    REQUIRE(setrlimit(RLIMIT_FSIZE, &lim) == 0);
    BOOL ok = unzip(Buf(tmp.sub("big.zip")), Buf(out));
    setrlimit(RLIMIT_FSIZE, &saved);
    signal(SIGXFSZ, oldHandler);
    CHECK(!ok);
    CHECK_EQ(t1stub::messageBoxCalls, 1);
    CHECK(soundtest::readFile(out + "/big.sy1").size() <= 1000);
}

/** @brief extractZipBank fails without extracting when its target folder name is taken by a file. */
TEST(sound_zip, extract_zip_bank_target_is_a_file)
{
    LibState st;
    TempDir tmp;
    REQUIRE(!tmp.path().empty());
    std::string name = soundtest::putZip("flat.zip", tmp.path(), "blocked.zip");
    REQUIRE(!name.empty());
    std::string target = extractDirFor(name, 0x77);
    char root[1100];
    strcpy(root, soundtest::zipTempRoot().c_str());
    REQUIRE(myMkDir(root));
    REQUIRE(writeFile(target, "not a folder"));
    char out[1024];
    CHECK(!CBank::extractZipBank(Buf(tmp.sub(name)), out, 0x77));
    CHECK_STR(out, target);
    CHECK_EQ(t1stub::messageBoxCalls, 0);
    CHECK_STR(soundtest::readFile(target), "not a folder");
    CHECK(!soundtest::fileExists(target + "/extractDone"));
}

/** @brief getZipFilesHash skips *.zip entries it cannot stat (a dangling symbolic link). */
TEST(sound_zip, zip_files_hash_skips_dangling_link)
{
    LibState st;
    TempDir tmp;
    REQUIRE(!tmp.path().empty());
    REQUIRE(writeFile(tmp.sub("a.zip"), "12345"));
    int ha = hashOf(tmp.path(), "a.zip");
    REQUIRE(symlink(tmp.sub("gone.zip").c_str(), tmp.sub("link.zip").c_str()) == 0);
    CHECK_EQ(CSoundLibrary::getZipFilesHash(Buf(tmp.path())), ha);
}

/** @brief A rescan resets a zip check time of -1 to 0. */
TEST(sound_zip, rescan_resets_check_time)
{
    LibState st;
    TempDir tmp;
    REQUIRE(!tmp.path().empty());
    std::string name = soundtest::putZip("flat.zip", tmp.path(), "ct.zip");
    REQUIRE(!name.empty());
    LibState::setZipFolder(tmp.path());
    s1g->m_zipCheckTime = -1;
    CSoundLibrary::checkZipFile();
    CHECK_EQ(s1g->m_zipCheckTime, 0);
    CHECK_EQ(CSoundLibrary::getZipNum(), 1);
    s1g->m_zipCheckTime = 1234;
    REQUIRE(writeFile(tmp.sub("x.zip"), "x"));     // hash changes, not -1: kept
    CSoundLibrary::checkZipFile();
    CHECK_EQ(s1g->m_zipCheckTime, 1234);
}

/** @brief When the zip folder disappears, the next scan drops the zip banks it had. */
TEST(sound_zip, vanished_zip_folder_drops_banks)
{
    LibState st;
    TempDir tmp;
    REQUIRE(!tmp.path().empty());
    std::string dir = tmp.sub("zips");
    REQUIRE(soundtest::makeDir(dir));
    std::string name = soundtest::putZip("flat.zip", dir, "gone.zip");
    REQUIRE(!name.empty());
    LibState::setZipFolder(dir);
    CSoundLibrary::checkZipFile();
    REQUIRE(CSoundLibrary::getZipNum() == 1);
    REQUIRE(CSoundLibrary::getBank(-1, Buf(name)) != NULL);
    soundtest::removeTree(dir);
    CSoundLibrary::checkZipFile();
    CHECK_EQ(s1g->m_zipFilesHash, 0);
    CHECK_EQ(CSoundLibrary::getZipNum(), 0);
    CHECK(CSoundLibrary::getBank(-1, Buf(name)) == NULL);
}
