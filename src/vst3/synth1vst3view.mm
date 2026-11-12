/**
 * @file synth1vst3view.mm
 * @brief Editor of the Synth1 VST3 plug-in (see synth1vst3view.h).
 */
#import <Cocoa/Cocoa.h>

#include "synth1vst3view.h"
#include "synth1vst3.h"
#include "synth1vst3_hooks.h"

#include "pluginterfaces/gui/iplugview.h"

// techpop GUI (synwin.cpp): the zoom of the option dialog, in percent
int GetPanelScale();

// panel size at 100% (as in audiounit/Synth1CocoaViewFactory.mm)
static const int kPanelWidth = 466;
static const int kPanelHeight = 340;

using namespace Steinberg;

namespace Synth1 {

Synth1Vst3View::Synth1Vst3View(Synth1Vst3* plugin)
    : CPluginView(nullptr)
    , m_plugin(plugin)
    , m_view(nullptr)
{
    int scale = GetPanelScale();
    ViewRect size(0, 0, kPanelWidth * scale / 100, kPanelHeight * scale / 100);
    setRect(size);
    m_plugin->addRef();
}

Synth1Vst3View::~Synth1Vst3View()
{
    if (m_view) removed();
    m_plugin->release();
}

tresult PLUGIN_API Synth1Vst3View::isPlatformTypeSupported(FIDString type)
{
    return (type && strcmp(type, kPlatformTypeNSView) == 0) ? kResultTrue : kResultFalse;
}

tresult PLUGIN_API Synth1Vst3View::attached(void* parent, FIDString type)
{
    if (parent == nullptr || isPlatformTypeSupported(type) != kResultTrue) return kResultFalse;
    if (m_view) return kResultFalse;

    // The GUI is created when the view enters the host's window (Synth1CocoaView), with this
    // plug-in in place of the AudioUnit, so it reports edits through the VST3 hooks.
    NSView* view = (NSView*)Synth1Vst3_createView(rect.getWidth(), rect.getHeight(), m_plugin, m_plugin->synth());
    if (view == nil) return kResultFalse;
    [(NSView*)parent addSubview:view];
    m_view = view;
    return CPluginView::attached(parent, type);
}

tresult PLUGIN_API Synth1Vst3View::removed()
{
    if (m_view) {
        // leaving the window destroys the GUI (-[Synth1CocoaView viewWillMoveToWindow:])
        NSView* view = (NSView*)m_view;
        m_view = nullptr;
        [view removeFromSuperview];
        [view release];
    }
    return CPluginView::removed();
}

} // namespace Synth1
