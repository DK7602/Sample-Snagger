#!/usr/bin/env bash
# Builds "Sample Snagger-<version>-macOS.pkg" that installs:
#   AU         -> /Library/Audio/Plug-Ins/Components
#   VST3       -> /Library/Audio/Plug-Ins/VST3
#   Standalone -> /Applications
#
# usage: packaging/mac/build_pkg.sh <artefacts dir> <output dir>
set -euo pipefail

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

pkgbuild --root "$WORK/au"   --install-location "/Library/Audio/Plug-Ins/Components" --identifier "$ID.au"   --version "$VERSION" "$WORK/au.pkg"
pkgbuild --root "$WORK/vst3" --install-location "/Library/Audio/Plug-Ins/VST3"       --identifier "$ID.vst3" --version "$VERSION" "$WORK/vst3.pkg"
pkgbuild --root "$WORK/app"  --install-location "/Applications"                      --identifier "$ID.app"  --version "$VERSION" "$WORK/app.pkg"

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

productbuild --distribution "$WORK/distribution.xml" --resources "$WORK/resources" --package-path "$WORK" \
             "$OUT/Sample-Snagger-${VERSION}-macOS.pkg"

# A zip of the raw bundles too, for people who prefer to copy them by hand
mkdir -p "$WORK/bundles"
cp -R "$WORK/au/"* "$WORK/vst3/"* "$WORK/app/"* "$WORK/bundles/"
( cd "$WORK/bundles" && zip -qry "$WORK/bundles.zip" . )
mv "$WORK/bundles.zip" "$OUT/Sample-Snagger-${VERSION}-macOS-bundles.zip"

echo "Built $OUT/Sample-Snagger-${VERSION}-macOS.pkg"
