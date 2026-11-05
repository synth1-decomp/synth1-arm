/**
 * @file test_synth1conv.cpp
 * @brief Unit tests for the static value conversions of CSynthesizer1 (synth1.cpp): the host
 *        parameter mapping convValIntFloat() / valSyn2Vst() / valVst2Syn(), and the patch
 *        version upgrade steps (convertSingleParam*()).
 */
#include "test.h"

#include <math.h>
#include <string.h>

#include "engine.h"
#include "synth1.h"

namespace {

/**
 * @brief Looks up a parameter's MIDI range in g_midiAssignParams.
 *
 * @return True if the id is in the table.
 */
bool tableRange(int id, int* pMin, int* pMax)
{
    for (int i = 0; i < g_midiAssignParamsNum; i++) {
        if (g_midiAssignParams[i].id == id) {
            *pMin = g_midiAssignParams[i].min;
            *pMax = g_midiAssignParams[i].max;
            return true;
        }
    }
    return false;
}

/**
 * @brief Host value of an internal value (patch version 113).
 */
float toF(int id, int v, int ver = 113)
{
    return CSynthesizer1::valSyn2Vst(id, v, ver);
}

/**
 * @brief Internal value of a host value (patch version 113).
 */
int toI(int id, float f, int ver = 113)
{
    return CSynthesizer1::valVst2Syn(id, f, ver);
}

/**
 * @brief Checks that r is the index whose curve value is closest to target among r-1, r, r+1.
 */
template <class Curve>
bool isLocalArgmin(Curve curve, int r, double target)
{
    double d = fabs(curve(r) - target);
    if (r > 0 && fabs(curve(r - 1) - target) < d - 1e-9) return false;
    if (r < 127 && fabs(curve(r + 1) - target) < d - 1e-9) return false;
    return true;
}

double decayCurve(int i) { return (float)(pow(6000.0, i / 127.0) * 0.01); }
double amountCurve(int i) { return (i - 63) * 0.015625f; }
double chorusRateCurve(int i) { return i == 0 ? 0.01 : (float)(pow(20000.0, i / 127.0f) * 0.02); }
double chorusTimeCurve(int i)
{
    double x = i / 127.0;
    double t = x >= 0.5 ? x * 30.0 : pow(300.0, pow(x * 2, 0.6)) * 0.05;
    return (float)(t / 1000.0);
}

}  // namespace

// ---------------------------------------------------------------------------
// convValIntFloat

/** @brief Every sound parameter 0..98 and the common parameters 250, 251, 252, 254 are known;
 *         other ids return FALSE and write nothing. */
TEST(conv, known_and_unknown_ids)
{
    for (int id = 0; id <= 98; id++) {
        int mn, mx;
        int v = 0;                          // a valid value: the range minimum (polyphony: 1)
        if (tableRange(id, &mn, &mx) && mn != -1) v = mn;
        if (id == 94) v = 1;
        float f = -1;
        CHECK(CSynthesizer1::convValIntFloat(id, &v, &f, 1, 113) != 0);
        CHECK(f >= 0.0f && f <= 1.0f);
        v = -12345;
        f = 0.5f;
        CHECK(CSynthesizer1::convValIntFloat(id, &v, &f, 0, 113) != 0);
        CHECK(v != -12345);
    }
    int common[] = {250, 251, 252, 254};
    for (int k = 0; k < 4; k++) {
        int v = 1;
        float f = -1;
        CHECK(CSynthesizer1::convValIntFloat(common[k], &v, &f, 1, 113) != 0);
    }
    int unknown[] = {-1, 99, 100, 127, 200, 249, 253, 255, 256, 1000};
    for (int k = 0; k < 10; k++) {
        int v = 77;
        float f = 0.25f;
        CHECK_EQ(CSynthesizer1::convValIntFloat(unknown[k], &v, &f, 1, 113), 0);
        CHECK_CLOSE(f, 0.25, 0);
        CHECK_EQ(CSynthesizer1::convValIntFloat(unknown[k], &v, &f, 0, 113), 0);
        CHECK_EQ(v, 77);
    }
}

/** @brief For every parameter with a MIDI range, int -> float -> int returns the value for the
 *         whole range, and the host values lie in (0, 1). Chorus type 3 is the one exception
 *         (see chorus_type_quirk). */
TEST(conv, round_trip_all_ranges)
{
    int checked = 0;
    for (int id = 0; id <= 98; id++) {
        int mn, mx;
        if (!tableRange(id, &mn, &mx) || mn == -1) continue;
        for (int v = mn; v <= mx; v++) {
            if (id == 64 && v == 3) continue;
            float f = toF(id, v);
            CHECK(f > 0.0f && f < 1.0f);
            int back = toI(id, f);
            if (back != v) {
                char msg[128];
                snprintf(msg, sizeof msg, "round trip id %d value %d -> %g -> %d", id, v, f, back);
                t1::fail(__FILE__, __LINE__, msg);
            } else {
                t1::pass();
            }
            checked++;
        }
    }
    CHECK(checked > 3000);
}

/** @brief Parameters not in the MIDI table round-trip over their documented ranges: polyphony
 *         1..32, program 0..127, bank 0..99, volume 0..127. */
TEST(conv, round_trip_other_params)
{
    for (int v = 1; v <= 32; v++) CHECK_EQ(toI(94, toF(94, v)), v);
    for (int v = 0; v <= 127; v++) CHECK_EQ(toI(252, toF(252, v)), v);
    for (int v = 0; v <= 99; v++) CHECK_EQ(toI(251, toF(251, v)), v);
    for (int v = 0; v <= 127; v++) CHECK_EQ(toI(254, toF(254, v)), v);
}

/** @brief The MIDI controller source / assign parameters (86..89) map v to (v + 1) / 65536 and
 *         round-trip every controller source and assignable parameter id (and -1). */
TEST(conv, round_trip_midi_ctrl)
{
    t1::initEngine();
    CSynthesizer1::initCCMap();
    CHECK_CLOSE(toF(86, 0xb001), (0xb001 + 1) / 65536.0, 1e-9);
    CHECK_CLOSE(toF(87, -1), 0.0, 0);
    for (int i = 0; i < s1g->m_midiCtrlSrcNum; i++) {
        int v = s1g->m_midiCtrlSrcVals[i];
        CHECK_EQ(toI(86, toF(86, v)), v);
        CHECK_EQ(toI(88, toF(88, v)), v);
    }
    for (int i = 0; i < s1g->m_wheelAssignNum; i++) {
        int v = s1g->m_wheelAssignVals[i];
        CHECK_EQ(toI(87, toF(87, v)), v);
        CHECK_EQ(toI(89, toF(89, v)), v);
    }
}

/** @brief Continuous 0..127 parameters use 128 equal slots: v -> (v + 0.5) / 128; the host
 *         value is strictly increasing; 0 -> 0, 0.5 -> 64, 1 -> 127. */
TEST(conv, continuous_params)
{
    int ids[] = {2, 19, 21, 37, 44, 45, 53, 60, 83, 98, 254};
    for (int k = 0; k < 11; k++) {
        int id = ids[k];
        float prev = -1;
        for (int v = 0; v <= 127; v++) {
            float f = toF(id, v);
            CHECK_CLOSE(f, (v + 0.5) / 128.0, 1e-6);
            CHECK(f > prev);
            prev = f;
        }
        CHECK_EQ(toI(id, 0.0f), 0);
        CHECK_EQ(toI(id, 0.5f), 64);
        CHECK_EQ(toI(id, 1.0f), 127);
        CHECK_EQ(toI(id, 0.999f), 127);
    }
}

/** @brief Known host values of the option lists and the float -> int thresholds. */
TEST(conv, option_list_values)
{
    // osc mod dest
    CHECK_CLOSE(toF(71, 0), 0.166, 1e-6);
    CHECK_CLOSE(toF(71, 1), 0.5, 1e-6);
    CHECK_CLOSE(toF(71, 2), 0.833, 1e-6);
    CHECK_EQ(toI(71, 0.32f), 0);
    CHECK_EQ(toI(71, 0.34f), 1);
    CHECK_EQ(toI(71, 0.7f), 2);
    // osc1 shape / sub shape: 4 slots
    float shapes[] = {0.125f, 0.375f, 0.625f, 0.875f};
    for (int v = 0; v < 4; v++) {
        CHECK_CLOSE(toF(0, v), shapes[v], 0);
        CHECK_CLOSE(toF(96, v), shapes[v], 0);
        CHECK_CLOSE(toF(1, v + 1), shapes[v], 0);      // osc2 shape 1..4
        CHECK_CLOSE(toF(31, v + 1), shapes[v], 0);     // arp type 1..4
    }
    CHECK_EQ(toI(0, 0.0f), 0);
    CHECK_EQ(toI(0, 0.25f), 1);
    CHECK_EQ(toI(0, 0.5f), 2);
    CHECK_EQ(toI(0, 0.75f), 3);
    CHECK_EQ(toI(0, 1.0f), 3);
    CHECK_EQ(toI(1, 0.0f), 1);
    CHECK_EQ(toI(1, 1.0f), 4);
    // filter type: 5 slots
    CHECK_CLOSE(toF(14, 0), 0.1, 1e-7);
    CHECK_CLOSE(toF(14, 4), 0.9, 1e-7);
    CHECK_EQ(toI(14, 0.19f), 0);
    CHECK_EQ(toI(14, 0.61f), 3);
    // osc key shift -24..24 (49 slots)
    CHECK_CLOSE(toF(9, -24), 0.5 / 49, 1e-7);
    CHECK_CLOSE(toF(9, 0), 24.5 / 49, 1e-6);
    CHECK_EQ(toI(9, 0.0f), -24);
    CHECK_EQ(toI(9, 1.0f), 24);
    // on/off
    int onoff[] = {4, 6, 7, 10, 24, 57, 58, 59, 65, 66, 67, 68, 69, 70, 73, 74, 77, 97};
    for (int k = 0; k < 18; k++) {
        CHECK_CLOSE(toF(onoff[k], 0), 0.25, 0);
        CHECK_CLOSE(toF(onoff[k], 1), 0.75, 0);
        CHECK_CLOSE(toF(onoff[k], 5), 0.75, 0);         // any nonzero is "on"
        CHECK_EQ(toI(onoff[k], 0.49f), 0);
        CHECK_EQ(toI(onoff[k], 0.5f), 1);
    }
    // lfo type: slider order saw, tri, sine, square, s&h, smooth random
    int lfoType[] = {0, 1, 5, 2, 3, 4};
    for (int k = 0; k < 6; k++) {
        CHECK_CLOSE(toF(42, lfoType[k]), 0.2 + 0.1 * k, 1e-6);
        CHECK_CLOSE(toF(47, lfoType[k]), 0.2 + 0.1 * k, 1e-6);
        CHECK_EQ(toI(42, (float)(0.2 + 0.1 * k)), lfoType[k]);
    }
    CHECK_EQ(toI(42, 0.0f), 0);
    CHECK_EQ(toI(42, 1.0f), 4);
    // play mode: 0 -> 0.125, 1 -> 0.625, 2 -> 0.875; the second quarter also reads as 0
    CHECK_CLOSE(toF(38, 0), 0.125, 0);
    CHECK_CLOSE(toF(38, 1), 0.625, 0);
    CHECK_CLOSE(toF(38, 2), 0.875, 0);
    CHECK_EQ(toI(38, 0.3f), 0);
    CHECK_EQ(toI(38, 0.6f), 1);
    CHECK_EQ(toI(38, 0.8f), 2);
    // equal slots of the integer lists
    CHECK_CLOSE(toF(33, 0), 0.5 / 19, 1e-7);
    CHECK_EQ(toI(33, 1.0f), 18);
    CHECK_CLOSE(toF(78, 9), 0.95, 1e-6);
    CHECK_EQ(toI(78, 1.0f), 9);
    CHECK_CLOSE(toF(82, 1), 0.5, 1e-6);
    CHECK_EQ(toI(82, 1.0f), 2);
    CHECK_CLOSE(toF(35, 0), 0.025, 1e-7);
    CHECK_EQ(toI(35, 1.0f), 19);
    CHECK_CLOSE(toF(94, 1), 0.015625, 0);
    CHECK_EQ(toI(94, 1.0f), 32);
    CHECK_CLOSE(toF(93, 2), 0.5 / 7, 1e-7);
    CHECK_EQ(toI(93, 1.0f), 8);
    CHECK_EQ(toI(85, 1.0f), 48);
    CHECK_EQ(toI(40, 1.0f), 24);
    CHECK_CLOSE(toF(40, 12), 12.5 / 25, 1e-6);
    CHECK_EQ(toI(252, 1.0f), 127);
    CHECK_CLOSE(toF(252, 0), 0.5 / 128, 0);
}

/** @brief Chorus type 1..4 maps 1, 2 to the first two quarters and 3, 4 both to 0.875; reading
 *         back, the third quarter gives 2 and the last 4, so type 3 does not round-trip. */
TEST(conv, chorus_type_quirk)
{
    CHECK_CLOSE(toF(64, 1), 0.125, 0);
    CHECK_CLOSE(toF(64, 2), 0.375, 0);
    CHECK_CLOSE(toF(64, 3), 0.875, 0);
    CHECK_CLOSE(toF(64, 4), 0.875, 0);
    CHECK_EQ(toI(64, 0.1f), 1);
    CHECK_EQ(toI(64, 0.3f), 2);
    CHECK_EQ(toI(64, 0.6f), 2);
    CHECK_EQ(toI(64, 0.9f), 4);
    CHECK_EQ(toI(64, toF(64, 3)), 4);
}

/** @brief The arpeggiator octave range is 1..4 in version 1.00 patches and 0..3 later. */
TEST(conv, arp_oct_range_by_version)
{
    for (int v = 1; v <= 4; v++) {
        CHECK_CLOSE(toF(32, v, 100), (v - 1) * 0.25 + 0.125, 0);
        CHECK_EQ(toI(32, toF(32, v, 100), 100), v);
    }
    for (int v = 0; v <= 3; v++) {
        CHECK_CLOSE(toF(32, v, 113), v * 0.25 + 0.125, 0);
        CHECK_CLOSE(toF(32, v, 101), v * 0.25 + 0.125, 0);
        CHECK_EQ(toI(32, toF(32, v, 113), 113), v);
    }
    CHECK_EQ(toI(32, 1.0f, 100), 4);
    CHECK_EQ(toI(32, 1.0f, 113), 3);
    CHECK_EQ(toI(32, 0.0f, 100), 1);
    CHECK_EQ(toI(32, 0.0f, 113), 0);
}

/** @brief The LFO destination list has 5 entries up to version 1.00, 6 up to 1.06 and 7 later,
 *         with different host values for each. */
TEST(conv, lfo_dest_by_version)
{
    int ids[] = {41, 46};
    for (int k = 0; k < 2; k++) {
        int id = ids[k];
        for (int v = 1; v <= 5; v++) {
            CHECK_CLOSE(toF(id, v, 100), 0.1 + 0.2 * (v - 1), 1e-6);
            CHECK_EQ(toI(id, toF(id, v, 100), 100), v);
        }
        CHECK_EQ(toI(id, 1.0f, 100), 5);
        for (int v = 1; v <= 6; v++) {
            CHECK_CLOSE(toF(id, v, 105), 0.1 + 0.1 * v, 1e-6);
            CHECK_EQ(toI(id, toF(id, v, 106), 106), v);
        }
        CHECK_CLOSE(toF(id, 7, 106), 0.7, 1e-6);        // 7 did not exist yet
        CHECK_EQ(toI(id, 1.0f, 106), 6);
        for (int v = 1; v <= 7; v++) {
            CHECK_CLOSE(toF(id, v, 107), 0.1 + 0.1 * v, 1e-6);
            CHECK_EQ(toI(id, toF(id, v, 113), 113), v);
        }
        CHECK_EQ(toI(id, 1.0f, 113), 7);
        CHECK_EQ(toI(id, 0.0f, 113), 1);
    }
}

/** @brief The bank parameter has 10 banks before version 1.07 and in 1.08, 100 otherwise. */
TEST(conv, bank_count_by_version)
{
    CHECK_CLOSE(toF(251, 3, 100), 0.35, 1e-6);
    CHECK_CLOSE(toF(251, 3, 106), 0.35, 1e-6);
    CHECK_CLOSE(toF(251, 3, 108), 0.35, 1e-6);
    CHECK_CLOSE(toF(251, 3, 107), 0.035, 1e-6);
    CHECK_CLOSE(toF(251, 3, 109), 0.035, 1e-6);
    CHECK_CLOSE(toF(251, 3, 113), 0.035, 1e-6);
    CHECK_EQ(toI(251, 1.0f, 100), 9);
    CHECK_EQ(toI(251, 1.0f, 108), 9);
    CHECK_EQ(toI(251, 1.0f, 107), 99);
    CHECK_EQ(toI(251, 1.0f, 113), 99);
    for (int v = 0; v < 10; v++) CHECK_EQ(toI(251, toF(251, v, 100), 100), v);
}

/** @brief The version parameter (250) maps v to v / 1000. */
TEST(conv, version_param)
{
    CHECK_CLOSE(toF(250, 113), 0.113, 1e-7);
    CHECK_CLOSE(toF(250, 100), 0.1, 1e-7);
    CHECK_EQ(toI(250, 0.5f), 500);
    CHECK_EQ(toI(250, 0.0f), 0);
}

/** @brief valVstOld2Syn() converts the host value, then upgrades it from the patch version. */
TEST(conv, val_vst_old_2_syn)
{
    CSoundData dst;
    dst.setParamVal(84, 99);
    CSoundData src;
    // delay dry/wet 100 in a 1.05 patch is 50 now
    CHECK_EQ(CSynthesizer1::valVstOld2Syn(37, toF(37, 100), 105, &src, &dst), 50);
    CHECK_EQ(CSynthesizer1::valVstOld2Syn(37, toF(37, 100), 113, &src, &dst), 100);
    CHECK_EQ(CSynthesizer1::valVstOld2Syn(19, toF(19, 40), 105, &src, &dst), 40);
}

// ---------------------------------------------------------------------------
// version upgrade steps

/** @brief 1.05 -> 1.07: the delay dry/wet is halved. */
TEST(conv, convert105_delay_dry_wet)
{
    CSoundData dst;
    for (int v = 0; v <= 127; v++) {
        CHECK_EQ(CSynthesizer1::convertSingleParam105To107(37, v, &dst), v / 2);
    }
}

/** @brief 1.05 -> 1.07: parameters added in 1.07 (75..85, 90) take the default from pDst
 *         (0 if pDst does not have them); unrelated ids are unchanged. */
TEST(conv, convert105_new_params_from_dst)
{
    CSoundData dst;
    int added[] = {75, 76, 77, 78, 79, 80, 81, 82, 83, 84, 85, 90};
    for (int k = 0; k < 12; k++) dst.setParamVal(added[k], 100 + k);
    for (int k = 0; k < 12; k++) {
        CHECK_EQ(CSynthesizer1::convertSingleParam105To107(added[k], 5, &dst), 100 + k);
    }
    CSoundData empty;
    CHECK_EQ(CSynthesizer1::convertSingleParam105To107(90, 5, &empty), 0);
    int same[] = {0, 1, 19, 20, 35, 36, 44, 56, 86, 89, 91, 98};
    for (int k = 0; k < 12; k++) {
        for (int v = 0; v <= 127; v += 9) {
            CHECK_EQ(CSynthesizer1::convertSingleParam105To107(same[k], v, &dst), v);
        }
    }
}

/** @brief 1.05 -> 1.07: the filter amount v (0..127 meaning 0..1) becomes the nearest value of
 *         the bipolar curve (i - 63) / 64: 0 -> 63, 127 -> 127, monotonic. */
TEST(conv, convert105_filter_amount)
{
    CSoundData dst;
    CHECK_EQ(CSynthesizer1::convertSingleParam105To107(21, 0, &dst), 63);
    CHECK_EQ(CSynthesizer1::convertSingleParam105To107(21, 127, &dst), 127);
    int prev = 0;
    for (int v = 0; v <= 127; v++) {
        int r = CSynthesizer1::convertSingleParam105To107(21, v, &dst);
        CHECK(r >= prev && r <= 127);
        CHECK(isLocalArgmin(amountCurve, r, v / 127.0));
        prev = r;
    }
}

/** @brief 1.05 -> 1.07: decay/release times (13, 16, 18, 26, 28) map to the nearest value on the
 *         new exponential curve; the result is monotonic, 0 stays 0. */
TEST(conv, convert105_decay_curves)
{
    CSoundData dst;
    int ids[] = {13, 16, 18, 26, 28};
    for (int k = 0; k < 5; k++) {
        int prev = 0;
        CHECK_EQ(CSynthesizer1::convertSingleParam105To107(ids[k], 0, &dst), 0);
        for (int v = 0; v <= 127; v++) {
            int r = CSynthesizer1::convertSingleParam105To107(ids[k], v, &dst);
            CHECK(r >= prev && r <= 127);
            double target = pow(60.0, pow(v / 127.0, 2.95)) - 1.0;
            CHECK(isLocalArgmin(decayCurve, r, target));
            prev = r;
        }
    }
}

/** @brief 1.05 -> 1.07: chorus delay time (52) and rate (54) map to the nearest value of the
 *         new curves, monotonically. */
TEST(conv, convert105_chorus_time_and_rate)
{
    CSoundData dst;
    int prevT = 0, prevR = 0;
    for (int v = 0; v <= 127; v++) {
        int t = CSynthesizer1::convertSingleParam105To107(52, v, &dst);
        double x = v / 127.0;
        double target = (x >= 0.5 ? x * 30.0 : pow(50.0, x * 2) * 0.3) / 1000.0;
        CHECK(t >= prevT && t <= 127);
        CHECK(isLocalArgmin(chorusTimeCurve, t, target));
        prevT = t;
        int r = CSynthesizer1::convertSingleParam105To107(54, v, &dst);
        double rt = pow(10.0, pow(v / 127.0, 1.8)) - 1.0 + 0.05;
        CHECK(r >= prevR && r <= 127);
        CHECK(isLocalArgmin(chorusRateCurve, r, rt));
        prevR = r;
    }
    // the upper half of the delay time curve is unchanged
    CHECK_EQ(CSynthesizer1::convertSingleParam105To107(52, 100, &dst), 100);
}

/** @brief 1.05 -> 1.07: the chorus feedback target uses an integer division (val / 127), so
 *         every value below 127 maps to the centre 64 and 127 maps to 6. */
TEST(conv, convert105_chorus_feedback)
{
    CSoundData dst;
    for (int v = 0; v < 127; v++) {
        CHECK_EQ(CSynthesizer1::convertSingleParam105To107(55, v, &dst), 64);
    }
    CHECK_EQ(CSynthesizer1::convertSingleParam105To107(55, 127, &dst), 6);
}

/** @brief 1.07 -> 1.071 sets fixed defaults; 1.09 -> 1.11 and 1.11 -> 1.12 take the new
 *         parameters from pDst; 1.12 -> 1.13 changes nothing. */
TEST(conv, later_steps)
{
    CSoundData dst;
    for (int id = 0; id < 99; id++) dst.setParamVal(id, 200 + id);
    CHECK_EQ(CSynthesizer1::convertSingleParam107To1071(84, 1), 64);
    CHECK_EQ(CSynthesizer1::convertSingleParam107To1071(85, 1), 24);
    for (int id = 86; id <= 89; id++) CHECK_EQ(CSynthesizer1::convertSingleParam107To1071(id, 9), 0);
    CHECK_EQ(CSynthesizer1::convertSingleParam107To1071(19, 9), 9);
    for (int id = 95; id <= 97; id++) {
        CHECK_EQ(CSynthesizer1::convertSingleParam109To111(id, 1, 0, &dst), 200 + id);
    }
    CHECK_EQ(CSynthesizer1::convertSingleParam109To111(98, 1, 0, &dst), 1);
    CHECK_EQ(CSynthesizer1::convertSingleParam111To112(98, 1, 0, &dst), 298);
    CHECK_EQ(CSynthesizer1::convertSingleParam111To112(97, 1, 0, &dst), 1);
    for (int id = 0; id < 99; id++) {
        CHECK_EQ(CSynthesizer1::convertSingleParam112To113(id, id + 3, 0, 0), id + 3);
    }
}

/** @brief 1.071 -> 1.09: unison voice count from the play mode, MIDI sensitivities rescaled by
 *         the LFO depth, new parameters from pDst. */
TEST(conv, convert1071_to_109)
{
    CSoundData dst;
    int fromDst[] = {86, 87, 88, 89, 91, 92, 94};
    for (int k = 0; k < 7; k++) dst.setParamVal(fromDst[k], 50 + k);
    CSoundData src;
    for (int k = 0; k < 7; k++) {
        CHECK_EQ(CSynthesizer1::convertSingleParam1071To109(fromDst[k], 1, &src, &dst), 50 + k);
    }
    src.setParamVal(38, 0);
    CHECK_EQ(CSynthesizer1::convertSingleParam1071To109(93, 9, &src, &dst), 2);
    src.setParamVal(38, 1);
    CHECK_EQ(CSynthesizer1::convertSingleParam1071To109(93, 9, &src, &dst), 4);
    // sensitivity: with depth 0 the full value maps to 64 + 63
    src.setParamVal(44, 0);
    src.setParamVal(43, 127);
    CHECK_EQ(CSynthesizer1::convertSingleParam1071To109(50, 127, &src, &dst), 127);
    CHECK_EQ(CSynthesizer1::convertSingleParam1071To109(50, 0, &src, &dst), 64);
    CHECK_EQ(CSynthesizer1::convertSingleParam1071To109(51, 127, &src, &dst), 64);  // depth 127
    // unison detune: monotonic, 0..127
    src.setParamVal(38, 0);
    int prev = -1;
    for (int v = 0; v <= 127; v++) {
        int r = CSynthesizer1::convertSingleParam1071To109(75, v, &src, &dst);
        CHECK(r >= prev && r >= 0 && r <= 127);
        prev = r;
    }
    CHECK_EQ(CSynthesizer1::convertSingleParam1071To109(19, 33, &src, &dst), 33);
}

/** @brief convertSingleParam() applies the steps from the given version on: unlisted versions
 *         (110, 113 and newer) leave the value unchanged. */
TEST(conv, convert_single_param_chain)
{
    CSoundData dst;
    dst.setParamVal(98, 77);
    dst.setParamVal(84, 55);
    CSoundData src;
    CHECK_EQ(CSynthesizer1::convertSingleParam(37, 100, 105, &src, &dst), 50);
    CHECK_EQ(CSynthesizer1::convertSingleParam(37, 100, 106, &src, &dst), 50);
    CHECK_EQ(CSynthesizer1::convertSingleParam(37, 100, 100, &src, &dst), 50);
    CHECK_EQ(CSynthesizer1::convertSingleParam(37, 100, 107, &src, &dst), 100);
    CHECK_EQ(CSynthesizer1::convertSingleParam(98, 5, 109, &src, &dst), 77);
    CHECK_EQ(CSynthesizer1::convertSingleParam(98, 5, 111, &src, &dst), 77);
    CHECK_EQ(CSynthesizer1::convertSingleParam(98, 5, 112, &src, &dst), 5);
    CHECK_EQ(CSynthesizer1::convertSingleParam(98, 5, 110, &src, &dst), 5);
    CHECK_EQ(CSynthesizer1::convertSingleParam(98, 5, 113, &src, &dst), 5);
    // 1.05: 84 comes from pDst in the 1.05 step, then 1.07 -> 1.071 forces 64
    CHECK_EQ(CSynthesizer1::convertSingleParam(84, 5, 105, &src, &dst), 64);
    CHECK_EQ(CSynthesizer1::convertSingleParam(84, 5, 107, &src, &dst), 5);
}

/** @brief 1.00 -> 1.05: attacks, sustain/gain, velocity and LFO speed map to nearby values on
 *         the new curves (monotonic, within range); 67..72 come from pDst. */
TEST(conv, convert100_to_105)
{
    CSoundData dst;
    for (int id = 67; id <= 72; id++) dst.setParamVal(id, id + 1);
    for (int id = 67; id <= 72; id++) {
        CHECK_EQ(CSynthesizer1::convertSingleParam100To105(id, 3, &dst), id + 1);
    }
    int ids[] = {12, 15, 25, 27, 29, 43, 48};
    for (int k = 0; k < 7; k++) {
        int prev = 0;
        for (int v = 0; v <= 127; v++) {
            int r = CSynthesizer1::convertSingleParam100To105(ids[k], v, &dst);
            CHECK(r >= prev && r <= 127);
            prev = r;
        }
    }
    // velocity sens: decreasing target, increasing result
    int prev = 0;
    for (int v = 0; v <= 127; v++) {
        int r = CSynthesizer1::convertSingleParam100To105(30, v, &dst);
        CHECK(r >= prev && r <= 127);
        prev = r;
    }
    CHECK_EQ(CSynthesizer1::convertSingleParam100To105(30, 0, &dst), 0);
    // sustain/gain: 0 stays 0, full stays full
    CHECK_EQ(CSynthesizer1::convertSingleParam100To105(27, 0, &dst), 0);
    CHECK_EQ(CSynthesizer1::convertSingleParam100To105(27, 127, &dst), 127);
    // osc2 fine tune: the centre maps into the dead zone 62..66
    int c = CSynthesizer1::convertSingleParam100To105(3, 64, &dst);
    CHECK(c >= 61 && c <= 66);
    CHECK_EQ(CSynthesizer1::convertSingleParam100To105(19, 42, &dst), 42);
}
