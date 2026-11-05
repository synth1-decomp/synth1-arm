# Third-party sources

Every file under the paths below is an unmodified public release, except the one configure-generated line
in zlib's `zconf.h` noted below. To re-verify, download the archives listed here and `diff` them against
`src/`. The SHA-256 values are of the archives as downloaded on 2026-11-01.

## zlib 1.2.5 (with minizip)

- **Location:** `src/zlib125/zlib-1.2.5/` (the complete release tree, including `contrib/minizip`)
- **Source:** https://zlib.net/fossils/zlib-1.2.5.tar.gz
  (SHA-256 `6064e52e513facb0fbb7998c6413406cf253cfb986063d68f4771c2bf7a3f958`)
- **Licence:** zlib licence (see `README` and `zlib.h` in that directory)
- **Difference from the tarball:** `zconf.h`, line 359: `#ifdef HAVE_UNISTD_H` is replaced by
  `#if 1    /* was set to #if 1 by ./configure */`. This is exactly the edit zlib's own `./configure` makes on macOS.
  The original Synth1 build used the configured header.

## Ooura FFT

- **Location:** `src/techpop/fft4g.c`
- **Source:** `fft4g.c` from Takuya Ooura's General Purpose FFT Package,
  https://www.kurims.kyoto-u.ac.jp/~ooura/fft.tgz
  (SHA-256 `52bb637c70b971958ec79c9c8752b1df5ff0218a4db4510e60826e0cb79b5296`)
- **Licence:** the permissive notice in the file header
- **Difference:** none

## Apple Core Audio Utility Classes

- **Location:** `src/audiounit/AUPublic/` (57 files) and `src/audiounit/PublicUtility/` (117 files)
- **Licence:** Apple sample-code licence (the disclaimer header in each file)

The original Synth1 build used the AU base classes from Apple's 2016 *Audio Unit Examples* sample code.
That sample ships only part of the utility classes. The remaining files, most of which are not linked into
the plugin, come from the 2014 *Core Audio Utility Classes* (version 1.1). Every file is byte-identical to
one of these two releases.

1. **Audio Unit Examples** (AudioUnit Effect, Generator, Instrument, MIDI Processor and Offline), sc2195 /
   DTS40013969, version 2.0, 2016-02-19 revision. Its `AUPublic/` and `PublicUtility/` folders are used
   **in full**.
   - **Source:** https://developer.apple.com/library/archive/samplecode/sc2195/AudioUnitExamplesAudioUnitEffectGeneratorInstrumentMIDIProcessorandOffline.zip
     (SHA-256 `8d58e8d9e2fffcfeda53782906db7ac6bc58c99f3dd2e6220c6082eda55a8a6e`)
   - **Files taken from it:** 39 in `AUPublic/` and 37 in `PublicUtility/`.

2. **Core Audio Utility Classes**, version 1.1 (2014). These files are taken from `CoreAudio/AudioUnits/AUPublic/` and
   `CoreAudio/PublicUtility/`.
   - **Source:** https://developer.apple.com/library/archive/samplecode/CoreAudioUtilityClasses/CoreAudioUtilityClasses.zip
     (SHA-256 `95582fa7457efb2a94ec7a0b74a8e4746141dc910c73d9840f1d15cc6de08280`)
   - **Files taken from it:** 18 in `AUPublic/` and 80 in `PublicUtility/`:

   `AUPublic/`:
  - `AUBase/AUResources.r`
  - `AUCarbonViewBase/AUCarbonViewBase.cpp`
  - `AUCarbonViewBase/AUCarbonViewBase.h`
  - `AUCarbonViewBase/AUCarbonViewControl.cpp`
  - `AUCarbonViewBase/AUCarbonViewControl.h`
  - `AUCarbonViewBase/AUCarbonViewDispatch.cpp`
  - `AUCarbonViewBase/AUControlGroup.cpp`
  - `AUCarbonViewBase/AUControlGroup.h`
  - `AUCarbonViewBase/CarbonEventHandler.cpp`
  - `AUCarbonViewBase/CarbonEventHandler.h`
  - `AUViewBase/AUViewLocalizedStringKeys.h`
  - `OtherBases/AUOutputBase.cpp`
  - `OtherBases/AUOutputBase.h`
  - `OtherBases/AUPannerBase.cpp`
  - `OtherBases/AUPannerBase.h`
  - `Utility/AUInputFormatConverter.h`
  - `Utility/AUTimestampGenerator.cpp`
  - `Utility/AUTimestampGenerator.h`

   `PublicUtility/`:
  - `AUOutputBL.cpp`
  - `AUOutputBL.h`
  - `AUParamInfo.cpp`
  - `AUParamInfo.h`
  - `CAAUParameter.cpp`
  - `CAAUParameter.h`
  - `CAAUProcessor.cpp`
  - `CAAUProcessor.h`
  - `CAAudioBufferList.cpp`
  - `CAAudioBufferList.h`
  - `CAAudioFileFormats.cpp`
  - `CAAudioFileFormats.h`
  - `CAAudioTimeStamp.cpp`
  - `CAAudioTimeStamp.h`
  - `CAAudioUnit.cpp`
  - `CAAudioUnit.h`
  - `CAAudioUnitOutputCapturer.h`
  - `CAAudioValueRange.cpp`
  - `CAAudioValueRange.h`
  - `CABitOperations.h`
  - `CABool.h`
  - `CABundleLocker.cpp`
  - `CABundleLocker.h`
  - `CACFArray.cpp`
  - `CACFArray.h`
  - `CACFData.h`
  - `CACFDictionary.cpp`
  - `CACFDictionary.h`
  - `CACFDistributedNotification.cpp`
  - `CACFDistributedNotification.h`
  - `CACFMachPort.cpp`
  - `CACFMachPort.h`
  - `CACFMessagePort.cpp`
  - `CACFMessagePort.h`
  - `CACFNumber.cpp`
  - `CACFNumber.h`
  - `CACFObject.h`
  - `CACFPlugIn.h`
  - `CACFPreferences.cpp`
  - `CACFPreferences.h`
  - `CACFString.cpp`
  - `CACFString.h`
  - `CAComponent.cpp`
  - `CAComponent.h`
  - `CAComponentDescription.cpp`
  - `CAComponentDescription.h`
  - `CAExtAudioFile.h`
  - `CAFilePathUtils.cpp`
  - `CAFilePathUtils.h`
  - `CAHALAudioDevice.cpp`
  - `CAHALAudioDevice.h`
  - `CAHALAudioObject.cpp`
  - `CAHALAudioObject.h`
  - `CAHALAudioStream.cpp`
  - `CAHALAudioStream.h`
  - `CAHALAudioSystemObject.cpp`
  - `CAHALAudioSystemObject.h`
  - `CAMixMap.h`
  - `CAPThread.cpp`
  - `CAPThread.h`
  - `CAPersistence.cpp`
  - `CAProcess.cpp`
  - `CAProcess.h`
  - `CAPropertyAddress.h`
  - `CARingBuffer.cpp`
  - `CARingBuffer.h`
  - `CASettingsStorage.cpp`
  - `CASettingsStorage.h`
  - `CASharedLibrary.cpp`
  - `CASharedLibrary.h`
  - `CASpectralProcessor.cpp`
  - `CASpectralProcessor.h`
  - `CAStreamRangedDescription.cpp`
  - `CAStreamRangedDescription.h`
  - `CATink.h`
  - `CATokenMap.h`
  - `CAVolumeCurve.cpp`
  - `CAVolumeCurve.h`
  - `MatrixMixerVolumes.cpp`
  - `MatrixMixerVolumes.h`
