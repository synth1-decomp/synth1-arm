/**
 * @file synth1vst3_hooks.h
 * @brief Calls from the shared GUI code into the VST3 plug-in (VST3 build only).
 *
 * In the AU builds the GUI talks to the host through the AudioUnit it was created with.
 * In the VST3 build (SYNTH1_VST3_BUILD) the same void* carries the plug-in instance
 * instead, and the GUI reports to the host through these functions.
 */
#ifndef __SYNTH1VST3_HOOKS_H__
#define __SYNTH1VST3_HOOKS_H__

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Reports a parameter edited in the GUI to the host (begin, perform and end edit).
 *
 * @param pPlugin   The plug-in instance (the GUI's "AudioUnit" pointer).
 * @param id        Parameter id, as the AU build would report it.
 */
void Synth1Vst3_paramEdited(void* pPlugin, int id);

/**
 * @brief Reports a bank/program change made in the GUI, after which every parameter may
 *        have a new value.
 *
 * @param pPlugin   The plug-in instance.
 */
void Synth1Vst3_programChanged(void* pPlugin);

/**
 * @brief Creates the editor view (Synth1CocoaView) for the plug-in.
 *
 * @param width     View width in points (the zoomed panel size).
 * @param height    View height in points.
 * @param pPlugin   The plug-in instance, handed to the GUI in place of the AudioUnit.
 * @param pSynth1   The plug-in's CSynthesizer1*.
 * @return The new NSView* (retained; the caller releases it).
 */
void* Synth1Vst3_createView(double width, double height, void* pPlugin, void* pSynth1);

#ifdef __cplusplus
}
#endif

#endif
