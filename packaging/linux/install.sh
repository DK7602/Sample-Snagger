#!/usr/bin/env bash
# Installs Sample Snagger for the current user on Linux.
#   VST3 -> ~/.vst3   LV2 -> ~/.lv2   app -> ~/.local/bin/sample-snagger
set -euo pipefail
HERE="$(cd "$(dirname "$0")" && pwd)"

mkdir -p ~/.vst3 ~/.lv2 ~/.local/bin
rm -rf ~/.vst3/"Sample Snagger.vst3" ~/.lv2/"Sample Snagger.lv2"
cp -R "$HERE/Sample Snagger.vst3" ~/.vst3/
cp -R "$HERE/Sample Snagger.lv2" ~/.lv2/
install -m 755 "$HERE/Sample Snagger" ~/.local/bin/sample-snagger

echo "Installed. Rescan plug-ins in your DAW, or run: sample-snagger"
echo "The built-in browser needs WebKitGTK (e.g. sudo apt install libwebkit2gtk-4.1-0)."
