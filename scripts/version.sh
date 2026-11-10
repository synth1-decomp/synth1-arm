# Reads the version from src/Version.xcconfig. Source this file; it defines:
#
#   version_setting NAME   prints one setting, for example version_setting SYNTH1_VERSION_MINOR
#   marketing_version      prints the version, for example 1.14.0
#   au_version             prints the Audio Unit version, for example 0x00010E00 (0xMMMMmmbb)
#   build_id               prints the first 7 characters of the commit ID, or "dev"
#
# It expects $root to be the repository root.

version_setting() {
    local value
    value=$(sed -n "s/^[[:space:]]*$1[[:space:]]*=[[:space:]]*\([0-9][0-9]*\)[[:space:]]*$/\1/p" \
        "$root/src/Version.xcconfig")
    if [ -z "$value" ]; then
        echo "error: cannot read $1 from src/Version.xcconfig" >&2
        return 1
    fi
    echo "$value"
}

marketing_version() {
    echo "$(version_setting SYNTH1_VERSION_MAJOR).$(version_setting SYNTH1_VERSION_MINOR).$(version_setting SYNTH1_VERSION_BUGFIX)"
}

au_version() {
    local major minor bugfix
    major=$(version_setting SYNTH1_VERSION_MAJOR)
    minor=$(version_setting SYNTH1_VERSION_MINOR)
    bugfix=$(version_setting SYNTH1_VERSION_BUGFIX)
    printf '0x%08X\n' $(( (major << 16) | (minor << 8) | bugfix ))
}

build_id() {
    local commit
    if ! commit=$(git -C "$root" rev-parse HEAD 2>/dev/null); then
        echo "dev"                      # not a git repository
    elif ! git -C "$root" diff --quiet HEAD --; then
        echo "dev"                      # tracked files have uncommitted changes
    else
        printf '%.7s\n' "$commit"       # clean: the short commit ID
    fi
}
