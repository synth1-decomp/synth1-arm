#!/bin/bash
# Builds Synth1.component (arm64 and x86_64) with src/Synth1.xcodeproj.
set -euo pipefail

usage() {
    cat <<'EOF'
Usage: scripts/build_component.sh [Debug|Release]

Builds the given configuration (default: Release) into
build/modern/Build/Products/CONFIG/Synth1.component.
EOF
}

root=$(cd "$(dirname "$0")/.." && pwd)
config=${1:-Release}

case "$config" in
    Debug|Release) ;;
    -h|--help) usage; exit 0 ;;
    *) echo "error: the configuration must be Debug or Release (it is '$config')" >&2; exit 2 ;;
esac

# The generic destination builds every architecture; "My Mac" would build arm64 only
xcodebuild -project "$root/src/Synth1.xcodeproj" \
           -scheme Synth1AudioUnit \
           -configuration "$config" \
           -destination 'generic/platform=macOS' \
           -derivedDataPath "$root/build/modern" \
           build

echo "Built $root/build/modern/Build/Products/$config/Synth1.component"
