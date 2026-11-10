#!/bin/bash
# Checks a built Synth1.component: both architectures, the version from src/Version.xcconfig,
# and a valid signature.
#
#   scripts/check_component.sh [path/to/Synth1.component]
#
# Default: build/modern/Build/Products/Release/Synth1.component
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

root=$(cd "$(dirname "$0")/.." && pwd)
source "$root/scripts/version.sh"

component=${1:-$root/build/modern/Build/Products/Release/Synth1.component}
component=${component%/}
view_factory=$component/Contents/Resources/Synth1CocoaViewFactory.bundle
if [ ! -d "$component" ]; then
    echo "error: no component at $component" >&2
    exit 2
fi

version=$(marketing_version)
au_version=$(au_version)

echo "Checking $component"
check "plugin architectures" "$(architectures "$component/Contents/MacOS/Synth1")" "arm64 x86_64"
check "view architectures" "$(architectures "$view_factory/Contents/MacOS/Synth1CocoaViewFactory")" "arm64 x86_64"
check "plugin version" "$(plist_value "$component" CFBundleShortVersionString)" "$version"
check "view version" "$(plist_value "$view_factory" CFBundleShortVersionString)" "$version"
check "AU version" "$(printf '0x%08X' "$(plist_value "$component" :AudioComponents:0:version)")" "$au_version"
check "signature" "$(codesign --verify --deep --strict "$component" 2>&1 && echo valid)" "valid"

if [ $failures -gt 0 ]; then
    echo "$failures checks failed"
    exit 1
fi
