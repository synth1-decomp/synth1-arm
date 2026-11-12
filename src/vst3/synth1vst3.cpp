/**
 * @file synth1vst3.cpp
 * @brief The Synth1 VST3 plug-in (see synth1vst3.h).
 */
#include "synth1vst3.h"
#include "synth1vst3_hooks.h"
#include "synth1vst3view.h"

#include "pluginterfaces/base/ibstream.h"
#include "pluginterfaces/base/ustring.h"
#include "pluginterfaces/vst/ivstevents.h"
#include "pluginterfaces/vst/ivstparameterchanges.h"
#include "pluginterfaces/vst/ivstprocesscontext.h"
#include "public.sdk/source/common/memorystream.h"
#include "public.sdk/source/vst/utility/vst2persistence.h"

#include <algorithm>

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

    // MIDI controller proxies (getMidiControllerAssignment); hidden, not automatable
    for (int32 n = 0; n < kNumMidiProxies; n++) {
        char name[32];
        if (n == kAfterTouch) snprintf(name, sizeof(name), "MIDI channel pressure");
        else if (n == kPitchBend) snprintf(name, sizeof(name), "MIDI pitch bend");
        else snprintf(name, sizeof(name), "MIDI CC %d", (int)n);
        UString128 title;
        title.fromAscii(name);
        parameters.addParameter(title, nullptr, 0, (n == kPitchBend) ? 0.5 : 0.0,
                                ParameterInfo::kIsHidden, kMidiProxyBase + n);
    }

    processContextRequirements.needTempo();
    return kResultOk;
}

tresult PLUGIN_API Synth1Vst3::terminate()
{
    delete m_pSynth;
    m_pSynth = nullptr;
    return SingleComponentEffect::terminate();
}

// The engine's state (VSTChunk in techpop/synth1.cpp): a header string, then 0xA40 bytes in all.
static const char kChunkHeader[] = "Synth1 VST Chunk Data";
static const int kChunkSize = 0xA40;
static const int kMinStateSize = 0x20;         // the header field; getChunkVer() strcmp()s it
static const int kStateBufferSize = 0x1000;    // SYNTH1_CHUNK_SIZE of the AU
static const int32 kVst2UniqueID = 'S1Vs';     // the original VST2's unique id

tresult PLUGIN_API Synth1Vst3::getState(IBStream* state)
{
    if (state == nullptr) return kInvalidArgument;
    std::vector<unsigned char> buf(kStateBufferSize);
    int size = m_pSynth->makeVSTChunkData(buf.data(), (int)buf.size());
    int32 written = 0;
    if (state->write(buf.data(), size, &written) != kResultOk || written != size) return kResultFalse;
    return kResultOk;
}

tresult PLUGIN_API Synth1Vst3::setState(IBStream* state)
{
    if (state == nullptr) return kInvalidArgument;
    std::vector<unsigned char> data;
    unsigned char buf[4096];
    int32 n = 0;
    while (state->read(buf, sizeof(buf), &n) == kResultOk && n > 0) {
        data.insert(data.end(), buf, buf + n);
    }

    std::vector<unsigned char> chunk;
    if (!findChunk(data, chunk)) return kResultFalse;

    // Synth1::RestoreState
    int ver = m_pSynth->getChunkVer(chunk.data(), (int)chunk.size());
    if (ver == -1) return kResultFalse;
    if (ver == 0) {
        m_pSynth->setDxiSaveParams(chunk.data());
    } else {
        m_pSynth->setVSTChunkData(chunk.data(), (int)chunk.size(), ver);
    }

    // the host reads the restored values back from the controller part
    for (ParamID id = 0; id < kNumSoundParams; id++) {
        if (Parameter* param = parameters.getParameter(id)) param->setNormalized(engineValue(id));
    }
    if (Parameter* param = parameters.getParameter(kParamMasterVolume)) {
        param->setNormalized(engineValue(kParamMasterVolume));
    }
    return kResultOk;
}

bool Synth1Vst3::findChunk(const std::vector<unsigned char>& data, std::vector<unsigned char>& chunk) const
{
    // the engine's own formats, as the VST2 and the AU save them
    if (data.size() >= kMinStateSize && m_pSynth->getChunkVer((unsigned char*)data.data(), (int)data.size()) != -1) {
        chunk = data;
        return true;
    }

    // a VST2 bank or program holding the chunk (hosts that migrate VST2 projects pass these)
    auto fromVst2 = [&](const VST3::Vst2xChunk& c) {
        chunk.assign((const unsigned char*)c.data(), (const unsigned char*)c.data() + c.size());
        return chunk.size() >= kMinStateSize && m_pSynth->getChunkVer(chunk.data(), (int)chunk.size()) != -1;
    };
    {
        MemoryStream stream((void*)data.data(), (TSize)data.size());
        auto bank = VST3::tryVst2StateLoad(stream, VST3::Optional<int32_t>(kVst2UniqueID));
        if (bank && !bank->chunk.empty() && fromVst2(bank->chunk)) return true;
    }
    {
        MemoryStream stream((void*)data.data(), (TSize)data.size());
        auto program = VST3::tryVst2ProgramLoad(stream, VST3::Optional<int32_t>(kVst2UniqueID));
        if (program && !program->chunk.empty() && fromVst2(program->chunk)) return true;
    }

    // a host's own header in front of the chunk (FL Studio puts 21 bytes there): the whole chunk
    // must follow its header string, near the start
    for (size_t off = 1; off < 256 && off + kChunkSize <= data.size(); off++) {
        if (memcmp(&data[off], kChunkHeader, sizeof(kChunkHeader)) == 0) {
            chunk.assign(data.begin() + off, data.begin() + off + kChunkSize);
            return m_pSynth->getChunkVer(chunk.data(), (int)chunk.size()) > 0;
        }
    }
    return false;
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
    // Only on a change, like the AU (AUBase calls ChangeStreamFormat only for a different
    // format): setSamplingFreq() reinitializes engine state, so calling it again at the current
    // rate would make the first notes sound different from the AU.
    if ((int)setup.sampleRate != m_pSynth->m_samplingFreq) {
        m_pSynth->setSamplingFreq((int)setup.sampleRate);
    }
    m_events.reserve(1024);     // grows in process() only if a block brings more
    return SingleComponentEffect::setupProcessing(setup);
}

tresult PLUGIN_API Synth1Vst3::process(ProcessData& data)
{
    // follow the host tempo (Synth1::Render)
    if (data.processContext && (data.processContext->state & ProcessContext::kTempoValid) &&
        data.processContext->tempo > 0) {
        m_pSynth->setBaseTempo((float)data.processContext->tempo);
    }

    m_events.clear();
    applyParameterChanges(data.inputParameterChanges);
    collectEvents(data.inputEvents);
    std::stable_sort(m_events.begin(), m_events.end(),
                     [](const MidiEvent& a, const MidiEvent& b) { return a.frame < b.frame; });

    if (data.numOutputs == 0 || data.outputs[0].numChannels < 2 || data.numSamples <= 0) {
        // no audio to render (e.g. a parameter flush): still play the MIDI
        for (const MidiEvent& ev : m_events) {
            m_pSynth->processMIDIShortData(ev.status, ev.data1, ev.data2);
        }
        return kResultOk;
    }

    AudioBusBuffers& out = data.outputs[0];
    int silent = render(out.channelBuffers32[0], out.channelBuffers32[1], data.numSamples);
    out.silenceFlags = silent ? 3 : 0;
    return kResultOk;
}

void Synth1Vst3::applyParameterChanges(IParameterChanges* changes)
{
    if (changes == nullptr) return;
    for (int32 i = 0; i < changes->getParameterCount(); i++) {
        IParamValueQueue* queue = changes->getParameterData(i);
        if (queue == nullptr || queue->getPointCount() <= 0) continue;
        ParamID id = queue->getParameterId();
        int32 offset;
        ParamValue value;

        if (id >= kMidiProxyBase && id < kMidiProxyBase + kNumMidiProxies) {
            // every point becomes a MIDI message at its offset
            int32 n = id - kMidiProxyBase;
            for (int32 p = 0; p < queue->getPointCount(); p++) {
                if (queue->getPoint(p, offset, value) != kResultOk) continue;
                MidiEvent ev;
                ev.frame = offset & ~15;    // as Synth1::HandleMidiEvent
                if (n == kPitchBend) {
                    int v = std::min(16383, std::max(0, (int)(value * 16383.0 + 0.5)));
                    ev.status = 0xE0;
                    ev.data1 = v & 0x7F;
                    ev.data2 = v >> 7;
                } else {
                    int v = std::min(127, std::max(0, (int)(value * 127.0 + 0.5)));
                    ev.status = (n == kAfterTouch) ? 0xD0 : 0xB0;
                    ev.data1 = (n == kAfterTouch) ? v : n;
                    ev.data2 = (n == kAfterTouch) ? 0 : v;
                }
                m_events.push_back(ev);
            }
            continue;
        }

        // Synth1 parameters take the block's last value (as the AU and the VST2 apply a host
        // value immediately)
        if (queue->getPoint(queue->getPointCount() - 1, offset, value) != kResultOk) continue;
        if (id < kNumSoundParams) {
            // Synth1::SetParameter
            m_pSynth->setSoundParam(id, CSynthesizer1::valVst2Syn(id, (float)value, 113), 0);
        } else if (id == kParamMasterVolume) {
            // The VST2 (setParameter, index 254) converted 0.0 and values >= 1.0 by truncation,
            // which made 1.0 the quietest setting; here the whole range uses valVst2Syn.
            m_pSynth->setCommonParam(id, CSynthesizer1::valVst2Syn(id, (float)value, 113), 0);
        } else {
            continue;
        }
        m_pSynth->m_bParamChanged[id] = 1;
        if (m_pSynth->m_hNotifyWnd) {
            PostMessage(m_pSynth->m_hNotifyWnd, WM_APP + 3, 0, 0);
        }
    }
}

void Synth1Vst3::collectEvents(IEventList* events)
{
    if (events == nullptr) return;
    for (int32 i = 0; i < events->getEventCount(); i++) {
        Event e;
        if (events->getEvent(i, e) != kResultOk) continue;
        MidiEvent ev;
        ev.frame = e.sampleOffset & ~15;    // as Synth1::HandleMidiEvent
        switch (e.type) {
        case Event::kNoteOnEvent:
            if (e.noteOn.pitch < 0 || e.noteOn.pitch > 127) continue;
            ev.status = 0x90;
            ev.data1 = (unsigned char)e.noteOn.pitch;
            // a note-on always has a velocity of at least 1 (0 would be a note-off in MIDI)
            ev.data2 = (unsigned char)std::min(127, std::max(1, (int)(e.noteOn.velocity * 127.0f + 0.5f)));
            break;
        case Event::kNoteOffEvent:
            if (e.noteOff.pitch < 0 || e.noteOff.pitch > 127) continue;
            ev.status = 0x80;
            ev.data1 = (unsigned char)e.noteOff.pitch;
            ev.data2 = (unsigned char)std::min(127, std::max(0, (int)(e.noteOff.velocity * 127.0f + 0.5f)));
            break;
        case Event::kPolyPressureEvent:
            if (e.polyPressure.pitch < 0 || e.polyPressure.pitch > 127) continue;
            ev.status = 0xA0;
            ev.data1 = (unsigned char)e.polyPressure.pitch;
            ev.data2 = (unsigned char)std::min(127, std::max(0, (int)(e.polyPressure.pressure * 127.0f + 0.5f)));
            break;
        default:
            continue;   // sysex and the rest are ignored, as in the AU
        }
        m_events.push_back(ev);
    }
}

int Synth1Vst3::render(float* pL, float* pR, int numFrames)
{
    // Synth1::Process1: render up to each event, then play it
    float buf[512] __attribute__((aligned(32)));
    int bSilent;
    int allSilent = 1;
    size_t evNo = 0;
    int pos = 0;

    while (numFrames > 0) {
        int len = numFrames;
        const MidiEvent* pEvent = NULL;
        if (evNo < m_events.size()) {
            pEvent = &m_events[evNo];
            len = pEvent->frame - pos;
            if (numFrames < len) len = numFrames;
        }
        int rest = len;
        while (rest > 0) {
            int size = (rest > (int)(sizeof(buf) / sizeof(float) / 2)) ? (int)(sizeof(buf) / sizeof(float) / 2) : rest;
            int n = m_pSynth->processSignalW(size, buf, &bSilent);
            if (bSilent) {
                for (int i = 0; i < n * 2; i += 2) {
                    *pL++ = 0;
                    *pR++ = 0;
                }
            } else {
                allSilent = 0;
                for (int i = 0; i < n * 2; i += 2) {
                    *pL++ = buf[i];
                    *pR++ = buf[i + 1];
                }
            }
            rest -= n;
        }
        numFrames -= len;
        if (pEvent) {
            m_pSynth->processMIDIShortData(pEvent->status, pEvent->data1, pEvent->data2);
            evNo++;
        }
        pos += len;
    }
    return allSilent;
}

IPlugView* PLUGIN_API Synth1Vst3::createView(FIDString name)
{
    if (name && strcmp(name, ViewType::kEditor) == 0) return new Synth1Vst3View(this);
    return nullptr;
}

tresult PLUGIN_API Synth1Vst3::getMidiControllerAssignment(int32 busIndex, int16 /*channel*/,
                                                         CtrlNumber midiControllerNumber, ParamID& id)
{
    if (busIndex != 0 || midiControllerNumber < 0 || midiControllerNumber >= kNumMidiProxies) {
        return kResultFalse;
    }
    id = kMidiProxyBase + midiControllerNumber;
    return kResultTrue;
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
