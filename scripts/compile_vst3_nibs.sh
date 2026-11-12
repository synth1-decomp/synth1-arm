#!/bin/sh
# Build phase of the Synth1VST3 target: compiles the dialog nibs from copies of the xibs
# with the Objective-C classes renamed as in src/vst3/vst3_prefix.h, so the nibs
# instantiate the VST3's own classes and not the Audio Unit's (see that header).
set -eu

: "${SRCROOT:?}" "${DERIVED_FILE_DIR:?}" "${TARGET_BUILD_DIR:?}" "${UNLOCALIZED_RESOURCES_FOLDER_PATH:?}"
: "${MACOSX_DEPLOYMENT_TARGET:?}"

# One pair of sed expressions per "#define Name  S1V3_Name" line of the prefix header. A xib names
# a class either as customClass="Name" (current format, IDD_OPTION) or as element text
# >Name</string> (the old IB 3 format of IDD_SAVESOUND: NSClassName and NSWindowClass).
script="$DERIVED_FILE_DIR/vst3_nib_renames.sed"
mkdir -p "$DERIVED_FILE_DIR"
sed -n 's/^#define[[:space:]]\{1,\}\([A-Za-z0-9_]\{1,\}\)[[:space:]]\{1,\}\(S1V3_[A-Za-z0-9_]\{1,\}\).*$/\1 \2/p' \
    "$SRCROOT/vst3/vst3_prefix.h" |
while read -r name renamed; do
    echo "s|customClass=\"$name\"|customClass=\"$renamed\"|g"
    echo "s|>$name</string>|>$renamed</string>|g"
done > "$script"

out="$TARGET_BUILD_DIR/$UNLOCALIZED_RESOURCES_FOLDER_PATH"
mkdir -p "$out"
for name in IDD_OPTION IDD_SAVESOUND; do
    sed -f "$script" "$SRCROOT/resources/$name.xib" > "$DERIVED_FILE_DIR/$name.xib"
    xcrun ibtool --errors --warnings --output-format human-readable-text \
        --minimum-deployment-target "$MACOSX_DEPLOYMENT_TARGET" \
        --compile "$out/$name.nib" "$DERIVED_FILE_DIR/$name.xib"
done
