#!/bin/bash
# Installs a Synth1.component for this user and runs Apple's Audio Unit validator (auval) on it.
#
# This replaces any Synth1.component in ~/Library/Audio/Plug-Ins/Components. It is meant for CI;
# locally, auval -v aumu Syn1 daiC checks the plugin you already have installed.
set -euo pipefail

usage() {
    cat <<'EOF'
Usage: scripts/run_auval_tests.sh [options]

Options:
  --component PATH  The Synth1.component to validate
                    (default: build/modern/Build/Products/Release/Synth1.component).
  --arch ARCH       arm64 or x86_64 (default: this Mac's). x86_64 runs under Rosetta on Apple Silicon.
  -h, --help        Show this help.
EOF
}

# Sets component and arch from the command line.
parse_options() {
    component=$root/build/modern/Build/Products/Release/Synth1.component
    arch=$(uname -m)

    while [ $# -gt 0 ]; do
        case "$1" in
            --component) component=${2%/}; shift ;;
            --arch)      arch=$2; shift ;;
            -h|--help)   usage; exit 0 ;;
            *)           echo "error: unknown option $1" >&2; usage >&2; exit 2 ;;
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
}

# Copies the component to the user's plug-in folder and waits until the system has registered it.
install_component() {
    local attempt
    mkdir -p "$plugins"
    rm -rf "$plugins/Synth1.component"
    ditto "$component" "$plugins/Synth1.component"
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

root=$(cd "$(dirname "$0")/.." && pwd)
plugins=$HOME/Library/Audio/Plug-Ins/Components

parse_options "$@"
install_component
arch -"$arch" auval -v aumu Syn1 daiC
