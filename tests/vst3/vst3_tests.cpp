/**
 * @file vst3_tests.cpp
 * @brief Black-box tests of the built Synth1 VST3 plug-in (src/vst3/).
 *
 * Loads Synth1.vst3 with CFBundle (bundleEntry / GetPluginFactory) and drives it only through
 * the VST3 interfaces, the way a host does: the class factory, buses, parameters, the MIDI
 * controller mapping (IMidiMapping), rendering with note events, parameter changes and the MIDI
 * controller proxy parameters, the plug-in state (getState / setState), including state saved
 * by the original VST2 in the forms hosts hand it over, and the editor (attached to an
 * off-screen window).
 *
 * The golden scenarios of the Audio Unit tests (tests/au/au_tests.c) are replayed through the
 * VST3 interfaces after restoring the same default patch, and must reproduce the AU's golden
 * hashes (tests/au/reference.txt, generated from the original release).
 *
 * Usage:
 *   vst3_tests <Synth1.vst3> <tests folder> [filter ...]
 *   vst3_tests --child <Synth1.vst3> <tests folder> scenario <index>   (internal)
 *
 * <tests folder> is the repository's tests/ folder (au/reference.txt, au/default_state.plist and
 * data/vst3/). As in the AU tests, each golden scenario renders in a fresh child process (the
 * engine's random generator is process-wide), and the scripts run the host with
 * tests/au/au_isolation.c loaded, so the plug-in sees default settings and a temporary folder
 * instead of ~/Documents.
 */

#include <CoreFoundation/CoreFoundation.h>
#include <mach-o/dyld.h>
#include <spawn.h>
#include <sys/wait.h>
#include <unistd.h>

#include <algorithm>
#include <cmath>
#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

#include "pluginterfaces/base/ipluginbase.h"
#include "pluginterfaces/base/funknownimpl.h"
#include "pluginterfaces/vst/ivstaudioprocessor.h"
#include "pluginterfaces/vst/ivstcomponent.h"
#include "pluginterfaces/vst/ivsteditcontroller.h"
#include "pluginterfaces/vst/ivstevents.h"
#include "pluginterfaces/vst/ivstmidicontrollers.h"
#include "public.sdk/source/common/memorystream.h"
#include "public.sdk/source/vst/hosting/eventlist.h"
#include "public.sdk/source/vst/hosting/parameterchanges.h"
#include "pluginterfaces/gui/iplugview.h"

#include "view_host.h"

#include "../au/golden_scenarios.h"

using namespace Steinberg;
using namespace Steinberg::Vst;

extern char** environ;

/* ------------------------------------------------------------------------------------------ */
/* minimal test framework (same output format as the AU tests)                                */

static int g_checksOk, g_checksFailed, g_testFailed, g_tests, g_testsFailed;

static void fail_at(const char* file, int line, const char* fmt, ...) __attribute__((format(printf, 3, 4)));
static void fail_at(const char* file, int line, const char* fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    printf("    %s:%d: ", file, line);
    vprintf(fmt, ap);
    printf("\n");
    va_end(ap);
    g_testFailed = 1;
    g_checksFailed++;
}

#define CHECK(c) do { if (c) g_checksOk++; else fail_at(__FILE__, __LINE__, "CHECK(%s)", #c); } while (0)
#define CHECK_EQ(a, b) do { long long a_ = (long long)(a), b_ = (long long)(b); \
    if (a_ == b_) g_checksOk++; else fail_at(__FILE__, __LINE__, "%s == %s (%lld vs %lld)", #a, #b, a_, b_); } while (0)
#define CHECK_STREQ(a, b) do { std::string a_ = (a), b_ = (b); \
    if (a_ == b_) g_checksOk++; else fail_at(__FILE__, __LINE__, "%s == %s (\"%s\" vs \"%s\")", #a, #b, a_.c_str(), b_.c_str()); } while (0)
#define REQUIRE(c) do { if (c) g_checksOk++; else { fail_at(__FILE__, __LINE__, "REQUIRE(%s)", #c); return; } } while (0)

/* ------------------------------------------------------------------------------------------ */
/* plug-in loading                                                                            */

static CFBundleRef g_bundle;
static char g_selfPath[2048];
static const char* g_pluginPath;
static std::string g_testsDir;
static IPluginFactory* g_factory;

// "VST" 'S1Vs' "synth1 vs": the class id derived from the original VST2 (src/vst3/synth1vst3.h)
static const char* kExpectedCID = "5653545331567373796E746831207673";

static int load_plugin(const char* path)
{
    CFURLRef url = CFURLCreateFromFileSystemRepresentation(NULL, (const UInt8*)path, strlen(path), true);
    g_bundle = url ? CFBundleCreate(NULL, url) : NULL;
    if (url) CFRelease(url);
    if (!g_bundle || !CFBundleLoadExecutable(g_bundle)) return 1;
    typedef bool (*BundleEntry)(CFBundleRef);
    BundleEntry entry = (BundleEntry)CFBundleGetFunctionPointerForName(g_bundle, CFSTR("bundleEntry"));
    GetFactoryProc getFactory = (GetFactoryProc)CFBundleGetFunctionPointerForName(g_bundle, CFSTR("GetPluginFactory"));
    if (!entry || !getFactory || !entry(g_bundle)) return 1;
    g_factory = getFactory();
    return g_factory ? 0 : 1;
}

static std::string cid_string(const TUID cid)
{
    char s[33];
    FUID::fromTUID(cid).toString(s);
    return s;
}

/** One instance: component (processor and controller in one object). */
struct Plugin {
    IComponent* component = nullptr;
    IAudioProcessor* processor = nullptr;
    IEditController* controller = nullptr;

    bool open(double sampleRate, int32 maxBlock)
    {
        PClassInfo info;
        if (g_factory->getClassInfo(0, &info) != kResultOk) return false;
        if (g_factory->createInstance(info.cid, IComponent::iid, (void**)&component) != kResultOk) return false;
        if (component->initialize(nullptr) != kResultOk) return false;
        component->queryInterface(IAudioProcessor::iid, (void**)&processor);
        component->queryInterface(IEditController::iid, (void**)&controller);
        if (!processor || !controller) return false;
        SpeakerArrangement out = SpeakerArr::kStereo;
        if (processor->setBusArrangements(nullptr, 0, &out, 1) != kResultTrue) return false;
        component->activateBus(kAudio, kOutput, 0, true);
        component->activateBus(kEvent, kInput, 0, true);
        ProcessSetup setup = { kRealtime, kSample32, maxBlock, sampleRate };
        if (processor->setupProcessing(setup) != kResultOk) return false;
        if (component->setActive(true) != kResultOk) return false;
        processor->setProcessing(true);
        return true;
    }

    void close()
    {
        if (processor) { processor->setProcessing(false); processor->release(); }
        if (component) { component->setActive(false); }
        if (controller) controller->release();
        if (component) { component->terminate(); component->release(); }
        component = nullptr;
        processor = nullptr;
        controller = nullptr;
    }
};

/* ------------------------------------------------------------------------------------------ */
/* rendering                                                                                  */

/** A render run: the plug-in, the sample position and the FNV-1a 64 hash of the output. */
struct Run {
    Plugin* plugin;
    long pos = 0;
    uint64_t hash = 1469598103934665603ULL;
    double peak = 0;
    int nonFinite = 0;
    int err = 0;
    EventList events{512};
    ParameterChanges changes{512};
};

static void add_change(Run& r, ParamID id, ParamValue value, int32 offset)
{
    int32 index;
    IParamValueQueue* q = r.changes.addParameterData(id, index);
    if (q) q->addPoint(offset, value, index);
}

/**
 * Converts an AU-test event to what a VST3 host sends: note events, a parameter change, or a
 * change of the plug-in's MIDI controller proxy parameter (IMidiMapping).
 */
static void add_event(Run& r, const ::Event& e, int32 offset)
{
    if (e.status == 0) {
        add_change(r, e.d1, (double)e.value, 0);    // AudioUnitSetParameter takes effect at once
        return;
    }
    Steinberg::Vst::Event ve = {};
    ve.busIndex = 0;
    ve.sampleOffset = offset;
    uint8_t kind = e.status & 0xF0;
    if (kind == 0x90 && e.d2 > 0) {
        ve.type = Steinberg::Vst::Event::kNoteOnEvent;
        ve.noteOn.pitch = e.d1;
        ve.noteOn.velocity = e.d2 / 127.0f;
        ve.noteOn.noteId = -1;
        r.events.addEvent(ve);
    } else if (kind == 0x90 || kind == 0x80) {
        ve.type = Steinberg::Vst::Event::kNoteOffEvent;
        ve.noteOff.pitch = e.d1;
        ve.noteOff.velocity = e.d2 / 127.0f;
        ve.noteOff.noteId = -1;
        r.events.addEvent(ve);
    } else {
        IMidiMapping* mapping = nullptr;
        r.plugin->component->queryInterface(IMidiMapping::iid, (void**)&mapping);
        if (!mapping) { r.err = 1; return; }
        CtrlNumber ctrl = kind == 0xE0 ? kPitchBend : kind == 0xD0 ? kAfterTouch : e.d1;
        ParamID id;
        if (mapping->getMidiControllerAssignment(0, e.status & 0x0F, ctrl, id) != kResultTrue) r.err = 1;
        else if (kind == 0xE0) add_change(r, id, ((e.d2 << 7) | e.d1) / 16383.0, offset);
        else add_change(r, id, (kind == 0xD0 ? e.d1 : e.d2) / 127.0, offset);
        mapping->release();
    }
}

/** Renders one block of n frames (n <= 8192) into the run. */
static void render_block(Run& r, int32 n)
{
    static float L[8192], R[8192];
    float* channels[2] = { L, R };
    AudioBusBuffers out = {};
    out.numChannels = 2;
    out.channelBuffers32 = channels;
    ProcessData data;
    data.processMode = kRealtime;
    data.symbolicSampleSize = kSample32;
    data.numSamples = n;
    data.numOutputs = 1;
    data.outputs = &out;
    data.inputEvents = &r.events;
    data.inputParameterChanges = &r.changes;
    if (r.plugin->processor->process(data) != kResultOk) { r.err = 1; return; }
    for (int32 i = 0; i < n; i++) {
        if (!std::isfinite(L[i]) || !std::isfinite(R[i])) r.nonFinite++;
        double a = std::max(std::fabs(L[i]), std::fabs(R[i]));
        if (a > r.peak) r.peak = a;
        hash_bytes(&r.hash, &L[i], 4);
        hash_bytes(&r.hash, &R[i], 4);
    }
    r.pos += n;
    r.events.clear();
    r.changes.clearQueue();
}

/** Renders total frames in blocks of the cyclic sizes[], sending each block's events with it. */
static void render_events(Run& r, long total, const uint32_t* sizes, int nSizes, const ::Event* ev, int nEv)
{
    int si = 0, ei = 0;
    long end = r.pos + total;
    while (r.pos < end && !r.err) {
        int32 n = (int32)sizes[si++ % nSizes];
        if (r.pos + n > end) n = (int32)(end - r.pos);
        while (ei < nEv && ev[ei].frame < r.pos + n) {
            long off = ev[ei].frame - r.pos;
            add_event(r, ev[ei], (int32)(off < 0 ? 0 : off));
            ei++;
        }
        render_block(r, n);
    }
}

/* ------------------------------------------------------------------------------------------ */
/* tests                                                                                      */

/** Finds a parameter's info by its id. */
static bool param_info(IEditController* c, ParamID id, ParameterInfo& info)
{
    for (int32 i = 0; i < c->getParameterCount(); i++) {
        if (c->getParameterInfo(i, info) == kResultOk && info.id == id) return true;
    }
    return false;
}

static std::string utf16_to_ascii(const TChar* s)
{
    std::string out;
    for (; *s; s++) out += (*s < 128) ? (char)*s : '?';
    return out;
}

/** The factory has one class: the VST2-compatible class id, "Synth1" by Daichi, an instrument. */
static void test_factory(void)
{
    PFactoryInfo fi;
    REQUIRE(g_factory->getFactoryInfo(&fi) == kResultOk);
    CHECK_STREQ(fi.vendor, "Daichi");
    CHECK_EQ(g_factory->countClasses(), 1);

    PClassInfo ci;
    REQUIRE(g_factory->getClassInfo(0, &ci) == kResultOk);
    CHECK_STREQ(cid_string(ci.cid), kExpectedCID);
    CHECK_STREQ(ci.category, kVstAudioEffectClass);
    CHECK_STREQ(ci.name, "Synth1");

    IPluginFactory2* f2 = nullptr;
    REQUIRE(g_factory->queryInterface(IPluginFactory2::iid, (void**)&f2) == kResultOk);
    PClassInfo2 ci2;
    REQUIRE(f2->getClassInfo2(0, &ci2) == kResultOk);
    CHECK_STREQ(ci2.subCategories, "Instrument|Synth");
    f2->release();
}

/** No audio inputs, one stereo output, one event input; only 32-bit float. */
static void test_buses(void)
{
    Plugin p;
    REQUIRE(p.open(44100, 512));
    CHECK_EQ(p.component->getBusCount(kAudio, kInput), 0);
    CHECK_EQ(p.component->getBusCount(kAudio, kOutput), 1);
    CHECK_EQ(p.component->getBusCount(kEvent, kInput), 1);
    CHECK_EQ(p.component->getBusCount(kEvent, kOutput), 0);
    CHECK(p.processor->canProcessSampleSize(kSample32) == kResultTrue);
    CHECK(p.processor->canProcessSampleSize(kSample64) != kResultTrue);
    SpeakerArrangement mono = SpeakerArr::kMono;
    CHECK(p.processor->setBusArrangements(nullptr, 0, &mono, 1) != kResultTrue);
    p.close();
}

/**
 * The VST2's parameters keep their ids: 0..98 (94 is unnamed: hidden and read-only) and 254,
 * the master volume; then the 130 hidden MIDI controller proxies.
 */
static void test_parameters(void)
{
    Plugin p;
    REQUIRE(p.open(44100, 512));
    IEditController* c = p.controller;
    CHECK_EQ(c->getParameterCount(), 99 + 1 + kCountCtrlNumber);

    int automatable = 0, hidden = 0;
    for (int32 i = 0; i < c->getParameterCount(); i++) {
        ParameterInfo info;
        REQUIRE(c->getParameterInfo(i, info) == kResultOk);
        if (info.flags & ParameterInfo::kCanAutomate) automatable++;
        if (info.flags & ParameterInfo::kIsHidden) hidden++;
        if (i < 99) CHECK_EQ(info.id, i);
    }
    CHECK_EQ(automatable, 99);              // 98 named sound parameters + master volume
    CHECK_EQ(hidden, 1 + kCountCtrlNumber); // id 94 and the proxies

    ParameterInfo info;
    REQUIRE(c->getParameterInfo(0, info) == kResultOk);
    CHECK_STREQ(utf16_to_ascii(info.title), "osc1 shape");
    REQUIRE(c->getParameterInfo(94, info) == kResultOk);
    CHECK(info.flags & ParameterInfo::kIsReadOnly);
    REQUIRE(c->getParameterInfo(99, info) == kResultOk);
    CHECK_EQ(info.id, 254);
    CHECK_STREQ(utf16_to_ascii(info.title), "master volume");
    p.close();
}

/** Every channel's CCs, channel pressure and pitch bend map to the same proxies; bus 1 to none. */
static void test_midi_mapping(void)
{
    Plugin p;
    REQUIRE(p.open(44100, 512));
    IMidiMapping* m = nullptr;
    REQUIRE(p.component->queryInterface(IMidiMapping::iid, (void**)&m) == kResultOk);
    ParamID id, id2;
    CHECK(m->getMidiControllerAssignment(0, 0, 1, id) == kResultTrue);
    CHECK(m->getMidiControllerAssignment(0, 15, 1, id2) == kResultTrue);
    CHECK_EQ(id, id2);
    CHECK(m->getMidiControllerAssignment(0, 0, kPitchBend, id) == kResultTrue);
    ParameterInfo info;
    CHECK(param_info(p.controller, id, info));
    CHECK(info.flags & ParameterInfo::kIsHidden);
    CHECK(!(info.flags & ParameterInfo::kCanAutomate));
    CHECK(info.defaultNormalizedValue == 0.5);
    CHECK(m->getMidiControllerAssignment(1, 0, 1, id) != kResultTrue);
    m->release();
    p.close();
}

/**
 * With no notes the output is inaudible (the engine settles at around 1e-7, so it is not exactly
 * zero), and silence flags are only set for blocks that really are all zeros.
 */
static void test_idle_silence(void)
{
    Plugin p;
    REQUIRE(p.open(44100, 512));
    static float L[512], R[512];
    float* channels[2] = { L, R };
    for (int block = 0; block < 20; block++) {
        AudioBusBuffers out = {};
        out.numChannels = 2;
        out.channelBuffers32 = channels;
        ProcessData data;
        data.symbolicSampleSize = kSample32;
        data.numSamples = 512;
        data.numOutputs = 1;
        data.outputs = &out;
        for (int i = 0; i < 512; i++) L[i] = R[i] = 1.0f;
        REQUIRE(p.processor->process(data) == kResultOk);
        float peak = 0;
        int nonZero = 0;
        for (int i = 0; i < 512; i++) {
            peak = std::max(peak, std::max(std::fabs(L[i]), std::fabs(R[i])));
            if (L[i] != 0 || R[i] != 0) nonZero++;
        }
        CHECK(peak < 1e-5f);
        if (out.silenceFlags) CHECK_EQ(nonZero, 0);
    }
    p.close();
}

/** Renders evs at 44.1 kHz in 512-frame blocks on a fresh instance with fixed oscillator phases. */
static Run render_fixed(Plugin& p, const std::vector<::Event>& evs, long frames)
{
    std::vector<::Event> all = { { 0, 0, P_PHASE_SHIFT, 0, 0.3f } };   // fixed phases (see the AU tests)
    all.insert(all.end(), evs.begin(), evs.end());
    Run r;
    r.plugin = &p;
    render_events(r, frames, kB512, 1, all.data(), (int)all.size());
    return r;
}

/** Peak of the output from frame `from` on (renders evs, keeping the samples). */
static double peak_after(const std::vector<::Event>& evs, long frames, long from)
{
    Plugin p;
    if (!p.open(44100, 4096)) return -1;
    Run r;
    r.plugin = &p;
    std::vector<::Event> all = { { 0, 0, P_PHASE_SHIFT, 0, 0.3f } };
    all.insert(all.end(), evs.begin(), evs.end());
    render_events(r, from, kB512, 1, all.data(), (int)all.size());
    r.peak = 0;
    std::vector<::Event> rest;
    for (auto& e : all) if (e.frame >= from) rest.push_back(e);
    render_events(r, frames - from, kB512, 1, rest.data(), (int)rest.size());
    p.close();
    return r.err ? -1 : r.peak;
}

/** A note sounds; after its note-off and release it fades out. */
static void test_render_note(void)
{
    std::vector<::Event> ev = { { 0, 0x90, 60, 100, 0 }, { 22050, 0x80, 60, 0, 0 } };
    double sounding = peak_after(ev, 22050, 0);
    double released = peak_after(ev, 5 * 44100, 4 * 44100);
    CHECK(sounding > 0.1);
    CHECK(released >= 0 && released < 1e-3);
}

/** Pitch bend reaches the engine through its proxy parameter (IMidiMapping) and changes the sound. */
static void test_pitch_bend(void)
{
    std::vector<::Event> plain = { { 0, 0x90, 60, 100, 0 } };
    std::vector<::Event> bent = plain;
    bent.push_back({ 4096, 0xE0, 0x00, 0x60, 0 });
    Plugin a, b, c;
    REQUIRE(a.open(44100, 4096) && b.open(44100, 4096) && c.open(44100, 4096));
    Run ra = render_fixed(a, plain, 22050), rb = render_fixed(b, plain, 22050), rc = render_fixed(c, bent, 22050);
    CHECK(!ra.err && !rb.err && !rc.err);
    CHECK(ra.hash == rb.hash);  // fixed phases: the same input renders the same output
    CHECK(ra.hash != rc.hash);  // and the bend changed it
    a.close(); b.close(); c.close();
}

/** CC 123 (all notes off) through its proxy parameter silences a held note. */
static void test_all_notes_off(void)
{
    std::vector<::Event> held = { { 0, 0x90, 60, 100, 0 } };
    std::vector<::Event> stopped = held;
    stopped.push_back({ 22050, 0xB0, 123, 0, 0 });
    double stillPlaying = peak_after(held, 4 * 44100, 3 * 44100);
    double afterCC = peak_after(stopped, 4 * 44100, 3 * 44100);
    CHECK(stillPlaying > 0.05);
    CHECK(afterCC >= 0 && afterCC < 1e-3);
}

/* ------------------------------------------------------------------------------------------ */
/* state                                                                                      */

typedef std::vector<unsigned char> Bytes;

static bool read_file(const std::string& path, Bytes& out)
{
    FILE* f = fopen(path.c_str(), "rb");
    if (!f) return false;
    unsigned char buf[4096];
    size_t n;
    out.clear();
    while ((n = fread(buf, 1, sizeof buf, f)) > 0) out.insert(out.end(), buf, buf + n);
    fclose(f);
    return true;
}

/** The "data" of a saved AU preset (ClassInfo): the engine chunk the AU saved. */
static bool au_preset_chunk(const std::string& path, Bytes& out)
{
    Bytes file;
    if (!read_file(path, file)) return false;
    CFDataRef d = CFDataCreate(NULL, file.data(), (CFIndex)file.size());
    CFPropertyListRef pl = CFPropertyListCreateWithData(NULL, d, kCFPropertyListImmutable, NULL, NULL);
    CFRelease(d);
    if (!pl) return false;
    CFDataRef data = (CFDataRef)CFDictionaryGetValue((CFDictionaryRef)pl, CFSTR("data"));
    bool ok = data && CFGetTypeID(data) == CFDataGetTypeID();
    if (ok) out.assign(CFDataGetBytePtr(data), CFDataGetBytePtr(data) + CFDataGetLength(data));
    CFRelease(pl);
    return ok;
}

static Bytes get_state(Plugin& p)
{
    MemoryStream stream;
    Bytes out;
    if (p.component->getState(&stream) != kResultOk) return out;
    out.assign((unsigned char*)stream.getData(), (unsigned char*)stream.getData() + stream.getSize());
    return out;
}

static tresult set_state(Plugin& p, const Bytes& data)
{
    MemoryStream stream((void*)data.data(), (TSize)data.size());
    return p.component->setState(&stream);
}

/** Big-endian writer for the VST2 fxb / fxp formats. */
struct BigEndian {
    Bytes b;
    void i32(int32_t v) { for (int s = 24; s >= 0; s -= 8) b.push_back((unsigned char)(v >> s)); }
    void raw(const Bytes& d) { b.insert(b.end(), d.begin(), d.end()); }
    void zeros(size_t n) { b.insert(b.end(), n, 0); }
};

static const int32_t kVst2ID = 'S1Vs';

/** A VST2 bank (fxb) holding the chunk, as a VST2 host saves a programs-are-chunks plug-in. */
static Bytes vst2_bank(const Bytes& chunk)
{
    BigEndian w;
    w.i32('CcnK'); w.i32((int32_t)(152 + chunk.size())); w.i32('FBCh'); w.i32(2);
    w.i32(kVst2ID); w.i32(1); w.i32(1); w.i32(0); w.zeros(124);
    w.i32((int32_t)chunk.size()); w.raw(chunk);
    return w.b;
}

/** A VST2 program (fxp) holding the chunk. */
static Bytes vst2_program(const Bytes& chunk)
{
    BigEndian w;
    w.i32('CcnK'); w.i32((int32_t)(52 + chunk.size())); w.i32('FPCh'); w.i32(1);
    w.i32(kVst2ID); w.i32(1); w.i32(255); w.zeros(28);
    w.i32((int32_t)chunk.size()); w.raw(chunk);
    return w.b;
}

/** A bank in the 'VstW' wrapper a Steinberg host writes when it hands VST2 state to a VST3. */
static Bytes vstw_bank(const Bytes& chunk)
{
    BigEndian w;
    w.i32('VstW'); w.i32(8); w.i32(1); w.i32(0);
    w.raw(vst2_bank(chunk));
    return w.b;
}

/** The parameter part of a chunk (VSTChunk::params: 0x101 x {valid, value}). */
static Bytes chunk_params(const Bytes& chunk)
{
    if (chunk.size() < 0xA40) return Bytes();
    return Bytes(chunk.begin() + 0x238, chunk.begin() + 0xA40);
}

/** getState writes the engine chunk (2624 bytes, "Synth1 VST Chunk Data" version 2). */
static void test_state_format(void)
{
    Plugin p;
    REQUIRE(p.open(44100, 512));
    Bytes st = get_state(p);
    CHECK_EQ(st.size(), 0xA40);
    REQUIRE(st.size() >= 0x24);
    CHECK_STREQ(std::string((const char*)st.data()), "Synth1 VST Chunk Data");
    CHECK_EQ(st[0x20], 2);
    p.close();
}

/** Changed parameters survive getState / setState into another instance, and the host sees them. */
static void test_state_roundtrip(void)
{
    Plugin a, b;
    REQUIRE(a.open(44100, 512) && b.open(44100, 512));
    Run r;
    r.plugin = &a;
    add_change(r, 19, 0.3, 0);      // filter freq
    add_change(r, 20, 0.7, 0);      // filter resonance
    add_change(r, 254, 0.25, 0);    // master volume
    render_block(r, 64);
    Bytes saved = get_state(a);
    CHECK(saved != get_state(b));
    REQUIRE(set_state(b, saved) == kResultOk);
    CHECK(get_state(b) == saved);
    CHECK(std::fabs(b.controller->getParamNormalized(19) - 0.3) < 0.01);
    CHECK(std::fabs(b.controller->getParamNormalized(254) - 0.25) < 0.01);
    a.close();
    b.close();
}

/**
 * State saved by the original Windows VST2 loads in every form a host passes it: the raw chunk
 * (FL Studio's own record, with its 21-byte header, from a real project), a VST2 bank or program,
 * and a bank in a Steinberg host's 'VstW' wrapper. Each restores the same patch.
 */
static void test_state_vst2(void)
{
    Bytes fl;
    REQUIRE(read_file(g_testsDir + "/data/vst3/fl_studio_synth1_state.bin", fl));
    REQUIRE(fl.size() == 21 + 0xA40);
    Bytes chunk(fl.begin() + 21, fl.end());

    Plugin ref;
    REQUIRE(ref.open(44100, 512));
    Bytes fresh = get_state(ref);
    REQUIRE(set_state(ref, chunk) == kResultOk);
    Bytes loaded = get_state(ref);
    CHECK(chunk_params(loaded) != chunk_params(fresh));     // the project's patch is not the default
    ref.close();

    // every parameter the project stored comes back
    Bytes want = chunk_params(chunk), got = chunk_params(loaded);
    int mismatches = 0;
    for (size_t i = 0; i + 8 <= want.size(); i += 8) {
        int id = (int)(i / 8);
        bool valid = want[i] != 0;
        if (valid && (id < 99 || id == 254) && memcmp(&want[i + 4], &got[i + 4], 4) != 0) mismatches++;
    }
    CHECK_EQ(mismatches, 0);

    const struct { const char* name; Bytes data; } forms[] = {
        { "FL Studio record", fl },
        { "VST2 bank", vst2_bank(chunk) },
        { "VST2 program", vst2_program(chunk) },
        { "VstW bank", vstw_bank(chunk) },
    };
    for (const auto& f : forms) {
        Plugin p;
        REQUIRE(p.open(44100, 512));
        if (set_state(p, f.data) != kResultOk) fail_at(__FILE__, __LINE__, "%s: setState failed", f.name);
        else if (get_state(p) != loaded) fail_at(__FILE__, __LINE__, "%s: different patch", f.name);
        else g_checksOk++;
        p.close();
    }
}

/** Garbage, a truncated chunk and an empty stream are refused and leave the patch alone. */
static void test_state_reject(void)
{
    Bytes fl;
    REQUIRE(read_file(g_testsDir + "/data/vst3/fl_studio_synth1_state.bin", fl));
    Plugin p;
    REQUIRE(p.open(44100, 512));
    Bytes before = get_state(p);
    Bytes garbage(3000, 0x5A);
    Bytes truncated(fl.begin(), fl.begin() + 21 + 0x800);
    CHECK(set_state(p, garbage) != kResultOk);
    CHECK(set_state(p, truncated) != kResultOk);
    CHECK(set_state(p, Bytes()) != kResultOk);
    CHECK(get_state(p) == before);
    p.close();
}

/* ------------------------------------------------------------------------------------------ */
/* editor                                                                                     */

/** Renders a note; true if it sounded (the instance still works). */
static bool still_renders(Plugin& p)
{
    std::vector<::Event> ev = { { 0, 0x90, 60, 100, 0 }, { 4096, 0x80, 60, 0, 0 } };
    Run r;
    r.plugin = &p;
    render_events(r, 8192, kB512, 1, ev.data(), (int)ev.size());
    return !r.err && r.peak > 0.05;
}

/**
 * The editor is an NSView of 466 x 340 at the option dialog's zoom (150% by default). Attached to
 * a host window it adds the VST3's own GUI view (the renamed class, not the Audio Unit's), and
 * removing it takes the GUI down again; twice, as when a user closes and reopens the editor.
 */
static void test_editor(void)
{
    Plugin p;
    REQUIRE(p.open(44100, 512));
    CHECK(p.controller->createView("something else") == nullptr);

    for (int round = 0; round < 2; round++) {
        IPlugView* view = p.controller->createView(ViewType::kEditor);
        REQUIRE(view != nullptr);
        CHECK(view->isPlatformTypeSupported(kPlatformTypeNSView) == kResultTrue);
        CHECK(view->isPlatformTypeSupported(kPlatformTypeHWND) != kResultTrue);
        CHECK(view->canResize() != kResultTrue);
        ViewRect size;
        REQUIRE(view->getSize(&size) == kResultOk);
        CHECK_EQ(size.getWidth(), 466 * 150 / 100);
        CHECK_EQ(size.getHeight(), 340 * 150 / 100);

        void* parent = view_host_open(size.getWidth(), size.getHeight());
        REQUIRE(view->attached(parent, kPlatformTypeNSView) == kResultOk);
        CHECK_EQ(view_host_subview_count(parent), 1);
        CHECK_STREQ(view_host_subview_class(parent, 0), "S1V3_Synth1CocoaView");
        double w, h;
        view_host_subview_size(parent, 0, &w, &h);
        CHECK_EQ((long)w, size.getWidth());
        CHECK_EQ((long)h, size.getHeight());
        CHECK(view->attached(parent, kPlatformTypeNSView) != kResultOk);    // already attached

        REQUIRE(view->removed() == kResultOk);
        CHECK_EQ(view_host_subview_count(parent), 0);
        view_host_close(parent);
        view->release();
        CHECK(still_renders(p));
    }
    p.close();
}

/* ------------------------------------------------------------------------------------------ */
/* golden scenarios (tests/au/golden_scenarios.h), replayed through the VST3 interfaces        */

/** Restores the AU tests' default patch, then renders the scenario. */
static int render_scenario(const Scenario* s, uint64_t* pHash, double* pPeak)
{
    Bytes defaults;
    if (!au_preset_chunk(g_testsDir + "/au/default_state.plist", defaults)) return 2;
    Plugin p;
    if (!p.open(s->sr, 4096)) { p.close(); return 1; }
    if (set_state(p, defaults) != kResultOk) { p.close(); return 2; }
    std::vector<::Event> ev(s->ev, s->ev + s->nEv);
    for (auto& e : ev) e.frame = scale_frame(s, e.frame);
    Run r;
    r.plugin = &p;
    render_events(r, s->frames, s->sizes, s->nSizes, ev.data(), (int)ev.size());
    p.close();
    if (r.err || r.nonFinite) return 3;
    *pHash = r.hash;
    *pPeak = r.peak;
    return 0;
}

static int child_main(int argc, char** argv)
{
    // --child <plugin> <tests folder> scenario <index>
    if (argc < 6 || load_plugin(argv[2])) return 2;
    g_testsDir = argv[3];
    if (!strcmp(argv[4], "scenario")) {
        int idx = atoi(argv[5]);
        if (idx < 0 || idx >= NUM_SCENARIOS) return 2;
        uint64_t h;
        double peak;
        if (render_scenario(&kScenarios[idx], &h, &peak)) return 1;
        printf("hash %016llx peak %.6f\n", (unsigned long long)h, peak);
        return 0;
    }
    return 2;
}

/**
 * Runs this program as "--child <plugin> <tests folder> args..." and returns its first output
 * line ("" on failure). Started directly, not through a shell, so it inherits
 * DYLD_INSERT_LIBRARIES.
 */
static std::string run_child(const std::vector<std::string>& args)
{
    std::vector<std::string> all = { g_selfPath, "--child", g_pluginPath, g_testsDir };
    all.insert(all.end(), args.begin(), args.end());
    std::vector<char*> argv;
    for (auto& a : all) argv.push_back((char*)a.c_str());
    argv.push_back(nullptr);

    int fds[2];
    if (pipe(fds) != 0) return "";
    posix_spawn_file_actions_t actions;
    posix_spawn_file_actions_init(&actions);
    posix_spawn_file_actions_adddup2(&actions, fds[1], STDOUT_FILENO);
    posix_spawn_file_actions_addclose(&actions, fds[0]);
    posix_spawn_file_actions_addclose(&actions, fds[1]);
    pid_t pid;
    int status = posix_spawn(&pid, g_selfPath, &actions, nullptr, argv.data(), environ);
    posix_spawn_file_actions_destroy(&actions);
    close(fds[1]);
    if (status != 0) { close(fds[0]); return ""; }

    std::string out;
    char buf[256];
    FILE* f = fdopen(fds[0], "r");
    if (f) {
        while (fgets(buf, sizeof buf, f)) out += buf;
        fclose(f);
    }
    if (waitpid(pid, &status, 0) < 0 || !WIFEXITED(status) || WEXITSTATUS(status) != 0) return "";
    return out.substr(0, out.find('\n'));
}

/**
 * Each AU golden scenario, replayed through the VST3 interfaces after restoring the AU tests'
 * default patch with setState, matches the hash of the original release.
 */
static void test_golden(void)
{
    std::string reference = g_testsDir + "/au/reference.txt";
    for (int i = 0; i < NUM_SCENARIOS; i++) {
        uint64_t want;
        if (!find_reference(reference.c_str(), kScenarios[i].name, &want)) {
            fail_at(__FILE__, __LINE__, "no reference for %s", kScenarios[i].name);
            continue;
        }
        std::string line = run_child({ "scenario", std::to_string(i) });
        unsigned long long got;
        if (sscanf(line.c_str(), "hash %llx", &got) != 1) {
            fail_at(__FILE__, __LINE__, "%s: render failed", kScenarios[i].name);
            continue;
        }
        if (got == want) g_checksOk++;
        else fail_at(__FILE__, __LINE__, "%s: %016llx, expected %016llx", kScenarios[i].name, got,
                     (unsigned long long)want);
    }
}

static const struct { const char* name; void (*fn)(void); } kTests[] = {
    { "vst3.factory", test_factory },
    { "vst3.buses", test_buses },
    { "vst3.parameters", test_parameters },
    { "vst3.midi_mapping", test_midi_mapping },
    { "vst3.idle_silence", test_idle_silence },
    { "vst3.render_note", test_render_note },
    { "vst3.pitch_bend", test_pitch_bend },
    { "vst3.all_notes_off", test_all_notes_off },
    { "vst3.state_format", test_state_format },
    { "vst3.state_roundtrip", test_state_roundtrip },
    { "vst3.state_vst2", test_state_vst2 },
    { "vst3.state_reject", test_state_reject },
    { "vst3.editor", test_editor },
    { "vst3.golden", test_golden },
};

int main(int argc, char** argv)
{
    uint32_t sz = sizeof g_selfPath;
    setvbuf(stdout, NULL, _IOLBF, 0);
    if (_NSGetExecutablePath(g_selfPath, &sz) != 0) return 2;
    if (argc >= 3 && !strcmp(argv[1], "--child")) {
        g_pluginPath = argv[2];
        return child_main(argc, argv);
    }
    if (argc < 3) {
        fprintf(stderr, "usage: %s <Synth1.vst3> <tests folder> [filter ...]\n", argv[0]);
        return 2;
    }
    g_pluginPath = argv[1];
    g_testsDir = argv[2];
    if (load_plugin(g_pluginPath)) { printf("FAIL vst3.load: cannot load %s\n", g_pluginPath); return 1; }
    for (const auto& t : kTests) {
        bool selected = argc <= 3;
        for (int j = 3; j < argc; j++) if (strstr(t.name, argv[j])) selected = true;
        if (!selected) continue;
        g_testFailed = 0;
        g_tests++;
        t.fn();
        if (g_testFailed) g_testsFailed++;
        printf("%s %s\n", g_testFailed ? "FAIL" : "ok  ", t.name);
    }
    printf("\n%d tests, %d failed; %d checks passed, %d failed\n", g_tests, g_testsFailed, g_checksOk, g_checksFailed);
    return g_testsFailed ? 1 : 0;
}
