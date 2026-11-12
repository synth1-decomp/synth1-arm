/**
 * @file synth1vst3.cpp
 * @brief The Synth1 VST3 plug-in (see synth1vst3.h).
 */
#include "synth1vst3.h"
#include "synth1vst3_hooks.h"

#include "pluginterfaces/base/ustring.h"
#include "pluginterfaces/vst/ivstparameterchanges.h"

#include "../windef.h"
#include "../techpop/common.h"
#include "../techpop/synth1.h"

using namespace Steinberg;
using namespace Steinberg::Vst;

// process-wide globals of the engine (s1g points here); the AU defines its own in Synth1.cpp
S1G s1gArea;

namespace Synth1 {

Synth1Vst3::Synth1Vst3()
    : m_pSynth(nullptr)
{
}

Synth1Vst3::~Synth1Vst3()
{
    delete m_pSynth;
}

FUnknown* Synth1Vst3::createInstance(void*)
{
    return static_cast<IAudioProcessor*>(new Synth1Vst3());
}

tresult PLUGIN_API Synth1Vst3::initialize(FUnknown* context)
{
    tresult result = SingleComponentEffect::initialize(context);
    if (result != kResultOk) return result;

    // as in the AU constructor (Synth1::Synth1)
    s1g = &s1gArea;
    m_pSynth = new CSynthesizer1();

    addEventInput(STR16("MIDI In"), 16);
    addAudioOutput(STR16("Stereo Out"), SpeakerArr::kStereo);

    // The VST2's parameters: 0..98 named from the MIDI assign table (unnamed ids are
    // kept, read-only and hidden, so the numbering stays the VST2's) and the master volume.
    for (ParamID id = 0; id < kNumSoundParams; id++) {
        char name[64];
        UString128 title;
        int32 flags = ParameterInfo::kCanAutomate;
        if (CSynthesizer1::getParamName(id, name)) {
            title.fromAscii(name);
        } else {
            snprintf(name, sizeof(name), "param %u", id);
            title.fromAscii(name);
            flags = ParameterInfo::kIsReadOnly | ParameterInfo::kIsHidden;
        }
        parameters.addParameter(title, nullptr, 0, engineValue(id), flags, id);
    }
    parameters.addParameter(STR16("master volume"), nullptr, 0, engineValue(kParamMasterVolume),
                            ParameterInfo::kCanAutomate, kParamMasterVolume);
    return kResultOk;
}

tresult PLUGIN_API Synth1Vst3::terminate()
{
    delete m_pSynth;
    m_pSynth = nullptr;
    return SingleComponentEffect::terminate();
}

tresult PLUGIN_API Synth1Vst3::setBusArrangements(SpeakerArrangement* inputs, int32 numIns,
                                                  SpeakerArrangement* outputs, int32 numOuts)
{
    // no audio inputs, one stereo output
    if (numIns == 0 && numOuts == 1 && outputs[0] == SpeakerArr::kStereo) {
        return SingleComponentEffect::setBusArrangements(inputs, numIns, outputs, numOuts);
    }
    return kResultFalse;
}

tresult PLUGIN_API Synth1Vst3::canProcessSampleSize(int32 symbolicSampleSize)
{
    return symbolicSampleSize == kSample32 ? kResultTrue : kResultFalse;
}

tresult PLUGIN_API Synth1Vst3::setupProcessing(ProcessSetup& setup)
{
    m_pSynth->setSamplingFreq((int)setup.sampleRate);
    return SingleComponentEffect::setupProcessing(setup);
}

tresult PLUGIN_API Synth1Vst3::process(ProcessData& data)
{
    // Skeleton: silence. Rendering, MIDI and parameter input come in the next step.
    for (int32 bus = 0; bus < data.numOutputs; bus++) {
        AudioBusBuffers& out = data.outputs[bus];
        for (int32 ch = 0; ch < out.numChannels; ch++) {
            if (out.channelBuffers32 && out.channelBuffers32[ch]) {
                memset(out.channelBuffers32[ch], 0, data.numSamples * sizeof(float));
            }
        }
        out.silenceFlags = ((uint64)1 << out.numChannels) - 1;
    }
    return kResultOk;
}

ParamValue Synth1Vst3::engineValue(ParamID id) const
{
    if (id == kParamMasterVolume) {
        return CSynthesizer1::valSyn2Vst(id, m_pSynth->m_volumeVal, 113);
    }
    return CSynthesizer1::valSyn2Vst(id, m_pSynth->getCurrentSound()->getParamVal(id), 113);
}

void Synth1Vst3::paramEditedFromGUI(int id)
{
    // The GUI reports a master volume change as id 0, like the AU build does (synwin.cpp),
    // so for id 0 the master volume is checked too.
    ParamID ids[2] = { (ParamID)id, kParamMasterVolume };
    int n = (id == 0) ? 2 : 1;
    for (int i = 0; i < n; i++) {
        Parameter* param = parameters.getParameter(ids[i]);
        if (param == nullptr || (param->getInfo().flags & ParameterInfo::kIsReadOnly)) continue;
        ParamValue value = engineValue(ids[i]);
        if (value == param->getNormalized()) continue;
        param->setNormalized(value);
        beginEdit(ids[i]);
        performEdit(ids[i], value);
        endEdit(ids[i]);
    }
}

void Synth1Vst3::programChangedFromGUI()
{
    for (int32 i = 0; i < parameters.getParameterCount(); i++) {
        Parameter* param = parameters.getParameterByIndex(i);
        param->setNormalized(engineValue(param->getInfo().id));
    }
    if (componentHandler) componentHandler->restartComponent(kParamValuesChanged);
}

} // namespace Synth1

//------------------------------------------------------------------------
// Hooks called by the shared GUI code (synth1vst3_hooks.h)

extern "C" void Synth1Vst3_paramEdited(void* pPlugin, int id)
{
    static_cast<Synth1::Synth1Vst3*>(pPlugin)->paramEditedFromGUI(id);
}

extern "C" void Synth1Vst3_programChanged(void* pPlugin)
{
    static_cast<Synth1::Synth1Vst3*>(pPlugin)->programChangedFromGUI();
}
