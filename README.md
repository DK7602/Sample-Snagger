# Sample Snagger

**Capture · Chop · Separate.** A sampling plug-in and standalone app in gloss black, gold and neon red.
Browse YouTube (or any site) inside the plug-in, grab the audio the moment you hear something,
chop it, pitch it, split vocals from music, and drag the result straight onto a DAW track.

![Studio](docs/screenshots/studio.png)
![Stems](docs/screenshots/stems.png)

| Format | Where it works |
|---|---|
| **VST3** | Ableton Live, FL Studio, Cubase, Studio One, Reaper, Bitwig, Cakewalk... (Mac + Windows) |
| **AU** | Logic Pro, GarageBand, MainStage, Ableton on Mac |
| **Standalone app** | Mac + Windows (+ Linux) |
| **AAX** (optional) | Pro Tools (see [BUILD.md](BUILD.md#pro-tools-aax)) |
| **LV2** | Linux hosts |

---

## What it does

### 1. BROWSE - get audio from anywhere
- **Built-in web browser.** YouTube opens by default; one-click links for SoundCloud, TikTok, Instagram,
  Vimeo, Bandcamp, Internet Archive and Freesound. Type search words in the address bar to search YouTube.
- **LIVE REC** records whatever is playing on the page, like a tape deck. Press it, play the video, press again.
- **Hindsight: GRAB LAST 10s / 20s / 30s / 60s.** Sample Snagger quietly keeps the last minute of audio that played,
  so when you hear something great you can grab it *after* it happened.
- **HQ SNAG** downloads the page's audio in full quality (via yt-dlp - works with YouTube and 1,000+ sites).
  Use **SET IN / SET OUT** while the video plays to snag just a section.
- **IMPORT** any audio or video file (mp3, wav, flac, aiff, ogg, m4a, mp4, mov, mkv, webm, avi...) - or just drop
  files anywhere on the window.
- **REC INPUT** records your audio interface (standalone) or whatever your DAW routes into the plug-in.

Every capture lands in the **Session tray** at the bottom. Tray clips are saved with your DAW project.

### 2. STUDIO - edit and chop
- Zoomable waveform with overview, selection, playhead and loop playback (Space = play/stop).
- **Edit:** Trim, Cut, Silence, Normalize, Fade in / out, Reverse, Mono - with 30 levels of undo / redo.
- **Pitch & Time:** high-quality pitch shifting and time-stretching (Signalsmith Stretch) with formant
  preservation, old-school *Tape* varispeed, BPM detection and **MATCH BPM** to your DAW's tempo.
- **Tone:** gain, 24 dB/oct low cut / high cut.
- **Chop:** AUTO CHOP (transient detection with sensitivity), EQUAL slices, or alt-click to add markers by hand.
- **MIDI pads:** chop 1 plays on C3, chop 2 on C#3, and so on. Play them from your MIDI keyboard or the on-screen pads.
  One-shot or gated. **KEYS mode** plays the sample chromatically instead.
- **Drag to DAW:** drag the selection, the whole sample, or *any single pad* onto a track.
- **SAVE** writes a WAV into your sample library.

### 3. STEMS - separate vocals and music
- **Quick Split (built-in):** instant, no downloads. Vocals + Music, or Vocals / Drums / Bass / Other.
  Best on stereo mixes.
- **AI Split (Demucs, runs locally):** studio-quality separation - *Fast*, *Best*, or *6 stems* (adds guitar and piano).
- Solo / mute each stem, play the mix, open any stem in STUDIO to chop it, save it, or drag it into your DAW.
  **DRAG MIX** exports just the stems you left un-muted (e.g. drums + bass only).

### 4. LIBRARY
Your saved samples (a normal folder, default `Documents/Sample Snagger/Samples`). Click to audition,
double-click to load into the tray, drag rows into your DAW.

---

## Install

Grab the installer for your system from the **Releases** page (built automatically by GitHub Actions - see below):

- **Mac:** `Sample-Snagger-x.y.z-macOS.pkg` - installs the AU, VST3 and the app.
  The builds are not notarised by Apple yet, so the first time: right-click the .pkg -> **Open**.
  If a DAW refuses to load the plug-in, run once in Terminal:
  `sudo xattr -dr com.apple.quarantine "/Library/Audio/Plug-Ins/Components/Sample Snagger.component" "/Library/Audio/Plug-Ins/VST3/Sample Snagger.vst3" "/Applications/Sample Snagger.app"`
- **Windows:** `Sample-Snagger-x.y.z-Windows-Setup.exe` - installs the VST3 into `C:\Program Files\Common Files\VST3`
  and the standalone app. (SmartScreen may warn about an unknown publisher -> *More info* -> *Run anyway*.)
- **Linux:** unpack the `.tar.gz` and run `./install.sh`.

Then rescan plug-ins in your DAW. Sample Snagger is an **instrument** plug-in - put it on an instrument / MIDI track.

### First run: helper tools (one click)
Open **Settings** (gear icon, top right):

| Tool | What it's for | Size |
|---|---|---|
| **FFmpeg** | video files + exotic audio formats, needed for HQ SNAG | ~30 MB |
| **yt-dlp** | HQ SNAG from YouTube and 1,000+ sites | ~35 MB |
| **Deno** | helps yt-dlp read YouTube reliably | ~40 MB |
| **AI Stem Engine** | Demucs AI separation (needs Python 3.9-3.13 from python.org) | ~2-3 GB |

Click **INSTALL ALL** for the first three. They download from their official GitHub releases into
Sample Snagger's own folder - nothing else on your system is touched. LIVE REC, hindsight grabs,
Quick Split and everything in STUDIO work with no tools at all.

---

## Tips
- **YouTube asks you to sign in / age-restricted videos:** Settings -> *Use cookies from* -> pick the browser
  you're logged into. Or just use LIVE REC / hindsight.
- **HQ SNAG fails on YouTube after a while:** click **UPDATE** next to yt-dlp - sites change often and yt-dlp keeps up.
- **Record system audio from other apps (Spotify, a game...):** use the standalone app, choose a loopback
  input in Audio Settings (BlackHole on Mac, "Stereo Mix" or VB-Cable on Windows) and press REC INPUT.
- **Recording inside a DAW:** enable Sample Snagger's side-chain / audio input in your DAW and route a track into it,
  then REC INPUT. (Some DAWs, such as Ableton and FL Studio, don't feed audio into instrument plug-ins - use the
  browser capture, IMPORT or the standalone app there.)
- **Keyboard:** Space play/stop · Cmd/Ctrl+Z undo · Shift+Cmd/Ctrl+Z redo · Delete cuts the selection ·
  Cmd/Ctrl+1-4 switch tabs · Cmd/Ctrl+scroll zooms the waveform · double-click a chop selects it.

## Where things are stored
| | Mac | Windows |
|---|---|---|
| Sample library | `~/Documents/Sample Snagger/Samples` | `Documents\Sample Snagger\Samples` |
| Helper tools, session cache, settings | `~/Library/Application Support/Sample Snagger` | `%APPDATA%\Sample Snagger` |

Session clips are cached as WAVs so projects reopen with their samples. You can delete old folders in
`Sessions` whenever you like.

## Sampling responsibly
Sample Snagger captures audio you can already hear or have on disk. Clear any samples you use in released music,
and follow the terms of the sites you capture from.

## Building from source
See [BUILD.md](BUILD.md). Short version: push this folder to a GitHub repository and the included workflow
builds installers for macOS, Windows and Linux; or build locally with CMake.

## Credits & licences
- [JUCE](https://juce.com) 8 - audio plug-in framework (AGPLv3 or JUCE commercial licence; if you sell this, check JUCE's licence tiers)
- [Signalsmith Stretch](https://github.com/Signalsmith-Audio/signalsmith-stretch) - pitch / time (MIT)
- [yt-dlp](https://github.com/yt-dlp/yt-dlp) (Unlicense), [FFmpeg](https://ffmpeg.org) (LGPL/GPL, downloaded separately),
  [Deno](https://deno.com) (MIT), [Demucs](https://github.com/facebookresearch/demucs) (MIT) - installed on demand, not bundled
- Fonts: Cinzel and Montserrat (SIL Open Font License, see `Resources/Fonts`)
