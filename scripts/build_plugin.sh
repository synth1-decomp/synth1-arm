#!/bin/bash
# Builds Synth1.component or Synth1.vst3 (arm64 and x86_64) with src/Synth1.xcodeproj.
set -euo pipefail

usage() {
    cat <<'EOF'
Usage: scripts/build_plugin.sh --format au|vst3 [Debug|Release]

Builds the given format and configuration (default: Release) into
build/modern/Build/Products/CONFIG/Synth1.component (au) or Synth1.vst3 (vst3).
EOF
}

root=$(cd "$(dirname "$0")/.." && pwd)
format=""
config=Release

while [ $# -gt 0 ]; do
    case "$1" in
        --format)  format=$2; shift ;;
        -h|--help) usage; exit 0 ;;
        *)         config=$1 ;;
    esac
    shift
done

case "$config" in
    Debug|Release) ;;
    *) echo "error: the configuration must be Debug or Release (it is '$config')" >&2; exit 2 ;;
esac
case "$format" in
    au)   scheme=Synth1AudioUnit; product=Synth1.component ;;
    vst3) scheme=Synth1VST3;      product=Synth1.vst3 ;;
    "") echo "error: --format is required (au or vst3)" >&2; usage >&2; exit 2 ;;
    *)  echo "error: --format must be au or vst3 (it is '$format')" >&2; exit 2 ;;
esac

# The generic destination builds every architecture; "My Mac" would build arm64 only
xcodebuild -project "$root/src/Synth1.xcodeproj" \
           -scheme "$scheme" \
           -configuration "$config" \
           -destination 'generic/platform=macOS' \
           -derivedDataPath "$root/build/modern" \
           build

echo "Built $root/build/modern/Build/Products/$config/$product"
