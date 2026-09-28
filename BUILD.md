# Building Sample Snagger

Everything (JUCE 8.0.9, Signalsmith Stretch, demucs.cpp + Eigen for the built-in AI, and on Windows the WebView2 SDK)
is downloaded automatically by CMake. Pass `-DSNAGGER_BUILTIN_AI=OFF` to leave the AI engine out. You only need a compiler and CMake 3.24+.

## Option A - let GitHub build it (no dev tools needed)
1. Create a repository on GitHub and push this folder to it.
2. Open the **Actions** tab -> *Build Sample Snagger* runs automatically on every push.
3. When it's green, download the installers from the run's **Artifacts** section.
4. To publish a release: put `[release]` in a commit message on `main`, or push a tag such as `v1.2.0`.
   The installers are attached to a GitHub Release named after the version in `CMakeLists.txt`.

## Option B - build on your own machine

### macOS (Xcode 15+ command line tools, CMake, Ninja)
```bash
brew install cmake ninja
cmake -B build -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_OSX_ARCHITECTURES="arm64;x86_64"
cmake --build build --parallel
./build/SnaggerTests_artefacts/Release/SnaggerTests          # optional: run the tests (--ai also tests the AI stems)
bash packaging/mac/build_pkg.sh build/SampleSnagger_artefacts/Release dist
```
To just copy the plug-ins yourself:
```bash
cp -R "build/SampleSnagger_artefacts/Release/AU/Sample Snagger.component" ~/Library/Audio/Plug-Ins/Components/
cp -R "build/SampleSnagger_artefacts/Release/VST3/Sample Snagger.vst3"    ~/Library/Audio/Plug-Ins/VST3/
```
Tip: add `-DJUCE_COPY_PLUGIN_AFTER_BUILD=ON` to install them automatically after every build.

### Windows (Visual Studio 2022 with "Desktop development with C++", CMake)
```powershell
cmake -B build -G "Visual Studio 17 2022" -A x64
cmake --build build --config Release --parallel
build\SnaggerTests_artefacts\Release\SnaggerTests.exe          # optional
iscc /DAppVersion=1.0.0 /DArtefacts="%CD%\build\SampleSnagger_artefacts\Release" packaging\windows\SampleSnagger.iss
```
(Inno Setup 6 provides `iscc`.) Or copy `build\SampleSnagger_artefacts\Release\VST3\Sample Snagger.vst3` into
`C:\Program Files\Common Files\VST3`.

### Linux
```bash
sudo apt install build-essential cmake ninja-build libasound2-dev libjack-jackd2-dev libx11-dev libxext-dev \
  libxrandr-dev libxinerama-dev libxcursor-dev libfreetype-dev libfontconfig1-dev libcurl4-openssl-dev \
  libwebkit2gtk-4.1-dev libgtk-3-dev
cmake -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build --parallel
```

## CMake options
| Option | Default | |
|---|---|---|
| `SNAGGER_WEB_BROWSER` | `ON` | the built-in browser (LIVE REC, hindsight). `OFF` keeps HQ SNAG via pasted links. |
| `SNAGGER_BUILD_AAX` | `OFF` | also build an AAX plug-in for Pro Tools |
| `SNAGGER_BUILD_TESTS` | `ON` | the `SnaggerTests` runner |
| `SNAGGER_LINUX_MINIMAL` | `OFF` | headless Linux builds without ALSA/JACK/Xrandr headers |

## Pro Tools (AAX)
JUCE 8 ships the AAX SDK, so `-DSNAGGER_BUILD_AAX=ON` builds `Sample Snagger.aaxplugin`.
Unsigned AAX plug-ins only load in **Pro Tools Developer** (free from Avid's developer program). To load in
regular Pro Tools the plug-in must be signed with PACE / iLok tools (free for Avid developers, requires an account).

## Test runner
```
SnaggerTests                    unit tests (edits, stretch, chops, BPM, stem split, file import, sampler, state)
SnaggerTests --network          + downloads the helper tools and runs a real HQ SNAG against a local web server
SnaggerTests --shots <dir>      + renders every screen to PNG
```

## Code map
```
Source/
  PluginProcessor.*    audio engine: MIDI chop sampler, preview player, input recorder, state
  PluginEditor.*       window, header, tabs, drag-and-drop import, toasts
  Actions.*            background jobs: import, HQ SNAG (yt-dlp), stem split, pitch/stretch, save/drag
  StandaloneBridge.*   audio settings / input for the standalone app
  core/
    AudioData.h        immutable audio + Clip (name, chops, undo/redo)
    Session.*          the tray; clips cached as WAV and saved with the DAW project
    EditOps.*          trim/cut/fade/normalize/reverse/filter, Signalsmith pitch+time, transients, BPM
    QuickSplit.*       instant STFT stem separator (centre extraction + harmonic/percussive masks)
    AiStems.*          built-in AI stems: model downloads, resampling, 4 / 6 stem and vocal modes
    WebCapture.*       receives audio from the browser page (hindsight ring + recorder)
    Tools.*            finds / installs FFmpeg, yt-dlp, Deno, the AI models and the optional Python AI
    Jobs.*, Process.h  background job runner, command-line process runner
    AudioFileIO.*      loading any audio/video (FFmpeg fallback), WAV export
  ai/DemucsBridge.*    runs demucs.cpp (Demucs v4 in C++/Eigen) in parallel chunks; the only code that sees Eigen
  ui/                  Theme (gold plate, black glass, satin controls, fonts, icons), pages, waveform, tray, settings
Resources/
  Scripts/webtap.js     injected into web pages to tap <video>/<audio> audio
  Scripts/snagger_ai.py  optional Python AI engine (GPU): installs Demucs in a private venv
  gold_plate.jpg         the faceplate texture, rendered by Design/make_gold.py
```
