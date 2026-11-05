/**
 * @file test_synth1if.cpp
 * @brief Unit tests for the extern "C" engine interface declared in synth1if.h (defined in
 *        techpop/synth1.cpp) as the Objective-C dialogs use it: engine objects passed as void*.
 *
 * synth1if.h redeclares g_midiAssignParams with a C struct, so this file cannot include
 * synth1.h; the synth and its isolated sound library (SynthEnv) are created through helpers
 * defined in test_synth1.cpp. The
 * functions that write the preferences (setEnableBankSelect, setEnableProgramChange, setCCMap,
 * changeBankFolder, updateZipFolderPath) are not called.
 */
#include "test.h"

#include <math.h>
#include <string.h>

#include "windef.h"
#include "stubs.h"

extern "C" {
#include "synth1if.h"
}

// helpers from test_synth1.cpp
void* t1s_newEnv();
void t1s_deleteEnv(void* p);
void* t1s_newSynth();
void t1s_deleteSynth(void* p);
void* t1s_currentSound(void* p);
int* t1s_ccMap();
char* t1s_zipFolder();
int t1s_masterTune();
float t1s_freq(int nn);
void t1s_setCurrentBankNo(void* p, int bankNo);

/** @brief The current sound accessors return the engine's current patch and its bank,
 *         program, name and colour. */
TEST(synth1if, current_sound)
{
    void* env = t1s_newEnv();
    void* s = t1s_newSynth();
    void* snd = Synthesizer1_getCurrentSound(s);
    CHECK(snd == t1s_currentSound(s));
    CHECK_EQ(Synthesizer1_getCurrentSoundBankNo(s), 0);
    CHECK_EQ(SoundData_getBankNo(snd), 0);
    CHECK_EQ(SoundData_getProgramNo(snd), 0);
    CHECK_STREQ(SoundData_getName(snd), "initial sound");
    CHECK_EQ(SoundData_getColor(snd), 0xffffffffu);
    t1s_setCurrentBankNo(s, -1);
    CHECK_EQ(Synthesizer1_getCurrentSoundBankNo(s), -1);
    CHECK_EQ(SoundData_getBankNo(snd), -1);
    // the name is the patch's own buffer
    CHECK(SoundData_getName(snd) == SoundData_getName(Synthesizer1_getCurrentSound(s)));
    t1s_deleteSynth(s);
    t1s_deleteEnv(env);
}

/** @brief The enable flag getters reflect the process-wide CInst flags (1 by default when the
 *         preferences have no entry). */
TEST(synth1if, enable_flags)
{
    void* env = t1s_newEnv();
    void* s = t1s_newSynth();
    int b = Synthesizer1_getEnableBankSelect(s);
    int p = Synthesizer1_getEnableProgramChange(s);
    CHECK(b == 0 || b == 1);
    CHECK(p == 0 || p == 1);
    CHECK_EQ(b, getIniInt("enableBankSelect", 1));
    CHECK_EQ(p, getIniInt("enableProgramChange", 1));
    t1s_deleteSynth(s);
    t1s_deleteEnv(env);
}

/** @brief The CC map table is the process-wide s1g map; the zip folder path is s1g's. */
TEST(synth1if, process_wide_tables)
{
    void* env = t1s_newEnv();
    void* s = t1s_newSynth();
    CHECK(Synthesizer1_getCCMapTable() == t1s_ccMap());
    CHECK(Synthesizer1_getZipFolderPath(s) == t1s_zipFolder());
    CHECK(strlen(Synthesizer1_getZipFolderPath(s)) > 0);
    t1s_deleteSynth(s);
    t1s_deleteEnv(env);
}

/** @brief The CSV helpers convert a CC map to text and back. */
TEST(synth1if, cc_map_csv)
{
    int map[128], back[128];
    for (int i = 0; i < 128; i++) map[i] = (i * 7) % 99;
    char str[0x400];
    Synthesizer1_createCSVStringFromCCMap(0, map, str);
    CHECK(strncmp(str, "0,7,14,", 7) == 0);
    CHECK_EQ(Synthesizer1_getCCMapFromCSV(0, back, str), 1);
    CHECK(memcmp(map, back, sizeof map) == 0);
    t1stub::reset();
    strcpy(str, "");
    CHECK_EQ(Synthesizer1_getCCMapFromCSV(0, back, str), 0);
    CHECK_EQ(t1stub::messageBoxCalls, 1);
    CHECK_EQ(back[0], -1);
}

/** @brief setMasterTune() rebuilds the note frequency table (A4 = 440 Hz * 2^(cents / 1200)). */
TEST(synth1if, master_tune)
{
    void* env = t1s_newEnv();
    void* s = t1s_newSynth();
    int saved = t1s_masterTune();
    Synthesizer1_setMasterTune(s, 0);
    CHECK_EQ(t1s_masterTune(), 0);
    CHECK_CLOSE(t1s_freq(69), 440.0, 1e-3);
    CHECK_CLOSE(t1s_freq(81), 880.0, 1e-3);
    CHECK_CLOSE(t1s_freq(57), 220.0, 1e-3);
    Synthesizer1_setMasterTune(s, 100);
    CHECK_EQ(t1s_masterTune(), 100);
    CHECK_CLOSE(t1s_freq(69), 440.0 * pow(2.0, 100 / 1200.0), 1e-3);
    CHECK_CLOSE(t1s_freq(68), 440.0, 1e-3);
    Synthesizer1_setMasterTune(s, saved);
    t1s_deleteSynth(s);
    t1s_deleteEnv(env);
}

/** @brief getProgramName() reports "not found" for a zip bank that does not exist. */
TEST(synth1if, program_name_of_missing_bank)
{
    void* env = t1s_newEnv();
    void* s = t1s_newSynth();
    char name[64] = "";
    char path[] = "/nonexistent/synth1test/bank.zip";
    Synthesizer1_getProgramName(s, -1, path, 0, name);
    CHECK_STREQ(name, "not found");
    t1s_deleteSynth(s);
    t1s_deleteEnv(env);
}
