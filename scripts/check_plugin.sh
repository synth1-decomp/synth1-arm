#!/bin/bash
# Checks a built Synth1.component or Synth1.vst3: both architectures, the version from
# src/Version.xcconfig and a valid signature; for the VST3 also the bundle identifier and that only
# the VST3 entry points are exported.
#
#   scripts/check_plugin.sh --format au|vst3 [path/to/Synth1.component or Synth1.vst3]
#
# Default: build/modern/Build/Products/Release/Synth1.component (au) or Synth1.vst3 (vst3)
set -euo pipefail

failures=0

# Prints a check's result and counts the failures.
#   $1 = description, $2 = actual value, $3 = expected value
check() {
    if [ "$2" = "$3" ]; then
        echo "ok    $1: $2"
    else
        echo "FAIL  $1: $2, expected $3"
        failures=$((failures + 1))
    fi
}

# Prints the architectures in a binary, sorted, for example "arm64 x86_64".
architectures() {
    lipo -archs "$1" | tr ' ' '\n' | sort | tr '\n' ' ' | sed 's/ $//'
}

# Prints a value from a bundle's Info.plist.
#   $1 = bundle, $2 = PlistBuddy key path
plist_value() {
    /usr/libexec/PlistBuddy -c "Print $2" "$1/Contents/Info.plist" 2>/dev/null || echo "(missing)"
}

# Prints the symbols a binary exports for one architecture, sorted, on one line.
#   $1 = binary, $2 = architecture
exports() {
    nm -gU -arch "$2" "$1" | awk '{print $3}' | LC_ALL=C sort | tr '\n' ' ' | sed 's/ $//'
}

root=$(cd "$(dirname "$0")/.." && pwd)
source "$root/scripts/version.sh"

format=""
if [ "${1:-}" = --format ]; then
    format=${2:-}
    shift 2 || shift $#
fi
case "$format" in
    au)   bundle=${1:-$root/build/modern/Build/Products/Release/Synth1.component} ;;
    vst3) bundle=${1:-$root/build/modern/Build/Products/Release/Synth1.vst3} ;;
    "")   echo "error: --format is required (au or vst3)" >&2; exit 2 ;;
    *)    echo "error: --format must be au or vst3 (it is '$format')" >&2; exit 2 ;;
esac
bundle=${bundle%/}
if [ ! -d "$bundle" ]; then
    echo "error: no plug-in at $bundle" >&2
    exit 2
fi

version=$(marketing_version)

echo "Checking $bundle"
check "plugin architectures" "$(architectures "$bundle/Contents/MacOS/Synth1")" "arm64 x86_64"
check "plugin version" "$(plist_value "$bundle" CFBundleShortVersionString)" "$version"
case "$format" in
    au)
        view_factory=$bundle/Contents/Resources/Synth1CocoaViewFactory.bundle
        check "view architectures" "$(architectures "$view_factory/Contents/MacOS/Synth1CocoaViewFactory")" "arm64 x86_64"
        check "view version" "$(plist_value "$view_factory" CFBundleShortVersionString)" "$version"
        check "AU version" "$(printf '0x%08X' "$(plist_value "$bundle" :AudioComponents:0:version)")" "$(au_version)"
        ;;
    vst3)
        check "bundle identifier" "$(plist_value "$bundle" CFBundleIdentifier)" "jp.daichi.synth1.vst3"
        for arch in arm64 x86_64; do
            check "$arch exports" "$(exports "$bundle/Contents/MacOS/Synth1" "$arch")" \
                "_GetPluginFactory _bundleEntry _bundleExit"
        done
        ;;
esac
check "signature" "$(codesign --verify --deep --strict "$bundle" 2>&1 && echo valid)" "valid"

if [ $failures -gt 0 ]; then
    echo "$failures checks failed"
    exit 1
fi
