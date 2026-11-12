#!/bin/bash
# Builds an installer package (.pkg) for one plug-in format:
# - Synth1.component into /Library/Audio/Plug-Ins/Components
# - Synth1.vst3 into /Library/Audio/Plug-Ins/VST3
set -euo pipefail

usage() {
    cat <<'EOF'
Usage: scripts/build_pkg.sh --format au|vst3 [options]

Options:
  --format FORMAT   au or vst3 (required). Picks the plug-in to package and the name:
                    Synth1-AU-VERSION-BUILDID-CONFIG.pkg or Synth1-VST3-VERSION-BUILDID-CONFIG.pkg.
  --config CONFIG   Debug or Release (default: Release). Picks the default plug-in and the
                    CONFIG part of the name: release or debug.
  --au PATH         The Synth1.component to package with --format au
                    (default: build/modern/Build/Products/CONFIG/Synth1.component).
  --vst3 PATH       The Synth1.vst3 to package with --format vst3
                    (default: build/modern/Build/Products/CONFIG/Synth1.vst3).
  --output DIR      Where to write the package (default: build/pkg).
  -h, --help        Show this help.

The version comes from src/Version.xcconfig. The build ID is the one the plugin window shows:
the first 7 characters of the commit ID, or "dev" when tracked files have uncommitted changes.
EOF
}

# Sets format, config, au, vst3 and output_dir from the command line.
parse_options() {
    format=""
    config=Release
    au=""
    vst3=""
    output_dir=$root/build/pkg

    while [ $# -gt 0 ]; do
        case "$1" in
            --format)    format=$2; shift ;;
            --config)    config=$2; shift ;;
            --au)        au=${2%/}; shift ;;
            --vst3)      vst3=${2%/}; shift ;;
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
}

# Sets the format's plug-in bundle and package settings: bundle, bundle_name, identifier,
# install_location, name_prefix and postinstall (whether the package runs a postinstall script).
# Each format has its own package identifier, so the two installers keep separate receipts.
set_format() {
    local products=$root/build/modern/Build/Products/$config
    case "$format" in
        au)
            bundle=${au:-$products/Synth1.component}
            bundle_name=Synth1.component
            identifier=jp.daichi.synth1.pkg
            install_location=/Library/Audio/Plug-Ins/Components
            name_prefix=Synth1-AU
            postinstall=yes
            ;;
        vst3)
            bundle=${vst3:-$products/Synth1.vst3}
            bundle_name=Synth1.vst3
            identifier=jp.daichi.synth1.vst3.pkg
            install_location=/Library/Audio/Plug-Ins/VST3
            name_prefix=Synth1-VST3
            postinstall=no
            ;;
        "")
            echo "error: --format is required (au or vst3)" >&2
            usage >&2
            exit 2
            ;;
        *)
            echo "error: --format must be au or vst3 (it is '$format')" >&2
            exit 2
            ;;
    esac
    if [ ! -d "$bundle" ]; then
        echo "error: no $bundle_name at $bundle" >&2
        exit 2
    fi
}

# Copies the plug-in into the package root, the folder whose contents get installed.
stage_payload() {
    mkdir -p "$work/root"
    ditto "$bundle" "$work/root/$bundle_name"
}

# Writes the component settings: always install to the install location (not to a copy found
# elsewhere on disk), and always replace the installed copy, even with an older version.
write_component_plist() {
    pkgbuild --analyze --root "$work/root" "$work/components.plist" > /dev/null
    plutil -replace 0.BundleIsRelocatable -bool NO "$work/components.plist"
    plutil -replace 0.BundleIsVersionChecked -bool NO "$work/components.plist"
}

# Writes the script the installer runs afterwards, so the system picks up the new component
# without a logout (Audio Units only).
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
    local scripts=()
    [ "$postinstall" = yes ] && scripts=(--scripts "$work/scripts")
    mkdir -p "$output_dir"
    pkgbuild --root "$work/root" \
             --component-plist "$work/components.plist" \
             ${scripts[@]+"${scripts[@]}"} \
             --identifier "$identifier" \
             --version "$version" \
             --install-location "$install_location" \
             "$package"
}

root=$(cd "$(dirname "$0")/.." && pwd)
source "$root/scripts/version.sh"

parse_options "$@"
set_format
version=$(marketing_version)
suffix=$(echo "$config" | tr '[:upper:]' '[:lower:]')
package=$output_dir/$name_prefix-$version-$(build_id)-$suffix.pkg
work=$(mktemp -d "${TMPDIR:-/tmp}/synth1-pkg.XXXXXX")
trap 'rm -rf "$work"' EXIT

stage_payload
write_component_plist
[ "$postinstall" = yes ] && write_postinstall
build_package
echo "Built $package"
