/**
 * @file synth1vst3view.h
 * @brief Editor of the Synth1 VST3 plug-in: hosts the shared Cocoa GUI (Synth1CocoaView) in the
 *        NSView the host provides, the VST3 counterpart of the AU's view factory.
 */
#ifndef __SYNTH1VST3VIEW_H__
#define __SYNTH1VST3VIEW_H__

#include "public.sdk/source/common/pluginview.h"

namespace Synth1 {

class Synth1Vst3;

/**
 * @brief The editor view (IPlugView, macOS only).
 *
 * The panel is 466 x 340 points at 100% and is zoomed by the setting of the option dialog
 * (GetPanelScale()). The zoom is read when the view is created, as in the AU, so a new zoom
 * applies the next time the editor opens; the view is not resizable.
 */
class Synth1Vst3View : public Steinberg::CPluginView
{
public:
    explicit Synth1Vst3View(Synth1Vst3* plugin);
    ~Synth1Vst3View() SMTG_OVERRIDE;

    Steinberg::tresult PLUGIN_API isPlatformTypeSupported(Steinberg::FIDString type) SMTG_OVERRIDE;
    Steinberg::tresult PLUGIN_API attached(void* parent, Steinberg::FIDString type) SMTG_OVERRIDE;
    Steinberg::tresult PLUGIN_API removed() SMTG_OVERRIDE;

private:
    Synth1Vst3* m_plugin;
    void* m_view;       ///< the Synth1CocoaView (NSView*), while attached
};

} // namespace Synth1

#endif
