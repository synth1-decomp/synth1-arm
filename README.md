# synth1-arm

Updated fork of Synth1 for modern macOS. Runs natively on both Apple Silicon and Intel Macs.

Based on the decompilation of Synth1 v1.13 beta11. See: [synth1-decomp](https://github.com/synth1-decomp/synth1-decomp).

## Synth1

[Synth1](https://daichilab.sakura.ne.jp/softsynth/index.html) is a popular freeware VST/AU plugin developed by Daichi.

The code in this repo is a reconstruction of Synth1 for preservation and interoperability, not the original code.

Synth1 is copyright Daichi (Daichi Laboratory). All rights to Synth1 remain with its author.

## Third-party code

Synth1 leverages several third-party libraries.

See: [THIRD_PARTY.md](docs/THIRD_PARTY.md)

## Installing

Requires macOS 12 or later.

1. Download `Synth1-<version>-release.pkg` from the [latest release](https://github.com/synth1-decomp/synth1-arm/releases/latest).
2. Double-click the downloaded `.pkg` installer.
   1. macOS will show a scary looking warning popup and refuse to open the installer because it isn't signed yet.
   2. Click **Done** to dismiss the popup for now.
3. Open **System Settings**, click **Privacy & Security**, then scroll down and click **Open Anyway**.
4. The warning popup will appear again but this time with its own **Open Anyway** option. Click it to start the installer.
5. Proceed through the installer like you would any other.
6. Open your DAW of choice and scan for plugins.

To uninstall, delete `/Library/Audio/Plug-Ins/Components/Synth1.component`.
