/**
 * @file test_inst.cpp
 * @brief Unit tests for techpop/inst.h: CInst MIDI short-message parsing and dispatch to the
 *        virtual handlers, bank/NRPN/data-entry state and the shared enable flags.
 */
#include "test.h"

#include <string.h>

#include "inst.h"

namespace {

/**
 * @brief One recorded handler call.
 */
struct Call {
    char name[24];
    int a;
    int b;
    int c;
};

/**
 * @brief CInst subclass that records every handler call and exposes the protected state.
 */
class RecInst : public CInst {
public:
    RecInst() : num(0) {}

    void rec(const char* name, int a = 0, int b = 0, int c = 0)
    {
        if (num < 512) {
            strncpy(calls[num].name, name, sizeof calls[num].name - 1);
            calls[num].name[sizeof calls[num].name - 1] = 0;
            calls[num].a = a;
            calls[num].b = b;
            calls[num].c = c;
        }
        num++;
    }

    int count(const char* name) const
    {
        int n = 0;
        for (int i = 0; i < num && i < 512; i++) if (strcmp(calls[i].name, name) == 0) n++;
        return n;
    }

    void clear() { num = 0; }

    virtual void processMIDIShortDataIndividual(unsigned char st, unsigned char dt1, unsigned char dt2)
    {
        rec("individual", st, dt1, dt2);
    }
    virtual void noteOn(int nn, int vel) { rec("noteOn", nn, vel); }
    virtual void noteOff(int nn) { rec("noteOff", nn); }
    virtual void noteOnFromArp(int nn, int vel) { rec("noteOnFromArp", nn, vel); }
    virtual void noteOffFromArp(int nn) { rec("noteOffFromArp", nn); }
    virtual void setModulationWheel(unsigned char v) { rec("modWheel", v); }
    virtual void changedNRPNData(int bHI) { rec("nrpnData", bHI, m_dataEntryHI, m_dataEntryLO); }
    virtual void setVolume(unsigned char v) { rec("volume", v); }
    virtual void setSustainEnable(int b) { rec("sustain", b); }
    virtual void receiveProgramChange(int progNo) { rec("progChange", progNo); }
    virtual void receiveBankChange(int bankNo) { rec("bankChange", bankNo); }
    virtual void setPitchBend(short v) { rec("pitchBend", v); }

    int nrpnMSB() const { return m_NRPNNoMSB; }
    int nrpnLSB() const { return m_NRPNNoLSB; }
    int dataHI() const { return m_dataEntryHI; }
    int dataLO() const { return m_dataEntryLO; }
    int bendRange() const { return m_pitchBendRange; }
    int bankNo() const { return m_bankNo; }

    Call calls[512];
    int num;
};

/**
 * @brief CInst subclass that records only resetAllControl/noteOffAll overrides, to check that the
 *        channel mode messages are dispatched virtually.
 */
class ModeInst : public RecInst {
public:
    virtual void noteOffAll() { rec("noteOffAll"); }
    virtual void resetAllControl() { rec("resetAll"); }
};

/**
 * @brief Restores the shared static enable flags.
 */
struct FlagGuard {
    int bank, prog;
    FlagGuard() : bank(CInst::s_bEnableBankSelect), prog(CInst::s_bEnableProgChange) {}
    ~FlagGuard()
    {
        CInst::s_bEnableBankSelect = bank;
        CInst::s_bEnableProgChange = prog;
    }
};

}  // namespace

/**
 * @brief The constructor clears the MIDI state and sets the pitch-bend range to 12.
 */
TEST(inst, constructor_defaults)
{
    RecInst r;
    CHECK_EQ(r.nrpnMSB(), 0);
    CHECK_EQ(r.nrpnLSB(), 0);
    CHECK_EQ(r.dataHI(), 0);
    CHECK_EQ(r.dataLO(), 0);
    CHECK_EQ(r.bendRange(), 12);
    CHECK_EQ(r.bankNo(), 0);
    CHECK_EQ(r.num, 0);
}

/**
 * @brief The base class handlers are no-ops and can be driven with any message.
 */
TEST(inst, base_class_handlers_are_harmless)
{
    CInst i;
    unsigned char st[] = {0x80, 0x90, 0xA0, 0xB0, 0xC0, 0xD0, 0xE0, 0xF0};
    for (unsigned k = 0; k < sizeof st; k++) {
        i.processMIDIShortData(st[k], 60, 100);
        i.processMIDIShortData(st[k], 0x79, 0);
        i.processMIDIShortData(st[k], 0x7B, 0);
    }
    i.noteOffAll();
    i.resetAllControl();
    i.changedNRPNData(1);
    CHECK(true);
}

/**
 * @brief Note on with a velocity calls noteOn(); the channel nibble is ignored.
 */
TEST(inst, note_on)
{
    RecInst r;
    r.processMIDIShortData(0x90, 60, 100);
    r.processMIDIShortData(0x9F, 127, 1);
    REQUIRE(r.num == 2);
    CHECK_STREQ(r.calls[0].name, "noteOn");
    CHECK_EQ(r.calls[0].a, 60);
    CHECK_EQ(r.calls[0].b, 100);
    CHECK_STREQ(r.calls[1].name, "noteOn");
    CHECK_EQ(r.calls[1].a, 127);
    CHECK_EQ(r.calls[1].b, 1);
}

/**
 * @brief Note on with velocity 0 and note off both call noteOff().
 */
TEST(inst, note_off_and_zero_velocity)
{
    RecInst r;
    r.processMIDIShortData(0x93, 64, 0);
    r.processMIDIShortData(0x80, 65, 64);
    r.processMIDIShortData(0x8A, 0, 0);
    REQUIRE(r.num == 3);
    CHECK_STREQ(r.calls[0].name, "noteOff");
    CHECK_EQ(r.calls[0].a, 64);
    CHECK_STREQ(r.calls[1].name, "noteOff");
    CHECK_EQ(r.calls[1].a, 65);
    CHECK_STREQ(r.calls[2].name, "noteOff");
    CHECK_EQ(r.calls[2].a, 0);
}

/**
 * @brief Note numbers above 127 are dropped for note on and note off.
 */
TEST(inst, notes_above_127_are_dropped)
{
    RecInst r;
    r.processMIDIShortData(0x90, 128, 100);
    r.processMIDIShortData(0x90, 200, 0);
    r.processMIDIShortData(0x80, 255, 0);
    CHECK_EQ(r.num, 0);
}

/**
 * @brief Modulation wheel, volume and damper pedal CCs call their handlers, followed by
 *        processMIDIShortDataIndividual() with the raw message.
 */
TEST(inst, basic_control_changes)
{
    RecInst r;
    r.processMIDIShortData(0xB2, 0x01, 77);
    r.processMIDIShortData(0xB0, 0x07, 100);
    REQUIRE(r.num == 4);
    CHECK_STREQ(r.calls[0].name, "modWheel");
    CHECK_EQ(r.calls[0].a, 77);
    CHECK_STREQ(r.calls[1].name, "individual");
    CHECK_EQ(r.calls[1].a, 0xB2);
    CHECK_EQ(r.calls[1].b, 0x01);
    CHECK_EQ(r.calls[1].c, 77);
    CHECK_STREQ(r.calls[2].name, "volume");
    CHECK_EQ(r.calls[2].a, 100);
    CHECK_STREQ(r.calls[3].name, "individual");
}

/**
 * @brief The damper pedal turns on at values >= 64 and off below.
 */
TEST(inst, damper_pedal_threshold)
{
    RecInst r;
    const int vals[] = {0, 63, 64, 127};
    const int expect[] = {0, 0, 1, 1};
    for (int k = 0; k < 4; k++) {
        r.clear();
        r.processMIDIShortData(0xB0, 0x40, (unsigned char)vals[k]);
        REQUIRE(r.num == 2);
        CHECK_STREQ(r.calls[0].name, "sustain");
        CHECK_EQ(r.calls[0].a, expect[k]);
    }
}

/**
 * @brief Unhandled controllers (< 120) only reach processMIDIShortDataIndividual().
 */
TEST(inst, other_controllers_go_to_individual)
{
    RecInst r;
    for (int cc = 0; cc < 120; cc++) {
        if (cc == 0x00 || cc == 0x20 || cc == 0x01 || cc == 0x06 || cc == 0x26 || cc == 0x07 ||
            cc == 0x40) {
            continue;
        }
        r.clear();
        r.processMIDIShortData(0xB0, (unsigned char)cc, 5);
        CHECK_EQ(r.num, 1);
        CHECK_STREQ(r.calls[0].name, "individual");
        CHECK_EQ(r.calls[0].b, cc);
    }
}

/**
 * @brief Bank select MSB (CC 0) and LSB (CC 32) combine into MSB << 8 | LSB and each calls
 *        receiveBankChange() with the combined number.
 */
TEST(inst, bank_select_combines_msb_lsb)
{
    FlagGuard guard;
    RecInst r;
    r.enableBankSelect(1);
    r.processMIDIShortData(0xB0, 0x00, 3);
    CHECK_EQ(r.bankNo(), 3 << 8);
    r.processMIDIShortData(0xB0, 0x20, 5);
    CHECK_EQ(r.bankNo(), (3 << 8) | 5);
    r.processMIDIShortData(0xB0, 0x00, 1);  // MSB change keeps LSB
    CHECK_EQ(r.bankNo(), (1 << 8) | 5);
    r.processMIDIShortData(0xB0, 0x20, 127);  // LSB change keeps MSB
    CHECK_EQ(r.bankNo(), (1 << 8) | 127);
    CHECK_EQ(r.count("bankChange"), 4);
    CHECK_EQ(r.count("individual"), 4);
    CHECK_STREQ(r.calls[0].name, "bankChange");
    CHECK_EQ(r.calls[0].a, 3 << 8);
    CHECK_EQ(r.calls[2].a, (3 << 8) | 5);
    CHECK_EQ(r.calls[6].a, (1 << 8) | 127);
}

/**
 * @brief With bank select disabled, CC 0/32 do not change the bank (but still reach
 *        processMIDIShortDataIndividual()).
 */
TEST(inst, bank_select_disabled)
{
    FlagGuard guard;
    RecInst r;
    r.enableBankSelect(0);
    CHECK_EQ(r.getEnableBankSelect(), 0);
    r.processMIDIShortData(0xB0, 0x00, 3);
    r.processMIDIShortData(0xB0, 0x20, 4);
    CHECK_EQ(r.bankNo(), 0);
    CHECK_EQ(r.count("bankChange"), 0);
    CHECK_EQ(r.count("individual"), 2);
}

/**
 * @brief Program change calls receiveProgramChange() only when enabled; the second data byte is
 *        ignored and processMIDIShortDataIndividual() is not called.
 */
TEST(inst, program_change)
{
    FlagGuard guard;
    RecInst r;
    r.enableProgramChange(1);
    CHECK(r.getEnableProgramChange() != 0);
    r.processMIDIShortData(0xC5, 42, 99);
    REQUIRE(r.num == 1);
    CHECK_STREQ(r.calls[0].name, "progChange");
    CHECK_EQ(r.calls[0].a, 42);
    r.enableProgramChange(0);
    CHECK_EQ(r.getEnableProgramChange(), 0);
    r.processMIDIShortData(0xC0, 7, 0);
    CHECK_EQ(r.num, 1);
}

/**
 * @brief The enable flags are static and shared by all instances.
 */
TEST(inst, enable_flags_are_shared)
{
    FlagGuard guard;
    RecInst a, b;
    a.enableBankSelect(0);
    a.enableProgramChange(0);
    CHECK_EQ(b.getEnableBankSelect(), 0);
    CHECK_EQ(b.getEnableProgramChange(), 0);
    CHECK_EQ(CInst::s_bEnableBankSelect, 0);
    b.enableBankSelect(1);
    b.enableProgramChange(5);
    CHECK_EQ(a.getEnableBankSelect(), 1);
    CHECK_EQ(a.getEnableProgramChange(), 5);
    CHECK_EQ(CInst::s_bEnableProgChange, 5);
}

/**
 * @brief Pitch bend combines the two data bytes LSB first into -8192..8191, then calls
 *        processMIDIShortDataIndividual().
 */
TEST(inst, pitch_bend)
{
    RecInst r;
    r.processMIDIShortData(0xE0, 0x00, 0x40);
    r.processMIDIShortData(0xE1, 0x00, 0x00);
    r.processMIDIShortData(0xE2, 0x7F, 0x7F);
    r.processMIDIShortData(0xE3, 0x01, 0x40);
    r.processMIDIShortData(0xE4, 0x7F, 0x3F);
    REQUIRE(r.num == 10);
    const int expect[] = {0, -8192, 8191, 1, -1};
    for (int k = 0; k < 5; k++) {
        CHECK_STREQ(r.calls[2 * k].name, "pitchBend");
        CHECK_EQ(r.calls[2 * k].a, expect[k]);
        CHECK_STREQ(r.calls[2 * k + 1].name, "individual");
        CHECK_EQ(r.calls[2 * k + 1].a, 0xE0 + k);
    }
}

/**
 * @brief Channel pressure goes straight to processMIDIShortDataIndividual().
 */
TEST(inst, channel_pressure)
{
    RecInst r;
    r.processMIDIShortData(0xD7, 90, 0);
    REQUIRE(r.num == 1);
    CHECK_STREQ(r.calls[0].name, "individual");
    CHECK_EQ(r.calls[0].a, 0xD7);
    CHECK_EQ(r.calls[0].b, 90);
}

/**
 * @brief Polyphonic key pressure and system messages are ignored.
 */
TEST(inst, ignored_messages)
{
    RecInst r;
    r.processMIDIShortData(0xA0, 60, 50);
    r.processMIDIShortData(0xF0, 1, 2);
    r.processMIDIShortData(0xF8, 0, 0);
    r.processMIDIShortData(0x40, 60, 100);  // running-status data byte: no type nibble matches
    CHECK_EQ(r.num, 0);
}

/**
 * @brief Data entry MSB/LSB store the byte and call changedNRPNData(1)/(0) before
 *        processMIDIShortDataIndividual(); NRPN CC 99/98 store the parameter number silently.
 */
TEST(inst, nrpn_and_data_entry)
{
    RecInst r;
    r.processMIDIShortData(0xB0, 0x63, 12);
    r.processMIDIShortData(0xB0, 0x62, 34);
    CHECK_EQ(r.nrpnMSB(), 12);
    CHECK_EQ(r.nrpnLSB(), 34);
    CHECK_EQ(r.count("nrpnData"), 0);
    CHECK_EQ(r.count("individual"), 2);
    r.clear();
    r.processMIDIShortData(0xB0, 0x06, 100);
    r.processMIDIShortData(0xB0, 0x26, 50);
    REQUIRE(r.num == 4);
    CHECK_STREQ(r.calls[0].name, "nrpnData");
    CHECK_EQ(r.calls[0].a, 1);
    CHECK_EQ(r.calls[0].b, 100);  // stored before the callback
    CHECK_STREQ(r.calls[1].name, "individual");
    CHECK_STREQ(r.calls[2].name, "nrpnData");
    CHECK_EQ(r.calls[2].a, 0);
    CHECK_EQ(r.calls[2].c, 50);
    CHECK_EQ(r.dataHI(), 100);
    CHECK_EQ(r.dataLO(), 50);
}

/**
 * @brief setDataEntryHI()/LO() can be called directly with the same effect.
 */
TEST(inst, set_data_entry_direct)
{
    RecInst r;
    r.setDataEntryHI(9);
    r.setDataEntryLO(8);
    REQUIRE(r.num == 2);
    CHECK_EQ(r.calls[0].a, 1);
    CHECK_EQ(r.calls[1].a, 0);
    CHECK_EQ(r.dataHI(), 9);
    CHECK_EQ(r.dataLO(), 8);
}

/**
 * @brief All sound off (CC 120) and all notes off (CC 123) turn the sustain pedal off and send
 *        noteOff() for all 128 notes, without calling processMIDIShortDataIndividual().
 */
TEST(inst, all_notes_off)
{
    const unsigned char ccs[] = {0x78, 0x7B};
    for (int k = 0; k < 2; k++) {
        RecInst r;
        r.processMIDIShortData(0xB0, ccs[k], 0);
        REQUIRE(r.num == 129);
        CHECK_STREQ(r.calls[0].name, "sustain");
        CHECK_EQ(r.calls[0].a, 0);
        int bad = 0;
        for (int n = 0; n < 128; n++) {
            if (strcmp(r.calls[n + 1].name, "noteOff") != 0 || r.calls[n + 1].a != n) bad++;
        }
        CHECK_EQ(bad, 0);
        CHECK_EQ(r.count("individual"), 0);
    }
}

/**
 * @brief Reset all controllers (CC 121) zeroes pitch bend and mod wheel and sets the NRPN number
 *        to the null value 127/127; data entry and bank are kept.
 */
TEST(inst, reset_all_controllers)
{
    FlagGuard guard;
    RecInst r;
    r.enableBankSelect(1);
    r.processMIDIShortData(0xB0, 0x63, 1);
    r.processMIDIShortData(0xB0, 0x62, 2);
    r.processMIDIShortData(0xB0, 0x06, 3);
    r.processMIDIShortData(0xB0, 0x00, 4);
    r.clear();
    r.processMIDIShortData(0xB0, 0x79, 0);
    REQUIRE(r.num == 2);
    CHECK_STREQ(r.calls[0].name, "pitchBend");
    CHECK_EQ(r.calls[0].a, 0);
    CHECK_STREQ(r.calls[1].name, "modWheel");
    CHECK_EQ(r.calls[1].a, 0);
    CHECK_EQ(r.nrpnMSB(), 127);
    CHECK_EQ(r.nrpnLSB(), 127);
    CHECK_EQ(r.dataHI(), 3);
    CHECK_EQ(r.bankNo(), 4 << 8);
}

/**
 * @brief Other channel mode messages (CC 122, 124..127) do nothing.
 */
TEST(inst, other_channel_mode_messages_ignored)
{
    RecInst r;
    const unsigned char ccs[] = {0x7A, 0x7C, 0x7D, 0x7E, 0x7F};
    for (int k = 0; k < 5; k++) r.processMIDIShortData(0xB0, ccs[k], 0);
    CHECK_EQ(r.num, 0);
}

/**
 * @brief The channel mode messages dispatch to noteOffAll()/resetAllControl() virtually.
 */
TEST(inst, channel_mode_dispatch_is_virtual)
{
    ModeInst m;
    m.processMIDIShortData(0xB0, 0x78, 0);
    m.processMIDIShortData(0xB0, 0x7B, 0);
    m.processMIDIShortData(0xB0, 0x79, 0);
    REQUIRE(m.num == 3);
    CHECK_STREQ(m.calls[0].name, "noteOffAll");
    CHECK_STREQ(m.calls[1].name, "noteOffAll");
    CHECK_STREQ(m.calls[2].name, "resetAll");
}

/**
 * @brief The arpeggiator handlers are not reached by MIDI parsing.
 */
TEST(inst, arp_handlers_not_called_by_parser)
{
    RecInst r;
    r.processMIDIShortData(0x90, 60, 100);
    r.processMIDIShortData(0x80, 60, 0);
    CHECK_EQ(r.count("noteOnFromArp"), 0);
    CHECK_EQ(r.count("noteOffFromArp"), 0);
    r.noteOnFromArp(1, 2);
    CHECK_EQ(r.count("noteOnFromArp"), 1);
}
