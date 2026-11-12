/**
 * @file vst3_tests.cpp
 * @brief Black-box tests of the built Synth1 VST3 plug-in (src/vst3/).
 *
 * Loads Synth1.vst3 with CFBundle (bundleEntry / GetPluginFactory) and drives it only through
 * the VST3 interfaces, the way a host does: the class factory, buses, parameters, the MIDI
 * controller mapping (IMidiMapping), and rendering with note events, parameter changes and the
 * MIDI controller proxy parameters.
 *
 * Usage:
 *   vst3_tests <Synth1.vst3> [filter ...]
 *
 * The scripts run the host with tests/au/au_isolation.c loaded, so the plug-in sees default
 * settings and a temporary folder instead of ~/Documents.
 */

#include <CoreFoundation/CoreFoundation.h>

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
#include "public.sdk/source/vst/hosting/eventlist.h"
#include "public.sdk/source/vst/hosting/parameterchanges.h"

using namespace Steinberg;
using namespace Steinberg::Vst;

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

/** An AU-test event: status 0 = parameter change (d1 = id, value), else a MIDI message. */
struct TestEvent {
    long frame;
    uint8_t status, d1, d2;
    float value;
};

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

static void hash_bytes(uint64_t* h, const void* p, size_t n)
{
    const unsigned char* b = (const unsigned char*)p;
    for (size_t i = 0; i < n; i++) { *h ^= b[i]; *h *= 1099511628211ULL; }
}

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
static void add_event(Run& r, const TestEvent& e, int32 offset)
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
static void render_events(Run& r, long total, const int32* sizes, int nSizes, const TestEvent* ev, int nEv)
{
    int si = 0, ei = 0;
    long end = r.pos + total;
    while (r.pos < end && !r.err) {
        int32 n = sizes[si++ % nSizes];
        if (r.pos + n > end) n = (int32)(end - r.pos);
        while (ei < nEv && ev[ei].frame < r.pos + n) {
            long off = ev[ei].frame - r.pos;
            add_event(r, ev[ei], (int32)(off < 0 ? 0 : off));
            ei++;
        }
        render_block(r, n);
    }
}

/** "osc phase shift" (g_midiAssignParams in src/techpop/synmidi.cpp): above 0 the oscillators
    start at fixed phases instead of random ones, so renders in one process are repeatable. */
static const ParamID P_PHASE_SHIFT = 91;

static const int32 kB512[] = { 512 };

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
static Run render_fixed(Plugin& p, const std::vector<TestEvent>& evs, long frames)
{
    std::vector<TestEvent> all = { { 0, 0, P_PHASE_SHIFT, 0, 0.3f } };   // fixed phases (see the AU tests)
    all.insert(all.end(), evs.begin(), evs.end());
    Run r;
    r.plugin = &p;
    render_events(r, frames, kB512, 1, all.data(), (int)all.size());
    return r;
}

/** Peak of the output from frame `from` on (renders evs, keeping the samples). */
static double peak_after(const std::vector<TestEvent>& evs, long frames, long from)
{
    Plugin p;
    if (!p.open(44100, 4096)) return -1;
    Run r;
    r.plugin = &p;
    std::vector<TestEvent> all = { { 0, 0, P_PHASE_SHIFT, 0, 0.3f } };
    all.insert(all.end(), evs.begin(), evs.end());
    render_events(r, from, kB512, 1, all.data(), (int)all.size());
    r.peak = 0;
    std::vector<TestEvent> rest;
    for (auto& e : all) if (e.frame >= from) rest.push_back(e);
    render_events(r, frames - from, kB512, 1, rest.data(), (int)rest.size());
    p.close();
    return r.err ? -1 : r.peak;
}

/** A note sounds; after its note-off and release it fades out. */
static void test_render_note(void)
{
    std::vector<TestEvent> ev = { { 0, 0x90, 60, 100, 0 }, { 22050, 0x80, 60, 0, 0 } };
    double sounding = peak_after(ev, 22050, 0);
    double released = peak_after(ev, 5 * 44100, 4 * 44100);
    CHECK(sounding > 0.1);
    CHECK(released >= 0 && released < 1e-3);
}

/** Pitch bend reaches the engine through its proxy parameter (IMidiMapping) and changes the sound. */
static void test_pitch_bend(void)
{
    std::vector<TestEvent> plain = { { 0, 0x90, 60, 100, 0 } };
    std::vector<TestEvent> bent = plain;
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
    std::vector<TestEvent> held = { { 0, 0x90, 60, 100, 0 } };
    std::vector<TestEvent> stopped = held;
    stopped.push_back({ 22050, 0xB0, 123, 0, 0 });
    double stillPlaying = peak_after(held, 4 * 44100, 3 * 44100);
    double afterCC = peak_after(stopped, 4 * 44100, 3 * 44100);
    CHECK(stillPlaying > 0.05);
    CHECK(afterCC >= 0 && afterCC < 1e-3);
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
};

int main(int argc, char** argv)
{
    setvbuf(stdout, NULL, _IOLBF, 0);
    if (argc < 2) {
        fprintf(stderr, "usage: %s <Synth1.vst3> [filter ...]\n", argv[0]);
        return 2;
    }
    if (load_plugin(argv[1])) { printf("FAIL vst3.load: cannot load %s\n", argv[1]); return 1; }
    for (const auto& t : kTests) {
        bool selected = argc <= 2;
        for (int j = 2; j < argc; j++) if (strstr(t.name, argv[j])) selected = true;
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
