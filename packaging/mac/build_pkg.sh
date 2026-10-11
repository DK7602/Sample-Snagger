#!/usr/bin/env bash
# Builds "Sample Snagger-<version>-macOS.pkg" that installs:
#   AU         -> /Library/Audio/Plug-Ins/Components
#   VST3       -> /Library/Audio/Plug-Ins/VST3
#   Standalone -> /Applications
#
# usage: packaging/mac/build_pkg.sh <artefacts dir> <output dir>
#
# Signing and notarisation (optional - see SIGNING.md). Without these the installer is built as
# it always was, and macOS asks the user to allow it:
#   MACOS_INSTALLER_IDENTITY   "Developer ID Installer: ..." certificate to sign the .pkg with
#   APPLE_ID, APPLE_TEAM_ID, APPLE_APP_PASSWORD
#                              the Apple account that sends it to Apple's notary service
set -Eeuo pipefail
# (on GitHub Actions a failure shows up on the run's summary page, with the command that failed)
trap 'echo "::error title=macOS installer::build_pkg.sh line $LINENO: $BASH_COMMAND"' ERR

ART="${1:?artefacts dir, e.g. build/SampleSnagger_artefacts/Release}"
OUT="${2:-dist}"
HERE="$(cd "$(dirname "$0")" && pwd)"
ROOT="$(cd "$HERE/../.." && pwd)"
VERSION="$(grep -Eo 'project\(SampleSnagger VERSION [0-9.]+' "$ROOT/CMakeLists.txt" | awk '{print $3}')"
ID="com.snaggeraudio.samplesnagger"

WORK="$(mktemp -d)"
trap 'rm -rf "$WORK"' EXIT
mkdir -p "$OUT" "$WORK/au" "$WORK/vst3" "$WORK/app"

cp -R "$ART/AU/Sample Snagger.component"   "$WORK/au/"
cp -R "$ART/VST3/Sample Snagger.vst3"      "$WORK/vst3/"
cp -R "$ART/Standalone/Sample Snagger.app" "$WORK/app/"

# By default the Installer treats a bundle as "relocatable": if it finds another copy of it anywhere
# on the Mac (the bundles .zip unpacked in Downloads, an old build folder), it updates *that* copy
# and puts nothing in /Applications or /Library/Audio/Plug-Ins - the install "works" but the DAW
# never sees the plug-in. This pins every bundle to its install location.
pin_bundles() {   # <root dir> <plist to write>
    pkgbuild --analyze --root "$1" "$2" > /dev/null
    local i=0
    while /usr/libexec/PlistBuddy -c "Print :$i:RootRelativeBundlePath" "$2" > /dev/null 2>&1; do
        # (the key is only in the template for some kinds of bundle: change it, or put it there)
        /usr/libexec/PlistBuddy -c "Set :$i:BundleIsRelocatable false" "$2" > /dev/null 2>&1 \
            || /usr/libexec/PlistBuddy -c "Add :$i:BundleIsRelocatable bool false" "$2"
        i=$((i + 1))
    done
    echo "pinned $i bundle(s) in $(basename "$1")"
    [ "$i" -gt 0 ] || echo "::warning title=macOS installer::pkgbuild found no bundle in $(basename "$1"), so nothing was pinned there"
}
pin_bundles "$WORK/au"   "$WORK/au.plist"
pin_bundles "$WORK/vst3" "$WORK/vst3.plist"
pin_bundles "$WORK/app"  "$WORK/app.plist"

pkgbuild --root "$WORK/au"   --component-plist "$WORK/au.plist"   --install-location "/Library/Audio/Plug-Ins/Components" --identifier "$ID.au"   --version "$VERSION" "$WORK/au.pkg"
pkgbuild --root "$WORK/vst3" --component-plist "$WORK/vst3.plist" --install-location "/Library/Audio/Plug-Ins/VST3"       --identifier "$ID.vst3" --version "$VERSION" "$WORK/vst3.pkg"
pkgbuild --root "$WORK/app"  --component-plist "$WORK/app.plist"  --install-location "/Applications"                      --identifier "$ID.app"  --version "$VERSION" "$WORK/app.pkg"

cat > "$WORK/distribution.xml" <<EOF
<?xml version="1.0" encoding="utf-8"?>
<installer-gui-script minSpecVersion="2">
    <title>Sample Snagger ${VERSION}</title>
    <welcome file="welcome.html" mime-type="text/html"/>
    <options customize="allow" require-scripts="false" hostArchitectures="arm64,x86_64"/>
    <domains enable_localSystem="true"/>
    <choices-outline>
        <line choice="au"/>
        <line choice="vst3"/>
        <line choice="app"/>
    </choices-outline>
    <choice id="au"   title="Audio Unit (Logic Pro, GarageBand, ...)" description="Installs to /Library/Audio/Plug-Ins/Components"><pkg-ref id="$ID.au"/></choice>
    <choice id="vst3" title="VST3 (Ableton Live, FL Studio, Cubase, Studio One, Reaper, Bitwig ...)" description="Installs to /Library/Audio/Plug-Ins/VST3"><pkg-ref id="$ID.vst3"/></choice>
    <choice id="app"  title="Standalone app" description="Installs Sample Snagger.app to /Applications"><pkg-ref id="$ID.app"/></choice>
    <pkg-ref id="$ID.au">au.pkg</pkg-ref>
    <pkg-ref id="$ID.vst3">vst3.pkg</pkg-ref>
    <pkg-ref id="$ID.app">app.pkg</pkg-ref>
</installer-gui-script>
EOF

mkdir -p "$WORK/resources"
cat > "$WORK/resources/welcome.html" <<'EOF'
<html><body style="font-family:-apple-system,Helvetica;">
<h2>Sample Snagger</h2>
<p>Capture samples from any video or audio, chop them, separate vocals and music,
and drag the results straight into your DAW.</p>
<p>After installing, rescan plug-ins in your DAW. Open <b>Settings</b> (gear icon) inside
Sample Snagger once to install the free helper tools for HQ downloads and AI stems.</p>
</body></html>
EOF

PKG="$OUT/Sample-Snagger-${VERSION}-macOS.pkg"
INSTALLER_IDENTITY="${MACOS_INSTALLER_IDENTITY:-}"
if [ -n "$INSTALLER_IDENTITY" ]; then
    productbuild --distribution "$WORK/distribution.xml" --resources "$WORK/resources" --package-path "$WORK" \
                 --sign "$INSTALLER_IDENTITY" --timestamp "$PKG"
else
    productbuild --distribution "$WORK/distribution.xml" --resources "$WORK/resources" --package-path "$WORK" "$PKG"
fi

# A zip of the raw bundles too, for people who prefer to copy them by hand
# (ditto keeps everything a signature covers)
mkdir -p "$WORK/bundles"
cp -R "$WORK/au/"* "$WORK/vst3/"* "$WORK/app/"* "$WORK/bundles/"
ZIP="$OUT/Sample-Snagger-${VERSION}-macOS-bundles.zip"
rm -f "$ZIP"
ditto -c -k "$WORK/bundles" "$ZIP"

# Notarisation: Apple checks the signed files and from then on macOS opens them without the
# "can't be opened" warning. Needs files signed with a Developer ID (sign.sh, and the .pkg above).
notarise() {   # <file>
    local log="$WORK/notary.log"
    xcrun notarytool submit "$1" --apple-id "$APPLE_ID" --team-id "$APPLE_TEAM_ID" --password "$APPLE_APP_PASSWORD" --wait 2>&1 | tee "$log"
    if ! grep -q "status: Accepted" "$log"; then
        echo "::error title=macOS notarisation::Apple did not accept $(basename "$1") - see the log of this step (notarytool log <id> shows why)"
        return 1
    fi
}

if [ -n "${APPLE_ID:-}" ] && [ -n "${APPLE_TEAM_ID:-}" ] && [ -n "${APPLE_APP_PASSWORD:-}" ]; then
    if [ -n "$INSTALLER_IDENTITY" ]; then
        notarise "$PKG"
        xcrun stapler staple "$PKG"   # (the proof travels with the file: it opens without asking Apple)
    else
        echo "::warning title=macOS notarisation::The installer isn't signed (no Developer ID Installer certificate), so it wasn't notarised"
    fi
    if [ -n "${MACOS_SIGN_IDENTITY:-}" ]; then
        notarise "$ZIP"               # (a zip can't carry the proof; macOS looks it up when the bundles are first used)
    else
        echo "::warning title=macOS notarisation::The bundles aren't signed with a Developer ID, so they weren't notarised"
    fi
    echo "::notice title=macOS installer::Signed and notarised"
else
    echo "Not notarised (no Apple account was given)"
fi

echo "Built $PKG"
