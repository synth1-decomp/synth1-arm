#!/bin/bash
# Builds an installer package (.pkg) that installs Synth1.component into
# /Library/Audio/Plug-Ins/Components.
set -euo pipefail

usage() {
    cat <<'EOF'
Usage: scripts/build_pkg.sh [options]

Options:
  --config CONFIG   Debug or Release (default: Release). Picks the default component and the
                    name: Synth1-VERSION-BUILDID-release.pkg or ...-debug.pkg.
  --component PATH  The Synth1.component to package
                    (default: build/modern/Build/Products/CONFIG/Synth1.component).
  --output DIR      Where to write the package (default: build/pkg).
  -h, --help        Show this help.

The version comes from src/Version.xcconfig. The build ID is the one the plugin window shows:
the first 7 characters of the commit ID, or "dev" when tracked files have uncommitted changes.
EOF
}

# Sets config, component and output_dir from the command line.
parse_options() {
    config=Release
    component=""
    output_dir=$root/build/pkg

    while [ $# -gt 0 ]; do
        case "$1" in
            --config)    config=$2; shift ;;
            --component) component=${2%/}; shift ;;
            --output)    output_dir=${2%/}; shift ;;
            -h|--help)   usage; exit 0 ;;
            *)           echo "error: unknown option $1" >&2; usage >&2; exit 2 ;;
        esac
        shift
    done

    case "$config" in
        Debug|Release) ;;
        *) echo "error: --config must be Debug or Release (it is '$config')" >&2; exit 2 ;;
    esac
    component=${component:-$root/build/modern/Build/Products/$config/Synth1.component}
    if [ ! -d "$component" ]; then
        echo "error: no component at $component" >&2
        exit 2
    fi
}

# Copies the component into the package root, the folder whose contents get installed.
stage_payload() {
    mkdir -p "$work/root"
    ditto "$component" "$work/root/Synth1.component"
}

# Writes the component settings: always install to the install location (not to a copy found
# elsewhere on disk), and always replace the installed copy, even with an older version.
write_component_plist() {
    pkgbuild --analyze --root "$work/root" "$work/components.plist" > /dev/null
    plutil -replace 0.BundleIsRelocatable -bool NO "$work/components.plist"
    plutil -replace 0.BundleIsVersionChecked -bool NO "$work/components.plist"
}

# Writes the script the installer runs afterwards, so the system picks up the new component
# without a logout.
write_postinstall() {
    mkdir -p "$work/scripts"
    cat > "$work/scripts/postinstall" <<'EOF'
#!/bin/sh
killall -9 AudioComponentRegistrar 2>/dev/null
exit 0
EOF
    chmod +x "$work/scripts/postinstall"
}

build_package() {
    mkdir -p "$output_dir"
    pkgbuild --root "$work/root" \
             --component-plist "$work/components.plist" \
             --scripts "$work/scripts" \
             --identifier jp.daichi.synth1.pkg \
             --version "$version" \
             --install-location /Library/Audio/Plug-Ins/Components \
             "$package"
}

root=$(cd "$(dirname "$0")/.." && pwd)
source "$root/scripts/version.sh"

parse_options "$@"
version=$(marketing_version)
suffix=$(echo "$config" | tr '[:upper:]' '[:lower:]')
package=$output_dir/Synth1-$version-$(build_id)-$suffix.pkg
work=$(mktemp -d "${TMPDIR:-/tmp}/synth1-pkg.XXXXXX")
trap 'rm -rf "$work"' EXIT

stage_payload
write_component_plist
write_postinstall
build_package
echo "Built $package"
