#!/bin/bash
# Builds the Audio Unit test host and runs it against a Synth1.component built with the current Xcode.
#
# The tests (tests/au/au_tests.c) are built with SYNTH1_MODERN_BUILD and kept away from the user's
# Synth1 settings and files (tests/au/au_isolation.c).
set -euo pipefail

usage() {
    cat <<'EOF'
Usage: scripts/run_au_tests.sh [options] [filter ...]

Options:
  --component PATH  The Synth1.component to test
                    (default: build/modern/Build/Products/Release/Synth1.component).
  --arch ARCH       arm64 or x86_64 (default: this Mac's). The component must contain it.
                    x86_64 runs under Rosetta on Apple Silicon.
  -h, --help        Show this help.

Filters only run the tests whose name contains one of them.

The tests never touch your Synth1 settings or ~/Documents. The Audio Unit gets default settings
and a temporary folder for its patches, which is removed afterwards.
EOF
}

# Sets component, arch and filters from the command line.
parse_options() {
    component=$root/build/modern/Build/Products/Release/Synth1.component
    arch=$(uname -m)
    filters=()

    while [ $# -gt 0 ]; do
        case "$1" in
            --component) component=${2%/}; shift ;;
            --arch)      arch=$2; shift ;;
            -h|--help)   usage; exit 0 ;;
            -*)          echo "error: unknown option $1" >&2; usage >&2; exit 2 ;;
            *)           filters+=("$1") ;;
        esac
        shift
    done

    case "$arch" in
        arm64|x86_64) ;;
        *) echo "error: --arch must be arm64 or x86_64 (it is '$arch')" >&2; exit 2 ;;
    esac
    if [ ! -d "$component" ]; then
        echo "error: no component at $component" >&2
        exit 2
    fi
    component=$(cd "$component" && pwd)
    if ! lipo "$component/Contents/MacOS/Synth1" -verify_arch "$arch" 2>/dev/null; then
        echo "error: $component has no $arch code" >&2
        exit 2
    fi
}

# Compiles the isolation library and the test host, linked against it, for the chosen architecture.
build() {
    local target=(-arch "$arch" -isysroot "$(xcrun --show-sdk-path)" -mmacosx-version-min=12.0)
    mkdir -p "$build_dir"

    xcrun clang "${target[@]}" -dynamiclib -O1 -g -Wall -Wno-deprecated-declarations \
        "$tests/au_isolation.c" -framework CoreFoundation -framework CoreServices \
        -install_name @executable_path/libau_isolation.dylib -o "$build_dir/libau_isolation.dylib"

    xcrun clang "${target[@]}" -std=gnu99 -O1 -g -Wall -Wno-four-char-constants \
        -DSYNTH1_MODERN_BUILD -DSYNTH1_EXPECTED_AU_VERSION="$au_version" \
        "$tests/au_tests.c" "$build_dir/libau_isolation.dylib" \
        -framework AudioToolbox -framework AudioUnit -framework CoreFoundation \
        -o "$test_binary"
}

# Runs the tests with Synth1's settings and folders in a temporary home folder (see
# tests/au/au_isolation.c). The host runs each golden scenario in a fresh child process of itself.
run_tests() {
    local status=0
    test_home=$(mktemp -d "${TMPDIR:-/tmp}/synth1-au-tests.XXXXXX")
    trap 'rm -rf "$test_home"' EXIT

    echo "Testing $component ($arch, expecting AU version $au_version)"
    SYNTH1_TEST_HOME=$test_home "$test_binary" "$component" "$tests/reference.txt" "$tests/default_state.plist" \
        "${filters[@]+"${filters[@]}"}" || status=$?
    return $status
}

root=$(cd "$(dirname "$0")/.." && pwd)
tests=$root/tests/au
source "$root/scripts/version.sh"

parse_options "$@"
build_dir=$root/build/au-tests-modern-$arch
test_binary=$build_dir/au_tests
au_version=$(au_version)

build
run_tests
