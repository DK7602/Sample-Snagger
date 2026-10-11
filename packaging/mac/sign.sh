#!/usr/bin/env bash
# Signs the three bundles (Audio Unit, VST3, standalone app).
#
#   MACOS_SIGN_IDENTITY set   -> signed with that Developer ID Application certificate, with the
#                                hardened runtime and a timestamp: what Apple's notarisation needs.
#   MACOS_SIGN_IDENTITY unset -> signed "ad hoc" (no certificate), as the builds always were:
#                                they run, but macOS asks the user to allow them first.
#
# usage: packaging/mac/sign.sh <artefacts dir>     (see SIGNING.md)
set -Eeuo pipefail
trap 'echo "::error title=macOS signing::sign.sh line $LINENO: $BASH_COMMAND"' ERR

ART="${1:?artefacts dir, e.g. build/SampleSnagger_artefacts/Release}"
HERE="$(cd "$(dirname "$0")" && pwd)"
IDENTITY="${MACOS_SIGN_IDENTITY:-}"

for bundle in "$ART/AU/Sample Snagger.component" "$ART/VST3/Sample Snagger.vst3" "$ART/Standalone/Sample Snagger.app"; do
    if [ -n "$IDENTITY" ]; then
        codesign --force --deep --options runtime --timestamp \
                 --entitlements "$HERE/entitlements.plist" -s "$IDENTITY" "$bundle"
    else
        codesign --force --deep -s - "$bundle"
    fi
    codesign --verify --deep --strict "$bundle"
done

if [ -n "$IDENTITY" ]; then
    echo "Signed with: $IDENTITY"
else
    echo "Signed ad hoc (no Developer ID certificate was given)"
fi
