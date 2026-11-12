/**
 * @file synth1vst3.h
 * @brief The Synth1 VST3 plug-in: glue between the VST3 SDK and the techpop engine
 *        CSynthesizer1, the VST3 counterpart of audiounit/Synth1.cpp.
 *
 * Processor and edit controller are one object (SingleComponentEffect), like the AU,
 * because the GUI works on the engine directly.
 */
#ifndef __SYNTH1VST3_H__
#define __SYNTH1VST3_H__

#include "public.sdk/source/vst/vstsinglecomponenteffect.h"
#include "pluginterfaces/vst/ivsteditcontroller.h"
#include "pluginterfaces/vst/ivstmidicontrollers.h"

#include <vector>

class CSynthesizer1;

namespace Synth1 {

/**
 * @brief Parameter ids of the original VST2 Synth1 ("Synth1 VSTi", unique id 'S1Vs').
 *
 * The VST2 had 255 parameters. Indices 0..98 are the patch parameters (engine ids) and
 * 254 is the master volume; 250..253 report the patch version, bank and program. The
 * VST3 uses the same ids, so automation written against the VST2 lands on the same
 * parameters.
 */
enum : Steinberg::Vst::ParamID {
    kNumSoundParams = 99,       ///< ids 0..98: patch parameters
    kParamMasterVolume = 254,   ///< master volume (CSynthesizer1::setCommonParam)

    /**
     * Hidden parameters that carry MIDI controllers, which VST3 hosts do not send as events:
     * kMidiProxyBase + n for CC n (0..127), Steinberg::Vst::kAfterTouch (channel pressure) and
     * Steinberg::Vst::kPitchBend. One set serves all channels; the engine ignores the channel.
     */
    kMidiProxyBase = 1000,
    kNumMidiProxies = Steinberg::Vst::kCountCtrlNumber,
};

/**
 * @brief Class id (FUID) of the plug-in, derived from the VST2 plug-in so hosts can
 *        replace the VST2 with this VST3.
 *
 * Built like Steinberg's VST2 wrapper does it: "VST", the VST2 unique id 'S1Vs' and the first
 * nine characters of the VST2 effect name ("Synth1 VSTi", read from the original
 * Synth1 VST64.dll), lower-cased:
 * 56 53 54 | 53 31 56 73 | 73 79 6E 74 68 31 20 76 73  ("VST" 'S1Vs' "synth1 vs").
 */
static const Steinberg::FUID kClassID(0x56535453, 0x31567373, 0x796E7468, 0x31207673);

/**
 * @brief The plug-in.
 */
class Synth1Vst3 : public Steinberg::Vst::SingleComponentEffect, public Steinberg::Vst::IMidiMapping
{
public:
    Synth1Vst3();
    ~Synth1Vst3() SMTG_OVERRIDE;

    /** @brief Factory function (createInstance of the class factory). */
    static Steinberg::FUnknown* createInstance(void*);

    // IPluginBase
    Steinberg::tresult PLUGIN_API initialize(Steinberg::FUnknown* context) SMTG_OVERRIDE;
    Steinberg::tresult PLUGIN_API terminate() SMTG_OVERRIDE;

    // IAudioProcessor
    Steinberg::tresult PLUGIN_API setBusArrangements(Steinberg::Vst::SpeakerArrangement* inputs, Steinberg::int32 numIns,
                                                     Steinberg::Vst::SpeakerArrangement* outputs, Steinberg::int32 numOuts) SMTG_OVERRIDE;
    Steinberg::tresult PLUGIN_API canProcessSampleSize(Steinberg::int32 symbolicSampleSize) SMTG_OVERRIDE;
    Steinberg::tresult PLUGIN_API setupProcessing(Steinberg::Vst::ProcessSetup& setup) SMTG_OVERRIDE;
    Steinberg::tresult PLUGIN_API process(Steinberg::Vst::ProcessData& data) SMTG_OVERRIDE;

    // IMidiMapping
    Steinberg::tresult PLUGIN_API getMidiControllerAssignment(Steinberg::int32 busIndex, Steinberg::int16 channel,
                                                              Steinberg::Vst::CtrlNumber midiControllerNumber,
                                                              Steinberg::Vst::ParamID& id) SMTG_OVERRIDE;

    OBJ_METHODS(Synth1Vst3, SingleComponentEffect)
    DEFINE_INTERFACES
        DEF_INTERFACE(Steinberg::Vst::IMidiMapping)
    END_DEFINE_INTERFACES(SingleComponentEffect)
    REFCOUNT_METHODS(SingleComponentEffect)

    /** @brief The engine of this instance. */
    CSynthesizer1* synth() const { return m_pSynth; }

    /**
     * @brief Reports a parameter edited in the GUI to the host (Synth1Vst3_paramEdited).
     *
     * @param id    Parameter id as the GUI reports it (see the .cpp for id 0).
     */
    void paramEditedFromGUI(int id);

    /**
     * @brief Re-reads every parameter after a bank/program change in the GUI
     *        (Synth1Vst3_programChanged).
     */
    void programChangedFromGUI();

private:
    /**
     * @brief MIDI message for the engine, at a sample offset in the current block
     *        (the AU's Synth1MidiEvent).
     */
    struct MidiEvent {
        unsigned char status, data1, data2;
        int frame;
    };

    /** @brief Applies the parameter changes of a block and turns MIDI proxy changes into m_events. */
    void applyParameterChanges(Steinberg::Vst::IParameterChanges* changes);

    /** @brief Appends the block's note events to m_events. */
    void collectEvents(Steinberg::Vst::IEventList* events);

    /**
     * @brief Renders a block, playing m_events at their offsets (Synth1::Process1).
     *
     * @return Nonzero if the whole block was silent.
     */
    int render(float* pL, float* pR, int numFrames);

    /**
     * @brief Returns a parameter's current value from the engine, normalized like the VST2.
     *
     * @param id    Parameter id (0..98 or kParamMasterVolume).
     * @return Normalized value 0..1.
     */
    Steinberg::Vst::ParamValue engineValue(Steinberg::Vst::ParamID id) const;

    CSynthesizer1* m_pSynth;
    std::vector<MidiEvent> m_events;    ///< MIDI of the current block, sorted by frame
};

} // namespace Synth1

#endif
