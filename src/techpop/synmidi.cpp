/**
 * @file synmidi.cpp
 * @brief CSynthesizer1: MIDI controller handling (CC map, MIDI learn, controller assign).
 *
 * Holds the parameter table g_midiAssignParams (MIDI ranges, default CCs and names) and
 * the CSynthesizer1 members that map MIDI CCs to sound parameters, run MIDI learn mode
 * and drive the two controller assignment slots. The members are documented in synth1.h.
 */

#include <string.h>
#include "common.h"
#include "debug.h"
#include "synth1.h"

// id, min, max, default CC, name, assignable to the wheel slots, short name
MidiAssignParam g_midiAssignParams[] = {
    {0, 0, 3, -1, "osc1 shape", 0, NULL},
    {76, 0, 127, -1, "osc1 detune", 1, "osc1 det"},
    {45, 0, 127, -1, "osc1 FM", 1, NULL},
    {95, 0, 127, -1, "osc1 sub gain", 1, "osc1 sub"},
    {96, 0, 3, -1, "osc1 sub shape", 0, NULL},
    {97, 0, 1, -1, "osc1 sub octave", 0, NULL},
    {1, 1, 4, -1, "osc2 shape", 0, NULL},
    {6, 0, 1, -1, "osc2 sync", 0, NULL},
    {7, 0, 1, -1, "osc2 ring modulation", 0, NULL},
    {4, 0, 1, -1, "osc2 kbd track", 0, NULL},
    {2, 0, 127, -1, "osc2 pitch", 1, NULL},
    {3, 0, 127, -1, "osc2 fine tune", 0, NULL},
    {10, 0, 1, -1, "osc mod env on/off", 0, NULL},
    {12, 0, 127, -1, "osc mod env attack", 1, "osc env A"},
    {13, 0, 127, -1, "osc mod env decay", 1, "osc env D"},
    {11, 0, 127, -1, "osc mod env amount", 1, "osc env amt"},
    {71, 0, 2, -1, "osc mod dest", 0, NULL},
    {9, -24, 24, -1, "osc key shift", 0, NULL},
    {5, 0, 127, -1, "osc mix", 1, NULL},
    {8, 0, 127, -1, "osc pulse width", 1, "osc p/w"},
    {91, 0, 127, -1, "osc phase shift", 1, "osc ph.shift"},
    {72, 0, 127, -1, "osc1,2 fine tune", 0, NULL},
    {14, 0, 4, -1, "filter type", 0, NULL},
    {15, 0, 127, -1, "filter attack", 1, "filter A"},
    {16, 0, 127, -1, "filter decay", 1, "filter D"},
    {17, 0, 127, -1, "filter sustain", 1, "filter S"},
    {18, 0, 127, -1, "filter release", 1, "filter R"},
    {19, 0, 127, -1, "*filter freq", 1, "filter frq"},
    {20, 0, 127, -1, "*filter resonance", 1, "filter res"},
    {21, 0, 127, -1, "filter amount", 1, "filter amt"},
    {23, 0, 127, -1, "filter saturation", 1, "filter sat"},
    {22, 0, 127, -1, "filter kbd track", 1, "filter trk"},
    {24, 0, 1, -1, "filter velocity switch", 0, NULL},
    {25, 0, 127, -1, "amp attack", 1, "amp A"},
    {26, 0, 127, -1, "amp decay", 1, "amp D"},
    {27, 0, 127, -1, "amp sustain", 1, "amp S"},
    {28, 0, 127, -1, "amp release", 1, "amp R"},
    {29, 0, 127, -1, "amp gain", 1, NULL},
    {30, 0, 127, -1, "amp velocity sens", 0, NULL},
    {77, 0, 1, -1, "effect on/off", 0, NULL},
    {78, 0, 9, -1, "effect type", 0, NULL},
    {79, 0, 127, -1, "effect control1", 1, "effect ctl1"},
    {80, 0, 127, -1, "effect control2", 1, "effect ctl2"},
    {81, 0, 127, -1, "effect level/mix", 1, "effect level"},
    {61, 0, 127, -1, "equalizer freq", 1, "eq freq"},
    {62, 0, 127, -1, "equalizer level", 1, "eq level"},
    {63, 0, 127, -1, "equalizer Q", 1, "eq Q"},
    {60, 0, 127, -1, "equalizer tone", 1, "eq tone"},
    {90, 0, 127, -1, "pan", 1, NULL},
    {65, 0, 1, -1, "delay on/off", 0, NULL},
    {82, 0, 2, -1, "delay type", 0, NULL},
    {35, 0, 19, -1, "delay time", 1, NULL},
    {83, 0, 127, -1, "delay time spread", 1, "delay sprd"},
    {36, 0, 127, -1, "delay feedback", 1, "delay fdbk"},
    {98, 0, 127, -1, "delay tone", 1, NULL},
    {37, 0, 127, -1, "delay dry/wet", 1, "delay d/w"},
    {66, 0, 1, -1, "chorus on/off", 0, NULL},
    {64, 1, 4, -1, "chorus type", 0, NULL},
    {52, 0, 127, -1, "chorus delay time", 1, "chorus time"},
    {53, 0, 127, -1, "chorus depth", 1, "chorus depth"},
    {54, 0, 127, -1, "chorus rate", 1, NULL},
    {55, 0, 127, -1, "chorus feedback", 1, "chorus fdbk"},
    {56, 0, 127, -1, "chorus level", 1, NULL},
    {38, 0, 2, -1, "play mode type", 0, NULL},
    {39, 0, 127, -1, "portament time", 1, "porta time"},
    {74, 0, 1, -1, "portament auto mode", 0, NULL},
    {73, 0, 1, -1, "unison mode", 0, NULL},
    {93, 2, 8, -1, "unison voice num", 0, NULL},
    {75, 0, 127, -1, "unison detune", 1, "unison det"},
    {92, 0, 127, -1, "unison phase shift", 1, "unison ph.shift"},
    {84, 0, 127, -1, "unison pan spread", 1, "unison sprd"},
    {85, 0, 48, -1, "unison pitch", 1, NULL},
    {40, 0, 24, -1, "pitch bend range", 0, NULL},
    {86, -1, -1, -2, "midi ctrl src1", 0, NULL},
    {88, -1, -1, -2, "midi ctrl src2", 0, NULL},
    {50, 0, 127, -1, "midi ctrl sens1", 0, NULL},
    {51, 0, 127, -1, "midi ctrl sens2", 0, NULL},
    {87, -1, -1, -2, "midi ctrl assign1", 0, NULL},
    {89, -1, -1, -2, "midi ctrl assign2", 0, NULL},
    {59, 0, 1, -1, "arpeggiator on/off", 0, NULL},
    {31, 1, 4, -1, "arpeggiator type", 0, NULL},
    {32, 0, 3, -1, "arpeggiator oct range", 0, NULL},
    {33, 0, 18, -1, "arpeggiator beat", 0, NULL},
    {34, 0, 127, -1, "arpeggiator gate", 1, "arp gate"},
    {57, 0, 1, -1, "lfo1 on/off", 0, NULL},
    {42, 0, 5, -1, "lfo1 type", 0, NULL},
    {41, 1, 7, -1, "lfo1 destination", 0, NULL},
    {43, 0, 127, -1, "lfo1 speed", 1, NULL},
    {44, 0, 127, -1, "lfo1 depth", 1, NULL},
    {67, 0, 1, -1, "lfo1 tempo sync", 0, NULL},
    {68, 0, 1, -1, "lfo1 key sync", 0, NULL},
    {58, 0, 1, -1, "lfo2 on/off", 0, NULL},
    {47, 0, 5, -1, "lfo2 type", 0, NULL},
    {46, 1, 7, -1, "lfo2 destination", 0, NULL},
    {48, 0, 127, -1, "lfo2 speed", 1, NULL},
    {49, 0, 127, -1, "lfo2 depth", 1, NULL},
    {69, 0, 1, -1, "lfo2 tempo sync", 0, NULL},
    {70, 0, 1, -1, "lfo2 key sync", 0, NULL},
};
int g_midiAssignParamsNum = sizeof(g_midiAssignParams) / sizeof(MidiAssignParam);

void CSynthesizer1::initCCMap()
{
    static int wheelAssignVals[100];
    static const char* wheelAssignText[100];
    char str[0x400];

    if (s1g->m_bCCMapNotLoaded == 0) return;
    s1g->m_bCCMapNotLoaded = 0;

    getIniString("installfolder", s1g->m_installFolder, 0x400, "c:\\");

    getIniString("CCMap", str, 0x400, "");
    if (str[0] == 0 || getCCMapFromCSV(s1g->m_ccMap, str) == 0) {
        getDefaultCcMap(s1g->m_ccMap);
    }

    for (int i = 0; i < 512; i++) {
        s1g->m_paramRange[i] = -1;
    }
    for (int i = 0; i < g_midiAssignParamsNum; i++) {
        s1g->m_paramRange[g_midiAssignParams[i].id * 2] = g_midiAssignParams[i].min;
        s1g->m_paramRange[g_midiAssignParams[i].id * 2 + 1] = g_midiAssignParams[i].max;
    }

    static int midiCtrlSrcVals[] = {
        0xb001, 0xb002, 0xb003, 0xb004, 0xb005, 0xb006, 0xb007, 0xb008,
        0xb009, 0xb00a, 0xb00b, 0xd000, 0xe000,
    };
    static const char* midiCtrlSrcText[] = {
        "#1:mod wheel", "#2:breath ctl", "#3:ctrl3", "#4:foot ctrl",
        "#5:porta time", "#6:data entry", "#7:volume", "#8:balance",
        "#9:ctrl9", "#10;pan", "#11:expression", "chan a.touch", "pitch bend",
    };
    s1g->m_midiCtrlSrcVals = midiCtrlSrcVals;
    s1g->m_midiCtrlSrcText = midiCtrlSrcText;
    s1g->m_midiCtrlSrcNum = sizeof(midiCtrlSrcVals) / sizeof(int);

    int n = 0;
    wheelAssignVals[n] = -1;
    wheelAssignText[n] = "---";
    n++;
    for (int i = 0; i < g_midiAssignParamsNum; i++) {
        if (g_midiAssignParams[i].bAssignable) {
            wheelAssignVals[n] = g_midiAssignParams[i].id;
            if (g_midiAssignParams[i].shortName) {
                wheelAssignText[n] = g_midiAssignParams[i].shortName;
            } else {
                wheelAssignText[n] = g_midiAssignParams[i].name;
            }
            n++;
        }
    }
    s1g->m_wheelAssignVals = wheelAssignVals;
    s1g->m_wheelAssignText = wheelAssignText;
    s1g->m_wheelAssignNum = n;
}

void CSynthesizer1::getDefaultCcMap(int* pMap)
{
    memset(pMap, 0xff, sizeof(int) * 128);
    for (int i = 0; i < g_midiAssignParamsNum; i++) {
        int cc = g_midiAssignParams[i].cc;
        if (cc >= 0 && cc < 128) {
            pMap[cc] = g_midiAssignParams[i].id;
        }
    }
}

void CSynthesizer1::setCCMap(int* pMap)
{
    for (int i = 0; i < 128; i++) {
        s1g->m_ccMap[i] = pMap[i];
    }
    char str[0x400];
    createCSVStringFromCCMap(s1g->m_ccMap, str);
    setIniString("CCMap", str);
}

void CSynthesizer1::processMIDIShortDataIndividual(unsigned char st, unsigned char dt1, unsigned char dt2)
{
    // CC -> sound parameter
    if ((st & 0xF0) == 0xB0 && dt1 < 120) {
        int* ccMap = s1g->m_ccMap;
        int* pID = &ccMap[dt1];
        int id;
        if (m_bMidiLearning) {
            if (m_midiLearnParamID == -1) goto CTRL;
            for (int i = 0; i < sizeof(s1g->m_ccMap) / sizeof(int); i++) {
                if (ccMap[i] == m_midiLearnParamID) {
                    ccMap[i] = -1;
                    break;
                }
            }
            *pID = m_midiLearnParamID;
            id = m_midiLearnParamID;
        } else {
            id = *pID;
            if (id == -1) goto CTRL;
        }
        int val;
        if (checkSoundParam(id, dt2, &val)) {
            setSoundParam(id, val, 1);
        }
    }
CTRL:
    // midi ctrl src -> assigned parameter
    int kind = st & 0xF0;
    float ccVal = dt2 / 127.0f;
    float atVal = dt1 / 127.0f;
    float pbVal = (dt2 * 128 + dt1 - 0x2000) / 8192.0f;
    for (int i = 0; i < 2; i++) {
        int id = m_midiCtrl[i].paramID;
        if (id == -1) continue;
        int src = m_midiCtrl[i].src;
        if ((src >> 8) != kind) continue;
        float v;
        if (kind == 0xB0) {
            if (m_midiCtrl[i].srcCC != dt1) continue;
            v = ccVal;
        } else if (kind == 0xD0) {
            v = atVal;
        } else if (kind == 0xE0) {
            v = pbVal;
        } else {
            continue;
        }
        m_midiCtrl[i].val = v;
        setSoundParam2Engine(id, m_pCurSound->getParamVal(id));
    }
}

int CSynthesizer1::endMidiLearning()
{
    BOOL rtn = FALSE;
    char str[0x400];
    if (m_bMidiLearning) {
        m_bMidiLearning = 0;
        m_midiLearnParamID = -1;
        createCSVStringFromCCMap(s1g->m_ccMap, str);
        setIniString("CCMap", str);
        if (m_pSynWin) {
            MessageBox(m_pSynWin, "Midi CC learn mode end.", "Synth1", 2);
        }
        rtn = TRUE;
    }
    return rtn;
}

int CSynthesizer1::startMidiLearning()
{
    if (m_bMidiLearning) return 0;
    if (m_pSynWin) {
        MessageBox(m_pSynWin, "Midi CC learn mode start.", "Synth1", 2);
    }
    m_bMidiLearning = 1;
    m_midiLearnParamID = -1;
    return 1;
}

void CSynthesizer1::getParamValRange(int id, int* pMin, int* pMax)
{
    int i = id * 2;
    *pMin = s1g->m_paramRange[i];
    *pMax = s1g->m_paramRange[i + 1];
}

void CSynthesizer1::setMidiCtrlAssignParamID(int no, int id)
{
    for (int i = 0; i < s1g->m_wheelAssignNum; i++) {
        if (s1g->m_wheelAssignVals[i] == id) {
            MidiCtrlAssign* pCtrl = &m_midiCtrl[no];
            int prev = pCtrl->paramID;
            if (prev != -1) {
                // restore the previous parameter without the controller offset
                float v = pCtrl->val;
                pCtrl->val = 0;
                setSoundParam2EngineMain(prev, m_pCurSound->getParamVal(prev));
                pCtrl->val = v;
            }
            pCtrl->paramID = id;
            if (id == -1) return;
            setSoundParam2EngineMain(id, m_pCurSound->getParamVal(id));
            return;
        }
    }
}

void CSynthesizer1::setMidiCtrlSrc(int no, int src)
{
    for (int i = 0; i < s1g->m_midiCtrlSrcNum; i++) {
        if (s1g->m_midiCtrlSrcVals[i] == src) {
            MidiCtrlAssign* pCtrl = &m_midiCtrl[no];
            pCtrl->src = src;
            pCtrl->val = 0;
            int id = pCtrl->paramID;
            if (id == -1) return;
            setSoundParam2EngineMain(id, m_pCurSound->getParamVal(id));
            return;
        }
    }
}

void CSynthesizer1::setMidiCtrlSens(int no, float sens)
{
    m_midiCtrl[no].sens = sens;
    int id = m_midiCtrl[no].paramID;
    if (id == -1) return;
    setSoundParam2EngineMain(id, m_pCurSound->getParamVal(id));
}

int CSynthesizer1::isMidiCCAssignable(int id)
{
    for (int i = 0; i < g_midiAssignParamsNum; i++) {
        if (g_midiAssignParams[i].id == id) {
            return g_midiAssignParams[i].cc != -2;
        }
    }
    return 0;
}

int CSynthesizer1::getParamName(int id, char* name)
{
    for (int i = 0; i < g_midiAssignParamsNum; i++) {
        if (g_midiAssignParams[i].id == id) {
            strcpy(name, g_midiAssignParams[i].name);
            return 1;
        }
    }
    return 0;
}
