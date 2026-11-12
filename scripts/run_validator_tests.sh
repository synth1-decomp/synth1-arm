#!/bin/bash
# Runs a plug-in format's validator on a built Synth1: Apple's Audio Unit validator (auval) on a
# Synth1.component, or Steinberg's VST3 validator on a Synth1.vst3.
#
# --format au installs the component for this user, replacing any Synth1.component in
# ~/Library/Audio/Plug-Ins/Components. It is meant for CI; locally, auval -v aumu Syn1 daiC checks
# the plugin you already have installed.
#
# --format vst3 builds the VST3 validator from the SDK sources (src/vst3sdk) into
# build/vst3-validator-ARCH, once per architecture. The SDK normally builds it with CMake; this
# project builds with Xcode, so it is compiled here directly.
set -euo pipefail

usage() {
    cat <<'EOF'
Usage: scripts/run_validator_tests.sh --format au|vst3 [options]

Options:
  --format FORMAT   au or vst3: the plug-in to validate (required).
  --au PATH         The Synth1.component to validate with --format au
                    (default: build/modern/Build/Products/Release/Synth1.component).
  --vst3 PATH       The Synth1.vst3 to validate with --format vst3
                    (default: build/modern/Build/Products/Release/Synth1.vst3).
  --arch ARCH       arm64 or x86_64 (default: this Mac's). x86_64 runs under Rosetta on Apple Silicon.
  -h, --help        Show this help.
EOF
}

# The VST3 validator's sources, as the SDK's CMake files list them for macOS (the validator target
# and the sdk_hosting, sdk_common, base and pluginterfaces libraries it links), for SDK 3.8.1.
SOURCES=(
    base/source/baseiids.cpp
    base/source/fbuffer.cpp
    base/source/fdebug.cpp
    base/source/fdynlib.cpp
    base/source/fobject.cpp
    base/source/fstreamer.cpp
    base/source/fstring.cpp
    base/source/timer.cpp
    base/source/updatehandler.cpp
    base/thread/source/fcondition.cpp
    base/thread/source/flock.cpp
    pluginterfaces/base/conststringtable.cpp
    pluginterfaces/base/coreiids.cpp
    pluginterfaces/base/funknown.cpp
    pluginterfaces/base/ustring.cpp
    public.sdk/samples/vst-hosting/validator/source/main.cpp
    public.sdk/samples/vst-hosting/validator/source/usediids.cpp
    public.sdk/samples/vst-hosting/validator/source/validator.cpp
    public.sdk/source/common/commoniids.cpp
    public.sdk/source/common/commonstringconvert.cpp
    public.sdk/source/common/memorystream.cpp
    public.sdk/source/common/openurl.cpp
    public.sdk/source/common/readfile.cpp
    public.sdk/source/common/systemclipboard_mac.mm
    public.sdk/source/common/threadchecker_mac.mm
    public.sdk/source/main/moduleinit.cpp
    public.sdk/source/vst/hosting/connectionproxy.cpp
    public.sdk/source/vst/hosting/eventlist.cpp
    public.sdk/source/vst/hosting/hostclasses.cpp
    public.sdk/source/vst/hosting/module.cpp
    public.sdk/source/vst/hosting/module_mac.mm
    public.sdk/source/vst/hosting/parameterchanges.cpp
    public.sdk/source/vst/hosting/pluginterfacesupport.cpp
    public.sdk/source/vst/hosting/plugprovider.cpp
    public.sdk/source/vst/hosting/processdata.cpp
    public.sdk/source/vst/hosting/test/connectionproxytest.cpp
    public.sdk/source/vst/hosting/test/eventlisttest.cpp
    public.sdk/source/vst/hosting/test/hostclassestest.cpp
    public.sdk/source/vst/hosting/test/miditovsttest.cpp
    public.sdk/source/vst/hosting/test/parameterchangestest.cpp
    public.sdk/source/vst/hosting/test/pluginterfacesupporttest.cpp
    public.sdk/source/vst/hosting/test/processdatatest.cpp
    public.sdk/source/vst/moduleinfo/moduleinfoparser.cpp
    public.sdk/source/vst/testsuite/bus/busactivation.cpp
    public.sdk/source/vst/testsuite/bus/busconsistency.cpp
    public.sdk/source/vst/testsuite/bus/businvalidindex.cpp
    public.sdk/source/vst/testsuite/bus/checkaudiobusarrangement.cpp
    public.sdk/source/vst/testsuite/bus/scanbusses.cpp
    public.sdk/source/vst/testsuite/bus/sidechainarrangement.cpp
    public.sdk/source/vst/testsuite/general/editorclasses.cpp
    public.sdk/source/vst/testsuite/general/midilearn.cpp
    public.sdk/source/vst/testsuite/general/midimapping.cpp
    public.sdk/source/vst/testsuite/general/plugcompat.cpp
    public.sdk/source/vst/testsuite/general/scanparameters.cpp
    public.sdk/source/vst/testsuite/general/suspendresume.cpp
    public.sdk/source/vst/testsuite/general/terminit.cpp
    public.sdk/source/vst/testsuite/noteexpression/keyswitch.cpp
    public.sdk/source/vst/testsuite/noteexpression/noteexpression.cpp
    public.sdk/source/vst/testsuite/processing/automation.cpp
    public.sdk/source/vst/testsuite/processing/process.cpp
    public.sdk/source/vst/testsuite/processing/processcontextrequirements.cpp
    public.sdk/source/vst/testsuite/processing/processformat.cpp
    public.sdk/source/vst/testsuite/processing/processinputoverwriting.cpp
    public.sdk/source/vst/testsuite/processing/processtail.cpp
    public.sdk/source/vst/testsuite/processing/processthreaded.cpp
    public.sdk/source/vst/testsuite/processing/silenceflags.cpp
    public.sdk/source/vst/testsuite/processing/silenceprocessing.cpp
    public.sdk/source/vst/testsuite/processing/speakerarrangement.cpp
    public.sdk/source/vst/testsuite/processing/variableblocksize.cpp
    public.sdk/source/vst/testsuite/state/bypasspersistence.cpp
    public.sdk/source/vst/testsuite/state/invalidstatetransition.cpp
    public.sdk/source/vst/testsuite/state/repeatidenticalstatetransition.cpp
    public.sdk/source/vst/testsuite/state/validstatetransition.cpp
    public.sdk/source/vst/testsuite/testbase.cpp
    public.sdk/source/vst/testsuite/unit/checkunitstructure.cpp
    public.sdk/source/vst/testsuite/unit/scanprograms.cpp
    public.sdk/source/vst/testsuite/unit/scanunits.cpp
    public.sdk/source/vst/testsuite/vsttestsuite.cpp
    public.sdk/source/vst/utility/stringconvert.cpp
    public.sdk/source/vst/utility/testing.cpp
    public.sdk/source/vst/vstinitiids.cpp
    public.sdk/source/vst/vstpresetfile.cpp
)

# Sets format, bundle and arch from the command line.
parse_options() {
    local au="" vst3=""
    format=""
    arch=$(uname -m)

    while [ $# -gt 0 ]; do
        case "$1" in
            --format)    format=$2; shift ;;
            --au)        au=${2%/}; shift ;;
            --vst3)      vst3=${2%/}; shift ;;
            --arch)      arch=$2; shift ;;
            -h|--help)   usage; exit 0 ;;
            *)           echo "error: unknown option $1" >&2; usage >&2; exit 2 ;;
        esac
        shift
    done

    case "$format" in
        au)   bundle=${au:-$root/build/modern/Build/Products/Release/Synth1.component} ;;
        vst3) bundle=${vst3:-$root/build/modern/Build/Products/Release/Synth1.vst3} ;;
        "")   echo "error: --format is required (au or vst3)" >&2; usage >&2; exit 2 ;;
        *)    echo "error: --format must be au or vst3 (it is '$format')" >&2; exit 2 ;;
    esac
    case "$arch" in
        arm64|x86_64) ;;
        *) echo "error: --arch must be arm64 or x86_64 (it is '$arch')" >&2; exit 2 ;;
    esac
    if [ ! -d "$bundle" ]; then
        echo "error: no plug-in at $bundle" >&2
        exit 2
    fi
}

# Copies the component to the user's plug-in folder and waits until the system has registered it.
install_component() {
    local plugins=$HOME/Library/Audio/Plug-Ins/Components attempt
    mkdir -p "$plugins"
    rm -rf "$plugins/Synth1.component"
    ditto "$bundle" "$plugins/Synth1.component"
    killall -9 AudioComponentRegistrar 2>/dev/null || true

    for attempt in $(seq 30); do
        if auval -a 2>/dev/null | grep -q "aumu Syn1 daiC"; then
            return
        fi
        sleep 2
    done
    echo "error: Synth1 did not show up in auval -a" >&2
    exit 1
}

# Compiles the VST3 validator's sources in parallel and links it, unless it is already built.
build_vst3_validator() {
    local sdk=$root/src/vst3sdk
    local flags=(-arch "$arch" -isysroot "$(xcrun --show-sdk-path)" -mmacosx-version-min=12.0
                 -std=c++17 -O1 -w -DRELEASE=1 -I"$sdk")
    [ -x "$validator" ] && return
    mkdir -p "$build_dir/obj"
    echo "Building the VST3 validator ($arch)"
    printf '%s\n' "${SOURCES[@]}" | xargs -P "$(sysctl -n hw.ncpu)" -I{} sh -c '
        src=$1; shift
        obj="'"$build_dir"'/obj/$(echo "$src" | tr / _).o"
        case "$src" in
            *.mm) exec xcrun clang++ "$@" -fobjc-arc -x objective-c++ -c "'"$sdk"'/$src" -o "$obj" ;;
            *)    exec xcrun clang++ "$@" -c "'"$sdk"'/$src" -o "$obj" ;;
        esac' sh {} "${flags[@]}"
    xcrun clang++ -arch "$arch" -mmacosx-version-min=12.0 "$build_dir"/obj/*.o \
        -framework Cocoa -framework CoreFoundation -o "$validator.tmp"
    mv "$validator.tmp" "$validator"
}

root=$(cd "$(dirname "$0")/.." && pwd)
parse_options "$@"

case "$format" in
    au)
        install_component
        arch -"$arch" auval -v aumu Syn1 daiC
        ;;
    vst3)
        build_dir=$root/build/vst3-validator-$arch
        validator=$build_dir/validator
        build_vst3_validator
        arch -"$arch" "$validator" "$bundle"
        ;;
esac
