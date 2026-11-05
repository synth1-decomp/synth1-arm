/**
 * @file test_sound.cpp
 * @brief Unit tests for techpop/sound.cpp, part 1: CSoundData, the line reader, CPatch,
 *        CBank (folder scanning, patch numbering, child banks) and the extern "C" helpers.
 *
 * Fixtures are created at test time in a mkdtemp folder under /tmp.
 */
#include "sound_fixture.h"

using soundtest::Buf;
using soundtest::TempDir;
using soundtest::writeFile;

#define FIELD_OFFSET(obj, m) ((int)((char*)&(obj).m - (char*)&(obj)))

// ---------------------------------------------------------------------------
// layout

/** @brief The class sizes and member offsets match the documented binary layout. */
TEST(sound_layout, sizes_and_offsets)
{
    CHECK_EQ(sizeof(CSoundData), 0xb60);
    CHECK_EQ(sizeof(CPatch), 0x48);
    CHECK_EQ(sizeof(CBank), 0x3940);
    CHECK_EQ(sizeof(CSoundLibrary), 0x1368);
    CHECK_EQ(sizeof(WriteProgramInfo), 0x2c);

    CSoundData sd;
    CHECK_EQ(FIELD_OFFSET(sd, m_name), 0x008);
    CHECK_EQ(FIELD_OFFSET(sd, m_color), 0x028);
    CHECK_EQ(FIELD_OFFSET(sd, m_paramID), 0x02c);
    CHECK_EQ(FIELD_OFFSET(sd, m_paramVal), 0x1bc);
    CHECK_EQ(FIELD_OFFSET(sd, m_paramNum), 0x34c);
    CHECK_EQ(FIELD_OFFSET(sd, m_bankNo), 0x350);
    CHECK_EQ(FIELD_OFFSET(sd, m_path), 0x354);
    CHECK_EQ(FIELD_OFFSET(sd, m_progNo), 0xb54);
    CHECK_EQ(FIELD_OFFSET(sd, m_version), 0xb58);
    // loadProgram copies m_name .. m_version (inclusive)
    CHECK_EQ(FIELD_OFFSET(sd, m_version) + 4 - FIELD_OFFSET(sd, m_name), SOUNDDATA_BODY_SIZE);

    CBank* b = new CBank;
    CHECK_EQ(FIELD_OFFSET(*b, m_bankNo), 0x0008);
    CHECK_EQ(FIELD_OFFSET(*b, m_patchNum), 0x000c);
    CHECK_EQ(FIELD_OFFSET(*b, m_name), 0x0010);
    CHECK_EQ(FIELD_OFFSET(*b, m_path), 0x0110);
    CHECK_EQ(FIELD_OFFSET(*b, m_readmePath), 0x0510);
    CHECK_EQ(FIELD_OFFSET(*b, m_zipPath), 0x0910);
    CHECK_EQ(FIELD_OFFSET(*b, m_innerPath), 0x0d10);
    CHECK_EQ(FIELD_OFFSET(*b, m_bNotOwnChildren), 0x1510);
    CHECK_EQ(FIELD_OFFSET(*b, m_patch), 0x1518);
    CHECK_EQ(FIELD_OFFSET(*b, m_checkTime), 0x3918);
    CHECK_EQ(FIELD_OFFSET(*b, m_bExist), 0x3920);
    CHECK_EQ(FIELD_OFFSET(*b, m_children), 0x3928);
    CHECK_EQ(FIELD_OFFSET(b->m_patch[0], m_progNo), 0x08);
    CHECK_EQ(FIELD_OFFSET(b->m_patch[0], m_pBank), 0x10);
    CHECK_EQ(FIELD_OFFSET(b->m_patch[0], m_name), 0x18);
    CHECK_EQ(FIELD_OFFSET(b->m_patch[0], m_color), 0x38);
    CHECK_EQ(FIELD_OFFSET(b->m_patch[0], m_ts), 0x3c);
    delete b;
}

// ---------------------------------------------------------------------------
// CSoundData

/** @brief A new CSoundData is empty: name "not defined", no parameters, all slots -1/0. */
TEST(sounddata, constructor_defaults)
{
    CSoundData sd;
    CHECK_STREQ(sd.m_name, "not defined");
    CHECK_EQ(sd.getParamsNum(), 0);
    CHECK_EQ(sd.m_paramNum, 0);
    bool allEmpty = true;
    for (int i = 0; i < SOUNDDATA_PARAMS_MAX; i++) {
        if (sd.m_paramID[i] != -1 || sd.m_paramVal[i] != 0) allEmpty = false;
    }
    CHECK(allEmpty);
    CHECK_EQ(sd.getParamVal(0), 0);
    CHECK_EQ(sd.getParamVal(42), 0);
}

/** @brief setParamVal appends new ids in order and overwrites existing ones in place. */
TEST(sounddata, set_get_and_enum)
{
    CSoundData sd;
    sd.setParamVal(10, 100);
    sd.setParamVal(3, -5);
    sd.setParamVal(77, 0);
    CHECK_EQ(sd.getParamsNum(), 3);
    CHECK_EQ(sd.getParamVal(10), 100);
    CHECK_EQ(sd.getParamVal(3), -5);
    CHECK_EQ(sd.getParamVal(77), 0);
    CHECK_EQ(sd.getParamVal(4), 0);

    sd.setParamVal(3, 64);  // overwrite keeps the slot
    CHECK_EQ(sd.getParamsNum(), 3);
    CHECK_EQ(sd.getParamVal(3), 64);

    int id = 0, val = 0;
    sd.enumParam(0, &id, &val);
    CHECK_EQ(id, 10);
    CHECK_EQ(val, 100);
    sd.enumParam(1, &id, &val);
    CHECK_EQ(id, 3);
    CHECK_EQ(val, 64);
    sd.enumParam(2, &id, &val);
    CHECK_EQ(id, 77);
    CHECK_EQ(val, 0);
    sd.enumParam(3, &id, &val);  // first empty slot
    CHECK_EQ(id, -1);
    CHECK_EQ(val, 0);
}

/** @brief Id 0 is an ordinary parameter id. */
TEST(sounddata, id_zero_is_valid)
{
    CSoundData sd;
    sd.setParamVal(0, 9);
    CHECK_EQ(sd.getParamsNum(), 1);
    CHECK_EQ(sd.getParamVal(0), 9);
}

/** @brief After 100 distinct ids, new ids are silently dropped but existing ones still update. */
TEST(sounddata, capacity_is_100)
{
    CSoundData sd;
    for (int i = 0; i < SOUNDDATA_PARAMS_MAX; i++) sd.setParamVal(1000 + i, i);
    CHECK_EQ(sd.getParamsNum(), SOUNDDATA_PARAMS_MAX);
    sd.setParamVal(5000, 1);
    CHECK_EQ(sd.getParamsNum(), SOUNDDATA_PARAMS_MAX);
    CHECK_EQ(sd.getParamVal(5000), 0);
    sd.setParamVal(1099, -1);
    CHECK_EQ(sd.getParamVal(1099), -1);
    CHECK_EQ(sd.getParamVal(1000), 0);
    CHECK_EQ(sd.getParamVal(1050), 50);
}

/**
 * @brief Id -1 matches the first empty slot: the value is stored there but the id stays -1 and
 *        the count does not change, so the value is invisible (characterization).
 */
TEST(sounddata, id_minus_one_hits_empty_slot)
{
    CSoundData sd;
    sd.setParamVal(-1, 55);
    CHECK_EQ(sd.getParamsNum(), 0);
    CHECK_EQ(sd.m_paramID[0], -1);
    CHECK_EQ(sd.m_paramVal[0], 55);
    CHECK_EQ(sd.getParamVal(-1), 0);  // only m_paramNum slots are searched
    sd.setParamVal(4, 8);             // the slot is then taken by a real id
    CHECK_EQ(sd.getParamsNum(), 1);
    CHECK_EQ(sd.getParamVal(4), 8);
}

/** @brief init() empties the list and resets the name but keeps colour, location and version. */
TEST(sounddata, init_keeps_color_location_version)
{
    CSoundData sd;
    strcpy(sd.m_name, "My Patch");
    sd.m_color = RGB(1, 2, 3);
    sd.m_bankNo = 7;
    sd.m_progNo = 42;
    sd.m_version = 113;
    strcpy(sd.m_path, "zip/inner");
    sd.setParamVal(1, 2);
    sd.setParamVal(3, 4);
    sd.init();
    CHECK_STREQ(sd.m_name, "not defined");
    CHECK_EQ(sd.getParamsNum(), 0);
    CHECK_EQ(sd.m_paramID[0], -1);
    CHECK_EQ(sd.m_paramVal[1], 0);
    CHECK_EQ(sd.getParamVal(1), 0);
    CHECK_EQ(sd.m_color, RGB(1, 2, 3));
    CHECK_EQ(sd.m_bankNo, 7);
    CHECK_EQ(sd.m_progNo, 42);
    CHECK_EQ(sd.m_version, 113);
    CHECK_STREQ(sd.m_path, "zip/inner");
}

/** @brief Patch colour names map to the defined colours and back; unknown ones are "default". */
TEST(sounddata, color_names)
{
    CHECK_EQ(getColorFromString(Buf("red")), RGB(255, 0, 0));
    CHECK_EQ(getColorFromString(Buf("green")), RGB(0, 255, 0));
    CHECK_EQ(getColorFromString(Buf("blue")), RGB(0, 0, 255));
    CHECK_EQ(getColorFromString(Buf("yellow")), RGB(255, 255, 0));
    CHECK_EQ(getColorFromString(Buf("magenta")), RGB(255, 0, 255));
    CHECK_EQ(getColorFromString(Buf("cyan")), RGB(0, 255, 255));
    CHECK_EQ(getColorFromString(Buf("default")), 0xffffffffu);
    CHECK_EQ(getColorFromString(Buf("Red")), 0xffffffffu);
    char s[64];
    getColorStringFromVal(s, RGB(0, 0, 255));
    CHECK_STREQ(s, "blue");
    getColorStringFromVal(s, 0xffffffff);
    CHECK_STREQ(s, "default");
    getColorStringFromVal(s, RGB(1, 2, 3));
    CHECK_STREQ(s, "default");
}

// ---------------------------------------------------------------------------
// line reader

/** @brief readLine strips LF and CRLF endings, returns FALSE at EOF and splits long lines. */
TEST(sound_readline, endings_eof_and_chunks)
{
    TempDir tmp;
    REQUIRE(!tmp.path().empty());
    std::string f = tmp.sub("lines.txt");
    REQUIRE(writeFile(f, "abc\ndef\r\nlonger line here\nno newline"));
    void* fp = openFile(Buf(f));
    REQUIRE(fp != NULL);
    char buf[64];
    CHECK(readLine(fp, buf, sizeof(buf)));
    CHECK_STREQ(buf, "abc");
    CHECK(readLine(fp, buf, sizeof(buf)));
    CHECK_STREQ(buf, "def");
    CHECK(readLine(fp, buf, 8));  // 7 characters per piece
    CHECK_STREQ(buf, "longer ");
    CHECK(readLine(fp, buf, 8));
    CHECK_STREQ(buf, "line he");
    CHECK(readLine(fp, buf, 8));
    CHECK_STREQ(buf, "re");
    CHECK(readLine(fp, buf, sizeof(buf)));
    CHECK_STREQ(buf, "no newline");
    CHECK(!readLine(fp, buf, sizeof(buf)));
    closeFile(fp);
    CHECK(openFile(Buf(tmp.sub("missing.txt"))) == NULL);
}

// ---------------------------------------------------------------------------
// CPatch

/** @brief A standalone slot starts "not loaded"; setNoFile/invalidateInfo switch the state. */
TEST(sound_patch, state_machine)
{
    CPatch p;
    CHECK(p.isInfoNotLoaded());
    CHECK(!p.isNoFile());
    p.setNoFile();
    CHECK(!p.isInfoNotLoaded());
    CHECK(p.isNoFile());
    CHECK_EQ(p.m_ts.dwLowDateTime, 1);
    CHECK_EQ(p.m_ts.dwHighDateTime, 1);
    // no file: fixed name/colour without touching the (unset) bank
    CHECK_STREQ(p.getName(), "initial sound");
    CHECK_EQ(p.getColor(), 0xffffffffu);
    p.invalidateInfo();
    CHECK(p.isInfoNotLoaded());
    CHECK_EQ(p.m_ts.dwLowDateTime, 0);
    CHECK_EQ(p.m_ts.dwHighDateTime, 0);
}

/** @brief isSameTS compares both halves of the stored time stamp. */
TEST(sound_patch, is_same_ts)
{
    CPatch p;
    FILETIME t;
    t.dwLowDateTime = 0;
    t.dwHighDateTime = 0;
    CHECK(p.isSameTS(t));
    t.dwHighDateTime = 1;
    CHECK(!p.isSameTS(t));
    p.setNoFile();
    CHECK(!p.isSameTS(t));
    t.dwLowDateTime = 1;
    CHECK(p.isSameTS(t));
}

/** @brief A slot of a bank without a folder reports "initial sound" and the default colour. */
TEST(sound_patch, no_folder_gives_initial_sound)
{
    CBank* b = new CBank;
    CHECK_STREQ(b->m_patch[5].getName(), "initial sound");
    CHECK(b->m_patch[5].isNoFile());
    CHECK_EQ(b->m_patch[6].getColor(), 0xffffffffu);
    delete b;
}

/** @brief The name and colour come from the first two lines of the patch file. */
TEST(sound_patch, loads_name_and_color)
{
    TempDir tmp;
    REQUIRE(!tmp.path().empty());
    REQUIRE(writeFile(tmp.sub("001.sy1"), "Bass Lead\ncolor=magenta\nver=113\n0,1\n"));
    REQUIRE(writeFile(tmp.sub("002.sy1"), "CRLF Pad\r\ncolor=cyan\r\n"));
    REQUIRE(writeFile(tmp.sub("003.sy1"), "No Color\nver=113\n1,2\n"));
    REQUIRE(writeFile(tmp.sub("004.sy1"), "Odd Color\ncolor=purple\n"));
    CBank* b = new CBank;
    b->setPath(Buf(tmp.path()));
    CHECK_STREQ(b->m_patch[0].getName(), "Bass Lead");
    CHECK_EQ(b->m_patch[0].getColor(), RGB(255, 0, 255));
    CHECK_STREQ(b->m_patch[1].getName(), "CRLF Pad");
    CHECK_EQ(b->m_patch[1].getColor(), RGB(0, 255, 255));
    CHECK_STREQ(b->m_patch[2].getName(), "No Color");
    CHECK_EQ(b->m_patch[2].getColor(), 0xffffffffu);
    CHECK_STREQ(b->m_patch[3].getName(), "Odd Color");
    CHECK_EQ(b->m_patch[3].getColor(), 0xffffffffu);
    CHECK_STREQ(b->m_patch[4].getName(), "initial sound");
    delete b;
}

/** @brief A file with only the name line counts as "no file" for the info. */
TEST(sound_patch, single_line_file_is_no_file)
{
    TempDir tmp;
    REQUIRE(!tmp.path().empty());
    REQUIRE(writeFile(tmp.sub("001.sy1"), "Only A Name\n"));
    CBank* b = new CBank;
    b->setPath(Buf(tmp.path()));
    CHECK_EQ(b->m_patchNum, 1);  // the folder scan still counts it
    CHECK_STREQ(b->m_patch[0].getName(), "initial sound");
    CHECK(b->m_patch[0].isNoFile());
    CHECK_EQ(b->m_patch[0].getColor(), 0xffffffffu);
    delete b;
}

/** @brief An empty patch file (no name line) also counts as "no file" for the info. */
TEST(sound_patch, empty_file_is_no_file)
{
    TempDir tmp;
    REQUIRE(!tmp.path().empty());
    REQUIRE(writeFile(tmp.sub("001.sy1"), ""));
    CBank* b = new CBank;
    b->setPath(Buf(tmp.path()));
    CHECK_STREQ(b->m_patch[0].getName(), "initial sound");
    CHECK(b->m_patch[0].isNoFile());
    CHECK_EQ(b->m_patch[0].getColor(), 0xffffffffu);
    delete b;
}

/**
 * @brief Names are cut to 31 characters; the rest of a long name line is then read as the colour
 *        line, so the real colour line is missed (characterization).
 */
TEST(sound_patch, long_name_is_truncated_and_hides_color)
{
    TempDir tmp;
    REQUIRE(!tmp.path().empty());
    REQUIRE(writeFile(tmp.sub("001.sy1"), "ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789\ncolor=red\n"));
    CBank* b = new CBank;
    b->setPath(Buf(tmp.path()));
    CHECK_STREQ(b->m_patch[0].getName(), "ABCDEFGHIJKLMNOPQRSTUVWXYZ01234");
    CHECK_EQ(strlen(b->m_patch[0].getName()), 31);
    CHECK_EQ(b->m_patch[0].getColor(), 0xffffffffu);
    delete b;
}

/** @brief The info time stamp is not updated on success, so the file is reread on every access. */
TEST(sound_patch, info_is_reread_each_time)
{
    TempDir tmp;
    REQUIRE(!tmp.path().empty());
    REQUIRE(writeFile(tmp.sub("001.sy1"), "First\ncolor=red\n"));
    CBank* b = new CBank;
    b->setPath(Buf(tmp.path()));
    CHECK_STREQ(b->m_patch[0].getName(), "First");
    CHECK(b->m_patch[0].isInfoNotLoaded());
    REQUIRE(writeFile(tmp.sub("001.sy1"), "Second\ncolor=blue\n"));
    CHECK_STREQ(b->m_patch[0].getName(), "Second");
    CHECK_EQ(b->m_patch[0].getColor(), RGB(0, 0, 255));
    // once the file is gone the slot falls back to "no file" on the next read
    REQUIRE(remove(tmp.sub("001.sy1").c_str()) == 0);
    CHECK_STREQ(b->m_patch[0].getName(), "initial sound");
    delete b;
}

// ---------------------------------------------------------------------------
// CBank

/** @brief A new bank has no folder, number 0, no patches, and slots linked to it in order. */
TEST(sound_bank, constructor_defaults)
{
    CBank* b = new CBank;
    CHECK_EQ(b->m_bankNo, 0);
    CHECK_EQ(b->m_patchNum, 0);
    CHECK_STREQ(b->m_name, "");
    CHECK_STREQ(b->getPath(), "");
    CHECK_STREQ(b->m_readmePath, "");
    CHECK_STREQ(b->m_zipPath, "");
    CHECK_STREQ(b->m_innerPath, "");
    CHECK(!b->m_bNotOwnChildren);
    CHECK(!b->m_bExist);
    CHECK_EQ(b->m_checkTime, 0);
    CHECK_EQ(b->getChildNum(), 0);
    bool linked = true;
    for (int i = 0; i < BANK_PATCH_NUM; i++) {
        if (b->m_patch[i].m_progNo != i || b->m_patch[i].m_pBank != b) linked = false;
        if (!b->m_patch[i].isInfoNotLoaded()) linked = false;
    }
    CHECK(linked);
    delete b;
}

/** @brief Patch file names map to program numbers as "NNN" - 1; others give -1. */
TEST(sound_bank, prog_no_from_filename)
{
    CHECK_EQ(CBank::getProgNoFromFilename(Buf("001.sy1")), 0);
    CHECK_EQ(CBank::getProgNoFromFilename(Buf("128.sy1")), 127);
    CHECK_EQ(CBank::getProgNoFromFilename(Buf("042")), 41);
    CHECK_EQ(CBank::getProgNoFromFilename(Buf("000.sy1")), -1);
    CHECK_EQ(CBank::getProgNoFromFilename(Buf("999.sy1")), 998);
    CHECK_EQ(CBank::getProgNoFromFilename(Buf("1234.sy1")), 122);
    CHECK_EQ(CBank::getProgNoFromFilename(Buf("12a.sy1")), -1);
    CHECK_EQ(CBank::getProgNoFromFilename(Buf("a01.sy1")), -1);
    CHECK_EQ(CBank::getProgNoFromFilename(Buf("1.sy1")), -1);
}

/** @brief getProgPath builds folder/NNN.sy1 (NNN = progNo + 1), "???.sy1" for -1, fails without folder. */
TEST(sound_bank, prog_path)
{
    char path[1024];
    CBank* b = new CBank;
    strcpy(path, "junk");
    CHECK(!b->getProgPath(0, path));
    CHECK_STREQ(path, "");
    strcpy(b->m_path, "/some/bank");
    CHECK(b->getProgPath(0, path));
    CHECK_STREQ(path, "/some/bank/001.sy1");
    CHECK(b->getProgPath(127, path));
    CHECK_STREQ(path, "/some/bank/128.sy1");
    CHECK(b->getProgPath(9, path));
    CHECK_STREQ(path, "/some/bank/010.sy1");
    CHECK(b->getProgPath(-1, path));
    CHECK_STREQ(path, "/some/bank/???.sy1");
    strcpy(b->m_path, "/some/bank/");
    CHECK(b->getProgPath(1, path));
    CHECK_STREQ(path, "/some/bank/002.sy1");
    delete b;
}

/** @brief setPath sets the name (last folder component), the readme path, and rescans. */
TEST(sound_bank, set_path_name_readme_and_scan)
{
    TempDir tmp;
    REQUIRE(!tmp.path().empty());
    std::string dir = tmp.sub("My Bank");
    REQUIRE(soundtest::makeDir(dir));
    REQUIRE(writeFile(dir + "/001.sy1", "A\ncolor=red\n"));
    REQUIRE(writeFile(dir + "/064.sy1", "B\ncolor=red\n"));
    CBank* b = new CBank;
    b->m_checkTime = 5;
    b->setPath(Buf(dir));
    CHECK_STR(b->getPath(), dir);
    CHECK_STREQ(b->m_name, "My Bank");
    CHECK_STR(b->m_readmePath, (dir + "/readme.txt"));
    CHECK(b->m_bExist);
    CHECK_EQ(b->m_patchNum, 2);
    CHECK_EQ(b->m_checkTime, 0);

    // a trailing slash still gives the folder name
    b->setPath(Buf(dir + "/"));
    CHECK_STREQ(b->m_name, "My Bank");
    CHECK_EQ(b->m_patchNum, 2);
    delete b;
}

/** @brief Only files named 001.sy1 .. 128.sy1 (exact lowercase extension) are counted. */
TEST(sound_bank, scan_counts_only_valid_patch_names)
{
    TempDir tmp;
    REQUIRE(!tmp.path().empty());
    const char* good[] = {"001.sy1", "002.sy1", "050.sy1", "127.sy1", "128.sy1"};
    const char* bad[] = {"000.sy1", "129.sy1", "999.sy1", "01.sy1", "0001.sy1", "abc.sy1",
                         "003.SY1", "001.sy1.bak", "001.sy2", "readme.txt", "x001.sy1"};
    for (int i = 0; i < 5; i++) REQUIRE(writeFile(tmp.sub(good[i]), "N\ncolor=red\n"));
    for (int i = 0; i < 11; i++) REQUIRE(writeFile(tmp.sub(bad[i]), "N\ncolor=red\n"));
    CBank* b = new CBank;
    b->setPath(Buf(tmp.path()));
    CHECK(b->m_bExist);
    CHECK_EQ(b->m_patchNum, 5);
    CHECK(b->m_patch[0].isInfoNotLoaded());
    CHECK(b->m_patch[1].isInfoNotLoaded());
    CHECK(b->m_patch[49].isInfoNotLoaded());
    CHECK(b->m_patch[126].isInfoNotLoaded());
    CHECK(b->m_patch[127].isInfoNotLoaded());
    CHECK(b->m_patch[2].isNoFile());
    CHECK(b->m_patch[98].isNoFile());
    int noFile = 0;
    for (int i = 0; i < BANK_PATCH_NUM; i++) {
        if (b->m_patch[i].isNoFile()) noFile++;
    }
    CHECK_EQ(noFile, BANK_PATCH_NUM - 5);
    CHECK(b->m_patch[2].isNoFile());  // "003.SY1": the extension is case sensitive
    delete b;
}

/** @brief A missing folder gives m_bExist FALSE, no patches and every slot "no file". */
TEST(sound_bank, missing_folder)
{
    TempDir tmp;
    REQUIRE(!tmp.path().empty());
    CBank* b = new CBank;
    b->setPath(Buf(tmp.sub("does-not-exist")));
    CHECK(!b->m_bExist);
    CHECK_EQ(b->m_patchNum, 0);
    CHECK_STREQ(b->m_name, "does-not-exist");
    bool allNoFile = true;
    for (int i = 0; i < BANK_PATCH_NUM; i++) {
        if (!b->m_patch[i].isNoFile()) allNoFile = false;
    }
    CHECK(allNoFile);
    CHECK_STREQ(b->m_patch[0].getName(), "initial sound");
    delete b;
}

/** @brief An existing empty folder exists but holds no patches. */
TEST(sound_bank, empty_folder)
{
    TempDir tmp;
    REQUIRE(!tmp.path().empty());
    CBank* b = new CBank;
    b->setPath(Buf(tmp.path()));
    CHECK(b->m_bExist);
    CHECK_EQ(b->m_patchNum, 0);
    CHECK(b->m_patch[0].isNoFile());
    delete b;
}

/** @brief checkCache picks up added and removed patch files. */
TEST(sound_bank, check_cache_tracks_changes)
{
    TempDir tmp;
    REQUIRE(!tmp.path().empty());
    REQUIRE(writeFile(tmp.sub("003.sy1"), "Three\ncolor=red\n"));
    CBank* b = new CBank;
    b->setPath(Buf(tmp.path()));
    CHECK_EQ(b->m_patchNum, 1);
    CHECK(b->m_patch[0].isNoFile());
    CHECK_STREQ(b->m_patch[0].getName(), "initial sound");

    REQUIRE(writeFile(tmp.sub("001.sy1"), "One\ncolor=green\n"));
    CHECK_STREQ(b->m_patch[0].getName(), "initial sound");  // not seen before a rescan
    b->checkCache();
    CHECK_EQ(b->m_patchNum, 2);
    CHECK(b->m_patch[0].isInfoNotLoaded());
    CHECK_STREQ(b->m_patch[0].getName(), "One");
    CHECK_EQ(b->m_patch[0].getColor(), RGB(0, 255, 0));

    REQUIRE(remove(tmp.sub("003.sy1").c_str()) == 0);
    b->checkCache();
    CHECK_EQ(b->m_patchNum, 1);
    CHECK(b->m_patch[2].isNoFile());

    // m_checkTime -1 is reset to 0 by a scan
    b->m_checkTime = -1;
    b->checkCache();
    CHECK_EQ(b->m_checkTime, 0);
    delete b;
}

/** @brief Child banks are appended in order; with m_bNotOwnChildren set they are not deleted. */
TEST(sound_bank, children_and_ownership)
{
    CBank* parent = new CBank;
    CBank* c1 = new CBank;
    CBank* c2 = new CBank;
    parent->addChildBank(c1);
    parent->addChildBank(c2);
    CHECK_EQ(parent->getChildNum(), 2);
    CHECK(parent->getChild(0) == c1);
    CHECK(parent->getChild(1) == c2);
    parent->m_bNotOwnChildren = TRUE;
    delete parent;
    // the children survive their parent and are still usable
    CHECK_EQ(c1->getChildNum(), 0);
    c1->addChildBank(c2);  // c1 now owns c2
    CHECK_EQ(c1->getChildNum(), 1);
    delete c1;
}

/** @brief bankComp orders by inner path; makeInnerPath appends the folder name to the parent's. */
TEST(sound_bank, inner_path_and_ordering)
{
    CBank* a = new CBank;
    CBank* b = new CBank;
    strcpy(a->m_path, "/x/y/Alpha");
    a->makeInnerPath(Buf("bank.zip"));
    CHECK_STREQ(a->m_innerPath, "bank.zip/Alpha");
    strcpy(b->m_path, "/x/y/beta/");
    b->makeInnerPath(Buf("bank.zip/Sub"));
    CHECK_STREQ(b->m_innerPath, "bank.zip/Sub/beta");
    CHECK(CBank::bankComp(a, b));
    CHECK(!CBank::bankComp(b, a));
    CHECK(!CBank::bankComp(a, a));
    delete a;
    delete b;
}

/**
 * @brief constructZipBank turns sub folders into child banks (recursively, bank number -1,
 *        sorted by inner path) and registers each in the map by inner path; files are ignored.
 */
TEST(sound_bank, construct_zip_bank_from_folders)
{
    TempDir tmp;
    REQUIRE(!tmp.path().empty());
    std::string root = tmp.sub("root");
    REQUIRE(soundtest::makeDir(root));
    REQUIRE(soundtest::makeDir(root + "/zeta"));
    REQUIRE(soundtest::makeDir(root + "/alpha"));
    REQUIRE(soundtest::makeDir(root + "/Mid"));
    REQUIRE(soundtest::makeDir(root + "/alpha/inner"));
    REQUIRE(writeFile(root + "/001.sy1", "Root\ncolor=red\n"));
    REQUIRE(writeFile(root + "/alpha/001.sy1", "A1\ncolor=red\n"));
    REQUIRE(writeFile(root + "/alpha/002.sy1", "A2\ncolor=red\n"));
    REQUIRE(writeFile(root + "/alpha/inner/007.sy1", "I7\ncolor=blue\n"));
    REQUIRE(writeFile(root + "/notes.txt", "x"));

    CBank* bank = new CBank;
    bank->m_bankNo = -1;
    bank->setPath(Buf(root));
    strcpy(bank->m_innerPath, "R.zip");
    BankMap map;
    bank->constructZipBank(Buf(root), &map);

    REQUIRE(bank->getChildNum() == 3);
    // strcmp order: uppercase before lowercase
    CHECK_STREQ(bank->getChild(0)->m_innerPath, "R.zip/Mid");
    CHECK_STREQ(bank->getChild(1)->m_innerPath, "R.zip/alpha");
    CHECK_STREQ(bank->getChild(2)->m_innerPath, "R.zip/zeta");
    CBank* alpha = bank->getChild(1);
    CHECK_EQ(alpha->m_bankNo, -1);
    CHECK_STREQ(alpha->m_name, "alpha");
    CHECK_STR(alpha->getPath(), (root + "/alpha"));
    CHECK_EQ(alpha->m_patchNum, 2);
    CHECK_STREQ(alpha->m_patch[1].getName(), "A2");
    REQUIRE(alpha->getChildNum() == 1);
    CBank* inner = alpha->getChild(0);
    CHECK_STREQ(inner->m_innerPath, "R.zip/alpha/inner");
    CHECK_EQ(inner->m_patchNum, 1);
    CHECK_EQ(inner->m_patch[6].getColor(), RGB(0, 0, 255));
    CHECK_EQ(bank->getChild(0)->getChildNum(), 0);
    CHECK_EQ(bank->m_patchNum, 1);

    CHECK_EQ(map.size(), 4);
    CHECK(map.find("R.zip/alpha") != map.end() && map["R.zip/alpha"] == alpha);
    CHECK(map.find("R.zip/alpha/inner") != map.end() && map["R.zip/alpha/inner"] == inner);
    CHECK(map.find("R.zip/Mid") != map.end());
    CHECK(map.find("R.zip/zeta") != map.end());
    CHECK(map.find("R.zip") == map.end());  // the bank itself is not added
    delete bank;  // deletes the whole tree
}

// ---------------------------------------------------------------------------
// extern "C" helpers

/** @brief Bank_getPatchNum counts the patch files of a folder (0 for a missing folder). */
TEST(sound_capi, bank_get_patch_num)
{
    TempDir tmp;
    REQUIRE(!tmp.path().empty());
    CHECK_EQ(Bank_getPatchNum(Buf(tmp.path())), 0);
    REQUIRE(writeFile(tmp.sub("001.sy1"), "A\ncolor=red\n"));
    REQUIRE(writeFile(tmp.sub("100.sy1"), "B\ncolor=red\n"));
    REQUIRE(writeFile(tmp.sub("200.sy1"), "C\ncolor=red\n"));
    CHECK_EQ(Bank_getPatchNum(Buf(tmp.path())), 2);
    CHECK_EQ(Bank_getPatchNum(Buf(tmp.sub("nope"))), 0);
}

/** @brief SoundLibrary_getBankInfo rescans and reports the count (or "invalid folder") and path. */
TEST(sound_capi, get_bank_info)
{
    soundtest::LibState lib;
    TempDir tmp;
    REQUIRE(!tmp.path().empty());
    std::string dir = tmp.sub("bankA");
    REQUIRE(soundtest::makeDir(dir));
    REQUIRE(writeFile(dir + "/001.sy1", "A\ncolor=red\n"));
    soundtest::LibState::setBank(2, dir);
    REQUIRE(writeFile(dir + "/002.sy1", "B\ncolor=red\n"));  // seen thanks to the rescan
    char num[64], path[1024];
    SoundLibrary_getBankInfo(2, num, path);
    CHECK_STREQ(num, "2");
    CHECK_STR(path, dir);

    soundtest::LibState::setBank(3, tmp.sub("missing"));
    SoundLibrary_getBankInfo(3, num, path);
    CHECK_STREQ(num, "invalid folder");
    CHECK_STR(path, tmp.sub("missing"));

    soundtest::LibState::setBank(4, tmp.path());
    SoundLibrary_getBankInfo(4, num, path);
    CHECK_STREQ(num, "0");
}

/** @brief SoundLibrary_getBankInfo1 formats "NN:name (count)" from the cached count. */
TEST(sound_capi, get_bank_info1)
{
    soundtest::LibState lib;
    TempDir tmp;
    REQUIRE(!tmp.path().empty());
    std::string dir = tmp.sub("Strings");
    REQUIRE(soundtest::makeDir(dir));
    REQUIRE(writeFile(dir + "/001.sy1", "A\ncolor=red\n"));
    REQUIRE(writeFile(dir + "/005.sy1", "B\ncolor=red\n"));
    soundtest::LibState::setBank(5, dir);
    char str[512];
    SoundLibrary_getBankInfo1(5, str);
    CHECK_STREQ(str, "05:Strings (2)");
    REQUIRE(writeFile(dir + "/006.sy1", "C\ncolor=red\n"));
    SoundLibrary_getBankInfo1(5, str);
    CHECK_STREQ(str, "05:Strings (2)");  // no rescan
    s1g->m_banks[5].checkCache();
    SoundLibrary_getBankInfo1(5, str);
    CHECK_STREQ(str, "05:Strings (3)");
}
