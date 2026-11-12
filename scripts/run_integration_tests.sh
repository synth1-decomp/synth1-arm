#!/bin/bash
# Builds a test host and runs it against a Synth1.component or Synth1.vst3 built with the current
# Xcode.
#
# The AU tests (tests/au/au_tests.c) are built with SYNTH1_MODERN_BUILD; the VST3 tests
# (tests/vst3/vst3_tests.cpp) drive the plug-in through the VST3 interfaces. Both are kept away
# from the user's Synth1 settings and files (tests/au/au_isolation.c).
set -euo pipefail

usage() {
    cat <<'EOF'
Usage: scripts/run_integration_tests.sh --format au|vst3 [options] [filter ...]

Options:
  --format FORMAT   au or vst3: the plug-in to test (required).
  --au PATH         The Synth1.component to test with --format au
                    (default: build/modern/Build/Products/Release/Synth1.component).
  --vst3 PATH       The Synth1.vst3 to test with --format vst3
                    (default: build/modern/Build/Products/Release/Synth1.vst3).
  --arch ARCH       arm64 or x86_64 (default: this Mac's). The plug-in must contain it.
                    x86_64 runs under Rosetta on Apple Silicon.
  -h, --help        Show this help.

Filters only run the tests whose name contains one of them.

The tests never touch your Synth1 settings or ~/Documents. The plug-in gets default settings
and a temporary folder for its patches, which is removed afterwards.
EOF
}

# Sets format, bundle, arch and filters from the command line.
parse_options() {
    local au="" vst3=""
    format=""
    arch=$(uname -m)
    filters=()

    while [ $# -gt 0 ]; do
        case "$1" in
            --format)    format=$2; shift ;;
            --au)        au=${2%/}; shift ;;
            --vst3)      vst3=${2%/}; shift ;;
            --arch)      arch=$2; shift ;;
            -h|--help)   usage; exit 0 ;;
            -*)          echo "error: unknown option $1" >&2; usage >&2; exit 2 ;;
            *)           filters+=("$1") ;;
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
    bundle=$(cd "$bundle" && pwd)
    if ! lipo "$bundle/Contents/MacOS/Synth1" -verify_arch "$arch" 2>/dev/null; then
        echo "error: $bundle has no $arch code" >&2
        exit 2
    fi
}

# Compiles the isolation library and the test host, linked against it, for the chosen architecture.
build() {
    local target=(-arch "$arch" -isysroot "$(xcrun --show-sdk-path)" -mmacosx-version-min=12.0)
    local sdk=$root/src/vst3sdk
    mkdir -p "$build_dir"

    xcrun clang "${target[@]}" -dynamiclib -O1 -g -Wall -Wno-deprecated-declarations \
        "$root/tests/au/au_isolation.c" -framework CoreFoundation -framework CoreServices \
        -install_name @executable_path/libau_isolation.dylib -o "$build_dir/libau_isolation.dylib"

    case "$format" in
        au)
            xcrun clang "${target[@]}" -std=gnu99 -O1 -g -Wall -Wno-four-char-constants \
                -DSYNTH1_MODERN_BUILD -DSYNTH1_EXPECTED_AU_VERSION="$(au_version)" \
                "$root/tests/au/au_tests.c" "$build_dir/libau_isolation.dylib" \
                -framework AudioToolbox -framework AudioUnit -framework CoreFoundation \
                -o "$test_binary"
            ;;
        vst3)
            # with the few VST3 SDK hosting classes the host uses
            xcrun clang++ "${target[@]}" -std=c++17 -O1 -g -Wall -DRELEASE=1 -I"$sdk" \
                "$root/tests/vst3/vst3_tests.cpp" \
                "$sdk/pluginterfaces/base/coreiids.cpp" \
                "$sdk/pluginterfaces/base/funknown.cpp" \
                "$sdk/public.sdk/source/vst/vstinitiids.cpp" \
                "$sdk/public.sdk/source/vst/hosting/eventlist.cpp" \
                "$sdk/public.sdk/source/vst/hosting/parameterchanges.cpp" \
                "$build_dir/libau_isolation.dylib" \
                -framework CoreFoundation \
                -o "$test_binary"
            ;;
    esac
}

# Runs the tests with Synth1's settings and folders in a temporary home folder (see
# tests/au/au_isolation.c). The AU host runs each golden scenario in a fresh child process of itself.
run_tests() {
    local status=0 args=()
    test_home=$(mktemp -d "${TMPDIR:-/tmp}/synth1-$format-tests.XXXXXX")
    trap 'rm -rf "$test_home"' EXIT

    case "$format" in
        au)
            echo "Testing $bundle ($arch, expecting AU version $(au_version))"
            args=("$root/tests/au/reference.txt" "$root/tests/au/default_state.plist")
            ;;
        vst3)
            echo "Testing $bundle ($arch)"
            ;;
    esac
    SYNTH1_TEST_HOME=$test_home "$test_binary" "$bundle" ${args[@]+"${args[@]}"} \
        "${filters[@]+"${filters[@]}"}" || status=$?
    return $status
}

root=$(cd "$(dirname "$0")/.." && pwd)
source "$root/scripts/version.sh"

parse_options "$@"
build_dir=$root/build/$format-tests-modern-$arch
test_binary=$build_dir/${format}_tests

build
run_tests
