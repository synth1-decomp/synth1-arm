/**
 * @file test_synth1_settings.cpp
 * @brief Unit tests for the CSynthesizer1 settings that are persisted in the preferences:
 *        the bank select / program change switches (techpop/synth1.cpp), the MIDI CC map and
 *        its CSV form (initCCMap, setCCMap, endMidiLearning in techpop/synmidi.cpp), and the
 *        extern "C" wrappers that store settings (Synthesizer1_setEnableBankSelect,
 *        Synthesizer1_setEnableProgramChange, Synthesizer1_setCCMap,
 *        Synthesizer1_changeBankFolder, Synthesizer1_updateZipFolderPath).
 *
 * The preferences are the in-memory store of tests/harness/os_fakes.cpp (prefstest::PrefsScope
 * starts each test with an empty "global" section of "jp.daichi.synth1"). The synths are built
 * on a soundtest::IsolatedLibrary (banks and zip folder in a temporary folder). SettingsState
 * saves and restores the process-wide state these tests change: the CC map, its "not loaded"
 * flag, the install folder and the CInst enable switches.
 */
#include "prefs_fixture.h"

#include "test.h"

#include <string.h>
#include <string>

#include "common.h"
#include "engine.h"
#include "stubs.h"
#include "synth1.h"
#include "sound_fixture.h"

using prefstest::PrefsScope;
using prefstest::stored;
using soundtest::IsolatedLibrary;

// extern "C" wrappers of synth1.cpp (declared in synth1if.h, which cannot be included together
// with synth1.h)
extern "C" {
void Synthesizer1_setEnableBankSelect(CSynthesizer1* pSynth, int b);
void Synthesizer1_setEnableProgramChange(CSynthesizer1* pSynth, int b);
void Synthesizer1_setCCMap(CSynthesizer1* pSynth, int* pMap);
void Synthesizer1_changeBankFolder(CSynthesizer1* pSynth, int no, char* path);
void Synthesizer1_updateZipFolderPath(CSynthesizer1* pSynth, char* path);
int Synthesizer1_getEnableBankSelect(CSynthesizer1* pSynth);
int Synthesizer1_getEnableProgramChange(CSynthesizer1* pSynth);
}

namespace {

/**
 * @brief Saves and restores the process-wide settings state touched by these tests.
 */
class SettingsState {
public:
    SettingsState()
    {
        t1::initEngine();
        memcpy(m_ccMap, s1g->m_ccMap, sizeof m_ccMap);
        memcpy(m_installFolder, s1g->m_installFolder, sizeof m_installFolder);
        m_notLoaded = s1g->m_bCCMapNotLoaded;
        m_bankSelect = CInst::s_bEnableBankSelect;
        m_progChange = CInst::s_bEnableProgChange;
    }
    ~SettingsState()
    {
        memcpy(s1g->m_ccMap, m_ccMap, sizeof m_ccMap);
        memcpy(s1g->m_installFolder, m_installFolder, sizeof m_installFolder);
        s1g->m_bCCMapNotLoaded = m_notLoaded;
        CInst::s_bEnableBankSelect = m_bankSelect;
        CInst::s_bEnableProgChange = m_progChange;
    }

private:
    int m_ccMap[128];
    char m_installFolder[0x400];
    int m_notLoaded;
    int m_bankSelect;
    int m_progChange;
};

/**
 * @brief Everything a settings test needs, in construction order: saved globals, clean
 *        preferences and an isolated sound library.
 */
struct Env {
    SettingsState state;
    PrefsScope prefs;
    IsolatedLibrary lib;
};

/**
 * @brief Builds a synth at 44.1 kHz with no GUI window.
 */
CSynthesizer1* newSynth()
{
    CSynthesizer1* s = new CSynthesizer1();
    s->setSamplingFreq(44100);
    return s;
}

/**
 * @brief The CSV text createCSVStringFromCCMap() makes for a map.
 */
std::string csvOf(int* map)
{
    char str[0x400];
    CSynthesizer1::createCSVStringFromCCMap(map, str);
    return str;
}

/**
 * @brief A recognisable CC map: CC i -> parameter (i * 3) % 100, every 5th CC unmapped.
 */
void patternMap(int* map)
{
    for (int i = 0; i < 128; i++) map[i] = (i % 5 == 0) ? -1 : (i * 3) % 100;
}

/**
 * @brief Dummy non-NULL GUI window handle (the stubs only count the calls).
 */
void* fakeWindow()
{
    static char w;
    return &w;
}

}  // namespace

// ---------------------------------------------------------------------------
// bank select / program change switches

/** @brief setEnableBankSelect switches the process-wide flag and stores "enableBankSelect";
 *         a new synth's constructor reads the stored value back. */
TEST(synth1_settings, enable_bank_select_persists)
{
    Env env;
    CSynthesizer1* a = newSynth();
    a->setEnableBankSelect(0);
    CHECK_EQ(a->getEnableBankSelect(), 0);
    CHECK_STR(stored("enableBankSelect"), "0");
    CHECK_EQ(getIniInt("enableBankSelect", 1), 0);
    a->enableBankSelect(1);  // not persisted
    CHECK_STR(stored("enableBankSelect"), "0");
    CSynthesizer1* b = newSynth();
    CHECK_EQ(b->getEnableBankSelect(), 0);
    CHECK_EQ(a->getEnableBankSelect(), 0);  // the flag is shared by all instances
    a->setEnableBankSelect(1);
    CHECK_STR(stored("enableBankSelect"), "1");
    CSynthesizer1* c = newSynth();
    CHECK_EQ(c->getEnableBankSelect(), 1);
    CHECK_EQ(t1fake::setCalls, 2);
    CHECK_EQ(t1fake::syncCalls, 2);
    CHECK_STR(stored("enableProgramChange"), "<unset>");
    delete c;
    delete b;
    delete a;
}

/** @brief setEnableProgramChange switches the process-wide flag and stores
 *         "enableProgramChange"; a new synth's constructor reads it back. */
TEST(synth1_settings, enable_program_change_persists)
{
    Env env;
    CSynthesizer1* a = newSynth();
    a->setEnableProgramChange(0);
    CHECK_EQ(a->getEnableProgramChange(), 0);
    CHECK_STR(stored("enableProgramChange"), "0");
    a->enableProgramChange(1);  // not persisted
    CSynthesizer1* b = newSynth();
    CHECK_EQ(b->getEnableProgramChange(), 0);
    a->setEnableProgramChange(1);
    CHECK_STR(stored("enableProgramChange"), "1");
    CSynthesizer1* c = newSynth();
    CHECK_EQ(c->getEnableProgramChange(), 1);
    CHECK_STR(stored("enableBankSelect"), "<unset>");
    delete c;
    delete b;
    delete a;
}

/** @brief Without stored values the constructor enables both switches (default 1); stored
 *         values (any nonzero number) are taken as they are. */
TEST(synth1_settings, constructor_reads_switches)
{
    Env env;
    CInst::s_bEnableBankSelect = 0;
    CInst::s_bEnableProgChange = 0;
    CSynthesizer1* a = newSynth();
    CHECK_EQ(a->getEnableBankSelect(), 1);
    CHECK_EQ(a->getEnableProgramChange(), 1);
    prefstest::seed("enableBankSelect", "0");
    prefstest::seed("enableProgramChange", "2");
    CSynthesizer1* b = newSynth();
    CHECK_EQ(b->getEnableBankSelect(), 0);
    CHECK(b->getEnableProgramChange() != 0);
    // the constructor only reads
    CHECK_EQ(t1fake::setCalls, 0);
    delete b;
    delete a;
}

/** @brief The C wrappers Synthesizer1_setEnableBankSelect / _setEnableProgramChange persist the
 *         switches like the member functions. */
TEST(synth1_settings, enable_wrappers)
{
    Env env;
    CSynthesizer1* s = newSynth();
    Synthesizer1_setEnableBankSelect(s, 0);
    Synthesizer1_setEnableProgramChange(s, 0);
    CHECK_EQ(Synthesizer1_getEnableBankSelect(s), 0);
    CHECK_EQ(Synthesizer1_getEnableProgramChange(s), 0);
    CHECK_STR(stored("enableBankSelect"), "0");
    CHECK_STR(stored("enableProgramChange"), "0");
    Synthesizer1_setEnableBankSelect(s, 1);
    CHECK_STR(stored("enableBankSelect"), "1");
    CHECK_STR(stored("enableProgramChange"), "0");
    CHECK_EQ(Synthesizer1_getEnableBankSelect(s), 1);
    CHECK_EQ(t1fake::setCalls, 3);
    delete s;
}

// ---------------------------------------------------------------------------
// CC map

/** @brief setCCMap copies the map into s1g and stores its CSV form under "CCMap". */
TEST(synth1_settings, set_cc_map_stores_csv)
{
    Env env;
    int map[128];
    patternMap(map);
    CSynthesizer1::setCCMap(map);
    CHECK(memcmp(s1g->m_ccMap, map, sizeof map) == 0);
    std::string csv = stored("CCMap");
    CHECK_STR(csv, csvOf(map));
    CHECK_EQ(csv.compare(0, 13, "-1,3,6,9,12,-"), 0);
    CHECK_STR(csv.substr(csv.size() - 6), ",78,81");
    int commas = 0;
    for (unsigned int i = 0; i < csv.size(); i++) commas += csv[i] == ',';
    CHECK_EQ(commas, 127);
    CHECK_EQ(t1fake::setCalls, 1);
    CHECK_EQ(t1fake::syncCalls, 1);
}

/** @brief Synthesizer1_setCCMap persists the map like setCCMap. */
TEST(synth1_settings, set_cc_map_wrapper)
{
    Env env;
    CSynthesizer1* s = newSynth();
    int map[128];
    for (int i = 0; i < 128; i++) map[i] = 127 - i;
    Synthesizer1_setCCMap(s, map);
    CHECK(memcmp(s1g->m_ccMap, map, sizeof map) == 0);
    CHECK_STR(stored("CCMap"), csvOf(map));
    delete s;
}

/** @brief initCCMap loads a valid stored CC map and the install folder (once: it does nothing
 *         while the map is marked as loaded). */
TEST(synth1_settings, init_cc_map_loads_stored_map)
{
    Env env;
    int map[128];
    patternMap(map);
    std::string csv = csvOf(map);
    prefstest::seed("CCMap", csv.c_str());
    prefstest::seed("installfolder", "/Library/Audio/Plug-Ins/Components");
    memset(s1g->m_ccMap, 0, sizeof s1g->m_ccMap);
    s1g->m_bCCMapNotLoaded = 1;
    t1stub::reset();
    CSynthesizer1::initCCMap();
    CHECK_EQ(s1g->m_bCCMapNotLoaded, 0);
    CHECK(memcmp(s1g->m_ccMap, map, sizeof map) == 0);
    CHECK_STREQ(s1g->m_installFolder, "/Library/Audio/Plug-Ins/Components");
    CHECK_EQ(t1stub::messageBoxCalls, 0);
    // loaded: a second call ignores the preferences
    int other[128];
    for (int i = 0; i < 128; i++) other[i] = 1;
    std::string csv2 = csvOf(other);
    prefstest::seed("CCMap", csv2.c_str());
    CSynthesizer1::initCCMap();
    CHECK(memcmp(s1g->m_ccMap, map, sizeof map) == 0);
    CHECK_EQ(t1fake::setCalls, 0);
}

/** @brief Stored values outside 0..255 load as -1 (unmapped); whitespace also separates. */
TEST(synth1_settings, init_cc_map_out_of_range_values)
{
    Env env;
    std::string csv = "300,-5\t255 7";
    for (int i = 4; i < 128; i++) csv += ",1";
    prefstest::seed("CCMap", csv.c_str());
    s1g->m_bCCMapNotLoaded = 1;
    t1stub::reset();
    CSynthesizer1::initCCMap();
    CHECK_EQ(s1g->m_ccMap[0], -1);
    CHECK_EQ(s1g->m_ccMap[1], -1);
    CHECK_EQ(s1g->m_ccMap[2], 255);
    CHECK_EQ(s1g->m_ccMap[3], 7);
    CHECK_EQ(s1g->m_ccMap[127], 1);
    CHECK_EQ(t1stub::messageBoxCalls, 0);
}

/** @brief An invalid stored CC map (fewer than 128 values) shows the format error and falls
 *         back to the default map; the stored text is left as it is. */
TEST(synth1_settings, init_cc_map_invalid_falls_back_to_default)
{
    Env env;
    prefstest::seed("CCMap", "1,2,3");
    memset(s1g->m_ccMap, 0, sizeof s1g->m_ccMap);
    s1g->m_bCCMapNotLoaded = 1;
    t1stub::reset();
    CSynthesizer1::initCCMap();
    int def[128];
    CSynthesizer1::getDefaultCcMap(def);
    CHECK(memcmp(s1g->m_ccMap, def, sizeof def) == 0);
    CHECK_EQ(t1stub::messageBoxCalls, 1);
    CHECK_EQ(s1g->m_bCCMapNotLoaded, 0);
    CHECK_STR(stored("CCMap"), "1,2,3");
    CHECK_EQ(t1fake::setCalls, 0);
}

/** @brief Without a stored CC map initCCMap uses the default map silently and the install
 *         folder defaults to "c:\". */
TEST(synth1_settings, init_cc_map_missing_uses_default)
{
    Env env;
    memset(s1g->m_ccMap, 0, sizeof s1g->m_ccMap);
    s1g->m_bCCMapNotLoaded = 1;
    t1stub::reset();
    CSynthesizer1::initCCMap();
    int def[128];
    CSynthesizer1::getDefaultCcMap(def);
    CHECK(memcmp(s1g->m_ccMap, def, sizeof def) == 0);
    CHECK_EQ(t1stub::messageBoxCalls, 0);
    CHECK_STREQ(s1g->m_installFolder, "c:\\");
    // an empty stored map counts as missing
    prefstest::seed("CCMap", "");
    memset(s1g->m_ccMap, 0, sizeof s1g->m_ccMap);
    s1g->m_bCCMapNotLoaded = 1;
    CSynthesizer1::initCCMap();
    CHECK(memcmp(s1g->m_ccMap, def, sizeof def) == 0);
    CHECK_EQ(t1stub::messageBoxCalls, 0);
}

/** @brief A map stored by setCCMap is what the next initCCMap (e.g. in a new process) loads. */
TEST(synth1_settings, cc_map_round_trip)
{
    Env env;
    int map[128];
    for (int i = 0; i < 128; i++) map[i] = (i * 37) % 256;
    map[5] = -1;
    CSynthesizer1::setCCMap(map);
    memset(s1g->m_ccMap, 0, sizeof s1g->m_ccMap);
    s1g->m_bCCMapNotLoaded = 1;
    t1stub::reset();
    CSynthesizer1::initCCMap();
    CHECK(memcmp(s1g->m_ccMap, map, sizeof map) == 0);
    CHECK_EQ(t1stub::messageBoxCalls, 0);
    // a new synth's constructor does the same
    memset(s1g->m_ccMap, 0, sizeof s1g->m_ccMap);
    s1g->m_bCCMapNotLoaded = 1;
    CSynthesizer1* s = newSynth();
    CHECK(memcmp(s1g->m_ccMap, map, sizeof map) == 0);
    delete s;
}

/** @brief endMidiLearning while learning leaves learn mode, stores the CC map (a CC received
 *         while learning moved the parameter to that CC) and returns TRUE. */
TEST(synth1_settings, end_midi_learning_stores_map)
{
    Env env;
    CSynthesizer1* s = newSynth();
    int def[128];
    CSynthesizer1::getDefaultCcMap(def);
    CSynthesizer1::setCCMap(def);
    int oldCC = -1;
    for (int i = 0; i < 128; i++) {
        if (def[i] == 48) { oldCC = i; break; }  // lfo2 speed
    }
    int newCC = 102;
    REQUIRE(oldCC != newCC);
    REQUIRE(def[newCC] != 48);
    t1fake::resetPrefs();
    prefstest::seedSection();
    CHECK_EQ(s->startMidiLearning(), 1);
    s->m_midiLearnParamID = 48;
    s->processMIDIShortDataIndividual(0xB0, (unsigned char)newCC, 100);
    CHECK_EQ(s1g->m_ccMap[newCC], 48);
    if (oldCC >= 0) CHECK_EQ(s1g->m_ccMap[oldCC], -1);
    CHECK_STR(stored("CCMap"), "<unset>");  // learning itself does not store
    CHECK_EQ(s->endMidiLearning(), 1);
    CHECK_EQ(s->m_bMidiLearning, 0);
    CHECK_EQ(s->m_midiLearnParamID, -1);
    CHECK_STR(stored("CCMap"), csvOf(s1g->m_ccMap));
    CHECK_EQ(t1fake::setCalls, 1);
    // reload: the learned assignment survives
    s1g->m_bCCMapNotLoaded = 1;
    memset(s1g->m_ccMap, 0, sizeof s1g->m_ccMap);
    CSynthesizer1::initCCMap();
    CHECK_EQ(s1g->m_ccMap[newCC], 48);
    delete s;
}

/** @brief endMidiLearning when not learning stores nothing and returns FALSE; with a GUI window
 *         ending learn mode shows a message box. */
TEST(synth1_settings, end_midi_learning_not_learning_and_window)
{
    Env env;
    CSynthesizer1* s = newSynth();
    CHECK_EQ(s->endMidiLearning(), 0);
    CHECK_STR(stored("CCMap"), "<unset>");
    CHECK_EQ(t1fake::setCalls, 0);
    CHECK_EQ(t1fake::syncCalls, 0);
    s->setSynWin(fakeWindow());
    t1stub::reset();
    t1stub::sandBox = 1;
    CHECK_EQ(s->startMidiLearning(), 1);
    CHECK_EQ(t1stub::messageBoxCalls, 1);
    CHECK_EQ(s->endMidiLearning(), 1);
    CHECK_EQ(t1stub::messageBoxCalls, 2);
    CHECK_STR(stored("CCMap"), csvOf(s1g->m_ccMap));
    CHECK_EQ(s->endMidiLearning(), 0);
    CHECK_EQ(t1stub::messageBoxCalls, 2);
    CHECK_EQ(t1fake::setCalls, 1);
    s->setSynWin(NULL);
    delete s;
}

// ---------------------------------------------------------------------------
// bank folder and zip folder wrappers

/** @brief Synthesizer1_changeBankFolder repoints the bank and, outside the sandbox, stores
 *         "bankfolder<N>"; in the sandbox nothing is stored. */
TEST(synth1_settings, change_bank_folder_wrapper)
{
    Env env;
    CSynthesizer1* s = newSynth();
    std::string p3 = env.lib.tmp.sub("other3");
    REQUIRE(soundtest::makeDir(p3));
    t1stub::sandBox = 0;
    Synthesizer1_changeBankFolder(s, 3, soundtest::Buf(p3));
    CHECK_STR(s1g->m_banks[3].m_path, p3);
    CHECK_STR(stored("bankfolder3"), p3);
    CHECK_EQ(t1fake::setCalls, 1);
    CHECK_EQ(s->m_soundLib.changeBankFolder(4, soundtest::Buf(p3 + "x")), 1);
    CHECK_STR(stored("bankfolder4"), p3 + "x");
    CHECK_STR(s1g->m_banks[4].m_path, p3 + "x");
    // sandbox: the bank changes, the preferences do not
    t1stub::sandBox = 1;
    std::string p5 = env.lib.tmp.sub("other5");
    Synthesizer1_changeBankFolder(s, 5, soundtest::Buf(p5));
    CHECK_STR(s1g->m_banks[5].m_path, p5);
    CHECK_STR(stored("bankfolder5"), "<unset>");
    CHECK_EQ(t1fake::setCalls, 2);
    delete s;
}

/** @brief Synthesizer1_updateZipFolderPath switches the zip folder, rescans it and, outside the
 *         sandbox, stores "zipfolder"; the same path again does nothing. */
TEST(synth1_settings, update_zip_folder_wrapper)
{
    Env env;
    CSynthesizer1* s = newSynth();
    std::string zdir = env.lib.tmp.sub("zips");
    REQUIRE(soundtest::makeDir(zdir));
    REQUIRE(!soundtest::putZip("flat.zip", zdir, "upd.zip").empty());
    CHECK_EQ(CSoundLibrary::getZipNum(), 0);
    t1stub::sandBox = 0;
    Synthesizer1_updateZipFolderPath(s, soundtest::Buf(zdir));
    CHECK_STR(s1g->m_zipFolder, zdir);
    CHECK_STR(stored("zipfolder"), zdir);
    CHECK_EQ(CSoundLibrary::getZipNum(), 1);
    CHECK_EQ(t1fake::setCalls, 1);
    // unchanged path: no write, no rescan
    Synthesizer1_updateZipFolderPath(s, soundtest::Buf(zdir));
    CHECK_EQ(t1fake::setCalls, 1);
    CHECK_EQ(t1fake::syncCalls, 1);
    // sandbox: switched and rescanned but not stored
    t1stub::sandBox = 1;
    std::string empty = env.lib.tmp.sub("zip");
    Synthesizer1_updateZipFolderPath(s, soundtest::Buf(empty));
    CHECK_STR(s1g->m_zipFolder, empty);
    CHECK_STR(stored("zipfolder"), zdir);
    CHECK_EQ(CSoundLibrary::getZipNum(), 0);
    CHECK_EQ(t1fake::setCalls, 1);
    delete s;
}
