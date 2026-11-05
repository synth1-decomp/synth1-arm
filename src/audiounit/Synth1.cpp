/**
 * @file Synth1.cpp
 * @brief The Synth1 Audio Unit: glue between the AU SDK (AUInstrumentBase) and the
 *        techpop synthesizer engine CSynthesizer1.
 *
 * Also defines the AU component entry point and the engine globals (s1gArea)
 * used by the AU side of the plug-in.
 */

#include "Synth1.h"
#include "../techpop/common.h"

CFStringRef g_hBundleID = CFSTR("jp.daichi.synth1.audiounit");
HINSTANCE hInst;

// process-wide globals of the engine (s1g points here)
S1G s1gArea;

AUDIOCOMPONENT_ENTRY(AUMusicDeviceFactory, Synth1)

static const UInt32 kMaxActiveNotes = 8;

const double twopi = 2.0 * 3.14159265358979;

/**
 * @brief Raises a value to the fifth power (waveform of TestNote).
 *
 * @param x     Value.
 * @return x^5.
 */
inline double pow5(double x) { double x2 = x*x; return x2*x2*x; }

//~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
//  Synth1::Synth1
//
// This synth has No inputs, One output
//~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
Synth1::Synth1(AudioUnit inComponentInstance)
    : AUMonotimbralInstrumentBase(inComponentInstance, 0, 1)
{
    CreateElements();

    m_restoreTime = 0;
    m_chunkVer = 113;
    m_midiEventNum = 0;
    m_midiEventMax = 0;
    m_pMidiEvents = NULL;

    s1g = &s1gArea;
    m_pSynth = new CSynthesizer1();
}

Synth1::~Synth1()
{
    if(m_pSynth) delete m_pSynth;
    m_pSynth = NULL;
    if(m_pMidiEvents) free(m_pMidiEvents);
}

OSStatus Synth1::Initialize()
{
    AUMonotimbralInstrumentBase::Initialize();
    SetNotes(kNumNotes, kMaxActiveNotes, mTestNotes, sizeof(TestNote));
    return noErr;
}

OSStatus Synth1::GetParameterList(AudioUnitScope inScope, AudioUnitParameterID* outParameterList, UInt32& outNumParameters)
{
    if(inScope == kAudioUnitScope_Global){
        for(int i=0;i<SYNTH1_PARAM_NUM;i++){
            if(outParameterList) outParameterList[i] = i;
        }
        outNumParameters = SYNTH1_PARAM_NUM;
    }
    return noErr;
}

OSStatus Synth1::GetParameterInfo(AudioUnitScope inScope, AudioUnitParameterID inParameterID, AudioUnitParameterInfo& outParameterInfo)
{
    if(inParameterID >= SYNTH1_PARAM_NUM) return kAudioUnitErr_InvalidParameter;
    if(inScope != kAudioUnitScope_Global) return kAudioUnitErr_InvalidScope;

    outParameterInfo.flags = kAudioUnitParameterFlag_IsReadable;
    for(int i=0;i<g_midiAssignParamsNum;i++){
        if(g_midiAssignParams[i].id == inParameterID){
            outParameterInfo.cfNameString = CFStringCreateWithCString(kCFAllocatorDefault, g_midiAssignParams[i].name, kCFStringEncodingUTF8);
            outParameterInfo.flags |= kAudioUnitParameterFlag_IsWritable | kAudioUnitParameterFlag_HasCFNameString | kAudioUnitParameterFlag_CFNameRelease;
            strcpy(outParameterInfo.name, g_midiAssignParams[i].name);
            outParameterInfo.unit = kAudioUnitParameterUnit_Generic;
            outParameterInfo.minValue = 0.0f;
            outParameterInfo.maxValue = 1.0f;
            outParameterInfo.defaultValue = 0.5f;
            break;
        }
    }
    return noErr;
}

OSStatus Synth1::GetParameter(AudioUnitParameterID inID, AudioUnitScope inScope, AudioUnitElement inElement, AudioUnitParameterValue& outValue)
{
    if(inID < SYNTH1_PARAM_NUM && inScope == kAudioUnitScope_Global){
        outValue = CSynthesizer1::valSyn2Vst(inID, m_pSynth->getCurrentSound()->getParamVal(inID), 113);
        return noErr;
    }
    return AUBase::GetParameter(inID, inScope, inElement, outValue);
}

void Synth1::postUpdate()
{
    if(m_pSynth->m_hNotifyWnd){
        PostMessage(m_pSynth->m_hNotifyWnd, WM_APP + 3, 0, 0);
    }
}

void Synth1::changedBankProgramFromGUI()
{
    CSoundData* pSound = m_pSynth->getCurrentSound();
    AUPreset preset;
    preset.presetNumber = 0;
    preset.presetName = CFStringCreateWithCString(kCFAllocatorDefault, pSound->m_name, kCFStringEncodingUTF8);
    if(SetAFactoryPresetAsCurrent(preset)){
        CFRelease(preset.presetName);
    }
    PropertyChanged(kAudioUnitProperty_FactoryPresets, kAudioUnitScope_Global, 0);
    PropertyChanged(kAudioUnitProperty_PresentPreset, kAudioUnitScope_Global, 0);
}

OSStatus Synth1::SetParameter(AudioUnitParameterID inID, AudioUnitScope inScope, AudioUnitElement inElement, AudioUnitParameterValue inValue, UInt32 inBufferOffsetInFrames)
{
    if(inID < SYNTH1_PARAM_NUM && inScope == kAudioUnitScope_Global){
        if(inID == 0 && inValue == 0.000123f){
            // magic value sent by the view after a bank/program change
            changedBankProgramFromGUI();
        }else{
            int val = CSynthesizer1::valVst2Syn(inID, inValue, 113);
            m_pSynth->setSoundParam(inID, val, 0);
            m_pSynth->m_bParamChanged[inID] = 1;
            if(m_pSynth->m_hNotifyWnd){
                PostMessage(m_pSynth->m_hNotifyWnd, WM_APP + 3, 0, 0);
            }
        }
        return noErr;
    }
    return AUBase::SetParameter(inID, inScope, inElement, inValue, inBufferOffsetInFrames);
}

static void SaveInteger(CFMutableDictionaryRef dict, const void* key, int value);

OSStatus Synth1::SaveState(CFPropertyListRef* outData)
{
    CFMutableDictionaryRef dict = CFDictionaryCreateMutable(kCFAllocatorDefault, 0, &kCFTypeDictionaryKeyCallBacks, &kCFTypeDictionaryValueCallBacks);
    AudioComponentDescription desc = GetComponentDescription();

    SaveInteger(dict, CFSTR(kAUPresetVersionKey), 0);
    SaveInteger(dict, CFSTR(kAUPresetTypeKey), desc.componentType);
    SaveInteger(dict, CFSTR(kAUPresetSubtypeKey), desc.componentSubType);
    SaveInteger(dict, CFSTR(kAUPresetManufacturerKey), desc.componentManufacturer);

    CFMutableDataRef data = CFDataCreateMutable(NULL, 0);
    int size = m_pSynth->makeVSTChunkData(m_chunk, SYNTH1_CHUNK_SIZE);
    CFDataAppendBytes(data, m_chunk, size);
    CFDictionarySetValue(dict, CFSTR(kAUPresetDataKey), data);
    CFRelease(data);

    CSoundData* pSound = m_pSynth->getCurrentSound();
    if(pSound){
        CFStringRef name = CFStringCreateWithCString(kCFAllocatorDefault, pSound->m_name, kCFStringEncodingUTF8);
        if(name){
            CFDictionarySetValue(dict, CFSTR(kAUPresetNameKey), name);
            CFRelease(name);
        }else{
            CFDictionarySetValue(dict, CFSTR(kAUPresetNameKey), CFSTR(""));
        }
    }
    *outData = dict;
    return noErr;
}

/**
 * @brief Stores a 32-bit integer as a CFNumber in a dictionary.
 *
 * @param[in,out] dict  Dictionary to update.
 * @param key           Key (a CFStringRef).
 * @param value         Value to store.
 */
static void SaveInteger(CFMutableDictionaryRef dict, const void* key, int value)
{
    CFNumberRef num = CFNumberCreate(NULL, kCFNumberSInt32Type, &value);
    CFDictionarySetValue(dict, key, num);
    CFRelease(num);
}

OSStatus Synth1::RestoreState(CFPropertyListRef inData)
{
    CFDataRef data = (CFDataRef)CFDictionaryGetValue((CFDictionaryRef)inData, CFSTR(kAUPresetDataKey));
    if(data == NULL) return kAudioUnitErr_InvalidFile;

    int ver = m_pSynth->getChunkVer((unsigned char*)CFDataGetBytePtr(data), CFDataGetLength(data));
    if(ver == -1) return noErr;
    if(ver == 0){
        m_pSynth->setDxiSaveParams((unsigned char*)CFDataGetBytePtr(data));
    }else{
        m_pSynth->setVSTChunkData((unsigned char*)CFDataGetBytePtr(data), CFDataGetLength(data), ver);
    }

    CSoundData* pSound = m_pSynth->getCurrentSound();
    AUPreset preset;
    preset.presetNumber = 0;
    preset.presetName = CFStringCreateWithCString(kCFAllocatorDefault, pSound->m_name, kCFStringEncodingUTF8);
    SetAFactoryPresetAsCurrent(preset);
    CFRelease(preset.presetName);
    PropertyChanged(kAudioUnitProperty_FactoryPresets, kAudioUnitScope_Global, 0);
    PropertyChanged(kAudioUnitProperty_PresentPreset, kAudioUnitScope_Global, 0);
    m_restoreTime = GetTickCount();
    return noErr;
}

void Synth1::Process1(float** outputs, UInt32 inNumberFrames)
{
    float buf[512] __attribute__((aligned(32)));
    int bSilent;
    float* pL = outputs[0];
    float* pR = outputs[1];
    int evNo = 0;
    int pos = 0;

    while((int)inNumberFrames > 0){
        int len = inNumberFrames;
        Synth1MidiEvent* pEvent = NULL;
        if(evNo < m_midiEventNum){
            pEvent = &m_pMidiEvents[evNo];
            len = pEvent->frame - pos;
            if(inNumberFrames < len) len = inNumberFrames;
        }
        int rest = len;
        while(rest > 0){
            int size = (rest > sizeof(buf)/sizeof(float)/2) ? sizeof(buf)/sizeof(float)/2 : rest;
            int n = m_pSynth->processSignalW(size, buf, &bSilent);
            if(bSilent){
                for(int i=0;i<n*2;i+=2){
                    *pL++ = 0;
                    *pR++ = 0;
                }
            }else{
                for(int i=0;i<n*2;i+=2){
                    *pL++ = buf[i];
                    *pR++ = buf[i+1];
                }
            }
            rest -= n;
        }
        inNumberFrames -= len;
        if(pEvent){
            m_pSynth->processMIDIShortData(pEvent->status + pEvent->channel, pEvent->data1, pEvent->data2);
            evNo++;
        }
        pos += len;
    }
    m_midiEventNum = 0;
}

OSStatus Synth1::Render(AudioUnitRenderActionFlags& ioActionFlags, const AudioTimeStamp& inTimeStamp, UInt32 inNumberFrames)
{
    // follow the host tempo
    if(mHostCallbackInfo.beatAndTempoProc){
        Float64 beat, tempo;
        if(mHostCallbackInfo.beatAndTempoProc(mHostCallbackInfo.hostUserData, &beat, &tempo) == noErr && tempo > 0){
            m_pSynth->setBaseTempo(tempo);
        }
    }

    UInt32 numOutputs = Outputs().GetNumberOfElements();
    for(UInt32 i=0;i<numOutputs;i++){
        AudioBufferList& bufferList = GetOutput(i)->GetBufferList();
        if(bufferList.mNumberBuffers == 1){
            float* outputs[2] = { (float*)bufferList.mBuffers[0].mData, NULL };
            Process1(outputs, inNumberFrames);
        }else{
            float* outputs[2] = { (float*)bufferList.mBuffers[0].mData, (float*)bufferList.mBuffers[1].mData };
            Process1(outputs, inNumberFrames);
        }
    }
    mAbsoluteSampleFrame += inNumberFrames;
    return noErr;
}

OSStatus Synth1::HandleMidiEvent(UInt8 inStatus, UInt8 inChannel, UInt8 inData1, UInt8 inData2, UInt32 inStartFrame)
{
    if(inStatus == 0xf0) return noErr;  // sysex

    if(m_midiEventMax <= m_midiEventNum){
        int newMax = (m_midiEventMax > 0) ? m_midiEventMax * 2 : 1;
        Synth1MidiEvent* pNew = (Synth1MidiEvent*)malloc(newMax * sizeof(Synth1MidiEvent));
        for(int i=0;i<m_midiEventNum;i++){
            pNew[i] = m_pMidiEvents[i];
        }
        if(m_pMidiEvents) free(m_pMidiEvents);
        m_pMidiEvents = pNew;
        m_midiEventMax = newMax;
    }
    Synth1MidiEvent* pEvent = &m_pMidiEvents[m_midiEventNum];
    pEvent->status = inStatus;
    pEvent->channel = inChannel;
    pEvent->data1 = inData1;
    pEvent->data2 = inData2;
    pEvent->frame = inStartFrame & ~15;
    m_midiEventNum++;
    return noErr;
}

OSStatus Synth1::ChangeStreamFormat(AudioUnitScope inScope, AudioUnitElement inElement, const CAStreamBasicDescription& inPrevFormat, const CAStreamBasicDescription& inNewFormat)
{
    int samplingFreq = (int)inNewFormat.mSampleRate;
    m_pSynth->setSamplingFreq(samplingFreq);
    return AUBase::ChangeStreamFormat(inScope, inElement, inPrevFormat, inNewFormat);
}

int Synth1::GetNumCustomCocoaUIViews()
{
    return 1;
}

OSStatus Synth1::GetCustomCocoaUIViewInfos(AudioUnitCocoaViewInfo* outInfo)
{
    CFBundleRef bundle = CFBundleGetBundleWithIdentifier(CFSTR("jp.daichi.synth1.audiounit"));
    if(bundle == NULL) return kAudioUnitErr_CannotDoInCurrentContext;
    CFURLRef url = CFBundleCopyResourceURL(bundle, CFSTR("Synth1CocoaViewFactory"), CFSTR("bundle"), NULL);
    if(url == NULL) return kAudioUnitErr_CannotDoInCurrentContext;
    outInfo->mCocoaAUViewBundleLocation = url;
    outInfo->mCocoaAUViewClass[0] = CFSTR("Synth1CocoaViewFactory");
    return noErr;
}

OSStatus Synth1::GetPropertyInfo(AudioUnitPropertyID inID, AudioUnitScope inScope, AudioUnitElement inElement, UInt32& outDataSize, Boolean& outWritable)
{
    if(inScope == kAudioUnitScope_Global){
        switch(inID){
        case kAudioUnitProperty_CocoaUI:
            outDataSize = GetNumCustomCocoaUIViews();
            if(outDataSize == 0) return kAudioUnitErr_InvalidProperty;
            outDataSize *= sizeof(AudioUnitCocoaViewInfo);
            outWritable = false;
            return noErr;
        case kSynth1Property_Synth:
            outDataSize = sizeof(void*) * 2;
            outWritable = false;
            return noErr;
        }
    }
    return MusicDeviceBase::GetPropertyInfo(inID, inScope, inElement, outDataSize, outWritable);
}

OSStatus Synth1::GetProperty(AudioUnitPropertyID inID, AudioUnitScope inScope, AudioUnitElement inElement, void* outData)
{
    if(inScope == kAudioUnitScope_Global){
        switch(inID){
        case kSynth1Property_Synth:
            ((void**)outData)[1] = s1g;
            ((void**)outData)[0] = m_pSynth;
            return noErr;
        case kAudioUnitProperty_CocoaUI:
            return GetCustomCocoaUIViewInfos((AudioUnitCocoaViewInfo*)outData);
        }
    }
    return MusicDeviceBase::GetProperty(inID, inScope, inElement, outData);
}

CFURLRef Synth1::CopyIconLocation()
{
    CFBundleRef bundle = CFBundleGetBundleWithIdentifier(CFSTR("jp.daichi.synth1.audiounit.ui"));
    return CFBundleCopyResourceURL(bundle, CFSTR("s1_07.png"), NULL, NULL);
}

#pragma mark TestNote Methods

OSStatus TestNote::Render(UInt64 inAbsoluteSampleFrame, UInt32 inNumFrames, AudioBufferList** inBufferList, UInt32 inOutBusCount)
{
    float *left, *right;
    float globalVol = GetGlobalParameter(0);

    // TestNote only writes into the first bus regardless of what is handed to us.
    const int bus0 = 0;
    int numChans = inBufferList[bus0]->mNumberBuffers;
    if (numChans > 2) return -1;

    left = (float*)inBufferList[bus0]->mBuffers[0].mData;
    right = numChans == 2 ? (float*)inBufferList[bus0]->mBuffers[1].mData : 0;

    double sampleRate = SampleRate();
    double freq = Frequency() * (twopi/sampleRate);

    switch (GetState())
    {
        case kNoteState_Attacked :
        case kNoteState_Sostenutoed :
        case kNoteState_ReleasedButSostenutoed :
        case kNoteState_ReleasedButSustained :
            {
                for (UInt32 frame=0; frame<inNumFrames; ++frame)
                {
                    if (amp < maxamp) amp += up_slope;
                    float out = pow5(sin(phase)) * amp * globalVol;
                    phase += freq;
                    if (phase > twopi) phase -= twopi;
                    left[frame] += out;
                    if (right) right[frame] += out;
                }
            }
            break;
        case kNoteState_Released :
            {
                UInt32 endFrame = 0xFFFFFFFF;
                for (UInt32 frame=0; frame<inNumFrames; ++frame)
                {
                    if (amp > 0.0) amp += dn_slope;
                    else if (endFrame == 0xFFFFFFFF) endFrame = frame;
                    float out = pow5(sin(phase)) * amp * globalVol;
                    phase += freq;
                    left[frame] += out;
                    if (right) right[frame] += out;
                }
                if (endFrame != 0xFFFFFFFF) {
                    NoteEnded(endFrame);
                }
            }
            break;
        case kNoteState_FastReleased :
            {
                UInt32 endFrame = 0xFFFFFFFF;
                for (UInt32 frame=0; frame<inNumFrames; ++frame)
                {
                    if (amp > 0.0) amp += fast_dn_slope;
                    else if (endFrame == 0xFFFFFFFF) endFrame = frame;
                    float out = pow5(sin(phase)) * amp * globalVol;
                    phase += freq;
                    left[frame] += out;
                    if (right) right[frame] += out;
                }
                if (endFrame != 0xFFFFFFFF) {
                    NoteEnded(endFrame);
                }
            }
            break;
        default :
            break;
    }
    return noErr;
}
