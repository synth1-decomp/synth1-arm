/**
 * @file inst.cpp
 * @brief CInst: MIDI message parser / instrument interface.
 *
 * Also defines the process-wide bank-select and program-change enable flags
 * shared by all instances.
 */

#include "inst.h"

int CInst::s_bEnableBankSelect = 1;
int CInst::s_bEnableProgChange = 1;

CInst::CInst()
{
    m_NRPNNoMSB = 0;
    m_NRPNNoLSB = 0;
    m_dataEntryLO = 0;
    m_dataEntryHI = 0;
    m_pitchBendRange = 12;
    m_bankNo = 0;
    m_pbEnableBankSelect = &s_bEnableBankSelect;
    m_pbEnableProgChange = &s_bEnableProgChange;
}

void CInst::enableBankSelect(int b)
{
    *m_pbEnableBankSelect = b;
}

void CInst::enableProgramChange(int b)
{
    *m_pbEnableProgChange = b;
}

int CInst::getEnableBankSelect()
{
    return *m_pbEnableBankSelect;
}

int CInst::getEnableProgramChange()
{
    return *m_pbEnableProgChange;
}

void CInst::processMIDIShortData(unsigned char st, unsigned char dt1, unsigned char dt2)
{
    switch (st & 0xF0) {
    case 0x90:  // note on
        if (dt2 != 0) {
            if (dt1 > 127) return;
            noteOn(dt1, dt2);
            return;
        }
        // velocity 0 : note off
    case 0x80:  // note off
        if (dt1 > 127) return;
        noteOff(dt1);
        return;

    case 0xB0:
        if (dt1 < 120) {
            // control change
            switch (dt1) {
            case 0x00:  // bank select MSB
                if (*m_pbEnableBankSelect) {
                    m_bankNo = (short)((unsigned char)m_bankNo | (dt2 << 8));
                    receiveBankChange(m_bankNo);
                }
                break;
            case 0x20:  // bank select LSB
                if (*m_pbEnableBankSelect) {
                    m_bankNo = (short)(dt2 | (m_bankNo & 0xFF00));
                    receiveBankChange(m_bankNo);
                }
                break;
            case 0x01:  // modulation wheel
                setModulationWheel(dt2);
                break;
            case 0x06:  // data entry MSB
                setDataEntryHI(dt2);
                break;
            case 0x26:  // data entry LSB
                setDataEntryLO(dt2);
                break;
            case 0x07:  // volume
                setVolume(dt2);
                break;
            case 0x40:  // damper pedal
                setSustainEnable(dt2 >= 64);
                break;
            case 0x62:  // NRPN LSB
                m_NRPNNoLSB = dt2;
                break;
            case 0x63:  // NRPN MSB
                m_NRPNNoMSB = dt2;
                break;
            }
            processMIDIShortDataIndividual(st, dt1, dt2);
        } else {
            // channel mode messages
            switch (dt1) {
            case 0x78:  // all sound off
            case 0x7B:  // all note off
                noteOffAll();
                break;
            case 0x79:  // reset all controllers
                resetAllControl();
                break;
            }
        }
        return;

    case 0xC0:  // program change
        if (*m_pbEnableProgChange) {
            receiveProgramChange(dt1);
        }
        return;

    case 0xD0:  // channel pressure
        processMIDIShortDataIndividual(st, dt1, dt2);
        return;

    case 0xE0:  // pitch bend
        setPitchBend((short)(dt2 * 128 + dt1 - 0x2000));
        processMIDIShortDataIndividual(st, dt1, dt2);
        return;
    }
}

void CInst::setDataEntryHI(unsigned char v)
{
    m_dataEntryHI = v;
    changedNRPNData(1);
}

void CInst::setDataEntryLO(unsigned char v)
{
    m_dataEntryLO = v;
    changedNRPNData(0);
}

void CInst::changedNRPNData(int bHI)
{
}

void CInst::resetAllControl()
{
    setPitchBend(0);
    setModulationWheel(0);
    m_NRPNNoMSB = 127;
    m_NRPNNoLSB = 127;
}

void CInst::noteOffAll()
{
    setSustainEnable(0);
    for (int i = 0; i < 128; i++) {
        noteOff(i);
    }
}
