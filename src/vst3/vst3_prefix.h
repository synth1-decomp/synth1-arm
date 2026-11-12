/**
 * @file vst3_prefix.h
 * @brief Prefix header of the VST3 target (Synth1VST3 in Synth1.xcodeproj).
 *
 * Objective-C class names are global to the host process. A host that loads both the
 * Audio Unit and the VST3 would otherwise get two copies of each GUI class, and the
 * runtime could resolve a class to the AU bundle's copy, which uses that bundle's
 * engine globals and resources. Every Objective-C class of the shared GUI code is
 * therefore renamed in the VST3 build. The nibs that name classes (IDD_OPTION and
 * IDD_SAVESOUND) are compiled from copies with the same renames (see the target's
 * "Compile renamed nibs" build phase).
 */
#ifndef __VST3_PREFIX_H__
#define __VST3_PREFIX_H__

// SynView.m
#define SynView             S1V3_SynView
#define VolView             S1V3_VolView
#define SoundSelectView     S1V3_SoundSelectView
#define NSTreeItem          S1V3_NSTreeItem
#define MSMessageParam      S1V3_MSMessageParam
// SynWindow.m
#define SynWindow           S1V3_SynWindow
#define VolWindow           S1V3_VolWindow
#define SoundSelectWindow   S1V3_SoundSelectWindow
#define CCDlg               S1V3_CCDlg
#define OptionWindow        S1V3_OptionWindow
#define BankListItem        S1V3_BankListItem
#define MidiListItem        S1V3_MidiListItem
#define SaveSoundWindow     S1V3_SaveSoundWindow
// winobj_au.m
#define Synth1MouseCapture  S1V3_Synth1MouseCapture
// audiounit/Synth1CocoaViewFactory.mm
#define Synth1CocoaView     S1V3_Synth1CocoaView

#endif
